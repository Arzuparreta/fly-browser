// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_menu.h"

#include <algorithm>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/tabs/public/tab_interface.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/focus_ring.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
namespace {
constexpr int kChipHeight = 18;
constexpr int kChipPadding = 6;

int Scaled(int pixels) {
  return FlyScaled(pixels);
}

gfx::FontList ChipFont() {
  return FlyFont(FlyFace::kMono, kMenuCaptionSize);
}

void PaintFocusOutline(gfx::Canvas* canvas, gfx::RectF bounds, float radius) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1.5f);
  flags.setColor(GetFlyColors().accent);
  bounds.Inset(0.75f);
  canvas->DrawRoundRect(bounds, radius, flags);
}

void CollectFocusable(views::View* view, std::vector<views::View*>& out) {
  if (!view->GetVisible())
    return;
  if (view->IsFocusable() && view->IsDrawn())
    out.push_back(view);
  for (views::View* child : view->children())
    CollectFocusable(child, out);
}
}  // namespace

const gfx::VectorIcon& MenuIcon(MenuKind kind) {
  switch (kind) {
    case MenuKind::kCommands:
      return kBoltIcon;
    case MenuKind::kAddress:
      return vector_icons::kSearchIcon;
    case MenuKind::kHistory:
      return vector_icons::kHistoryIcon;
    case MenuKind::kBookmarks:
      return kStarIcon;
    case MenuKind::kDownloads:
      return vector_icons::kDownloadIcon;
    case MenuKind::kExtensions:
      return vector_icons::kExtensionFilledIcon;
    case MenuKind::kWorkspaces:
      return kGridViewIcon;
    case MenuKind::kTabMove:
      return kTabIcon;
    case MenuKind::kPreferences:
      return vector_icons::kSettingsIcon;
    case MenuKind::kDiagnostics:
      return kSpeedIcon;
  }
}

std::unique_ptr<views::Widget> CreateMenuWidget(views::Widget* desktop,
                                                const gfx::Rect& bounds,
                                                const char* name) {
  auto widget = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = desktop->GetNativeWindow();
  params.child = true;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.native_widget = new views::NativeWidgetAura(widget.get());
  params.name = name;
  params.bounds = bounds;
  widget->Init(std::move(params));
  return widget;
}

gfx::Rect MenuWidgetBounds(views::Widget* desktop, const gfx::Insets& insets) {
  gfx::Rect bounds = desktop->GetContentsView()->GetBoundsInScreen();
  bounds.Inset(insets);
  wm::ConvertRectFromScreen(desktop->GetNativeWindow(), &bounds);
  return bounds;
}

std::vector<base::CallbackListSubscription> WatchMenuSourceTab(
    content::WebContents* contents,
    base::RepeatingClosure close) {
  std::vector<base::CallbackListSubscription> subscriptions;
  auto* tab =
      contents ? tabs::TabInterface::MaybeGetFromContents(contents) : nullptr;
  if (!tab)
    return subscriptions;
  subscriptions.push_back(tab->RegisterWillDeactivate(base::BindRepeating(
      [](base::RepeatingClosure close, tabs::TabInterface*) { close.Run(); },
      close)));
  subscriptions.push_back(tab->RegisterWillDetach(base::BindRepeating(
      [](base::RepeatingClosure close, tabs::TabInterface*,
         tabs::TabInterface::DetachReason) { close.Run(); },
      close)));
  return subscriptions;
}

MenuScrim::MenuScrim(SkColor scrim, base::RepeatingClosure dismiss)
    : dismiss_(std::move(dismiss)) {
  if (SkColorGetA(scrim))
    SetBackground(views::CreateSolidBackground(scrim));
}
MenuScrim::~MenuScrim() = default;
views::View* MenuScrim::SetPanel(std::unique_ptr<views::View> panel) {
  if (panel_)
    RemoveChildViewT(panel_.get());
  panel_ = AddChildView(std::move(panel));
  return panel_;
}
void MenuScrim::SetPanelBounds(const gfx::Rect& bounds) {
  bounds_ = bounds;
  grows_ = false;
  InvalidateLayout();
}
void MenuScrim::SetPanelGrowsTo(const gfx::Rect& bounds_at_max_height) {
  bounds_ = bounds_at_max_height;
  grows_ = true;
  InvalidateLayout();
}
void MenuScrim::Layout(PassKey) {
  if (!panel_)
    return;
  auto bounds = bounds_;
  if (grows_)
    bounds.set_height(std::min(
        bounds_.height(), panel_->GetHeightForWidth(bounds_.width())));
  panel_->SetBoundsRect(bounds);
}
bool MenuScrim::OnMousePressed(const ui::MouseEvent& event) {
  if (panel_ && panel_->bounds().Contains(event.location()))
    return false;
  // Close after this event: the menu may destroy this widget.
  if (dismiss_)
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                                dismiss_);
  return true;
}
BEGIN_METADATA(MenuScrim)
END_METADATA

MenuPanel::MenuPanel() = default;
MenuPanel::~MenuPanel() = default;
void MenuPanel::OnPaintBackground(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(FlyRoleColor(FlyRole::kOverlaySidebar));
  canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), Scaled(kMenuRadius),
                        flags);
}
void MenuPanel::OnPaintBorder(gfx::Canvas* canvas) {
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(FlyRoleColor(FlyRole::kBorder));
  gfx::RectF bounds(GetLocalBounds());
  bounds.Inset(0.5f);
  canvas->DrawRoundRect(bounds, Scaled(kMenuRadius), flags);
}
BEGIN_METADATA(MenuPanel)
END_METADATA

MenuBadge::MenuBadge(MenuKind kind) : kind_(kind) {
  GetViewAccessibility().SetIsIgnored(true);
}
MenuBadge::~MenuBadge() = default;
gfx::Size MenuBadge::CalculatePreferredSize(const views::SizeBounds&) const {
  return gfx::Size(Scaled(24), Scaled(24));
}
void MenuBadge::OnPaint(gfx::Canvas* canvas) {
  const auto& colors = GetFlyColors();
  const int side = std::min(width(), height());
  const gfx::Rect square((width() - side) / 2, (height() - side) / 2, side,
                         side);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  flags.setColor(colors.accent);
  canvas->DrawRoundRect(gfx::RectF(square), Scaled(6), flags);
  const int icon = Scaled(16);
  canvas->Save();
  canvas->Translate(gfx::Vector2d(square.x() + (side - icon) / 2,
                                  square.y() + (side - icon) / 2));
  gfx::PaintVectorIcon(canvas, MenuIcon(kind_), icon,
                       color_utils::GetColorWithMaxContrast(colors.accent));
  canvas->Restore();
}
BEGIN_METADATA(MenuBadge)
END_METADATA

FlyTextfield::FlyTextfield(std::u16string accessible_name,
                           std::u16string placeholder,
                           int size)
    : size_(size) {
  views::FocusRing::Remove(this);
  RemoveHoverEffect();
  GetViewAccessibility().SetName(accessible_name);
  SetPlaceholderText(placeholder.empty() ? accessible_name : placeholder);
  SetFontList(FlyFont(FlyFace::kSans, size_));
  UpdateLook();
}
FlyTextfield::~FlyTextfield() = default;
void FlyTextfield::SetSearchStyle(bool search) {
  search_ = search;
  UpdateLook();
  PreferredSizeChanged();
}
gfx::Size FlyTextfield::CalculatePreferredSize(
    const views::SizeBounds& available) const {
  auto size = views::Textfield::CalculatePreferredSize(available);
  size.set_height(Scaled(search_ ? 34 : kMenuControlHeight));
  return size;
}
void FlyTextfield::OnThemeChanged() {
  views::Textfield::OnThemeChanged();
  SetFontList(FlyFont(FlyFace::kSans, size_));
  UpdateLook();
}
void FlyTextfield::OnFocus() {
  views::Textfield::OnFocus();
  UpdateLook();
}
void FlyTextfield::OnBlur() {
  views::Textfield::OnBlur();
  UpdateLook();
}
void FlyTextfield::UpdateLook() {
  const auto& colors = GetFlyColors();
  const bool focused = HasFocus();
  const int left = search_ ? Scaled(32) : Scaled(8);
  SetBorder(FlyBorder(focused ? FlyRole::kAccent : FlyRole::kBorder, 1,
                      Scaled(search_ ? 6 : 4),
                      gfx::Insets::TLBR(0, left, 0, Scaled(8))));
  SetBackgroundColor(search_ ? colors.bg_main : colors.cell);
  SetColor(GetEnabled() ? colors.text : colors.dim);
  SchedulePaint();
}
void FlyTextfield::OnPaint(gfx::Canvas* canvas) {
  views::Textfield::OnPaint(canvas);
  if (!search_)
    return;
  const int icon = Scaled(16);
  canvas->Save();
  canvas->Translate(gfx::Vector2d(Scaled(10), (height() - icon) / 2));
  gfx::PaintVectorIcon(canvas, vector_icons::kSearchIcon, icon,
                       HasFocus() ? GetFlyColors().accent
                                  : GetFlyColors().dim);
  canvas->Restore();
}
BEGIN_METADATA(FlyTextfield)
END_METADATA

MenuHeader::MenuHeader(MenuKind kind, std::u16string title) {
  auto* layout = SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), Scaled(10)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  AddChildView(std::make_unique<MenuBadge>(kind));
  if (!title.empty()) {
    title_ = AddChildView(std::make_unique<FlyLabel>(
        std::move(title), FlyFace::kSans, 14, gfx::Font::Weight::SEMIBOLD));
    title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
  SetMinimumCrossAxisSize(Scaled(34));
}
MenuHeader::~MenuHeader() = default;
void MenuHeader::SetFlexibleTitle() {
  if (!title_)
    return;
  title_->SetElideBehavior(gfx::ELIDE_TAIL);
  static_cast<views::BoxLayout*>(GetLayoutManager())->SetFlexForView(title_, 1);
}
FlyTextfield* MenuHeader::SetSearch(std::unique_ptr<FlyTextfield> search) {
  search->SetSearchStyle(true);
  auto* field = AddChildView(std::move(search));
  static_cast<views::BoxLayout*>(GetLayoutManager())->SetFlexForView(field, 1);
  return field;
}
FlyLabel* MenuHeader::AddStatus() {
  auto* status = AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, kMenuCaptionSize, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kDim));
  status->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  return status;
}
void MenuHeader::AddClose(base::RepeatingClosure close) {
  auto button = MakeFlyIconButton(
      base::BindRepeating([](base::RepeatingClosure close) { close.Run(); },
                          std::move(close)),
      vector_icons::kCloseIcon, u"Close (Esc)", 28);
  // Escape closes every menu; Tab stays among the menu's own controls.
  button->SetFocusBehavior(FocusBehavior::ACCESSIBLE_ONLY);
  AddChildView(std::move(button));
}
void MenuHeader::SetMinimumCrossAxisSize(int size) {
  static_cast<views::BoxLayout*>(GetLayoutManager())
      ->set_minimum_cross_axis_size(size);
}
BEGIN_METADATA(MenuHeader)
END_METADATA

void StyleMenuButton(FlyButton* button, MenuButtonStyle style) {
  button->SetFlyFont(FlyFace::kSans, kMenuSmallTextSize,
                     style == MenuButtonStyle::kPrimary
                         ? gfx::Font::Weight::SEMIBOLD
                         : gfx::Font::Weight::NORMAL);
  button->SetCustomPadding(style == MenuButtonStyle::kQuiet
                               ? gfx::Insets::VH(3, 8)
                               : gfx::Insets::VH(5, 12));
  button->SetCornerRadius(Scaled(4));
  button->SetOutlined(style != MenuButtonStyle::kQuiet);
  button->SetAccentText(style == MenuButtonStyle::kPrimary);
  button->SetPrimary(style == MenuButtonStyle::kPrimary);
  button->SetMinSize(gfx::Size(
      0, Scaled(style == MenuButtonStyle::kQuiet ? 24 : kMenuControlHeight)));
}

std::unique_ptr<FlyButton> MakeMenuButton(views::Button::PressedCallback callback,
                                          std::u16string text,
                                          MenuButtonStyle style) {
  auto button = std::make_unique<FlyButton>(std::move(callback), text);
  StyleMenuButton(button.get(), style);
  return button;
}

FlyToggle::FlyToggle(PressedCallback callback, std::u16string accessible_name)
    : views::Button(std::move(callback)) {
  SetInstallFocusRingOnFocus(false);
  SetFocusBehavior(FocusBehavior::ALWAYS);
  auto& accessibility = GetViewAccessibility();
  accessibility.SetRole(ax::mojom::Role::kSwitch);
  accessibility.SetName(accessible_name);
  accessibility.SetCheckedState(ax::mojom::CheckedState::kFalse);
}
FlyToggle::~FlyToggle() = default;
void FlyToggle::SetChecked(bool checked) {
  if (checked_ == checked)
    return;
  checked_ = checked;
  GetViewAccessibility().SetCheckedState(
      checked ? ax::mojom::CheckedState::kTrue
              : ax::mojom::CheckedState::kFalse);
  SchedulePaint();
}
gfx::Size FlyToggle::CalculatePreferredSize(const views::SizeBounds&) const {
  return gfx::Size(Scaled(36), Scaled(22));
}
void FlyToggle::NotifyClick(const ui::Event& event) {
  SetChecked(!checked_);
  views::Button::NotifyClick(event);
}
void FlyToggle::OnFocus() {
  views::Button::OnFocus();
  SchedulePaint();
}
void FlyToggle::OnBlur() {
  views::Button::OnBlur();
  SchedulePaint();
}
void FlyToggle::StateChanged(ButtonState old_state) {
  views::Button::StateChanged(old_state);
  SchedulePaint();
}
void FlyToggle::PaintButtonContents(gfx::Canvas* canvas) {
  const auto& colors = GetFlyColors();
  const bool enabled = GetEnabled();
  const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
  gfx::RectF track(GetContentsBounds());
  const float track_height = Scaled(18);
  track = gfx::RectF(track.x() + Scaled(1), track.CenterPoint().y() -
                                                 track_height / 2,
                     track.width() - Scaled(2), track_height);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  SkColor fill = checked_ ? colors.accent : colors.cell;
  if (!enabled)
    fill = color_utils::AlphaBlend(fill, colors.bg_sidebar, 0.4f);
  flags.setColor(fill);
  canvas->DrawRoundRect(track, track_height / 2, flags);
  if (!checked_) {
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(hot && enabled ? colors.dim : colors.border);
    auto outline = track;
    outline.Inset(0.5f);
    canvas->DrawRoundRect(outline, track_height / 2, flags);
    flags.setStyle(cc::PaintFlags::kFill_Style);
  }
  const float radius = track_height / 2 - Scaled(3);
  const float x = checked_ ? track.right() - track_height / 2
                           : track.x() + track_height / 2;
  SkColor thumb = checked_ ? color_utils::GetColorWithMaxContrast(colors.accent)
                           : colors.dim;
  if (!enabled)
    thumb = color_utils::AlphaBlend(thumb, colors.bg_sidebar, 0.5f);
  flags.setColor(thumb);
  canvas->DrawCircle(gfx::PointF(x, track.CenterPoint().y()), radius, flags);
  if (HasFocus()) {
    auto ring = track;
    ring.Outset(Scaled(2));
    PaintFocusOutline(canvas, ring, ring.height() / 2);
  }
}
BEGIN_METADATA(FlyToggle)
END_METADATA

FlySelect::FlySelect(std::vector<std::u16string> items,
                     base::RepeatingCallback<void(size_t)> changed,
                     std::u16string accessible_name)
    : FlyButton(PressedCallback(), u""),
      items_(std::move(items)),
      changed_(std::move(changed)) {
  SetCallback(base::BindRepeating(&FlySelect::ShowList, base::Unretained(this)));
  StyleMenuButton(this, MenuButtonStyle::kSecondary);
  SetHorizontalAlignment(gfx::ALIGN_LEFT);
  SetCustomPadding(gfx::Insets::TLBR(5, 10, 5, 28));
  auto& accessibility = GetViewAccessibility();
  accessibility.SetRole(ax::mojom::Role::kComboBoxSelect);
  accessibility.SetName(accessible_name);
}
FlySelect::~FlySelect() {
  if (runner_)
    runner_->Cancel();
}
void FlySelect::SetSelectedIndex(std::optional<size_t> index) {
  if (index && *index >= items_.size())
    index.reset();
  selected_ = index;
  const std::u16string text = selected_ ? items_[*selected_] : u"—";
  SetText(text);
  GetViewAccessibility().SetValue(text);
}
void FlySelect::Choose(size_t index) {
  if (index >= items_.size() || !GetEnabled())
    return;
  SetSelectedIndex(index);
  if (changed_)
    changed_.Run(index);
}
gfx::Size FlySelect::CalculatePreferredSize(
    const views::SizeBounds& available) const {
  const auto font = FlyFont(FlyFace::kSans, kMenuSmallTextSize);
  int text = 0;
  for (const auto& item : items_)
    text = std::max(text, gfx::GetStringWidth(item, font));
  const auto insets = GetInsets();
  return gfx::Size(text + insets.width() + Scaled(2),
                   std::max(Scaled(kMenuControlHeight),
                            font.GetHeight() + insets.height()));
}
void FlySelect::PaintButtonContents(gfx::Canvas* canvas) {
  FlyButton::PaintButtonContents(canvas);
  const int icon = Scaled(16);
  canvas->Save();
  canvas->Translate(
      gfx::Vector2d(width() - icon - Scaled(8), (height() - icon) / 2));
  gfx::PaintVectorIcon(canvas, vector_icons::kKeyboardArrowDownIcon, icon,
                       GetEnabled() ? GetFlyColors().text : GetFlyColors().dim);
  canvas->Restore();
}
bool FlySelect::OnKeyPressed(const ui::KeyEvent& event) {
  if (!GetEnabled() || items_.empty())
    return false;
  if (event.key_code() == ui::VKEY_UP || event.key_code() == ui::VKEY_DOWN) {
    const size_t count = items_.size();
    size_t next = selected_.value_or(0);
    if (selected_)
      next = event.key_code() == ui::VKEY_DOWN ? std::min(next + 1, count - 1)
                                               : (next ? next - 1 : 0);
    if (next != selected_)
      Choose(next);
    return true;
  }
  return FlyButton::OnKeyPressed(event);
}
void FlySelect::ShowList() {
  if (!GetWidget() || items_.empty())
    return;
  model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (size_t i = 0; i < items_.size(); ++i)
    model_->AddCheckItem(static_cast<int>(i), items_[i]);
  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::COMBOBOX);
  runner_->RunMenuAt(GetWidget(), nullptr, GetBoundsInScreen(),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kNone);
}
bool FlySelect::IsCommandIdChecked(int command_id) const {
  return selected_ && static_cast<int>(*selected_) == command_id;
}
void FlySelect::ExecuteCommand(int command_id, int) {
  if (command_id >= 0)
    Choose(static_cast<size_t>(command_id));
}
BEGIN_METADATA(FlySelect)
END_METADATA

FlySlider::FlySlider(views::SliderListener* listener) : views::Slider(listener) {
  SetRenderingStyle(RenderingStyle::kDefaultStyle);
}
FlySlider::~FlySlider() = default;
SkColor FlySlider::GetThumbColor() const {
  return GetEnabled() ? GetFlyColors().accent : GetFlyColors().dim;
}
SkColor FlySlider::GetTroughColor() const {
  return GetFlyColors().border;
}
BEGIN_METADATA(FlySlider)
END_METADATA

int KeyChipWidth(std::u16string_view key) {
  return gfx::GetStringWidth(std::u16string(key), FlyFont(FlyFace::kMono,
                                                           kMenuCaptionSize)) +
         2 * Scaled(kChipPadding);
}

int PaintKeyChip(gfx::Canvas* canvas,
                 int x,
                 int centre_y,
                 std::u16string_view key,
                 ChipTone tone) {
  const auto& colors = GetFlyColors();
  const int width = KeyChipWidth(key);
  const int height = Scaled(kChipHeight);
  const gfx::Rect chip(x, centre_y - height / 2, width, height);
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  if (tone != ChipTone::kQuiet) {
    flags.setColor(colors.cell);
    canvas->DrawRoundRect(gfx::RectF(chip), Scaled(4), flags);
  }
  flags.setStyle(cc::PaintFlags::kStroke_Style);
  flags.setStrokeWidth(1);
  flags.setColor(tone == ChipTone::kAccent ? colors.accent : colors.border);
  auto outline = gfx::RectF(chip);
  outline.Inset(0.5f);
  canvas->DrawRoundRect(outline, Scaled(4), flags);
  canvas->DrawStringRectWithFlags(
      std::u16string(key), ChipFont(),
      tone == ChipTone::kAccent  ? colors.accent
      : tone == ChipTone::kQuiet ? colors.dim
                                 : colors.text,
      chip, gfx::Canvas::TEXT_ALIGN_CENTER | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  return width;
}

namespace {
// The chips that fit in `space`, with "+n" for the rest.
std::vector<std::u16string> FittingChips(int space,
                                         const std::vector<std::u16string>& keys) {
  const int gap = Scaled(kKeyChipGap);
  int total = 0;
  size_t shown = 0;
  for (size_t i = 0; i < keys.size(); ++i) {
    const int width = KeyChipWidth(keys[i]) + (shown ? gap : 0);
    const int rest =
        i + 1 < keys.size()
            ? KeyChipWidth(u"+" + base::NumberToString16(keys.size() - i - 1)) +
                  gap
            : 0;
    if (space < total + width + rest)
      break;
    total += width;
    ++shown;
  }
  std::vector<std::u16string> painted(keys.begin(), keys.begin() + shown);
  if (shown < keys.size())
    painted.push_back(u"+" + base::NumberToString16(keys.size() - shown));
  return painted;
}
int ChipsWidth(const std::vector<std::u16string>& keys) {
  int width = 0;
  for (size_t i = 0; i < keys.size(); ++i)
    width += KeyChipWidth(keys[i]) + (i ? Scaled(kKeyChipGap) : 0);
  return width;
}
}  // namespace

int PaintKeyChipsRightAligned(gfx::Canvas* canvas,
                              int left,
                              int right,
                              int centre_y,
                              const std::vector<std::u16string>& keys,
                              ChipTone tone) {
  if (keys.empty())
    return right;
  const auto painted = FittingChips(right - left, keys);
  int x = std::max(left, right - ChipsWidth(painted));
  const int start = x;
  for (const auto& key : painted)
    x += PaintKeyChip(canvas, x, centre_y, key, tone) + Scaled(kKeyChipGap);
  return start;
}

int PaintKeyChipsLeftAligned(gfx::Canvas* canvas,
                             int left,
                             int right,
                             int centre_y,
                             const std::vector<std::u16string>& keys,
                             ChipTone tone) {
  int x = left;
  for (const auto& key : FittingChips(right - left, keys))
    x += PaintKeyChip(canvas, x, centre_y, key, tone) + Scaled(kKeyChipGap);
  return x;
}

KeyHints::KeyHints(std::vector<Hint> hints) : hints_(std::move(hints)) {
  GetViewAccessibility().SetRole(ax::mojom::Role::kStaticText);
  SetHints(std::move(hints_));
}
KeyHints::~KeyHints() = default;
void KeyHints::SetHints(std::vector<Hint> hints) {
  hints_ = std::move(hints);
  std::u16string description;
  for (const auto& [key, label] : hints_)
    description += key + u": " + label + u". ";
  GetViewAccessibility().SetName(description);
  PreferredSizeChanged();
  SchedulePaint();
}
gfx::Size KeyHints::CalculatePreferredSize(const views::SizeBounds& available) const {
  const auto font = FlyFont(FlyFace::kSans, kMenuSmallTextSize);
  const int limit = std::max(1, available.width().value_or(100000));
  int width = 0, row = 0, lines = 1;
  for (const auto& [key, label] : hints_) {
    const int item = KeyChipWidth(key) + Scaled(6) + gfx::GetStringWidth(label, font) + Scaled(16);
    if (row && row + item > limit) { width = std::max(width, row); row = 0; ++lines; }
    row += item;
  }
  return gfx::Size(std::min(limit, std::max(width, row)), lines * Scaled(26));
}
void KeyHints::OnPaint(gfx::Canvas* canvas) {
  const auto font = FlyFont(FlyFace::kSans, kMenuSmallTextSize);
  const auto& colors = GetFlyColors();
  int x = 0;
  int y = 0;
  const int row_height = Scaled(26);
  for (const auto& [key, label] : hints_) {
    const int label_width = gfx::GetStringWidth(label, font);
    const int item = KeyChipWidth(key) + Scaled(6) + label_width + Scaled(16);
    if (x && x + item > width()) { x = 0; y += row_height; }
    x += PaintKeyChip(canvas, x, y + row_height / 2, key) + Scaled(6);
    canvas->DrawStringRectWithFlags(
        label, font, colors.dim,
        gfx::Rect(x, y, std::min(label_width, std::max(0, width() - x)), row_height),
        gfx::Canvas::NO_SUBPIXEL_RENDERING);
    x += label_width + Scaled(16);
  }
}
BEGIN_METADATA(KeyHints)
END_METADATA

std::unique_ptr<FlyLabel> MakeMenuCaption(std::u16string text) {
  auto label = std::make_unique<FlyLabel>(std::move(text), FlyFace::kMono,
                                          kMenuCaptionSize,
                                          gfx::Font::Weight::MEDIUM,
                                          FlyLabel::Tone::kDim);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}

views::View* FocusedMenuView(views::View* root) {
  auto* manager = root ? root->GetFocusManager() : nullptr;
  auto* focused = manager ? manager->GetFocusedView() : nullptr;
  return focused && root->Contains(focused) ? focused : nullptr;
}

bool CycleMenuFocus(views::View* root, bool reverse) {
  if (!root)
    return false;
  std::vector<views::View*> focusable;
  CollectFocusable(root, focusable);
  if (focusable.empty())
    return false;
  auto* focused = FocusedMenuView(root);
  const auto found = std::ranges::find(focusable, focused);
  const int count = static_cast<int>(focusable.size());
  int index;
  if (found == focusable.end())
    index = reverse ? count - 1 : 0;
  else
    index = (static_cast<int>(found - focusable.begin()) +
             (reverse ? count - 1 : 1)) % count;
  focusable[index]->RequestFocus();
  focusable[index]->ScrollViewToVisible();
  return true;
}

}  // namespace fly
