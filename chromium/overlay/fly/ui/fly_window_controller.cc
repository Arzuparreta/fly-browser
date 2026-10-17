// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_window_controller.h"

#include <algorithm>
#include <utility>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"

namespace fly {
namespace {
std::unique_ptr<FlyWindowController>& Instance() {
  static base::NoDestructor<std::unique_ptr<FlyWindowController>> instance;
  return *instance;
}
}  // namespace

bool FlyWindowController::ShouldEmbed(BrowserWindowInterface* browser) {
  // Explicit development gate until migration and the acceptance suite pass.
  return base::CommandLine::ForCurrentProcess()->HasSwitch("fly-native") &&
         browser->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL;
}

FlyWindowController& FlyWindowController::Get() {
  if (!Instance()) {
    Instance() = std::make_unique<FlyWindowController>();
  }
  return *Instance();
}
FlyWindowController* FlyWindowController::GetIfCreated() {
  return Instance().get();
}

FlyWindowController::FlyWindowController()
    : workspaces_(*g_browser_process->local_state()),
      keep_alive_(
          std::make_unique<ScopedKeepAlive>(KeepAliveOrigin::BROWSER,
                                            KeepAliveRestartOption::DISABLED)),
      desktop_(std::make_unique<views::Widget>()) {
  SetTitle(u"Fly");
  SetCanResize(true);
  SetCanMinimize(true);
  SetCanMaximize(true);
  auto root = std::make_unique<views::View>();
  auto* layout = root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto sidebar = std::make_unique<views::View>();
  sidebar->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 6));
  sidebar->SetPreferredSize(gfx::Size(180, 0));
  sidebar->AddChildView(std::make_unique<views::Label>(u"Fly"));
  sidebar->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&FlyWindowController::CreateWorkspace,
                          weak_factory_.GetWeakPtr()),
      u"New workspace"));
  workspace_buttons_ = sidebar->AddChildView(std::make_unique<views::View>());
  workspace_buttons_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  root->AddChildView(std::move(sidebar));
  content_ = root->AddChildView(std::make_unique<views::View>());
  content_->SetLayoutManager(std::make_unique<views::FillLayout>());
  layout->SetFlexForView(content_, 1);
  SetContentsView(std::move(root));
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  params.delegate = this;
  params.name = "FlyDesktopWindow";
  params.bounds = gfx::Rect(80, 80, 1200, 800);
  desktop_->Init(std::move(params));
  desktop_->AddObserver(this);
  ArmCloseCallback();
  close_subscription_ = chrome::AddClosingAllBrowsersCallback(
      base::BindRepeating(&FlyWindowController::OnClosingAllBrowsers,
                          weak_factory_.GetWeakPtr()));
  RebuildWorkspaceButtons();
}

FlyWindowController::~FlyWindowController() {
  CHECK(entries_.empty());
  desktop_->RemoveObserver(this);
  // Destruction is only posted after all profile-bound BrowserWidgets finish
  // their own beforeunload and destruction paths.
  desktop_.reset();
}

void FlyWindowController::Prepare(BrowserWindowInterface* browser) {
  CHECK(!entries_.contains(browser));
  const auto directory =
      browser->GetProfile()->GetPath().BaseName().AsUTF8Unsafe();
  std::string workspace_id;
  if (!browser->GetProfile()->IsOffTheRecord()) {
    auto catalog = workspaces_.Read();
    if (catalog) {
      auto item = std::ranges::find(catalog->items, directory,
                                    &Workspace::profile_directory);
      if (item != catalog->items.end()) {
        workspace_id = item->id;
      } else {
        // Incoming native startup/extension windows keep their real Profile.
        // Never bind a new display name to an already retired directory.
        auto added = workspaces_.Add(
            "Workspace " + base::NumberToString(catalog->items.size() + 1),
            directory);
        if (added) {
          workspace_id = added->id;
        }
      }
    }
  }
  auto* host =
      content_->AddChildView(std::make_unique<views::NativeViewHost>());
  host->SetVisible(false);
  entries_.emplace(browser, Entry{browser, nullptr, host, workspace_id});
  RebuildWorkspaceButtons();
}

void FlyWindowController::Attach(BrowserWindowInterface* browser,
                                 views::Widget* child) {
  auto& entry = entries_.at(browser);
  entry.widget = child;
  child->AddObserver(this);
  entry.host->Attach(child->GetNativeView());
}

bool FlyWindowController::IsSelected(
    const BrowserWindowInterface* browser) const {
  return selected_ == browser;
}

void FlyWindowController::Select(BrowserWindowInterface* browser, bool focus) {
  auto it = entries_.find(browser);
  if (it == entries_.end() || !it->second.widget || closing_) {
    return;
  }
  if (selected_ != browser) {
    if (selected_) {
      auto& previous = entries_.at(selected_);
      previous.widget->OnNativeWidgetActivationChanged(false);
      previous.host->SetVisible(false);
      previous.widget->Hide();
    }
    selected_ = browser;
    auto& next = it->second;
    next.host->SetVisible(true);
    next.widget->ShowInactive();
    if (!next.workspace_id.empty()) {
      workspaces_.Activate(next.workspace_id);
    }
    next.widget->OnNativeWidgetActivationChanged(desktop_->IsActive());
  }
  desktop_->Show();
  if (focus) {
    desktop_->Activate();
    if (auto* contents = browser->GetTabStripModel()->GetActiveWebContents()) {
      contents->Focus();
    }
  }
}

void FlyWindowController::RebuildWorkspaceButtons() {
  workspace_buttons_->RemoveAllChildViews();
  const auto catalog = workspaces_.Read();
  if (!catalog) {
    workspace_buttons_->AddChildView(
        std::make_unique<views::Label>(u"Workspace data needs recovery"));
    return;
  }
  for (const auto& item : catalog->items) {
    workspace_buttons_->AddChildView(std::make_unique<views::MdTextButton>(
        base::BindRepeating(&FlyWindowController::OpenWorkspace,
                            weak_factory_.GetWeakPtr(), item.id),
        base::UTF8ToUTF16(item.name)));
  }
  for (const auto& [browser, entry] : entries_) {
    if (entry.workspace_id.empty()) {
      workspace_buttons_->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&FlyWindowController::Select,
                              weak_factory_.GetWeakPtr(), browser, true),
          browser->GetProfile()->IsOffTheRecord() ? u"Private" : u"Recovery"));
    }
  }
}

void FlyWindowController::OpenWorkspace(std::string id) {
  if (closing_ || creating_profile_) {
    return;
  }
  for (const auto& [browser, entry] : entries_) {
    if (entry.workspace_id == id) {
      Select(browser, true);
      return;
    }
  }
  const auto catalog = workspaces_.Read();
  if (!catalog) {
    return;
  }
  auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end()) {
    return;
  }
  auto* manager = g_browser_process->profile_manager();
  const auto path =
      manager->user_data_dir().AppendASCII(item->profile_directory);
  creating_profile_ = true;
  manager->CreateProfileAsync(
      path, base::BindOnce(&FlyWindowController::OnProfileReady,
                           weak_factory_.GetWeakPtr(), std::move(id)));
}

void FlyWindowController::CreateWorkspace() {
  if (closing_ || creating_profile_) {
    return;
  }
  const auto catalog = workspaces_.Read();
  if (!catalog) {
    return;
  }
  auto* manager = g_browser_process->profile_manager();
  const auto path = manager->GenerateNextProfileDirectoryPath();
  // Reserve a unique directory before asynchronous creation. A failed load
  // retains the entry for retry; it must not silently attach another Profile.
  std::string name;
  size_t number = catalog->items.size() + 1;
  do {
    name = "Workspace " + base::NumberToString(number++);
  } while (std::ranges::any_of(catalog->items, [&](const Workspace& item) {
    return item.name == name;
  }));
  const auto added = workspaces_.Add(name, path.BaseName().AsUTF8Unsafe());
  if (!added) {
    return;
  }
  RebuildWorkspaceButtons();
  OpenWorkspace(added->id);
}

void FlyWindowController::OnProfileReady(std::string id, Profile* profile) {
  creating_profile_ = false;
  if (!profile || closing_) {
    return;
  }
  BrowserWindowCreateParams params(*profile, true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params),
                      base::BindOnce(
                          [](base::WeakPtr<FlyWindowController> window,
                             BrowserWindowInterface* browser) {
                            if (!window || !browser) {
                              return;
                            }
                            chrome::NewTab(browser,
                                           NewTabTypes::kNewTabCommand);
                            window->Select(browser, true);
                          },
                          weak_factory_.GetWeakPtr()));
}

void FlyWindowController::ArmCloseCallback() {
  desktop_->MakeCloseSynchronous(base::BindOnce(
      &FlyWindowController::RequestClose, weak_factory_.GetWeakPtr()));
}
void FlyWindowController::RequestClose(views::Widget::ClosedReason reason) {
  ArmCloseCallback();
  if (closing_) {
    return;
  }
  closing_ = true;
  if (entries_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyWindowController::FinishClose,
                                  weak_factory_.GetWeakPtr()));
  } else {
    chrome::CloseAllBrowsers();
  }
}
void FlyWindowController::OnClosingAllBrowsers(bool closing) {
  closing_ = closing;
  if (closing && entries_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyWindowController::FinishClose,
                                  weak_factory_.GetWeakPtr()));
  }
}
void FlyWindowController::FinishClose() {
  if (closing_ && entries_.empty()) {
    Instance().reset();
    chrome::ShutdownIfNeeded();
  }
}

void FlyWindowController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == desktop_.get()) {
    return;
  }
  auto it = std::ranges::find_if(
      entries_, [&](const auto& pair) { return pair.second.widget == widget; });
  if (it == entries_.end()) {
    return;
  }
  widget->RemoveObserver(this);
  const bool was_selected = selected_ == it->first;
  it->second.host->Detach();
  content_->RemoveChildViewT(it->second.host.get());
  entries_.erase(it);
  RebuildWorkspaceButtons();
  if (was_selected) {
    selected_ = nullptr;
    if (!closing_ && !entries_.empty()) {
      Select(entries_.begin()->first, true);
    }
  }
  if (closing_ && entries_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyWindowController::FinishClose,
                                  weak_factory_.GetWeakPtr()));
  }
}
void FlyWindowController::OnWidgetActivationChanged(views::Widget* widget,
                                                    bool active) {
  if (widget == desktop_.get() && selected_) {
    auto* child = entries_.at(selected_).widget.get();
    child->OnNativeWidgetActivationChanged(active);
  }
}
}  // namespace fly
