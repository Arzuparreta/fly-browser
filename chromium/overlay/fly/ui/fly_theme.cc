// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_theme.h"

#include <utility>

#include "base/base_paths.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/path_service.h"
#include "build/build_config.h"
#include "cc/paint/paint_flags.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkRRect.h"
#include "ui/views/view_utils.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/animation/ink_drop.h"
#include "ui/views/view.h"

#if BUILDFLAG(IS_LINUX)
#include <fontconfig/fontconfig.h>
#include "ui/gfx/linux/fontconfig_util.h"
#endif

namespace fly {
namespace {
FlyColors& Palette() {
  static FlyColors colors = DefaultFlyColors(true);
  return colors;
}
class RoleBackground : public views::Background {
 public:
  RoleBackground(FlyRole role, float radius) : role_(role), radius_(radius) {
    SetColor(FlyRoleColor(role_));
  }
  // The resolved colour also tells Label that text is painted on an opaque
  // surface (subpixel rendering).
  void OnViewThemeChanged(views::View* view) override {
    SetColor(FlyRoleColor(role_));
    view->SchedulePaint();
  }
  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(FlyRoleColor(role_));
    if (radius_ > 0)
      canvas->DrawRoundRect(gfx::RectF(view->GetLocalBounds()), radius_,
                            flags);
    else
      canvas->DrawRect(view->GetLocalBounds(), flags);
  }

 private:
  const FlyRole role_;
  const float radius_;
};
class RoleBorder : public views::Border {
 public:
  RoleBorder(FlyRole role, int thickness, float radius, gfx::Insets padding)
      : role_(role),
        thickness_(thickness),
        radius_(radius),
        padding_(padding) {}
  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(thickness_);
    flags.setColor(FlyRoleColor(role_));
    gfx::RectF bounds(view.GetLocalBounds());
    bounds.Inset(thickness_ / 2.0f);
    canvas->DrawRoundRect(bounds, radius_, flags);
  }
  gfx::Insets GetInsets() const override {
    return gfx::Insets(thickness_) + padding_;
  }
  gfx::Size GetMinimumSize() const override {
    return gfx::Size(GetInsets().width(), GetInsets().height());
  }

 private:
  const FlyRole role_;
  const int thickness_;
  const float radius_;
  const gfx::Insets padding_;
};
class EdgeBorder : public views::Border {
 public:
  EdgeBorder(FlyRole role, gfx::Insets edges, gfx::Insets padding)
      : role_(role), edges_(edges), padding_(padding) {}
  void Paint(const views::View& view, gfx::Canvas* canvas) override {
    const auto bounds = view.GetLocalBounds();
    const SkColor color = FlyRoleColor(role_);
    if (edges_.top())
      canvas->FillRect(gfx::Rect(0, 0, bounds.width(), edges_.top()), color);
    if (edges_.bottom())
      canvas->FillRect(gfx::Rect(0, bounds.height() - edges_.bottom(),
                                 bounds.width(), edges_.bottom()),
                       color);
    if (edges_.left())
      canvas->FillRect(gfx::Rect(0, 0, edges_.left(), bounds.height()), color);
    if (edges_.right())
      canvas->FillRect(gfx::Rect(bounds.width() - edges_.right(), 0,
                                 edges_.right(), bounds.height()),
                       color);
  }
  gfx::Insets GetInsets() const override { return edges_ + padding_; }
  gfx::Size GetMinimumSize() const override {
    return gfx::Size(GetInsets().width(), GetInsets().height());
  }

 private:
  const FlyRole role_;
  const gfx::Insets edges_;
  const gfx::Insets padding_;
};
}  // namespace

std::unique_ptr<views::Border> FlyEdgeBorder(FlyRole role,
                                             const gfx::Insets& edges,
                                             const gfx::Insets& padding) {
  return std::make_unique<EdgeBorder>(role, edges, padding);
}

FlyColors DefaultFlyColors(bool dark) {
  FlyColors colors;
  colors.dark = dark;
  colors.accent = SkColorSetRGB(0xe0, 0x26, 0x1c);
  colors.warning = SkColorSetRGB(0xc6, 0xa0, 0x4a);
  if (dark) {
    colors.bg_main = SkColorSetRGB(0x12, 0x12, 0x11);
    colors.bg_sidebar = SkColorSetRGB(0x0b, 0x0b, 0x0a);
    colors.text = SkColorSetRGB(0xe7, 0xe1, 0xd3);
    colors.border = SkColorSetRGB(0x2b, 0x2a, 0x27);
    colors.selected = SkColorSetRGB(0x1b, 0x1a, 0x18);
    colors.panel = SkColorSetRGB(0x1b, 0x1a, 0x18);
    colors.cell = SkColorSetRGB(0x22, 0x21, 0x1e);
    colors.dim = SkColorSetRGB(0x8a, 0x85, 0x7a);
    colors.link = SkColorSetRGB(0x6a, 0xa2, 0xf5);
  } else {
    colors.bg_main = SkColorSetRGB(0xec, 0xe6, 0xd8);
    colors.bg_sidebar = SkColorSetRGB(0xe3, 0xdc, 0xcb);
    colors.text = SkColorSetRGB(0x1a, 0x18, 0x14);
    colors.border = SkColorSetRGB(0xcf, 0xc6, 0xb2);
    colors.selected = SkColorSetRGB(0xf3, 0xee, 0xe3);
    colors.panel = SkColorSetRGB(0xf3, 0xee, 0xe3);
    colors.cell = SkColorSetRGB(0xd8, 0xd0, 0xbd);
    colors.dim = SkColorSetRGB(0x7a, 0x73, 0x65);
    colors.link = SkColorSetRGB(0x24, 0x57, 0xb8);
  }
  return colors;
}
const FlyColors& GetFlyColors() {
  return Palette();
}
bool SetFlyColors(const FlyColors& colors) {
  auto& current = Palette();
  const bool changed =
      current.dark != colors.dark || current.accent != colors.accent ||
      current.bg_main != colors.bg_main ||
      current.bg_sidebar != colors.bg_sidebar || current.text != colors.text ||
      current.border != colors.border || current.selected != colors.selected ||
      current.panel != colors.panel || current.cell != colors.cell ||
      current.dim != colors.dim || current.link != colors.link ||
      current.sidebar_opacity != colors.sidebar_opacity ||
      current.overlay_opacity != colors.overlay_opacity;
  current = colors;
  return changed;
}
SkColor FlyRoleColor(FlyRole role) {
  const auto& colors = GetFlyColors();
  switch (role) {
    case FlyRole::kMain:
      return colors.bg_main;
    case FlyRole::kSidebar:
      return colors.bg_sidebar;
    case FlyRole::kPanel:
      return colors.panel;
    case FlyRole::kCell:
      return colors.cell;
    case FlyRole::kSelected:
      return colors.selected;
    case FlyRole::kBorder:
      return colors.border;
    case FlyRole::kAccent:
      return colors.accent;
    case FlyRole::kSidebarSurface:
      return color_utils::AlphaBlend(colors.bg_sidebar, colors.bg_main,
                                     colors.sidebar_opacity / 100.f);
    case FlyRole::kOverlayMain:
      return SkColorSetA(colors.bg_main, colors.overlay_opacity * 255 / 100);
    case FlyRole::kOverlaySidebar:
      return SkColorSetA(colors.bg_sidebar,
                         colors.overlay_opacity * 255 / 100);
  }
}
void DisableSubpixelText(views::View* root) {
  if (!root)
    return;
  if (auto* label = views::AsViewClass<views::Label>(root))
    label->SetSubpixelRenderingEnabled(false);
  for (views::View* child : root->children())
    DisableSubpixelText(child);
}
namespace {
float g_interface_scale = 1.0f;
}  // namespace
void SetFlyInterfaceScale(float scale) {
  g_interface_scale = scale;
}
float GetFlyInterfaceScale() {
  return g_interface_scale;
}
int FlyScaled(int pixels) {
  return static_cast<int>(pixels * g_interface_scale + 0.5f);
}
gfx::FontList FlyFont(FlyFace face, int size, gfx::Font::Weight weight) {
  size = static_cast<int>(size * g_interface_scale + 0.5f);
  if (face == FlyFace::kDisplay) {
    // The bundled Doto.ttf names its family "Doto Black" (a variable font
    // whose weights are named instances).
    return gfx::FontList({"Doto Black", "Doto", "monospace"},
                         gfx::Font::NORMAL, size, weight);
  }
  const char* family =
      face == FlyFace::kSans ? "IBM Plex Sans" : "IBM Plex Mono";
  return gfx::FontList({family, face == FlyFace::kSans ? "sans" : "monospace"},
                       gfx::Font::NORMAL, size, weight);
}
void RegisterFlyFonts() {
#if BUILDFLAG(IS_LINUX)
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  base::FilePath dir;
  if (!base::PathService::Get(base::DIR_ASSETS, &dir))
    return;
  dir = dir.AppendASCII("fly_fonts");
  if (!base::DirectoryExists(dir))
    return;
  FcConfigAppFontAddDir(
      gfx::GetGlobalFontConfig(),
      reinterpret_cast<const FcChar8*>(dir.value().c_str()));
#endif
}
std::unique_ptr<views::Background> FlyBackground(FlyRole role, float radius) {
  return std::make_unique<RoleBackground>(role, radius);
}
std::unique_ptr<views::Border> FlyBorder(FlyRole role,
                                         int thickness,
                                         float radius,
                                         const gfx::Insets& padding) {
  return std::make_unique<RoleBorder>(role, thickness, radius, padding);
}

FlyButton::FlyButton(PressedCallback callback,
                     std::u16string_view text,
                     FlyFace face,
                     int size)
    : views::MdTextButton(std::move(callback), text),
      face_(face),
      size_(size) {
  SetCustomPadding(gfx::Insets::VH(4, 8));
  SetCornerRadius(2);
  SetInstallFocusRingOnFocus(false);
  views::InkDrop::Get(this)->SetMode(views::InkDropHost::InkDropMode::OFF);
  SetHasInkDropActionOnClick(false);
  label()->SetFontList(FlyFont(face_, size_, weight_));
  label()->SetElideBehavior(gfx::ELIDE_TAIL);
  // Rows and badges paint over translucent overlays and scroll layers.
  label()->SetSubpixelRenderingEnabled(false);
}
FlyButton::~FlyButton() = default;
void FlyButton::SetSelected(bool selected) {
  if (selected_ == selected)
    return;
  selected_ = selected;
  UpdateColors();
}
void FlyButton::SetVectorIcon(const gfx::VectorIcon& icon, int size) {
  icon_ = &icon;
  icon_size_ = size;
  UpdateColors();
}
void FlyButton::SetAccentText(bool accent) {
  accent_text_ = accent;
  UpdateColors();
}
void FlyButton::SetOutlined(bool outlined) {
  outlined_ = outlined;
  SchedulePaint();
}
void FlyButton::SetIconTone(IconTone tone) {
  icon_tone_ = tone;
  UpdateColors();
}
void FlyButton::SetPillHalf(PillHalf half) {
  pill_half_ = half;
  SchedulePaint();
}
void FlyButton::SetHoverFill(bool hover_fill) {
  hover_fill_ = hover_fill;
  SchedulePaint();
}
void FlyButton::SetRestingFill(SkColor color) {
  resting_fill_ = color;
  SchedulePaint();
}
void FlyButton::SetFlyFont(FlyFace face, int size, gfx::Font::Weight weight) {
  face_ = face;
  size_ = size;
  weight_ = weight;
  label()->SetFontList(FlyFont(face_, size_, weight_));
  PreferredSizeChanged();
}
void FlyButton::SetScaledSize(int size) {
  scaled_size_ = size;
  SetPreferredSize(gfx::Size(FlyScaled(size), FlyScaled(size)));
  SetMinSize(gfx::Size(FlyScaled(size), FlyScaled(size)));
}
void FlyButton::OnThemeChanged() {
  views::MdTextButton::OnThemeChanged();
  if (scaled_size_)
    SetScaledSize(scaled_size_);
  label()->SetFontList(FlyFont(face_, size_, weight_));
  if (GetText().empty() && GetCustomPadding())
    SetBorder(views::CreateEmptyBorder(*GetCustomPadding()));
  UpdateColors();
}
void FlyButton::StateChanged(ButtonState old_state) {
  views::MdTextButton::StateChanged(old_state);
  UpdateColors();
}
void FlyButton::SetText(std::u16string_view text) {
  views::MdTextButton::SetText(text);
  if (text.empty() && GetCustomPadding())
    SetBorder(views::CreateEmptyBorder(*GetCustomPadding()));
}
void FlyButton::OnFocus() {
  views::MdTextButton::OnFocus();
  SchedulePaint();
}
void FlyButton::OnBlur() {
  views::MdTextButton::OnBlur();
  SchedulePaint();
}
bool FlyButton::ShowsSelected() const {
  return selected_ || GetIsDefault() ||
         GetStyle() == ui::ButtonStyle::kTonal ||
         GetStyle() == ui::ButtonStyle::kProminent;
}
void FlyButton::UpdateBackgroundColor() {
  SetBackground(nullptr);
}
void FlyButton::UpdateColors() {
  const auto& colors = GetFlyColors();
  const SkColor text = accent_text_ ? colors.accent : colors.text;
  for (auto state : {STATE_NORMAL, STATE_HOVERED, STATE_PRESSED})
    SetTextColor(state, text);
  SetTextColor(STATE_DISABLED, colors.dim);
  if (icon_) {
    SkColor rest =
        ShowsSelected() ? text : (accent_text_ ? colors.accent : colors.dim);
    if (icon_tone_ == IconTone::kText && !ShowsSelected())
      rest = text;
    if (icon_tone_ == IconTone::kAccent)
      rest = colors.accent;
    views::LabelButton::SetImageModel(
        STATE_NORMAL,
        ui::ImageModel::FromVectorIcon(*icon_, rest, FlyScaled(icon_size_)));
    for (auto state : {STATE_HOVERED, STATE_PRESSED})
      views::LabelButton::SetImageModel(
          state,
          ui::ImageModel::FromVectorIcon(*icon_, text, FlyScaled(icon_size_)));
    views::LabelButton::SetImageModel(
        STATE_DISABLED,
        ui::ImageModel::FromVectorIcon(*icon_, colors.border,
                                       FlyScaled(icon_size_)));
  }
  SetBackground(nullptr);
  SchedulePaint();
}
void FlyButton::OnPaintBackground(gfx::Canvas* canvas) {
  const auto& colors = GetFlyColors();
  const bool hot = GetVisualState() == STATE_HOVERED ||
                   GetVisualState() == STATE_PRESSED;
  const bool selected = ShowsSelected();
  const auto shape = [&](gfx::RectF bounds) {
    auto radii = GetCornerRadii();
    const float round = bounds.height() / 2;
    if (pill_half_ == PillHalf::kLeft)
      radii = gfx::RoundedCornersF(round, 0, 0, round);
    else if (pill_half_ == PillHalf::kRight)
      radii = gfx::RoundedCornersF(0, round, round, 0);
    SkVector corners[4] = {{radii.upper_left(), radii.upper_left()},
                           {radii.upper_right(), radii.upper_right()},
                           {radii.lower_right(), radii.lower_right()},
                           {radii.lower_left(), radii.lower_left()}};
    SkRRect rrect;
    rrect.setRectRadii(gfx::RectFToSkRect(bounds), corners);
    return SkPath::RRect(rrect);
  };
  gfx::RectF bounds(GetLocalBounds());
  cc::PaintFlags flags;
  flags.setAntiAlias(true);
  if (selected || hot || resting_fill_) {
    flags.setColor(selected || !hot ? (selected ? colors.selected
                                                : *resting_fill_)
                   : hover_fill_ ? SkColorSetA(colors.text, 18)
                                 : colors.selected);
    canvas->DrawPath(shape(bounds), flags);
  }
  if (selected || outlined_ || HasFocus()) {
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(selected || HasFocus() || (outlined_ && hot)
                       ? colors.accent
                       : colors.border);
    bounds.Inset(0.5f);
    canvas->DrawPath(shape(bounds), flags);
  }
}
BEGIN_METADATA(FlyButton)
END_METADATA

std::unique_ptr<FlyButton> MakeFlyIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name,
    int size) {
  auto button = std::make_unique<FlyButton>(std::move(callback), u"");
  button->SetVectorIcon(icon, 14);
  button->SetCustomPadding(gfx::Insets());
  button->SetHorizontalAlignment(gfx::ALIGN_CENTER);
  button->SetScaledSize(size);
  button->SetTooltipText(accessible_name);
  button->GetViewAccessibility().SetName(accessible_name);
  return button;
}

FlyLabel::FlyLabel(std::u16string text,
                   FlyFace face,
                   int size,
                   gfx::Font::Weight weight,
                   Tone tone)
    : views::Label(std::move(text)),
      tone_(tone),
      face_(face),
      size_(size),
      weight_(weight) {
  SetFontList(FlyFont(face, size, weight));
  SetAutoColorReadabilityEnabled(false);
  SetBackgroundColor(SK_ColorTRANSPARENT);
  SetSubpixelRenderingEnabled(false);
}
FlyLabel::~FlyLabel() = default;
void FlyLabel::SetTone(Tone tone) {
  tone_ = tone;
  if (GetWidget())
    OnThemeChanged();
}
void FlyLabel::OnThemeChanged() {
  views::Label::OnThemeChanged();
  SetFontList(FlyFont(face_, size_, weight_));
  const auto& colors = GetFlyColors();
  SetEnabledColor(tone_ == Tone::kDim      ? colors.dim
                  : tone_ == Tone::kAccent ? colors.accent
                  : tone_ == Tone::kLink   ? colors.link
                                           : colors.text);
}
BEGIN_METADATA(FlyLabel)
END_METADATA
}  // namespace fly
