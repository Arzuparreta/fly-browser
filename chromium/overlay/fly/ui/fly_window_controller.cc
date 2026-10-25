// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_window_controller.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/check.h"
#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "extensions/common/extension.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/commands/calculator.h"
#include "fly/commands/keyboard_router.h"
#include "fly/preferences/search_service.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/diagnostics_hub.h"
#include "fly/ui/download_shelf.h"
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_browser_frame.h"
#include "fly/ui/fly_extensions.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/new_tab_ui.h"
#include "fly/ui/sidebar_panel.h"
#include "fly/ui/status_bar.h"
#include "fly/ui/tab_move_transaction.h"
#include "fly/ui/tab_sidebar.h"
#include "fly/ui/user_content.h"
#include "skia/ext/image_operations.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_targeter.h"
#include "ui/base/base_window.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/base/ime/input_method.h"
#include "ui/base/ime/text_input_client.h"
#include "ui/base/ime/text_input_type.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/events/event.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/animation/tween.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/text_utils.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/animation_builder.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/context_menu_controller.h"
#include "ui/views/controls/button/image_button.h"
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
// The workspace badge: a trailing number of one or two digits after the first
// letter ("Workspace 1" is "W1"), the first two digits of a longer number,
// otherwise the first two letters in capitals.
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

// The texts of Preferences → About → Desktop integration.
void ShowDesktopIntegrationIn(KeyboardEditor& editor,
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

// Footer extension action: icon with the action's badge text.
class ExtensionActionButton : public FlyButton {
  METADATA_HEADER(ExtensionActionButton, FlyButton)

 public:
  ExtensionActionButton(PressedCallback callback,
                        std::string id,
                        const gfx::Image& icon,
                        std::u16string title,
                        std::string badge)
      : FlyButton(std::move(callback), u""), id_(std::move(id)) {
    SetCustomPadding(gfx::Insets());
    SetPreferredSize(gfx::Size(20, 18));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    Update(icon, title, std::move(badge));
  }
  const std::string& id() const { return id_; }
  const std::string& badge() const { return badge_; }
  // Extensions update their action often (a blocker's count on every
  // request); only what changed is resized or repainted.
  void Update(const gfx::Image& icon,
              const std::u16string& title,
              std::string badge) {
    if (icon.IsEmpty() != icon_.IsEmpty() ||
        (!icon.IsEmpty() &&
         !icon.AsImageSkia().BackedBySameObjectAs(icon_.AsImageSkia()))) {
      icon_ = icon;
      views::LabelButton::SetImageModel(
          STATE_NORMAL,
          icon.IsEmpty()
              ? ui::ImageModel()
              : ui::ImageModel::FromImageSkia(
                    gfx::ImageSkiaOperations::CreateResizedImage(
                        icon.AsImageSkia(), skia::ImageOperations::RESIZE_BEST,
                        gfx::Size(14, 14))));
    }
    SetTooltipText(title);
    GetViewAccessibility().SetName(title);
    if (badge != badge_) {
      badge_ = std::move(badge);
      SchedulePaint();
    }
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    FlyButton::PaintButtonContents(canvas);
    if (badge_.empty())
      return;
    const auto text = base::UTF8ToUTF16(badge_.substr(0, 4));
    const auto font = FlyFont(FlyFace::kMono, 7, gfx::Font::Weight::BOLD);
    const int width =
        std::min(this->width(), gfx::GetStringWidth(text, font) + 4);
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
  gfx::Image icon_;
  std::string badge_;
};
BEGIN_METADATA(ExtensionActionButton)
END_METADATA

// The decorated browser windows in the order they were opened.
std::vector<BrowserWindowInterface*> DecoratedBrowsers() {
  std::vector<BrowserWindowInterface*> result;
  auto* app = FlyApp::GetIfCreated();
  if (!app)
    return result;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (app->WindowFor(browser) && !browser->IsDeleteScheduled())
          result.push_back(browser);
        return true;
      },
      BrowserCollection::Order::kCreation);
  return result;
}
}  // namespace

bool FlyWindowController::ShouldDecorate(
    const BrowserWindowInterface* browser) {
  return base::CommandLine::ForCurrentProcess()->HasSwitch("fly-native") &&
         browser->GetType() == BrowserWindowInterface::Type::TYPE_NORMAL;
}

FlyWindowController* FlyWindowController::FromBrowser(
    const BrowserWindowInterface* browser) {
  auto* app = FlyApp::GetIfCreated();
  return app && browser ? app->WindowFor(browser) : nullptr;
}

FlyWindowController* FlyWindowController::FromContents(
    content::WebContents* contents) {
  auto* tab = contents ? tabs::TabInterface::MaybeGetFromContents(contents)
                       : nullptr;
  return tab ? FromBrowser(tab->GetBrowserWindowInterface()) : nullptr;
}

FlyWindowController::FlyWindowController(BrowserWindowInterface* browser,
                                         views::Widget* widget)
    : browser_(browser), widget_(widget) {
  auto& app = FlyApp::Get();
  closed_subscription_ = browser->RegisterBrowserDidClose(base::BindRepeating(
      &FlyWindowController::OnBrowserDidClose, weak_factory_.GetWeakPtr()));
  history_ = std::make_unique<HistoryService>(
      *g_browser_process->profile_manager(), app.workspaces());
  history_->SetChangedCallback(base::BindRepeating(
      &FlyWindowController::OnHistoryChanged, weak_factory_.GetWeakPtr()));
  BuildSidebar();
  BuildFooter();
  BuildSurfaces();
  widget_->AddObserver(this);
  widget_->GetFocusManager()->AddFocusChangeListener(this);
  observing_sidebar_focus_ = true;
  sidebar_focus_client_ =
      aura::client::GetFocusClient(widget_->GetNativeWindow()->GetRootWindow());
  if (sidebar_focus_client_)
    sidebar_focus_client_->AddObserver(this);
  widget_->GetNativeWindow()->AddPreTargetHandler(this);
  if (auto* frame = FlyBrowserFrame::FromWidget(widget_))
    InstallInto(frame);
  tab_sidebar_->SetBrowser(browser_);
  const auto workspace = app.WorkspaceFor(browser_->GetProfile());
  status_workspace_name_ =
      workspace ? base::UTF8ToUTF16(workspace->name) : std::u16string();
  status_bar_->SetBrowser(browser_, status_workspace_name_);
  download_shelf_->AddProfile(browser_->GetProfile());
  SetNotificationsEnabled(
      app.preferences().ReadBool("web_notifications").value_or(true));
  OnPreferencesChanged(true);
  UpdateWorkspaceHeader();
  ScheduleWorkspaceRefresh();
  ScheduleExtensionRefresh();
  UpdateModeLabel();
  UpdateSplitButton();
}

FlyWindowController::~FlyWindowController() {
  weak_factory_.InvalidateWeakPtrs();
  CancelSurfaces();
  StopSidebarFocusObservation();
  if (widget_) {
    if (auto* native_window = widget_->GetNativeWindow())
      native_window->RemovePreTargetHandler(this);
    widget_->RemoveObserver(this);
  }
  if (frame_)
    frame_->SetController(nullptr);
  hints_.reset();
  palette_.reset();
  bookmark_picker_.reset();
  address_picker_.reset();
  history_picker_.reset();
  workspace_picker_.reset();
  tab_move_picker_.reset();
  downloads_picker_.reset();
  extensions_picker_.reset();
  settings_menu_.reset();
  keyboard_editor_.reset();
  diagnostics_hub_.reset();
  history_clear_widget_.reset();
  history_clear_delegate_.reset();
  rename_widget_.reset();
  rename_delegate_.reset();
  remove_widget_.reset();
  remove_delegate_.reset();
  ClearSidebarViews();
  if (sidebar_widget_) {
    sidebar_widget_->RemoveObserver(this);
    sidebar_widget_.reset();
  }
}

bool FlyWindowController::Closing() const {
  auto* app = FlyApp::GetIfCreated();
  return browser_closed_ || !widget_ || !app || app->closing();
}

content::WebContents* FlyWindowController::ActiveContents() const {
  return browser_ && !browser_closed_
             ? browser_->GetTabStripModel()->GetActiveWebContents()
             : nullptr;
}

void FlyWindowController::RunAction(std::string action) {
  if (!Closing())
    InvokeNativeCommand(browser_, std::move(action));
}

void FlyWindowController::BuildSidebar() {
  auto action = [this](std::string name) {
    return base::BindRepeating(&FlyWindowController::RunAction,
                               weak_factory_.GetWeakPtr(), std::move(name));
  };
  // The sidebar: one 260px panel whose left 36px column is the collapsed
  // rail.
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
  for (const auto& [name, icon, label] :
       {std::tuple{"minimize", &views::kChromeMinimizeIcon, u"Minimize window"},
        std::tuple{"maximize", &views::kChromeMaximizeIcon, u"Maximize window"},
        std::tuple{"close", &views::kCloseIcon, u"Close window"}}) {
    auto* button =
        window_controls_->AddChildView(views::ImageButton::CreateIconButton(
            base::BindRepeating(&FlyWindowController::RunWindowControl,
                                weak_factory_.GetWeakPtr(), std::string(name)),
            *icon, label, views::ImageButton::MaterialIconStyle::kSmall));
    button->SetPreferredSize(gfx::Size(26, 24));
    window_control_buttons_.emplace(name, button);
  }
  parts.window_controls = window_controls_;
  // The workspace header opens the dashboard; the short badge (rail) and the
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
      action("go-back"), vector_icons::kArrowBackIcon, u"Go back", 30));
  auto* forward = sidebar->AddChildView(MakeFlyIconButton(
      action("go-forward"), vector_icons::kArrowForwardIcon, u"Go forward",
      30));
  // .nav-history-group: one pill, each half with its own border.
  back->SetPillHalf(FlyButton::PillHalf::kLeft);
  forward->SetPillHalf(FlyButton::PillHalf::kRight);
  back->SetPillPartner(forward);
  forward->SetPillPartner(back);
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
  tab_sidebar_->SetStateChangedCallback(base::BindRepeating(
      &FlyWindowController::UpdateSplitButton, weak_factory_.GetWeakPtr()));
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
  // The bottom row: pin, theme and the other sidebar controls, in order.
  sidebar_pin_button_ = sidebar->AddChildView(MakeFlyIconButton(
      base::BindRepeating(&FlyWindowController::ToggleSidebarAutoCollapse,
                          weak_factory_.GetWeakPtr()),
      views::kKeepIcon, u"Pin / Auto-Collapse Sidebar", 28));
  split_button_ = sidebar->AddChildView(MakeFlyIconButton(
      action("toggle-split"), kSplitSceneIcon, u"Toggle Split View", 28));
  reader_button_ = sidebar->AddChildView(MakeFlyIconButton(
      action("toggle-reader-view"), kMenuBookIcon, u"Toggle Reader View", 28));
  auto* search = sidebar->AddChildView(
      MakeFlyIconButton(action("toggle-address"), vector_icons::kSearchIcon,
                        u"Search or enter address", 28));
  auto* extensions = sidebar->AddChildView(MakeFlyIconButton(
      action("open-extension-actions"), vector_icons::kExtensionOldIcon,
      u"Extensions", 28));
  preferences_button_ = sidebar->AddChildView(MakeFlyIconButton(
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) {
            if (window && !window->Closing()) {
              window->RunCommand("toggle-help");
              window->keyboard_editor_->ShowAppearance(true);
            }
          },
          weak_factory_.GetWeakPtr()),
      vector_icons::kSettingsIcon, u"Settings", 28));
  preferences_button_->SetEnabled(true);
  for (FlyButton* button :
       {sidebar_pin_button_.get(), split_button_.get(), reader_button_.get(),
        search, extensions, preferences_button_.get()}) {
    // Pointer actions must not leave a focused accent after dismissal.
    // Keyboard traversal can still focus and activate these controls.
    button->SetRequestFocusOnPress(false);
    button->SetOutlined(true);
    button->SetCornerRadius(4);
    button->SetIconTone(FlyButton::IconTone::kText);
    button->SetVectorIcon(*button->vector_icon(), 12);
    parts.footer.push_back(button);
  }
  sidebar->SetParts(std::move(parts));
  // Details that exist only expanded fade with the edge.
  for (views::View* view :
       {static_cast<views::View*>(window_controls_.get()),
        static_cast<views::View*>(workspace_label_.get()),
        static_cast<views::View*>(new_tab_button_.get()),
        static_cast<views::View*>(back), static_cast<views::View*>(forward),
        static_cast<views::View*>(workspace_buttons_.get()),
        static_cast<views::View*>(clock_row),
        static_cast<views::View*>(split_button_.get()),
        static_cast<views::View*>(reader_button_.get()),
        static_cast<views::View*>(search),
        static_cast<views::View*>(preferences_button_.get())})
    sidebar->AddExpandedOnly(view);
  // The badge sits over the header and never takes its clicks.
  sidebar->AddRailOnly(workspace_badge_, false);
  sidebar->AddExpandedOnly(tab_sidebar_->expanded_surfaces(), false);
  sidebar->AddRailOnly(tab_sidebar_->rail_surfaces(), false);
  sidebar->AddRailOnly(tab_sidebar_->rail_marks(), false);

  sidebar_widget_ = std::make_unique<SidebarWidget>(base::BindRepeating(
      [](FlyWindowController* window) { return window->sidebar_hit_width_; },
      base::Unretained(this)));
  views::Widget::InitParams sidebar_params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  sidebar_params.parent = widget_->GetNativeWindow();
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
  auto host = std::make_unique<views::NativeViewHost>();
  // The sidebar overlays the page while it slides. A clip window that Aura
  // stacks above the page's native views keeps it on top; Views-managed layer
  // reordering would put it below the BrowserView's contents.
  host->SetLayerManagedByViews(false);
  host->SetProperty(views::kViewIgnoredByLayoutKey, true);
  owned_sidebar_host_ = std::move(host);
  sidebar_host_ = owned_sidebar_host_.get();
}

void FlyWindowController::BuildFooter() {
  auto footer = std::make_unique<views::View>();
  footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  footer->SetBackground(FlyBackground(FlyRole::kMain));
  // The download shelf, above the status bar; it appears when a
  // download starts and makes room for itself.
  download_shelf_ =
      footer->AddChildView(std::make_unique<DownloadShelf>(base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) {
            if (window && !window->Closing())
              window->ToggleDownloads();
          },
          weak_factory_.GetWeakPtr())));
  status_bar_ =
      footer->AddChildView(std::make_unique<StatusBar>(StatusBar::Actions{
          .zoom = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window,
                 std::string action) {
                if (window && !window->Closing())
                  InvokeNativeCommand(window->browser_, action);
              },
              weak_factory_.GetWeakPtr()),
          .mode = base::BindRepeating(&FlyWindowController::ClickModeBadge,
                                      weak_factory_.GetWeakPtr()),
          .notifications = base::BindRepeating(
              &FlyWindowController::RunAction, weak_factory_.GetWeakPtr(),
              std::string("toggle-notifications")),
          .workspace =
              base::BindRepeating(&FlyWindowController::ToggleWorkspacePicker,
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
  footer_ = footer.get();
  owned_footer_ = std::move(footer);
}

void FlyWindowController::BuildSurfaces() {
  auto& app = FlyApp::Get();
  hints_ = std::make_unique<HintController>(
      widget_, base::BindRepeating(
                   [](base::WeakPtr<FlyWindowController> window, bool) {
                     if (window)
                       window->UpdateModeLabel();
                   },
                   weak_factory_.GetWeakPtr()));
  palette_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            if (!window || window->Closing())
              return false;
            return action == "trigger-hints" || action == "focus-sidebar" ||
                   action == "new-workspace" || action == "next-workspace" ||
                   action == "previous-workspace" ||
                   action.starts_with("extension:") ||
                   action.starts_with("extension-options:") ||
                   action.starts_with("extensions-") ||
                   NativeCommandAvailable(window->browser_, action);
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             content::WebContents* source, std::string action) {
            if (!window || window->Closing() ||
                window->ActiveContents() != source)
              return;
            if (action.starts_with("terminal:")) {
              OpenTerminalWithCommand(action.substr(9));
              return;
            }
            FlyApp::Get().preferences().RecordRecentCommand(action);
            if (action.starts_with("extension"))
              window->ChooseExtensionAction(source, std::move(action));
            else
              window->RunAction(std::move(action));
          },
          weak_factory_.GetWeakPtr()));
  address_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->Closing() &&
                   window->address_choices_.contains(std::string(action));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ExecuteAddress,
                          weak_factory_.GetWeakPtr()));
  history_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->Closing() &&
                   window->history_choices_.contains(std::string(action));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::OpenHistory,
                          weak_factory_.GetWeakPtr()));
  bookmark_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, std::string_view url) {
            return window && !window->Closing() &&
                   window->bookmark_urls_.contains(std::string(url));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::OpenBookmark,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::RemoveBookmark,
                          weak_factory_.GetWeakPtr()));
  workspace_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && window->WorkspaceChoiceAvailable(action);
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseWorkspace,
                          weak_factory_.GetWeakPtr()));
  tab_move_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            if (!window || window->Closing())
              return false;
            auto& app = FlyApp::Get();
            auto* source = window->move_source_browser_.get();
            return app.CanMoveTab(source, window->move_source_.get()) &&
                   std::ranges::any_of(app.TabMoveEntries(source),
                                       [&](const PaletteEntry& entry) {
                                         return entry.action == action;
                                       });
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseTabMoveDestination,
                          weak_factory_.GetWeakPtr()));
  downloads_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->Closing() &&
                   (action == "downloads-page" ||
                    action.starts_with("download:"));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, content::WebContents*,
             std::string action) {
            if (!window || window->Closing())
              return;
            if (action == "downloads-page") {
              FlyApp::Get().OpenInNewTab(GURL("chrome://downloads/"),
                                         window->browser_);
              return;
            }
            if (!OpenDownloadEntry(FlyApp::Get().WorkspaceProfiles(),
                                   window->browser_, action))
              window->ShowStatus(u"That download is no longer available.");
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, content::WebContents*,
             std::string action) {
            if (!window || window->Closing())
              return;
            const auto profiles = FlyApp::Get().WorkspaceProfiles();
            RemoveDownloadEntry(profiles, action);
            if (window->downloads_picker_->active())
              window->downloads_picker_->UpdateItems(DownloadEntries(profiles),
                                                     true);
          },
          weak_factory_.GetWeakPtr()));
  extensions_picker_ = std::make_unique<CommandPalette>(
      widget_,
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window,
             std::string_view action) {
            return window && !window->Closing() &&
                   (action.starts_with("extension:") ||
                    action.starts_with("extensions-"));
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyWindowController::ChooseExtensionAction,
                          weak_factory_.GetWeakPtr()));
  settings_menu_ = std::make_unique<SettingsMenu>(
      app.preferences(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window, std::string action) {
            base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                FROM_HERE,
                base::BindOnce(
                    [](base::WeakPtr<FlyWindowController> window,
                       std::string action) {
                      if (window)
                        window->RunAction(std::move(action));
                    },
                    window, std::move(action)));
          },
          weak_factory_.GetWeakPtr()));
  using Layout = PaletteStyle::Layout;
  extensions_picker_->SetStyle({.layout = Layout::kCentre,
                                .kind = MenuKind::kExtensions,
                                .title = u"Extensions",
                                .width = 560,
                                .enter_label = u"Run",
                                .empty_text = u"No extension actions in this "
                                              u"workspace."});
  downloads_picker_->SetStyle({.layout = Layout::kCentre,
                               .kind = MenuKind::kDownloads,
                               .title = u"Downloads",
                               .width = 640,
                               .enter_label = u"Open",
                               .empty_text = u"No downloads yet."});
  address_picker_->SetStyle({.layout = Layout::kTop,
                             .kind = MenuKind::kAddress,
                             .width = 680,
                             .enter_label = u"Go"});
  history_picker_->SetStyle({.layout = Layout::kTop,
                             .kind = MenuKind::kHistory,
                             .title = u"History",
                             .width = 680,
                             .empty_text = u"No history yet."});
  auto page_insets = base::BindRepeating(
      [](base::WeakPtr<FlyWindowController> window) {
        if (!window)
          return gfx::Insets();
        return gfx::Insets::TLBR(0, window->SidebarSlotWidth(),
                                 window->FooterHeight(), 0);
      },
      weak_factory_.GetWeakPtr());
  bookmark_picker_->SetStyle({.layout = Layout::kPage,
                              .kind = MenuKind::kBookmarks,
                              .title = u"Bookmarks",
                              .width = 820,
                              .page_insets = page_insets,
                              .empty_text = u"No bookmarks yet."});
  palette_->SetStyle({.layout = Layout::kCentre,
                      .kind = MenuKind::kCommands,
                      .title = u"Commands",
                      .width = 760,
                      .page_insets = page_insets,
                      .command_centre = true,
                      .enter_label = u"Run"});
  workspace_picker_->SetStyle({.layout = Layout::kPage,
                               .kind = MenuKind::kWorkspaces,
                               .title = u"Workspaces",
                               .width = 820,
                               .page_insets = page_insets});
  // The dashboard: drag a tab onto a tab to reorder it (or move it to
  // that tab's workspace), or onto a workspace to move it there.
  workspace_picker_->SetDragDrop(
      base::BindRepeating(
          [](std::string_view action) { return action.starts_with("tab/"); }),
      base::BindRepeating(&FlyWindowController::DropDashboardTab,
                          weak_factory_.GetWeakPtr()));
  tab_move_picker_->SetStyle({.layout = Layout::kCentre,
                              .kind = MenuKind::kTabMove,
                              .title = u"Move tab to workspace",
                              .width = 560,
                              .enter_label = u"Move"});
  keyboard_editor_ = std::make_unique<KeyboardEditor>(
      widget_, app.commands(), *g_browser_process->local_state());
  keyboard_editor_->SetActionRunner(base::BindRepeating(
      &FlyWindowController::RunPreferencesAction, weak_factory_.GetWeakPtr()));
  diagnostics_hub_ = std::make_unique<DiagnosticsHub>(
      widget_,
      DiagnosticsHub::Delegate{
          .session = base::BindRepeating([] {
            auto* app = FlyApp::GetIfCreated();
            return app ? app->CurrentDiagnostics() : DiagnosticsSession();
          }),
          .set_recording = base::BindRepeating([](bool on) {
            auto* app = FlyApp::GetIfCreated();
            if (!app)
              return;
            app->preferences().SetBool("enable_diagnostics", on);
            if (!on)
              app->ClearDiagnostics();
          }),
          .clear_events = base::BindRepeating([] {
            if (auto* app = FlyApp::GetIfCreated())
              app->ClearDiagnostics();
          }),
          .open_url = base::BindRepeating(
              [](base::WeakPtr<FlyWindowController> window, GURL url) {
                if (window && !window->Closing())
                  FlyApp::Get().OpenInNewTab(url, window->browser_);
              },
              weak_factory_.GetWeakPtr()),
          .page_insets = page_insets});
  keyboard_editor_->SetExtensions(
      &app.extensions(),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window)
              -> BrowserWindowInterface* {
            return window && !window->Closing() ? window->browser_.get()
                                                : nullptr;
          },
          weak_factory_.GetWeakPtr()),
      base::BindRepeating(
          [](base::WeakPtr<FlyWindowController> window) -> Profile* {
            return window && !window->Closing()
                       ? window->browser_->GetProfile()
                       : nullptr;
          },
          weak_factory_.GetWeakPtr()));
}

void FlyWindowController::InstallInto(FlyBrowserFrame* frame) {
  if (frame_ == frame)
    return;
  // A recreated frame takes the views over from its predecessor.
  if (footer_ && footer_->parent())
    owned_footer_ = footer_->parent()->RemoveChildViewT(footer_.get());
  if (sidebar_host_ && sidebar_host_->parent()) {
    sidebar_targeter_ = nullptr;
    if (sidebar_host_->native_view())
      sidebar_host_->Detach();
    owned_sidebar_host_ =
        sidebar_host_->parent()->RemoveChildViewT(sidebar_host_.get());
  }
  if (frame_)
    frame_->SetController(nullptr);
  frame_ = frame;
  frame_->SetController(weak_factory_.GetWeakPtr());
  frame_->AddChildView(std::move(owned_footer_));
  frame_->AddChildView(std::move(owned_sidebar_host_));
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
  sidebar_motion_applied_ = false;
  UpdateFullscreenChrome();
  frame_->InvalidateLayout();
}

void FlyWindowController::FrameDestroying(FlyBrowserFrame* frame) {
  if (frame_ != frame)
    return;
  frame_ = nullptr;
  // The frame destroys its children with it; a replacement frame installs
  // them first. Without one the window is closing.
  sidebar_targeter_ = nullptr;
  if (sidebar_host_ && sidebar_host_->native_view())
    sidebar_host_->Detach();
  sidebar_host_ = nullptr;
  footer_ = nullptr;
  download_shelf_ = nullptr;
  status_bar_ = nullptr;
  status_ = nullptr;
}

int FlyWindowController::SidebarSlotWidth() const {
  if (!widget_ || widget_->IsFullscreen())
    return 0;
  return FlyScaled(sidebar_auto_collapse_ ? kSidebarRailWidth : kSidebarWidth);
}

int FlyWindowController::FooterHeight() const {
  if (!widget_ || widget_->IsFullscreen() || !footer_ || !footer_->GetVisible())
    return 0;
  return footer_->GetPreferredSize().height();
}

void FlyWindowController::LayoutFrameChrome() {
  if (!frame_)
    return;
  const auto bounds = frame_->GetLocalBounds();
  if (footer_) {
    const int slot = SidebarSlotWidth();
    const int height = FooterHeight();
    footer_->SetBounds(slot, bounds.height() - height,
                       std::max(0, bounds.width() - slot), height);
  }
  UpdateSidebarBounds();
}

void FlyWindowController::UpdateFullscreenChrome() {
  if (Closing() || !sidebar_host_ || !footer_)
    return;
  const bool fullscreen = widget_->IsFullscreen();
  sidebar_host_->SetVisible(!fullscreen);
  if (fullscreen)
    sidebar_widget_->Hide();
  footer_->SetVisible(!fullscreen);
  if (!fullscreen)
    UpdateSidebarVisibility();
  if (frame_)
    frame_->InvalidateLayout();
}

views::ImageButton* FlyWindowController::window_control_for_testing(
    std::string_view action) const {
  const auto found = window_control_buttons_.find(std::string(action));
  return found == window_control_buttons_.end() ? nullptr : found->second.get();
}

void FlyWindowController::CancelSurfaces() {
  CancelWorkspaceRename();
  if (hints_)
    hints_->Cancel();
  for (auto* picker :
       {palette_.get(), address_picker_.get(), workspace_picker_.get(),
        tab_move_picker_.get(), bookmark_picker_.get(),
        downloads_picker_.get(), extensions_picker_.get()}) {
    if (picker)
      picker->Cancel();
  }
  if (settings_menu_)
    settings_menu_->Cancel();
  if (history_)
    CancelHistory();
  if (keyboard_editor_)
    keyboard_editor_->Cancel();
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
  split_button_ = nullptr;
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
  if (observing_sidebar_focus_ && widget_) {
    widget_->GetFocusManager()->RemoveFocusChangeListener(this);
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
  if (!sidebar_widget_ || !sidebar_widget_->GetNativeWindow() || !frame_ ||
      !sidebar_host_)
    return;
  auto bounds = frame_->GetLocalBounds();
  if (bounds.IsEmpty())
    return;
  // The host keeps one size in both states: the edge moves on the
  // compositor, and only the hit area follows it.
  bounds.set_width(FlyScaled(kSidebarWidth) +
                   (sidebar_auto_collapse_ ? kSidebarShadowExtent : 0));
  sidebar_host_->SetBoundsRect(bounds);
  // This overlay is excluded from layout; update its native clip explicitly.
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
  sidebar_hit_width_ =
      FlyScaled(kSidebarWidth) - static_cast<int>(sidebar_shift_to_ + 0.5f);
  UpdateSidebarHitArea();
}

void FlyWindowController::UpdateSidebarMode() {
  if (!sidebar_content_)
    return;
  auto& preferences = FlyApp::Get().preferences();
  const bool collapse = preferences.ReadSidebarAutoCollapse().value_or(false);
  const bool changed = collapse != sidebar_auto_collapse_;
  sidebar_auto_collapse_ = collapse;
  if (auto* pin = sidebar_pin_button_.get()) {
    const auto* label = collapse ? u"Pin sidebar" : u"Unpin sidebar";
    pin->SetVectorIcon(collapse ? views::kKeepIcon : views::kKeepFilledIcon,
                       12);
    pin->SetSelected(!collapse);
    pin->SetAccentText(!collapse);
    pin->SetTooltipText(label);
    pin->GetViewAccessibility().SetName(label);
    pin->SetEnabled(preferences.CanEditSidebarAutoCollapse());
  }
  if (changed && collapse) {
    sidebar_keyboard_expanded_ = false;
    sidebar_focus_inside_ = false;
    if (sidebar_content_->Contains(widget_->GetFocusManager()->GetFocusedView()))
      ReturnFocusFromSidebar();
  }
  // A mode or scale change applies at once (the slide distance, the shadow
  // and the details follow the new mode); other preference writes leave a
  // running slide alone. Fly starts collapsed instead of animating closed.
  sidebar_motion_ready_ = false;
  if (changed || sidebar_motion_scale_ != GetFlyInterfaceScale())
    sidebar_motion_applied_ = false;
  // Pinning or unpinning resizes the page once; hovering never does.
  if (frame_)
    frame_->InvalidateLayout();
  UpdateSidebarBounds();
  UpdateSidebarVisibility();
  sidebar_motion_ready_ = true;
}

std::u16string FlyWindowController::workspace_header_for_testing() const {
  return std::u16string(workspace_label_->GetText());
}

void FlyWindowController::UpdateSidebarVisibility() {
  if (Closing() || !sidebar_content_ || !sidebar_host_ ||
      !sidebar_widget_->GetNativeWindow())
    return;
  if (widget_->IsFullscreen()) {
    sidebar_host_->SetVisible(false);
    sidebar_widget_->Hide();
    return;
  }
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
  sidebar_host_->SetVisible(true);
  // Keep the overlay above the page's native views; a floating surface
  // stays above the sidebar.
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

// The slide: collapsed, the edge sits 224px to the left and the panel 224px
// back to the right inside it, so the panel's left 36px show. Both transforms
// run on the compositor; the shadow and the state-specific details fade with
// them.
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
    for (auto* layer :
         {edge->layer(), panel->layer(), sidebar_root_->shadow()->layer()})
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
  auto& preferences = FlyApp::Get().preferences();
  const auto collapse = preferences.ReadSidebarAutoCollapse();
  if (collapse)
    preferences.SetSidebarAutoCollapse(!*collapse);
}

void FlyWindowController::ReturnFocusFromSidebar() {
  sidebar_keyboard_expanded_ = false;
  sidebar_focus_inside_ = false;
  if (sidebar_content_ &&
      sidebar_content_->Contains(widget_->GetFocusManager()->GetFocusedView()))
    widget_->GetFocusManager()->ClearFocus();
  if (auto* contents = ActiveContents())
    contents->Focus();
  UpdateSidebarVisibility();
}

void FlyWindowController::FocusSidebar() {
  if (Closing() || !sidebar_content_)
    return;
  if (sidebar_focus_inside_) {
    ReturnFocusFromSidebar();
    return;
  }
  sidebar_keyboard_expanded_ = true;
  // Activating a row sends Aura focus to the page while the focus manager
  // still records that row. Requesting the same view again would then be a
  // no-op, leaving the expanded sidebar without keyboard focus.
  if (sidebar_content_->Contains(widget_->GetFocusManager()->GetFocusedView()))
    widget_->GetFocusManager()->ClearFocus();
  UpdateSidebarBounds();
  UpdateSidebarVisibility();
  if (!browser_->GetTabStripModel()->empty())
    tab_sidebar_->FocusActiveTab();
  else
    workspace_picker_button_->RequestFocus();
}

bool FlyWindowController::SidebarHasFocus() const {
  if (!sidebar_content_ || !sidebar_focus_client_ || !sidebar_widget_ ||
      !widget_)
    return false;
  const auto* focused = sidebar_focus_client_->GetFocusedWindow();
  return focused && (sidebar_widget_->GetNativeWindow()->Contains(focused) ||
                     (focused == widget_->GetNativeWindow() &&
                      sidebar_content_->Contains(
                          widget_->GetFocusManager()->GetFocusedView())));
}

void FlyWindowController::OnDidChangeFocus(views::View*, views::View* now) {
  if (Closing() || !sidebar_content_)
    return;
  sidebar_focus_inside_ = sidebar_content_->Contains(now);
  if (sidebar_focus_inside_)
    sidebar_keyboard_expanded_ = false;
  UpdateSidebarVisibility();
}

void FlyWindowController::OnWindowFocused(aura::Window* gained,
                                          aura::Window*) {
  if (Closing() || !sidebar_content_ || !sidebar_widget_->GetNativeWindow())
    return;
  const auto* native = sidebar_widget_->GetNativeWindow();
  sidebar_focus_inside_ =
      gained &&
      (native->Contains(gained) || gained == widget_->GetNativeWindow()) &&
      sidebar_content_->Contains(widget_->GetFocusManager()->GetFocusedView());
  if (!sidebar_focus_inside_)
    sidebar_keyboard_expanded_ = false;
  UpdateSidebarVisibility();
}

void FlyWindowController::OnWidgetBoundsChanged(views::Widget* widget,
                                                const gfx::Rect&) {
  if (widget == widget_)
    UpdateSidebarBounds();
}

void FlyWindowController::OnMouseEvent(ui::MouseEvent* event) {
  if (Closing() || !sidebar_content_ || SidebarOverlayBlocked())
    return;
  if (event->type() != ui::EventType::kMouseMoved &&
      event->type() != ui::EventType::kMouseEntered &&
      event->type() != ui::EventType::kMouseExited &&
      event->type() != ui::EventType::kMousePressed)
    return;
  // Leaving the window keeps the sidebar as it is: moving
  // from the page to the screen's left edge opens it, and touching the
  // window border does not close it. Entering the page closes it.
  if (event->type() == ui::EventType::kMouseExited)
    return;
  auto point = event->root_location();
  wm::ConvertPointToScreen(widget_->GetNativeWindow()->GetRootWindow(),
                           &point);
  const auto window = widget_->GetRootView()->GetBoundsInScreen();
  if (!window.Contains(point) && event->type() != ui::EventType::kMousePressed)
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
            widget_->GetFocusManager()->GetFocusedView())) {
      widget_->GetFocusManager()->ClearFocus();
      // A click on the page moves keyboard focus from the sidebar to it.
      auto* page = ActiveContents();
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
  const bool visible = FlyApp::Get()
                           .preferences()
                           .ReadWindowControlsVisible()
                           .value_or(false);
  if (!visible &&
      window_controls_->Contains(widget_->GetFocusManager()->GetFocusedView())) {
    widget_->GetFocusManager()->ClearFocus();
    if (auto* contents = ActiveContents())
      contents->Focus();
  }
  window_controls_->SetVisible(visible);
  const bool maximized = widget_->IsMaximized();
  auto* maximize = window_control_buttons_.at("maximize").get();
  const auto icon = ui::ImageModel::FromVectorIcon(
      maximized ? views::kChromeRestoreIcon : views::kChromeMaximizeIcon,
      ui::kColorIcon, 16);
  for (auto state : {views::Button::STATE_NORMAL, views::Button::STATE_HOVERED,
                     views::Button::STATE_PRESSED,
                     views::Button::STATE_DISABLED})
    maximize->SetImageModel(state, icon);
  const auto* title = maximized ? u"Restore window" : u"Maximize window";
  maximize->SetTooltipText(title);
  maximize->GetViewAccessibility().SetName(title);
}

void FlyWindowController::RunWindowControl(std::string action) {
  if (Closing() || !window_controls_->GetVisible())
    return;
  // Complete pointer/keyboard button dispatch before native close callbacks can
  // destroy this control or the window.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<FlyWindowController> window, std::string action) {
            if (!window || window->Closing() ||
                !FlyApp::Get()
                     .preferences()
                     .ReadWindowControlsVisible()
                     .value_or(false))
              return;
            if (action == "minimize")
              window->widget_->Minimize();
            else if (action == "maximize") {
              if (window->widget_->IsMaximized())
                window->widget_->Restore();
              else
                window->widget_->Maximize();
            } else if (action == "close") {
              window->browser_->GetWindow()->Close();
            }
          },
          weak_factory_.GetWeakPtr(), std::move(action)));
}

void FlyWindowController::OnWidgetShowStateChanged(views::Widget* widget) {
  if (widget != widget_)
    return;
  UpdateFullscreenChrome();
  UpdateWindowControls();
  UpdateClock();
}

void FlyWindowController::OnWidgetVisibilityChanged(views::Widget* widget,
                                                    bool) {
  if (widget == widget_)
    UpdateClock();
}

void FlyWindowController::OnWidgetActivationChanged(views::Widget* widget,
                                                    bool active) {
  if (widget != widget_)
    return;
  // A blurred window can no longer see the modifier's release.
  if (!active)
    SetTabNumbers(false);
}

void FlyWindowController::OnWidgetDestroying(views::Widget* widget) {
  if (widget == sidebar_widget_.get()) {
    ClearSidebarViews();
    return;
  }
  if (widget != widget_)
    return;
  // The floating surfaces observe this window themselves and close with it.
  // Child widgets go with their parent's native window; their Widget objects
  // are released with this controller, outside this notification.
  StopSidebarFocusObservation();
  clock_timer_.Stop();
  reader_poll_.Stop();
  tab_numbers_timer_.Stop();
  if (auto* native_window = widget_->GetNativeWindow())
    native_window->RemovePreTargetHandler(this);
  widget_->RemoveObserver(this);
  if (frame_) {
    frame_->SetController(nullptr);
    FrameDestroying(frame_);
  }
  widget_ = nullptr;
  // The Browser is deleted right after its widget.
  browser_closed_ = true;
  browser_ = nullptr;
  if (auto* app = FlyApp::GetIfCreated())
    app->ReleaseWindow(this);
}

void FlyWindowController::OnBrowserDidClose(BrowserWindowInterface* browser) {
  if (browser != browser_)
    return;
  CancelSurfaces();
  browser_closed_ = true;
  if (tab_sidebar_)
    tab_sidebar_->SetBrowser(nullptr);
  if (status_bar_)
    status_bar_->SetBrowser(nullptr, u"");
  if (auto* app = FlyApp::GetIfCreated())
    app->BroadcastWorkspaceRefresh();
}

void FlyWindowController::ApplyColorMode() {
  const auto theme = FlyApp::Get().preferences().ReadTheme();
  if (!theme)
    return;
  const auto color_mode = theme->mode == "dark"
                              ? ui::ColorProviderKey::ColorMode::kDark
                              : ui::ColorProviderKey::ColorMode::kLight;
  widget_->SetColorModeOverride(color_mode);
  sidebar_widget_->SetColorModeOverride(color_mode);
}

void FlyWindowController::OnPreferencesChanged(bool theme_changed) {
  if (!widget_)
    return;
  if (theme_changed)
    ApplyColorMode();
  UpdateClock();
  UpdateSidebarMode();
  UpdateWindowControls();
}

void FlyWindowController::OnPaletteChanged(bool scale_changed) {
  if (!widget_)
    return;
  for (auto* widget : {widget_.get(), sidebar_widget_.get()}) {
    if (widget && widget->GetNativeWindow()) {
      // Fly's palette and interface scale live outside Chromium's color
      // provider key. Its theme-change optimization must not skip our font,
      // geometry and artwork cache updates when that key stays the same.
      widget->ResetLastColorProviderKey();
      widget->ThemeChanged();
      widget->GetRootView()->SchedulePaint();
    }
  }
  if (scale_changed) {
    // Widths follow the interface scale.
    if (auto* sidebar = sidebar_content_.get())
      sidebar->SetPreferredSize(gfx::Size(FlyScaled(260), 0));
    UpdateSidebarMode();
    UpdateSidebarBounds();
    if (frame_)
      frame_->InvalidateLayout();
  }
}

void FlyWindowController::SetNotificationsEnabled(bool enabled) {
  if (status_bar_)
    status_bar_->SetNotificationsEnabled(enabled);
}

void FlyWindowController::SetUpdateStatus(std::u16string status,
                                          std::string version) {
  if (keyboard_editor_)
    keyboard_editor_->SetUpdateStatus(std::move(status), std::move(version));
}

void FlyWindowController::RefreshDesktopIntegration() {
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         DesktopIntegrationStatus status) {
        if (window)
          window->ShowDesktopIntegration(status, u"");
      },
      weak_factory_.GetWeakPtr()));
}

void FlyWindowController::ShowDesktopIntegration(
    const DesktopIntegrationStatus& status,
    std::u16string error) {
  if (keyboard_editor_)
    ShowDesktopIntegrationIn(*keyboard_editor_, status, std::move(error));
}

void FlyWindowController::UpdateWorkspaceHeader() {
  if (!workspace_picker_button_ || browser_closed_)
    return;
  std::u16string name = u"Workspace";
  std::u16string badge = u"?";
  std::u16string compact = u"W";
  const bool is_private = browser_->GetProfile()->IsOffTheRecord();
  auto& app = FlyApp::Get();
  const auto catalog = app.workspaces().Read();
  if (is_private) {
    name = u"Private";
    badge = u"P";
    compact = u"P";
  } else if (catalog) {
    if (const auto workspace = app.WorkspaceFor(browser_->GetProfile())) {
      for (size_t i = 0; i < catalog->items.size(); ++i) {
        if (catalog->items[i].id != workspace->id)
          continue;
        name = base::UTF8ToUTF16(catalog->items[i].name);
        badge = base::NumberToString16(i + 1);
        compact = u"W" + badge;
      }
    }
  }
  workspace_badge_->SetText(is_private ? compact : WorkspaceBadgeText(name));
  workspace_label_->SetText(name);
  workspace_picker_button_->GetViewAccessibility().SetName(
      u"Workspace " + name + u": open the workspace dashboard");
  // The list is only needed to reach other workspaces or windows directly.
  workspace_buttons_->SetVisible(
      (catalog && catalog->items.size() > 1) ||
      workspace_buttons_->children().size() >
          (catalog ? catalog->items.size() : 0));
}

void FlyWindowController::UpdateClock() {
  if (!clock_ || !widget_)
    return;
  auto& preferences = FlyApp::Get().preferences();
  const bool footer = preferences.ReadStatusBarVisible().value_or(false);
  if (status_bar_)
    status_bar_->SetVisible(footer);
  const auto settings = preferences.ReadClock();
  const bool visible = settings && settings->visible;
  if (!visible && clock_->HasFocus()) {
    if (auto* contents = ActiveContents())
      contents->Focus();
  }
  clock_->SetVisible(visible);
  clock_->SetEnabled(preferences.CanEditClock());
  if (!visible) {
    if (status_bar_)
      status_bar_->SetClockVisible(false);
    clock_timer_.Stop();
    return;
  }
  // The clock always carries the current time: assistive technology reads it
  // even while the sidebar is collapsed. Only a shown clock keeps ticking.
  const auto now = base::Time::Now();
  const auto text = FormatClock(now, settings->format);
  // The sidebar and the footer draw the date and the time separately (time
  // in the accent display face).
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
  const bool window_shown = widget_->IsVisible() && !widget_->IsMinimized();
  if (!window_shown || (!sidebar_expanded_ && !footer)) {
    if (status_bar_)
      status_bar_->SetClockVisible(true);
    clock_timer_.Stop();
    return;
  }
  if (status_bar_)
    status_bar_->SetClock(date, time, text.tooltip, true);
  // Wake at the next visible change only: the next second for the seconds
  // format, otherwise the next minute.
  base::Time::Exploded local;
  now.LocalExplode(&local);
  const auto delay =
      settings->format == "seconds"
          ? base::Milliseconds(1000 - local.millisecond)
          : base::Seconds(60 - local.second) -
                base::Milliseconds(local.millisecond);
  clock_timer_.Start(FROM_HERE, delay + base::Milliseconds(5),
                     base::BindOnce(&FlyWindowController::UpdateClock,
                                    weak_factory_.GetWeakPtr()));
}

void FlyWindowController::CycleClockFormat() {
  FlyApp::Get().preferences().CycleClockFormat();
}

void FlyWindowController::UpdateSplitButton() {
  if (Closing() || !split_button_)
    return;
  auto* model = browser_->GetTabStripModel();
  split_button_->SetSelected(
      model->active_index() >= 0 &&
      model->GetSplitForTab(model->active_index()).has_value());
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

void FlyWindowController::ActivateTab(BrowserWindowInterface* browser,
                                      content::WebContents* contents) {
  if (!browser || browser->IsDeleteScheduled())
    return;
  auto* model = browser->GetTabStripModel();
  if (contents) {
    const int index = model->GetIndexOfWebContents(contents);
    if (index != TabStripModel::kNoTab)
      model->ActivateTabAt(index);
  }
  auto* window = browser->GetWindow();
  if (window->IsMinimized())
    window->Restore();
  window->Show();
  window->Activate();
}

void FlyWindowController::RebuildWorkspaceButtons() {
  workspace_refresh_pending_ = false;
  if (!workspace_buttons_ || Closing())
    return;
  // WorkspaceEntries() rebuilds the dashboard's window choices itself.
  if (workspace_picker_->active())
    workspace_picker_->UpdateItems(WorkspaceEntries(), false);
  workspace_buttons_->RemoveAllChildViews();
  auto& app = FlyApp::Get();
  const auto catalog = app.workspaces().Read();
  if (!catalog) {
    workspace_buttons_->AddChildView(std::make_unique<FlyLabel>(
        u"Workspace data needs recovery", FlyFace::kSans, 12));
    return;
  }
  const auto own = app.WorkspaceFor(browser_->GetProfile());
  const bool is_private = browser_->GetProfile()->IsOffTheRecord();
  const auto browsers = DecoratedBrowsers();
  const bool editable = app.workspaces().CanEdit();
  for (const auto& item : catalog->items) {
    auto* button =
        workspace_buttons_->AddChildView(std::make_unique<WorkspaceMenuButton>(
            base::BindRepeating(
                [](std::string id) {
                  if (auto* app = FlyApp::GetIfCreated())
                    app->OpenWorkspace(id);
                },
                item.id),
            base::UTF8ToUTF16(item.name), item.id,
            editable && &item != &catalog->items.front(),
            editable && &item != &catalog->items.back(),
            base::BindRepeating(
                [](std::string id, int direction) {
                  if (auto* app = FlyApp::GetIfCreated())
                    app->ReorderWorkspace(id, direction);
                },
                item.id),
            editable,
            base::BindRepeating(&FlyWindowController::RenameWorkspace,
                                weak_factory_.GetWeakPtr(), item.id),
            editable && catalog->items.size() > 1 &&
                !app.workspace_removal_pending(),
            base::BindRepeating(&FlyWindowController::RemoveWorkspace,
                                weak_factory_.GetWeakPtr(), item.id)));
    const bool active = !is_private && own && own->id == item.id;
    button->SetStyle(active ? ui::ButtonStyle::kTonal : ui::ButtonStyle::kText);
    // A workspace with several windows lists them, so each can be reached.
    std::vector<BrowserWindowInterface*> windows;
    for (auto* browser : browsers) {
      const auto workspace = app.WorkspaceFor(browser->GetProfile());
      if (!browser->GetProfile()->IsOffTheRecord() && workspace &&
          workspace->id == item.id)
        windows.push_back(browser);
    }
    if (windows.size() < 2)
      continue;
    for (size_t i = 0; i < windows.size(); ++i) {
      auto* browser = windows[i];
      auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
      const auto title = contents && !contents->GetTitle().empty()
                             ? contents->GetTitle()
                             : u"New tab";
      const auto text =
          u"Window " + base::NumberToString16(i + 1) + u" · " + title;
      auto* choice =
          workspace_buttons_->AddChildView(std::make_unique<FlyButton>(
              base::BindRepeating(
                  [](base::WeakPtr<FlyWindowController> window,
                     base::WeakPtr<BrowserWindowInterface> browser) {
                    if (window && browser)
                      window->ActivateTab(browser.get(), nullptr);
                  },
                  weak_factory_.GetWeakPtr(), browser->GetWeakPtr()),
              text));
      choice->SetStyle(browser == browser_.get() ? ui::ButtonStyle::kTonal
                                                 : ui::ButtonStyle::kText);
      choice->GetViewAccessibility().SetName(base::UTF8ToUTF16(item.name) +
                                             u", " + text);
    }
  }
  // Private windows and windows of profiles outside the catalogue.
  for (auto* browser : browsers) {
    const auto workspace = app.WorkspaceFor(browser->GetProfile());
    const bool otr = browser->GetProfile()->IsOffTheRecord();
    if (!otr && workspace)
      continue;
    std::u16string label = u"Recovery";
    if (otr) {
      label = workspace ? u"Private · " + base::UTF8ToUTF16(workspace->name)
                        : u"Private workspace";
    }
    auto* choice = workspace_buttons_->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(
            [](base::WeakPtr<FlyWindowController> window,
               base::WeakPtr<BrowserWindowInterface> browser) {
              if (window && browser)
                window->ActivateTab(browser.get(), nullptr);
            },
            weak_factory_.GetWeakPtr(), browser->GetWeakPtr()),
        label));
    choice->SetStyle(browser == browser_.get() ? ui::ButtonStyle::kTonal
                                               : ui::ButtonStyle::kText);
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

views::Widget* FlyWindowController::workspace_rename_dialog_for_testing()
    const {
  return rename_widget_ && !rename_widget_->IsClosed() ? rename_widget_.get()
                                                       : nullptr;
}

views::Widget* FlyWindowController::workspace_remove_dialog_for_testing()
    const {
  return remove_widget_ && !remove_widget_->IsClosed() ? remove_widget_.get()
                                                       : nullptr;
}

views::Widget* FlyWindowController::history_clear_dialog_for_testing() const {
  return history_clear_widget_ && !history_clear_widget_->IsClosed()
             ? history_clear_widget_.get()
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

void FlyWindowController::RemoveWorkspace(std::string id) {
  auto& app = FlyApp::Get();
  if (Closing() || app.workspace_removal_pending())
    return;
  const auto catalog = app.workspaces().Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  const auto path =
      g_browser_process->profile_manager()->user_data_dir().AppendASCII(
          item->profile_directory);
  if (!app.CanRemoveWorkspace(id, path))
    return;
  CancelSurfaces();
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
      u"Close this workspace's windows, including its private windows, and "
      u"remove it from Fly? Downloads in its private windows will stop. Its "
      u"local data is kept for recovery. Recreating the name starts fresh."));
  message->SetMultiLine(true);
  auto* error = body->AddChildView(std::make_unique<views::Label>());
  error->SetID(2);
  error->SetMultiLine(true);
  remove_delegate_->SetContentsView(std::move(body));
  remove_delegate_->SetAcceptCallbackWithClose(base::BindRepeating(
      [](std::string id, base::FilePath path, views::Label* error) {
        auto* app = FlyApp::GetIfCreated();
        if (!app || app->closing())
          return true;
        if (app->workspace_removal_pending() ||
            !app->CanRemoveWorkspace(id, path)) {
          error->SetText(
              u"The workspace must still be editable and at least "
              u"one other workspace must remain.");
          return false;
        }
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
            FROM_HERE, base::BindOnce(
                           [](std::string id, base::FilePath path) {
                             if (auto* app = FlyApp::GetIfCreated())
                               app->BeginWorkspaceRemoval(std::move(id),
                                                          std::move(path));
                           },
                           std::move(id), std::move(path)));
        return true;
      },
      std::move(id), path, error));
  remove_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      remove_delegate_.get(), widget_->GetNativeWindow(),
      widget_->GetNativeView()));
  remove_widget_->Show();
}

void FlyWindowController::RenameWorkspace(std::string id) {
  auto& app = FlyApp::Get();
  if (Closing() || !app.workspaces().CanEdit())
    return;
  const auto catalog = app.workspaces().Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  CancelSurfaces();
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
      [](std::string id, views::Textfield* field, views::Label* error) {
        auto* app = FlyApp::GetIfCreated();
        if (!app || app->closing())
          return true;
        if (!app->workspaces().Rename(id,
                                      base::UTF16ToUTF8(field->GetText()))) {
          error->SetText(u"Use a unique, non-empty workspace name. "
                         u"The workspace must still be editable.");
          return false;
        }
        return true;
      },
      std::move(id), field, error));
  rename_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      rename_delegate_.get(), widget_->GetNativeWindow(),
      widget_->GetNativeView()));
  rename_widget_->Show();
  field->SelectAll(false);
}

void FlyWindowController::PromptNewWorkspace() {
  auto& app = FlyApp::Get();
  if (Closing() || app.creating_profile() || !app.workspaces().CanEdit())
    return;
  CancelSurfaces();
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
  field->SetText(base::UTF8ToUTF16(app.NextWorkspaceName()));
  field->GetViewAccessibility().SetName(u"Workspace name");
  auto* error = body->AddChildView(std::make_unique<views::Label>());
  error->SetMultiLine(true);
  rename_delegate_->SetInitiallyFocusedView(field);
  rename_delegate_->SetContentsView(std::move(body));
  rename_delegate_->SetAcceptCallbackWithClose(base::BindRepeating(
      [](views::Textfield* field, views::Label* error) {
        auto* app = FlyApp::GetIfCreated();
        if (!app || app->closing())
          return true;
        const auto name = std::string(base::TrimWhitespaceASCII(
            base::UTF16ToUTF8(field->GetText()), base::TRIM_ALL));
        const auto catalog = app->workspaces().Read();
        if (name.empty() || !catalog ||
            std::ranges::any_of(catalog->items, [&](const Workspace& item) {
              return item.name == name;
            })) {
          error->SetText(u"Use a unique, non-empty workspace name.");
          return false;
        }
        app->CreateWorkspace(name);
        return true;
      },
      field, error));
  rename_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      rename_delegate_.get(), widget_->GetNativeWindow(),
      widget_->GetNativeView()));
  rename_widget_->Show();
  field->SelectAll(false);
}

// The workspace dashboard: every workspace with the tabs of its windows
// (pinned ones marked), rename and delete on each workspace, and a move on
// each tab.
std::vector<PaletteEntry> FlyWindowController::WorkspaceEntries() {
  window_choices_.clear();
  tab_choices_.clear();
  std::vector<PaletteEntry> result;
  auto& app = FlyApp::Get();
  const auto catalog = app.workspaces().Read();
  if (!catalog || browser_closed_)
    return result;
  const auto own = app.WorkspaceFor(browser_->GetProfile());
  const std::string active =
      own && !browser_->GetProfile()->IsOffTheRecord() ? own->id : "";
  const bool editable = app.workspaces().CanEdit();
  const auto browsers = DecoratedBrowsers();
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
    size_t number = 0;
    for (auto* browser : browsers) {
      const auto workspace = app.WorkspaceFor(browser->GetProfile());
      if (browser->GetProfile()->IsOffTheRecord() || !workspace ||
          workspace->id != item.id)
        continue;
      auto* model = browser->GetTabStripModel();
      for (int index = 0; index < model->count(); ++index) {
        auto* contents = model->GetWebContentsAt(index);
        const auto key = item.id + "/" + base::NumberToString(number++);
        tab_choices_.emplace(
            key, TabChoice{browser->GetWeakPtr(), contents->GetWeakPtr()});
        const auto title = contents->GetTitle().empty()
                               ? contents->GetVisibleURL().spec()
                               : base::UTF16ToUTF8(contents->GetTitle());
        PaletteEntry tab{"tab/" + key,
                         "tab/" + key,
                         "- " + title +
                             (model->IsTabPinned(index) ? " \xF0\x9F\x93\x8C"
                                                        : ""),
                         "",
                         {},
                         {}};
        tab.indent = 1;
        tab.row_actions.emplace_back("move/" + key, "Move");
        result.push_back(std::move(tab));
      }
    }
  }
  size_t number = 0;
  for (auto* browser : browsers) {
    if (!browser->GetProfile()->IsOffTheRecord())
      continue;
    const auto workspace = app.WorkspaceFor(browser->GetProfile());
    const auto title = workspace ? "Private · " + workspace->name
                                 : std::string("Private workspace");
    const auto action = "private/" + base::NumberToString(number++);
    window_choices_.emplace(action, browser->GetWeakPtr());
    result.push_back({action, action, title, "Private window", {}, {}});
  }
  result.push_back({"create", "create", "New workspace", "", {}, {}});
  return result;
}

bool FlyWindowController::WorkspaceChoiceAvailable(
    std::string_view action) const {
  auto* app = FlyApp::GetIfCreated();
  if (Closing() || !app)
    return false;
  const auto catalog = app->workspaces().Read();
  if (!catalog)
    return false;
  if (action == "create")
    return !app->creating_profile() && app->workspaces().CanEdit();
  if (action.starts_with("workspace/"))
    return std::ranges::find(catalog->items, action.substr(10),
                             &Workspace::id) != catalog->items.end();
  if (action.starts_with("rename/"))
    return app->workspaces().CanEdit() &&
           std::ranges::find(catalog->items, action.substr(7),
                             &Workspace::id) != catalog->items.end();
  if (action.starts_with("delete/")) {
    const auto item =
        std::ranges::find(catalog->items, action.substr(7), &Workspace::id);
    return item != catalog->items.end() && !app->workspace_removal_pending() &&
           app->CanRemoveWorkspace(item->id,
                                   g_browser_process->profile_manager()
                                       ->user_data_dir()
                                       .AppendASCII(item->profile_directory));
  }
  if (action.starts_with("tab/") || action.starts_with("move/")) {
    const auto choice =
        tab_choices_.find(std::string(action.substr(action.find('/') + 1)));
    if (choice == tab_choices_.end() || !choice->second.browser ||
        !choice->second.contents)
      return false;
    return !action.starts_with("move/") ||
           TabMoveTransaction::CanMoveUrl(
               choice->second.contents->GetLastCommittedURL());
  }
  const auto found = window_choices_.find(std::string(action));
  return found != window_choices_.end() && found->second &&
         !found->second->IsDeleteScheduled();
}

void FlyWindowController::ChooseWorkspace(content::WebContents* source,
                                          std::string action) {
  if (!WorkspaceChoiceAvailable(action) || ActiveContents() != source)
    return;
  auto& app = FlyApp::Get();
  if (action == "create") {
    PromptNewWorkspace();
  } else if (action.starts_with("workspace/")) {
    app.OpenWorkspace(action.substr(10));
  } else if (action.starts_with("rename/")) {
    RenameWorkspace(action.substr(7));
  } else if (action.starts_with("delete/")) {
    RemoveWorkspace(action.substr(7));
  } else if (action.starts_with("tab/") || action.starts_with("move/")) {
    const auto choice = tab_choices_.find(action.substr(action.find('/') + 1));
    if (choice == tab_choices_.end() || !choice->second.browser ||
        !choice->second.contents)
      return;
    auto* browser = choice->second.browser.get();
    auto* contents = choice->second.contents.get();
    if (browser->GetTabStripModel()->GetIndexOfWebContents(contents) ==
        TabStripModel::kNoTab)
      return;
    ActivateTab(browser, contents);
    if (action.starts_with("move/")) {
      if (auto* window = app.WindowFor(browser))
        window->ShowTabMovePicker(contents);
    }
  } else if (const auto choice = window_choices_.find(action);
             choice != window_choices_.end() && choice->second) {
    ActivateTab(choice->second.get(), nullptr);
  }
}

void FlyWindowController::DropDashboardTab(std::string source,
                                           std::string target) {
  if (Closing() || !workspace_picker_->active() || !source.starts_with("tab/"))
    return;
  const auto from = tab_choices_.find(source.substr(4));
  if (from == tab_choices_.end() || !from->second.browser ||
      !from->second.contents)
    return;
  auto* browser = from->second.browser.get();
  auto* contents = from->second.contents.get();
  auto* model = browser->GetTabStripModel();
  const int index = model->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab)
    return;
  std::string workspace;
  if (target.starts_with("workspace/")) {
    workspace = target.substr(10);
  } else if (target.starts_with("tab/")) {
    const auto to = tab_choices_.find(target.substr(4));
    if (to == tab_choices_.end() || !to->second.browser || !to->second.contents)
      return;
    if (to->second.browser.get() == browser) {
      // Same window: reorder.
      const int destination =
          model->GetIndexOfWebContents(to->second.contents.get());
      if (destination != TabStripModel::kNoTab && destination != index)
        model->MoveWebContentsAt(index, destination, false);
      workspace_picker_->UpdateItems(WorkspaceEntries(), false);
      return;
    }
    workspace = target.substr(4, target.find('/', 4) - 4);
  } else {
    return;
  }
  auto& app = FlyApp::Get();
  const auto current = app.WorkspaceFor(browser->GetProfile());
  if (!current || current->id == workspace)
    return;
  // Another workspace: the same transaction as Move, which appends the tab.
  model->ActivateTabAt(index);
  app.MoveTab(browser, contents, "move:" + workspace);
  workspace_picker_->UpdateItems(WorkspaceEntries(), false);
}

void FlyWindowController::ToggleWorkspacePicker() {
  if (Closing() || !CanShowHistory())
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  const auto config =
      FlyApp::Get().commands().Read().value_or(DefaultKeyboardConfig());
  workspace_picker_->ToggleItems(ActiveContents(), WorkspaceEntries(),
                                 config.shortcuts.at("toggle-dashboard"),
                                 u"Search workspaces", false, true);
}

void FlyWindowController::choose_workspace_for_testing(std::string action) {
  ChooseWorkspace(ActiveContents(), std::move(action));
}

bool FlyWindowController::ShowTabMovePicker(content::WebContents* contents) {
  if (tab_move_picker_->active()) {
    tab_move_picker_->Cancel(true);
    return true;
  }
  auto& app = FlyApp::Get();
  if (Closing() || !app.CanMoveTab(browser_, contents))
    return false;
  auto* tabs = browser_->GetTabStripModel();
  tabs->ActivateTabAt(tabs->GetIndexOfWebContents(contents));
  CancelWorkspaceRename();
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  workspace_picker_->Cancel();
  keyboard_editor_->Cancel();
  move_source_browser_ = browser_->GetWeakPtr();
  move_source_ = contents->GetWeakPtr();
  const auto config = app.commands().Read().value_or(DefaultKeyboardConfig());
  tab_move_picker_->ToggleItems(contents, app.TabMoveEntries(browser_),
                                config.shortcuts.at("move-tab-to-workspace"),
                                u"Move tab to workspace", false);
  return true;
}

void FlyWindowController::ChooseTabMoveDestination(
    content::WebContents* source,
    std::string action) {
  if (!source || source != move_source_.get() || !move_source_browser_)
    return;
  auto* browser = move_source_browser_.get();
  move_source_browser_.reset();
  move_source_.reset();
  FlyApp::Get().MoveTab(browser, source, action);
}

void FlyWindowController::OnWorkspacesChanged() {
  if (Closing())
    return;
  ScheduleWorkspaceRefresh();
  UpdateWorkspaceHeader();
  auto& app = FlyApp::Get();
  // A rename shows in the status bar; other catalogue writes (the active
  // workspace changes with every window activation) leave it alone.
  if (status_bar_) {
    const auto workspace = app.WorkspaceFor(browser_->GetProfile());
    const auto name =
        workspace ? base::UTF8ToUTF16(workspace->name) : std::u16string();
    if (name != status_workspace_name_) {
      status_workspace_name_ = name;
      status_bar_->SetBrowser(browser_, name);
    }
  }
  if (tab_move_picker_->active()) {
    if (app.CanMoveTab(move_source_browser_.get(), move_source_.get()))
      tab_move_picker_->UpdateItems(
          app.TabMoveEntries(move_source_browser_.get()), false);
    else
      tab_move_picker_->Cancel();
  }
  if (!workspace_picker_->active())
    return;
  if (!CanShowHistory())
    workspace_picker_->Cancel(true);
  else
    workspace_picker_->UpdateItems(WorkspaceEntries(), false);
}

void FlyWindowController::CreateTab() {
  if (!Closing())
    OpenNativeBlankTab(browser_);
}

bool FlyWindowController::CanShowHistory() const {
  auto* app = FlyApp::GetIfCreated();
  return app && app->workspaces().Read().has_value();
}

void FlyWindowController::OnHistoryChanged() {
  if (Closing())
    return;
  if (history_picker_ && history_picker_->active())
    LoadHistory(HistorySurface::kHistory);
  else if (address_picker_ && address_picker_->active())
    LoadHistory(HistorySurface::kAddress);
  else
    history_entries_.clear();
}

void FlyWindowController::CancelHistory() {
  ++history_generation_;
  history_->Cancel();
  if (history_picker_)
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
  if (!FlyApp::Get().history_clear_running()) {
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
  auto* contents = ActiveContents();
  auto* picker = surface == HistorySurface::kAddress ? address_picker_.get()
                                                     : history_picker_.get();
  if (!picker->active())
    return;
  const auto generation = ++history_generation_;
  history_->Query(base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window,
         base::WeakPtr<content::WebContents> source, bool empty_source,
         uint64_t generation, HistorySurface surface,
         std::optional<std::vector<HistoryEntry>> rows) {
        if (!window || window->Closing() || (!empty_source && !source) ||
            generation != window->history_generation_ ||
            window->ActiveContents() != source.get())
          return;
        auto* picker = surface == HistorySurface::kAddress
                           ? window->address_picker_.get()
                           : window->history_picker_.get();
        if (!picker->active())
          return;
        window->history_entries_ = rows.value_or(std::vector<HistoryEntry>());
        if (!rows && window->status_)
          window->status_->SetText(
              u"History is unavailable. Saved data has been preserved.");
        if (surface == HistorySurface::kAddress)
          picker->RefreshQuery();
        else
          picker->UpdateItems(window->HistoryEntries(), false);
      },
      weak_factory_.GetWeakPtr(),
      contents ? contents->GetWeakPtr() : base::WeakPtr<content::WebContents>(),
      !contents, generation, surface));
}

void FlyWindowController::ToggleHistory() {
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
  const auto config =
      FlyApp::Get().commands().Read().value_or(DefaultKeyboardConfig());
  history_picker_->ToggleItems(ActiveContents(), {},
                               config.shortcuts.at("toggle-history"),
                               u"Search history", false, true);
  LoadHistory(HistorySurface::kHistory);
}

void FlyWindowController::OpenHistory(content::WebContents* source,
                                      std::string action) {
  if (Closing() || ActiveContents() != source ||
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
         base::WeakPtr<content::WebContents> source, bool empty_source,
         std::string url, std::optional<std::vector<HistoryEntry>> rows) {
        if (!window || window->Closing() || (!empty_source && !source) ||
            window->ActiveContents() != source.get() || !rows ||
            !std::ranges::any_of(*rows, [&](const HistoryEntry& item) {
              return item.url.spec() == url;
            }))
          return;
        NavigateParams params(window->browser_, GURL(url),
                              ui::PAGE_TRANSITION_TYPED);
        params.disposition = WindowOpenDisposition::CURRENT_TAB;
        params.window_action = NavigateParams::WindowAction::kShowWindow;
        Navigate(&params);
      },
      weak_factory_.GetWeakPtr(),
      source ? source->GetWeakPtr() : base::WeakPtr<content::WebContents>(),
      !source, std::move(action)));
}

void FlyWindowController::ConfirmHistoryClear(content::WebContents* source) {
  if (FlyApp::Get().history_clear_running())
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
         base::WeakPtr<content::WebContents> source, bool empty_source) {
        if (!window || window->Closing() || (!empty_source && !source) ||
            window->ActiveContents() != source.get())
          return;
        window->history_entries_.clear();
        window->history_choices_.clear();
        ++window->history_generation_;
        window->ShowStatus(u"Clearing history…");
        FlyApp::Get().ClearHistory();
      },
      weak_factory_.GetWeakPtr(),
      source ? source->GetWeakPtr() : base::WeakPtr<content::WebContents>(),
      !source));
  history_clear_widget_.reset(views::DialogDelegate::CreateDialogWidget(
      history_clear_delegate_.get(), widget_->GetNativeWindow(),
      widget_->GetNativeView()));
  history_clear_widget_->Show();
}

void FlyWindowController::ToggleAddress() {
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
  auto* contents = ActiveContents();
  const auto config =
      FlyApp::Get().commands().Read().value_or(DefaultKeyboardConfig());
  const auto url = contents ? contents->GetVisibleURL() : GURL(kFlyNewTabURL);
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
  auto& app = FlyApp::Get();
  const auto lower = base::ToLowerASCII(input);
  if (lower.starts_with("ws:") || lower.starts_with("ws ")) {
    const auto query = base::ToLowerASCII(std::string(
        base::TrimWhitespaceASCII(input.substr(3), base::TRIM_ALL)));
    if (const auto catalog = app.workspaces().Read())
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
  if (const auto bookmarks = app.bookmarks().Read())
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
  if (Closing() || ActiveContents() != source)
    return;
  const auto choice = address_choices_.find(action);
  if (choice == address_choices_.end())
    return;
  const auto payload = choice->second;
  address_choices_.clear();
  auto& app = FlyApp::Get();
  if (action.starts_with("workspace:")) {
    app.OpenWorkspace(payload);
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
  if (action.starts_with("bookmark:") && app.bookmarks().Contains(payload))
    target = GURL(payload);
  else if (action.starts_with("navigate:"))
    target = SearchService(*g_browser_process->local_state())
                 .ResolveNavigation(payload);
  if (!target || !target->is_valid())
    return;
  NavigateParams params(browser_, *target, ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::CURRENT_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
}

std::vector<PaletteEntry> FlyWindowController::BookmarkEntries() {
  bookmark_urls_.clear();
  std::vector<PaletteEntry> entries;
  if (const auto bookmarks = FlyApp::Get().bookmarks().Read()) {
    for (const auto& bookmark : *bookmarks) {
      bookmark_urls_.insert(bookmark.url);
      entries.push_back({bookmark.url, bookmark.url, bookmark.title,
                         bookmark.url, {}, {}});
    }
  }
  return entries;
}

void FlyWindowController::OnBookmarksChanged() {
  if (Closing())
    return;
  auto& bookmarks = FlyApp::Get().bookmarks();
  if (!bookmarks.Read().has_value()) {
    if (bookmark_picker_->active())
      bookmark_picker_->Cancel(true);
    return;
  }
  if (bookmark_picker_->active())
    bookmark_picker_->UpdateItems(BookmarkEntries(), bookmarks.CanEdit());
}

void FlyWindowController::OpenBookmark(content::WebContents* source,
                                       std::string url) {
  if (Closing() || ActiveContents() != source || !bookmark_urls_.contains(url))
    return;
  auto& app = FlyApp::Get();
  if (!app.bookmarks().Contains(url))
    return;
  app.OpenInNewTab(GURL(url), browser_);
}

void FlyWindowController::RemoveBookmark(content::WebContents* source,
                                         std::string url) {
  if (Closing() || ActiveContents() != source)
    return;
  auto& bookmarks = FlyApp::Get().bookmarks();
  if (bookmarks.Contains(url))
    bookmarks.Remove(url);
}

bool FlyWindowController::RunCommand(std::string_view action) {
  if (Closing())
    return false;
  auto& app = FlyApp::Get();
  // All pickers share the window's keyboard owner. Opening a different one
  // dismisses the previous one, while invoking the same action still toggles.
  for (auto [picker, command] :
       {std::pair{palette_.get(), "toggle-palette"},
        {address_picker_.get(), "toggle-address"},
        {history_picker_.get(), "toggle-history"},
        {bookmark_picker_.get(), "toggle-bookmarks"},
        {downloads_picker_.get(), "toggle-downloads"},
        {extensions_picker_.get(), "open-extension-actions"}}) {
    if (action != command)
      picker->Cancel();
  }
  if (action == "toggle-history") {
    ToggleHistory();
    return true;
  }
  if (action == "toggle-address") {
    ToggleAddress();
    return true;
  }
  if (action == "open-extension-actions") {
    ToggleExtensionActions();
    return true;
  }
  if (action == "open-diagnostics") {
    ShowDiagnostics();
    return true;
  }
  if (RunProductAction(action))
    return true;
  if (action == "move-tab-to-workspace")
    return ShowTabMovePicker(ActiveContents());
  if (action == "hibernate-tabs" || action == "wake-all-tabs") {
    // Suspending covers every window; waking, this window's workspace.
    std::vector<base::WeakPtr<content::WebContents>> tabs;
    for (auto* browser : app.BrowsersByActivation()) {
      if (action == "wake-all-tabs" &&
          browser->GetProfile() != browser_->GetProfile())
        continue;
      auto* model = browser->GetTabStripModel();
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
    // the window list while a tab mutation may reenter its owner.
    std::vector<base::WeakPtr<content::WebContents>> contents;
    bool any_unmuted = false;
    for (auto* browser : app.BrowsersByActivation()) {
      auto* model = browser->GetTabStripModel();
      for (int index = 0; index < model->count(); ++index) {
        auto* tab = model->GetWebContentsAt(index);
        contents.push_back(tab->GetWeakPtr());
        any_unmuted |= !tab->IsAudioMuted();
      }
    }
    if (action == "toggle-global-media") {
      std::vector<std::pair<base::WeakPtr<content::WebContents>, bool>>
          sessions;
      for (const auto& tab : contents) {
        auto* session =
            tab ? content::MediaSession::GetIfExists(tab.get()) : nullptr;
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
        auto* session =
            tab ? content::MediaSession::GetIfExists(tab.get()) : nullptr;
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
    auto* contents = ActiveContents();
    return contents &&
           app.bookmarks().Add(contents->GetLastCommittedURL().spec(),
                               base::UTF16ToUTF8(contents->GetTitle()));
  }
  if (action == "toggle-bookmarks") {
    if (!app.bookmarks().Read())
      return false;
    CancelHistory();
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    keyboard_editor_->Cancel();
    const auto config = app.commands().Read().value_or(DefaultKeyboardConfig());
    bookmark_picker_->ToggleItems(
        ActiveContents(), BookmarkEntries(),
        config.shortcuts.at("toggle-bookmarks"), u"Search bookmarks",
        app.bookmarks().CanEdit(), true);
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
    palette_->SetRecent(app.preferences().ReadRecentCommands());
    const auto config = app.commands().Read().value_or(DefaultKeyboardConfig());
    auto items = CommandCentreCatalog(config);
    for (auto& entry : ExtensionCommandEntries())
      items.push_back(std::move(entry));
    palette_->ToggleItems(ActiveContents(), std::move(items),
                          config.shortcuts.at("toggle-palette"),
                          u"Search commands and shortcuts", false, true);
    return true;
  }
  if (action == "toggle-private-mode") {
    app.TogglePrivate(browser_);
    return true;
  }
  if (action == "trigger-hints") {
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->Cancel();
    hints_->Toggle(ActiveContents());
    return true;
  }
  auto& preferences = app.preferences();
  if (action == "toggle-theme")
    return preferences.ToggleTheme();
  if (action == "toggle-zen-mode" || action == "toggle-sidebar-pin") {
    const auto collapse = preferences.ReadSidebarAutoCollapse();
    return collapse && preferences.SetSidebarAutoCollapse(!*collapse);
  }
  if (action == "toggle-window-controls") {
    const auto visible = preferences.ReadWindowControlsVisible();
    return visible && preferences.SetWindowControlsVisible(!*visible);
  }
  if (action == "toggle-status-bar") {
    const auto visible = preferences.ReadStatusBarVisible();
    return visible && preferences.SetStatusBarVisible(!*visible);
  }
  if (action == "cycle-clock-format")
    return preferences.CycleClockFormat();
  if (action == "toggle-clock") {
    const auto clock = preferences.ReadClock();
    return clock && preferences.SetClockVisible(!clock->visible);
  }
  if (action == "toggle-help" || action == "open-keyboard-settings") {
    hints_->Cancel();
    palette_->Cancel();
    address_picker_->Cancel();
    workspace_picker_->Cancel();
    tab_move_picker_->Cancel();
    bookmark_picker_->Cancel();
    CancelHistory();
    keyboard_editor_->SetPrivateMode(browser_->GetProfile()->IsOffTheRecord());
    if (action == "toggle-help" || !keyboard_editor_->active())
      keyboard_editor_->Toggle(ActiveContents());
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
    app.OpenAdjacentWorkspace(browser_, action == "next-workspace" ? 1 : -1);
    return true;
  }
  const bool handled = ExecuteNativeCommand(browser_, action);
  if (!handled && status_)
    status_->SetText(base::UTF8ToUTF16(ActionLabel(action) + ": unavailable"));
  return handled;
}

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
  const auto tabs =
      FlyApp::Get().commands().Read().value_or(DefaultKeyboardConfig()).tabs;
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
  if (Closing())
    return;
  if ((rename_widget_ && !rename_widget_->IsClosed()) ||
      (remove_widget_ && !remove_widget_->IsClosed()) ||
      (history_clear_widget_ && !history_clear_widget_->IsClosed()))
    return;
  const auto consume = [&](bool handled) {
    if (handled) {
      event->SetHandled();
      event->StopPropagation();
    }
  };
  if (diagnostics_hub_ && diagnostics_hub_->active()) {
    consume(diagnostics_hub_->HandleOverlayKey(*event));
    return;
  }
  for (auto* picker :
       {downloads_picker_.get(), extensions_picker_.get(),
        address_picker_.get(), tab_move_picker_.get(), workspace_picker_.get(),
        history_picker_.get(), bookmark_picker_.get()}) {
    if (!picker->active())
      continue;
    consume(picker->HandleOverlayKey(*event));
    return;
  }
  if (keyboard_editor_->active()) {
    consume(keyboard_editor_->HandleOverlayKey(*event));
    return;
  }
  if (event->type() != ui::EventType::kKeyPressed || event->is_char())
    return;
  if (palette_->active()) {
    consume(palette_->HandleOverlayKey(*event));
    return;
  }
  if (event->key_code() == ui::VKEY_ESCAPE && sidebar_focus_inside_) {
    ReturnFocusFromSidebar();
    consume(true);
    return;
  }
  auto* contents = ActiveContents();
  // Page events use BrowserView::PreHandleKeyboardEvent after native IME
  // processing. Do not dispatch them a second time at the window.
  if (!contents || (contents->GetRenderWidgetHostView() &&
                    contents->GetRenderWidgetHostView()->HasFocus()))
    return;
  auto* input = widget_->GetInputMethod();
  auto* client = input ? input->GetTextInputClient() : nullptr;
  const bool editable =
      client && client->GetTextInputType() != ui::TEXT_INPUT_TYPE_NONE;
  const input::NativeWebKeyboardEvent native(*event);
  consume(RouteNativePageKey(browser_, native, editable) ==
          content::KeyboardEventProcessingResult::HANDLED);
}

void FlyWindowController::UpdateTargetURL(content::WebContents* source,
                                          const GURL& url) {
  if (status_bar_ && !Closing())
    status_bar_->SetTargetURL(source, url);
}

void FlyWindowController::UpdateModeLabel() {
  if (!status_ || Closing())
    return;
  const std::u16string mode =
      hints_ && hints_->active()
          ? (hints_->truncated() ? u"HINTS (target limit)" : u"HINTS")
          : NativePageModeLabel(browser_);
  // The workspace badge says when the workspace is private.
  status_->SetText(mode);
}

// The mode badge: a click cancels hints, leaves passthrough, or
// enters it from NORMAL (INSERT ignores it).
void FlyWindowController::ClickModeBadge() {
  if (Closing())
    return;
  if (hints_ && hints_->active()) {
    InvokeNativeCommand(browser_, "trigger-hints");
    return;
  }
  const auto mode = NativePageModeLabel(browser_);
  if (mode == u"PASSTHROUGH" || mode == u"NORMAL")
    InvokeNativeCommand(browser_, "toggle-passthrough");
}

std::u16string FlyWindowController::status_text_for_testing() const {
  return status_bar_ ? status_bar_->message_for_testing() : std::u16string();
}

void FlyWindowController::ShowStatus(std::u16string text) {
  if (auto* app = FlyApp::GetIfCreated()) {
    app->RecordDiagnostic(
        text.starts_with(u"Update failed") || text.starts_with(u"Could not")
            ? "warn"
            : "info",
        "status", text);
  }
  // The status bar's link area, for a while.
  if (status_bar_)
    status_bar_->ShowMessage(std::move(text), base::Milliseconds(3500));
}

void FlyWindowController::ToggleDownloads() {
  if (Closing())
    return;
  hints_->Cancel();
  palette_->Cancel();
  address_picker_->Cancel();
  workspace_picker_->Cancel();
  tab_move_picker_->Cancel();
  bookmark_picker_->Cancel();
  CancelHistory();
  keyboard_editor_->Cancel();
  auto& app = FlyApp::Get();
  const auto config = app.commands().Read().value_or(DefaultKeyboardConfig());
  downloads_picker_->ToggleItems(
      ActiveContents(), DownloadEntries(app.WorkspaceProfiles()),
      config.shortcuts.at("toggle-downloads"), u"Search downloads", true, true);
}

void FlyWindowController::ShowDiagnostics() {
  if (Closing() || !diagnostics_hub_)
    return;
  hints_->Cancel();
  palette_->Cancel();
  keyboard_editor_->Cancel();
  diagnostics_hub_->Toggle();
}

bool FlyWindowController::RunProductAction(std::string_view action) {
  auto* contents = ActiveContents();
  auto* profile = browser_->GetProfile();
  auto& app = FlyApp::Get();
  if (action == "toggle-menu-bar" ||
      action == "toggle-native-menu-visibility") {
    auto point = widget_->GetRootView()->GetBoundsInScreen().origin();
    point.Offset(14, 14);
    settings_menu_->Show(widget_, point);
    return true;
  }
  if (action == "reload-user-content") {
    app.user_content().Reload();
    return true;
  }
  if (action == "open-extension-actions") {
    ToggleExtensionActions();
    return true;
  }
  if (action == "open-task-manager") {
    chrome::OpenTaskManager(browser_);
    return true;
  }
  if (action == "toggle-downloads") {
    // Toggles the download shelf; with nothing on it, Fly lists every
    // workspace's downloads.
    if (download_shelf_ && download_shelf_->has_cards())
      download_shelf_->Toggle();
    else
      ToggleDownloads();
    return true;
  }
  if (action == "toggle-reader-view") {
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
  if (action == "open-scripts-dir" || action == "open-styles-dir") {
    OpenUserContentDirectory(
        profile, action == "open-scripts-dir" ? "scripts" : "styles");
    return true;
  }
  if (action == "test-google-signin") {
    // A fresh off-the-record profile: no workspace cookies or tokens.
    NavigateParams params(
        profile->GetOriginalProfile()->GetPrimaryOTRProfile(
            /*create_if_needed=*/true),
        GURL("https://accounts.google.com/"),
        ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    return true;
  }
  if (action == "check-for-updates") {
    app.CheckUpdates(true, browser_);
    return true;
  }
  if (action == "show-whats-new") {
    ResolveWhatsNewURL(
        "", base::BindOnce(
                [](base::WeakPtr<FlyWindowController> window, GURL url) {
                  if (window && !window->Closing())
                    FlyApp::Get().OpenInNewTab(url, window->browser_);
                },
                weak_factory_.GetWeakPtr()));
    return true;
  }
  if (action == "open-diagnostics") {
    ShowDiagnostics();
    return true;
  }
  auto& preferences = app.preferences();
  if (action == "toggle-diagnostics-recording") {
    const bool enabled =
        !preferences.ReadBool("enable_diagnostics").value_or(false);
    if (preferences.SetBool("enable_diagnostics", enabled)) {
      if (!enabled)
        app.ClearDiagnostics();
      ShowStatus(enabled ? u"Diagnostics recording on (memory only)."
                         : u"Diagnostics recording off; events cleared.");
    }
    return true;
  }
  if (action == "toggle-notifications") {
    const bool enabled =
        !preferences.ReadBool("web_notifications").value_or(true);
    if (preferences.SetBool("web_notifications", enabled))
      ShowStatus(enabled ? u"Web notifications on."
                         : u"Web notifications off.");
    return true;
  }
  if (action == "focus-nav-buttons") {
    FocusSidebar();
    if (new_tab_button_ && sidebar_content_->Contains(
                               widget_->GetFocusManager()->GetFocusedView()))
      new_tab_button_->RequestFocus();
    return true;
  }
  return false;
}

views::View* FlyWindowController::extension_actions_for_testing() const {
  return status_bar_ ? status_bar_->extensions_container() : nullptr;
}

std::string FlyWindowController::extension_badge_for_testing(
    size_t index) const {
  auto* container = extension_actions_for_testing();
  auto* button = container && index < container->children().size()
                     ? views::AsViewClass<ExtensionActionButton>(
                           container->children()[index])
                     : nullptr;
  return button ? button->badge() : std::string();
}

void FlyWindowController::ScheduleExtensionRefresh() {
  if (extension_refresh_pending_ || Closing())
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
  if (Closing() || !status_bar_)
    return;
  auto& extensions = FlyApp::Get().extensions();
  auto* container = status_bar_->extensions_container();
  auto* contents = ActiveContents();
  // A new window has no tab until its first one is added.
  if (!contents) {
    container->RemoveAllChildViews();
    return;
  }
  const auto actions = extensions.ActionExtensions(browser_->GetProfile());
  // The same extensions in the same order update in place.
  bool same = container->children().size() == actions.size();
  for (size_t i = 0; same && i < actions.size(); ++i) {
    auto* button =
        views::AsViewClass<ExtensionActionButton>(container->children()[i]);
    same = button && button->id() == actions[i]->id();
  }
  if (same) {
    for (size_t i = 0; i < actions.size(); ++i) {
      views::AsViewClass<ExtensionActionButton>(container->children()[i])
          ->Update(extensions.ActionIcon(contents, *actions[i]),
                   extensions.ActionTitle(contents, *actions[i]),
                   extensions.ActionBadge(contents, *actions[i]));
    }
    return;
  }
  // Buttons may be the target of the click that led here: rebuild only after
  // the input has been dispatched (this runs from a posted task).
  container->RemoveAllChildViews();
  for (const auto* extension : actions) {
    const std::string id = extension->id();
    container->AddChildView(std::make_unique<ExtensionActionButton>(
        base::BindRepeating(
            [](base::WeakPtr<FlyWindowController> window, std::string id) {
              if (!window || window->Closing() || !window->status_bar_)
                return;
              for (const auto& child :
                   window->status_bar_->extensions_container()->children()) {
                auto* button =
                    views::AsViewClass<ExtensionActionButton>(child.get());
                if (button && button->id() == id) {
                  FlyApp::Get().extensions().RunAction(window->browser_, id,
                                                       button);
                  return;
                }
              }
            },
            weak_factory_.GetWeakPtr(), id),
        id, extensions.ActionIcon(contents, *extension),
        extensions.ActionTitle(contents, *extension),
        extensions.ActionBadge(contents, *extension)));
  }
  container->InvalidateLayout();
  status_bar_->InvalidateLayout();
}

void FlyWindowController::ToggleExtensionActions() {
  if (Closing())
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
  auto& app = FlyApp::Get();
  const auto config = app.commands().Read().value_or(DefaultKeyboardConfig());
  extensions_picker_->ToggleItems(
      ActiveContents(), app.extensions().ActionEntries(browser_->GetProfile()),
      config.shortcuts.at("open-extension-actions"), u"Search extensions",
      false, true);
}

void FlyWindowController::ChooseExtensionAction(content::WebContents*,
                                                std::string action) {
  if (Closing())
    return;
  auto& app = FlyApp::Get();
  if (action == "extensions-settings" || action == "extensions-open-settings") {
    RunCommand("toggle-help");
    keyboard_editor_->ShowExtensions();
    return;
  }
  if (action.starts_with("extension-options:")) {
    app.extensions().OpenOptions(
        browser_, action.substr(std::string_view("extension-options:").size()),
        browser_->GetProfile());
    return;
  }
  if (action == "extensions-reload") {
    ShowStatus(base::UTF8ToUTF16(
        "Extensions reloaded." +
        std::string(app.extensions().ReloadUnpacked() ? "" : " None to reload.")));
    return;
  }
  if (action == "extensions-page" || action == "extensions-store") {
    app.OpenInNewWindow(GURL(action == "extensions-page"
                                 ? "chrome://extensions/"
                                 : "https://chromewebstore.google.com/"),
                        browser_->GetProfile());
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
    anchor = widget_->GetRootView();
  }
  app.extensions().RunAction(browser_, id, anchor);
}

// The reader button is highlighted while the active tab shows the reader.
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
  content::WebContents* active = ActiveContents();
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
  content::WebContents* active = ActiveContents();
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
              if (!window || window->Closing())
                return;
              auto* contents = window->ActiveContents();
              QueryReaderView(
                  contents,
                  base::BindOnce(
                      [](base::WeakPtr<FlyWindowController> window,
                         base::WeakPtr<content::WebContents> contents,
                         bool open) {
                        if (window && contents && !open)
                          window->SetReaderActive(contents.get(), false);
                      },
                      window,
                      contents ? contents->GetWeakPtr()
                               : base::WeakPtr<content::WebContents>()));
            },
            weak_factory_.GetWeakPtr()));
  }
}

// Command Centre entries for the extensions' commands.
std::vector<PaletteEntry> FlyWindowController::ExtensionCommandEntries() {
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
  auto& extensions = FlyApp::Get().extensions();
  if (!extensions.Enabled() || Closing())
    return entries;
  add("extensions-reload", "Reload Extensions",
      "Re-read extension code from disk in every workspace using it",
      "Reloads unpacked extensions in every workspace. Pages that already ran "
      "the old content scripts need a reload.",
      "Use this after replacing an extension folder with a newer version.",
      {"extensions", "reload", "refresh", "update"});
  auto* profile = browser_->GetProfile();
  for (const auto* extension : extensions.ActionExtensions(profile))
    add("extension:" + extension->id(), extension->name() + ": Open Popup",
        "Open the extension popup for the active tab",
        extension->name() + " " + extension->VersionString(),
        "Press Esc or click outside the popup to close it.",
        {"extension", "popup", base::ToLowerASCII(extension->name())});
  for (const auto& info : extensions.List(profile)) {
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
  if (Closing())
    return;
  auto& app = FlyApp::Get();
  if (action.starts_with("open-window:")) {
    app.OpenInNewWindow(GURL(action.substr(12)), browser_->GetProfile());
    return;
  }
  auto* profile = browser_->GetProfile();
  auto* contents = ActiveContents();
  auto status = base::BindOnce(
      [](base::WeakPtr<FlyWindowController> window, std::u16string text) {
        if (window)
          window->ShowStatus(std::move(text));
      },
      weak_factory_.GetWeakPtr());
  if (action == "toggle-private-mode") {
    keyboard_editor_->Cancel();
    app.TogglePrivate(browser_);
  } else if (action == "clear-site-cookies") {
    ClearSiteCookies(profile,
                     contents ? contents->GetLastCommittedURL() : GURL(),
                     std::move(status));
  } else if (action == "clear-cache") {
    ClearWorkspaceCache(profile, std::move(status));
  } else if (action == "open-diagnostics" || action == "show-whats-new") {
    keyboard_editor_->Cancel();
    InvokeNativeCommand(browser_, action);
  } else if (action == "check-for-updates") {
    keyboard_editor_->SetUpdateStatus(u"Checking for updates…", "");
    app.CheckUpdates(false, browser_);
  } else if (action == "install-update") {
    app.InstallUpdate();
  } else if (action == "restart") {
    chrome::AttemptRestart();
  } else if (action == "desktop-entry" || action == "desktop-default") {
    app.UpdateDesktopIntegration(action == "desktop-default");
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
            profile->GetWeakPtr()));
  }
}
}  // namespace fly
