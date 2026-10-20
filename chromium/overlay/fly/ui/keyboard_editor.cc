// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_extensions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"

#include <limits>

#include <algorithm>
#include <set>
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/native_commands.h"
#include "ui/aura/window.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
KeyboardEditor::KeyboardEditor(views::Widget* desktop,
                               CommandService& commands,
                               PrefService& local_state)
    : desktop_(desktop),
      commands_(commands),
      preferences_(local_state),
      search_preferences_(local_state) {
  desktop_->AddObserver(this);
  preferences_registrar_.Init(&local_state);
  preferences_registrar_.Add(
      kPreferencesPref, base::BindRepeating(&KeyboardEditor::RefreshAppearance,
                                           weak_factory_.GetWeakPtr()));
}
KeyboardEditor::~KeyboardEditor() {
  weak_factory_.InvalidateWeakPtrs();
  preferences_registrar_.RemoveAll();
  recording_timer_.Stop();
  Observe(nullptr);
  fields_.clear();
  search_ = nullptr;
  tabs_ = nullptr;
  rows_ = nullptr;
  rows_scroll_ = nullptr;
  status_ = nullptr;
  save_ = nullptr;
  ClearAppearance();
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void KeyboardEditor::Toggle(content::WebContents* contents) {
  if (overlay_) {
    Cancel(true);
    return;
  }
  if (!desktop_ || !contents)
    return;
  restore_page_focus_ = ShouldRestoreNativePageFocus(contents);
  Observe(contents);
  contents->StoreFocus();
  const auto current = commands_->Read();
  original_ = current.value_or(DefaultKeyboardConfig());
  generation_ = commands_->generation();
  catalog_ = PaletteCatalog(original_);
  std::set<std::string> actions;
  std::erase_if(catalog_, [&](const PaletteEntry& entry) {
    return !actions.insert(entry.action).second;
  });
  close_bindings_ = original_.shortcuts.at("toggle-help");
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  closing_ = cancelled_ = false;
  show_all_rows_ = false;
  overlay_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = desktop_->GetNativeWindow();
  params.child = true;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.native_widget = new views::NativeWidgetAura(overlay_.get());
  params.name = "FlyKeyboardPreferences";
  params.bounds = desktop_->GetContentsView()->GetBoundsInScreen();
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &params.bounds);
  const auto bounds = params.bounds;
  overlay_->Init(std::move(params));
  auto root = std::make_unique<views::View>();
  root->SetBackground(
      views::CreateSolidBackground(SkColorSetARGB(96, 0, 0, 0)));
  const int margin = std::max(8, (bounds.width() - 900) / 2);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, margin, 16, margin)));
  auto panel = std::make_unique<views::View>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
  panel->SetPreferredSize(gfx::Size(900, std::max(160, bounds.height() - 32)));
  panel->SetBackground(FlyBackground(FlyRole::kSidebar, 4));
  panel->SetBorder(FlyBorder(FlyRole::kBorder, 1, 4));
  auto* heading = panel->AddChildView(std::make_unique<FlyLabel>(
      u"Preferences", FlyFace::kSans, 15, gfx::Font::Weight::SEMIBOLD));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  sections_ = panel->AddChildView(std::make_unique<views::TabbedPane>());
  sections_->SetListener(this);
  layout->SetFlexForView(sections_, 1);
  auto* appearance_scroll = sections_->AddTab(
      u"Appearance", std::make_unique<views::ScrollView>());
  appearance_scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto* appearance = appearance_scroll->SetContents(std::make_unique<views::View>());
  appearance->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical));
  for (const auto& [key, label] :
       {std::pair{"theme", u"Dark interface"},
        std::pair{"sync_website_theme", u"Match website color scheme to Fly"},
        std::pair{"show_clock", u"Show clock"},
        std::pair{"show_frequent_sites", u"Show frequent sites with bookmarks"},
        std::pair{"show_status_bar", u"Show status bar"},
        std::pair{"show_window_controls", u"Show window controls"},
        std::pair{"sidebar_auto_collapse",
                  u"Automatically collapse sidebar"}}) {
    appearance_[key] = appearance->AddChildView(std::make_unique<views::Checkbox>(
        label, base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                                   weak_factory_.GetWeakPtr(), std::string(key))));
    appearance_[key]->SetMultiLine(true);
  }
  for (const auto& [key, label] :
       {std::pair{"interface_scale", u"Large interface (125%)"},
        std::pair{"web_notifications", u"Allow websites to ask to show notifications"}}) {
    appearance_[key] = appearance->AddChildView(std::make_unique<views::Checkbox>(
        label, base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                                   weak_factory_.GetWeakPtr(), std::string(key))));
  }
  appearance->AddChildView(std::make_unique<views::Label>(
      u"Spellchecker language (for example en-GB; \"disabled\" turns it off)"));
  spellcheck_ = appearance->AddChildView(std::make_unique<views::Textfield>());
  spellcheck_->GetViewAccessibility().SetName(u"Spellchecker language");
  appearance->AddChildView(std::make_unique<views::Label>(
      u"Email for Share: \"system\", or a compose URL with %s (subject) and "
      u"%b (body)"));
  email_handler_ = appearance->AddChildView(std::make_unique<views::Textfield>());
  email_handler_->GetViewAccessibility().SetName(u"Email handler");
  appearance->AddChildView(std::make_unique<views::Label>(
      u"Interface colours for the current theme (#rrggbb, empty uses the default)"));
  for (const auto& [key, label] :
       {std::pair{"accent", u"Accent"}, std::pair{"bg_main", u"Main background"},
        std::pair{"bg_sidebar", u"Sidebar background"},
        std::pair{"text", u"Text"}}) {
    auto* row = appearance->AddChildView(std::make_unique<views::View>());
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    auto* name = row->AddChildView(std::make_unique<views::Label>(label));
    name->SetPreferredSize(gfx::Size(160, 24));
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* field = row->AddChildView(std::make_unique<views::Textfield>());
    field->GetViewAccessibility().SetName(std::u16string(label) + u" colour");
    row_layout->SetFlexForView(field, 1);
    color_fields_[key] = field;
  }
  auto* apply_row = appearance->AddChildView(std::make_unique<views::View>());
  apply_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  apply_row->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ApplyProductTexts,
                          weak_factory_.GetWeakPtr()),
      u"Apply language, email and colours"));
  apply_row->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active()) {
              editor->preferences_.ResetThemeColors();
              editor->RefreshAppearance();
            }
          },
          weak_factory_.GetWeakPtr()),
      u"Default colours"));
  clock_format_ = appearance->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::CycleClockFormat,
                          weak_factory_.GetWeakPtr()), u"Clock format"));
  clock_format_->SetTooltipText(u"Click to change the clock format");
  appearance_status_ = appearance->AddChildView(std::make_unique<views::Label>());
  appearance_status_->SetMultiLine(true);
  auto* performance =
      sections_->AddTab(u"Performance", std::make_unique<views::View>());
  performance->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 12));
  suspension_timeout_ =
      performance->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&KeyboardEditor::CycleSuspendTimeout,
                              weak_factory_.GetWeakPtr()),
          u"Suspend background pages"));
  suspension_timeout_->SetTooltipText(
      u"Click to change the delay. Eligible pages pause without unloading. "
      u"Visible, pinned and busy pages keep running.");
  for (const auto& [key, label] :
       {std::pair{"disable_gpu",
                  u"Disable GPU hardware acceleration (after restart)"},
        std::pair{"background_throttling",
                  u"Throttle timers in background pages (after restart)"},
        std::pair{"enable_diagnostics",
                  u"Record diagnostics events in memory"}}) {
    appearance_[key] = performance->AddChildView(std::make_unique<views::Checkbox>(
        label, base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                                   weak_factory_.GetWeakPtr(), std::string(key))));
  }
  process_limit_ = performance->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (!editor || !editor->active())
              return;
            const int limit =
                editor->preferences_.ReadInt("process_limit").value_or(4);
            editor->preferences_.SetInt("process_limit", limit % 5 + 1);
            editor->RefreshAppearance();
          },
          weak_factory_.GetWeakPtr()),
      u"Renderer process limit"));
  suspension_status_ =
      performance->AddChildView(std::make_unique<views::Label>());
  suspension_status_->SetMultiLine(true);
  auto* keyboard = sections_->AddTab(u"Keyboard", std::make_unique<views::View>());
  auto* keyboard_layout = keyboard->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(8), 8));
  keyboard->AddChildView(std::make_unique<views::Label>(
      u"One binding per line. Add key records a shortcut or a two-key "
      u"NORMAL sequence."));
  search_ = keyboard->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search actions");
  search_->GetViewAccessibility().SetName(u"Search keyboard actions");
  search_->set_controller(this);
  auto* modifiers = keyboard->AddChildView(std::make_unique<views::View>());
  auto* modifier_layout =
      modifiers->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal));
  modifiers->AddChildView(
      std::make_unique<views::Label>(u"Numbered tab modifier  "));
  tabs_ = modifiers->AddChildView(std::make_unique<views::Textfield>());
  tabs_->SetText(tab_modifiers_);
  tabs_->GetViewAccessibility().SetName(u"Numbered tab modifier");
  tabs_->set_controller(this);
  modifier_layout->SetFlexForView(tabs_, 1);
  auto* scroll = keyboard->AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  rows_scroll_ = scroll;
  // A bounded viewport gives the BoxLayout contents real bounds.
  scroll->ClipHeightTo(0, std::numeric_limits<int>::max());
  rows_ = scroll->SetContents(std::make_unique<views::View>());
  rows_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
  keyboard_layout->SetFlexForView(scroll, 1);
  status_ = keyboard->AddChildView(std::make_unique<views::Label>(u""));
  status_->SetMultiLine(true);
  status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  auto* buttons = keyboard->AddChildView(std::make_unique<views::View>());
  buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ResetAll,
                          weak_factory_.GetWeakPtr()),
      u"Restore defaults"));
  save_ = buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::Save, weak_factory_.GetWeakPtr()),
      u"Apply"));
  save_->SetStyle(ui::ButtonStyle::kProminent);
  static_cast<FlyButton*>(save_.get())->SetAccentText(true);
  save_->SetEnabled(commands_->CanEdit());
  if (!current)
    status_->SetText(
        u"Existing keyboard settings need recovery. They have been preserved.");
  else if (!commands_->CanEdit())
    status_->SetText(u"Keyboard settings are managed.");
  panel->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::Cancel, weak_factory_.GetWeakPtr(), true),
      u"Close"));
  root->AddChildView(std::move(panel));
  BuildSearchSection();
  BuildExtensionsSection();
  overlay_->SetContentsView(std::move(root));
  RefreshAppearance();
  Refresh();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  ShowKeyboard();
}
void KeyboardEditor::BuildSearchSection() {
  auto* scroll =
      sections_->AddTab(u"Search", std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  auto* panel = scroll->SetContents(std::make_unique<views::View>());
  panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
  const auto settings = search_preferences_.Read();
  for (const auto& [key, label] :
       {std::pair{"engine", u"Search URL (%s is replaced by your query)"},
        std::pair{"alias", u"Alias key"}, std::pair{"name", u"Alias name"},
        std::pair{"url", u"Alias search URL"}}) {
    panel->AddChildView(std::make_unique<views::Label>(label));
    auto* field = panel->AddChildView(std::make_unique<views::Textfield>());
    field->GetViewAccessibility().SetName(label);
    search_settings_[key] = field;
    if (std::string_view(key) == "engine" && settings)
      field->SetText(base::UTF8ToUTF16(settings->engine));
    if (std::string_view(key) == "engine")
      search_save_ = panel->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                              weak_factory_.GetWeakPtr(), "engine"),
          u"Save search engine"));
  }
  alias_save_ = panel->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                          weak_factory_.GetWeakPtr(), "alias"),
      u"Add or update alias"));
  alias_remove_ = panel->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                          weak_factory_.GetWeakPtr(), "remove"),
      u"Remove custom alias by key"));
  search_settings_status_ =
      panel->AddChildView(std::make_unique<views::Label>());
  search_settings_status_->SetMultiLine(true);
  search_settings_status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  alias_list_ = panel->AddChildView(std::make_unique<views::Label>());
  alias_list_->SetMultiLine(true);
}
void KeyboardEditor::RefreshSearchSection() {
  if (!alias_list_)
    return;
  const auto settings = search_preferences_.Read();
  const bool editable = search_preferences_.CanEdit();
  for (auto& [key, field] : search_settings_)
    field->SetEnabled(editable);
  for (auto* button :
       {search_save_.get(), alias_save_.get(), alias_remove_.get()})
    button->SetEnabled(editable);
  std::string aliases;
  if (settings)
    for (const auto& [key, alias] : settings->aliases)
      aliases += key + " — " + alias.name + " — " + alias.url + "\n";
  alias_list_->SetText(base::UTF8ToUTF16(aliases));
  if (!editable)
    search_settings_status_->SetText(
        settings ? u"Search choices are managed."
                 : u"Search settings need recovery. Saved choices have been "
                   u"preserved.");
}
views::Textfield* KeyboardEditor::search_setting_for_testing(
    std::string_view key) {
  const auto found = search_settings_.find(std::string(key));
  return found == search_settings_.end() ? nullptr : found->second.get();
}
void KeyboardEditor::ShowSearch() {
  if (!active())
    return;
  StopRecording();
  sections_->SelectTabAt(3, false);
  search_settings_.at("engine")->RequestFocus();
}
void KeyboardEditor::SaveSearchSetting(std::string action) {
  if (!active() || !search_preferences_.CanEdit())
    return;
  auto value = [&](std::string key) {
    return base::UTF16ToUTF8(search_settings_.at(key)->GetText());
  };
  const bool saved =
      action == "engine"  ? search_preferences_.SetEngine(value("engine"))
      : action == "alias" ? search_preferences_.SetAlias(
                                value("alias"), value("name"), value("url"))
                          : search_preferences_.RemoveAlias(value("alias"));
  search_settings_status_->SetText(saved
                                       ? u"Search choices saved."
                                       : u"Cannot save this choice. Check the "
                                         u"alias key and HTTP or HTTPS URL.");
  RefreshSearchSection();
}
void KeyboardEditor::ShowKeyboard() {
  if (!active())
    return;
  StopRecording();
  sections_->SelectTabAt(2, false);
  search_->RequestFocus();
}
void KeyboardEditor::ShowAppearance(bool return_to_page) {
  if (!active())
    return;
  restore_page_focus_ |= return_to_page;
  sections_->SelectTabAt(0, false);
  appearance_.at("theme")->RequestFocus();
}
void KeyboardEditor::SetExtensions(
    FlyExtensions* extensions,
    base::RepeatingCallback<BrowserWindowInterface*()> browser) {
  extensions_ = extensions;
  extensions_browser_ = std::move(browser);
}
void KeyboardEditor::ShowExtensions() {
  if (!active() || !extensions_list_)
    return;
  StopRecording();
  sections_->SelectTabAt(4, false);
  extensions_enabled_->RequestFocus();
}
void KeyboardEditor::BuildExtensionsSection() {
  if (!extensions_)
    return;
  auto* scroll =
      sections_->AddTab(u"Extensions", std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->ClipHeightTo(0, std::numeric_limits<int>::max());
  auto* panel = scroll->SetContents(std::make_unique<views::View>());
  panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(12), 8));
  extensions_enabled_ = panel->AddChildView(std::make_unique<views::Checkbox>(
      u"Enable Chrome extensions",
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (!editor || !editor->active())
              return;
            editor->extensions_->SetEnabled(
                editor->extensions_enabled_->GetChecked());
            editor->RefreshExtensions();
          },
          weak_factory_.GetWeakPtr())));
  panel->AddChildView(std::make_unique<views::Label>(
      u"Extensions belong to the current workspace. Included uBlock Origin "
      u"Lite runs in every workspace unless switched off here; Remove takes "
      u"it out of all workspaces."))->SetMultiLine(true);
  extensions_list_ = panel->AddChildView(std::make_unique<views::View>());
  extensions_list_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  auto* buttons = panel->AddChildView(std::make_unique<views::View>());
  buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  for (const auto& [label, url] :
       {std::pair{u"Browse Chrome Web Store", "https://chromewebstore.google.com/"},
        std::pair{u"Manage in chrome://extensions", "chrome://extensions/"}}) {
    buttons->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, std::string url) {
              if (!editor || !editor->active() || !editor->extensions_browser_)
                return;
              auto* browser = editor->extensions_browser_.Run();
              editor->Cancel(true);
              if (!browser)
                return;
              NavigateParams params(browser, GURL(url),
                                    ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
              params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
              Navigate(&params);
            },
            weak_factory_.GetWeakPtr(), std::string(url)),
        label));
  }
  RefreshExtensions();
}
void KeyboardEditor::RefreshExtensions() {
  if (!extensions_list_ || !extensions_)
    return;
  auto* browser = extensions_browser_ ? extensions_browser_.Run() : nullptr;
  Profile* profile = browser ? browser->GetProfile() : nullptr;
  extensions_enabled_->SetChecked(extensions_->Enabled());
  extensions_enabled_->SetEnabled(extensions_->CanEdit());
  // Rows are rebuilt detached and attached once (see Refresh()).
  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  if (!profile || profile->IsOffTheRecord()) {
    rows->AddChildView(std::make_unique<views::Label>(
        u"Private workspaces run no extensions."));
  }
  const auto list = profile && !profile->IsOffTheRecord()
                        ? extensions_->List(profile)
                        : std::vector<FlyExtensionInfo>();
  if (profile && !profile->IsOffTheRecord() && list.empty())
    rows->AddChildView(
        std::make_unique<views::Label>(u"No extensions in this workspace."));
  for (const auto& info : list) {
    auto* row = rows->AddChildView(std::make_unique<views::View>());
    auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* enabled = row->AddChildView(std::make_unique<views::Checkbox>(
        info.name + u"  " + base::UTF8ToUTF16(info.version) +
        (info.bundled ? u"  (included)" : u"")));
    enabled->SetChecked(info.enabled);
    enabled->SetEnabled(info.removable && extensions_->Enabled());
    enabled->SetCallback(base::BindRepeating(
        [](base::WeakPtr<KeyboardEditor> editor, std::string id,
           views::Checkbox* box) {
          if (!editor || !editor->active() || !editor->extensions_browser_)
            return;
          auto* browser = editor->extensions_browser_.Run();
          if (browser)
            editor->extensions_->SetExtensionEnabled(browser->GetProfile(), id,
                                                     box->GetChecked());
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshExtensions,
                                        editor));
        },
        weak_factory_.GetWeakPtr(), info.id, enabled));
    layout->SetFlexForView(enabled, 1);
    if (info.has_options) {
      row->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(
              [](base::WeakPtr<KeyboardEditor> editor, std::string id) {
                if (!editor || !editor->active() || !editor->extensions_browser_)
                  return;
                auto* browser = editor->extensions_browser_.Run();
                editor->Cancel(true);
                editor->extensions_->OpenOptions(browser, id);
              },
              weak_factory_.GetWeakPtr(), info.id),
          u"Options"));
    }
    auto* remove = row->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, std::string id) {
              if (!editor || !editor->active() || !editor->extensions_browser_)
                return;
              if (auto* browser = editor->extensions_browser_.Run())
                editor->extensions_->Remove(browser->GetProfile(), id);
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshExtensions,
                                            editor));
            },
            weak_factory_.GetWeakPtr(), info.id),
        u"Remove"));
    remove->SetEnabled(info.removable);
  }
  auto* parent = extensions_list_->parent();
  const size_t index = parent->GetIndexOf(extensions_list_).value();
  auto old = parent->RemoveChildViewT(extensions_list_.get());
  extensions_list_ = parent->AddChildViewAt(std::move(rows), index);
  parent->InvalidateLayout();
}
void KeyboardEditor::ShowPerformance() {
  if (!active())
    return;
  StopRecording();
  sections_->SelectTabAt(1, false);
  suspension_timeout_->RequestFocus();
}
void KeyboardEditor::TabSelectedAt(int index) {
  if (index != 2)
    StopRecording();
}
void KeyboardEditor::ClearAppearance() {
  if (sections_)
    sections_->SetListener(nullptr);
  search_settings_.clear();
  search_save_ = nullptr;
  alias_save_ = nullptr;
  alias_remove_ = nullptr;
  search_settings_status_ = nullptr;
  alias_list_ = nullptr;
  appearance_.clear();
  extensions_list_ = nullptr;
  extensions_enabled_ = nullptr;
  color_fields_.clear();
  spellcheck_ = nullptr;
  email_handler_ = nullptr;
  process_limit_ = nullptr;
  clock_format_ = nullptr;
  suspension_timeout_ = nullptr;
  suspension_status_ = nullptr;
  appearance_status_ = nullptr;
  sections_ = nullptr;
}
views::Checkbox* KeyboardEditor::appearance_for_testing(std::string_view key) {
  auto it = appearance_.find(std::string(key));
  return it == appearance_.end() ? nullptr : it->second.get();
}
void KeyboardEditor::RefreshAppearance() {
  RefreshSearchSection();
  if (appearance_.empty())
    return;
  const auto theme = preferences_.ReadTheme();
  const auto clock = preferences_.ReadClock();
  const auto frequent = preferences_.ReadFrequentSitesVisible();
  const auto footer = preferences_.ReadStatusBarVisible();
  const auto controls = preferences_.ReadWindowControlsVisible();
  const auto collapse = preferences_.ReadSidebarAutoCollapse();
  const auto timeout = preferences_.ReadSuspendTimeoutMinutes();
  suspension_timeout_->SetEnabled(preferences_.CanEditSuspendTimeout());
  suspension_timeout_->SetText(
      !timeout        ? u"Automatic suspension unavailable"
      : *timeout == 0 ? u"Automatically suspend background pages: Off"
                      : u"Automatically suspend after " +
                            base::NumberToString16(*timeout) + u" minutes");
  suspension_status_->SetText(
      !timeout ? u"Performance settings need recovery. Saved data has been "
                 u"preserved."
      : !preferences_.CanEditSuspendTimeout()
          ? u"Performance settings are managed."
          : u"Changes are saved immediately. Protected pages stay running.");
  const auto set = [&](const char* key, bool checked, bool enabled) {
    auto* checkbox = appearance_.at(key).get();
    checkbox->SetChecked(checked);
    checkbox->SetEnabled(enabled);
  };
  set("theme", theme && theme->mode == "dark", preferences_.CanEditTheme());
  set("sync_website_theme", theme && theme->sync_websites,
      preferences_.CanEditTheme());
  set("show_clock", clock && clock->visible, preferences_.CanEditClock());
  set("show_frequent_sites", frequent.value_or(false),
      preferences_.CanEditFrequentSites());
  set("show_status_bar", footer.value_or(false), preferences_.CanEditStatusBar());
  set("show_window_controls", controls.value_or(false),
      preferences_.CanEditWindowControls());
  set("sidebar_auto_collapse", collapse.value_or(true),
      preferences_.CanEditSidebarAutoCollapse());
  const bool editable = preferences_.CanEdit();
  set("interface_scale",
      preferences_.ReadString("interface_scale").value_or("normal") == "large",
      editable);
  for (const char* key : {"web_notifications", "disable_gpu",
                          "background_throttling", "enable_diagnostics"})
    set(key, preferences_.ReadBool(key).value_or(false), editable);
  process_limit_->SetEnabled(editable);
  process_limit_->SetText(
      u"Renderer process limit: " +
      base::NumberToString16(preferences_.ReadInt("process_limit").value_or(4)) +
      u" (after restart)");
  if (!spellcheck_->HasFocus())
    spellcheck_->SetText(base::UTF8ToUTF16(
        preferences_.ReadString("spellchecker_language").value_or("")));
  spellcheck_->SetEnabled(editable);
  if (!email_handler_->HasFocus())
    email_handler_->SetText(base::UTF8ToUTF16(
        preferences_.ReadString("email_handler").value_or("system")));
  email_handler_->SetEnabled(editable);
  const bool dark = theme && theme->mode == "dark";
  for (auto& [key, field] : color_fields_) {
    if (!field->HasFocus())
      field->SetText(base::UTF8ToUTF16(
          preferences_.ReadThemeColor(dark, key).value_or("")));
    field->SetPlaceholderText(dark ? u"dark default" : u"light default");
    field->SetEnabled(editable);
  }
  clock_format_->SetEnabled(preferences_.CanEditClock());
  clock_format_->SetText(!clock ? u"Clock format unavailable"
      : clock->format == "datetime" ? u"Clock: date and time"
      : clock->format == "time" ? u"Clock: time"
                                 : u"Clock: date, time and seconds");
  appearance_status_->SetText(
      !theme || !clock || !footer || !controls || !collapse || !frequent
          ? u"Appearance settings need recovery. Saved data has been preserved."
      : !preferences_.CanEditTheme()
          ? u"Appearance settings are managed."
          : u"Appearance changes are saved immediately.");
}
void KeyboardEditor::ApplyProductTexts() {
  if (!active())
    return;
  const auto language = base::UTF16ToUTF8(spellcheck_->GetText());
  std::vector<std::u16string> refused;
  if (!preferences_.SetString(
          "spellchecker_language",
          std::string(base::TrimWhitespaceASCII(language, base::TRIM_ALL))))
    refused.push_back(u"the language (up to 64 letters)");
  const auto email = std::string(base::TrimWhitespaceASCII(
      base::UTF16ToUTF8(email_handler_->GetText()), base::TRIM_ALL));
  if (!preferences_.SetString("email_handler",
                              email.empty() ? "system" : email))
    refused.push_back(u"the email handler (\"system\" or an http(s) URL)");
  const auto theme = preferences_.ReadTheme();
  const bool dark = !theme || theme->mode == "dark";
  bool colours_ok = true;
  for (auto& [key, field] : color_fields_) {
    const auto value = std::string(base::TrimWhitespaceASCII(
        base::UTF16ToUTF8(field->GetText()), base::TRIM_ALL));
    if (value.empty())
      continue;
    colours_ok &= preferences_.SetThemeColor(dark, key, value);
  }
  if (!colours_ok)
    refused.push_back(u"colours (#rrggbb)");
  appearance_status_->SetText(
      refused.empty()
          ? u"Language, email and colours saved."
          : u"Not saved: " + base::JoinString(refused, u", ") + u".");
  RefreshAppearance();
}
void KeyboardEditor::ChangeAppearance(std::string key) {
  if (!active())
    return;
  auto found = appearance_.find(key);
  if (found == appearance_.end())
    return;
  const bool checked = found->second->GetChecked();
  if (key == "theme") {
    const auto theme = preferences_.ReadTheme();
    if (theme && checked != (theme->mode == "dark"))
      preferences_.ToggleTheme();
  } else if (key == "sync_website_theme") {
    preferences_.SetWebsiteThemeSync(checked);
  } else if (key == "show_clock") {
    preferences_.SetClockVisible(checked);
  } else if (key == "show_frequent_sites") {
    preferences_.SetFrequentSitesVisible(checked);
  } else if (key == "show_status_bar") {
    preferences_.SetStatusBarVisible(checked);
  } else if (key == "show_window_controls") {
    preferences_.SetWindowControlsVisible(checked);
  } else if (key == "sidebar_auto_collapse") {
    preferences_.SetSidebarAutoCollapse(checked);
  } else if (key == "interface_scale") {
    preferences_.SetString("interface_scale", checked ? "large" : "normal");
  } else {
    preferences_.SetBool(key, checked);
  }
  RefreshAppearance();
}
void KeyboardEditor::CycleClockFormat() {
  if (active())
    preferences_.CycleClockFormat();
}
void KeyboardEditor::CycleSuspendTimeout() {
  if (active())
    preferences_.CycleSuspendTimeout();
}
void KeyboardEditor::Refresh() {
  StopRecording();
  fields_.clear();
  // Build the rows detached and attach them once: adding layered views one by
  // one to a live widget reorders every layer each time (seconds for the full
  // catalogue).
  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 10));
  // Each action carries two editors and six buttons. Building the whole
  // catalogue costs about a second, so the list starts with the first actions;
  // searching or "Show all" reaches the rest.
  constexpr size_t kInitialRows = 25;
  const auto matches = FilterPalette(catalog_, search_->GetText());
  const bool limited = !show_all_rows_ && search_->GetText().empty() &&
                       matches.size() > kInitialRows;
  for (const auto& entry : matches) {
    if (limited && rows->children().size() == kInitialRows)
      break;
    auto* row = rows->AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    row->AddChildView(
        std::make_unique<views::Label>(base::UTF8ToUTF16(entry.title)));
    for (const auto normal : {false, true}) {
      const Key key{entry.action, normal};
      auto* scope = row->AddChildView(std::make_unique<views::View>());
      auto* layout = scope->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
      auto* label = scope->AddChildView(
          std::make_unique<views::Label>(normal ? u"NORMAL" : u"Global"));
      label->SetPreferredSize(gfx::Size(70, 24));
      auto* field = scope->AddChildView(std::make_unique<views::Textarea>());
      field->SetPreferredSize(gfx::Size(320, 52));
      field->SetText(draft_[key]);
      field->set_controller(this);
      field->GetViewAccessibility().SetName(
          base::UTF8ToUTF16(entry.title) +
          (normal ? u", NORMAL bindings" : u", global bindings"));
      layout->SetFlexForView(field, 1);
      auto* record = scope->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&KeyboardEditor::StartRecording,
                              weak_factory_.GetWeakPtr(), key),
          u"Add key"));
      scope->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&KeyboardEditor::WriteField,
                              weak_factory_.GetWeakPtr(), key, u""),
          u"Clear"));
      scope->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&KeyboardEditor::ResetField,
                              weak_factory_.GetWeakPtr(), key),
          u"Default"));
      fields_.emplace(key, Field{field, record});
    }
  }
  if (limited) {
    auto* more = rows->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor) {
              if (!editor || !editor->active())
                return;
              editor->show_all_rows_ = true;
              editor->Refresh();
            },
            weak_factory_.GetWeakPtr()),
        u"Show all " + base::NumberToString16(matches.size()) +
            u" actions"));
    more->SetStyle(ui::ButtonStyle::kText);
  }
  rows_ = nullptr;
  rows_ = rows_scroll_->SetContents(std::move(rows));
  rows_scroll_->InvalidateLayout();
}
void KeyboardEditor::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& text) {
  if (!active())
    return;
  if (sender == search_) {
    Refresh();
  } else if (sender == tabs_) {
    tab_modifiers_ = text;
  } else {
    for (const auto& [key, field] : fields_)
      if (field.input == sender) {
        draft_[key] = text;
        break;
      }
  }
}
void KeyboardEditor::WriteField(Key key, std::u16string value) {
  StopRecording();
  draft_[key] = value;
  if (const auto field = fields_.find(key); field != fields_.end())
    field->second.input->SetText(value);
}
void KeyboardEditor::ResetField(Key key) {
  const auto& table = key.second ? DefaultKeyboardConfig().normal
                                 : DefaultKeyboardConfig().shortcuts;
  const auto found = table.find(key.first);
  WriteField(key, found == table.end() ? u""
                                       : base::UTF8ToUTF16(base::JoinString(
                                             found->second, "\n")));
}
void KeyboardEditor::ResetAll() {
  StopRecording();
  for (const auto& [key, value] : draft_) {
    const auto& table = key.second ? DefaultKeyboardConfig().normal
                                   : DefaultKeyboardConfig().shortcuts;
    const auto found = table.find(key.first);
    draft_[key] =
        found == table.end()
            ? u""
            : base::UTF8ToUTF16(base::JoinString(found->second, "\n"));
  }
  tab_modifiers_ = base::UTF8ToUTF16(DefaultKeyboardConfig().tabs);
  tabs_->SetText(tab_modifiers_);
  status_->SetText(u"Defaults restored in this draft. Apply to save them.");
  Refresh();
}
void KeyboardEditor::StartRecording(Key key) {
  StopRecording();
  recording_ = std::move(key);
  fields_.at(*recording_).record->RequestFocus();
  status_->SetText(recording_->second
                       ? u"Press one or two keys. Escape cancels recording."
                       : u"Press a shortcut. Escape cancels recording.");
}
void KeyboardEditor::StopRecording() {
  recording_timer_.Stop();
  recording_.reset();
  chord_.clear();
}
void KeyboardEditor::FinishRecording() {
  if (!recording_ || chord_.empty())
    return;
  const auto key = *recording_;
  auto bindings =
      base::SplitString(base::UTF16ToUTF8(draft_[key]), "\n",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (std::ranges::find(bindings, chord_) == bindings.end())
    bindings.push_back(chord_);
  WriteField(key, base::UTF8ToUTF16(base::JoinString(bindings, "\n")));
  status_->SetText(u"Binding added to this draft. Apply to save it.");
}
bool KeyboardEditor::HandleOverlayKey(const ui::KeyEvent& event) {
  if (!active())
    return false;
  if (event.type() != ui::EventType::kKeyPressed || event.is_char())
    return recording();
  const input::NativeWebKeyboardEvent native(event);
  const auto stroke = NativeEventStroke(native);
  if (stroke == "Escape") {
    if (recording()) {
      StopRecording();
      status_->SetText(u"Recording cancelled.");
    } else {
      Cancel(true);
    }
    return true;
  }
  if (recording()) {
    if (native.skip_if_unhandled ||
        (native.GetModifiers() & (blink::WebInputEvent::kIsComposing |
                                  blink::WebInputEvent::kAltGrKey))) {
      StopRecording();
      status_->SetText(u"Composition keys are not recorded.");
      return false;
    }
    if (event.is_repeat() || stroke.empty())
      return true;
    if (chord_.empty()) {
      chord_ = stroke;
      if (!recording_->second) {
        FinishRecording();
      } else {
        recording_timer_.Start(FROM_HERE, base::Milliseconds(700),
                               base::BindOnce(&KeyboardEditor::FinishRecording,
                                              weak_factory_.GetWeakPtr()));
      }
    } else {
      chord_ += " " + stroke;
      FinishRecording();
    }
    return true;
  }
  if (!stroke.empty() &&
      std::ranges::find(close_bindings_, stroke) != close_bindings_.end()) {
    Cancel(true);
    return true;
  }
  return false;
}
bool KeyboardEditor::HandleKeyEvent(views::Textfield*,
                                    const ui::KeyEvent& event) {
  return HandleOverlayKey(event);
}
void KeyboardEditor::Save() {
  StopRecording();
  if (commands_->generation() != generation_) {
    status_->SetText(
        u"Keyboard settings changed while this editor was open. "
        u"Reopen it to edit the latest settings.");
    return;
  }
  auto config = original_.ToValue();
  for (const auto& [key, text] : draft_) {
    base::ListValue bindings;
    for (const auto& chord :
         base::SplitString(base::UTF16ToUTF8(text), "\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY))
      bindings.Append(chord);
    config.FindDict(key.second ? "normal" : "shortcuts")
        ->Set(key.first, std::move(bindings));
  }
  config.Set("tabs", base::UTF16ToUTF8(tab_modifiers_));
  const auto saved = commands_->Save(config);
  if (!saved.config) {
    status_->SetText(base::UTF8ToUTF16(saved.error));
    return;
  }
  Cancel(true);
}
void KeyboardEditor::Cancel(bool restore_focus) {
  StopRecording();
  if (!overlay_)
    return;
  if (!restore_focus)
    cancelled_ = true;
  if (closing_)
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::FinishClose,
                                weak_factory_.GetWeakPtr(), restore_focus));
}
void KeyboardEditor::FinishClose(bool restore_focus) {
  auto contents = web_contents() ? web_contents()->GetWeakPtr() : nullptr;
  Observe(nullptr);
  fields_.clear();
  search_ = nullptr;
  tabs_ = nullptr;
  rows_ = nullptr;
  rows_scroll_ = nullptr;
  status_ = nullptr;
  save_ = nullptr;
  ClearAppearance();
  overlay_.reset();
  draft_.clear();
  catalog_.clear();
  closing_ = false;
  if (restore_focus && contents && !cancelled_) {
    if (restore_page_focus_)
      contents->Focus();
    else
      contents->RestoreFocus();
  }
}
void KeyboardEditor::EnsureRowForTesting(std::string_view action,
                                         bool normal) {
  if (active() && !fields_.contains({std::string(action), normal}) &&
      !show_all_rows_) {
    show_all_rows_ = true;
    Refresh();
  }
}
views::Textfield* KeyboardEditor::field_for_testing(std::string_view action,
                                                    bool normal) {
  EnsureRowForTesting(action, normal);
  auto it = fields_.find({std::string(action), normal});
  return it == fields_.end() ? nullptr : it->second.input.get();
}
views::MdTextButton* KeyboardEditor::record_for_testing(std::string_view action,
                                                        bool normal) {
  EnsureRowForTesting(action, normal);
  auto it = fields_.find({std::string(action), normal});
  return it == fields_.end() ? nullptr : it->second.record.get();
}
std::u16string KeyboardEditor::error_for_testing() const {
  return status_ ? std::u16string(status_->GetText()) : std::u16string();
}
void KeyboardEditor::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Cancel(true);
}
void KeyboardEditor::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    Cancel();
}
void KeyboardEditor::OnWidgetDestroying(views::Widget*) {
  desktop_ = nullptr;
  Cancel();
}
void KeyboardEditor::PrimaryPageChanged(content::Page&) {
  Cancel();
}
void KeyboardEditor::WebContentsDestroyed() {
  Cancel();
}
void KeyboardEditor::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
}  // namespace fly
