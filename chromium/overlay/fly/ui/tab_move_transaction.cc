// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/tab_move_transaction.h"
#include "fly/ui/new_tab_ui.h"

#include <utility>

#include "base/check.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "fly/sessions/tab_identity.h"
#include "fly/sessions/tab_move_gate.h"
#include "fly/ui/native_commands.h"
#include "ui/base/base_window.h"
#include "ui/base/page_transition_types.h"

namespace fly {
TabMoveTransaction::TabMoveTransaction(
    BrowserWindowInterface* source,
    content::WebContents* contents,
    base::FilePath destination_path,
    base::RepeatingCallback<bool(bool)> validate,
    Done done)
    : content::WebContentsObserver(contents),
      source_browser_(source->GetWeakPtr()),
      source_(contents->GetWeakPtr()),
      destination_path_(std::move(destination_path)),
      url_(contents->GetLastCommittedURL()),
      private_(source->GetProfile()->IsOffTheRecord()),
      validate_(std::move(validate)),
      done_(std::move(done)) {}

TabMoveTransaction::~TabMoveTransaction() {
  weak_factory_.InvalidateWeakPtrs();
  Observe(nullptr);
  TabStripModelObserver::StopObservingAll(this);
  gate_.reset();
  if (!committed_)
    CloseUnusedDestination();
}
bool TabMoveTransaction::CanMoveUrl(const GURL& url) {
  return url.is_valid() && !url.has_username() && !url.has_password() &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIsFile() ||
          url.SchemeIs("data") || url.SchemeIs("chrome") ||
          IsFlyNewTabPage(url));
}
bool TabMoveTransaction::SourceValid(bool require_selection) const {
  return !aborted_ && source_ && source_browser_ &&
         !source_browser_->IsDeleteScheduled() &&
         validate_.Run(require_selection) &&
         source_browser_->GetTabStripModel()->GetIndexOfWebContents(
             source_.get()) != TabStripModel::kNoTab &&
         !source_->GetController().GetPendingEntry() &&
         source_->GetLastCommittedURL() == url_;
}
bool TabMoveTransaction::DestinationValid() const {
  return destination_ && !destination_->IsDeleteScheduled() &&
         destination_->GetProfile()->GetOriginalProfile()->GetPath() ==
             destination_path_ &&
         destination_->GetProfile()->IsOffTheRecord() == private_ &&
         GetBrowserWindowCreationStatusForProfile(
             *destination_->GetProfile()) ==
             BrowserWindowInterface::CreationStatus::kOk;
}
void TabMoveTransaction::Start() {
  VLOG(1) << "Fly tab move: start";
  if (!CanMoveUrl(url_) || !SourceValid(true)) {
    ScheduleFinish(false);
    return;
  }
  source_browser_->GetTabStripModel()->AddObserver(this);
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (browser->GetType() == BrowserWindowInterface::TYPE_NORMAL &&
            browser->GetProfile()->GetOriginalProfile()->GetPath() ==
                destination_path_ &&
            browser->GetProfile()->IsOffTheRecord() == private_ &&
            !browser->IsDeleteScheduled()) {
          destination_ = browser->GetWeakPtr();
          return false;
        }
        return true;
      },
      BrowserCollection::Order::kActivation);
  if (DestinationValid()) {
    OnBrowserReady(destination_.get());
    return;
  }
  g_browser_process->profile_manager()->CreateProfileAsync(
      destination_path_, base::BindOnce(&TabMoveTransaction::OnProfileReady,
                                        weak_factory_.GetWeakPtr()));
}
void TabMoveTransaction::OnProfileReady(Profile* original) {
  VLOG(1) << "Fly tab move: profile ready; source=" << SourceValid(true);
  if (!SourceValid(true) || !original || original->IsOffTheRecord() ||
      original->GetPath() != destination_path_ ||
      !original->AllowsBrowserWindows()) {
    ScheduleFinish(false);
    return;
  }
  destination_keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
      original, ProfileKeepAliveOrigin::kChromeViewsDelegate);
  if (!destination_keep_alive_) {
    ScheduleFinish(false);
    return;
  }
  auto* profile = private_ ? original->GetPrimaryOTRProfile(true) : original;
  if (GetBrowserWindowCreationStatusForProfile(*profile) !=
      BrowserWindowInterface::CreationStatus::kOk) {
    ScheduleFinish(false);
    return;
  }
  BrowserWindowCreateParams params(*profile, true);
  params.should_trigger_session_restore = false;
  created_destination_ = true;
  CreateBrowserWindow(std::move(params),
                      base::BindOnce(
                          [](base::WeakPtr<TabMoveTransaction> move,
                             BrowserWindowInterface* browser) {
                            if (move) {
                              move->OnBrowserReady(browser);
                            } else if (browser &&
                                       !browser->IsDeleteScheduled() &&
                                       browser->GetTabStripModel()->empty()) {
                              browser->GetWindow()->Close();
                            }
                          },
                          weak_factory_.GetWeakPtr()));
}
void TabMoveTransaction::OnBrowserReady(BrowserWindowInterface* destination) {
  if (destination)
    destination_ = destination->GetWeakPtr();
  VLOG(1) << "Fly tab move: browser ready; source=" << SourceValid(true)
          << "; destination=" << DestinationValid();
  if (!SourceValid(true) || !DestinationValid()) {
    ScheduleFinish(false);
    return;
  }
  gate_ = std::make_unique<ScopedTabMoveGate>(
      source_.get(),
      base::BindRepeating(
          [](base::WeakPtr<TabMoveTransaction> move) {
            return move && move->ValidateClose();
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<TabMoveTransaction> move) {
            return move && move->Commit();
          },
          weak_factory_.GetWeakPtr()));
  close_requested_ = true;
  ResumeNativeTab(source_.get());
  source_browser_->GetTabStripModel()->CloseWebContents(
      source_.get(), TabCloseTypes::CLOSE_USER_GESTURE);
}
bool TabMoveTransaction::ValidateClose() {
  VLOG(1) << "Fly tab move: close gate; source=" << SourceValid(false)
          << "; destination=" << DestinationValid();
  if (!SourceValid(false) || !DestinationValid()) {
    aborted_ = true;
    ScheduleFinish(false);
    return false;
  }
  return true;
}
bool TabMoveTransaction::Commit() {
  VLOG(1) << "Fly tab move: committing";
  if (committed_)
    return true;
  if (!ValidateClose())
    return false;
  auto* source = source_.get();
  auto* strip = source_browser_->GetTabStripModel();
  const int source_index = strip->GetIndexOfWebContents(source);
  const bool pinned = strip->IsTabPinned(source_index);
  const bool muted = source->IsAudioMuted();
  const auto title = source->GetTitle();
  auto* target = destination_->GetTabStripModel();
  auto fresh = content::WebContents::Create(
      content::WebContents::CreateParams(destination_->GetProfile()));
  if (!TabIdentity::PrepareProfileMove(source, fresh.get())) {
    VLOG(1) << "Fly tab move: identity handoff rejected";
    aborted_ = true;
    ScheduleFinish(false);
    return false;
  }
  moved_ = fresh->GetWeakPtr();
  const int index = target->InsertWebContentsAt(
      pinned ? target->IndexOfFirstNonPinnedTab() : target->count(),
      std::move(fresh),
      pinned ? AddTabTypes::ADD_PINNED : AddTabTypes::ADD_NONE);
  CHECK_NE(index, TabStripModel::kNoTab);
  CHECK(moved_ && moved_->GetBrowserContext() == destination_->GetProfile());
  committed_ = true;
  VLOG(1) << "Fly tab move: destination inserted";
  SetTabAudioMuted(moved_.get(), muted, TabMutedReason::kAudioIndicator, {});
  content::NavigationController::LoadURLParams params(url_);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  moved_->GetController().LoadURLWithParams(params);
  if (auto* entry = moved_->GetController().GetPendingEntry();
      entry && !title.empty())
    moved_->UpdateTitleForEntry(entry, title);
  return true;
}
void TabMoveTransaction::BeforeUnloadFired(bool proceed) {
  if (!proceed)
    ScheduleFinish(false);
}
void TabMoveTransaction::BeforeUnloadDialogCancelled() {
  ScheduleFinish(false);
}
void TabMoveTransaction::DidStartNavigation(content::NavigationHandle* handle) {
  if (!committed_ && handle->IsInPrimaryMainFrame()) {
    aborted_ = true;
    // Preserve a pending native close gate until its decision rejects closing.
    if (!close_requested_)
      ScheduleFinish(false);
  }
}
void TabMoveTransaction::WebContentsDestroyed() {
  ScheduleFinish(committed_);
}
void TabMoveTransaction::OnTabCloseCancelled(const tabs::TabInterface* tab) {
  if (source_ && tab->GetContents() == source_.get())
    ScheduleFinish(false);
}
void TabMoveTransaction::OnTabStripModelDestroyed(TabStripModel*) {
  if (!committed_)
    ScheduleFinish(false);
}
void TabMoveTransaction::CloseUnusedDestination() {
  if (created_destination_ && destination_ &&
      !destination_->IsDeleteScheduled() &&
      destination_->GetTabStripModel()->empty())
    destination_->GetWindow()->Close();
}
void TabMoveTransaction::ScheduleFinish(bool success) {
  if (finish_pending_)
    return;
  finish_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&TabMoveTransaction::Finish,
                                weak_factory_.GetWeakPtr(), success));
}
void TabMoveTransaction::Finish(bool success) {
  VLOG(1) << "Fly tab move: finished; success=" << success
          << "; committed=" << committed_;
  if (!committed_)
    CloseUnusedDestination();
  std::move(done_).Run(success, destination_, moved_);
}
}  // namespace fly
