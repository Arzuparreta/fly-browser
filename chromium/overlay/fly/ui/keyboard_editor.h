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
#include "base/values.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/preferences_service.h"
#include "fly/preferences/search_service.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/controls/slider.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane_listener.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Combobox;
class Slider;
class ScrollView;
class Widget;
class View;
class Label;
class MdTextButton;
class Checkbox;
class TabbedPane;
}  // namespace views
class BrowserWindowInterface;
namespace fly {
class FlyExtensions;
// Browser-owned editor. Drafts and recording stay in memory until the shared
// command service validates one complete transaction. The modal surface routes
// recorder keys before page/browser commands, without changing page visibility.
class KeyboardEditor : public views::TextfieldController,
                       public views::TabbedPaneListener,
                       public views::SliderListener,
                       public ui::SelectFileDialog::Listener,
                       public views::WidgetObserver,
                       public content::WebContentsObserver {
 public:
  KeyboardEditor(views::Widget* desktop, CommandService& commands,
                 PrefService& local_state);
  ~KeyboardEditor() override;
  void Toggle(content::WebContents* contents);
  void Cancel(bool restore_focus = false);
  // Pointer settings buttons return to the page when the surface closes.
  void ShowAppearance(bool return_to_page = false);
  void ShowExtensions();
  // Extension management for the selected workspace (Extensions tab).
  void SetExtensions(FlyExtensions* extensions,
                     base::RepeatingCallback<BrowserWindowInterface*()> browser);
  void ShowPerformance();
  void ShowSearch();
  // Electron's section order.
  enum Pane {
    kGeneralPane = 0,
    kAppearancePane,
    kKeyboardPane,
    kPrivacyPane,
    kExtensionsPane,
    kPerformancePane,
    kDiagnosticsPane,
    kAboutPane,
  };
  // Buttons that act outside Preferences (restart, private mode, updates,
  // desktop integration, extension installs) run through this.
  void SetActionRunner(base::RepeatingCallback<void(std::string)> run) {
    run_action_ = std::move(run);
  }
  void SetUpdateStatus(std::u16string status, std::string available_version);
  void SetDesktopIntegration(std::u16string entry_status,
                             std::u16string entry_action,
                             std::u16string default_status,
                             bool can_make_default);
  void SetPrivateMode(bool on);
  void SetExtensionUsage(std::u16string text);
  views::Textfield* settings_search_for_testing() { return settings_search_; }
  views::TabbedPane* sections_for_testing() { return sections_; }
  int selected_pane_for_testing() const;
  bool setting_visible_for_testing(std::u16string_view label) const;
  views::Textfield* search_setting_for_testing(std::string_view key);
  views::MdTextButton* search_save_for_testing() { return search_save_; }
  views::MdTextButton* alias_save_for_testing() { return alias_save_; }
  views::MdTextButton* alias_remove_for_testing() { return alias_remove_; }
  void ShowKeyboard();
  bool HandleOverlayKey(const ui::KeyEvent& event);
  bool active() const { return overlay_ && !closing_; }
  bool recording() const { return recording_.has_value(); }
  views::Textfield* field_for_testing(std::string_view action, bool normal);
  void EnsureRowForTesting(std::string_view action, bool normal);
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
  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override;
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
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
  views::View* AddPane(std::u16string title);
  views::View* AddGroup(views::View* pane, std::u16string title);
  views::View* AddItem(views::View* group,
                       std::u16string label,
                       std::u16string description,
                       std::unique_ptr<views::View> control,
                       bool control_below = false);
  void ApplySettingsSearch();
  void BuildGeneralPane();
  void BuildAppearancePane();
  void BuildKeyboardPane(bool readable);
  void BuildPrivacyPane();
  void BuildPerformancePane();
  void BuildDiagnosticsPane();
  void BuildAboutPane();
  void RunAction(std::string action);
  void ChooseEmailHandler();
  void DiscardKeyboard();
  void ResetSection(bool normal);
  bool IsCustomised(const std::string& action) const;
  void ChooseKeyboardFile(bool save);
  base::DictValue DraftChanges() const;
  void ImportKeyboardText(std::optional<std::string> text);
  void RefreshSearchSection();
  void SaveSearchSetting(std::string action);
  void ChangeAppearance(std::string key);
  void ApplyProductTexts();
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
  SearchService search_preferences_;
  std::map<std::string, raw_ptr<views::Textfield>> search_settings_;
  raw_ptr<views::MdTextButton> search_save_ = nullptr;
  raw_ptr<views::MdTextButton> alias_save_ = nullptr;
  raw_ptr<views::MdTextButton> alias_remove_ = nullptr;
  raw_ptr<views::Label> search_settings_status_ = nullptr;
  raw_ptr<views::Label> alias_list_ = nullptr;
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
  raw_ptr<views::ScrollView> rows_scroll_ = nullptr;
  bool show_all_rows_ = false;
  void BuildExtensionsSection();
  void RefreshExtensions();
  raw_ptr<FlyExtensions> extensions_ = nullptr;
  base::RepeatingCallback<BrowserWindowInterface*()> extensions_browser_;
  raw_ptr<views::View> extensions_list_ = nullptr;
  raw_ptr<views::Checkbox> extensions_enabled_ = nullptr;
  raw_ptr<views::Textfield> email_handler_ = nullptr;
  raw_ptr<views::Combobox> engine_choice_ = nullptr;
  raw_ptr<views::Combobox> email_choice_ = nullptr;
  raw_ptr<views::Combobox> spellcheck_choice_ = nullptr;
  raw_ptr<views::Combobox> scale_choice_ = nullptr;
  raw_ptr<views::Checkbox> modified_only_ = nullptr;
  raw_ptr<views::Checkbox> private_mode_ = nullptr;
  raw_ptr<views::Textfield> settings_search_ = nullptr;
  raw_ptr<views::Label> settings_search_status_ = nullptr;
  raw_ptr<views::Label> update_label_ = nullptr;
  raw_ptr<views::MdTextButton> update_action_ = nullptr;
  raw_ptr<views::Label> desktop_entry_label_ = nullptr;
  raw_ptr<views::MdTextButton> desktop_entry_button_ = nullptr;
  raw_ptr<views::Label> default_browser_label_ = nullptr;
  raw_ptr<views::MdTextButton> default_browser_button_ = nullptr;
  raw_ptr<views::Label> extensions_usage_ = nullptr;
  raw_ptr<views::Textfield> extensions_store_link_ = nullptr;
  std::map<std::string, raw_ptr<views::View>> color_swatches_;
  std::map<std::string, raw_ptr<views::Slider>> opacity_sliders_;
  std::map<std::string, raw_ptr<views::Label>> opacity_labels_;
  // Rows the settings search filters.
  struct SearchItem {
    int pane;
    raw_ptr<views::View> view;
    raw_ptr<views::View> group;
    std::u16string text;
  };
  std::vector<SearchItem> items_;
  std::vector<std::u16string> pane_titles_;
  int last_pane_ = kGeneralPane;
  base::RepeatingCallback<void(std::string)> run_action_;
  // State shown in About and Privacy; kept while Preferences is closed.
  std::u16string update_status_;
  std::string update_available_;
  std::u16string desktop_entry_status_;
  std::u16string desktop_entry_action_;
  std::u16string default_browser_status_;
  bool can_make_default_ = false;
  bool private_on_ = false;
  scoped_refptr<ui::SelectFileDialog> file_dialog_;
  enum class FileChoice {
    kExportKeyboard,
    kImportKeyboard,
    kExtensionPackage,
    kExtensionFolder,
  };
  FileChoice file_choice_ = FileChoice::kImportKeyboard;
  void ChooseFile(FileChoice choice);
  void OpenUrl(std::string url);
  raw_ptr<views::MdTextButton> process_limit_ = nullptr;
  std::map<std::string, raw_ptr<views::Textfield>> color_fields_;
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
