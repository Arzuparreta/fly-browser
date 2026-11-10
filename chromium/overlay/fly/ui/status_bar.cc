// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/status_bar.h"
#include "fly/ui/new_tab_ui.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/url_formatter/url_formatter.h"
#include "ui/base/models/image_model.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/fly_theme.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "ui/views/widget/widget.h"

namespace fly {
namespace {
// The bar's height before the interface scale, without its top rule.
constexpr int kBarHeight = 24;
}  // namespace

// A status bar segment, as in i3bar: flat text after a hairline separator.
// Segments with an action light up under the pointer.
class StatusBadge : public FlyButton {
  METADATA_HEADER(StatusBadge, FlyButton)

 public:
  enum class Tone {
    kNeutral,   // Dim: the usual state, nothing to look at.
    kStrong,    // Text colour: a value that differs from its default.
    kSecure,
    kInsecure,  // Latón, the palette's warning colour.
    kDanger,
    kLocal,
    kPrivate,
  };
  StatusBadge(PressedCallback callback, const gfx::VectorIcon* icon = nullptr)
      : FlyButton(std::move(callback), u"", FlyFace::kMono, 10) {
    SetFlyFont(FlyFace::kMono, 10, gfx::Font::Weight::MEDIUM);
    if (icon)
      SetVectorIcon(*icon, 10);
    SetCustomPadding(gfx::Insets::VH(0, 9));
    SetImageLabelSpacing(5);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    // A segment is as wide as its text.
    SetMinSize(gfx::Size());
    SetFocusBehavior(FocusBehavior::NEVER);
  }
  void SetTone(Tone tone) {
    if (tone_ == tone)
      return;
    tone_ = tone;
    UpdateColors();
  }
  Tone tone() const { return tone_; }
  void set_has_action(bool action) {
    has_action_ = action;
    SetFocusBehavior(action ? FocusBehavior::ACCESSIBLE_ONLY
                            : FocusBehavior::NEVER);
    UpdateColors();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    auto size = FlyButton::CalculatePreferredSize(available);
    size.set_height(FlyScaled(kBarHeight));
    return size;
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hot = has_action_ && (GetVisualState() == STATE_HOVERED ||
                                     GetVisualState() == STATE_PRESSED);
    if (hot || selected())
      canvas->FillRect(GetLocalBounds(), FlyHoverColor());
    // The separator from the segment on the left.
    const int inset = FlyScaled(7);
    canvas->FillRect(gfx::Rect(0, inset, 1, height() - 2 * inset),
                     colors.border);
    // A change worth a glance (the zoom just moved) or keyboard focus.
    if (selected() || HasFocus()) {
      canvas->FillRect(gfx::Rect(1, height() - FlyScaled(2), width() - 1,
                                 FlyScaled(2)),
                       colors.accent);
    }
  }

 protected:
  void UpdateColors() override {
    FlyButton::UpdateColors();
    const auto& colors = GetFlyColors();
    SkColor rest = colors.dim;
    switch (tone_) {
      case Tone::kStrong:
        rest = colors.text;
        break;
      case Tone::kInsecure:
        rest = colors.warning;
        break;
      case Tone::kDanger:
      case Tone::kPrivate:
        rest = colors.accent;
        break;
      case Tone::kLocal:
        rest = colors.link;
        break;
      case Tone::kSecure:
      case Tone::kNeutral:
        break;
    }
    // Only the quiet tones brighten under the pointer; a warning keeps its
    // colour.
    const SkColor hot = has_action_ && rest == colors.dim ? colors.text : rest;
    SetTextColor(STATE_NORMAL, rest);
    SetTextColor(STATE_HOVERED, hot);
    SetTextColor(STATE_PRESSED, hot);
    if (const auto* icon = vector_icon()) {
      for (const auto& [state, color] :
           {std::pair{STATE_NORMAL, rest}, std::pair{STATE_HOVERED, hot},
            std::pair{STATE_PRESSED, hot}}) {
        views::LabelButton::SetImageModel(
            state, ui::ImageModel::FromVectorIcon(*icon, color, FlyScaled(10)));
      }
    }
  }

 private:
  Tone tone_ = Tone::kNeutral;
  bool has_action_ = false;
};
BEGIN_METADATA(StatusBadge)
END_METADATA

namespace {
// The mode block, flush with the bar's left edge like i3bar's binding mode
// indicator. NORMAL rests on a cell; every other mode is a solid block, so a
// glance at the corner says where the keys are going. A click cancels hints,
// or leaves or enters passthrough.
class ModeLabel : public FlyLabel {
  METADATA_HEADER(ModeLabel, FlyLabel)

 public:
  explicit ModeLabel(base::RepeatingClosure pressed)
      : FlyLabel(u"NORMAL", FlyFace::kMono, 10, gfx::Font::Weight::MEDIUM),
        pressed_(std::move(pressed)) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(0, 10)));
    SetHorizontalAlignment(gfx::ALIGN_CENTER);
    SetTooltipText(u"Keyboard mode");
    GetViewAccessibility().SetRole(ax::mojom::Role::kButton);
  }
  // Labels take no events unless selectable; this one is a button.
  bool GetCanProcessEventsWithinSubtree() const override { return true; }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    return event.IsOnlyLeftMouseButton();
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (pressed_ && HitTestPoint(event.location()))
      pressed_.Run();
  }
  SkColor Fill() const {
    const auto& colors = GetFlyColors();
    const std::u16string_view mode = GetText();
    if (mode.starts_with(u"INSERT"))
      return colors.text;
    if (mode.starts_with(u"HINTS"))
      return colors.warning;
    if (mode.starts_with(u"PASSTHROUGH"))
      return colors.accent;
    return colors.cell;
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    canvas->FillRect(GetLocalBounds(), Fill());
  }
  void OnPaint(gfx::Canvas* canvas) override {
    // The text follows the block it sits on.
    const SkColor fill = Fill();
    const SkColor ink =
        fill == GetFlyColors().cell ? GetFlyColors().text : FlyInkOn(fill);
    if (ink != ink_) {
      ink_ = ink;
      SetEnabledColor(ink);
    }
    FlyLabel::OnPaint(canvas);
  }
  void OnThemeChanged() override {
    FlyLabel::OnThemeChanged();
    ink_.reset();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    auto size = FlyLabel::CalculatePreferredSize(available);
    // NORMAL and INSERT are as wide as each other, so the bar does not shift
    // every time a text field takes focus.
    size.set_width(std::max(size.width(), FlyScaled(68)));
    size.set_height(FlyScaled(kBarHeight));
    return size;
  }

 private:
  base::RepeatingClosure pressed_;
  std::optional<SkColor> ink_;
};
BEGIN_METADATA(ModeLabel)
END_METADATA

// What the bar shows for the page itself: the site, then the rest of the
// address. Fly's new tab has no address to show.
struct PageAddress {
  std::u16string site;
  std::u16string rest;
};
PageAddress DescribePage(const GURL& url) {
  if (url.is_empty() || IsFlyNewTabPage(url))
    return {};
  if (!url.SchemeIsHTTPOrHTTPS() || !url.has_host())
    return {std::u16string(), url_formatter::FormatUrl(url)};
  std::u16string site = url_formatter::IDNToUnicode(url.host());
  if (site.starts_with(u"www."))
    site.erase(0, 4);
  if (url.has_port())
    site += u":" + base::UTF8ToUTF16(url.port());
  std::string rest = url.PathForRequest();
  if (url.has_ref()) {
    rest += "#";
    rest += url.ref();
  }
  if (rest == "/")
    rest.clear();
  return {std::move(site), base::UTF8ToUTF16(rest)};
}
}  // namespace

// Zoom out, reset and zoom in, above the zoom pill.
std::unique_ptr<views::View> MakeZoomControls(
    base::RepeatingCallback<void(std::string)> action) {
  auto controls = std::make_unique<views::View>();
  controls->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
  for (const auto& [name, text, label] :
       {std::tuple{"zoom-out", u"\u2212", u"Zoom out"},
        std::tuple{"reset-zoom", u"100%", u"Reset zoom to 100%"},
        std::tuple{"zoom-in", u"+", u"Zoom in"}}) {
    auto* button = controls->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(action, std::string(name)), text, FlyFace::kMono,
        12));
    button->SetCustomPadding(gfx::Insets::VH(4, 10));
    button->SetOutlined(true);
    button->SetTooltipText(label);
    button->GetViewAccessibility().SetName(label);
  }
  return controls;
}

class StatusBar::TargetObserver : public content::WebContentsObserver {
 public:
  explicit TargetObserver(StatusBar* owner) : owner_(owner) {}
  void Set(content::WebContents* contents) { Observe(contents); }
  void OnVisibilityChanged(content::Visibility visibility) override {
    if (visibility != content::Visibility::VISIBLE)
      owner_->ClearTarget();
  }
  void PrimaryPageChanged(content::Page&) override { owner_->ClearTarget(); }
  void WebContentsDestroyed() override { owner_->ClearTarget(); }

 private:
  raw_ptr<StatusBar> owner_;
};
StatusBar::StatusBar(Actions actions)
    : contents_changed_(std::move(actions.contents_changed)),
      target_observer_(std::make_unique<TargetObserver>(this)) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetBackground(FlyBackground(FlyRole::kSidebar));
  SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0)));
  mode_ = AddChildView(std::make_unique<ModeLabel>(std::move(actions.mode)));
  // The keys of an unfinished sequence, as vim's showcmd does.
  keys_ = AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, 11, gfx::Font::Weight::MEDIUM,
      FlyLabel::Tone::kAccent));
  keys_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 8, 0, 0));
  keys_->SetVisible(false);
  reload_ = AddChildView(MakeFlyIconButton(
      base::BindRepeating(&StatusBar::ReloadOrStop, base::Unretained(this)),
      vector_icons::kRefreshIcon, u"Reload page", 20));
  reload_->SetVectorIcon(vector_icons::kRefreshIcon, 12);
  reload_->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 4, 0, 4));
  site_ = AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, 11, gfx::Font::Weight::MEDIUM));
  site_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  site_->SetVisible(false);
  // The address line holds either the page's own address (its site, then
  // the rest in `path_`) or, in `location_`, what replaces it for a moment:
  // a hovered link, the loading destination or a message.
  for (auto* label : {&path_, &location_}) {
    *label = AddChildView(std::make_unique<FlyLabel>(
        u"", FlyFace::kMono, 11, gfx::Font::Weight::NORMAL,
        FlyLabel::Tone::kDim));
    (*label)->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    (*label)->SetElideBehavior(gfx::ELIDE_TAIL);
    (*label)->SetProperty(views::kMarginsKey, gfx::Insets::TLBR(0, 0, 0, 8));
    layout->SetFlexForView(*label, 1);
  }
  path_->SetVisible(false);
  extensions_ = AddChildView(std::make_unique<views::View>());
  extensions_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
  notifications_ = AddChildView(MakeFlyIconButton(
      std::move(actions.notifications),
      vector_icons::kNotificationsIcon, u"Toggle web notifications", 18));
  notifications_->SetVectorIcon(vector_icons::kNotificationsIcon, 12);
  notifications_->SetProperty(views::kMarginsKey,
                              gfx::Insets::TLBR(0, 4, 0, 6));
  tabs_ = AddChildView(
      std::make_unique<StatusBadge>(views::Button::PressedCallback()));
  tabs_->SetVisible(false);
  zoom_action_ = std::move(actions.zoom);
  zoom_ = AddChildView(std::make_unique<StatusBadge>(base::BindRepeating(
      &StatusBar::ToggleZoomControls, base::Unretained(this))));
  zoom_->SetText(u"100%");
  zoom_->set_has_action(true);
  zoom_->SetTooltipText(u"Page zoom");
  security_ = AddChildView(std::make_unique<StatusBadge>(
      views::Button::PressedCallback(), &vector_icons::kLockIcon));
  workspace_ = AddChildView(
      std::make_unique<StatusBadge>(std::move(actions.workspace)));
  workspace_->set_has_action(true);
  workspace_->GetViewAccessibility().SetName(u"Workspace");
  security_->GetViewAccessibility().SetName(u"Connection");
  clock_ = AddChildView(
      std::make_unique<StatusBadge>(std::move(actions.clock)));
  clock_->set_has_action(true);
  clock_->GetViewAccessibility().SetName(u"Clock");
  clock_->SetVisible(false);
}
StatusBar::~StatusBar() {
  zoom_observation_.Reset();
  target_observer_.reset();
  Observe(nullptr);
  if (model_)
    model_->RemoveObserver(this);
}
views::Label* StatusBar::mode_label() const {
  return mode_;
}
views::Label* StatusBar::location_for_testing() const {
  return location_;
}
FlyButton* StatusBar::security_for_testing() const {
  return security_;
}
FlyButton* StatusBar::workspace_for_testing() const {
  return workspace_;
}
FlyButton* StatusBar::zoom_for_testing() const {
  return zoom_;
}
void StatusBar::SetBrowser(BrowserWindowInterface* browser,
                           std::u16string workspace) {
  if (model_)
    model_->RemoveObserver(this);
  browser_ = browser ? browser->GetWeakPtr() : nullptr;
  model_ = browser ? browser->GetTabStripModel() : nullptr;
  if (model_)
    model_->AddObserver(this);
  private_ = browser && browser->GetProfile()->IsOffTheRecord();
  workspace_->SetText(private_ ? u"PRIVATE"
                               : base::i18n::ToUpper(workspace));
  workspace_->SetTone(private_ ? StatusBadge::Tone::kPrivate
                               : StatusBadge::Tone::kNeutral);
  const std::u16string name = private_ ? u"Private" : workspace;
  workspace_->SetTooltipText(u"Workspace: " + name +
                             u" (click to manage workspaces)");
  workspace_->GetViewAccessibility().SetName(u"Workspace: " + name);
  target_ = GURL();
  target_observer_->Set(nullptr);
  Observe(nullptr);
  SelectContents();
}
FlyButton* StatusBar::clock_for_testing() const {
  return clock_;
}
void StatusBar::SetClock(const std::u16string& text,
                         const std::u16string& tooltip,
                         bool visible) {
  clock_->SetVisible(visible);
  clock_->SetText(text);
  clock_->SetTooltipText(tooltip);
  clock_->GetViewAccessibility().SetName(tooltip);
}
void StatusBar::SetClockVisible(bool visible) {
  // A hidden or paused clock keeps its last text and accessible name.
  clock_->SetVisible(visible && !clock_->GetText().empty());
}
void StatusBar::SetNotificationsEnabled(bool enabled) {
  notifications_->SetVectorIcon(enabled ? vector_icons::kNotificationsIcon
                                        : vector_icons::kNotificationsOffIcon,
                                12);
  notifications_->SetTooltipText(enabled ? u"Web notifications on"
                                         : u"Web notifications off");
  notifications_->GetViewAccessibility().SetName(
      notifications_->GetTooltipText());
}
void StatusBar::SelectContents() {
  auto* selected = model_ ? model_->GetActiveWebContents() : nullptr;
  if (selected != web_contents()) {
    // Another page: its zoom controls and feedback do not carry over.
    if (zoom_widget_)
      zoom_widget_->Close();
    zoom_flash_.Stop();
    zoom_->SetSelected(false);
    target_ = GURL();
    pending_url_ = GURL();
    pending_navigation_id_ = 0;
    message_.clear();
    message_timer_.Stop();
    target_observer_->Set(nullptr);
    Observe(selected);
    if (selected && selected->HasUncommittedNavigationInPrimaryMainFrame()) {
      if (auto* entry = selected->GetController().GetPendingEntry())
        pending_url_ = entry->GetURL();
    }
    zoom_observation_.Reset();
    if (auto* zoom =
            selected ? zoom::ZoomController::FromWebContents(selected)
                     : nullptr)
      zoom_observation_.Observe(zoom);
    if (contents_changed_)
      contents_changed_.Run();
  }
  Update();
}
void StatusBar::SetTargetURL(content::WebContents* source, const GURL& url) {
  // Both split panes can be visible. Accept the native pointer's visible pane,
  // while rejecting messages from hidden tabs or another profile/window model.
  if (!model_ || !source ||
      model_->GetIndexOfWebContents(source) == TabStripModel::kNoTab ||
      source->GetVisibility() != content::Visibility::VISIBLE)
    return;
  if (url.is_empty()) {
    if (target_observer_->web_contents() == source)
      ClearTarget();
    return;
  }
  target_ = url;
  message_.clear();
  message_timer_.Stop();
  target_observer_->Set(source);
  Update();
}
void StatusBar::ShowMessage(std::u16string text, base::TimeDelta timeout) {
  message_ = std::move(text);
  message_timer_.Stop();
  if (!timeout.is_zero()) {
    message_timer_.Start(FROM_HERE, timeout,
                         base::BindOnce(
                             [](StatusBar* bar) {
                               bar->message_.clear();
                               bar->Update();
                             },
                             base::Unretained(this)));
  }
  Update();
}
void StatusBar::ToggleZoomControls() {
  if (zoom_widget_) {
    zoom_widget_->Close();
    return;
  }
  zoom_delegate_ = std::make_unique<views::BubbleDialogDelegate>(
      zoom_, views::BubbleBorder::BOTTOM_CENTER,
      views::BubbleBorder::DIALOG_SHADOW, /*autosize=*/true);
  zoom_delegate_->set_margins(gfx::Insets(4));
  zoom_delegate_->set_corner_radius(6);
  zoom_delegate_->SetShowCloseButton(false);
  zoom_delegate_->SetButtons(static_cast<int>(ui::mojom::DialogButton::kNone));
  zoom_delegate_->SetBackgroundColor(GetFlyColors().bg_sidebar);
  zoom_delegate_->SetContentsView(MakeZoomControls(base::BindRepeating(
      [](StatusBar* bar, std::string action) {
        if (bar->zoom_action_)
          bar->zoom_action_.Run(action);
      },
      base::Unretained(this))));
  // The widget is ours; it is released after its close finishes.
  zoom_widget_ = views::BubbleDialogDelegate::CreateBubble(
      zoom_delegate_.get(),
      base::BindOnce(
          [](base::WeakPtr<StatusBar> bar, views::Widget::ClosedReason) {
            if (!bar)
              return;
            base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
                FROM_HERE, std::move(bar->zoom_widget_));
            base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
                FROM_HERE, std::move(bar->zoom_delegate_));
          },
          weak_factory_.GetWeakPtr()));
  zoom_widget_->Show();
}
bool StatusBar::zoom_controls_open_for_testing() const {
  return zoom_widget_ != nullptr;
}
void StatusBar::ClearTarget() {
  target_ = GURL();
  target_observer_->Set(nullptr);
  Update();
}
void StatusBar::SetPendingKeys(std::u16string keys) {
  keys_timer_.Stop();
  keys_->SetText(keys);
  keys_->SetVisible(!keys.empty());
  // The router drops an unfinished sequence after 700 ms.
  if (!keys.empty()) {
    keys_timer_.Start(FROM_HERE, base::Milliseconds(700),
                      base::BindOnce(&StatusBar::SetPendingKeys,
                                     base::Unretained(this),
                                     std::u16string()));
  }
}
std::u16string StatusBar::pending_keys_for_testing() const {
  return keys_->GetVisible() ? std::u16string(keys_->GetText())
                             : std::u16string();
}
std::u16string StatusBar::address_for_testing() const {
  return (site_->GetVisible() ? std::u16string(site_->GetText())
                              : std::u16string()) +
         (path_->GetVisible() ? std::u16string(path_->GetText())
                              : std::u16string());
}
FlyButton* StatusBar::tabs_for_testing() const {
  return tabs_;
}
void StatusBar::UpdateTabPosition() {
  const int count = model_ ? model_->count() : 0;
  const int index = model_ ? model_->active_index() : -1;
  tabs_->SetVisible(count > 0 && index >= 0);
  if (!tabs_->GetVisible())
    return;
  const auto position = base::NumberToString16(index + 1);
  const auto total = base::NumberToString16(count);
  tabs_->SetText(position + u"/" + total);
  const auto name = u"Tab " + position + u" of " + total;
  tabs_->SetTooltipText(name);
  tabs_->GetViewAccessibility().SetName(name);
}
void StatusBar::UpdateZoom() {
  auto* zoom = web_contents()
                   ? zoom::ZoomController::FromWebContents(web_contents())
                   : nullptr;
  const int percent = zoom ? zoom->GetZoomPercent() : 100;
  zoom_->SetText(base::NumberToString16(percent) + u"%");
  zoom_->SetTone(percent == 100 ? StatusBadge::Tone::kNeutral
                                : StatusBadge::Tone::kStrong);
  zoom_->GetViewAccessibility().SetName(u"Page zoom: " +
                                        base::NumberToString16(percent) + u"%");
  zoom_->SetTooltipText(u"Page zoom: " + base::NumberToString16(percent) +
                        u"%");
  zoom_->SetEnabled(web_contents() != nullptr);
}
void StatusBar::Update() {
  const bool loading = web_contents() && web_contents()->ShouldShowLoadingUI();
  reload_->SetEnabled(web_contents() != nullptr);
  reload_->SetVectorIcon(
      loading ? vector_icons::kCloseIcon : vector_icons::kRefreshIcon, 12);
  const auto* reload_label = loading ? u"Stop loading" : u"Reload page";
  reload_->SetTooltipText(reload_label);
  reload_->GetViewAccessibility().SetName(reload_label);
  reload_->SetIconTone(loading ? FlyButton::IconTone::kAccent
                               : FlyButton::IconTone::kDim);
  // A hovered link remains inspectable during loading. Otherwise show the
  // destination immediately, even while the previous document is still drawn.
  // With nothing else to say, the bar shows where the page is.
  std::u16string location = message_;
  PageAddress page_address;
  bool link = false;
  if (!target_.is_empty()) {
    location = url_formatter::FormatUrl(target_);
    link = true;
  } else if (location.empty() && loading) {
    const GURL& destination = pending_url_.is_empty()
                                  ? web_contents()->GetVisibleURL()
                                  : pending_url_;
    const auto host = destination.has_host()
                          ? base::UTF8ToUTF16(destination.host())
                          : u"page";
    location = (pending_url_.is_empty() ? u"Loading " : u"Opening ") + host +
               u"…";
  } else if (location.empty() && web_contents()) {
    page_address = DescribePage(web_contents()->GetVisibleURL());
  }
  site_->SetText(page_address.site);
  site_->SetVisible(!page_address.site.empty());
  path_->SetText(page_address.rest);
  path_->SetVisible(location.empty());
  const auto address = page_address.site + page_address.rest;
  path_->SetTooltipText(address);
  path_->GetViewAccessibility().SetName(u"Address: " + address);
  location_->SetText(location);
  location_->SetVisible(!location.empty());
  location_->SetTooltipText(location);
  location_->GetViewAccessibility().SetName(link ? u"Link: " + location
                                                 : location);
  UpdateTabPosition();
  UpdateZoom();
  if (!web_contents()) {
    security_->SetVisible(false);
    return;
  }
  const GURL page = web_contents()->GetVisibleURL();
  std::u16string text;
  const gfx::VectorIcon* icon = &vector_icons::kLockIcon;
  auto tone = StatusBadge::Tone::kNeutral;
  // Fly's own new tab and an empty page have no connection to describe.
  const bool blank =
      IsFlyNewTabPage(page) || page.is_empty() || page.IsAboutBlank();
  if (page.SchemeIs(url::kHttpsScheme)) {
    text = u"HTTPS";
  } else if (page.SchemeIs(url::kHttpScheme)) {
    text = u"HTTP";
    icon = &vector_icons::kNotSecureWarningOldIcon;
  } else if (page.SchemeIsFile()) {
    text = u"FILE";
    icon = &vector_icons::kCodeIcon;
    tone = StatusBadge::Tone::kLocal;
  } else {
    text = base::i18n::ToUpper(base::UTF8ToUTF16(page.scheme()));
    icon = &vector_icons::kGlobeIcon;
  }
  std::u16string security;
  switch (chrome_security_state::GetSecurityLevel(web_contents())) {
    case security_state::SECURE:
      security = u"Page secure";
      tone = StatusBadge::Tone::kSecure;
      break;
    case security_state::WARNING:
      security = u"Page not secure";
      tone = StatusBadge::Tone::kInsecure;
      break;
    case security_state::DANGEROUS:
      security = u"Page security warning";
      tone = StatusBadge::Tone::kDanger;
      icon = &vector_icons::kDangerousFilledIcon;
      break;
    default:
      break;
  }
  security_->SetVisible(!blank);
  security_->SetText(text);
  security_->SetVectorIcon(*icon, 10);
  security_->SetTone(tone);
  const auto formatted = url_formatter::FormatUrl(page);
  const auto tooltip =
      security.empty() ? text + u": " + formatted : security + u": " + formatted;
  security_->SetTooltipText(tooltip);
  security_->GetViewAccessibility().SetName(tooltip);
}
void StatusBar::ReloadOrStop() {
  auto* contents = web_contents();
  if (!contents)
    return;
  if (contents->ShouldShowLoadingUI())
    contents->Stop();
  else
    contents->GetController().Reload(content::ReloadType::NORMAL, true);
}
void StatusBar::OnTabStripModelChanged(TabStripModel*,
                                       const TabStripModelChange&,
                                       const TabStripSelectionChange&) {
  SelectContents();
}
gfx::Size StatusBar::CalculatePreferredSize(
    const views::SizeBounds& available) const {
  auto size = views::View::CalculatePreferredSize(available);
  size.set_height(FlyScaled(kBarHeight) + GetInsets().height());
  return size;
}
void StatusBar::OnTabStripModelDestroyed(TabStripModel*) {
  model_ = nullptr;
  browser_.reset();
  zoom_observation_.Reset();
  Observe(nullptr);
  ClearTarget();
}
void StatusBar::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame() &&
      (navigation->GetNavigationId() == pending_navigation_id_ ||
       !pending_navigation_id_)) {
    pending_url_ = GURL();
    pending_navigation_id_ = 0;
    Update();
  }
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame()) {
    ClearTarget();
    if (contents_changed_ && !navigation->IsSameDocument())
      contents_changed_.Run();
  }
}
void StatusBar::DidStartNavigation(content::NavigationHandle* navigation) {
  if (!navigation->IsInPrimaryMainFrame() || navigation->IsSameDocument())
    return;
  pending_url_ = navigation->GetURL();
  pending_navigation_id_ = navigation->GetNavigationId();
  message_.clear();
  message_timer_.Stop();
  ClearTarget();
}
void StatusBar::DidRedirectNavigation(content::NavigationHandle* navigation) {
  if (navigation->IsInPrimaryMainFrame() &&
      (navigation->GetNavigationId() == pending_navigation_id_ ||
       !pending_navigation_id_)) {
    pending_navigation_id_ = navigation->GetNavigationId();
    pending_url_ = navigation->GetURL();
    Update();
  }
}
void StatusBar::DidStartLoading() {
  Update();
}
void StatusBar::DidStopLoading() {
  pending_url_ = GURL();
  pending_navigation_id_ = 0;
  Update();
}
void StatusBar::DidChangeVisibleSecurityState() {
  Update();
}
void StatusBar::WebContentsDestroyed() {
  zoom_observation_.Reset();
  Observe(nullptr);
  ClearTarget();
}
void StatusBar::OnZoomControllerDestroyed(zoom::ZoomController*) {
  zoom_observation_.Reset();
}
void StatusBar::OnZoomChanged(
    const zoom::ZoomController::ZoomChangedEventData&) {
  UpdateZoom();
  // .zoom-feedback: the pill lights for 1.6 s after a change.
  zoom_->SetSelected(true);
  zoom_flash_.Start(FROM_HERE, base::Milliseconds(1600),
                    base::BindOnce(&FlyButton::SetSelected,
                                   base::Unretained(zoom_.get()), false));
}
void StatusBar::SetPaperGrain(gfx::ImageSkia grain) {
  paper_grain_ = std::move(grain);
  SchedulePaint();
}
void StatusBar::OnPaintBackground(gfx::Canvas* canvas) {
  views::View::OnPaintBackground(canvas);
  if (!GetFlyColors().dark && !paper_grain_.isNull())
    canvas->TileImageInt(paper_grain_, 0, 0, width(), height());
}
BEGIN_METADATA(StatusBar)
END_METADATA
}  // namespace fly
