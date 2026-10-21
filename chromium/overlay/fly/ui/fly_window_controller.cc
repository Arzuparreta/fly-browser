// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/new_tab_ui.h"

#include "build/build_config.h"
#include "base/strings/string_number_conversions.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/commands/calculator.h"
#include "fly/commands/keyboard_router.h"
#include "fly/preferences/search_service.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_actions.h"
#if BUILDFLAG(IS_LINUX)
#include "chrome/browser/shell_integration_linux.h"
#endif
#include "fly/ui/fly_extensions.h"
#include "fly/import/legacy_import.h"
#include "base/files/file_util.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/browser/history_service.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/common/url_constants.h"
#include "fly/ui/diagnostics_hub.h"
#include "fly/ui/site_zoom.h"
#include "fly/ui/user_content.h"
#include "fly/ui/welcome_view.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/text_utils.h"
#include "cc/paint/paint_flags.h"
#include "extensions/common/extension.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "skia/ext/image_operations.h"
#include "base/version.h"
#include "base/strings/stringprintf.h"
#include "base/base_paths.h"
#include "base/command_line.h"
#include "base/path_service.h"
#include "ui/gfx/image/image.h"
#include "base/containers/adapters.h"
#include "fly/fly_version.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/status_bar.h"
#include "fly/ui/tab_sidebar.h"
#include "fly/ui/sidebar_panel.h"
#include "ui/aura/window_targeter.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profiles_state.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/tab_move_transaction.h"
#include "fly/ui/workspace_close_transaction.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/window.h"
#include "ui/base/base_window.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/views/window/vector_icons/vector_icons.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
namespace {
// Electron's getWorkspaceBadgeText (webview.js): a trailing number of one or
// two digits after the first letter ("Workspace 1" is "W1"), the first two
// digits of a longer number, otherwise the first two letters in capitals.
std::u16string WorkspaceBadgeText(const std::u16string& name) {
  if (name.empty())
    return u"1";
  size_t start = name.size();
  while (start > 0 && base::IsAsciiDigit(name[start - 1]))
    --start;
  if (start < name.size()) {
    const auto number = name.substr(start);
    if (number.size() > 2)
      return number.substr(0, 2);
    if (name.size() <= 3)
      return name;
    std::u16string trimmed;
    base::TrimWhitespace(name, base::TRIM_LEADING, &trimmed);
    return base::ToUpperASCII(trimmed.substr(0, 1)) + number;
  }
  return base::ToUpperASCII(name.substr(0, 2));
}
}  // namespace

namespace {
class WorkspaceMenuButton : public FlyButton,
                            public views::ContextMenuController,
                            public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(WorkspaceMenuButton, FlyButton)
 public:
  WorkspaceMenuButton(PressedCallback select,
                      std::u16string_view name,
                      std::string id,
                      bool up,
                      bool down,
                      base::RepeatingCallback<void(int)> move,
                      bool editable,
                      base::RepeatingClosure rename,
                      bool removable,
                      base::RepeatingClosure remove)
      : FlyButton(std::move(select), name),
        id_(std::move(id)),
        up_(up),
        down_(down),
        move_(std::move(move)),
        editable_(editable),
        rename_(std::move(rename)),
        removable_(removable),
        remove_(std::move(remove)),
        model_(this) {
    set_context_menu_controller(this);
    model_.AddItem(1, u"Move workspace up");
    model_.AddItem(2, u"Move workspace down");
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
    model_.AddItem(3, u"Rename workspace");
    model_.AddItem(4, u"Remove workspace");
    runner_ = std::make_unique<views::MenuRunner>(
        &model_, views::MenuRunner::CONTEXT_MENU);
  }
  const std::string& workspace_id() const { return id_; }
  ui::SimpleMenuModel* running_model() {
    return runner_->IsRunning() ? &model_ : nullptr;
  }
  bool IsCommandIdEnabled(int command) const override {
    return command == 1   ? up_
           : command == 2 ? down_
           : command == 3 ? editable_
                          : command == 4 && removable_;
  }
  void ExecuteCommand(int command, int) override {
    if (!IsCommandIdEnabled(command))
      return;
    auto task = command == 4   ? base::BindOnce(remove_)
                : command == 3 ? base::BindOnce(rename_)
                               : base::BindOnce(move_, command == 1 ? -1 : 1);
    runner_->Cancel();
    // Reordering rebuilds this button. Complete menu dispatch first and only
    // then resolve the workspace's current stable identity in the catalog.
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
        FROM_HERE, std::move(task));
  }
  void ShowContextMenuForViewImpl(
      views::View*, const gfx::Point& point,
      ui::mojom::MenuSourceType source) override {
    runner_->RunMenuAt(GetWidget(), nullptr, gfx::Rect(point, gfx::Size()),
                       views::MenuAnchorPosition::kTopLeft, source);
  }
 private:
  const std::string id_;
  const bool up_;
  const bool down_;
  base::RepeatingCallback<void(int)> move_;
  const bool editable_;
  base::RepeatingClosure rename_;
  const bool removable_;
  base::RepeatingClosure remove_;
  ui::SimpleMenuModel model_;
  std::unique_ptr<views::MenuRunner> runner_;
};
BEGIN_METADATA(WorkspaceMenuButton)
END_METADATA

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
      bookmarks_(std::make_unique<BookmarkService>(*g_browser_process->local_state())),
      keep_alive_(
          std::make_unique<ScopedKeepAlive>(KeepAliveOrigin::FLY_WINDOW,
                                            KeepAliveRestartOption::DISABLED)),
      desktop_(std::make_unique<views::Widget>()) {
  frequent_sites_ =
      std::make_unique<FrequentSitesService>(*g_browser_process->local_state());
  RegisterFlyNewTabPage();
  SetTitle(u"Fly");
  SetCanResize(true);
  SetCanMinimize(true);
  SetCanMaximize(true);
  RegisterFlyFonts();
  auto root = std::make_unique<views::View>();
  root->SetBackground(FlyBackground(FlyRole::kMain));
  auto* layout = root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  auto command = [this](std::string action) {
    return base::BindRepeating(
        [](base::WeakPtr<FlyWindowController> window, std::string action) {
          if (window && window->selected_ && !window->closing_)
            InvokeNativeCommand(window->selected_, action);
        },
        weak_factory_.GetWeakPtr(), std::move(action));
  };
  // Electron's sidebar (index.html #Sidebar): one 260px panel whose left
  // 36px column is the collapsed rail.
  auto panel = std::make_unique<SidebarPanel>();
  auto* sidebar = panel.get();
  sidebar_content_ = sidebar;
  sidebar_panel_ = sidebar;
  SidebarPanel::Parts parts;
  window_controls_ = sidebar->AddChildView(std::make_unique<views::View>());
  // .window-controls: buttons at the right, a rule below.
  window_controls_->SetBorder(FlyEdgeBorder(FlyRole::kBorder,
                                            gfx::Insets::TLBR(0, 0, 1, 0),
                                            gfx::Insets::TLBR(2, 0, 6, 0)));
  window_controls_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 5))
      ->set_main_axis_alignment(views::BoxLayout::MainAxisAlignment::kEnd);
  for (const auto& [action, icon, label] :
       {std::tuple{"minimize", &views::kChromeMinimizeIcon, u"Minimize Fly"},
        std::tuple{"maximize", &views::kChromeMaximizeIcon, u"Maximize Fly"},
        std::tuple{"close", &views::kCloseIcon, u"Close Fly"}}) {
    auto* button =
        window_controls_->AddChildView(views::ImageButton::CreateIconButton(
            base::BindRepeating(&FlyWindowController::RunWindowControl,
                                weak_factory_.GetWeakPtr(),
                                std::string(action)),
            *icon, label, views::ImageButton::MaterialIconStyle::kSmall));
    button->SetPreferredSize(gfx::Size(26, 24));
    window_control_buttons_.emplace(action, button);
  }
  parts.window_controls = window_controls_;
  // #WorkspaceHeader opens the dashboard; the short badge (rail) and the
  // full name (expanded) share its slot and crossfade.
  workspace_picker_button_ = sidebar->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&FlyWindowController::ToggleWorkspacePicker,
                          weak_factory_.GetWeakPtr()),
      u"", FlyFace::kSans, 14));
  workspace_picker_button_->SetTooltipText(
      u"Workspace: Click to open Workspace Dashboard");
  workspace_picker_button_->SetCornerRadius(4);
  workspace_picker_button_->SetHoverFill(true);
  workspace_badge_ = sidebar->AddChildView(std::make_unique<FlyLabel>(
      u"W1", FlyFace::kMono, 12, gfx::Font::Weight::MEDIUM));
  workspace_badge_->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  workspace_badge_->SetCanProcessEventsWithinSubtree(false);
  workspace_label_ = sidebar->AddChildView(std::make_unique<FlyLabel>(
      u"Workspace", FlyFace::kSans, 14, gfx::Font::Weight::SEMIBOLD));
  workspace_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  workspace_label_->SetElideBehavior(gfx::ELIDE_TAIL);
  workspace_label_->SetCanProcessEventsWithinSubtree(false);
  new_tab_button_ = sidebar->AddChildView(MakeFlyIconButton(
      base::BindRepeating(&FlyWindowController::CreateTab,
                          weak_factory_.GetWeakPtr()),
      kAddIcon, u"New Tab / Navigation", 28));
  new_tab_button_->SetOutlined(true);
  new_tab_button_->SetCornerRadius(4);
  auto* back = sidebar->AddChildView(MakeFlyIconButton(
      command("go-back"), vector_icons::kArrowBackIcon, u"Go back", 30));
  auto* forward = sidebar->AddChildView(MakeFlyIconButton(
      command("go-forward"), vector_icons::kArrowForwardIcon, u"Go forward",
      30));
  // .nav-history-group: one pill, each half with its own border.
  back->SetPillHalf(FlyButton::PillHalf::kLeft);
  forward->SetPillHalf(FlyButton::PillHalf::kRight);
  for (FlyButton* button : {new_tab_button_.get(), back, forward}) {
    button->SetOutlined(true);
    button->SetIconTone(FlyButton::IconTone::kText);
  }
  parts.top = {.header = workspace_picker_button_,
               .badge = workspace_badge_,
               .label = workspace_label_,
               .new_tab = new_tab_button_,
               .back = back,
               .forward = forward};
  workspace_buttons_ = sidebar->AddChildView(std::make_unique<views::View>());
  workspace_buttons_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  parts.workspaces = workspace_buttons_;
  tab_sidebar_ =
      sidebar->AddChildView(std::make_unique<TabSidebar>(base::BindRepeating(
          &FlyWindowController::CreateTab, weak_factory_.GetWeakPtr())));
  parts.tabs = tab_sidebar_;
  // .sidebar-clock-row with .sidebar-clock: 6px above, 4px below.
  auto* clock_row = sidebar->AddChildView(std::make_unique<views::View>());
  clock_row->SetLayoutManager(std::make_unique<views::FillLayout>());
  clock_row->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(6, 0, 4, 0)));
  clock_ = clock_row->AddChildView(std::make_unique<SidebarClock>(
      base::BindRepeating(&FlyWindowController::CycleClockFormat,
                          weak_factory_.GetWeakPtr())));
  clock_->SetVectorIcon(kTimerIcon, 11);
  clock_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  clock_->SetIconTone(FlyButton::IconTone::kAccent);
  parts.clock_row = clock_row;
  // .bottom-theme-layout, in the Electron order.
  sidebar_pin_button_ = sidebar->AddChildView(MakeFlyIconButton(
      base::BindRepeating(&FlyWindowController::ToggleSidebarAutoCollapse,
                          weak_factory_.GetWeakPtr()),
      views::kKeepIcon, u"Pin / Auto-Collapse Sidebar", 28));
  auto* split = sidebar->AddChildView(MakeFlyIconButton(
      command("toggle-split"), kSplitSceneIcon, u"Toggle Split View", 28));
  reader_button_ = sidebar->AddChildView(MakeFlyIconButton(
      command("toggle-reader-view"), kMenuBookIcon, u"Toggle Reader View",
      28));
  auto* search = sidebar->AddChildView(MakeFlyIconButton(
      command("toggle-address"), vector_icons::kSearchIcon,
      u"Search or enter address", 28));
  auto* extensions = sidebar->AddChildView(MakeFlyIconButton(
      command("open-extension-actions"), vector_icons::kExtensionOldIcon,
      u"Extensions", 28));
  preferences_button_ = sidebar->AddChildView(MakeFlyIconButton(
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) {
            if (window && window->selected_) {
              InvokeNativeCommand(window->selected_, "toggle-help");
              window->keyboard_editor_->ShowAppearance(true);
            }
          }, weak_factory_.GetWeakPtr()),
      vector_icons::kSettingsIcon, u"Settings", 28));
  preferences_button_->SetEnabled(false);
  for (FlyButton* button : {sidebar_pin_button_.get(), split,
                            reader_button_.get(), search, extensions,
                            preferences_button_.get()}) {
    button->SetOutlined(true);
    button->SetCornerRadius(4);
    button->SetIconTone(FlyButton::IconTone::kText);
    button->SetVectorIcon(*button->vector_icon(), 12);
    parts.footer.push_back(button);
  }
  sidebar->SetParts(std::move(parts));
  // Details that exist only expanded fade with the edge (style.css).
  for (views::View* view :
       {static_cast<views::View*>(window_controls_.get()),
        static_cast<views::View*>(workspace_label_.get()),
        static_cast<views::View*>(new_tab_button_.get()),
        static_cast<views::View*>(back), static_cast<views::View*>(forward),
        static_cast<views::View*>(workspace_buttons_.get()),
        static_cast<views::View*>(clock_row), static_cast<views::View*>(split),
        static_cast<views::View*>(reader_button_.get()),
        static_cast<views::View*>(search),
        static_cast<views::View*>(preferences_button_.get())})
    sidebar->AddExpandedOnly(view);
  // The badge sits over the header and never takes its clicks.
  sidebar->AddRailOnly(workspace_badge_, false);
  sidebar->AddExpandedOnly(tab_sidebar_->expanded_surfaces(), false);
  sidebar->AddRailOnly(tab_sidebar_->rail_surfaces(), false);
  sidebar->AddRailOnly(tab_sidebar_->rail_marks(), false);
  sidebar_slot_ = root->AddChildView(std::make_unique<views::View>());
  sidebar_slot_->SetPreferredSize(gfx::Size(FlyScaled(kSidebarWidth), 0));
  auto* column = root->AddChildView(std::make_unique<views::View>());
  auto* column_layout =
      column->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  content_ = column->AddChildView(std::make_unique<views::View>());
  content_->SetLayoutManager(std::make_unique<views::FillLayout>());
  column_layout->SetFlexForView(content_, 1);
  // Below the workspace hosts: shown while the workspace has no tabs.
  welcome_ = content_->AddChildView(std::make_unique<WelcomeView>());
  welcome_->SetVisible(false);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() {
        base::FilePath assets;
        std::string bytes;
        if (base::PathService::Get(base::DIR_ASSETS, &assets))
          base::ReadFileToStringWithMaxSize(
              assets.AppendASCII("fly_resources")
                  .AppendASCII("identity")
                  .AppendASCII("icon-128.png"),
              &bytes, 1024 * 1024);
        return bytes;
      }),
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window, std::string bytes) {
            if (!window || !window->welcome_ || bytes.empty())
              return;
            const auto image = gfx::Image::CreateFrom1xPNGBytes(
                base::as_byte_span(bytes));
            if (!image.IsEmpty())
              window->welcome_->SetLogo(image.AsImageSkia());
          },
          weak_factory_.GetWeakPtr()));
  status_bar_ = column->AddChildView(std::make_unique<StatusBar>(StatusBar::Actions{
      .zoom = command("reset-zoom"),
      .notifications = command("toggle-notifications"),
      .workspace = base::BindRepeating(&FlyWindowController::ToggleWorkspacePicker,
                                       weak_factory_.GetWeakPtr()),
      .clock = base::BindRepeating(&FlyWindowController::CycleClockFormat,
                                   weak_factory_.GetWeakPtr()),
      .contents_changed = base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) {
            if (!window)
              return;
            window->ScheduleExtensionRefresh();
            window->RefreshReaderState();
          },
          weak_factory_.GetWeakPtr()),
  }));
  status_ = status_bar_->mode_label();
  layout->SetFlexForView(column, 1);
  SetContentsView(std::move(root));
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  params.delegate = this;
  params.name = "FlyDesktopWindow";
#if BUILDFLAG(IS_LINUX)
  // Task switchers and the menu entry (StartupWMClass) match on this; the
  // launcher's --class sets it.
  params.wm_class_name = shell_integration_linux::GetProgramClassName();
  params.wm_class_class = shell_integration_linux::GetProgramClassClass();
  params.wm_role_name = "browser";
  params.wayland_app_id = params.wm_class_class;
#endif
  params.bounds = gfx::Rect(80, 80, 1200, 800);
  desktop_->Init(std::move(params));
  sidebar_widget_ = std::make_unique<SidebarWidget>(base::BindRepeating(
      [](FlyWindowController* window) { return window->sidebar_hit_width_; },
      base::Unretained(this)));
  views::Widget::InitParams sidebar_params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  sidebar_params.parent = desktop_->GetNativeWindow();
  sidebar_params.child = true;
  sidebar_params.activatable = views::Widget::InitParams::Activatable::kNo;
  sidebar_params.native_widget =
      new views::NativeWidgetAura(sidebar_widget_.get());
  sidebar_params.name = "FlyNativeSidebar";
  // The panel's shadow falls on the page beside it.
  sidebar_params.opacity =
      views::Widget::InitParams::WindowOpacity::kTranslucent;
  sidebar_widget_->Init(std::move(sidebar_params));
  sidebar_root_ = sidebar_widget_->SetContentsView(
      std::make_unique<SidebarRoot>(std::move(panel)));
  sidebar_widget_->AddObserver(this);
  LoadSidebarArtwork();
  auto sidebar_host = std::make_unique<views::NativeViewHost>();
  // Profile BrowserWidgets retain Chromium's native transient-window stacking.
  // Use the clip-window host for every desktop child so Aura owns all sibling
  // layers consistently; Views-managed layer reordering cannot order this
  // transient hierarchy against independent native controls.
  sidebar_host->SetLayerManagedByViews(false);
  sidebar_host->SetProperty(views::kViewIgnoredByLayoutKey, true);
  sidebar_host_ =
      desktop_->GetContentsView()->AddChildView(std::move(sidebar_host));
  sidebar_host_->Attach(sidebar_widget_->GetNativeView());
  // Only the visible panel takes the pointer; the rest of the host (the
  // panel while collapsed, its shadow) leaves events to the page below.
  if (auto* clip = sidebar_host_->GetNativeViewContainer()) {
    // The clip window itself never takes events; the panel's hit mask
    // decides for points such as drops.
    clip->SetEventTargetingPolicy(
        aura::EventTargetingPolicy::kDescendantsOnly);
    clip->SetEventTargeter(std::make_unique<aura::WindowTargeter>());
    sidebar_targeter_ = clip->targeter();
  }
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
                   action.starts_with("extension:") ||
                   action.starts_with("extension-options:") ||
                   action.starts_with("extensions-") ||
                   NativeCommandAvailable(window->selected_, action);
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             content::WebContents* source, std::string action) {
            if (!window || !window->selected_ ||
                window->selected_->GetTabStripModel()->GetActiveWebContents() !=
                    source)
              return;
            if (action.starts_with("terminal:")) {
              OpenTerminalWithCommand(action.substr(9));
              return;
            }
            window->preferences_.RecordRecentCommand(action);
            if (action.starts_with("extension"))
              window->ChooseExtensionAction(source, std::move(action));
            else
              InvokeNativeCommand(window->selected_, std::move(action));
          },
          weak_factory_.GetWeakPtr()));
  address_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->closing_ &&
                   window->address_choices_.contains(std::string(action));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ExecuteAddress,
                          weak_factory_.GetWeakPtr()));
  history_ = std::make_unique<HistoryService>(
      *g_browser_process->profile_manager(), workspaces_);
  history_->SetChangedCallback(base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window) {
        if (!window || window->closing_)
          return;
        if (window->history_picker_ && window->history_picker_->active())
          window->LoadHistory(HistorySurface::kHistory);
        else if (window->address_picker_ && window->address_picker_->active())
          window->LoadHistory(HistorySurface::kAddress);
        else if (window->bookmark_picker_ &&
                 window->bookmark_picker_->active() &&
                 window->preferences_.ReadFrequentSitesVisible() == true)
          window->LoadHistory(HistorySurface::kBookmarks);
        else {
          window->history_entries_.clear();
        }
      },
      weak_factory_.GetWeakPtr()));
  history_clear_service_ = std::make_unique<HistoryService>(
      *g_browser_process->profile_manager(), workspaces_);
  history_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->closing_ &&
                   window->history_choices_.contains(std::string(action));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::OpenHistory,
                          weak_factory_.GetWeakPtr()));
  bookmark_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating([](base::WeakPtr<FlyWindowController> window,
                            std::string_view url) {
        return window && !window->closing_ &&
               window->bookmark_urls_.contains(std::string(url));
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::OpenBookmark,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::RemoveBookmark,
                          weak_factory_.GetWeakPtr()));
  workspace_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating([](base::WeakPtr<FlyWindowController> window,
                             std::string_view action) {
        return window && window->WorkspaceChoiceAvailable(action);
      }, weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseWorkspace,
                          weak_factory_.GetWeakPtr()));
  tab_move_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && window->TabMoveDestinationAvailable(action);
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseTabMoveDestination,
                          weak_factory_.GetWeakPtr()));
  downloads_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->closing_ &&
                   (action == "downloads-page" ||
                    action.starts_with("download:"));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, content::WebContents*,
             std::string action) {
            if (window && !window->closing_ &&
                !OpenDownloadEntry(window->WorkspaceProfiles(),
                                   window->selected_, action))
              window->ShowStatus(u"That download is no longer available.");
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, content::WebContents*,
             std::string action) {
            if (!window || window->closing_)
              return;
            RemoveDownloadEntry(window->WorkspaceProfiles(), action);
            if (window->downloads_picker_->active())
              window->downloads_picker_->UpdateItems(
                  DownloadEntries(window->WorkspaceProfiles()), true);
          },
          weak_factory_.GetWeakPtr()));
  extensions_ = std::make_unique<FlyExtensions>(
      preferences_,
      base::BindRepeating(&FlyWindowController::ScheduleExtensionRefresh,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ShowStatus,
                          weak_factory_.GetWeakPtr()));
  user_content_ = std::make_unique<UserContentService>(base::BindRepeating(
      &FlyWindowController::ShowStatus, weak_factory_.GetWeakPtr()));
  extensions_picker_ = std::make_unique<CommandPalette>(
      desktop_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->closing_ &&
                   (action.starts_with("extension:") ||
                    action.starts_with("extensions-"));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseExtensionAction,
                          weak_factory_.GetWeakPtr()));
  settings_menu_ = std::make_unique<SettingsMenu>(
      preferences_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, std::string action) {
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(
                    [](base::WeakPtr<FlyWindowController> window,
                       std::string action) {
                      if (window && window->selected_ && !window->closing_)
                        InvokeNativeCommand(window->selected_, action);
                    },
                    window, std::move(action)));
          },
          weak_factory_.GetWeakPtr()));
  using Layout = PaletteStyle::Layout;
  extensions_picker_->SetStyle(
      {Layout::kCentre, u"Extensions", u"Actions of this workspace's extensions",
       560});
  downloads_picker_->SetStyle(
      {Layout::kCentre, u"Downloads",
       u"Enter opens, pauses or resumes. Remove cancels or clears.", 640});
  address_picker_->SetStyle({.layout = Layout::kTop, .width = 640});
  history_picker_->SetStyle({.layout = Layout::kTop, .width = 600});
  auto page_insets = base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window) {
        if (!window || !window->sidebar_slot_)
          return gfx::Insets();
        return gfx::Insets::TLBR(
            0, window->sidebar_slot_->width(),
            window->status_bar_ && window->status_bar_->GetVisible()
                ? window->status_bar_->height()
                : 0,
            0);
      },
      weak_factory_.GetWeakPtr());
  bookmark_picker_->SetStyle(
      {Layout::kPage, u"Bookmarks Manager", u"", 720, page_insets});
  palette_->SetStyle({.layout = Layout::kPage,
                      .title = u"Command Centre",
                      .subtitle = u"Actions, feature guide & keyboard shortcuts",
                      .width = 1220,
                      .page_insets = page_insets,
                      .command_centre = true});
  workspace_picker_->SetStyle(
      {Layout::kPage, u"Workspace Dashboard", u"", 720, page_insets});
  tab_move_picker_->SetStyle(
      {Layout::kCentre, u"Move tab to workspace", u"", 560});
  keyboard_editor_ =
      std::make_unique<KeyboardEditor>(desktop_.get(), commands_,
                                       *g_browser_process->local_state());
  keyboard_editor_->SetActionRunner(base::BindRepeating(
      &FlyWindowController::RunPreferencesAction, weak_factory_.GetWeakPtr()));
  diagnostics_hub_ = std::make_unique<DiagnosticsHub>(
      desktop_.get(),
      DiagnosticsHub::Delegate{
          .session = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window) {
                return window ? window->CurrentDiagnostics()
                              : DiagnosticsSession();
              },
              weak_factory_.GetWeakPtr()),
          .set_recording = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window, bool on) {
                if (!window)
                  return;
                window->preferences_.SetBool("enable_diagnostics", on);
                if (!on)
                  window->diagnostics_events_.clear();
              },
              weak_factory_.GetWeakPtr()),
          .clear_events = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window) {
                if (window)
                  window->diagnostics_events_.clear();
              },
              weak_factory_.GetWeakPtr()),
          .open_url = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window, GURL url) {
                if (window)
                  window->OpenInNewTab(url);
              },
              weak_factory_.GetWeakPtr()),
          .page_insets = page_insets});
  keyboard_editor_->SetExtensions(
      extensions_.get(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window)
              -> BrowserWindowInterface* {
            return window ? window->selected_.get() : nullptr;
          },
          weak_factory_.GetWeakPtr()));
  desktop_->AddObserver(this);
  desktop_->GetFocusManager()->AddFocusChangeListener(this);
  observing_sidebar_focus_ = true;
  sidebar_focus_client_ = aura::client::GetFocusClient(
      desktop_->GetNativeWindow()->GetRootWindow());
  if (sidebar_focus_client_)
    sidebar_focus_client_->AddObserver(this);
  preferences_registrar_.Init(g_browser_process->local_state());
  preferences_registrar_.Add(
      kPreferencesPref,
      base::BindRepeating(&FlyWindowController::OnPreferencesChanged,
                          weak_factory_.GetWeakPtr()));
  preferences_registrar_.Add(kBookmarksPref,
      base::BindRepeating(&FlyWindowController::OnBookmarksChanged,
                          weak_factory_.GetWeakPtr()));
  preferences_registrar_.Add(kWorkspacesPref,
      base::BindRepeating(&FlyWindowController::OnWorkspacesChanged,
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
  closing_ = true;
  StopSidebarFocusObservation();
  tab_move_.reset();
  tab_move_picker_.reset();
  downloads_picker_.reset();
  extensions_picker_.reset();
  if (settings_menu_)
    settings_menu_->Cancel();
  settings_menu_.reset();
  if (keyboard_editor_)
    keyboard_editor_->SetExtensions(nullptr, {});
  extensions_.reset();
  user_content_.reset();
  diagnostics_hub_.reset();
  workspace_close_.reset();
  remove_widget_.reset();
  remove_delegate_.reset();
  CHECK(entries_.empty());
  weak_factory_.InvalidateWeakPtrs();
  clock_timer_.Stop();
  suspension_timer_.Stop();
  preferences_registrar_.RemoveAll();
  rename_widget_.reset();
  rename_delegate_.reset();
  offer_widget_.reset();
  offer_delegate_.reset();
  keyboard_editor_.reset();
  workspace_picker_.reset();
  history_clear_widget_.reset();
  history_clear_delegate_.reset();
  history_.reset();
  history_clear_service_.reset();
  history_picker_.reset();
  bookmark_picker_.reset();
  address_picker_.reset();
  palette_.reset();
  hints_.reset();
  // During application quit the native desktop can be destroyed before the
  // posted controller cleanup, while this client-owned Widget is still alive.
  if (auto* native_window = desktop_->GetNativeWindow())
    native_window->RemovePreTargetHandler(this);
  desktop_->RemoveObserver(this);
  // Destruction is only posted after all profile-bound BrowserWidgets finish
  // their own beforeunload and destruction paths.
  ClearSidebarViews();
  if (sidebar_host_) {
    sidebar_targeter_ = nullptr;
    sidebar_host_->Detach();
    sidebar_host_ = nullptr;
  }
  if (sidebar_widget_) {
    sidebar_widget_->RemoveObserver(this);
    sidebar_widget_.reset();
  }
  sidebar_slot_ = nullptr;
  welcome_ = nullptr;
  welcome_keep_alive_.reset();
  content_ = nullptr;
  status_ = nullptr;
  status_bar_ = nullptr;
  clock_ = nullptr;
  preferences_button_ = nullptr;
  tab_sidebar_ = nullptr;
  desktop_.reset();
}

void FlyWindowController::CancelHistory() {
  ++history_generation_;
  history_->Cancel();
  history_picker_->Cancel();
  history_entries_.clear();
  history_choices_.clear();
}
std::vector<PaletteEntry> FlyWindowController::HistoryEntries() {
  history_choices_.clear();
  std::vector<PaletteEntry> result;
  for (const auto& item : history_entries_) {
    const auto action = item.url.spec();
    if (!history_choices_.insert(action).second)
      continue;
    result.push_back(
        {action,
         action,
         item.title.empty() ? action : base::UTF16ToUTF8(item.title),
         action,
         {},
         {}});
  }
  if (!history_clear_running_) {
    history_choices_.insert("clear-history");
    result.push_back({"clear-history",
                      "clear-history",
                      "Clear history…",
                      "All normal workspaces",
                      {},
                      {}});
  }
  return result;
}
void FlyWindowController::LoadHistory(HistorySurface surface) {
  if (!selected_)
    return;
  auto* contents = selected_->GetTabStripModel()->GetActiveWebContents();
  auto* picker = surface == HistorySurface::kAddress   ? address_picker_.get()
                 : surface == HistorySurface::kHistory ? history_picker_.get()
                                                       : bookmark_picker_.get();
  if (!contents || !picker->active() ||
      (surface == HistorySurface::kBookmarks &&
       preferences_.ReadFrequentSitesVisible() != true))
    return;
  const auto generation = ++history_generation_;
  history_->Query(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         base::WeakPtr<content::WebContents> source, uint64_t generation,
         HistorySurface surface,
         std::optional<std::vector<HistoryEntry>> rows) {
        if (!window || window->closing_ || !source || !window->selected_ ||
            generation != window->history_generation_ ||
            window->selected_->GetTabStripModel()->GetActiveWebContents() !=
                source.get())
          return;
        auto* picker = surface == HistorySurface::kAddress
                           ? window->address_picker_.get()
                       : surface == HistorySurface::kHistory
                           ? window->history_picker_.get()
                           : window->bookmark_picker_.get();
        if (!picker->active())
          return;
        window->history_entries_ = rows.value_or(std::vector<HistoryEntry>());
        if (!rows)
          window->status_->SetText(
              u"History is unavailable. Saved data has been preserved.");
        if (surface == HistorySurface::kAddress)
          picker->RefreshQuery();
        else if (surface == HistorySurface::kHistory)
          picker->UpdateItems(window->HistoryEntries(), false);
        else
          picker->UpdateItems(window->BookmarkEntries(),
                              window->bookmarks_->CanEdit());
      },
      weak_factory_.GetWeakPtr(), contents->GetWeakPtr(), generation, surface));
}

void FlyWindowController::ToggleHistory(BrowserWindowInterface* browser) {
  if (history_picker_->active()) {
    history_picker_->Cancel(true);
    return;
  }
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  keyboard_editor_->Cancel();
  CancelHistory();
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents)
    return;
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  history_picker_->ToggleItems(contents, {},
                               config.shortcuts.at("toggle-history"),
                               u"Search workspace history", false);
  LoadHistory(HistorySurface::kHistory);
}
void FlyWindowController::OpenHistory(content::WebContents* source,
                                      std::string action) {
  if (closing_ || !selected_ || !source ||
      selected_->GetTabStripModel()->GetActiveWebContents() != source ||
      !history_choices_.contains(action))
    return;
  if (action == "clear-history") {
    ConfirmHistoryClear(source);
    return;
  }
  // Requery the native stores after dismissal, so retired workspaces or deleted
  // visits cannot execute stale cached rows. Keep the selected destination.
  history_->Query(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         base::WeakPtr<content::WebContents> source, std::string url,
         std::optional<std::vector<HistoryEntry>> rows) {
        if (!window || window->closing_ || !source || !window->selected_ ||
            window->selected_->GetTabStripModel()->GetActiveWebContents() !=
                source.get() ||
            !rows || !std::ranges::any_of(*rows, [&](const HistoryEntry& item) {
              return item.url.spec() == url;
            }))
          return;
        NavigateParams params(window->selected_, GURL(url),
                              ui::PAGE_TRANSITION_TYPED);
        params.disposition = WindowOpenDisposition::CURRENT_TAB;
        params.window_action = NavigateParams::WindowAction::kShowWindow;
        Navigate(&params);
      },
      weak_factory_.GetWeakPtr(), source->GetWeakPtr(), std::move(action)));
}

views::Widget* FlyWindowController::history_clear_dialog_for_testing() const {
  return history_clear_widget_ && !history_clear_widget_->IsClosed()
             ? history_clear_widget_.get()
             : nullptr;
}
void FlyWindowController::ConfirmHistoryClear(content::WebContents* source) {
  if (history_clear_running_)
    return;
  CancelWorkspaceRename();
  history_clear_widget_.reset();
  history_clear_delegate_ = std::make_unique<views::DialogDelegate>();
  history_clear_delegate_->SetOwnershipOfNewWidget(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  history_clear_delegate_->SetModalType(ui::mojom::ModalType::kWindow);
  history_clear_delegate_->SetTitle(u"Clear browsing history");
  history_clear_delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk,
                                          u"Clear history");
  history_clear_delegate_->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kCancel));
  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
  body->SetPreferredSize(gfx::Size(440, 140));
  auto* message = body->AddChildView(std::make_unique<views::Label>(
      u"Permanently remove browsing history from all normal workspaces? "
      u"Cookies, logins and bookmarks are kept. Private visits are never "
      u"saved."));
  message->SetMultiLine(true);
  history_clear_delegate_->SetContentsView(std::move(body));
  history_clear_delegate_->SetAcceptCallback(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         base::WeakPtr<content::WebContents> source) {
        if (!window || window->closing_ || !source || !window->selected_ ||
            window->selected_->GetTabStripModel()->GetActiveWebContents() !=
                source.get())
          return;
        window->history_entries_.clear();
        window->history_choices_.clear();
        ++window->history_generation_;
        window->status_->SetText(u"Clearing history…");
        window->history_clear_running_ = true;
        window->history_clear_service_->Clear(base::BindOnce(
            [](base::WeakPtr<FlyWindowController> window, bool cleared) {
              if (!window)
                return;
              // A cancelled quit clears `closing_` again; the lock must not
              // outlive the operation.
              window->history_clear_running_ = false;
              if (!window->closing_) {
                window->status_->SetText(
                    cleared
                        ? u"Browsing history cleared."
                        : u"History could not be cleared in every workspace.");
                if (window->history_picker_->active())
                  window->LoadHistory(HistorySurface::kHistory);
                else if (window->address_picker_->active())
                  window->LoadHistory(HistorySurface::kAddress);
              }
            },
            window));
      },
      weak_factory_.GetWeakPtr(), source->GetWeakPtr()));
  history_clear_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      history_clear_delegate_.get(), desktop_->GetNativeWindow(),
      desktop_->GetNativeView()));
  history_clear_widget_->Show();
}

void FlyWindowController::ToggleAddress(BrowserWindowInterface* browser) {
  if (address_picker_->active()) {
    address_picker_->Cancel(true);
    return;
  }
  hints_->Cancel();
  palette_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents)
    return;
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  const auto url = contents->GetVisibleURL();
  address_picker_->ToggleQuery(
      contents,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::u16string_view query) {
            return window ? window->AddressEntries(query)
                          : std::vector<PaletteEntry>();
          },
          weak_factory_.GetWeakPtr()),
      config.shortcuts.at("toggle-address"), u"Address or search",
      IsFlyNewTabPage(url) ? std::u16string()
                           : base::UTF8ToUTF16(url.spec()));
  LoadHistory(HistorySurface::kAddress);
}
std::vector<PaletteEntry> FlyWindowController::AddressEntries(
    std::u16string_view text) {
  address_choices_.clear();
  const auto input = base::UTF16ToUTF8(text);
  std::vector<PaletteEntry> result;
  auto add = [&](std::string kind, std::string payload, std::string title,
                 std::string detail) {
    const auto action = kind + ":" + payload;
    address_choices_.emplace(action, payload);
    result.push_back(
        {action, action, std::move(title), std::move(detail), {}, {}});
  };
  if (input.empty())
    return result;
  const auto lower = base::ToLowerASCII(input);
  if (lower.starts_with("ws:") || lower.starts_with("ws ")) {
    const auto query = base::ToLowerASCII(std::string(
        base::TrimWhitespaceASCII(input.substr(3), base::TRIM_ALL)));
    if (const auto catalog = workspaces_.Read())
      for (const auto& item : catalog->items)
        if (base::ToLowerASCII(item.name).find(query) != std::string::npos)
          add("workspace", item.id, "Switch to " + item.name, "Workspace");
    return result;
  }
  if (const auto value = EvaluateAddressExpression(input)) {
    const auto answer = base::NumberToString(*value);
    add("calculate", input, "= " + answer, "Copy result");
  }
  SearchService search(*g_browser_process->local_state());
  if (const auto target = search.ResolveNavigation(input))
    add("navigate", input, target->spec(), "Open in this workspace");
  // App bookmarks are an explicit shared product service, never workspace
  // cookies or private history. Results are inert text in native controls.
  if (const auto bookmarks = bookmarks_->Read())
    for (const auto& item : *bookmarks)
      if (result.size() < 12 &&
          base::ToLowerASCII(item.title + " " + item.url).find(lower) !=
              std::string::npos)
        add("bookmark", item.url, item.title, item.url);
  std::set<std::string> urls;
  for (const auto& item : history_entries_)
    if (result.size() < 12 && urls.insert(item.url.spec()).second &&
        base::ToLowerASCII(base::UTF16ToUTF8(item.title) + " " +
                           item.url.spec())
                .find(lower) != std::string::npos)
      add("history", item.url.spec(),
          item.title.empty() ? item.url.spec() : base::UTF16ToUTF8(item.title),
          item.url.spec());
  return result;
}
void FlyWindowController::ExecuteAddress(content::WebContents* source,
                                         std::string action) {
  if (closing_ || !selected_ || !source ||
      selected_->GetTabStripModel()->GetActiveWebContents() != source)
    return;
  const auto choice = address_choices_.find(action);
  if (choice == address_choices_.end())
    return;
  const auto payload = choice->second;
  address_choices_.clear();
  if (action.starts_with("workspace:")) {
    OpenWorkspace(payload);
    return;
  }
  if (action.starts_with("calculate:")) {
    if (const auto value = EvaluateAddressExpression(payload)) {
      ui::ScopedClipboardWriter(ui::ClipboardBuffer::kCopyPaste)
          .WriteText(base::UTF8ToUTF16(base::NumberToString(*value)));
    }
    return;
  }
  if (action.starts_with("history:")) {
    history_choices_.insert(payload);
    OpenHistory(source, payload);
    return;
  }
  std::optional<GURL> target;
  if (action.starts_with("bookmark:") && bookmarks_->Contains(payload))
    target = GURL(payload);
  else if (action.starts_with("navigate:"))
    target = SearchService(*g_browser_process->local_state())
                 .ResolveNavigation(payload);
  if (!target || !target->is_valid())
    return;
  NavigateParams params(selected_, *target, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
}

std::optional<std::vector<FrequentSite>> FlyWindowController::FrequentSites()
    const {
  std::vector<SiteVisit> visits;
  for (const auto& row : history_entries_)
    visits.push_back({row.url.spec(), row.visited});
  return frequent_sites_->Read(visits);
}
base::DictValue FlyWindowController::NewTabLists(bool off_the_record) const {
  // Only web pages are listed, as on Electron's new tab page.
  const auto link = [](const std::string& url, const std::string& title) {
    const GURL target(url);
    if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS())
      return std::optional<base::DictValue>();
    return std::optional<base::DictValue>(
        base::DictValue()
            .Set("url", target.spec())
            .Set("title", title.empty() ? target.host() : title));
  };
  base::ListValue bookmarks;
  base::ListValue sites;
  const bool show_sites =
      !off_the_record && preferences_.ReadFrequentSitesVisible().value_or(true);
  if (!off_the_record) {
    if (const auto saved = bookmarks_->Read()) {
      for (const auto& bookmark : *saved) {
        if (auto entry = link(bookmark.url, bookmark.title))
          bookmarks.Append(std::move(*entry));
      }
    }
  }
  if (show_sites) {
    if (const auto frequent = FrequentSites()) {
      for (const auto& site : *frequent) {
        if (auto entry = link(site.url, site.title))
          sites.Append(std::move(*entry));
      }
    }
  }
  return base::DictValue()
      .Set("showBookmarks", !off_the_record)
      .Set("showSites", show_sites)
      .Set("bookmarks", std::move(bookmarks))
      .Set("sites", std::move(sites));
}
bool FlyWindowController::HideFrequentSite(const std::string& url) {
  return !closing_ && frequent_sites_->Hide(url);
}
std::vector<PaletteEntry> FlyWindowController::BookmarkEntries() {
  bookmark_urls_.clear();
  std::vector<PaletteEntry> entries;
  if (const auto bookmarks = bookmarks_->Read()) {
    for (const auto& bookmark : *bookmarks) {
      bookmark_urls_.insert(bookmark.url);
      entries.push_back({bookmark.url, bookmark.url, bookmark.title,
                         bookmark.url, {}, {}});
    }
  }
  if (const auto sites = FrequentSites())
    for (const auto& site : *sites) {
      bookmark_urls_.insert(site.url);
      entries.push_back({site.url,
                         site.url,
                         site.title,
                         "Frequent site · " + site.url,
                         {},
                         {}});
    }
  return entries;
}
void FlyWindowController::OnBookmarksChanged() {
  const bool readable = bookmarks_->Read().has_value();
  if (!readable) {
    if (bookmark_picker_->active())
      bookmark_picker_->Cancel(true);
    return;
  }
  if (bookmark_picker_->active())
    bookmark_picker_->UpdateItems(BookmarkEntries(), bookmarks_->CanEdit());
}
void FlyWindowController::OpenBookmark(content::WebContents* source,
                                      std::string url) {
  if (closing_ || !selected_ ||
      selected_->GetTabStripModel()->GetActiveWebContents() != source ||
      !bookmark_urls_.contains(url))
    return;
  if (!bookmarks_->Contains(url)) {
    // Recompute against current native history and explicit hidden/visibility
    // choices after dismissal, rather than navigating a stale derived row.
    history_->Query(base::BindOnce(
        [](base::WeakPtr<FlyWindowController> window,
           base::WeakPtr<content::WebContents> source, std::string url,
           std::optional<std::vector<HistoryEntry>> rows) {
          if (!window || window->closing_ || !source || !window->selected_ ||
              window->selected_->GetTabStripModel()->GetActiveWebContents() !=
                  source.get() ||
              !rows)
            return;
          std::vector<SiteVisit> visits;
          for (const auto& row : *rows)
            visits.push_back({row.url.spec(), row.visited});
          const auto sites = window->frequent_sites_->Read(visits);
          if (!sites || std::ranges::find(*sites, url, &FrequentSite::url) ==
                            sites->end())
            return;
          NavigateParams params(window->selected_, GURL(url),
                                ui::PAGE_TRANSITION_AUTO_BOOKMARK);
          params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
          params.window_action = NavigateParams::WindowAction::kShowWindow;
          Navigate(&params);
        },
        weak_factory_.GetWeakPtr(), source->GetWeakPtr(), std::move(url)));
    return;
  }
  NavigateParams params(selected_, GURL(url), ui::PAGE_TRANSITION_AUTO_BOOKMARK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
}
void FlyWindowController::RemoveBookmark(content::WebContents* source,
                                        std::string url) {
  if (!closing_ && selected_ &&
      selected_->GetTabStripModel()->GetActiveWebContents() == source) {
    if (bookmarks_->Contains(url))
      bookmarks_->Remove(url);
    else if (bookmark_urls_.contains(url))
      frequent_sites_->Hide(url);
  }
}

void FlyWindowController::OnPreferencesChanged() {
  const auto frequent = preferences_.ReadFrequentSitesVisible();
  if (frequent != applied_frequent_visibility_) {
    applied_frequent_visibility_ = frequent;
    if (bookmark_picker_ && bookmark_picker_->active()) {
      if (frequent == true)
        LoadHistory(HistorySurface::kBookmarks);
      else {
        ++history_generation_;
        history_->Cancel();
        history_entries_.clear();
      }
      bookmark_picker_->UpdateItems(BookmarkEntries(), bookmarks_->CanEdit());
    }
  }
  const auto theme = preferences_.ReadTheme();
  if (theme && theme != applied_theme_) {
    applied_theme_ = theme;
    ApplyFlyPalette();
    const auto color_mode = theme->mode == "dark"
                                ? ui::ColorProviderKey::ColorMode::kDark
                                : ui::ColorProviderKey::ColorMode::kLight;
    desktop_->SetColorModeOverride(color_mode);
    sidebar_widget_->SetColorModeOverride(color_mode);
    // Native WebContents observers recompute actual renderer preferences,
    // including hidden/split/private/extension pages. No web API is replaced.
    ui::NativeTheme::GetInstanceForWeb()->NotifyOnNativeThemeUpdated();
  }
  ApplyFlyPalette();
  ApplyProfilePreferencesEverywhere();
  if (user_content_)
    user_content_->SetEmailHandler(
        preferences_.ReadString("email_handler").value_or("system"));
  UpdateClock();
  UpdateSidebarMode();
  UpdateWindowControls();
  UpdateSuspensionPolicy();
}

views::ImageButton* FlyWindowController::window_control_for_testing(
    std::string_view action) const {
  const auto found = window_control_buttons_.find(std::string(action));
  return found == window_control_buttons_.end() ? nullptr : found->second.get();
}
void FlyWindowController::ClearSidebarViews() {
  clock_timer_.Stop();
  window_control_buttons_.clear();
  window_controls_ = nullptr;
  workspace_buttons_ = nullptr;
  clock_ = nullptr;
  preferences_button_ = nullptr;
  tab_sidebar_ = nullptr;
  sidebar_content_ = nullptr;
  sidebar_pin_button_ = nullptr;
  reader_button_ = nullptr;
  reader_poll_.Stop();
  new_tab_button_ = nullptr;
  workspace_badge_ = nullptr;
  workspace_label_ = nullptr;
  workspace_picker_button_ = nullptr;
  sidebar_panel_ = nullptr;
  sidebar_root_ = nullptr;
  tab_numbers_timer_.Stop();
}
void FlyWindowController::StopSidebarFocusObservation() {
  if (sidebar_focus_client_) {
    sidebar_focus_client_->RemoveObserver(this);
    sidebar_focus_client_ = nullptr;
  }
  if (observing_sidebar_focus_) {
    desktop_->GetFocusManager()->RemoveFocusChangeListener(this);
    observing_sidebar_focus_ = false;
  }
}
bool FlyWindowController::SidebarOverlayBlocked() const {
  return (address_picker_ && address_picker_->active()) ||
         (palette_ && palette_->active()) ||
         (workspace_picker_ && workspace_picker_->active()) ||
         (tab_move_picker_ && tab_move_picker_->active()) ||
         (downloads_picker_ && downloads_picker_->active()) ||
         (extensions_picker_ && extensions_picker_->active()) ||
         (bookmark_picker_ && bookmark_picker_->active()) ||
         (history_picker_ && history_picker_->active()) ||
         (keyboard_editor_ && keyboard_editor_->active()) ||
         (hints_ && hints_->active());
}
void FlyWindowController::UpdateSidebarBounds() {
  if (!sidebar_widget_ || !sidebar_widget_->GetNativeWindow() ||
      !desktop_->GetNativeWindow())
    return;
  auto bounds = desktop_->GetContentsView()->GetLocalBounds();
  if (bounds.IsEmpty())
    return;
  // The host keeps one size in both states: the edge moves on the
  // compositor, and only the hit area follows it.
  bounds.set_width(FlyScaled(kSidebarWidth) +
                   (sidebar_auto_collapse_ ? kSidebarShadowExtent : 0));
  sidebar_host_->SetBoundsRect(bounds);
  // This overlay is excluded from BoxLayout; update its native clip explicitly.
  sidebar_host_->DeprecatedLayoutImmediately();
  UpdateSidebarHitArea();
}
void FlyWindowController::UpdateSidebarHitArea() {
  if (!sidebar_targeter_ || !sidebar_host_)
    return;
  sidebar_targeter_->SetInsets(gfx::Insets::TLBR(
      0, 0, 0, std::max(0, sidebar_host_->width() - sidebar_hit_width_)));
}
float FlyWindowController::SidebarShiftNow() const {
  if (!sidebar_motion_animating_)
    return sidebar_shift_to_;
  const double progress =
      std::clamp((base::TimeTicks::Now() - sidebar_motion_start_) /
                     kSidebarMotion,
                 0.0, 1.0);
  return gfx::Tween::FloatValueBetween(
      gfx::Tween::CalculateValue(gfx::Tween::FAST_OUT_SLOW_IN_3, progress),
      sidebar_shift_from_, sidebar_shift_to_);
}
int FlyWindowController::VisibleSidebarWidth() const {
  return FlyScaled(kSidebarWidth) - static_cast<int>(SidebarShiftNow() + 0.5f);
}
void FlyWindowController::OnSidebarMotionEnded() {
  sidebar_motion_animating_ = false;
  // A finished collapse gives the page back everything right of the rail.
  sidebar_hit_width_ = FlyScaled(kSidebarWidth) -
                       static_cast<int>(sidebar_shift_to_ + 0.5f);
  UpdateSidebarHitArea();
}
void FlyWindowController::UpdateSidebarMode() {
  if (!sidebar_content_)
    return;
  const bool collapse = preferences_.ReadSidebarAutoCollapse().value_or(false);
  const bool changed = collapse != sidebar_auto_collapse_;
  sidebar_auto_collapse_ = collapse;
  // Pinning or unpinning resizes the page once; hovering never does.
  sidebar_slot_->SetPreferredSize(gfx::Size(
      FlyScaled(collapse ? kSidebarRailWidth : kSidebarWidth), 0));
  if (auto* pin = sidebar_pin_button_.get()) {
    const auto* label = collapse ? u"Pin sidebar" : u"Unpin sidebar";
    pin->SetVectorIcon(collapse ? views::kKeepIcon : views::kKeepFilledIcon,
                       12);
    pin->SetSelected(!collapse);
    pin->SetAccentText(!collapse);
    pin->SetTooltipText(label);
    pin->GetViewAccessibility().SetName(label);
    pin->SetEnabled(preferences_.CanEditSidebarAutoCollapse());
  }
  if (changed && collapse) {
    sidebar_keyboard_expanded_ = false;
    sidebar_focus_inside_ = false;
    if (sidebar_content_->Contains(
            desktop_->GetFocusManager()->GetFocusedView()))
      ReturnFocusFromSidebar();
  }
  // A mode or scale change applies at once (the slide distance, the shadow
  // and the details follow the new mode); other preference writes leave a
  // running slide alone. Fly starts collapsed instead of animating closed.
  sidebar_motion_ready_ = false;
  if (changed || sidebar_motion_scale_ != GetFlyInterfaceScale())
    sidebar_motion_applied_ = false;
  UpdateSidebarBounds();
  UpdateSidebarVisibility();
  sidebar_motion_ready_ = true;
}
bool FlyWindowController::sidebar_expanded_for_testing() const {
  return sidebar_expanded_;
}
void FlyWindowController::UpdateSidebarVisibility() {
  if (closing_ || !sidebar_content_ || !sidebar_widget_->GetNativeWindow())
    return;
  bool menu_open = tab_sidebar_->context_menu_for_testing() ||
                   workspace_rename_dialog_for_testing() ||
                   workspace_remove_dialog_for_testing();
  for (const auto& child : workspace_buttons_->children()) {
    auto* button = views::AsViewClass<WorkspaceMenuButton>(child.get());
    menu_open |= button && button->running_model();
  }
  const bool blocked = SidebarOverlayBlocked();
  const bool expanded =
      !sidebar_auto_collapse_ ||
      (!blocked && (sidebar_pointer_inside_ || sidebar_focus_inside_ ||
                    sidebar_keyboard_expanded_ || menu_open));
  if (!sidebar_widget_->IsVisible())
    sidebar_widget_->ShowInactive();
  // Participate in the same Views layer ordering as the embedded browsers.
  // Manually stacking an independent Aura window among Views-owned browser
  // layers can violate WindowReorderer's ordering during tab rebuilds.
  sidebar_host_->SetVisible(true);
  if (!blocked) {
    auto* clip = sidebar_host_->GetNativeViewContainer();
    if (clip && clip->parent())
      clip->parent()->StackChildAtTop(clip);
  }
  if (expanded != sidebar_expanded_ || !sidebar_motion_applied_) {
    sidebar_expanded_ = expanded;
    ApplySidebarMotion(sidebar_motion_ready_ && sidebar_motion_applied_ &&
                       !gfx::Animation::PrefersReducedMotion());
  }
  UpdateClock();
}
// Electron's slide (style.css #Sidebar): collapsed, the edge sits 224px to
// the left and the panel 224px back to the right inside it, so the panel's
// left 36px show. Both transforms run on the compositor; the shadow
// and the state-specific details fade with them.
void FlyWindowController::ApplySidebarMotion(bool animate) {
  auto* panel = sidebar_root_ ? sidebar_root_->panel() : nullptr;
  auto* edge = sidebar_root_ ? sidebar_root_->edge() : nullptr;
  if (!panel || !panel->layer() || !edge || !edge->layer())
    return;
  sidebar_motion_applied_ = true;
  sidebar_motion_scale_ = GetFlyInterfaceScale();
  const bool rail = sidebar_auto_collapse_ && !sidebar_expanded_;
  const int shift = rail ? FlyScaled(kSidebarWidth - kSidebarRailWidth) : 0;
  sidebar_shift_from_ = SidebarShiftNow();
  sidebar_shift_to_ = shift;
  sidebar_motion_start_ = base::TimeTicks::Now();
  sidebar_motion_animating_ = animate;
  // Expanding takes the pointer at once; collapsing keeps the panel's area
  // until the edge has passed, so the page never gets clicks on a panel
  // that is still visible.
  if (!rail || !animate)
    sidebar_hit_width_ = FlyScaled(kSidebarWidth) - shift;
  gfx::Transform outer;
  outer.Translate(-shift, 0);
  gfx::Transform inner;
  inner.Translate(shift, 0);
  const float shadow = sidebar_auto_collapse_ && sidebar_expanded_ ? 1 : 0;
  UpdateSidebarHitArea();
  tab_sidebar_->SetRail(rail);
  if (!animate) {
    for (auto* layer : {edge->layer(), panel->layer(),
                        sidebar_root_->shadow()->layer()})
      layer->GetAnimator()->StopAnimating();
    edge->layer()->SetTransform(outer);
    panel->layer()->SetTransform(inner);
    sidebar_root_->shadow()->layer()->SetOpacity(shadow);
    sidebar_panel_->SetRail(rail, nullptr);
    return;
  }
  views::AnimationBuilder builder;
  builder.SetPreemptionStrategy(
      ui::LayerAnimator::IMMEDIATELY_ANIMATE_TO_NEW_TARGET);
  builder.OnEnded(base::BindOnce(&FlyWindowController::OnSidebarMotionEnded,
                                 weak_factory_.GetWeakPtr()));
  auto& block = builder.Once()
                    .SetDuration(kSidebarMotion)
                    .SetTransform(edge->layer(), outer,
                                  gfx::Tween::FAST_OUT_SLOW_IN_3)
                    .SetTransform(panel->layer(), inner,
                                  gfx::Tween::FAST_OUT_SLOW_IN_3)
                    .SetOpacity(sidebar_root_->shadow()->layer(), shadow,
                                gfx::Tween::FAST_OUT_SLOW_IN_3);
  sidebar_panel_->SetRail(rail, &block);
}
void FlyWindowController::LoadSidebarArtwork() {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce([]() {
        std::array<std::string, 3> bytes;
        base::FilePath assets;
        if (!base::PathService::Get(base::DIR_ASSETS, &assets))
          return bytes;
        const auto identity =
            assets.AppendASCII("fly_resources").AppendASCII("identity");
        const std::array<const char*, 3> names = {
            "engraving-chasis@2x.png", "engraving-papel@2x.png",
            "paper-grain.png"};
        for (size_t i = 0; i < names.size(); ++i) {
          base::ReadFileToStringWithMaxSize(identity.AppendASCII(names.at(i)),
                                            &bytes.at(i), 1024 * 1024);
        }
        return bytes;
      }),
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window,
             std::array<std::string, 3> bytes) {
            if (!window || !window->sidebar_panel_)
              return;
            // The engravings are 2x artwork (512px for a 256px image).
            const auto decode = [](const std::string& png, float scale) {
              const auto bitmap =
                  gfx::PNGCodec::Decode(base::as_byte_span(png));
              return bitmap.isNull()
                         ? gfx::ImageSkia()
                         : gfx::ImageSkia(gfx::ImageSkiaRep(bitmap, scale));
            };
            const auto grain = decode(bytes.at(2), 1);
            window->sidebar_panel_->SetArtwork(decode(bytes.at(0), 2),
                                               decode(bytes.at(1), 2), grain);
            if (window->status_bar_)
              window->status_bar_->SetPaperGrain(grain);
          },
          weak_factory_.GetWeakPtr()));
}
void FlyWindowController::ToggleSidebarAutoCollapse() {
  const auto collapse = preferences_.ReadSidebarAutoCollapse();
  if (collapse)
    preferences_.SetSidebarAutoCollapse(!*collapse);
}
void FlyWindowController::ReturnFocusFromSidebar() {
  sidebar_keyboard_expanded_ = false;
  sidebar_focus_inside_ = false;
  if (sidebar_content_ &&
      sidebar_content_->Contains(desktop_->GetFocusManager()->GetFocusedView()))
    desktop_->GetFocusManager()->ClearFocus();
  if (selected_) {
    if (auto* contents = selected_->GetTabStripModel()->GetActiveWebContents())
      contents->Focus();
  }
  UpdateSidebarVisibility();
}
void FlyWindowController::FocusSidebar() {
  if (closing_ || !sidebar_content_)
    return;
  if (sidebar_focus_inside_) {
    ReturnFocusFromSidebar();
    return;
  }
  sidebar_keyboard_expanded_ = true;
  // Activating a row sends Aura focus to the page while the desktop focus
  // manager still records that row. Requesting the same view again would then
  // be a no-op, leaving the expanded sidebar without keyboard focus.
  if (sidebar_content_->Contains(
          desktop_->GetFocusManager()->GetFocusedView()))
    desktop_->GetFocusManager()->ClearFocus();
  UpdateSidebarBounds();
  UpdateSidebarVisibility();
  if (selected_ && !selected_->GetTabStripModel()->empty())
    tab_sidebar_->FocusActiveTab();
  else
    workspace_picker_button_->RequestFocus();
}
bool FlyWindowController::SidebarHasFocus() const {
  if (!sidebar_content_ || !sidebar_focus_client_ || !sidebar_widget_)
    return false;
  const auto* focused = sidebar_focus_client_->GetFocusedWindow();
  return focused && (sidebar_widget_->GetNativeWindow()->Contains(focused) ||
                     (focused == desktop_->GetNativeWindow() &&
                      sidebar_content_->Contains(
                          desktop_->GetFocusManager()->GetFocusedView())));
}
void FlyWindowController::OnDidChangeFocus(views::View*, views::View* now) {
  if (closing_ || !sidebar_content_)
    return;
  sidebar_focus_inside_ = sidebar_content_->Contains(now);
  if (sidebar_focus_inside_)
    sidebar_keyboard_expanded_ = false;
  UpdateSidebarVisibility();
}
void FlyWindowController::OnWindowFocused(aura::Window* gained, aura::Window*) {
  if (closing_ || !sidebar_content_ || !sidebar_widget_->GetNativeWindow())
    return;
  const auto* native = sidebar_widget_->GetNativeWindow();
  sidebar_focus_inside_ =
      gained &&
      (native->Contains(gained) || gained == desktop_->GetNativeWindow()) &&
      sidebar_content_->Contains(desktop_->GetFocusManager()->GetFocusedView());
  if (!sidebar_focus_inside_)
    sidebar_keyboard_expanded_ = false;
  UpdateSidebarVisibility();
}
void FlyWindowController::OnWidgetBoundsChanged(views::Widget* widget,
                                                const gfx::Rect&) {
  if (widget == desktop_.get())
    UpdateSidebarBounds();
}
void FlyWindowController::OnMouseEvent(ui::MouseEvent* event) {
  if (closing_ || !sidebar_content_ || SidebarOverlayBlocked())
    return;
  if (event->type() != ui::EventType::kMouseMoved &&
      event->type() != ui::EventType::kMouseEntered &&
      event->type() != ui::EventType::kMouseExited &&
      event->type() != ui::EventType::kMousePressed)
    return;
  // Like Electron, leaving the window keeps the sidebar as it is: moving
  // from the page to the screen's left edge opens it, and touching the
  // window border does not close it. Entering the page closes it.
  if (event->type() == ui::EventType::kMouseExited)
    return;
  auto point = event->root_location();
  wm::ConvertPointToScreen(desktop_->GetNativeWindow()->GetRootWindow(),
                           &point);
  const auto window = desktop_->GetContentsView()->GetBoundsInScreen();
  if (!window.Contains(point) &&
      event->type() != ui::EventType::kMousePressed)
    return;
  // The edge the user sees, mid-slide included.
  auto bounds = window;
  bounds.set_width(VisibleSidebarWidth());
  const bool was_inside = sidebar_pointer_inside_;
  sidebar_pointer_inside_ = bounds.Contains(point);
  const bool pressed = event->type() == ui::EventType::kMousePressed;
  // Pointer motion only matters when it crosses the sidebar edge.
  if (!pressed && was_inside == sidebar_pointer_inside_)
    return;
  if (!sidebar_pointer_inside_ && pressed) {
    sidebar_keyboard_expanded_ = false;
    sidebar_focus_inside_ = false;
    if (SidebarHasFocus() ||
        sidebar_content_->Contains(
            desktop_->GetFocusManager()->GetFocusedView())) {
      desktop_->GetFocusManager()->ClearFocus();
      // Desktop and profile BrowserWidget focus managers are distinct. A real
      // page click crosses that boundary; Chrome toolbar clicks keep their
      // own native handling.
      auto* page = selected_
                       ? selected_->GetTabStripModel()->GetActiveWebContents()
                       : nullptr;
      auto* view = page ? page->GetRenderWidgetHostView() : nullptr;
      if (view && view->GetViewBounds().Contains(point))
        page->Focus();
    }
  }
  UpdateSidebarVisibility();
}

void FlyWindowController::UpdateWindowControls() {
  if (!window_controls_)
    return;
  const bool visible = preferences_.ReadWindowControlsVisible().value_or(false);
  if (!visible &&
      window_controls_->Contains(
          desktop_->GetFocusManager()->GetFocusedView()) &&
      selected_) {
    desktop_->GetFocusManager()->ClearFocus();
    if (auto* contents = selected_->GetTabStripModel()->GetActiveWebContents())
      contents->Focus();
  }
  window_controls_->SetVisible(visible);
  const bool maximized = desktop_->IsMaximized();
  auto* maximize = window_control_buttons_.at("maximize").get();
  const auto icon = ui::ImageModel::FromVectorIcon(
      maximized ? views::kChromeRestoreIcon : views::kChromeMaximizeIcon,
      ui::kColorIcon, 16);
  for (auto state : {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
                     views::Button::STATE_PRESSED,
                     views::Button::STATE_DISABLED})
    maximize->SetImageModel(state, icon);
  const auto* title = maximized ? u"Restore Fly" : u"Maximize Fly";
  maximize->SetTooltipText(title);
  maximize->GetViewAccessibility().SetName(title);
}
void FlyWindowController::RunWindowControl(std::string action) {
  VLOG(1) << "Fly window control requested: " << action;
  if (closing_ || !window_controls_->GetVisible())
    return;
  // Complete pointer/keyboard button dispatch before native close callbacks can
  // destroy this control, the selected browser or the entire desktop.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window, std::string action) {
            if (!window || window->closing_ ||
                !window->preferences_.ReadWindowControlsVisible().value_or(
                    false))
              return;
            VLOG(1) << "Fly window control executing: " << action;
            if (action == "minimize")
              window->desktop_->Minimize();
            else if (action == "maximize") {
              if (window->desktop_->IsMaximized())
                window->desktop_->Restore();
              else
                window->desktop_->Maximize();
            } else if (action == "close")
              window->desktop_->Close();
          },
          weak_factory_.GetWeakPtr(), std::move(action)));
}
void FlyWindowController::OnWidgetShowStateChanged(views::Widget* widget) {
  if (widget == desktop_.get()) {
    UpdateWindowControls();
    UpdateClock();
  }
}

void FlyWindowController::UpdateSuspensionPolicy() {
  SuspendIdleTabs();
  const auto minutes = preferences_.ReadSuspendTimeoutMinutes();
  if (closing_ || !minutes || *minutes == 0) {
    suspension_timer_.Stop();
  } else if (!suspension_timer_.IsRunning()) {
    suspension_timer_.Start(
        FROM_HERE, base::Seconds(30),
        base::BindRepeating(&FlyWindowController::SuspendIdleTabs,
                            weak_factory_.GetWeakPtr()));
  }
}
void FlyWindowController::SuspendIdleTabs() {
  if (closing_)
    return;
  std::vector<base::WeakPtr<content::WebContents>> pages;
  for (const auto& [browser, entry] : entries_) {
    if (entry.browser_closed)
      continue;
    auto* model = browser->GetTabStripModel();
    for (int index = 0; index < model->count(); ++index)
      pages.push_back(model->GetWebContentsAt(index)->GetWeakPtr());
  }
  const auto timeout =
      base::Minutes(preferences_.ReadSuspendTimeoutMinutes().value_or(0));
  const auto now = base::TimeTicks::Now();
  for (const auto& page : pages)
    if (page)
      MaybeAutoSuspendNativeTab(page.get(), timeout, now);
}
void FlyWindowController::ResumeAllTabs() {
  std::vector<base::WeakPtr<content::WebContents>> pages;
  for (const auto& [browser, entry] : entries_) {
    if (entry.browser_closed)
      continue;
    auto* model = browser->GetTabStripModel();
    for (int index = 0; index < model->count(); ++index)
      pages.push_back(model->GetWebContentsAt(index)->GetWeakPtr());
  }
  for (const auto& page : pages)
    if (page)
      ResumeNativeTab(page.get());
}

void FlyWindowController::ApplyFlyPalette() {
  const auto theme = preferences_.ReadTheme();
  const bool dark = !theme || theme->mode != "light";
  auto colors = DefaultFlyColors(dark);
  const auto custom = [&](std::string_view key, SkColor& target) {
    const auto hex = preferences_.ReadThemeColor(dark, key);
    uint32_t rgb = 0;
    if (hex && hex->size() == 7 && base::HexStringToUInt(hex->substr(1), &rgb))
      target = SkColorSetRGB(rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff);
  };
  custom("accent", colors.accent);
  custom("bg_main", colors.bg_main);
  custom("bg_sidebar", colors.bg_sidebar);
  custom("text", colors.text);
  colors.sidebar_opacity = preferences_.ReadThemeOpacity(dark, "sidebar_opacity");
  colors.overlay_opacity = preferences_.ReadThemeOpacity(dark, "overlay_opacity");
  const float scale =
      preferences_.ReadString("interface_scale").value_or("normal") == "large"
          ? 1.25f
          : 1.0f;
  const bool scale_changed = scale != GetFlyInterfaceScale();
  SetFlyInterfaceScale(scale);
  if (!SetFlyColors(colors) && !scale_changed)
    return;
  for (auto* widget : {desktop_.get(), sidebar_widget_.get()}) {
    if (widget && widget->GetNativeWindow()) {
      widget->ThemeChanged();
      widget->GetRootView()->SchedulePaint();
    }
  }
  if (scale_changed) {
    // Widths follow the scale like Electron's zoomed interface.
    if (auto* sidebar = sidebar_content_.get())
      sidebar->SetPreferredSize(gfx::Size(FlyScaled(260), 0));
    UpdateSidebarMode();
    UpdateSidebarBounds();
    if (desktop_ && desktop_->GetRootView())
      desktop_->GetRootView()->InvalidateLayout();
  }
}
void FlyWindowController::UpdateWorkspaceHeader() {
  if (!workspace_picker_button_)
    return;
  std::u16string name = u"Workspace";
  std::u16string badge = u"?";
  std::u16string compact = u"W";
  const bool is_private =
      selected_ && selected_->GetProfile()->IsOffTheRecord();
  const auto catalog = workspaces_.Read();
  if (is_private) {
    name = u"Private";
    badge = u"P";
    compact = u"P";
  } else if (catalog && selected_) {
    const auto& id = entries_.at(selected_).workspace_id;
    for (size_t i = 0; i < catalog->items.size(); ++i) {
      if (catalog->items[i].id != id)
        continue;
      name = base::UTF8ToUTF16(catalog->items[i].name);
      badge = base::NumberToString16(i + 1);
      compact = u"W" + badge;
    }
  }
  workspace_badge_->SetText(is_private ? compact : WorkspaceBadgeText(name));
  workspace_label_->SetText(name);
  workspace_picker_button_->GetViewAccessibility().SetName(
      u"Workspace " + name + u": open the workspace dashboard");
  // The list is only needed to reach other workspaces or windows directly.
  workspace_buttons_->SetVisible(
      (catalog && catalog->items.size() > 1) ||
      workspace_buttons_->children().size() > (catalog ? catalog->items.size()
                                                       : 0));
}
void FlyWindowController::UpdateClock() {
  if (!clock_)
    return;
  const bool footer = preferences_.ReadStatusBarVisible().value_or(false);
  if (status_bar_)
    status_bar_->SetVisible(footer);
  const auto settings = preferences_.ReadClock();
  const bool visible = settings && settings->visible;
  if (!visible && clock_->HasFocus() && selected_) {
    if (auto* contents = selected_->GetTabStripModel()->GetActiveWebContents())
      contents->Focus();
  }
  clock_->SetVisible(visible);
  clock_->SetEnabled(preferences_.CanEditClock());
  const bool desktop_shown = desktop_->IsVisible() && !desktop_->IsMinimized();
  const bool sidebar_shown = sidebar_expanded_;
  if (!visible || !desktop_shown || (!sidebar_shown && !footer)) {
    if (status_bar_)
      status_bar_->SetClockVisible(visible);
    clock_timer_.Stop();
    return;
  }
  const auto now = base::Time::Now();
  const auto text = FormatClock(now, settings->format);
  // The sidebar and the footer draw the date and the time separately (time
  // in the accent display face), like the Electron clocks.
  std::u16string date;
  std::u16string time = text.label;
  if (settings->format == "datetime") {
    const auto split = text.label.rfind(u' ');
    if (split != std::u16string::npos) {
      date = text.label.substr(0, split);
      base::TrimWhitespace(date, base::TRIM_TRAILING, &date);
      time = text.label.substr(split + 1);
    }
  }
  clock_->SetClock(text.label, date, time);
  if (clock_->GetTooltipText() != text.tooltip)
    clock_->SetTooltipText(text.tooltip);
  if (status_bar_) {
    status_bar_->SetClock(date, time, text.tooltip, true);
  }
  // Wake at the next visible change only: the next second for the seconds
  // format, otherwise the next minute.
  base::Time::Exploded local;
  now.LocalExplode(&local);
  const auto delay =
      settings->format == "seconds"
          ? base::Milliseconds(1000 - local.millisecond)
          : base::Seconds(60 - local.second) - base::Milliseconds(local.millisecond);
  clock_timer_.Start(FROM_HERE, delay + base::Milliseconds(5),
                     base::BindOnce(&FlyWindowController::UpdateClock,
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
  auto native_host = std::make_unique<views::NativeViewHost>();
  native_host->SetLayerManagedByViews(false);
  auto* host = content_->AddChildView(std::move(native_host));
  host->SetVisible(false);
  auto [entry, inserted] =
      entries_.emplace(browser, Entry{browser, nullptr, host, workspace_id});
  entry->second.order = next_window_order_++;
  entry->second.closed_subscription =
      browser->RegisterBrowserDidClose(base::BindRepeating(
          &FlyWindowController::OnBrowserDidClose, weak_factory_.GetWeakPtr()));
  ApplyProfilePreferences(browser->GetProfile(), preferences_);
  if (!browser->GetProfile()->IsOffTheRecord()) {
    extensions_->AddProfile(browser->GetProfile());
    user_content_->AddProfile(browser->GetProfile());
    site_zoom_.AddProfile(browser->GetProfile());
    QueueLegacyImport(browser);
  }
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
    CancelWorkspaceRename();
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
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    downloads_picker_->Cancel();
    extensions_picker_->Cancel();
    settings_menu_->Cancel();
    CancelHistory();
    keyboard_editor_->Cancel();
    selected_ = browser;
    preferences_button_->SetEnabled(true);
    OnBookmarksChanged();
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
    UpdateWorkspaceHeader();
    ScheduleExtensionRefresh();
    // Browser tests run without network access to the release feed and
    // assert that starting writes no product preference.
    if (!update_checked_ &&
        !base::CommandLine::ForCurrentProcess()->HasSwitch("test-type")) {
      update_checked_ = true;
      // Selection happens inside session restore; opening a tab there would
      // change the active tab under the restorer. Run after it finishes.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE, base::BindOnce(
                         [](base::WeakPtr<FlyWindowController> window) {
                           if (!window || window->closing_)
                             return;
                           window->MaybeShowWhatsNew();
                           window->CheckUpdates(false);
                           window->MaybeOfferDesktopIntegration();
                         },
                         weak_factory_.GetWeakPtr()));
    }
    next.host->SetVisible(true);
    next.widget->ShowInactive();
    if (!next.workspace_id.empty()) {
      workspaces_.Activate(next.workspace_id);
      last_windows_[next.workspace_id] = browser->GetWeakPtr();
    }
    next.widget->OnNativeWidgetActivationChanged(desktop_->IsActive());
    welcome_keep_alive_.reset();
    UpdateWelcome();
    if (!pending_empty_action_.empty()) {
      // The empty workspace view's Bookmarks or Command Centre, once the
      // workspace's new tab exists.
      base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](base::WeakPtr<BrowserWindowInterface> browser,
                 std::string action) {
                if (browser)
                  InvokeNativeCommand(browser.get(), action);
              },
              browser->GetWeakPtr(), std::exchange(pending_empty_action_, "")));
    }
  }
  ScheduleWorkspaceRefresh();
  UpdateModeLabel();
  desktop_->Show();
  UpdateSidebarBounds();
  UpdateSidebarVisibility();
  if (focus) {
    // Profile BrowserWidgets share one OS activation. Tell the native startup
    // coordinator which persistent workspace the user actually selected.
    if (!closing_ && !browser->GetProfile()->IsOffTheRecord())
      profiles::SetLastUsedProfile(browser->GetProfile()->GetPath().BaseName());
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
  if (!workspace_buttons_)
    return;
  if (workspace_picker_->active())
    workspace_picker_->UpdateItems(WorkspaceEntries(), false);
  workspace_buttons_->RemoveAllChildViews();
  const auto catalog = workspaces_.Read();
  if (!catalog) {
    workspace_buttons_->AddChildView(std::make_unique<FlyLabel>(
        u"Workspace data needs recovery", FlyFace::kSans, 12));
    return;
  }
  const bool editable = workspaces_.CanEdit();
  for (const auto& item : catalog->items) {
    auto* button =
        workspace_buttons_->AddChildView(std::make_unique<WorkspaceMenuButton>(
            base::BindRepeating(&FlyWindowController::OpenWorkspace,
                                weak_factory_.GetWeakPtr(), item.id),
            base::UTF8ToUTF16(item.name), item.id,
            editable && &item != &catalog->items.front(),
            editable && &item != &catalog->items.back(),
            base::BindRepeating(&FlyWindowController::ReorderWorkspace,
                                weak_factory_.GetWeakPtr(), item.id),
            editable,
            base::BindRepeating(&FlyWindowController::RenameWorkspace,
                                weak_factory_.GetWeakPtr(), item.id),
            editable && catalog->items.size() > 1 && !workspace_close_,
            base::BindRepeating(&FlyWindowController::RemoveWorkspace,
                                weak_factory_.GetWeakPtr(), item.id)));
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
          std::make_unique<FlyButton>(
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
      workspace_buttons_->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&FlyWindowController::SelectWindow,
                              weak_factory_.GetWeakPtr(),
                              browser->GetWeakPtr()),
          label));
    }
  }
  UpdateWorkspaceHeader();
}

ui::SimpleMenuModel* FlyWindowController::workspace_context_menu_for_testing(
    std::string_view id) const {
  for (const auto& child : workspace_buttons_->children()) {
    auto* button = views::AsViewClass<WorkspaceMenuButton>(child.get());
    if (button && button->workspace_id() == id)
      return button->running_model();
  }
  return nullptr;
}

void FlyWindowController::ReorderWorkspace(std::string id, int direction) {
  if (closing_ || !workspaces_.CanEdit() ||
      (direction != -1 && direction != 1))
    return;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  const auto index = item - catalog->items.begin();
  const auto target = index + direction;
  if (target < 0 || target >= static_cast<std::ptrdiff_t>(catalog->items.size()))
    return;
  std::vector<std::string> order;
  for (const auto& workspace : catalog->items)
    order.push_back(workspace.id);
  std::swap(order[index], order[target]);
  workspaces_.Reorder(order);
}

views::Widget* FlyWindowController::workspace_rename_dialog_for_testing() const {
  return rename_widget_ && !rename_widget_->IsClosed() ? rename_widget_.get()
                                                     : nullptr;
}

void FlyWindowController::CancelWorkspaceRename() {
  if (history_clear_widget_ && !history_clear_widget_->IsClosed())
    history_clear_widget_->Close();
  if (rename_widget_ && !rename_widget_->IsClosed())
    rename_widget_->Close();
  if (remove_widget_ && !remove_widget_->IsClosed())
    remove_widget_->Close();
}

views::Widget* FlyWindowController::workspace_remove_dialog_for_testing()
    const {
  return remove_widget_ && !remove_widget_->IsClosed() ? remove_widget_.get()
                                                       : nullptr;
}

bool FlyWindowController::CanRemoveWorkspace(std::string_view id,
                                             const base::FilePath& path) const {
  if (closing_ || creating_profile_ || tab_move_ || !workspaces_.CanEdit())
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog || catalog->items.size() < 2)
    return false;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  return item != catalog->items.end() &&
         path ==
             g_browser_process->profile_manager()->user_data_dir().AppendASCII(
                 item->profile_directory);
}

void FlyWindowController::RemoveWorkspace(std::string id) {
  if (workspace_close_)
    return;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  const auto path =
      g_browser_process->profile_manager()->user_data_dir().AppendASCII(
          item->profile_directory);
  if (!CanRemoveWorkspace(id, path))
    return;
  CancelWorkspaceRename();
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  remove_widget_.reset();
  remove_delegate_ = std::make_unique<views::DialogDelegate>();
  remove_delegate_->SetOwnershipOfNewWidget(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  remove_delegate_->SetModalType(ui::mojom::ModalType::kWindow);
  remove_delegate_->SetTitle(u"Remove workspace · " +
                             base::UTF8ToUTF16(item->name));
  remove_delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Remove");
  remove_delegate_->SetDefaultButton(
      static_cast<int>(ui::mojom::DialogButton::kCancel));
  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
  body->SetPreferredSize(gfx::Size(440, 160));
  auto* message = body->AddChildView(std::make_unique<views::Label>(
      u"Close this workspace's tabs and private windows and remove it from "
      u"Fly? "
      u"Downloads in its private windows will stop. Its local data is kept for "
      u"recovery. Recreating the name starts fresh."));
  message->SetMultiLine(true);
  auto* error = body->AddChildView(std::make_unique<views::Label>());
  error->SetID(2);
  error->SetMultiLine(true);
  remove_delegate_->SetContentsView(std::move(body));
  remove_delegate_->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window, std::string id,
         base::FilePath path, views::Label* error) {
        if (!window || window->closing_)
          return true;
        if (window->workspace_close_ || !window->CanRemoveWorkspace(id, path)) {
          error->SetText(
              u"The workspace must still be editable and at least "
              u"one other workspace must remain.");
          return false;
        }
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
            FROM_HERE,
            base::BindOnce(&FlyWindowController::BeginWorkspaceRemoval, window,
                           std::move(id), std::move(path)));
        return true;
      },
      weak_factory_.GetWeakPtr(), std::move(id), path, error));
  remove_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      remove_delegate_.get(), desktop_->GetNativeWindow(),
      desktop_->GetNativeView()));
  remove_widget_->Show();
}

void FlyWindowController::BeginWorkspaceRemoval(std::string id,
                                                base::FilePath path) {
  if (workspace_close_ || !CanRemoveWorkspace(id, path))
    return;
  workspace_close_ = std::make_unique<WorkspaceCloseTransaction>(
      path,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, const std::string& id,
             const base::FilePath& path) {
            return window && window->CanRemoveWorkspace(id, path);
          },
          weak_factory_.GetWeakPtr(), id, path),
      base::BindOnce(&FlyWindowController::FinishWorkspaceRemoval,
                     weak_factory_.GetWeakPtr(), id, path));
  ScheduleWorkspaceRefresh();
  workspace_close_->Start();
}

void FlyWindowController::FinishWorkspaceRemoval(std::string id,
                                                 base::FilePath path,
                                                 bool success) {
  const bool removed =
      success && CanRemoveWorkspace(id, path) && workspaces_.Remove(id);
  if (removed) {
    auto* local_state = g_browser_process->local_state();
    const auto directory = path.BaseName().AsUTF8Unsafe();
    auto active = local_state->GetList(prefs::kProfilesLastActive).Clone();
    active.EraseIf([&](const base::Value& value) {
      return value.is_string() &&
             base::EqualsCaseInsensitiveASCII(value.GetString(), directory);
    });
    local_state->SetList(prefs::kProfilesLastActive, std::move(active));
    const auto catalog = workspaces_.Read();
    for (const auto& item : catalog->items)
      if (item.id == catalog->active_id &&
          local_state->GetFilePath(prefs::kProfileLastUsed) == path.BaseName())
        local_state->SetFilePath(
            prefs::kProfileLastUsed,
            base::FilePath::FromUTF8Unsafe(item.profile_directory));
    // Remove only the profile picker's metadata. ProfileManager retains native
    // lifetime ownership; neither this operation nor retirement deletes files.
    g_browser_process->profile_manager()
        ->GetProfileAttributesStorage()
        .RemoveProfile(path);
  }
  workspace_close_.reset();
  ScheduleWorkspaceRefresh();
  if (!removed) {
    status_->SetText(
        u"Workspace kept. Closing was cancelled or its data changed.");
    return;
  }
  const auto catalog = workspaces_.Read();
  if (!closing_ && catalog && !catalog->active_id.empty())
    OpenWorkspace(catalog->active_id);
}

void FlyWindowController::RenameWorkspace(std::string id) {
  if (closing_ || !workspaces_.CanEdit())
    return;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  // Both objects are client-owned. Destroy the old widget before its delegate;
  // no callback retains the workspace row that opened this dialog.
  rename_widget_.reset();
  rename_delegate_ = std::make_unique<views::DialogDelegate>();
  rename_delegate_->SetOwnershipOfNewWidget(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  rename_delegate_->SetModalType(ui::mojom::ModalType::kWindow);
  rename_delegate_->SetTitle(u"Rename workspace");
  rename_delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Save");
  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
  body->SetPreferredSize(gfx::Size(400, 110));
  auto* field = body->AddChildView(std::make_unique<views::Textfield>());
  field->SetID(1);
  field->SetText(base::UTF8ToUTF16(item->name));
  field->GetViewAccessibility().SetName(u"Workspace name");
  auto* error = body->AddChildView(std::make_unique<views::Label>());
  error->SetID(2);
  error->SetMultiLine(true);
  rename_delegate_->SetInitiallyFocusedView(field);
  rename_delegate_->SetContentsView(std::move(body));
  rename_delegate_->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window, std::string id,
         views::Textfield* field, views::Label* error) {
        if (!window || window->closing_)
          return true;
        if (!window->workspaces_.Rename(id, base::UTF16ToUTF8(field->GetText()))) {
          error->SetText(u"Use a unique, non-empty workspace name. "
                         u"The workspace must still be editable.");
          return false;
        }
        if (window->selected_ &&
            window->entries_.at(window->selected_).workspace_id == id)
          window->status_bar_->SetBrowser(window->selected_, std::u16string(field->GetText()));
        return true;
      }, weak_factory_.GetWeakPtr(), std::move(id), field, error));
  rename_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      rename_delegate_.get(), desktop_->GetNativeWindow(),
      desktop_->GetNativeView()));
  rename_widget_->Show();
  field->SelectAll(false);
}

void FlyWindowController::PromptNewWorkspace() {
  if (closing_ || creating_profile_ || !workspaces_.CanEdit())
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  rename_widget_.reset();
  rename_delegate_ = std::make_unique<views::DialogDelegate>();
  rename_delegate_->SetOwnershipOfNewWidget(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET);
  rename_delegate_->SetModalType(ui::mojom::ModalType::kWindow);
  rename_delegate_->SetTitle(u"New workspace");
  rename_delegate_->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Create");
  auto body = std::make_unique<views::View>();
  body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 8));
  body->SetPreferredSize(gfx::Size(400, 110));
  auto* field = body->AddChildView(std::make_unique<views::Textfield>());
  field->SetText(base::UTF8ToUTF16(NextWorkspaceName()));
  field->GetViewAccessibility().SetName(u"Workspace name");
  auto* error = body->AddChildView(std::make_unique<views::Label>());
  error->SetMultiLine(true);
  rename_delegate_->SetInitiallyFocusedView(field);
  rename_delegate_->SetContentsView(std::move(body));
  rename_delegate_->SetAcceptCallbackWithClose(base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window, views::Textfield* field,
         views::Label* error) {
        if (!window || window->closing_)
          return true;
        const auto name = std::string(base::TrimWhitespaceASCII(
            base::UTF16ToUTF8(field->GetText()), base::TRIM_ALL));
        const auto catalog = window->workspaces_.Read();
        if (name.empty() || !catalog ||
            std::ranges::any_of(catalog->items, [&](const Workspace& item) {
              return item.name == name;
            })) {
          error->SetText(u"Use a unique, non-empty workspace name.");
          return false;
        }
        window->CreateWorkspace(name);
        return true;
      },
      weak_factory_.GetWeakPtr(), field, error));
  rename_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      rename_delegate_.get(), desktop_->GetNativeWindow(),
      desktop_->GetNativeView()));
  rename_widget_->Show();
  field->SelectAll(false);
}

// Electron's workspace dashboard: every workspace with its tabs (pinned ones
// marked), rename and delete on each workspace, and a move on each tab.
std::vector<PaletteEntry> FlyWindowController::WorkspaceEntries() {
  private_choices_.clear();
  tab_choices_.clear();
  std::vector<PaletteEntry> result;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return result;
  const std::string active =
      selected_ && entries_.contains(selected_)
          ? entries_.at(selected_).workspace_id
          : std::string();
  const bool editable = workspaces_.CanEdit();
  for (const auto& item : catalog->items) {
    const auto action = "workspace/" + item.id;
    PaletteEntry row{action, action,
                     item.name + (item.id == active ? " (Active)" : ""),
                     item.profile_directory, {}, {}};
    if (editable) {
      row.row_actions.emplace_back("rename/" + item.id, "Rename");
      if (catalog->items.size() > 1)
        row.row_actions.emplace_back("delete/" + item.id, "Delete");
    }
    result.push_back(std::move(row));
    std::vector<std::pair<BrowserWindowInterface*, const Entry*>> windows;
    for (const auto& [browser, entry] : entries_)
      if (!entry.browser_closed && entry.workspace_id == item.id)
        windows.emplace_back(browser, &entry);
    std::ranges::sort(windows, {}, [](const auto& window) {
      return window.second->order;
    });
    size_t number = 0;
    for (const auto& [browser, entry] : windows) {
      auto* model = browser->GetTabStripModel();
      for (int index = 0; index < model->count(); ++index) {
        auto* contents = model->GetWebContentsAt(index);
        const auto key = item.id + "/" + base::NumberToString(number++);
        tab_choices_.emplace(key, TabChoice{browser->GetWeakPtr(),
                                            contents->GetWeakPtr()});
        const auto title = contents->GetTitle().empty()
                               ? contents->GetVisibleURL().spec()
                               : base::UTF16ToUTF8(contents->GetTitle());
        PaletteEntry tab{"tab/" + key, "tab/" + key,
                         "- " + title +
                             (model->IsTabPinned(index) ? " \xF0\x9F\x93\x8C"
                                                        : ""),
                         "", {}, {}};
        tab.indent = 1;
        tab.row_actions.emplace_back("move/" + key, "Move");
        result.push_back(std::move(tab));
      }
    }
  }
  for (const auto& [browser, entry] : entries_) {
    if (entry.browser_closed || !browser->GetProfile()->IsOffTheRecord())
      continue;
    const auto directory = browser->GetProfile()->GetOriginalProfile()
                               ->GetPath().BaseName().AsUTF8Unsafe();
    const auto item = std::ranges::find(catalog->items, directory,
                                       &Workspace::profile_directory);
    const auto title = item == catalog->items.end()
                           ? std::string("Private workspace")
                           : "Private · " + item->name;
    const auto action = "private/" + base::NumberToString(entry.order);
    private_choices_.emplace(action, browser->GetWeakPtr());
    result.push_back({action, action, title, "Private window", {}, {}});
  }
  result.push_back({"create", "create", "New workspace", "", {}, {}});
  return result;
}

bool FlyWindowController::WorkspaceChoiceAvailable(std::string_view action) const {
  if (closing_)
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return false;
  if (action == "create")
    return !creating_profile_ && workspaces_.CanEdit();
  if (action.starts_with("workspace/"))
    return std::ranges::find(catalog->items, action.substr(10), &Workspace::id) !=
           catalog->items.end();
  if (action.starts_with("rename/"))
    return workspaces_.CanEdit() &&
           std::ranges::find(catalog->items, action.substr(7),
                             &Workspace::id) != catalog->items.end();
  if (action.starts_with("delete/")) {
    const auto item =
        std::ranges::find(catalog->items, action.substr(7), &Workspace::id);
    return item != catalog->items.end() && !workspace_close_ &&
           CanRemoveWorkspace(item->id,
                              g_browser_process->profile_manager()
                                  ->user_data_dir()
                                  .AppendASCII(item->profile_directory));
  }
  if (action.starts_with("tab/") || action.starts_with("move/")) {
    const auto choice = tab_choices_.find(
        std::string(action.substr(action.find('/') + 1)));
    if (choice == tab_choices_.end() || !choice->second.browser ||
        !choice->second.contents)
      return false;
    return !action.starts_with("move/") ||
           TabMoveTransaction::CanMoveUrl(
               choice->second.contents->GetLastCommittedURL());
  }
  const auto found = private_choices_.find(std::string(action));
  return found != private_choices_.end() && found->second &&
         !found->second->IsDeleteScheduled();
}

void FlyWindowController::ChooseWorkspace(content::WebContents* source,
                                           std::string action) {
  if (!WorkspaceChoiceAvailable(action) ||
      (selected_ ? selected_->GetTabStripModel()->GetActiveWebContents() != source
                 : source != nullptr))
    return;
  if (action == "create")
    PromptNewWorkspace();
  else if (action.starts_with("workspace/"))
    OpenWorkspace(action.substr(10));
  else if (action.starts_with("rename/"))
    RenameWorkspace(action.substr(7));
  else if (action.starts_with("delete/"))
    RemoveWorkspace(action.substr(7));
  else if (action.starts_with("tab/") || action.starts_with("move/")) {
    const auto choice = tab_choices_.find(action.substr(action.find('/') + 1));
    if (choice == tab_choices_.end() || !choice->second.browser ||
        !choice->second.contents)
      return;
    auto* browser = choice->second.browser.get();
    auto* contents = choice->second.contents.get();
    const int index = browser->GetTabStripModel()->GetIndexOfWebContents(contents);
    if (index == TabStripModel::kNoTab)
      return;
    Select(browser, true);
    browser->GetTabStripModel()->ActivateTabAt(index);
    if (action.starts_with("move/"))
      ShowTabMovePicker(browser, contents);
  }
  else if (const auto choice = private_choices_.find(action);
           choice != private_choices_.end() && choice->second)
    Select(choice->second.get(), true);
}

void FlyWindowController::ToggleWorkspacePicker() {
  if (closing_ || !CanShowWorkspacePicker())
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  workspace_picker_->ToggleItems(
      selected_ ? selected_->GetTabStripModel()->GetActiveWebContents() : nullptr,
      WorkspaceEntries(), config.shortcuts.at("toggle-dashboard"),
      u"Search workspaces", false, true);
}

bool FlyWindowController::CanMoveTab(BrowserWindowInterface* browser,
                                     content::WebContents* contents) const {
  if (!browser || !contents || closing_ || creating_profile_ || tab_move_ ||
      workspace_close_ || contents->GetController().GetPendingEntry() ||
      selected_ != browser || !entries_.contains(browser) ||
      entries_.at(browser).browser_closed ||
      !TabMoveTransaction::CanMoveUrl(contents->GetLastCommittedURL()))
    return false;
  auto* tabs = browser->GetTabStripModel();
  const auto index = tabs->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab ||
      !tabs->IsContextMenuCommandEnabled(index, TabStripModel::CommandCloseTab))
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return false;
  const auto original = browser->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  const bool mapped =
      std::ranges::any_of(catalog->items, [&](const Workspace& item) {
        return root.AppendASCII(item.profile_directory) == original;
      });
  return mapped && !IsWorkspaceRetiring(original) &&
         std::ranges::any_of(catalog->items, [&](const Workspace& item) {
           const auto path = root.AppendASCII(item.profile_directory);
           return path != original && !IsWorkspaceRetiring(path);
         });
}
std::vector<PaletteEntry> FlyWindowController::TabMoveEntries() const {
  std::vector<PaletteEntry> result;
  if (!move_source_browser_)
    return result;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return result;
  const auto original =
      move_source_browser_->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  for (const auto& item : catalog->items) {
    const auto path = root.AppendASCII(item.profile_directory);
    if (path != original && !IsWorkspaceRetiring(path)) {
      const auto action = "move:" + item.id;
      const auto label =
          (move_source_browser_->GetProfile()->IsOffTheRecord() ? "Private · "
                                                                : "") +
          item.name;
      result.push_back({action, action, label, "Workspaces", {}, {}});
    }
  }
  return result;
}
bool FlyWindowController::TabMoveDestinationAvailable(
    std::string_view action) const {
  if (!CanMoveTab(move_source_browser_.get(), move_source_.get()))
    return false;
  return std::ranges::any_of(TabMoveEntries(), [&](const PaletteEntry& entry) {
    return entry.action == action;
  });
}
bool FlyWindowController::ShowTabMovePicker(BrowserWindowInterface* browser,
                                            content::WebContents* contents) {
  if (tab_move_picker_->active()) {
    tab_move_picker_->Cancel(true);
    return true;
  }
  if (!CanMoveTab(browser, contents))
    return false;
  auto* tabs = browser->GetTabStripModel();
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(contents));
  CancelWorkspaceRename();
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  workspace_picker_->Cancel();
  keyboard_editor_->Cancel();
  move_source_browser_ = browser->GetWeakPtr();
  move_source_ = contents->GetWeakPtr();
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  tab_move_picker_->ToggleItems(contents, TabMoveEntries(),
                                config.shortcuts.at("move-tab-to-workspace"),
                                u"Move tab to workspace", false);
  return true;
}
bool FlyWindowController::ValidateTabMove(
    base::WeakPtr<BrowserWindowInterface> source_browser,
    base::WeakPtr<content::WebContents> source,
    const std::string& destination_id,
    const base::FilePath& destination_path,
    bool require_selection) const {
  if (closing_ || workspace_close_ || !source_browser || !source ||
      !entries_.contains(source_browser.get()) ||
      entries_.at(source_browser.get()).browser_closed ||
      source_browser->GetTabStripModel()->GetIndexOfWebContents(source.get()) ==
          TabStripModel::kNoTab ||
      (require_selection &&
       (selected_ != source_browser.get() ||
        source_browser->GetTabStripModel()->GetActiveWebContents() !=
            source.get())))
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return false;
  const auto original =
      source_browser->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  if (IsWorkspaceRetiring(original) || IsWorkspaceRetiring(destination_path) ||
      destination_path == original ||
      std::ranges::none_of(catalog->items, [&](const Workspace& item) {
        return root.AppendASCII(item.profile_directory) == original;
      }))
    return false;
  const auto destination =
      std::ranges::find(catalog->items, destination_id, &Workspace::id);
  return destination != catalog->items.end() &&
         destination_path == root.AppendASCII(destination->profile_directory);
}
void FlyWindowController::ChooseTabMoveDestination(content::WebContents* source,
                                                   std::string action) {
  if (!source || source != move_source_.get() ||
      !TabMoveDestinationAvailable(action))
    return;
  const auto id = action.substr(5);
  const auto catalog = workspaces_.Read();
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  const auto path =
      g_browser_process->profile_manager()->user_data_dir().AppendASCII(
          item->profile_directory);
  auto source_browser = move_source_browser_;
  auto contents = move_source_;
  tab_move_ = std::make_unique<TabMoveTransaction>(
      source_browser.get(), source, path,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             base::WeakPtr<BrowserWindowInterface> browser,
             base::WeakPtr<content::WebContents> contents,
             const std::string& id, const base::FilePath& path,
             bool require_selection) {
            return window && window->ValidateTabMove(browser, contents, id,
                                                     path, require_selection);
          },
          weak_factory_.GetWeakPtr(), source_browser, contents, id, path),
      base::BindOnce(&FlyWindowController::FinishTabMove,
                     weak_factory_.GetWeakPtr()));
  status_->SetText(u"Preparing destination workspace…");
  tab_move_->Start();
}
void FlyWindowController::FinishTabMove(
    bool success,
    base::WeakPtr<BrowserWindowInterface> destination,
    base::WeakPtr<content::WebContents> contents) {
  tab_move_.reset();
  move_source_browser_.reset();
  move_source_.reset();
  ScheduleWorkspaceRefresh();
  if (success && !closing_ && destination && contents &&
      !destination->IsDeleteScheduled()) {
    auto* tabs = destination->GetTabStripModel();
    const auto index = tabs->GetIndexOfWebContents(contents.get());
    if (index != TabStripModel::kNoTab) {
      tabs->ActivateTabAt(index);
      Select(destination.get(), true);
    }
  } else if (!closing_) {
    // An unrelated native close can destroy the source while preparation is
    // pending. Complete its deferred fallback only after releasing the move.
    if (!selected_) {
      for (const auto& [browser, entry] : entries_) {
        if (!entry.browser_closed && entry.widget &&
            !browser->IsDeleteScheduled()) {
          Select(browser, true);
          break;
        }
      }
    }
    status_->SetText(u"Tab move cancelled. The destination or source changed.");
  }
}

void FlyWindowController::OnWorkspacesChanged() {
  ScheduleWorkspaceRefresh();
  UpdateWelcome();
  if (tab_move_picker_->active()) {
    if (CanMoveTab(move_source_browser_.get(), move_source_.get()))
      tab_move_picker_->UpdateItems(TabMoveEntries(), false);
    else
      tab_move_picker_->Cancel();
  }
  if (!workspace_picker_->active())
    return;
  if (!CanShowWorkspacePicker())
    workspace_picker_->Cancel(true);
  else
    workspace_picker_->UpdateItems(WorkspaceEntries(), false);
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
    OpenNativeBlankTab(selected_);
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

std::string FlyWindowController::NextWorkspaceName() const {
  const auto catalog = workspaces_.Read();
  std::string name;
  size_t number = catalog ? catalog->items.size() + 1 : 1;
  do {
    name = "Workspace " + base::NumberToString(number++);
  } while (catalog &&
           std::ranges::any_of(catalog->items, [&](const Workspace& item) {
             return item.name == name;
           }));
  return name;
}

void FlyWindowController::CreateWorkspace(std::string name) {
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
  if (name.empty())
    name = NextWorkspaceName();
  const auto added = workspaces_.Add(name, path.BaseName().AsUTF8Unsafe());
  if (!added) {
    return;
  }
  ScheduleWorkspaceRefresh();
  OpenWorkspace(added->id);
}

bool FlyWindowController::WorkspaceMatchesProfile(std::string_view id,
                                                  Profile* profile) const {
  const auto catalog = workspaces_.Read();
  if (!profile || profile->IsOffTheRecord() || !catalog ||
      !profile->AllowsBrowserWindows())
    return false;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  return item != catalog->items.end() &&
         profile->GetPath() == g_browser_process->profile_manager()
                                  ->user_data_dir()
                                  .AppendASCII(item->profile_directory);
}

void FlyWindowController::OnProfileReady(std::string id, Profile* profile) {
  creating_profile_ = false;
  if (closing_ || !WorkspaceMatchesProfile(id, profile)) {
    return;
  }
  BrowserWindowCreateParams params(*profile, true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params),
                      base::BindOnce(
                          [](base::WeakPtr<FlyWindowController> window,
                             std::string id, BrowserWindowInterface* browser) {
                            if (!browser)
                              return;
                            if (!window || window->closing_ ||
                                !window->WorkspaceMatchesProfile(
                                    id, browser->GetProfile())) {
                              browser->GetWindow()->Close();
                              return;
                            }
                            OpenNativeBlankTab(browser);
                            window->Select(browser, true);
                          },
                          weak_factory_.GetWeakPtr(), std::move(id)));
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
    CancelWorkspaceRename();
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->Cancel();
    tab_sidebar_->SetBrowser(nullptr);
    status_bar_->SetBrowser(nullptr, u"");
    selected_ = nullptr;
    preferences_button_->SetEnabled(false);
    if (it->second.widget) {
      it->second.host->SetVisible(false);
      it->second.widget->Hide();
    }
    // A committed move selects its prepared destination after native contents
    // destruction. An intermediate normal-profile fallback would persist a
    // private move's context into the normal workspace selection.
    if (!closing_ &&
        !(tab_move_ && move_source_browser_.get() == browser)) {
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
      // Electron keeps the workspace whose last tab closed and shows its
      // empty workspace view; leaving private mode returns to the window it
      // came from.
      if (!choose(true) && private_return &&
          entries_.contains(private_return.get()) &&
          !entries_.at(private_return.get()).browser_closed)
        Select(private_return.get(), true);
      // The empty view keeps its workspace profile loaded, so reopening it
      // reuses the profile instead of racing its destruction.
      if (!selected_ && !workspace_close_ &&
          !closed_profile->IsOffTheRecord())
        welcome_keep_alive_ = ScopedProfileKeepAlive::TryAcquire(
            closed_profile, ProfileKeepAliveOrigin::kChromeViewsDelegate);
    }
    UpdateModeLabel();
    UpdateWelcome();
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
  return browser && browser->GetProfile()->AllowsBrowserWindows() &&
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
  BrowserWindowCreateParams params(*private_profile, true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params), base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         BrowserWindowInterface* private_browser) {
        if (!window || window->closing_ || !private_browser)
          return;
        OpenNativeBlankTab(private_browser);
        window->Select(private_browser, true);
      }, weak_factory_.GetWeakPtr()));
}

void FlyWindowController::ArmCloseCallback() {
  desktop_->MakeCloseSynchronous(base::BindOnce(
      &FlyWindowController::RequestClose, weak_factory_.GetWeakPtr()));
}
void FlyWindowController::RequestClose(views::Widget::ClosedReason reason) {
  tab_move_.reset();
  workspace_close_.reset();
  CancelWorkspaceRename();
  ArmCloseCallback();
  if (closing_) {
    return;
  }
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  closing_ = true;
  suspension_timer_.Stop();
  ResumeAllTabs();
  if (entries_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyWindowController::FinishClose,
                                  weak_factory_.GetWeakPtr()));
  } else {
    // This desktop represents the whole Fly application. Announce quit before
    // closing profile windows so Chromium preserves its last-opened profiles
    // and session services. The coordinator still honours unload cancellation.
    chrome::AttemptExit();
  }
}
void FlyWindowController::OnClosingAllBrowsers(bool closing) {
  if (closing) {
    tab_move_.reset();
    workspace_close_.reset();
    suspension_timer_.Stop();
    ResumeAllTabs();
    CancelWorkspaceRename();
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->Cancel();
  }
  closing_ = closing;
  if (!closing)
    UpdateSuspensionPolicy();
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
  if (widget == sidebar_widget_.get()) {
    ClearSidebarViews();
    return;
  }
  if (widget == desktop_.get()) {
    StopSidebarFocusObservation();
    clock_timer_.Stop();
    suspension_timer_.Stop();
    preferences_registrar_.RemoveAll();
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
  // A blurred window can no longer see the modifier's release.
  if (widget == desktop_.get() && !active)
    SetTabNumbers(false);
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
  if (action == "toggle-history") {
    ToggleHistory(browser);
    return true;
  }
  if (action == "toggle-address") {
    ToggleAddress(browser);
    return true;
  }
  if (RunProductAction(browser, action))
    return true;
  if (action == "move-tab-to-workspace")
    return ShowTabMovePicker(
        browser, browser->GetTabStripModel()->GetActiveWebContents());
  if (action == "hibernate-tabs" || action == "wake-all-tabs") {
    std::vector<base::WeakPtr<content::WebContents>> tabs;
    const auto workspace = entries_.at(browser).workspace_id;
    for (const auto& [window, entry] : entries_) {
      if (entry.browser_closed ||
          (action == "wake-all-tabs" &&
           (workspace.empty() ? window->GetProfile() != browser->GetProfile()
                              : entry.workspace_id != workspace)))
        continue;
      auto* model = window->GetTabStripModel();
      for (int index = 0; index < model->count(); ++index)
        tabs.push_back(model->GetWebContentsAt(index)->GetWeakPtr());
    }
    for (const auto& tab : tabs) {
      if (!tab)
        continue;
      if (action == "hibernate-tabs")
        RequestNativeTabSuspension(tab.get());
      else
        ResumeNativeTab(tab.get());
    }
    return true;
  }
  if (action == "toggle-global-audio-mute" || action == "toggle-global-media") {
    // Snapshot identities before notifying native tab observers. Never iterate
    // the workspace/window map while a tab mutation may reenter its owner.
    std::vector<base::WeakPtr<content::WebContents>> contents;
    bool any_unmuted = false;
    for (const auto& [window, entry] : entries_) {
      if (entry.browser_closed)
        continue;
      auto* model = window->GetTabStripModel();
      for (int index = 0; index < model->count(); ++index) {
        auto* tab = model->GetWebContentsAt(index);
        contents.push_back(tab->GetWeakPtr());
        any_unmuted |= !tab->IsAudioMuted();
      }
    }
    if (action == "toggle-global-media") {
      std::vector<std::pair<base::WeakPtr<content::WebContents>, bool>> sessions;
      for (const auto& tab : contents) {
        auto* session = tab ? content::MediaSession::GetIfExists(tab.get())
                            : nullptr;
        if (!session)
          continue;
        const auto info = session->GetMediaSessionInfoSync();
        if (info && info->is_controllable)
          sessions.emplace_back(
              tab, info->playback_state ==
                       media_session::mojom::MediaPlaybackState::kPaused);
      }
      // Decide before dispatch: resuming one session may natively change audio
      // focus in another. UI requests retain site Media Session handlers and
      // Chromium's normal player fallback, without injecting media scripts.
      for (const auto& [tab, resume] : sessions) {
        auto* session = tab ? content::MediaSession::GetIfExists(tab.get())
                            : nullptr;
        if (!session)
          continue;
        if (resume) {
          ResumeNativeTab(tab.get());
          session->Resume(content::MediaSession::SuspendType::kUI);
        } else {
          session->Suspend(content::MediaSession::SuspendType::kUI);
        }
      }
      return true;
    }
    for (const auto& tab : contents) {
      if (tab)
        SetTabAudioMuted(tab.get(), any_unmuted,
                         TabMutedReason::kAudioIndicator, std::string());
    }
    return !contents.empty();
  }
  if (action == "add-bookmark") {
    auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
    return contents && bookmarks_->Add(contents->GetLastCommittedURL().spec(),
                                      base::UTF16ToUTF8(contents->GetTitle()));
  }
  if (action == "toggle-bookmarks") {
    if (!bookmarks_->Read())
      return false;
    CancelHistory();
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    keyboard_editor_->Cancel();
    const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
    bookmark_picker_->ToggleItems(
        browser->GetTabStripModel()->GetActiveWebContents(), BookmarkEntries(),
        config.shortcuts.at("toggle-bookmarks"),
        u"Search bookmarks and frequent sites", bookmarks_->CanEdit());
    LoadHistory(HistorySurface::kBookmarks);
    return true;
  }
  if (action == "toggle-palette") {
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    hints_->Cancel();
    keyboard_editor_->Cancel();
    palette_->SetRecent(preferences_.ReadRecentCommands());
    const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
    auto items = CommandCentreCatalog(config);
    for (auto& entry : ExtensionCommandEntries(browser))
      items.push_back(std::move(entry));
    palette_->ToggleItems(
        browser->GetTabStripModel()->GetActiveWebContents(), std::move(items),
        config.shortcuts.at("toggle-palette"),
        u"Search commands, shortcuts, or type shell utility...", false);
    return true;
  }
  if (action == "toggle-private-mode") {
    TogglePrivate(browser);
    return true;
  }
  if (action == "trigger-hints") {
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->Cancel();
    hints_->Toggle(browser->GetTabStripModel()->GetActiveWebContents());
    return true;
  }
  if (action == "toggle-theme")
    return preferences_.ToggleTheme();
  if (action == "toggle-zen-mode" || action == "toggle-sidebar-pin") {
    const auto collapse = preferences_.ReadSidebarAutoCollapse();
    return collapse && preferences_.SetSidebarAutoCollapse(!*collapse);
  }
  if (action == "toggle-window-controls") {
    const auto visible = preferences_.ReadWindowControlsVisible();
    return visible && preferences_.SetWindowControlsVisible(!*visible);
  }
  if (action == "toggle-status-bar") {
    const auto visible = preferences_.ReadStatusBarVisible();
    return visible && preferences_.SetStatusBarVisible(!*visible);
  }
  if (action == "cycle-clock-format")
    return preferences_.CycleClockFormat();
  if (action == "toggle-clock") {
    const auto clock = preferences_.ReadClock();
    return clock && preferences_.SetClockVisible(!clock->visible);
  }
  if (action == "toggle-help" || action == "open-keyboard-settings") {
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->SetPrivateMode(browser->GetProfile()->IsOffTheRecord());
    if (action == "toggle-help" || !keyboard_editor_->active())
      keyboard_editor_->Toggle(browser->GetTabStripModel()->GetActiveWebContents());
    RefreshDesktopIntegration();
    if (action == "open-keyboard-settings")
      keyboard_editor_->ShowKeyboard();
    return true;
  }
  if (action == "toggle-dashboard") {
    ToggleWorkspacePicker();
    return true;
  }
  if (action == "new-workspace") {
    PromptNewWorkspace();
    return true;
  }
  if (action == "focus-sidebar") {
    FocusSidebar();
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
void FlyWindowController::TrackTabNumbers(bool key_down,
                                          ui::KeyboardCode code,
                                          ui::DomCode dom_code,
                                          int flags,
                                          bool repeat,
                                          bool passthrough) {
  const bool modifier =
      key_down && (code == ui::VKEY_CONTROL || code == ui::VKEY_MENU ||
                   code == ui::VKEY_SHIFT || code == ui::VKEY_LWIN ||
                   code == ui::VKEY_RWIN);
  // Right Alt is AltGr on many layouts; in passthrough the page receives
  // every shortcut, so the numbers would point at nothing.
  if (!modifier || passthrough || dom_code == ui::DomCode::ALT_RIGHT) {
    SetTabNumbers(false);
    return;
  }
  if (repeat)
    return;
  std::vector<std::string> held;
  const auto add = [&](bool on, const char* name) {
    if (on)
      held.push_back(name);
  };
  add(code == ui::VKEY_CONTROL || (flags & ui::EF_CONTROL_DOWN), "Ctrl");
  add(code == ui::VKEY_MENU || (flags & ui::EF_ALT_DOWN), "Alt");
  add(code == ui::VKEY_SHIFT || (flags & ui::EF_SHIFT_DOWN), "Shift");
  add(code == ui::VKEY_LWIN || code == ui::VKEY_RWIN ||
          (flags & ui::EF_COMMAND_DOWN),
      "Meta");
  const auto tabs = commands_.Read().value_or(DefaultKeyboardConfig()).tabs;
  if (tabs.empty() || base::JoinString(held, "+") != tabs) {
    SetTabNumbers(false);
    return;
  }
  // The delay keeps quick chords such as Alt+S from flashing them.
  if (!tab_numbers_shown_ && !tab_numbers_timer_.IsRunning()) {
    tab_numbers_timer_.Start(
        FROM_HERE, base::Milliseconds(150),
        base::BindOnce(&FlyWindowController::SetTabNumbers,
                       base::Unretained(this), true));
  }
}
void FlyWindowController::SetTabNumbers(bool shown) {
  tab_numbers_timer_.Stop();
  if (shown == tab_numbers_shown_)
    return;
  tab_numbers_shown_ = shown;
  if (tab_sidebar_)
    tab_sidebar_->SetNumbersVisible(shown);
}
void FlyWindowController::OnKeyEvent(ui::KeyEvent* event) {
  TrackTabNumbers(event->type() == ui::EventType::kKeyPressed,
                  event->key_code(), event->code(), event->flags(),
                  event->is_repeat(), false);
  if ((rename_widget_ && !rename_widget_->IsClosed()) ||
      (remove_widget_ && !remove_widget_->IsClosed()) ||
      (history_clear_widget_ && !history_clear_widget_->IsClosed()))
    return;
  if (diagnostics_hub_ && diagnostics_hub_->active()) {
    if (diagnostics_hub_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (address_picker_->active()) {
    if (address_picker_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (tab_move_picker_->active()) {
    if (tab_move_picker_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (workspace_picker_->active()) {
    if (workspace_picker_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (history_picker_->active()) {
    if (history_picker_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
  if (bookmark_picker_->active()) {
    if (bookmark_picker_->HandleOverlayKey(*event)) {
      event->SetHandled();
      event->StopPropagation();
    }
    return;
  }
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
  if (event->key_code() == ui::VKEY_ESCAPE && sidebar_focus_inside_) {
    ReturnFocusFromSidebar();
    event->SetHandled();
    event->StopPropagation();
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
      } else if (key.action == "toggle-dashboard") {
        ToggleWorkspacePicker();
      } else if (key.action == "new-workspace") {
        PromptNewWorkspace();
      } else if (key.action == "spawn-tab" ||
                 key.action == "toggle-bookmarks" ||
                 key.action == "toggle-palette") {
        RunFromEmptyWorkspace(key.action);
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

std::u16string FlyWindowController::status_text_for_testing() const {
  return status_ ? std::u16string(status_->GetText()) : std::u16string();
}
void FlyWindowController::ShowStatus(std::u16string text) {
  RecordDiagnostic(
      text.starts_with(u"Update failed") || text.starts_with(u"Could not")
          ? "warn"
          : "info",
      "status", text);
  if (status_)
    status_->SetText(std::move(text));
}
std::vector<Profile*> FlyWindowController::WorkspaceProfiles() const {
  std::vector<Profile*> profiles;
  for (const auto& [browser, entry] : entries_) {
    if (entry.browser_closed)
      continue;
    auto* profile = browser->GetProfile();
    if (std::ranges::find(profiles, profile) == profiles.end())
      profiles.push_back(profile);
  }
  return profiles;
}
void FlyWindowController::ApplyProfilePreferencesEverywhere() {
  // Only rewrite per-profile state when the relevant product settings move.
  const std::string key =
      (preferences_.ReadBool("web_notifications").value_or(true) ? "1" : "0") +
      preferences_.ReadString("spellchecker_language").value_or("");
  if (applied_profile_preferences_ == key)
    return;
  applied_profile_preferences_ = key;
  for (auto* profile : WorkspaceProfiles())
    ApplyProfilePreferences(profile, preferences_);
  if (status_bar_)
    status_bar_->SetNotificationsEnabled(
        preferences_.ReadBool("web_notifications").value_or(true));
}
void FlyWindowController::ToggleDownloads(BrowserWindowInterface* browser) {
  if (closing_)
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  downloads_picker_->ToggleItems(
      browser->GetTabStripModel()->GetActiveWebContents(),
      DownloadEntries(WorkspaceProfiles()),
      config.shortcuts.at("toggle-downloads"), u"Search downloads", true,
      true);
}
void FlyWindowController::ShowDiagnostics() {
  if (closing_ || !diagnostics_hub_)
    return;
  hints_->Cancel();
  palette_->Cancel();
  keyboard_editor_->Cancel();
  diagnostics_hub_->Toggle();
}
DiagnosticsSession FlyWindowController::CurrentDiagnostics() {
  DiagnosticsSession session;
  if (const auto catalog = workspaces_.Read()) {
    session.workspaces = catalog->items.size();
    for (const auto& item : catalog->items)
      if (item.id == catalog->active_id)
        session.active_workspace = base::UTF8ToUTF16(item.name);
  }
  for (const auto& [browser, entry] : entries_) {
    if (entry.browser_closed)
      continue;
    auto* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      auto* contents = model->GetWebContentsAt(i);
      ++session.tabs;
      if (NativeTabSuspensionRequested(contents))
        ++session.suspended;
      else if (!contents->WasDiscarded())
        ++session.loaded;
    }
  }
  session.recording =
      preferences_.ReadBool("enable_diagnostics").value_or(false);
  session.events = diagnostics_events_;
  return session;
}
void FlyWindowController::RecordDiagnostic(std::string level,
                                           std::string category,
                                           std::u16string message) {
  if (!preferences_.ReadBool("enable_diagnostics").value_or(false))
    return;
  diagnostics_events_.push_back(
      {base::Time::Now(), std::move(level), std::move(category),
       std::move(message)});
  if (diagnostics_events_.size() > 500)
    diagnostics_events_.erase(diagnostics_events_.begin());
}
void FlyWindowController::CheckUpdates(bool manual) {
  if (manual)
    ShowStatus(u"Checking for updates…");
  CheckForUpdates(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window, bool manual,
         UpdateCheckResult result) {
        if (!window || window->closing_)
          return;
        if (!result.ok) {
          window->keyboard_editor_->SetUpdateStatus(
              u"Could not check for updates.", "");
          if (manual)
            window->ShowStatus(u"Could not check for updates.");
          return;
        }
        window->update_ = result;
        if (!result.newer) {
          window->keyboard_editor_->SetUpdateStatus(
              u"Fly " + base::UTF8ToUTF16(std::string_view(kFlyVersion)) +
                  u" is the latest version.",
              "");
          if (manual)
            window->ShowStatus(u"Fly " + base::UTF8ToUTF16(std::string_view(kFlyVersion)) +
                               u" is the latest version.");
          return;
        }
        window->keyboard_editor_->SetUpdateStatus(
            u"Fly " + base::UTF8ToUTF16(result.latest) +
                (result.package.is_valid()
                     ? u" is available. Install it here; Fly restarts with "
                       u"your tabs."
                     : u" is available as a download."),
            result.latest);
        window->ShowStatus(u"Fly " + base::UTF8ToUTF16(result.latest) +
                           u" is available. Check for Updates opens it.");
        if (manual && window->selected_) {
          NavigateParams params(window->selected_, result.page,
                                ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
          params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
          Navigate(&params);
        }
      },
      weak_factory_.GetWeakPtr(), manual));
}
void FlyWindowController::MaybeShowWhatsNew() {
  if (whats_new_checked_ || !selected_ ||
      selected_->GetProfile()->IsOffTheRecord())
    return;
  whats_new_checked_ = true;
  const auto seen = preferences_.ReadString("last_seen_version");
  if (!seen || !preferences_.CanEdit() || *seen == kFlyVersion)
    return;
  preferences_.SetString("last_seen_version", kFlyVersion);
  // A new profile has nothing to catch up on; an upgraded one does.
  if (seen->empty() || base::Version(*seen) >= base::Version(kFlyVersion))
    return;
  ResolveWhatsNewURL(*seen, base::BindOnce(&FlyWindowController::OpenInNewTab,
                                           weak_factory_.GetWeakPtr()));
}
void FlyWindowController::OpenInNewTab(GURL url) {
  if (closing_ || !selected_)
    return;
  NavigateParams params(selected_, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}
bool FlyWindowController::RunProductAction(BrowserWindowInterface* browser,
                                           std::string_view action) {
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  const auto open = [&](const GURL& url, Profile* profile = nullptr) {
    NavigateParams params(profile ? profile : browser->GetProfile(), url,
                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    if (!profile)
      params.browser = browser;
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
  };
  if (action == "toggle-menu-bar" ||
      action == "toggle-native-menu-visibility") {
    auto point = desktop_->GetContentsView()->GetBoundsInScreen().origin();
    point.Offset(14, 14);
    settings_menu_->Show(desktop_.get(), point);
    return true;
  }
  if (action == "reload-user-content") {
    user_content_->Reload();
    return true;
  }
  if (action == "open-extension-actions") {
    ToggleExtensionActions(browser);
    return true;
  }
  if (action == "open-task-manager") {
    chrome::OpenTaskManager(browser);
    return true;
  }
  if (action == "toggle-downloads") {
    ToggleDownloads(browser);
    return true;
  }
  if (action == "toggle-reader-view") {
    // Messages as Electron Fly's toggleReaderViewOnActiveTab.
    ToggleReaderView(
        contents,
        base::BindOnce(
            [](base::WeakPtr<FlyWindowController> window,
               base::WeakPtr<content::WebContents> contents,
               ReaderResult result) {
              if (!window)
                return;
              if (!result.error.empty()) {
                window->ShowStatus(u"Reader View: " + result.error);
              } else if (result.active) {
                window->ShowStatus(
                    u"Reader view active (Escape to exit)" +
                    (result.title.empty()
                         ? std::u16string()
                         : u": " + result.title.substr(0, 45) + u"..."));
              } else {
                window->ShowStatus(u"Reader View Closed");
              }
              window->SetReaderActive(contents.get(),
                                      result.active && result.error.empty());
            },
            weak_factory_.GetWeakPtr(),
            contents ? contents->GetWeakPtr()
                     : base::WeakPtr<content::WebContents>()));
    return true;
  }
  if (action == "open-scripts-dir" || action == "open-styles-dir" ||
      action == "open-script-storage-dir") {
    OpenUserContentDirectory(browser->GetProfile(),
                             action == "open-scripts-dir"  ? "scripts"
                             : action == "open-styles-dir" ? "styles"
                                                           : "script-storage");
    return true;
  }
  if (action == "test-google-signin") {
    // A fresh off-the-record profile: no workspace cookies or tokens.
    open(GURL("https://accounts.google.com/"),
         browser->GetProfile()->GetOriginalProfile()->GetPrimaryOTRProfile(
             /*create_if_needed=*/true));
    return true;
  }
  if (action == "check-for-updates") {
    CheckUpdates(true);
    return true;
  }
  if (action == "show-whats-new") {
    ResolveWhatsNewURL("", base::BindOnce(&FlyWindowController::OpenInNewTab,
                                          weak_factory_.GetWeakPtr()));
    return true;
  }
  if (action == "open-diagnostics") {
    ShowDiagnostics();
    return true;
  }
  if (action == "toggle-diagnostics-recording") {
    const bool enabled =
        !preferences_.ReadBool("enable_diagnostics").value_or(false);
    if (preferences_.SetBool("enable_diagnostics", enabled)) {
      if (!enabled)
        diagnostics_events_.clear();
      ShowStatus(enabled ? u"Diagnostics recording on (memory only)."
                         : u"Diagnostics recording off; events cleared.");
    }
    return true;
  }
  if (action == "toggle-notifications") {
    const bool enabled =
        !preferences_.ReadBool("web_notifications").value_or(true);
    if (preferences_.SetBool("web_notifications", enabled))
      ShowStatus(enabled ? u"Web notifications on." : u"Web notifications off.");
    return true;
  }
  if (action == "focus-nav-buttons") {
    FocusSidebar();
    if (new_tab_button_ && sidebar_content_->Contains(
            desktop_->GetFocusManager()->GetFocusedView()))
      new_tab_button_->RequestFocus();
    return true;
  }
  return false;
}

namespace {
// Footer extension action: icon with the action's badge text.
class ExtensionActionButton : public FlyButton {
  METADATA_HEADER(ExtensionActionButton, FlyButton)

 public:
  ExtensionActionButton(PressedCallback callback,
                        std::string id,
                        const gfx::Image& icon,
                        std::u16string title,
                        std::string badge)
      : FlyButton(std::move(callback), u""), id_(std::move(id)),
        badge_(std::move(badge)) {
    SetCustomPadding(gfx::Insets());
    SetPreferredSize(gfx::Size(20, 18));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    if (!icon.IsEmpty())
      views::LabelButton::SetImageModel(
          STATE_NORMAL,
          ui::ImageModel::FromImageSkia(
              gfx::ImageSkiaOperations::CreateResizedImage(
                  icon.AsImageSkia(), skia::ImageOperations::RESIZE_BEST,
                  gfx::Size(14, 14))));
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
  }
  const std::string& id() const { return id_; }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    FlyButton::PaintButtonContents(canvas);
    if (badge_.empty())
      return;
    const auto text = base::UTF8ToUTF16(badge_.substr(0, 4));
    const auto font = FlyFont(FlyFace::kMono, 7, gfx::Font::Weight::BOLD);
    const int width = std::min(this->width(), gfx::GetStringWidth(text, font) + 4);
    const gfx::Rect box(this->width() - width, height() - 9, width, 9);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(GetFlyColors().accent);
    canvas->DrawRoundRect(gfx::RectF(box), 2, flags);
    canvas->DrawStringRectWithFlags(text, font, SK_ColorWHITE, box,
                                    gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const std::string id_;
  const std::string badge_;
};
BEGIN_METADATA(ExtensionActionButton)
END_METADATA
}  // namespace

views::View* FlyWindowController::extension_actions_for_testing() const {
  return status_bar_ ? status_bar_->extensions_container() : nullptr;
}
void FlyWindowController::ScheduleExtensionRefresh() {
  if (extension_refresh_pending_ || closing_)
    return;
  extension_refresh_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<FlyWindowController> window) {
                       if (window) {
                         window->extension_refresh_pending_ = false;
                         window->RefreshExtensionActions();
                       }
                     },
                     weak_factory_.GetWeakPtr()));
}
void FlyWindowController::RefreshExtensionActions() {
  if (closing_ || !status_bar_ || !extensions_)
    return;
  auto* container = status_bar_->extensions_container();
  // Buttons may be the target of the click that led here: rebuild only after
  // the input has been dispatched (this runs from a posted task).
  container->RemoveAllChildViews();
  if (!selected_)
    return;
  auto* contents = selected_->GetTabStripModel()->GetActiveWebContents();
  for (const auto* extension :
       extensions_->ActionExtensions(selected_->GetProfile())) {
    const std::string id = extension->id();
    container->AddChildView(std::make_unique<ExtensionActionButton>(
        base::BindRepeating(
            [](base::WeakPtr<FlyWindowController> window, std::string id) {
              if (!window || !window->selected_ || window->closing_)
                return;
              for (const auto& child :
                   window->status_bar_->extensions_container()->children()) {
                auto* button =
                    views::AsViewClass<ExtensionActionButton>(child.get());
                if (button && button->id() == id) {
                  window->extensions_->RunAction(window->selected_, id,
                                                 button);
                  return;
                }
              }
            },
            weak_factory_.GetWeakPtr(), id),
        id, extensions_->ActionIcon(contents, *extension),
        extensions_->ActionTitle(contents, *extension),
        extensions_->ActionBadge(contents, *extension)));
  }
  container->InvalidateLayout();
  status_bar_->InvalidateLayout();
}
void FlyWindowController::ToggleExtensionActions(
    BrowserWindowInterface* browser) {
  if (closing_)
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  downloads_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  extensions_picker_->ToggleItems(
      browser->GetTabStripModel()->GetActiveWebContents(),
      extensions_->ActionEntries(browser->GetProfile()),
      config.shortcuts.at("open-extension-actions"), u"Search extensions",
      false, true);
}
void FlyWindowController::ChooseExtensionAction(content::WebContents*,
                                                std::string action) {
  if (closing_ || !selected_)
    return;
  if (action == "extensions-settings" || action == "extensions-open-settings") {
    InvokeNativeCommand(selected_, "toggle-help");
    keyboard_editor_->ShowExtensions();
    return;
  }
  if (action.starts_with("extension-options:")) {
    extensions_->OpenOptions(
        selected_, action.substr(std::string_view("extension-options:").size()));
    return;
  }
  if (action == "extensions-reload") {
    ShowStatus(base::UTF8ToUTF16(
        "Extensions reloaded." +
        std::string(extensions_->ReloadUnpacked() ? "" : " None to reload.")));
    return;
  }
  if (action == "extensions-page" || action == "extensions-store") {
    OpenInNewTab(GURL(action == "extensions-page"
                          ? "chrome://extensions/"
                          : "https://chromewebstore.google.com/"));
    return;
  }
  const std::string id = action.substr(std::string_view("extension:").size());
  views::View* anchor = status_bar_;
  if (status_bar_ && status_bar_->GetVisible()) {
    for (const auto& child : status_bar_->extensions_container()->children())
      if (auto* button = views::AsViewClass<ExtensionActionButton>(child.get());
          button && button->id() == id)
        anchor = button;
  } else {
    anchor = desktop_->GetContentsView();
  }
  extensions_->RunAction(selected_, id, anchor);
}

void FlyWindowController::QueueLegacyImport(BrowserWindowInterface* browser) {
  if (legacy_import_) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyWindowController::ApplyLegacyImport,
                                  weak_factory_.GetWeakPtr(),
                                  browser->GetWeakPtr(), 0));
    return;
  }
  legacy_import_waiting_.push_back(browser->GetWeakPtr());
  if (legacy_import_loading_)
    return;
  legacy_import_loading_ = true;
  const auto file = g_browser_process->profile_manager()
                        ->user_data_dir()
                        .AppendASCII(kLegacyImportFile);
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath file) -> std::optional<base::DictValue> {
            std::string text;
            if (!base::ReadFileToStringWithMaxSize(file, &text,
                                                   64 * 1024 * 1024)) {
              if (base::PathExists(
                      file.DirName().AppendASCII(kLegacyImportWaitingFile)))
                return base::DictValue().Set(
                    "summary",
                    "Electron Fly was open, so its workspaces were not "
                    "imported. Close Electron Fly and restart Fly to import "
                    "them.");
              return std::nullopt;
            }
            return base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
          },
          file),
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window,
             std::optional<base::DictValue> state) {
            if (!window)
              return;
            window->legacy_import_ =
                state ? std::move(*state) : base::DictValue();
            if (const auto* summary =
                    window->legacy_import_->FindString("summary");
                summary && !window->legacy_import_->FindBool("announced"))
              window->ShowStatus(base::UTF8ToUTF16(*summary));
            for (auto& browser : window->legacy_import_waiting_)
              window->ApplyLegacyImport(browser, 0);
            window->legacy_import_waiting_.clear();
          },
          weak_factory_.GetWeakPtr()));
}
void FlyWindowController::ApplyLegacyImport(
    base::WeakPtr<BrowserWindowInterface> browser,
    int attempts) {
  if (!browser || closing_ || !legacy_import_)
    return;
  auto* profiles = legacy_import_->FindDict("profiles");
  Profile* profile = browser->GetProfile();
  const auto directory = profile->GetPath().BaseName().AsUTF8Unsafe();
  auto* entry = profiles ? profiles->FindDict(directory) : nullptr;
  if (!entry)
    return;
  auto* model = browser->GetTabStripModel();
  // Startup adds the browser's first tab after its window; wait for it.
  if (model->empty() && attempts < 20) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
        FROM_HERE,
        base::BindOnce(&FlyWindowController::ApplyLegacyImport,
                       weak_factory_.GetWeakPtr(), browser, attempts + 1),
        base::Milliseconds(100));
    return;
  }
  if (const auto* zoom = entry->FindDict("zoom")) {
    auto* map = content::HostZoomMap::GetDefaultForBrowserContext(profile);
    for (const auto [host, level] : *zoom)
      if (level.is_double() || level.is_int())
        map->SetZoomLevelForHost(host, level.GetDouble());
  }
  if (const auto* history = entry->FindList("history")) {
    if (auto* service = HistoryServiceFactory::GetForProfile(
            profile, ServiceAccessType::EXPLICIT_ACCESS)) {
      // Electron lists the newest visit first; adding the oldest first leaves
      // each page with its newest title.
      for (const auto& item : base::Reversed(*history)) {
        const auto* visit = item.GetIfDict();
        const auto* url = visit ? visit->FindString("url") : nullptr;
        if (!url || !GURL(*url).SchemeIsHTTPOrHTTPS())
          continue;
        const auto stamp = visit->FindDouble("timestamp");
        service->AddPage(GURL(*url),
                         stamp ? base::Time::FromMillisecondsSinceUnixEpoch(
                                     *stamp)
                               : base::Time::Now(),
                         history::SOURCE_BROWSED);
        if (const auto* title = visit->FindString("title"))
          service->SetPageTitle(GURL(*url), base::UTF8ToUTF16(*title));
      }
    }
  }
  if (const auto* tabs = entry->FindList("tabs"); tabs && !tabs->empty()) {
    // A lone blank startup tab gives way to the imported ones.
    content::WebContents* blank = nullptr;
    if (model->count() == 1) {
      const GURL& first = model->GetWebContentsAt(0)->GetVisibleURL();
      if (IsFlyNewTabPage(first) || first.SchemeIs(content::kChromeUIScheme))
        blank = model->GetWebContentsAt(0);
    }
    for (const auto& item : *tabs) {
      const auto* tab = item.GetIfDict();
      const auto* url = tab ? tab->FindString("url") : nullptr;
      if (!url || !GURL(*url).is_valid())
        continue;
      NavigateParams params(browser.get(), GURL(*url),
                            ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
      params.disposition = WindowOpenDisposition::NEW_BACKGROUND_TAB;
      Navigate(&params);
      if (tab->FindBool("pinned").value_or(false) &&
          params.navigated_or_inserted_contents) {
        const int index = model->GetIndexOfWebContents(
            params.navigated_or_inserted_contents);
        if (index != TabStripModel::kNoTab)
          model->SetTabPinned(index, true);
      }
    }
    if (blank && model->count() > 1) {
      const int index = model->GetIndexOfWebContents(blank);
      if (index != TabStripModel::kNoTab)
        model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_NONE);
    }
    if (!model->empty())
      model->ActivateTabAt(0);
  }
  profiles->Remove(directory);
  legacy_import_->Set("announced", true);
  std::string json;
  base::JSONWriter::WriteWithOptions(
      *legacy_import_, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  // One sequence keeps the snapshots in order, and shutdown waits for the
  // last one, so applied tabs and history are never added twice.
  if (!legacy_import_writer_) {
    legacy_import_writer_ = base::ThreadPool::CreateSequencedTaskRunner(
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::BLOCK_SHUTDOWN});
  }
  legacy_import_writer_->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath file, std::string json) {
            base::ImportantFileWriter::WriteFileAtomically(file, json);
          },
          g_browser_process->profile_manager()->user_data_dir().AppendASCII(
              kLegacyImportFile),
          std::move(json)));
}

// Electron highlights the reader button while the active tab shows the reader.
// The overlay can also close from the page (Escape, its close button), so the
// state is checked again while it is on.
void FlyWindowController::SetReaderActive(content::WebContents* contents,
                                          bool active) {
  if (!contents)
    return;
  if (active)
    reader_contents_ = contents->GetWeakPtr();
  else if (reader_contents_.get() == contents)
    reader_contents_ = nullptr;
  UpdateReaderButton();
}
void FlyWindowController::RefreshReaderState() {
  content::WebContents* active =
      selected_ ? selected_->GetTabStripModel()->GetActiveWebContents()
                : nullptr;
  reader_contents_ = nullptr;
  UpdateReaderButton();
  if (!active)
    return;
  QueryReaderView(
      active, base::BindOnce(
                  [](base::WeakPtr<FlyWindowController> window,
                     base::WeakPtr<content::WebContents> contents, bool open) {
                    if (window && contents && open)
                      window->SetReaderActive(contents.get(), true);
                  },
                  weak_factory_.GetWeakPtr(), active->GetWeakPtr()));
}
void FlyWindowController::UpdateReaderButton() {
  content::WebContents* active =
      selected_ ? selected_->GetTabStripModel()->GetActiveWebContents()
                : nullptr;
  // Another tab became active: its own reader state is checked below.
  if (reader_contents_.get() != active)
    reader_contents_ = nullptr;
  const bool on = active && reader_contents_.get() == active;
  if (reader_button_) {
    reader_button_->SetSelected(on);
    reader_button_->SetTooltipText(on ? u"Reader view active"
                                      : u"Toggle reader view");
  }
  if (!on) {
    reader_poll_.Stop();
    return;
  }
  if (!reader_poll_.IsRunning()) {
    reader_poll_.Start(
        FROM_HERE, base::Seconds(1),
        base::BindRepeating(
            [](base::WeakPtr<FlyWindowController> window) {
              if (!window || !window->selected_)
                return;
              auto* contents =
                  window->selected_->GetTabStripModel()->GetActiveWebContents();
              QueryReaderView(
                  contents,
                  base::BindOnce(
                      [](base::WeakPtr<FlyWindowController> window,
                         base::WeakPtr<content::WebContents> contents,
                         bool open) {
                        if (window && contents && !open)
                          window->SetReaderActive(contents.get(), false);
                      },
                      window, contents ? contents->GetWeakPtr()
                                       : base::WeakPtr<content::WebContents>()));
            },
            weak_factory_.GetWeakPtr()));
  }
}

void FlyWindowController::UpdateWelcome() {
  if (!welcome_)
    return;
  const bool show = !selected_ && !closing_;
  welcome_->SetVisible(show);
  if (!show)
    return;
  const auto catalog = workspaces_.Read();
  std::u16string name = u"Workspace";
  if (catalog)
    for (const auto& item : catalog->items)
      if (item.id == catalog->active_id)
        name = base::UTF8ToUTF16(item.name);
  const auto config = commands_.Read().value_or(DefaultKeyboardConfig());
  // Electron writes bindings as "Ctrl + T".
  const auto keys = [&](const char* action) {
    const auto found = config.shortcuts.find(action);
    if (found == config.shortcuts.end() || found->second.empty())
      return std::u16string();
    std::u16string text = base::UTF8ToUTF16(found->second.front());
    base::ReplaceSubstringsAfterOffset(&text, 0, u"+", u" + ");
    return text;
  };
  const auto run = [&](const char* action) {
    return base::BindRepeating(&FlyWindowController::RunFromEmptyWorkspace,
                               weak_factory_.GetWeakPtr(), std::string(action));
  };
  welcome_->Update(
      name, {{keys("spawn-tab"), u"Open New Tab", run("spawn-tab")},
             {keys("toggle-bookmarks"), u"Open Bookmarks",
              run("toggle-bookmarks")},
             {keys("toggle-palette"), u"Command Centre", run("toggle-palette")},
             {keys("toggle-dashboard"), u"Workspace Dashboard",
              run("toggle-dashboard")}});
}

// Actions of the empty workspace view. The dashboard needs no page; the others
// open the workspace with a new tab first.
void FlyWindowController::RunFromEmptyWorkspace(std::string action) {
  if (closing_)
    return;
  if (selected_) {
    InvokeNativeCommand(selected_, action);
    return;
  }
  if (action == "toggle-dashboard") {
    ToggleWorkspacePicker();
    return;
  }
  if (action != "spawn-tab")
    pending_empty_action_ = action;
  CreateTab();
}

void FlyWindowController::choose_workspace_for_testing(std::string action) {
  ChooseWorkspace(
      selected_ ? selected_->GetTabStripModel()->GetActiveWebContents()
                : nullptr,
      std::move(action));
}

// Electron's getExtensionPaletteCommands.
std::vector<PaletteEntry> FlyWindowController::ExtensionCommandEntries(
    BrowserWindowInterface* browser) {
  std::vector<PaletteEntry> entries;
  const auto add = [&](std::string action, std::string title, std::string desc,
                       std::string details, std::string tip,
                       std::vector<std::string> keywords) {
    PaletteEntry entry{action, action, std::move(title), "Extensions", {}, {}};
    entry.desc = std::move(desc);
    entry.details = std::move(details);
    entry.tip = std::move(tip);
    entry.keywords = std::move(keywords);
    entries.push_back(std::move(entry));
  };
  add("extensions-open-settings", "Extension Settings",
      "Enable Chrome extensions per workspace and see what they cost",
      "Opens the Extensions section in Preferences. uBlock Origin Lite runs in "
      "every workspace unless switched off there; each workspace keeps its own "
      "extensions.",
      "Install from Chrome Web Store, a ZIP or CRX package, or an unpacked "
      "folder.",
      {"extensions", "addons", "chrome", "plugins", "settings"});
  if (!extensions_->Enabled() || !browser)
    return entries;
  add("extensions-reload", "Reload Extensions",
      "Re-read extension code from disk in every workspace using it",
      "Reloads unpacked extensions in every workspace. Pages that already ran "
      "the old content scripts need a reload.",
      "Use this after replacing an extension folder with a newer version.",
      {"extensions", "reload", "refresh", "update"});
  auto* profile = browser->GetProfile();
  for (const auto* extension : extensions_->ActionExtensions(profile))
    add("extension:" + extension->id(), extension->name() + ": Open Popup",
        "Open the extension popup for the active tab",
        extension->name() + " " + extension->VersionString(),
        "Press Esc or click outside the popup to close it.",
        {"extension", "popup", base::ToLowerASCII(extension->name())});
  for (const auto& info : extensions_->List(profile)) {
    if (!info.enabled || !info.has_options)
      continue;
    const auto name = base::UTF16ToUTF8(info.name);
    add("extension-options:" + info.id, name + ": Options",
        "Open the extension options page in a new tab",
        "Options are stored per workspace, like everything else the extension "
        "saves.",
        "The options tab stays open like any other tab.",
        {"extension", "options", "settings", base::ToLowerASCII(name)});
  }
  return entries;
}

// Buttons of Preferences that act on the browser.
void FlyWindowController::RunPreferencesAction(std::string action) {
  if (closing_ || !selected_)
    return;
  auto* browser = selected_.get();
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  auto status = base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window, std::u16string text) {
        if (window)
          window->ShowStatus(std::move(text));
      },
      weak_factory_.GetWeakPtr());
  if (action == "toggle-private-mode") {
    keyboard_editor_->Cancel();
    TogglePrivate(browser);
  } else if (action == "clear-site-cookies") {
    ClearSiteCookies(browser->GetProfile(),
                     contents ? contents->GetLastCommittedURL() : GURL(),
                     std::move(status));
  } else if (action == "clear-cache") {
    ClearWorkspaceCache(browser->GetProfile(), std::move(status));
  } else if (action == "open-diagnostics" || action == "show-whats-new") {
    keyboard_editor_->Cancel();
    InvokeNativeCommand(browser, action);
  } else if (action == "check-for-updates") {
    keyboard_editor_->SetUpdateStatus(u"Checking for updates…", "");
    CheckUpdates(false);
  } else if (action == "install-update") {
    InstallUpdate();
  } else if (action == "restart") {
    chrome::AttemptRestart();
  } else if (action == "desktop-entry" || action == "desktop-default") {
    ChangeDesktopIntegration(action == "desktop-default");
  } else if (action == "extensions-open-dir") {
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce([]() {
          const auto dir = FlyExtensions::Folder();
          return base::CreateDirectory(dir) ? dir : base::FilePath();
        }),
        base::BindOnce(
            [](base::WeakPtr<Profile> profile, base::FilePath dir) {
              if (profile && !dir.empty())
                platform_util::OpenItem(profile.get(), dir,
                                        platform_util::OPEN_FOLDER, {});
            },
            browser->GetProfile()->GetWeakPtr()));
  }
}

void FlyWindowController::InstallUpdate() {
  if (update_installing_ || closing_)
    return;
  if (!update_.newer) {
    keyboard_editor_->SetUpdateStatus(u"Check for updates first.", "");
    return;
  }
  if (!update_.package.is_valid()) {
    // No native package in this release: open its page, as Electron's
    // "Download release".
    keyboard_editor_->Cancel();
    OpenInNewTab(update_.page);
    return;
  }
  update_installing_ = true;
  keyboard_editor_->SetUpdateStatus(
      u"Downloading Fly " + base::UTF8ToUTF16(update_.latest) + u"…", "");
  InstallNativeUpdate(
      update_,
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window, std::u16string error) {
            if (!window)
              return;
            window->update_installing_ = false;
            if (!error.empty()) {
              window->RecordDiagnostic("error", "update", error);
              window->keyboard_editor_->SetUpdateStatus(
                  u"Update failed: " + error, window->update_.latest);
              window->ShowStatus(u"Update failed: " + error);
              return;
            }
            window->keyboard_editor_->SetUpdateStatus(
                u"Fly " + base::UTF8ToUTF16(window->update_.latest) +
                    u" is installed. Restarting…",
                "");
            chrome::AttemptRestart();
          },
          weak_factory_.GetWeakPtr()));
}

namespace {
// Electron's Preferences → About → Desktop integration texts.
void ShowDesktopIntegration(KeyboardEditor& editor,
                            const DesktopIntegrationStatus& status,
                            std::u16string error) {
  std::u16string entry;
  std::u16string action;
  if (!status.entry.empty() && status.ours) {
    entry = u"Listed in Rofi and your desktop menu.";
    action = u"Remove from menu";
  } else if (!status.entry.empty()) {
    entry = u"Listed by the entry your installation provides (" +
            status.entry.LossyDisplayName() + u").";
  } else if (status.supported) {
    entry = u"Not listed yet. Fly can add an entry for this installation to "
            u"Rofi and your desktop menu.";
    action = u"Add to menu";
  } else {
    entry = u"No menu entry found. The package's install.sh adds one.";
  }
  if (!error.empty())
    entry = error + u". " + entry;
  const bool can_make_default =
      status.default_browser.has_value() &&
      (!status.entry.empty() || status.supported);
  std::u16string browser;
  if (!status.default_browser)
    browser = u"Install xdg-utils to let Fly read and change the default "
              u"browser.";
  else if (status.is_default)
    browser = u"Fly opens links from other applications.";
  else if (!status.default_browser->empty())
    browser = u"Links from other applications open in " +
              base::UTF8ToUTF16(std::string(base::RemoveSuffix(
                                    *status.default_browser, ".desktop")
                                    .value_or(*status.default_browser))) +
              u".";
  else
    browser = u"No default browser is set.";
  if (!status.is_default && can_make_default && status.entry.empty())
    browser += u" Making Fly the default also adds it to the menu.";
  editor.SetDesktopIntegration(entry, action, browser,
                               !status.is_default && can_make_default);
}
}  // namespace

void FlyWindowController::RefreshDesktopIntegration() {
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         DesktopIntegrationStatus status) {
        if (window && window->keyboard_editor_)
          ShowDesktopIntegration(*window->keyboard_editor_, status, u"");
      },
      weak_factory_.GetWeakPtr()));
}

void FlyWindowController::ChangeDesktopIntegration(bool make_default) {
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window, bool make_default,
         DesktopIntegrationStatus status) {
        if (!window)
          return;
        fly::ChangeDesktopIntegration(
            make_default ? DesktopIntegrationChange::kMakeDefault
            : status.ours ? DesktopIntegrationChange::kRemoveEntry
                          : DesktopIntegrationChange::kAddEntry,
            base::BindOnce(
                [](base::WeakPtr<FlyWindowController> window,
                   std::u16string error, DesktopIntegrationStatus status) {
                  if (window && window->keyboard_editor_)
                    ShowDesktopIntegration(*window->keyboard_editor_, status,
                                           error);
                },
                window));
      },
      weak_factory_.GetWeakPtr(), make_default));
}

// Electron's offerDesktopIntegration: once, on the first start of a package
// that no menu entry lists yet.
void FlyWindowController::MaybeOfferDesktopIntegration() {
  if (closing_ || !preferences_.CanEdit() ||
      preferences_.ReadBool("desktop_integration_offered").value_or(false))
    return;
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         DesktopIntegrationStatus status) {
        if (!window || window->closing_ || !status.supported ||
            !status.entry.empty())
          return;
        window->preferences_.SetBool("desktop_integration_offered", true);
        auto delegate = std::make_unique<views::DialogDelegate>();
        delegate->SetOwnershipOfNewWidget(
            views::Widget::InitParams::CLIENT_OWNS_WIDGET);
        delegate->SetModalType(ui::mojom::ModalType::kWindow);
        delegate->SetTitle(u"Add Fly to your applications");
        delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Add to menu");
        delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
        auto body = std::make_unique<views::View>();
        body->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 10));
        auto* message = body->AddChildView(std::make_unique<views::Label>(
            u"Add Fly to your applications menu?\n\nFly is running from its "
            u"package folder, so Rofi and your desktop menu do not list it "
            u"yet. Fly can add an entry that starts this installation.\n\nYou "
            u"can change this later in Preferences → About."));
        message->SetMultiLine(true);
        message->SetMaximumWidth(420);
        message->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        auto* make_default = body->AddChildView(std::make_unique<views::Checkbox>(
            u"Also make Fly the default browser"));
        delegate->SetContentsView(std::move(body));
        delegate->SetAcceptCallback(base::BindOnce(
            [](base::WeakPtr<FlyWindowController> window,
               views::Checkbox* make_default) {
              if (!window)
                return;
              fly::ChangeDesktopIntegration(
                  make_default->GetChecked()
                      ? DesktopIntegrationChange::kMakeDefault
                      : DesktopIntegrationChange::kAddEntry,
                  base::BindOnce(
                      [](base::WeakPtr<FlyWindowController> window,
                         std::u16string error, DesktopIntegrationStatus) {
                        if (window && !error.empty())
                          window->ShowStatus(error);
                      },
                      window));
            },
            window, make_default));
        window->offer_widget_.reset(views::DialogDelegate::CreateDialogWidget(
            delegate.get(), window->desktop_->GetNativeWindow(),
            window->desktop_->GetNativeView()));
        window->offer_delegate_ = std::move(delegate);
        window->offer_widget_->Show();
      },
      weak_factory_.GetWeakPtr()));
}
}  // namespace fly
