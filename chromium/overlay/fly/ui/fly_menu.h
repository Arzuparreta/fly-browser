// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_MENU_H_
#define FLY_UI_FLY_MENU_H_

// The visual system shared by Fly's menus: the command palette and its
// pickers, Preferences and the Diagnostics hub. Every menu has the same
// panel, header, controls and key hints; its badge (icon and title) and its
// shape tell the menus apart. Every panel is as tall as its contents, up to
// the room its menu gives it; past that, its scroll region scrolls.

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "fly/ui/fly_theme.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/slider.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/view.h"

namespace content {
class WebContents;
}  // namespace content
namespace gfx {
class Canvas;
struct VectorIcon;
}  // namespace gfx
namespace views {
class MenuRunner;
class ScrollView;
class Widget;
}  // namespace views

namespace fly {

enum class MenuKind {
  kCommands,
  kAddress,
  kHistory,
  kBookmarks,
  kDownloads,
  kExtensions,
  kWorkspaces,
  kTabMove,
  kPreferences,
  kDiagnostics,
};
const gfx::VectorIcon& MenuIcon(MenuKind kind);

// Measurements before the interface scale.
inline constexpr int kMenuRadius = 8;
inline constexpr int kMenuControlHeight = 28;
inline constexpr int kMenuRowHeight = 32;
inline constexpr int kMenuTallRowHeight = 44;
inline constexpr int kMenuTextSize = 13;
inline constexpr int kMenuSmallTextSize = 12;
inline constexpr int kMenuCaptionSize = 11;

// The child widget every menu draws in: it covers `bounds` (in the desktop
// window's coordinates), never takes activation from the desktop and lets
// the page show through outside the panel.
std::unique_ptr<views::Widget> CreateMenuWidget(views::Widget* desktop,
                                                const gfx::Rect& bounds,
                                                const char* name);

// The menu widget's bounds: the desktop window's contents (in the window's
// coordinates), less `insets`.
gfx::Rect MenuWidgetBounds(views::Widget* desktop,
                           const gfx::Insets& insets = gfx::Insets());

// A menu belongs to the tab it was opened on and closes when that tab stops
// being its window's active tab or leaves the window. `close` runs then.
// Nothing that happens to the window itself (losing the keyboard to another
// application, being hidden on another desktop, minimised or covered) runs
// it: the menu, its text and its focus wait for the window to come back.
std::vector<base::CallbackListSubscription> WatchMenuSourceTab(
    content::WebContents* contents,
    base::RepeatingClosure close);

// Root of a menu widget: dims what is around the panel and closes the menu
// on a click outside it.
class MenuScrim : public views::View {
  METADATA_HEADER(MenuScrim, views::View)

 public:
  MenuScrim(SkColor scrim, base::RepeatingClosure dismiss);
  ~MenuScrim() override;
  views::View* SetPanel(std::unique_ptr<views::View> panel);
  // The room the panel has: it takes the width and top of `area` and the
  // height of its contents, up to the height of `area`. The panel stays
  // anchored at the top, so its header never moves when its contents change.
  void SetPanelArea(const gfx::Rect& area);
  views::View* panel() { return panel_; }
  void Layout(PassKey) override;
  bool OnMousePressed(const ui::MouseEvent& event) override;

 private:
  base::RepeatingClosure dismiss_;
  raw_ptr<views::View> panel_ = nullptr;
  gfx::Rect area_;
};

// The scroll region of a menu panel. It asks for the height of all its
// contents, so the panel can fit them. Give it flex in the panel's layout:
// when the panel reaches the height of its area, it is the part that shrinks
// and scrolls.
std::unique_ptr<views::ScrollView> MakeMenuScroll();

// The panel: one surface, one border, one radius for every menu.
class MenuPanel : public views::View {
  METADATA_HEADER(MenuPanel, views::View)

 public:
  MenuPanel();
  ~MenuPanel() override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnPaintBorder(gfx::Canvas* canvas) override;
};

// The menu's identity: its icon in an accent square.
class MenuBadge : public views::View {
  METADATA_HEADER(MenuBadge, views::View)

 public:
  explicit MenuBadge(MenuKind kind);
  ~MenuBadge() override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  const MenuKind kind_;
};

// A text field in the menu style. Focus turns its border to the accent.
class FlyTextfield : public views::Textfield {
  METADATA_HEADER(FlyTextfield, views::Textfield)

 public:
  explicit FlyTextfield(std::u16string accessible_name,
                        std::u16string placeholder = std::u16string(),
                        int size = kMenuTextSize);
  ~FlyTextfield() override;
  // The search field of a menu: taller, with the search icon.
  void SetSearchStyle(bool search);
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void OnThemeChanged() override;
  void OnFocus() override;
  void OnBlur() override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  void UpdateLook();
  int size_;
  bool search_ = false;
};

// Header row: badge, title, optional search field and trailing items.
class MenuHeader : public views::View {
  METADATA_HEADER(MenuHeader, views::View)

 public:
  MenuHeader(MenuKind kind, std::u16string title);
  ~MenuHeader() override;
  FlyTextfield* SetSearch(std::unique_ptr<FlyTextfield> search);
  // A dim count or status shown before the close button.
  FlyLabel* AddStatus();
  void SetFlexibleTitle();
  void AddClose(base::RepeatingClosure close);
  template <typename T>
  T* AddTrailing(std::unique_ptr<T> view) {
    return AddChildView(std::move(view));
  }

 private:
  void SetMinimumCrossAxisSize(int size);
  raw_ptr<FlyLabel> title_ = nullptr;
};

// Buttons come in three looks only.
enum class MenuButtonStyle {
  kPrimary,    // The one action that completes the menu's task.
  kSecondary,  // Ordinary actions.
  kQuiet,      // Actions inside lists.
};
std::unique_ptr<FlyButton> MakeMenuButton(views::Button::PressedCallback callback,
                                          std::u16string text,
                                          MenuButtonStyle style =
                                              MenuButtonStyle::kSecondary);
void StyleMenuButton(FlyButton* button, MenuButtonStyle style);

// A switch for on/off settings.
class FlyToggle : public views::Button {
  METADATA_HEADER(FlyToggle, views::Button)

 public:
  FlyToggle(PressedCallback callback, std::u16string accessible_name);
  ~FlyToggle() override;
  void SetChecked(bool checked);
  bool GetChecked() const { return checked_; }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  void NotifyClick(const ui::Event& event) override;
  void OnFocus() override;
  void OnBlur() override;
  void StateChanged(ButtonState old_state) override;

 private:
  bool checked_ = false;
};

// A choice among a few values. Enter or Space opens the list; Up and Down
// change the value directly.
class FlySelect : public FlyButton, public ui::SimpleMenuModel::Delegate {
  METADATA_HEADER(FlySelect, FlyButton)

 public:
  FlySelect(std::vector<std::u16string> items,
            base::RepeatingCallback<void(size_t)> changed,
            std::u16string accessible_name);
  ~FlySelect() override;
  // Shows a value without reporting it.
  void SetSelectedIndex(std::optional<size_t> index);
  std::optional<size_t> selected_index() const { return selected_; }
  // Chooses a value as the user would.
  void Choose(size_t index);
  size_t item_count() const { return items_.size(); }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void PaintButtonContents(gfx::Canvas* canvas) override;
  bool OnKeyPressed(const ui::KeyEvent& event) override;
  // ui::SimpleMenuModel::Delegate:
  bool IsCommandIdChecked(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void ShowList();
  std::vector<std::u16string> items_;
  base::RepeatingCallback<void(size_t)> changed_;
  std::optional<size_t> selected_;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<views::MenuRunner> runner_;
};

// A slider in the palette colours.
class FlySlider : public views::Slider {
  METADATA_HEADER(FlySlider, views::Slider)

 public:
  explicit FlySlider(views::SliderListener* listener);
  ~FlySlider() override;

 private:
  SkColor GetThumbColor() const override;
  SkColor GetTroughColor() const override;
};

// Key chips: the one way keys are written in every menu. Quiet chips are
// NORMAL-mode keys; accent chips are changed or recording.
enum class ChipTone { kNormal, kAccent, kQuiet };
int KeyChipWidth(std::u16string_view key);
// Paints a chip whose left edge is `x`, centred on `centre_y`. Returns its
// width.
int PaintKeyChip(gfx::Canvas* canvas,
                 int x,
                 int centre_y,
                 std::u16string_view key,
                 ChipTone tone = ChipTone::kNormal);
// Chips for several bindings, right-aligned to `right` (or left-aligned to
// `left`). Returns the edge opposite the alignment. Bindings that do not fit
// are summarised as "+n".
int PaintKeyChipsRightAligned(gfx::Canvas* canvas,
                              int left,
                              int right,
                              int centre_y,
                              const std::vector<std::u16string>& keys,
                              ChipTone tone = ChipTone::kNormal);
int PaintKeyChipsLeftAligned(gfx::Canvas* canvas,
                             int left,
                             int right,
                             int centre_y,
                             const std::vector<std::u16string>& keys,
                             ChipTone tone = ChipTone::kNormal);
inline constexpr int kKeyChipGap = 4;

// The footer of a menu: what the keyboard does here.
class KeyHints : public views::View {
  METADATA_HEADER(KeyHints, views::View)

 public:
  using Hint = std::pair<std::u16string, std::u16string>;
  explicit KeyHints(std::vector<Hint> hints);
  ~KeyHints() override;
  void SetHints(std::vector<Hint> hints);
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void OnPaint(gfx::Canvas* canvas) override;

 private:
  std::vector<Hint> hints_;
};

// Section labels inside menus ("SEARCH", "Recently used").
std::unique_ptr<FlyLabel> MakeMenuCaption(std::u16string text);

// Moves keyboard focus to the next or previous focusable view below `root`,
// wrapping around. Returns false when nothing below `root` can take focus.
bool CycleMenuFocus(views::View* root, bool reverse);
// The focused view when it is below `root`.
views::View* FocusedMenuView(views::View* root);

}  // namespace fly

#endif  // FLY_UI_FLY_MENU_H_
