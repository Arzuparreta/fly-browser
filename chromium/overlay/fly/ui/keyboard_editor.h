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
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/values.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "chrome/browser/spellchecker/spellcheck_hunspell_dictionary.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/new_tab_tips.h"
#include "fly/preferences/preferences_service.h"
#include "fly/preferences/search_service.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/shell_dialogs/select_file_dialog.h"
#include "ui/views/controls/slider.h"
#include "ui/views/controls/textfield/textfield_controller.h"
#include "ui/views/widget/widget_observer.h"

namespace views {
class Button;
class Label;
class MenuRunner;
class ScrollView;
class Textfield;
class View;
class Widget;
}  // namespace views
class BrowserWindowInterface;
class Profile;
namespace fly {
class FlyButton;
class FlyExtensions;
class FlyLabel;
class FlySelect;
class FlySlider;
class FlyTextfield;
class FlyToggle;
class KeyHints;
class MenuScrim;
// Preferences: a panel with a section rail, one section built at a time and a
// search across every section. The Keyboard section edits a draft of the
// bindings in a table (action, shortcut, NORMAL key); the shared command
// service validates the whole draft on Save. The recorder takes keys before
// page and browser commands, without changing page visibility.
class KeyboardEditor : public views::TextfieldController,
                       public views::SliderListener,
                       public ui::SelectFileDialog::Listener,
                       public ui::SimpleMenuModel::Delegate,
                       public views::WidgetObserver,
                       public content::WebContentsObserver,
                       public SpellcheckHunspellDictionary::Observer {
 public:
  KeyboardEditor(views::Widget* desktop, CommandService& commands,
                 PrefService& local_state);
  ~KeyboardEditor() override;
  void Toggle(content::WebContents* contents);
  // Closes at once. A user's close asks first when shortcut changes are
  // unsaved (RequestClose).
  void Cancel(bool restore_focus = false);
  enum Pane {
    kGeneralPane = 0,
    kAppearancePane,
    kKeyboardPane,
    kPrivacyPane,
    kExtensionsPane,
    kPerformancePane,
    kAboutPane,
    kPaneCount,
  };
  void ShowPane(Pane pane);
  // Pointer settings buttons return to the page when the surface closes.
  void ShowAppearance(bool return_to_page = false);
  void ShowExtensions();
  // Extension management for the selected workspace (Extensions section).
  void SetExtensions(FlyExtensions* extensions,
                     base::RepeatingCallback<BrowserWindowInterface*()> browser,
                     base::RepeatingCallback<Profile*()> profile);
  void ShowPerformance();
  void ShowSearch();
  void ShowKeyboard();
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
  bool HandleOverlayKey(const ui::KeyEvent& event);
  bool active() const { return overlay_ && !closing_; }
  bool recording() const { return recording_.has_value(); }

  // Testing.
  views::View* root_for_testing();
  views::Textfield* settings_search_for_testing();
  int selected_pane_for_testing() const;
  bool setting_visible_for_testing(std::u16string_view label) const;
  views::Textfield* search_setting_for_testing(std::string_view key);
  views::Button* search_save_for_testing();
  views::Button* alias_save_for_testing();
  views::Button* alias_remove_for_testing(std::string_view alias);
  views::Textfield* search_for_testing();
  std::u16string bindings_for_testing(std::string_view action, bool normal);
  void SetBindingsForTesting(std::string_view action,
                             bool normal,
                             std::u16string text);
  // Shows the Keyboard section, selects and focuses the cell and returns the
  // Record button.
  views::Button* record_for_testing(std::string_view action, bool normal);
  views::Button* save_for_testing();
  views::Textfield* color_field_for_testing(std::string_view key);
  std::u16string error_for_testing() const;
  FlyToggle* appearance_for_testing(std::string_view key);
  FlySelect* clock_format_for_testing();
  FlySelect* suspension_timeout_for_testing();
  // Spelling: the shown languages in order, the buttons of one ("up",
  // "remove" or "retry"), the Add button and the choice its menu makes.
  std::vector<std::u16string> spelling_languages_for_testing() const;
  views::Button* spelling_button_for_testing(std::string_view action,
                                             std::string_view code);
  views::Button* spelling_add_for_testing();
  void AddSpellingLanguageForTesting(const std::string& code);

  void ContentsChanged(views::Textfield*, const std::u16string&) override;
  bool HandleKeyEvent(views::Textfield*, const ui::KeyEvent&) override;
  void SliderValueChanged(views::Slider* sender,
                          float value,
                          float old_value,
                          views::SliderChangeReason reason) override;
  void FileSelected(const ui::SelectedFileInfo& file, int index) override;
  void FileSelectionCanceled() override;
  // ui::SimpleMenuModel::Delegate (the Keyboard section's More menu):
  void ExecuteCommand(int command_id, int event_flags) override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetDestroying(views::Widget*) override;
  void PrimaryPageChanged(content::Page&) override;
  void WebContentsDestroyed() override;
  // SpellcheckHunspellDictionary::Observer (the shown download states):
  void OnHunspellDictionaryInitialized(const std::string& language) override;
  void OnHunspellDictionaryDownloadBegin(const std::string& language) override;
  void OnHunspellDictionaryDownloadSuccess(
      const std::string& language) override;
  void OnHunspellDictionaryDownloadFailure(
      const std::string& language) override;

  using Key = std::pair<std::string, bool>;

 private:
  class RailItem;
  class KeyCell;
  class KeyRow;
  friend class KeyCell;
  friend class KeyRow;
  // Centres the panel in the surface's bounds.
  void PlacePanel(const gfx::Rect& bounds);
  // Section content.
  void ShowContent();
  void ClearContent();
  void SelectPane(int pane, bool focus_rail);
  bool Matches(std::u16string_view text) const;
  void BeginGroup(std::u16string caption);
  views::View* AddRow(std::u16string label,
                      std::u16string description,
                      std::u16string keywords,
                      std::unique_ptr<views::View> control,
                      bool wide = false);
  views::View* AddNote(std::u16string text);
  std::unique_ptr<views::View> Controls(
      std::vector<std::unique_ptr<views::View>> views);
  std::unique_ptr<FlyToggle> MakeToggle(std::string key, std::u16string name);
  void BuildGeneral();
  void BuildAppearance();
  void BuildKeyboard();
  void BuildPrivacy();
  void BuildExtensions();
  void BuildPerformance();
  void BuildAbout();
  void BuildAliases();
  void BuildSpelling();
  // Values.
  void RefreshAppearance();
  void RefreshSearchSection();
  // Spelling. The list's buttons rebuild it, so they act after their event.
  Profile* SpellingProfile() const;
  void RefreshSpelling();
  void StopObservingDictionaries();
  void SaveSpelling(SpellcheckPreferences spelling, std::u16string done);
  void ShowSpellingMenu();
  void AddSpellingLanguage(const std::string& code);
  void ChangeSpellingLanguage(std::string action, std::string code);
  void RefreshExtensions();
  void SetStatus(std::u16string text);
  void RunAction(std::string action);
  void OpenUrl(std::string url);
  void ChooseEngine(size_t index);
  void ChooseEmailHandler(size_t index);
  void SaveSearchSetting(std::string action);
  void RemoveAlias(std::string alias);
  void ChangeAppearance(std::string key);
  void ApplyProductTexts();
  // Keyboard section.
  void RefreshKeyboardRows();
  void SelectCell(Key key, bool focus);
  void CellPressed(Key key, const ui::Event& event);
  void MoveCell(int rows, int columns);
  void UpdateCellDetails();
  void UpdateKeyboardState();
  bool KeyboardDirty() const;
  std::u16string DraftText(const Key& key) const;
  std::u16string DefaultBindings(const Key& key) const;
  bool IsCustomised(const Key& key) const;
  void DiscardKeyboard();
  void ResetSection(bool normal);
  void ResetAll();
  void ShowMoreMenu();
  void ChooseKeyboardFile(bool save);
  base::DictValue DraftChanges() const;
  void ImportKeyboardText(std::optional<std::string> text);
  void WriteField(Key key, std::u16string value);
  void ResetField(Key key);
  void StartRecording(Key key);
  void FinishRecording();
  void StopRecording();
  void Save();
  // Closing.
  void RequestClose();
  void FinishClose(bool restore_focus);

  raw_ptr<views::Widget> desktop_;
  const raw_ref<CommandService> commands_;
  PreferencesService preferences_;
  NewTabTips new_tab_tips_;
  SearchService search_preferences_;
  PrefChangeRegistrar preferences_registrar_;
  std::unique_ptr<views::Widget> overlay_;
  base::RepeatingCallback<void(std::string)> run_action_;

  std::vector<base::CallbackListSubscription> source_tab_;

  // Frame.
  raw_ptr<MenuScrim> root_ = nullptr;
  raw_ptr<FlyTextfield> settings_search_ = nullptr;
  std::vector<raw_ptr<RailItem>> rail_;
  raw_ptr<views::View> content_host_ = nullptr;
  raw_ptr<FlyLabel> status_ = nullptr;
  raw_ptr<KeyHints> hints_ = nullptr;
  int pane_ = kGeneralPane;
  std::u16string query_;

  // While a section is being built.
  int building_pane_ = 0;
  raw_ptr<views::View> building_parent_ = nullptr;
  raw_ptr<views::View> group_ = nullptr;
  std::u16string pending_group_;
  bool section_heading_shown_ = false;
  struct SearchRow {
    int pane;
    std::u16string text;
    raw_ptr<views::View> view;
  };
  std::vector<SearchRow> rows_;
  std::vector<int> matches_per_pane_;

  // Controls of the shown content; null when their section is not built.
  std::map<std::string, raw_ptr<views::Textfield>> search_settings_;
  raw_ptr<FlyButton> search_save_ = nullptr;
  raw_ptr<FlyButton> alias_save_ = nullptr;
  std::map<std::string, raw_ptr<FlyButton>> alias_remove_;
  raw_ptr<views::View> aliases_box_ = nullptr;
  std::map<std::string, raw_ptr<FlyToggle>> appearance_;
  raw_ptr<FlySelect> engine_choice_ = nullptr;
  raw_ptr<FlySelect> email_choice_ = nullptr;
  raw_ptr<views::View> email_custom_row_ = nullptr;
  raw_ptr<views::Textfield> email_handler_ = nullptr;
  raw_ptr<views::View> spelling_list_ = nullptr;
  raw_ptr<FlyButton> spelling_add_ = nullptr;
  std::map<std::string, raw_ptr<views::Button>> spelling_buttons_;
  std::vector<std::u16string> spelling_names_;
  // What the list shows, so that unrelated changes leave it alone.
  std::string spelling_shown_;
  std::unique_ptr<ui::SimpleMenuModel> spelling_model_;
  std::unique_ptr<views::MenuRunner> spelling_runner_;
  std::vector<std::string> spelling_menu_codes_;
  std::vector<base::WeakPtr<SpellcheckHunspellDictionary>>
      observed_dictionaries_;
  raw_ptr<FlySelect> scale_choice_ = nullptr;
  raw_ptr<FlySelect> clock_format_ = nullptr;
  raw_ptr<FlySelect> suspension_timeout_ = nullptr;
  raw_ptr<FlySelect> process_limit_ = nullptr;
  raw_ptr<FlyToggle> private_mode_ = nullptr;
  std::map<std::string, raw_ptr<views::Textfield>> color_fields_;
  std::map<std::string, raw_ptr<views::View>> color_swatches_;
  std::map<std::string, raw_ptr<FlySlider>> opacity_sliders_;
  std::map<std::string, raw_ptr<views::Label>> opacity_labels_;
  raw_ptr<views::Label> update_label_ = nullptr;
  raw_ptr<FlyButton> update_action_ = nullptr;
  raw_ptr<views::Label> desktop_entry_label_ = nullptr;
  raw_ptr<FlyButton> desktop_entry_button_ = nullptr;
  raw_ptr<views::Label> default_browser_label_ = nullptr;
  raw_ptr<FlyButton> default_browser_button_ = nullptr;
  // Extensions.
  raw_ptr<FlyExtensions> extensions_ = nullptr;
  base::RepeatingCallback<BrowserWindowInterface*()> extensions_browser_;
  base::RepeatingCallback<Profile*()> extensions_profile_;
  raw_ptr<views::View> extensions_list_ = nullptr;
  raw_ptr<FlyToggle> extensions_enabled_ = nullptr;
  raw_ptr<views::Label> extensions_usage_ = nullptr;
  raw_ptr<views::Textfield> extensions_store_link_ = nullptr;
  std::u16string extensions_usage_text_;

  // Keyboard section.
  raw_ptr<FlyTextfield> search_ = nullptr;
  raw_ptr<FlyTextfield> tabs_ = nullptr;
  raw_ptr<FlyToggle> modified_only_ = nullptr;
  raw_ptr<views::ScrollView> rows_scroll_ = nullptr;
  raw_ptr<views::View> key_rows_ = nullptr;
  std::map<Key, raw_ptr<KeyCell>> cells_;
  std::vector<std::string> row_actions_;
  raw_ptr<FlyLabel> cell_title_ = nullptr;
  raw_ptr<FlyButton> record_ = nullptr;
  raw_ptr<FlyButton> clear_ = nullptr;
  raw_ptr<FlyButton> default_ = nullptr;
  raw_ptr<FlyButton> save_ = nullptr;
  raw_ptr<FlyButton> discard_ = nullptr;
  raw_ptr<FlyButton> more_ = nullptr;
  std::unique_ptr<ui::SimpleMenuModel> more_model_;
  std::unique_ptr<views::MenuRunner> more_runner_;
  bool modified_only_checked_ = false;
  std::u16string keyboard_query_;
  std::optional<Key> selected_cell_;

  // State kept while Preferences is closed or between sections.
  std::u16string update_status_;
  std::string update_available_;
  std::u16string desktop_entry_status_;
  std::u16string desktop_entry_action_;
  std::u16string default_browser_status_;
  bool can_make_default_ = false;
  bool private_on_ = false;
  std::u16string status_text_;
  bool keyboard_readable_ = true;
  bool confirm_discard_ = false;

  scoped_refptr<ui::SelectFileDialog> file_dialog_;
  enum class FileChoice {
    kExportKeyboard,
    kImportKeyboard,
    kExtensionPackage,
    kExtensionFolder,
  };
  FileChoice file_choice_ = FileChoice::kImportKeyboard;
  void ChooseFile(FileChoice choice);

  // Keyboard draft.
  KeyboardConfig original_;
  std::map<Key, std::u16string> draft_;
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
