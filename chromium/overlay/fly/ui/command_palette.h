// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_COMMAND_PALETTE_H_
#define FLY_UI_COMMAND_PALETTE_H_

#include <memory>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "content/public/browser/web_contents_observer.h"
#include "fly/commands/palette_catalog.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Widget;
class MdTextButton;
}  // namespace views
namespace fly {
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
  void Toggle(content::WebContents* contents, const KeyboardConfig& config);
  // Only app-scoped choices may explicitly allow an initially empty source.
  // Destruction of an observed page always cancels its queued action.
  void ToggleItems(content::WebContents* contents, std::vector<PaletteEntry> items,
                   std::vector<std::string> close_bindings,
                   std::u16string placeholder, bool removable,
                   bool allow_empty_source = false);
  void UpdateItems(std::vector<PaletteEntry> items, bool removable);
  void Cancel(bool restore_focus = false);
  bool HandleOverlayKey(const ui::KeyEvent& event);
  bool active() const { return overlay_ && !closing_; }
  views::Textfield* search_for_testing() { return search_; }
  size_t result_count_for_testing() const { return filtered_.size(); }
  views::MdTextButton* remove_for_testing(size_t index) {
    return index < remove_buttons_.size() ? remove_buttons_[index].get() : nullptr;
  }
  void ContentsChanged(views::Textfield*, const std::u16string&) override;
  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent&) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetDestroying(views::Widget*) override;
  void PrimaryPageChanged(content::Page&) override;
  void WebContentsDestroyed() override;
  void OnVisibilityChanged(content::Visibility) override;

 private:
  void Refresh();
  void Select(int direction);
  void Activate(std::string action);
  void Remove(std::string action);
  void RemoveAfterEvent(std::string action);
  void FinishClose(bool restore_focus, std::string action);
  raw_ptr<views::Widget> desktop_;
  base::RepeatingCallback<bool(std::string_view)> available_;
  base::RepeatingCallback<void(content::WebContents*, std::string)> execute_;
  base::RepeatingCallback<void(content::WebContents*, std::string)> remove_;
  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::View> results_ = nullptr;
  std::vector<raw_ptr<views::MdTextButton>> buttons_;
  std::vector<raw_ptr<views::MdTextButton>> remove_buttons_;
  std::vector<std::string> palette_bindings_;
  std::vector<PaletteEntry> catalog_;
  std::vector<PaletteEntry> filtered_;
  int selected_ = -1;
  bool empty_source_ = false;
  bool closing_ = false;
  bool cancelled_ = false;
  bool restore_page_focus_ = false;
  bool removable_ = false;
  base::WeakPtrFactory<CommandPalette> weak_factory_{this};
};
}  // namespace fly
#endif
