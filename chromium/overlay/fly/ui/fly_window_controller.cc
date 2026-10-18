// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_window_controller.h"
#include "fly/commands/keyboard_router.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/status_bar.h"
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
#include "chrome/app/chrome_command_ids.h"
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
#include "ui/color/color_id.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/background.h"
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
      preferences_(*g_browser_process->local_state()),
      keep_alive_(
          std::make_unique<ScopedKeepAlive>(KeepAliveOrigin::FLY_WINDOW,
                                            KeepAliveRestartOption::DISABLED)),
      desktop_(std::make_unique<views::Widget>()) {
  SetTitle(u"Fly");
  SetCanResize(true);
  SetCanMinimize(true);
  SetCanMaximize(true);
  auto root = std::make_unique<views::View>();
  root->SetBackground(views::CreateSolidBackground(ui::kColorDialogBackground));
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
  clock_ = sidebar->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(&FlyWindowController::CycleClockFormat,
                          weak_factory_.GetWeakPtr()),
      u""));
  clock_->SetStyle(ui::ButtonStyle::kText);
  preferences_button_ = sidebar->AddChildView(std::make_unique<views::MdTextButton>(
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) {
            if (window && window->selected_) {
              InvokeNativeCommand(window->selected_, "toggle-help");
              window->keyboard_editor_->ShowAppearance();
            }
          }, weak_factory_.GetWeakPtr()), u"Preferences"));
  preferences_button_->SetEnabled(false);
  root->AddChildView(std::move(sidebar));
  auto* column = root->AddChildView(std::make_unique<views::View>());
  auto* column_layout =
      column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  content_ = column->AddChildView(std::make_unique<views::View>());
  content_->SetLayoutManager(std::make_unique<views::FillLayout>());
  column_layout->SetFlexForView(content_, 1);
  status_bar_ = column->AddChildView(std::make_unique<StatusBar>());
  status_ = status_bar_->mode_label();
  layout->SetFlexForView(column, 1);
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
  keyboard_editor_ =
      std::make_unique<KeyboardEditor>(desktop_.get(), commands_,
                                       *g_browser_process->local_state());
  desktop_->AddObserver(this);
  preferences_registrar_.Init(g_browser_process->local_state());
  preferences_registrar_.Add(
      kPreferencesPref,
      base::BindRepeating(&FlyWindowController::OnPreferencesChanged,
                          weak_factory_.GetWeakPtr()));
  OnPreferencesChanged();
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
  clock_timer_.Stop();
  preferences_registrar_.RemoveAll();
  keyboard_editor_.reset();
  palette_.reset();
  hints_.reset();
  desktop_->GetNativeWindow()->RemovePreTargetHandler(this);
  desktop_->RemoveObserver(this);
  // Destruction is only posted after all profile-bound BrowserWidgets finish
  // their own beforeunload and destruction paths.
  workspace_buttons_ = nullptr;
  content_ = nullptr;
  status_ = nullptr;
  status_bar_ = nullptr;
  clock_ = nullptr;
  preferences_button_ = nullptr;
  tab_sidebar_ = nullptr;
  desktop_.reset();
}

void FlyWindowController::OnPreferencesChanged() {
  const auto theme = preferences_.ReadTheme();
  if (theme && theme != applied_theme_) {
    applied_theme_ = theme;
    desktop_->SetColorModeOverride(
        theme->mode == "dark" ? ui::ColorProviderKey::ColorMode::kDark
                              : ui::ColorProviderKey::ColorMode::kLight);
    // Native WebContents observers recompute actual renderer preferences,
    // including hidden/split/private/extension pages. No web API is replaced.
    ui::NativeTheme::GetInstanceForWeb()->NotifyOnNativeThemeUpdated();
  }
  UpdateClock();
}

void FlyWindowController::UpdateClock() {
  if (status_bar_)
    status_bar_->SetVisible(
        preferences_.ReadStatusBarVisible().value_or(false));
  const auto settings = preferences_.ReadClock();
  const bool visible = settings && settings->visible;
  if (!visible && clock_->HasFocus() && selected_) {
    if (auto* contents = selected_->GetTabStripModel()->GetActiveWebContents())
      contents->Focus();
  }
  clock_->SetVisible(visible);
  clock_->SetEnabled(preferences_.CanEditClock());
  if (!visible || !desktop_->IsVisible()) {
    clock_timer_.Stop();
    return;
  }
  const auto text = FormatClock(base::Time::Now(), settings->format);
  if (clock_->GetText() != text.label)
    clock_->SetText(text.label);
  if (clock_->GetTooltipText() != text.tooltip)
    clock_->SetTooltipText(text.tooltip);
  if (!clock_timer_.IsRunning())
    clock_timer_.Start(FROM_HERE, base::Seconds(1),
                       base::BindRepeating(&FlyWindowController::UpdateClock,
                                           weak_factory_.GetWeakPtr()));
}
void FlyWindowController::CycleClockFormat() {
  preferences_.CycleClockFormat();
}
void FlyWindowController::OnWidgetVisibilityChanged(views::Widget*, bool) {
  UpdateClock();
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
    keyboard_editor_->Cancel();
    selected_ = browser;
    preferences_button_->SetEnabled(true);
    tab_sidebar_->SetBrowser(browser);
    auto& next = it->second;
    std::u16string workspace_name;
    if (const auto catalog = workspaces_.Read()) {
      auto item =
          std::ranges::find(catalog->items, next.workspace_id, &Workspace::id);
      if (item != catalog->items.end())
        workspace_name = base::UTF8ToUTF16(item->name);
    }
    status_bar_->SetBrowser(browser, std::move(workspace_name));
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
      std::u16string label = u"Recovery";
      if (browser->GetProfile()->IsOffTheRecord()) {
        label = u"Private workspace";
        const auto directory = browser->GetProfile()
                                   ->GetOriginalProfile()
                                   ->GetPath()
                                   .BaseName()
                                   .AsUTF8Unsafe();
        const auto item = std::ranges::find(catalog->items, directory,
                                            &Workspace::profile_directory);
        if (item != catalog->items.end())
          label = u"Private · " + base::UTF8ToUTF16(item->name);
      }
      workspace_buttons_->AddChildView(std::make_unique<views::MdTextButton>(
          base::BindRepeating(&FlyWindowController::SelectWindow,
                              weak_factory_.GetWeakPtr(),
                              browser->GetWeakPtr()),
          label));
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
  if (closing_ || creating_profile_ || !workspaces_.CanEdit()) {
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
  auto* closed_profile = browser->GetProfile();
  const auto return_it = private_returns_.find(closed_profile);
  const auto private_return = return_it == private_returns_.end()
                                  ? base::WeakPtr<BrowserWindowInterface>()
                                  : return_it->second;
  it->second.browser_closed = true;
  it->second.browser = nullptr;
  auto last = last_windows_.find(it->second.workspace_id);
  if (last != last_windows_.end() && last->second.get() == browser)
    last_windows_.erase(last);
  if (selected_ == browser) {
    hints_->Cancel();
    palette_->Cancel();
    keyboard_editor_->Cancel();
    tab_sidebar_->SetBrowser(nullptr);
    status_bar_->SetBrowser(nullptr, u"");
    selected_ = nullptr;
    preferences_button_->SetEnabled(false);
    if (it->second.widget) {
      it->second.host->SetVisible(false);
      it->second.widget->Hide();
    }
    if (!closing_) {
      auto choose = [&](bool same_workspace) {
        for (const auto& [next, entry] : entries_) {
          if (!entry.browser_closed && entry.widget &&
              (!same_workspace ||
               (entry.workspace_id == it->second.workspace_id &&
                (!entry.workspace_id.empty() ||
                 entry.browser->GetProfile() == closed_profile)))) {
            Select(next, true);
            return true;
          }
        }
        return false;
      };
      if (!choose(true)) {
        if (private_return && entries_.contains(private_return.get()) &&
            !entries_.at(private_return.get()).browser_closed)
          Select(private_return.get(), true);
        else
          choose(false);
      }
    }
    UpdateModeLabel();
  }
  if (closed_profile->IsOffTheRecord() &&
      std::ranges::none_of(entries_, [&](const auto& pair) {
        return !pair.second.browser_closed && pair.second.browser &&
               pair.second.browser->GetProfile() == closed_profile;
      })) {
    private_returns_.erase(closed_profile);
  }
  ScheduleWorkspaceRefresh();
}

bool FlyWindowController::CanTogglePrivate(
    BrowserWindowInterface* browser) const {
  return browser &&
         (browser->GetProfile()->IsOffTheRecord() ||
          chrome::IsCommandEnabled(browser, IDC_NEW_INCOGNITO_WINDOW));
}

void FlyWindowController::TogglePrivate(BrowserWindowInterface* browser) {
  if (!CanTogglePrivate(browser))
    return;
  auto* profile = browser->GetProfile();
  if (profile->IsOffTheRecord()) {
    // The native close coordinator resolves all beforeunload decisions before
    // closing this OTR profile's windows. Cancellation leaves them intact.
    chrome::CloseAllBrowsersWithIncognitoProfile(profile,
                                                 /*skip_beforeunload=*/false);
    return;
  }
  auto* private_profile = profile->GetPrimaryOTRProfile(true);
  private_returns_[private_profile] = browser->GetWeakPtr();
  for (const auto& [model, entry] : entries_) {
    if (!entry.browser_closed && entry.widget &&
        model->GetProfile() == private_profile) {
      Select(model, true);
      return;
    }
  }
  // Standard Chromium creation owns OTR lifetime, extension incognito access,
  // permissions, downloads and shutdown. No normal navigation/state is copied.
  chrome::NewEmptyWindow(private_profile,
                         /*should_trigger_session_restore=*/false);
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
  keyboard_editor_->Cancel();
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
    keyboard_editor_->Cancel();
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
    status_bar_->SetBrowser(nullptr, u"");
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
    keyboard_editor_->Cancel();
    palette_->Toggle(browser->GetTabStripModel()->GetActiveWebContents(),
                     commands_.Read().value_or(DefaultKeyboardConfig()));
    return true;
  }
  if (action == "toggle-private-mode") {
    TogglePrivate(browser);
    return true;
  }
  if (action == "trigger-hints") {
    keyboard_editor_->Cancel();
    hints_->Toggle(browser->GetTabStripModel()->GetActiveWebContents());
    return true;
  }
  if (action == "toggle-theme")
    return preferences_.ToggleTheme();
  if (action == "toggle-status-bar") {
    const auto visible = preferences_.ReadStatusBarVisible();
    return visible && preferences_.SetStatusBarVisible(!*visible);
  }
  if (action == "toggle-clock") {
    const auto clock = preferences_.ReadClock();
    return clock && preferences_.SetClockVisible(!clock->visible);
  }
  if (action == "toggle-help") {
    hints_->Cancel();
    palette_->Cancel();
    keyboard_editor_->Toggle(
        browser->GetTabStripModel()->GetActiveWebContents());
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
  if (keyboard_editor_->active()) {
    if (keyboard_editor_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
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
void FlyWindowController::UpdateTargetURL(BrowserWindowInterface* browser,
                                          content::WebContents* source,
                                          const GURL& url) {
  if (status_bar_ && selected_ == browser)
    status_bar_->SetTargetURL(source, url);
}
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
  const std::u16string mode =
      hints_ && hints_->active()
          ? (hints_->truncated() ? u"HINTS (target limit)" : u"HINTS")
          : NativePageModeLabel(selected_);
  status_->SetText(selected_ && selected_->GetProfile()->IsOffTheRecord()
                       ? u"PRIVATE · " + mode
                       : mode);
}
}  // namespace fly
