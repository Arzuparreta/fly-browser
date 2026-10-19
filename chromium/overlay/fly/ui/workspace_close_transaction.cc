// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/workspace_close_transaction.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/unload_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/workspaces/workspace_service.h"
#include "ui/base/base_window.h"

namespace fly {
WorkspaceCloseTransaction::WorkspaceCloseTransaction(
    base::FilePath original_profile_path,
    base::RepeatingCallback<bool()> validate,
    base::OnceCallback<void(bool)> done)
    : path_(std::move(original_profile_path)),
      validate_(std::move(validate)),
      done_(std::move(done)) {}

WorkspaceCloseTransaction::~WorkspaceCloseTransaction() {
  weak_factory_.InvalidateWeakPtrs();
  subscriptions_.clear();
  if (phase_ != Phase::kComplete)
    ResetHandlers();
}

void WorkspaceCloseTransaction::Start() {
  CHECK(!lease_);
  if (!validate_.Run() || IsWorkspaceRetiring(path_)) {
    Finish(false);
    return;
  }
  lease_ = std::make_unique<ScopedWorkspaceRetirement>(path_);
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetProfile()->GetOriginalProfile()->GetPath() != path_ ||
            browser->IsDeleteScheduled())
          return true;
        browsers_.push_back(browser->GetWeakPtr());
        subscriptions_.push_back(browser->RegisterBrowserDidClose(
            base::BindRepeating(&WorkspaceCloseTransaction::OnClosed,
                                weak_factory_.GetWeakPtr())));
        subscriptions_.push_back(browser->RegisterBrowserCloseCancelled(
            base::BindRepeating(&WorkspaceCloseTransaction::OnCancelled,
                                weak_factory_.GetWeakPtr())));
        auto* tabs = browser->GetTabStripModel();
        for (int i = 0; i < tabs->count(); ++i)
          ResumeNativeTab(tabs->GetWebContentsAt(i));
        return true;
      });
  ScheduleAdvance();
}

void WorkspaceCloseTransaction::ScheduleAdvance() {
  if (phase_ == Phase::kComplete || advance_pending_)
    return;
  advance_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&WorkspaceCloseTransaction::Advance,
                                weak_factory_.GetWeakPtr()));
}

void WorkspaceCloseTransaction::Advance() {
  advance_pending_ = false;
  if (phase_ == Phase::kComplete)
    return;
  if (cancelled_ || !validate_.Run()) {
    Finish(false);
    return;
  }
  if (phase_ == Phase::kClosing) {
    if (std::ranges::all_of(browsers_, [](const auto& browser) {
          return !browser || browser->IsDeleteScheduled();
        }))
      Finish(true);
    return;
  }
  while (index_ < browsers_.size() &&
         (!browsers_[index_] || browsers_[index_]->IsDeleteScheduled()))
    ++index_;
  if (index_ == browsers_.size()) {
    if (phase_ == Phase::kWarnings) {
      phase_ = Phase::kBeforeUnload;
      index_ = 0;
      ScheduleAdvance();
      return;
    }
    // A new tab may have registered beforeunload while another window's native
    // confirmation was open. Rescan before starting irreversible native closes.
    for (size_t i = 0; i < browsers_.size(); ++i) {
      const auto& browser = browsers_[i];
      if (browser && !browser->IsDeleteScheduled() &&
          UnloadController::From(browser.get())->TabsNeedBeforeUnloadFired()) {
        index_ = i;
        ScheduleAdvance();
        return;
      }
    }
    phase_ = Phase::kClosing;
    // Snapshot weak identities before native close callbacks can reenter.
    const auto browsers = browsers_;
    const auto weak = weak_factory_.GetWeakPtr();
    for (const auto& browser : browsers) {
      if (browser && !browser->IsDeleteScheduled())
        browser->GetWindow()->Close();
      if (!weak)
        return;
      if (cancelled_)
        break;
    }
    ScheduleAdvance();
    return;
  }
  auto* unload = UnloadController::From(browsers_[index_].get());
  if (phase_ == Phase::kWarnings) {
    const auto result = unload->MaybeWarnBeforeClosing(base::BindOnce(
        [](base::WeakPtr<WorkspaceCloseTransaction> transaction, size_t index,
           UnloadController::WarnBeforeClosingResult result) {
          if (transaction)
            transaction->Confirmed(
                index,
                result ==
                    UnloadController::WarnBeforeClosingResult::kOkToClose);
        },
        weak_factory_.GetWeakPtr(), index_));
    if (result == UnloadController::WarnBeforeClosingResult::kDoNotClose)
      return;
  } else if (unload->TryToCloseWindow(
                 false,
                 base::BindRepeating(&WorkspaceCloseTransaction::Confirmed,
                                     weak_factory_.GetWeakPtr(), index_))) {
    return;
  }
  ++index_;
  ScheduleAdvance();
}

void WorkspaceCloseTransaction::Confirmed(size_t index, bool proceed) {
  if (phase_ == Phase::kComplete || phase_ == Phase::kClosing ||
      index != index_)
    return;
  if (!proceed) {
    cancelled_ = true;
    // Avoid destruction inside the native UnloadController callback stack.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceCloseTransaction::Finish,
                                  weak_factory_.GetWeakPtr(), false));
    return;
  }
  ++index_;
  ScheduleAdvance();
}

void WorkspaceCloseTransaction::OnClosed(BrowserWindowInterface*) {
  ScheduleAdvance();
}
void WorkspaceCloseTransaction::OnCancelled(
    BrowserWindowInterface*,
    BrowserWindowInterface::ClosingStatus status) {
  if (status == BrowserWindowInterface::ClosingStatus::kDeniedByUser ||
      status == BrowserWindowInterface::ClosingStatus::kDeniedByPolicy) {
    cancelled_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&WorkspaceCloseTransaction::Finish,
                                  weak_factory_.GetWeakPtr(), false));
  }
}
void WorkspaceCloseTransaction::ResetHandlers() {
  // Reset can notify native global-close observers. Invalidate our callbacks
  // first so cancellation cannot reenter the transaction during teardown.
  for (const auto& browser : browsers_)
    if (browser && !browser->IsDeleteScheduled())
      UnloadController::From(browser.get())->ResetTryToCloseWindow();
}
void WorkspaceCloseTransaction::Finish(bool success) {
  if (phase_ == Phase::kComplete)
    return;
  phase_ = Phase::kComplete;
  weak_factory_.InvalidateWeakPtrs();
  subscriptions_.clear();
  if (!success)
    ResetHandlers();
  // Keep the creation lease until the owner commits persistent retirement.
  // The callback may delete this coordinator; access no members afterwards.
  std::move(done_).Run(success);
}
}  // namespace fly
