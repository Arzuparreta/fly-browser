// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_window_controller.h"
#include "fly/commands/keyboard_router.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/tab_sidebar.h"

#include <algorithm>
#include <utility>
#include <vector>

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
#include "components/input/native_web_keyboard_event.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "ui/aura/window.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/focus/focus_manager.h"
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

bool FlyWindowController::ShouldEmbed(const BrowserWindowInterface* browser) {
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
      commands_(*g_browser_process->local_state()),
      keep_alive_(
          std::make_unique<ScopedKeepAlive>(KeepAliveOrigin::FLY_WINDOW,
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
  auto* sidebar_layout =
      sidebar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 6));
  sidebar->SetPreferredSize(gfx::Size(280, 0));
  sidebar->AddChildView(std::make_unique<views::Label>(u"Fly"));
  sidebar->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&FlyWindowController::CreateWorkspace,
                          weak_factory_.GetWeakPtr()),
      u"New workspace"));
  workspace_buttons_ = sidebar->AddChildView(std::make_unique<views::View>());
  workspace_buttons_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  tab_sidebar_ =
      sidebar->AddChildView(std::make_unique<TabSidebar>(base::BindRepeating(
          &FlyWindowController::CreateTab, weak_factory_.GetWeakPtr())));
  sidebar_layout->SetFlexForView(tab_sidebar_, 1);
  status_ = sidebar->AddChildView(std::make_unique<views::Label>(u"NORMAL"));
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
  hints_ = std::make_unique<HintController>(
      desktop_.get(), base::BindRepeating(
                          [](base::WeakPtr<FlyWindowController> window, bool) {
                            if (window)
                              window->UpdateModeLabel();
                          },
                          weak_factory_.GetWeakPtr()));
  palette_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            if (!window || !window->selected_ || window->closing_)
              return false;
            return action == "trigger-hints" || action == "focus-sidebar" ||
                   action == "new-workspace" || action == "next-workspace" ||
                   action == "previous-workspace" ||
                   NativeCommandAvailable(window->selected_, action);
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             content::WebContents* source, std::string action) {
            if (window && window->selected_ &&
                window->selected_->GetTabStripModel()->GetActiveWebContents() ==
                    source)
              InvokeNativeCommand(window->selected_, std::move(action));
          },
          weak_factory_.GetWeakPtr()));
  desktop_->AddObserver(this);
  desktop_->GetNativeWindow()->AddPreTargetHandler(this);
  ArmCloseCallback();
  close_subscription_ = chrome::AddClosingAllBrowsersCallback(
      base::BindRepeating(&FlyWindowController::OnClosingAllBrowsers,
                          weak_factory_.GetWeakPtr()));
  ScheduleWorkspaceRefresh();
}

FlyWindowController::~FlyWindowController() {
  CHECK(entries_.empty());
  weak_factory_.InvalidateWeakPtrs();
  palette_.reset();
  hints_.reset();
  desktop_->GetNativeWindow()->RemovePreTargetHandler(this);
  desktop_->RemoveObserver(this);
  // Destruction is only posted after all profile-bound BrowserWidgets finish
  // their own beforeunload and destruction paths.
  workspace_buttons_ = nullptr;
  content_ = nullptr;
  status_ = nullptr;
  tab_sidebar_ = nullptr;
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
  auto [entry, inserted] =
      entries_.emplace(browser, Entry{browser, nullptr, host, workspace_id});
  entry->second.order = next_window_order_++;
  entry->second.closed_subscription =
      browser->RegisterBrowserDidClose(base::BindRepeating(
          &FlyWindowController::OnBrowserDidClose, weak_factory_.GetWeakPtr()));
  ScheduleWorkspaceRefresh();
}

void FlyWindowController::Attach(BrowserWindowInterface* browser,
                                 views::Widget* child) {
  auto& entry = entries_.at(browser);
  entry.widget = child;
  child->AddObserver(this);
  entry.host->Attach(child->GetNativeView());
  ScheduleWorkspaceRefresh();
}

bool FlyWindowController::IsSelected(
    const BrowserWindowInterface* browser) const {
  return selected_ == browser;
}

void FlyWindowController::Select(BrowserWindowInterface* browser, bool focus) {
  auto it = entries_.find(browser);
  if (it == entries_.end() || it->second.browser_closed || !it->second.widget) {
    return;
  }
  if (selected_ != browser) {
    if (selected_) {
      auto& previous = entries_.at(selected_);
      if (auto* contents =
              selected_->GetTabStripModel()->GetActiveWebContents())
        contents->StoreFocus();
      previous.widget->OnNativeWidgetActivationChanged(false);
      previous.host->SetVisible(false);
      previous.widget->Hide();
    }
    hints_->Cancel();
    palette_->Cancel();
    selected_ = browser;
    tab_sidebar_->SetBrowser(browser);
    auto& next = it->second;
    next.host->SetVisible(true);
    next.widget->ShowInactive();
    if (!next.workspace_id.empty()) {
      workspaces_.Activate(next.workspace_id);
      last_windows_[next.workspace_id] = browser->GetWeakPtr();
    }
    next.widget->OnNativeWidgetActivationChanged(desktop_->IsActive());
  }
  ScheduleWorkspaceRefresh();
  UpdateModeLabel();
  desktop_->Show();
  if (focus) {
    desktop_->Activate();
    if (auto* contents = browser->GetTabStripModel()->GetActiveWebContents()) {
      auto* view = contents->GetRenderWidgetHostView();
      if (!view || !view->HasFocus())
        contents->RestoreFocus();
    }
  }
}

void FlyWindowController::ScheduleWorkspaceRefresh() {
  if (workspace_refresh_pending_)
    return;
  workspace_refresh_pending_ = true;
  // Profile loading and browser close may finish synchronously inside a button
  // callback. Never destroy that originating button on its own dispatch stack.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE, base::BindOnce(&FlyWindowController::RebuildWorkspaceButtons,
                                weak_factory_.GetWeakPtr()));
}

void FlyWindowController::SelectWindow(
    base::WeakPtr<BrowserWindowInterface> browser) {
  if (browser)
    Select(browser.get(), true);
}

void FlyWindowController::RebuildWorkspaceButtons() {
  workspace_refresh_pending_ = false;
  workspace_buttons_->RemoveAllChildViews();
  const auto catalog = workspaces_.Read();
  if (!catalog) {
    workspace_buttons_->AddChildView(
        std::make_unique<views::Label>(u"Workspace data needs recovery"));
    return;
  }
  for (const auto& item : catalog->items) {
    auto* button =
        workspace_buttons_->AddChildView(std::make_unique<views::MdTextButton>(
            base::BindRepeating(&FlyWindowController::OpenWorkspace,
                                weak_factory_.GetWeakPtr(), item.id),
            base::UTF8ToUTF16(item.name)));
    const bool active =
        selected_ && entries_.at(selected_).workspace_id == item.id;
    button->SetStyle(active ? ui::ButtonStyle::kTonal : ui::ButtonStyle::kText);
    std::vector<const Entry*> windows;
    for (const auto& [browser, entry] : entries_)
      if (!entry.browser_closed && entry.widget &&
          entry.workspace_id == item.id)
        windows.push_back(&entry);
    std::ranges::sort(windows, {}, &Entry::order);
    if (windows.size() < 2)
      continue;
    for (size_t i = 0; i < windows.size(); ++i) {
      auto* browser = windows[i]->browser.get();
      auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
      const auto title = contents && !contents->GetTitle().empty()
                             ? contents->GetTitle()
                             : u"New tab";
      const auto text =
          u"Window " + base::NumberToString16(i + 1) + u" · " + title;
      auto* choice = workspace_buttons_->AddChildView(
          std::make_unique<views::MdTextButton>(
              base::BindRepeating(&FlyWindowController::SelectWindow,
                                  weak_factory_.GetWeakPtr(),
                                  browser->GetWeakPtr()),
              text));
      choice->SetStyle(selected_ == browser ? ui::ButtonStyle::kTonal
                                            : ui::ButtonStyle::kText);
      choice->GetViewAccessibility().SetName(base::UTF8ToUTF16(item.name) +
                                             u", " + text);
    }
  }
  for (const auto& [browser, entry] : entries_) {
    if (!entry.browser_closed && entry.widget && entry.workspace_id.empty()) {
      workspace_buttons_->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&FlyWindowController::SelectWindow,
                              weak_factory_.GetWeakPtr(),
                              browser->GetWeakPtr()),
          browser->GetProfile()->IsOffTheRecord() ? u"Private" : u"Recovery"));
    }
  }
}

void FlyWindowController::OpenWorkspace(std::string id) {
  if (closing_ || creating_profile_) {
    return;
  }
  if (auto last = last_windows_.find(id); last != last_windows_.end()) {
    if (last->second) {
      const auto entry = entries_.find(last->second.get());
      if (entry != entries_.end() && !entry->second.browser_closed &&
          entry->second.widget) {
        Select(last->second.get(), true);
        return;
      }
    }
    last_windows_.erase(last);
  }
  for (const auto& [browser, entry] : entries_) {
    if (!entry.browser_closed && entry.widget && entry.workspace_id == id) {
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

void FlyWindowController::CreateTab() {
  if (closing_)
    return;
  if (selected_) {
    chrome::NewTab(selected_, NewTabTypes::kNewTabCommand);
    return;
  }
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  if (catalog->items.empty())
    CreateWorkspace();
  else
    OpenWorkspace(catalog->active_id);
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
  ScheduleWorkspaceRefresh();
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

void FlyWindowController::OnBrowserDidClose(BrowserWindowInterface* browser) {
  auto it = entries_.find(browser);
  if (it == entries_.end())
    return;
  // This fires before the browser/tab-model teardown. The Widget observer
  // releases the native host later, without retaining a dead BWI meanwhile.
  it->second.browser_closed = true;
  it->second.browser = nullptr;
  auto last = last_windows_.find(it->second.workspace_id);
  if (last != last_windows_.end() && last->second.get() == browser)
    last_windows_.erase(last);
  if (selected_ == browser) {
    hints_->Cancel();
    palette_->Cancel();
    tab_sidebar_->SetBrowser(nullptr);
    selected_ = nullptr;
    if (it->second.widget) {
      it->second.host->SetVisible(false);
      it->second.widget->Hide();
    }
    if (!closing_) {
      auto choose = [&](bool same_workspace) {
        for (const auto& [next, entry] : entries_) {
          if (!entry.browser_closed && entry.widget &&
              (!same_workspace ||
               entry.workspace_id == it->second.workspace_id)) {
            Select(next, true);
            return true;
          }
        }
        return false;
      };
      if (!choose(true))
        choose(false);
    }
    UpdateModeLabel();
  }
  ScheduleWorkspaceRefresh();
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
  hints_->Cancel();
  palette_->Cancel();
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
  if (closing) {
    hints_->Cancel();
    palette_->Cancel();
  }
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
  if (was_selected) {
    tab_sidebar_->SetBrowser(nullptr);
  }
  it->second.host->Detach();
  auto removed_host = content_->RemoveChildViewT(it->second.host.get());
  entries_.erase(it);
  ScheduleWorkspaceRefresh();
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

namespace fly {
bool FlyWindowController::RunCommand(BrowserWindowInterface* browser,
                                     std::string_view action) {
  if (!entries_.contains(browser) || closing_)
    return false;
  if (action == "toggle-palette") {
    hints_->Cancel();
    palette_->Toggle(browser->GetTabStripModel()->GetActiveWebContents(),
                     commands_.Read().value_or(DefaultKeyboardConfig()));
    return true;
  }
  if (action == "trigger-hints") {
    hints_->Toggle(browser->GetTabStripModel()->GetActiveWebContents());
    return true;
  }
  if (action == "new-workspace") {
    CreateWorkspace();
    return true;
  }
  if (action == "focus-sidebar") {
    tab_sidebar_->FocusActiveTab();
    return true;
  }
  if (action == "next-workspace" || action == "previous-workspace") {
    const auto catalog = workspaces_.Read();
    if (!catalog || catalog->items.empty())
      return false;
    auto current = std::ranges::find(
        catalog->items, entries_.at(browser).workspace_id, &Workspace::id);
    size_t index =
        current == catalog->items.end() ? 0 : current - catalog->items.begin();
    const size_t count = catalog->items.size();
    index = action == "next-workspace" ? (index + 1) % count
                                       : (index + count - 1) % count;
    OpenWorkspace(catalog->items[index].id);
    return true;
  }
  const bool handled = ExecuteNativeCommand(browser, action);
  if (!handled) {
    status_->SetText(base::UTF8ToUTF16(ActionLabel(action) + ": unavailable"));
  }
  return handled;
}
}  // namespace fly

namespace fly {
void FlyWindowController::OnKeyEvent(ui::KeyEvent* event) {
  if (event->type() != ui::EventType::kKeyPressed || event->is_char())
    return;
  if (palette_->active()) {
    if (palette_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (!selected_) {
    if (closing_)
      return;
    if (!empty_router_ || empty_generation_ != commands_.generation()) {
      empty_router_ = std::make_unique<KeyboardRouter>(
          commands_.Read().value_or(DefaultKeyboardConfig()));
      empty_generation_ = commands_.generation();
    }
    const input::NativeWebKeyboardEvent native(*event);
    auto* input = desktop_->GetInputMethod();
    auto* client = input ? input->GetTextInputClient() : nullptr;
    const auto key = empty_router_->Route(
        {.stroke = NativeEventStroke(native),
         .time = base::TimeTicks::Now(),
         .repeat = event->is_repeat(),
         .composing =
             native.skip_if_unhandled ||
             bool(native.GetModifiers() & (blink::WebInputEvent::kIsComposing |
                                           blink::WebInputEvent::kAltGrKey)),
         .editable =
             client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE});
    UpdateModeLabel();
    if (key.kind == KeyResultKind::kPass)
      return;
    if (key.kind == KeyResultKind::kAction) {
      const auto catalog = workspaces_.Read();
      if (key.action == "toggle-passthrough" || key.action == "enter-insert") {
        UpdateModeLabel();
      } else if (key.action == "new-workspace") {
        CreateWorkspace();
      } else if (key.action == "spawn-tab") {
        CreateTab();
      } else if ((key.action == "next-workspace" ||
                  key.action == "previous-workspace") &&
                 catalog && !catalog->items.empty()) {
        auto current = std::ranges::find(catalog->items, catalog->active_id,
                                         &Workspace::id);
        const size_t count = catalog->items.size();
        const size_t index = current == catalog->items.end()
                                 ? 0
                                 : current - catalog->items.begin();
        OpenWorkspace(catalog
                          ->items[key.action == "next-workspace"
                                      ? (index + 1) % count
                                      : (index + count - 1) % count]
                          .id);
      } else {
        status_->SetText(
            base::UTF8ToUTF16(ActionLabel(key.action) + ": unavailable"));
      }
    }
    event->SetHandled();
    event->StopPropagation();
    return;
  }
  auto* contents = selected_->GetTabStripModel()->GetActiveWebContents();
  // Page events use BrowserView::PreHandleKeyboardEvent after native IME
  // processing. Do not dispatch them a second time at the desktop ancestor.
  if (!contents || (contents->GetRenderWidgetHostView() &&
                    contents->GetRenderWidgetHostView()->HasFocus()))
    return;
  auto* input = desktop_->GetInputMethod();
  auto* client = input ? input->GetTextInputClient() : nullptr;
  bool editable =
      client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE;
  const input::NativeWebKeyboardEvent native(*event);
  const auto handled = RouteNativePageKey(selected_, native, editable);
  if (handled == content::KeyboardEventProcessingResult::HANDLED) {
    event->SetHandled();
    event->StopPropagation();
  }
}
}  // namespace fly

namespace fly {
void FlyWindowController::UpdateModeLabel() {
  if (!status_)
    return;
  if (!selected_ && empty_router_) {
    status_->SetText(
        empty_router_->mode() == KeyboardMode::kPassthrough ? u"PASSTHROUGH"
        : empty_router_->mode() == KeyboardMode::kInsert    ? u"INSERT"
                                                            : u"NORMAL");
    return;
  }
  status_->SetText(
      hints_ && hints_->active()
          ? (hints_->truncated() ? u"HINTS (target limit)" : u"HINTS")
          : NativePageModeLabel(selected_));
}
}  // namespace fly
