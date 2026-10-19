// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_KEYBOARD_EDITOR_H_
#define FLY_UI_KEYBOARD_EDITOR_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/preferences_service.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane_listener.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Widget;
class View;
class Label;
class MdTextButton;
class Checkbox;
class TabbedPane;
}  // namespace views
namespace fly {
// Browser-owned editor. Drafts and recording stay in memory until the shared
// command service validates one complete transaction. The modal surface routes
// recorder keys before page/browser commands, without changing page visibility.
class KeyboardEditor : public views::TextfieldController,
                       public views::TabbedPaneListener,
                       public views::WidgetObserver,
                       public content::WebContentsObserver {
 public:
  KeyboardEditor(views::Widget* desktop, CommandService& commands,
                 PrefService& local_state);
  ~KeyboardEditor() override;
  void Toggle(content::WebContents* contents);
  void Cancel(bool restore_focus = false);
  void ShowAppearance();
  void ShowPerformance();
  void ShowKeyboard();
  bool HandleOverlayKey(const ui::KeyEvent& event);
  bool active() const { return overlay_ && !closing_; }
  bool recording() const { return recording_.has_value(); }
  views::Textfield* field_for_testing(std::string_view action, bool normal);
  views::MdTextButton* record_for_testing(std::string_view action, bool normal);
  views::MdTextButton* save_for_testing() { return save_; }
  views::Textfield* search_for_testing() { return search_; }
  std::u16string error_for_testing() const;
  views::Checkbox* appearance_for_testing(std::string_view key);
  views::MdTextButton* clock_format_for_testing() { return clock_format_; }
  views::MdTextButton* suspension_timeout_for_testing() {
    return suspension_timeout_;
  }

  void ContentsChanged(views::Textfield*, const std::u16string&) override;
  void TabSelectedAt(int index) override;
  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent&) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetDestroying(views::Widget*) override;
  void PrimaryPageChanged(content::Page&) override;
  void WebContentsDestroyed() override;
  void OnVisibilityChanged(content::Visibility) override;

 private:
  using Key = std::pair<std::string, bool>;
  struct Field {
    raw_ptr<views::Textfield> input;
    raw_ptr<views::MdTextButton> record;
  };
  void Refresh();
  void RefreshAppearance();
  void ChangeAppearance(std::string key);
  void CycleClockFormat();
  void CycleSuspendTimeout();
  void ClearAppearance();
  void ResetAll();
  void WriteField(Key key, std::u16string value);
  void ResetField(Key key);
  void StartRecording(Key key);
  void FinishRecording();
  void StopRecording();
  void Save();
  void FinishClose(bool restore_focus);
  raw_ptr<views::Widget> desktop_;
  const raw_ref<CommandService> commands_;
  PreferencesService preferences_;
  PrefChangeRegistrar preferences_registrar_;
  std::map<std::string, raw_ptr<views::Checkbox>> appearance_;
  raw_ptr<views::MdTextButton> clock_format_ = nullptr;
  raw_ptr<views::MdTextButton> suspension_timeout_ = nullptr;
  raw_ptr<views::Label> suspension_status_ = nullptr;
  raw_ptr<views::Label> appearance_status_ = nullptr;
  raw_ptr<views::TabbedPane> sections_ = nullptr;
  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<views::Textfield> search_ = nullptr;
  raw_ptr<views::Textfield> tabs_ = nullptr;
  raw_ptr<views::View> rows_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<views::MdTextButton> save_ = nullptr;
  KeyboardConfig original_;
  std::map<Key, std::u16string> draft_;
  std::map<Key, Field> fields_;
  std::u16string tab_modifiers_;
  std::vector<PaletteEntry> catalog_;
  std::vector<std::string> close_bindings_;
  std::optional<Key> recording_;
  std::string chord_;
  base::OneShotTimer recording_timer_;
  uint64_t generation_ = 0;
  bool closing_ = false;
  bool cancelled_ = false;
  bool restore_page_focus_ = false;
  base::WeakPtrFactory<KeyboardEditor> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_KEYBOARD_EDITOR_H_
