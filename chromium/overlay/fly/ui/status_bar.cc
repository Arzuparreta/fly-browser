// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/status_bar.h"
#include "fly/ui/new_tab_ui.h"

#include "base/i18n/case_conversion.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/omnibox/browser/vector_icons.h"
#include "components/url_formatter/url_formatter.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/fly_theme.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/views/widget/widget.h"
#include "ui/views/bubble/bubble_dialog_delegate_view.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/vector_icons.h"
#include "ui/views/view_class_properties.h"
#include "chrome/app/vector_icons/vector_icons.h"

namespace fly {
// A status bar badge: tinted fill and 1px outline.
class StatusBadge : public FlyButton {
  METADATA_HEADER(StatusBadge, FlyButton)

 public:
  enum class Tone { kNeutral, kSecure, kInsecure, kLocal, kPrivate, kPlain };
  StatusBadge(PressedCallback callback, const gfx::VectorIcon& icon)
      : FlyButton(std::move(callback), u"", FlyFace::kMono, 10) {
    SetFlyFont(FlyFace::kMono, 10, gfx::Font::Weight::BOLD);
    SetVectorIcon(icon, 10);
    SetCustomPadding(gfx::Insets::VH(1, 6));
    SetImageLabelSpacing(5);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetMinSize(gfx::Size(0, 16));
    if (!callback_set_)
      SetFocusBehavior(FocusBehavior::NEVER);
  }
  void SetTone(Tone tone) {
    if (tone_ == tone)
      return;
    tone_ = tone;
    SetAccentText(tone == Tone::kInsecure || tone == Tone::kPrivate);
    SchedulePaint();
  }
  void set_has_action(bool action) {
    callback_set_ = action;
    SetFocusBehavior(action ? FocusBehavior::ACCESSIBLE_ONLY
                            : FocusBehavior::NEVER);
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    SkColor fill = SkColorSetA(SK_ColorWHITE, 10);
    SkColor stroke = colors.border;
    switch (tone_) {
      case Tone::kSecure:
        fill = SkColorSetARGB(31, 158, 206, 106);
        stroke = SkColorSetARGB(64, 158, 206, 106);
        break;
      case Tone::kInsecure:
      case Tone::kPrivate:
        fill = SkColorSetARGB(38, 247, 118, 142);
        stroke = SkColorSetARGB(89, 247, 118, 142);
        break;
      case Tone::kLocal:
        fill = SkColorSetARGB(31, 125, 207, 255);
        stroke = SkColorSetARGB(64, 125, 207, 255);
        break;
      case Tone::kPlain:
      case Tone::kNeutral:
        break;
    }
    if (!colors.dark && fill == SkColorSetA(SK_ColorWHITE, 10))
      fill = SkColorSetA(SK_ColorBLACK, 10);
    const bool hot = callback_set_ && (GetVisualState() == STATE_HOVERED ||
                                       GetVisualState() == STATE_PRESSED);
    gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(hot ? colors.selected : fill);
    canvas->DrawRoundRect(bounds, 3, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(hot || HasFocus() ? colors.accent : stroke);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, 3, flags);
  }

 private:
  Tone tone_ = Tone::kNeutral;
  bool callback_set_ = false;
};
BEGIN_METADATA(StatusBadge)
END_METADATA

namespace {
// The mode badge: cell fill with an accent left edge and a 1px border tinted by
// the mode; a click cancels hints or leaves or enters passthrough.
class ModeLabel : public FlyLabel {
  METADATA_HEADER(ModeLabel, FlyLabel)

 public:
  explicit ModeLabel(base::RepeatingClosure pressed)
      : FlyLabel(u"NORMAL", FlyFace::kDisplay, 11, gfx::Font::Weight::BLACK),
        pressed_(std::move(pressed)) {
    SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(1, 8, 1, 6)));
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
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const std::u16string mode(GetText());
    SkColor border = SkColorSetARGB(77, 122, 162, 247);
    if (mode.starts_with(u"INSERT"))
      border = SkColorSetARGB(89, 158, 206, 106);
    else if (mode.starts_with(u"HINTS"))
      border = SkColorSetARGB(102, 224, 175, 104);
    else if (mode.starts_with(u"PASSTHROUGH"))
      border = SkColorSetARGB(102, 255, 158, 100);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(colors.cell);
    gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, 2, flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(border);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, 2, flags);
    canvas->FillRect(gfx::Rect(0, 0, 2, height()), colors.accent);
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    auto size = FlyLabel::CalculatePreferredSize(available);
    size.set_width(std::max(size.width(), 60));
    return size;
  }

 private:
  base::RepeatingClosure pressed_;
};
BEGIN_METADATA(ModeLabel)
END_METADATA
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
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(2, 10), 8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  SetBackground(FlyBackground(FlyRole::kSidebar));
  SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0)));
  mode_ = AddChildView(std::make_unique<ModeLabel>(std::move(actions.mode)));
  location_ = AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, 11, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kDim));
  location_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  location_->SetElideBehavior(gfx::ELIDE_MIDDLE);
  layout->SetFlexForView(location_, 1);
  zoom_action_ = std::move(actions.zoom);
  zoom_ = AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&StatusBar::ToggleZoomControls,
                          base::Unretained(this)),
      u"100%", FlyFace::kMono, 10));
  zoom_->SetVectorIcon(vector_icons::kSearchIcon, 11);
  zoom_->SetCustomPadding(gfx::Insets::VH(1, 6));
  zoom_->SetImageLabelSpacing(4);
  zoom_->SetOutlined(true);
  zoom_->SetTooltipText(u"Page zoom");
  extensions_ = AddChildView(std::make_unique<views::View>());
  extensions_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 2));
  notifications_ = AddChildView(MakeFlyIconButton(
      std::move(actions.notifications),
      vector_icons::kNotificationsIcon, u"Toggle web notifications", 18));
  notifications_->SetVectorIcon(vector_icons::kNotificationsIcon, 12);
  security_ = AddChildView(std::make_unique<StatusBadge>(
      views::Button::PressedCallback(), vector_icons::kLockIcon));
  workspace_ = AddChildView(std::make_unique<StatusBadge>(
      std::move(actions.workspace), vector_icons::kFolderOldIcon));
  workspace_->set_has_action(true);
  workspace_->GetViewAccessibility().SetName(u"Workspace");
  security_->GetViewAccessibility().SetName(u"Connection");
  clock_ = AddChildView(std::make_unique<StatusBadge>(
      std::move(actions.clock), kTimerIcon));
  clock_->set_has_action(true);
  clock_->GetViewAccessibility().SetName(u"Clock");
  clock_->SetFlyFont(FlyFace::kMono, 10, gfx::Font::Weight::MEDIUM);
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
  workspace_->SetText(private_ ? u"PRIVATE PROFILE"
                               : base::i18n::ToUpper(workspace));
  workspace_->SetTone(private_ ? StatusBadge::Tone::kPrivate
                               : StatusBadge::Tone::kNeutral);
  workspace_->SetVectorIcon(private_ ? views::kIncognitoIcon
                                     : vector_icons::kFolderOldIcon,
                            10);
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
    target_observer_->Set(nullptr);
    Observe(selected);
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
void StatusBar::UpdateZoom() {
  auto* zoom = web_contents()
                   ? zoom::ZoomController::FromWebContents(web_contents())
                   : nullptr;
  const int percent = zoom ? zoom->GetZoomPercent() : 100;
  zoom_->SetText(base::NumberToString16(percent) + u"%");
  zoom_->GetViewAccessibility().SetName(u"Page zoom: " +
                                        base::NumberToString16(percent) + u"%");
  zoom_->SetTooltipText(u"Page zoom: " + base::NumberToString16(percent) +
                        u"%");
  zoom_->SetEnabled(web_contents() != nullptr);
}
void StatusBar::Update() {
  // The location shows only a hovered link target.
  const auto location =
      target_.is_empty() ? message_ : url_formatter::FormatUrl(target_);
  location_->SetText(location);
  location_->SetTooltipText(location);
  location_->GetViewAccessibility().SetName(
      location.empty() ? std::u16string() : u"Link: " + location);
  UpdateZoom();
  if (!web_contents()) {
    security_->SetText(u"BLANK");
    security_->SetTone(StatusBadge::Tone::kNeutral);
    security_->SetTooltipText(u"Empty page");
    security_->GetViewAccessibility().SetName(u"Empty page");
    return;
  }
  const GURL page = web_contents()->GetVisibleURL();
  std::u16string text;
  const gfx::VectorIcon* icon = &vector_icons::kLockIcon;
  auto tone = StatusBadge::Tone::kNeutral;
  if (IsFlyNewTabPage(page)) {
    text = u"BLANK";
    icon = &vector_icons::kGlobeIcon;
  } else if (page.SchemeIs(url::kHttpsScheme)) {
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
      tone = StatusBadge::Tone::kInsecure;
      icon = &vector_icons::kDangerousFilledIcon;
      break;
    default:
      break;
  }
  security_->SetText(text);
  security_->SetVectorIcon(*icon, 10);
  security_->SetTone(tone);
  const auto formatted = url_formatter::FormatUrl(page);
  const auto tooltip =
      security.empty() ? text + u": " + formatted : security + u": " + formatted;
  security_->SetTooltipText(tooltip);
  security_->GetViewAccessibility().SetName(tooltip);
}
void StatusBar::OnTabStripModelChanged(TabStripModel*,
                                       const TabStripModelChange&,
                                       const TabStripSelectionChange&) {
  SelectContents();
}
void StatusBar::OnTabStripModelDestroyed(TabStripModel*) {
  model_ = nullptr;
  browser_.reset();
  zoom_observation_.Reset();
  Observe(nullptr);
  ClearTarget();
}
void StatusBar::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame()) {
    ClearTarget();
    if (contents_changed_ && !navigation->IsSameDocument())
      contents_changed_.Run();
  }
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
