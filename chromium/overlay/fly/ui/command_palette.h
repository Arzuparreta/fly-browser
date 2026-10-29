// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_COMMAND_PALETTE_H_
#define FLY_UI_COMMAND_PALETTE_H_

#include <memory>
#include <vector>
#include "base/callback_list.h"
#include "base/functional/callback.h"
#include "ui/gfx/geometry/insets.h"
#include "base/memory/raw_ptr.h"
#include "ui/gfx/geometry/point.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "fly/commands/palette_catalog.h"
#include "fly/ui/fly_menu.h"
#include "fly/ui/fly_theme.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Label;
class Widget;
class MdTextButton;
class ScrollView;
}  // namespace views
namespace fly {
// Presentation of a picker. Every picker shares the menu look (fly_menu.h);
// its kind gives it its badge and its layout its shape.
struct PaletteStyle {
  enum class Layout {
    kCentre,  // Compact card over a dimmed page (pickers, Command Centre).
    kTop,     // Floating card under the window top, page left visible
              // (address bar, history).
    kPage,    // Tall manager over the content area (bookmarks, workspace
              // dashboard).
  };
  Layout layout = Layout::kCentre;
  MenuKind kind = MenuKind::kCommands;
  std::u16string title;
  int width = 640;
  // kPage only: area of the desktop contents left uncovered (sidebar rail,
  // footer), queried when the surface opens.
  base::RepeatingCallback<gfx::Insets()> page_insets;
  // Command Centre: category chips, grouped list, match count, a detail line
  // for the selected command, recently used commands and the terminal
  // fallback.
  bool command_centre = false;
  // What Enter does, for the key hints ("Open", "Run", "Choose").
  std::u16string enter_label = u"Open";
  // Shown when there is nothing to list and no search.
  std::u16string empty_text = u"Nothing here yet.";
};
// A native modal child surface of the existing desktop Aura host. Commands
// execute after the surface is destroyed; no callback retains a row or tab
// index.
class CommandPalette : public views::TextfieldController,
                       public views::WidgetObserver,
                       public content::WebContentsObserver {
 public:
  CommandPalette(views::Widget* desktop,
                 base::RepeatingCallback<bool(std::string_view)> available,
                 base::RepeatingCallback<void(content::WebContents*,
                                              std::string)> execute,
                 base::RepeatingCallback<void(content::WebContents*,
                                              std::string)> remove = {});
  ~CommandPalette() override;
  void SetStyle(PaletteStyle style) { style_ = std::move(style); }
  // Runs with the action of each newly highlighted row.
  void SetSelectionCallback(
      base::RepeatingCallback<void(std::string_view)> callback) {
    selection_callback_ = std::move(callback);
  }
  void Toggle(content::WebContents* contents, const KeyboardConfig& config);
  // Only app-scoped choices may explicitly allow an initially empty source.
  // Destruction of an observed page always cancels its queued action.
  void ToggleItems(content::WebContents* contents, std::vector<PaletteEntry> items,
                   std::vector<std::string> close_bindings,
                   std::u16string placeholder, bool removable,
                   bool allow_empty_source = false);
  void UpdateItems(std::vector<PaletteEntry> items, bool removable);
  // Local native query provider, run synchronously on edit. It must not retain
  // row pointers or perform page script/network work.
  using QueryProvider =
      base::RepeatingCallback<std::vector<PaletteEntry>(std::u16string_view)>;
  void ToggleQuery(content::WebContents* contents,
                   QueryProvider provider,
                   std::vector<std::string> close_bindings,
                   std::u16string placeholder,
                   std::u16string initial);
  void RefreshQuery();
  // Command Centre: the last commands run from it, newest first.
  void SetRecent(std::vector<std::string> ids) { recent_ = std::move(ids); }
  // Workspace Dashboard: rows whose action `draggable` accepts can be dragged
  // onto another row; `drop` gets the source and target actions.
  void SetDragDrop(base::RepeatingCallback<bool(std::string_view)> draggable,
                   base::RepeatingCallback<void(std::string, std::string)> drop);
  void SetCategoryForTesting(std::string category);
  std::u16string inspector_title_for_testing() const;
  std::u16string count_for_testing() const;
  std::string selected_action_for_testing() const {
    return selected_ >= 0 ? filtered_[selected_].action : std::string();
  }
  void Cancel(bool restore_focus = false);
  bool HandleOverlayKey(const ui::KeyEvent& event);
  bool active() const { return overlay_ && !closing_; }
  // Still on screen or finishing its close.
  bool showing() const { return !!overlay_; }
  views::Textfield* search_for_testing() { return search_; }
  views::MdTextButton* row_for_testing(size_t index) {
    EnsureRows(index + 1);
    return index < buttons_.size() ? buttons_[index].get() : nullptr;
  }
  size_t built_rows_for_testing() const { return buttons_.size(); }
  size_t result_count_for_testing() const { return filtered_.size(); }
  views::MdTextButton* remove_for_testing(size_t index) {
    EnsureRows(index + 1);
    return index < remove_buttons_.size() ? remove_buttons_[index].get() : nullptr;
  }
  void ContentsChanged(views::Textfield*, const std::u16string&) override;
  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent&) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetDestroying(views::Widget*) override;
  void PrimaryPageChanged(content::Page&) override;
  void WebContentsDestroyed() override;

 private:
  // The surface's bounds in the desktop window and the panel inside them.
  gfx::Rect SurfaceBounds() const;
  void PlacePanel(const gfx::Rect& bounds);
  void Refresh();
  // Builds the rows of filtered_ up to `count`.
  void EnsureRows(size_t count);
  void AppendRow(size_t index);
  void OnResultsScrolled();
  void ScrollSelectedIntoView();
  void Select(int direction);
  void SetRowSelected(int index, bool selected);
  void Activate(std::string action);
  void Remove(std::string action);
  void RemoveAfterEvent(std::string action);
  void FinishClose(bool restore_focus, std::string action);
  void BuildCommandCentre(views::View* panel);
  void UpdateHints();
  void UpdateInspector();
  void SetCategory(std::string category);
  void Hover(size_t index);
  PaletteStyle style_;
  raw_ptr<views::Widget> desktop_;
  base::RepeatingCallback<bool(std::string_view)> available_;
  base::RepeatingCallback<void(content::WebContents*, std::string)> execute_;
  base::RepeatingCallback<void(content::WebContents*, std::string)> remove_;
  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<MenuScrim> scrim_ = nullptr;
  std::vector<base::CallbackListSubscription> source_tab_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::ScrollView> results_scroll_ = nullptr;
  raw_ptr<views::View> results_ = nullptr;
  std::vector<raw_ptr<FlyButton>> buttons_;
  // The action of each row in `buttons_`.
  std::vector<std::string> button_actions_;
  base::RepeatingCallback<bool(std::string_view)> draggable_;
  base::RepeatingCallback<void(std::string, std::string)> drop_;
  int drop_target_ = -1;
  int RowAt(const gfx::Point& screen) const;
  void DragOver(gfx::Point screen);
  void Drop(std::string source, gfx::Point screen);
  std::vector<raw_ptr<FlyButton>> remove_buttons_;
  std::vector<std::string> palette_bindings_;
  std::vector<PaletteEntry> catalog_;
  std::vector<PaletteEntry> filtered_;
  QueryProvider query_provider_;
  int selected_ = -1;
  base::RepeatingCallback<void(std::string_view)> selection_callback_;
  gfx::Point pointer_at_open_;
  bool pointer_moved_ = false;
  bool empty_source_ = false;
  bool keep_initial_new_tab_ = false;
  bool closing_ = false;
  bool cancelled_ = false;
  bool restore_page_focus_ = false;
  bool removable_ = false;
  // Command Centre.
  raw_ptr<views::Label> count_ = nullptr;
  raw_ptr<KeyHints> hints_ = nullptr;
  // Command Centre's detail line for the selected command.
  raw_ptr<views::View> inspector_ = nullptr;
  std::vector<raw_ptr<FlyButton>> chips_;
  std::string category_ = "All";
  std::vector<std::string> recent_;
  std::vector<std::string> groups_;
  // The Command Centre caption of the last built row.
  std::string last_group_;
  size_t initial_rows_ = 24;
  base::CallbackListSubscription scroll_subscription_;
  base::WeakPtrFactory<CommandPalette> weak_factory_{this};
};
}  // namespace fly
#endif
