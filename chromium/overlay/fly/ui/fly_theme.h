// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_THEME_H_
#define FLY_UI_FLY_THEME_H_

#include <memory>
#include <optional>
#include <string>

#include "base/functional/callback.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/font.h"
#include "ui/gfx/font_list.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"

namespace gfx {
struct VectorIcon;
}
namespace views {
class Background;
class Border;
class Label;
}  // namespace views

namespace fly {
// The product palette shared with the Electron interface (style.css and
// main.js DEFAULT_THEME_COLORS). Derived colours follow the same rules.
struct FlyColors {
  bool dark = true;
  SkColor accent;
  SkColor bg_main;
  SkColor bg_sidebar;
  SkColor text;
  SkColor border;
  SkColor selected;
  SkColor panel;
  SkColor cell;
  SkColor dim;
  SkColor link;
  SkColor warning;
  // Electron's sidebar_opacity and overlay_opacity (40-100).
  int sidebar_opacity = 100;
  int overlay_opacity = 100;
};
FlyColors DefaultFlyColors(bool dark);
const FlyColors& GetFlyColors();
// Returns true when the palette changed.
bool SetFlyColors(const FlyColors& colors);

enum class FlyFace { kSans, kMono, kDisplay };
gfx::FontList FlyFont(FlyFace face,
                      int size,
                      gfx::Font::Weight weight = gfx::Font::Weight::NORMAL);
// Electron's "large" interface scale (1.25) zooms Fly's whole interface:
// fonts, and every size given through FlyScaled().
void SetFlyInterfaceScale(float scale);
float GetFlyInterfaceScale();
int FlyScaled(int pixels);
// Makes the bundled product fonts (out/<dir>/fly_fonts) available to native UI.
void RegisterFlyFonts();

enum class FlyRole {
  kMain,
  kSidebar,
  kPanel,
  kCell,
  kSelected,
  kBorder,
  kAccent,
  // The sidebar over the canvas at the sidebar opacity
  // (color-mix(bg_sidebar, transparent) over bg_main in Electron).
  kSidebarSurface,
  // Overlay surfaces at the overlay opacity: the page shows through.
  kOverlayMain,
  kOverlaySidebar,
};
SkColor FlyRoleColor(FlyRole role);
// Overlay backgrounds may be translucent; labels drawn on them must not use
// subpixel text. Applies to every label below `root`.
void DisableSubpixelText(views::View* root);
// Paints the current palette colour at paint time, so palette changes only
// require a repaint.
std::unique_ptr<views::Background> FlyBackground(FlyRole role,
                                                 float radius = 0);
std::unique_ptr<views::Border> FlyBorder(FlyRole role,
                                         int thickness = 1,
                                         float radius = 0,
                                         const gfx::Insets& padding = {});
// Paints only the given edges (thicknesses) in the palette colour.
std::unique_ptr<views::Border> FlyEdgeBorder(FlyRole role,
                                             const gfx::Insets& edges,
                                             const gfx::Insets& padding = {});
// A flat, keyboard-focusable button in the product style. It keeps the
// MdTextButton API used throughout the native shell: SetStyle(kTonal) and
// SetIsDefault(true) both draw the selected row look (selected fill and accent
// outline), and keyboard focus draws the product's 1px accent outline.
class FlyButton : public views::MdTextButton {
  METADATA_HEADER(FlyButton, views::MdTextButton)

 public:
  FlyButton(PressedCallback callback,
            std::u16string_view text,
            FlyFace face = FlyFace::kSans,
            int size = 12);
  ~FlyButton() override;
  void SetSelected(bool selected);
  bool selected() const { return selected_; }
  void SetVectorIcon(const gfx::VectorIcon& icon, int size = 14);
  // A fixed square size that follows the interface scale.
  void SetScaledSize(int size);
  void SetAccentText(bool accent);
  // A bordered button (Electron's .nav-icon-button): the border turns to the
  // accent on hover.
  void SetOutlined(bool outlined);
  // One half of Electron's history pill: fully rounded on that side.
  enum class PillHalf { kNone, kLeft, kRight };
  void SetPillHalf(PillHalf half);
  // Hover tints with the text colour at 7% and no outline (#WorkspaceHeader).
  void SetHoverFill(bool hover_fill);
  // A fill shown at rest (.sidebar-clock).
  void SetRestingFill(SkColor color);
  const gfx::VectorIcon* vector_icon() const { return icon_; }
  // The icon's colour at rest: dim by default, the text colour (Electron's
  // .nav-icon-button) or the accent (.sidebar-clock i).
  enum class IconTone { kDim, kText, kAccent };
  void SetIconTone(IconTone tone);
  void SetFlyFont(FlyFace face, int size,
                  gfx::Font::Weight weight = gfx::Font::Weight::NORMAL);
  void OnThemeChanged() override;
  void StateChanged(ButtonState old_state) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  // MdTextButton drops its padding for empty text; Fly keeps the custom one.
  void SetText(std::u16string_view text) override;
  void OnFocus() override;
  void OnBlur() override;

 protected:
  void UpdateColors() override;
  void UpdateBackgroundColor() override;

 private:
  bool ShowsSelected() const;
  raw_ptr<const gfx::VectorIcon> icon_ = nullptr;
  int icon_size_ = 14;
  int scaled_size_ = 0;
  FlyFace face_;
  int size_;
  gfx::Font::Weight weight_ = gfx::Font::Weight::NORMAL;
  bool selected_ = false;
  bool accent_text_ = false;
  bool outlined_ = false;
  bool hover_fill_ = false;
  PillHalf pill_half_ = PillHalf::kNone;
  IconTone icon_tone_ = IconTone::kDim;
  std::optional<SkColor> resting_fill_;
};
// Square icon button (sidebar navigation and footer actions).
std::unique_ptr<FlyButton> MakeFlyIconButton(
    views::Button::PressedCallback callback,
    const gfx::VectorIcon& icon,
    std::u16string accessible_name,
    int size = 28);
// A label that follows the palette (text, or dim/accent/link variants).
class FlyLabel : public views::Label {
  METADATA_HEADER(FlyLabel, views::Label)

 public:
  enum class Tone { kText, kDim, kAccent, kLink };
  FlyLabel(std::u16string text,
           FlyFace face,
           int size,
           gfx::Font::Weight weight = gfx::Font::Weight::NORMAL,
           Tone tone = Tone::kText);
  ~FlyLabel() override;
  void SetTone(Tone tone);
  void OnThemeChanged() override;

 private:
  Tone tone_;
  FlyFace face_;
  int size_;
  gfx::Font::Weight weight_;
};
}  // namespace fly

#endif  // FLY_UI_FLY_THEME_H_
