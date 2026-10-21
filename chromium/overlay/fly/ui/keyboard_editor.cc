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
#include "chrome/browser/ui/tabs/tab_strip_model.h"

#include <array>
#include <limits>

#include <algorithm>
#include <set>
#include "base/functional/bind.h"
#include <cmath>
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "fly/import/legacy_import.h"
#include "fly/fly_version.h"
#include "base/version_info/version_info.h"
#include "base/task/thread_pool.h"
#include "base/files/file_util.h"
#include "base/json/json_writer.h"
#include "base/json/json_reader.h"
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
#include "components/vector_icons/vector_icons.h"
#include "ui/views/controls/slider.h"
#include "ui/base/models/simple_combobox_model.h"
#include "ui/views/controls/combobox/combobox.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/controls/textarea/textarea.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
namespace {
// Electron's Preferences choices (index.html).
constexpr std::array<std::string_view, 5> kSearchEngines = {
    "https://duckduckgo.com/?q=%s", "https://www.google.com/search?q=%s",
    "https://www.startpage.com/sp/search?query=%s",
    "https://search.brave.com/search?q=%s", "https://kagi.com/search?q=%s"};
constexpr auto kEmailHandlers = std::to_array<std::pair<std::string_view, std::u16string_view>>({
    {"system", u"System default (mailto:)"},
    {"https://mail.zoho.eu/zm/comp.do?originalMode=compose&app=6&subject=%s&body=%b",
     u"Zoho Mail (EU)"},
    {"https://mail.google.com/mail/u/0/#inbox?compose=new&subject=%s&body=%b",
     u"Gmail"},
    {"https://outlook.live.com/mail/0/deeplink/compose?subject=%s&body=%b",
     u"Outlook Web"},
    {"https://mail.proton.me/u/0/compose?subject=%s&body=%b", u"ProtonMail"},
    {"https://app.fastmail.com/mail/compose?subject=%s&body=%b", u"Fastmail"}});
constexpr auto kSpellcheckLanguages = std::to_array<std::pair<std::string_view, std::u16string_view>>({
        {"en-GB", u"English (United Kingdom)"},
        {"en-US", u"English (United States)"},
        {"en-CA", u"English (Canada)"},
        {"en-AU", u"English (Australia)"},
        {"cs", u"Czech (Čeština)"},
        {"de", u"German (Deutsch)"},
        {"fr", u"French (Français)"},
        {"es", u"Spanish (Español)"},
        {"it", u"Italian (Italiano)"},
        {"pt", u"Portuguese (Português)"},
        {"nl", u"Dutch (Nederlands)"},
        {"pl", u"Polish (Polski)"},
        {"disabled", u"Disabled"}});
constexpr auto kBuiltinAliases = std::to_array<std::pair<std::u16string_view, std::u16string_view>>({
        {u"!w", u"Wikipedia"},       {u"!gh", u"GitHub Search"},
        {u"!yt", u"YouTube Search"}, {u"!a", u"Arch Linux Wiki"},
        {u"!aur", u"AUR Packages"},  {u"!so", u"StackOverflow"},
        {u"!bangs", u"13,500+ Bangs"}, {u"g", u"Google Search"},
        {u"ddg", u"DuckDuckGo Search"}, {u"pkg", u"Arch Packages"},
        {u"r", u"Reddit Search"},    {u"b", u"Brave Search"},
        {u"k", u"Kagi Search"},      {u"sp", u"Startpage Search"}});
constexpr auto kCalculatorExamples = std::to_array<std::pair<std::u16string_view, std::u16string_view>>({{u"240 * 1.2", u"288"},
                             {u"15% of 200", u"30"},
                             {u"2^10", u"1024"},
                             {u"sqrt(144)", u"12"}});
// Labels in Preferences follow the Fly palette.
std::unique_ptr<FlyLabel> MakeEditorLabel(std::u16string text = u"") {
  auto label =
      std::make_unique<FlyLabel>(std::move(text), FlyFace::kSans, 12);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}
}  // namespace
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
  // An open file chooser must not call back into a destroyed editor.
  if (file_dialog_)
    file_dialog_->ListenerDestroyed();
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
  if (!desktop_)
    return;
  restore_page_focus_ = ShouldRestoreNativePageFocus(contents);
  Observe(contents);
  if (contents)
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
  // Electron shows Preferences as a page over the content area: an index of
  // sections on the left, the selected section on the right, and a search
  // that filters every section.
  const int width = std::min(FlyScaled(1100), std::max(320, bounds.width() - 32));
  const int margin = std::max(8, (bounds.width() - width) / 2);
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(16, margin, 16, margin)));
  auto panel = std::make_unique<views::View>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  panel->SetPreferredSize(
      gfx::Size(width, std::max(160, bounds.height() - 32)));
  panel->SetBackground(FlyBackground(FlyRole::kOverlayMain, 6));
  panel->SetBorder(FlyBorder(FlyRole::kBorder, 1, 6));
  auto* header = panel->AddChildView(std::make_unique<views::View>());
  auto* header_layout =
      header->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  header_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  header->AddChildView(std::make_unique<FlyLabel>(
      u"Preferences", FlyFace::kSans, 18, gfx::Font::Weight::SEMIBOLD,
      FlyLabel::Tone::kAccent));
  settings_search_ = header->AddChildView(std::make_unique<views::Textfield>());
  settings_search_->SetPlaceholderText(u"Search settings");
  settings_search_->GetViewAccessibility().SetName(u"Search settings");
  settings_search_->set_controller(this);
  header_layout->SetFlexForView(settings_search_, 1);
  settings_search_status_ = header->AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, 11, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kDim));
  header->AddChildView(MakeFlyIconButton(
      base::BindRepeating(&KeyboardEditor::Cancel, weak_factory_.GetWeakPtr(),
                          true),
      vector_icons::kCloseIcon, u"Close", 26));
  // Adding the first tab selects it; keep the section to reopen.
  const int reopen_pane = last_pane_;
  sections_ = panel->AddChildView(std::make_unique<views::TabbedPane>(
      views::TabbedPane::Orientation::kVertical,
      views::TabbedPane::TabStripStyle::kHighlight));
  sections_->SetListener(this);
  layout->SetFlexForView(sections_, 1);
  items_.clear();
  BuildGeneralPane();
  BuildAppearancePane();
  BuildKeyboardPane(current.has_value());
  BuildPrivacyPane();
  BuildExtensionsSection();
  BuildPerformancePane();
  BuildDiagnosticsPane();
  BuildAboutPane();
  for (size_t i = 0; i < sections_->GetTabCount(); ++i) {
    auto* tab = sections_->GetTabAt(i);
    tab->SetHeight(FlyScaled(44));
  }
  root->AddChildView(std::move(panel));
  DisableSubpixelText(root.get());
  overlay_->SetContentsView(std::move(root));
  RefreshAppearance();
  Refresh();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  // Electron reopens the section shown last in this session.
  last_pane_ = reopen_pane;
  sections_->SelectTabAt(last_pane_, false);
  if (last_pane_ == kKeyboardPane)
    search_->RequestFocus();
  else
    settings_search_->RequestFocus();
}

// One Electron .settings-group: a heading over its rows.
views::View* KeyboardEditor::AddGroup(views::View* pane, std::u16string title) {
  auto* group = pane->AddChildView(std::make_unique<views::View>());
  auto* layout = group->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::TLBR(0, 0, 18, 0),
      8));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* heading = group->AddChildView(std::make_unique<FlyLabel>(
      std::move(title), FlyFace::kMono, 11, gfx::Font::Weight::SEMIBOLD,
      FlyLabel::Tone::kAccent));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return group;
}

// One Electron .setting-item: name and description on the left, the control
// on the right (or below it for wide controls). Rows take part in search.
views::View* KeyboardEditor::AddItem(views::View* group,
                                     std::u16string label,
                                     std::u16string description,
                                     std::unique_ptr<views::View> control,
                                     bool control_below) {
  auto* item = group->AddChildView(std::make_unique<views::View>());
  item->SetBackground(FlyBackground(FlyRole::kOverlaySidebar, 6));
  item->SetBorder(FlyBorder(FlyRole::kBorder, 1, 6, gfx::Insets::VH(10, 12)));
  auto* layout = item->SetLayoutManager(std::make_unique<views::BoxLayout>(
      control_below ? views::BoxLayout::Orientation::kVertical
                    : views::BoxLayout::Orientation::kHorizontal,
      gfx::Insets(), control_below ? 8 : 16));
  layout->set_cross_axis_alignment(
      control_below ? views::BoxLayout::CrossAxisAlignment::kStretch
                    : views::BoxLayout::CrossAxisAlignment::kCenter);
  auto* info = item->AddChildView(std::make_unique<views::View>());
  info->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 3));
  if (!label.empty()) {
    auto* name = info->AddChildView(std::make_unique<FlyLabel>(
        label, FlyFace::kSans, 13, gfx::Font::Weight::SEMIBOLD));
    name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
  if (!description.empty()) {
    auto* text = info->AddChildView(std::make_unique<FlyLabel>(
        description, FlyFace::kSans, 11, gfx::Font::Weight::NORMAL,
        FlyLabel::Tone::kDim));
    text->SetMultiLine(true);
    text->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    text->SetMaximumWidth(560);
  }
  if (!control_below)
    layout->SetFlexForView(info, 1);
  if (control)
    item->AddChildView(std::move(control));
  items_.push_back({static_cast<int>(sections_->GetTabCount()) - 1, item,
                    group, base::ToLowerASCII(label + u" " + description)});
  return item;
}

views::View* KeyboardEditor::AddPane(std::u16string title) {
  auto* scroll =
      sections_->AddTab(title, std::make_unique<views::ScrollView>());
  // Highlight tabs default to 32px, including 16px of vertical padding.
  // Size them from the actual font metrics instead of clipping their labels.
  auto* tab = sections_->GetTabAt(sections_->GetTabCount() - 1);
  int title_height = 0;
  for (views::View* child : tab->children())
    title_height = std::max(title_height, child->GetPreferredSize().height());
  tab->SetHeight(
      std::max(FlyScaled(44), title_height + tab->GetInsets().height()));
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->ClipHeightTo(0, std::numeric_limits<int>::max());
  scroll->SetBackgroundColor(FlyRoleColor(FlyRole::kOverlayMain));
  auto* pane = scroll->SetContents(std::make_unique<views::View>());
  auto* layout = pane->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 20), 4));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  pane_titles_.push_back(title);
  auto* heading = pane->AddChildView(std::make_unique<FlyLabel>(
      title, FlyFace::kSans, 20, gfx::Font::Weight::BOLD));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  heading->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, 12, 0)));
  return pane;
}

// Electron's applySearch: every word must appear in a row's name or
// description; other rows and empty groups hide, and each section shows its
// match count.
void KeyboardEditor::ApplySettingsSearch() {
  if (!settings_search_)
    return;
  const auto query = base::ToLowerASCII(std::u16string(
      base::TrimWhitespace(settings_search_->GetText(), base::TRIM_ALL)));
  const auto terms = base::SplitString(query, u" \t", base::TRIM_WHITESPACE,
                                       base::SPLIT_WANT_NONEMPTY);
  std::vector<int> counts(pane_titles_.size(), 0);
  std::set<views::View*> visible_groups;
  for (auto& item : items_) {
    bool match = true;
    for (const auto& term : terms)
      match &= item.text.find(term) != std::u16string::npos;
    item.view->SetVisible(match);
    if (match) {
      if (item.pane >= 0 && item.pane < static_cast<int>(counts.size()))
        ++counts[item.pane];
      visible_groups.insert(item.group);
    }
  }
  for (auto& item : items_)
    item.group->SetVisible(terms.empty() ||
                           visible_groups.contains(item.group.get()));
  int total = 0;
  for (size_t i = 0; i < pane_titles_.size(); ++i) {
    total += counts[i];
    sections_->GetTabAt(i)->SetTitleText(
        terms.empty() || counts[i] == 0
            ? pane_titles_[i]
            : pane_titles_[i] + u" (" + base::NumberToString16(counts[i]) +
                  u")");
  }
  settings_search_status_->SetText(
      terms.empty() ? u""
      : total == 0  ? u"No settings match"
                    : base::NumberToString16(total) +
                          (total == 1 ? u" setting" : u" settings"));
  if (!terms.empty() && total > 0 &&
      counts[sections_->GetSelectedTabIndex()] == 0) {
    for (size_t i = 0; i < counts.size(); ++i)
      if (counts[i] > 0) {
        sections_->SelectTabAt(i, false);
        break;
      }
  }
  sections_->InvalidateLayout();
}

// Electron's General section: search and sharing, custom search aliases and
// the address bar's built-in aliases and calculator.
void KeyboardEditor::BuildGeneralPane() {
  auto* pane = AddPane(u"General");
  auto* sharing = AddGroup(pane, u"SEARCH & SHARING");
  const auto settings = search_preferences_.Read();
  // Default search engine: Electron's choices, or any URL with %s.
  auto engine = std::make_unique<views::View>();
  engine->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  engine_choice_ = engine->AddChildView(std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(
          std::vector<ui::SimpleComboboxModel::Item>{
              ui::SimpleComboboxModel::Item(u"DuckDuckGo"),
              ui::SimpleComboboxModel::Item(u"Google"),
              ui::SimpleComboboxModel::Item(u"Startpage"),
              ui::SimpleComboboxModel::Item(u"Brave Search"),
              ui::SimpleComboboxModel::Item(u"Kagi"),
              ui::SimpleComboboxModel::Item(u"Custom URL")})));
  engine_choice_->GetViewAccessibility().SetName(u"Default search engine");
  engine_choice_->SetCallback(base::BindRepeating(
      [](base::WeakPtr<KeyboardEditor> editor) {
        if (!editor || !editor->active())
          return;
        const auto index = editor->engine_choice_->GetSelectedIndex();
        if (!index || *index >= kSearchEngines.size())
          return;
        editor->search_settings_.at("engine")->SetText(
            base::UTF8ToUTF16(kSearchEngines.at(*index)));
        editor->SaveSearchSetting("engine");
      },
      weak_factory_.GetWeakPtr()));
  auto* engine_field = engine->AddChildView(std::make_unique<views::Textfield>());
  engine_field->GetViewAccessibility().SetName(
      u"Search URL (%s is replaced by your query)");
  engine_field->SetPlaceholderText(u"https://example.com/search?q=%s");
  if (settings)
    engine_field->SetText(base::UTF8ToUTF16(settings->engine));
  static_cast<views::BoxLayout*>(engine->GetLayoutManager())
      ->SetFlexForView(engine_field, 1);
  search_settings_["engine"] = engine_field;
  search_save_ = engine->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                          weak_factory_.GetWeakPtr(), "engine"),
      u"Save search engine"));
  AddItem(sharing, u"Default search engine",
          u"Used for searches typed in the address bar. %s is replaced by "
          u"your query.",
          std::move(engine), true);
  std::vector<ui::SimpleComboboxModel::Item> email_items;
  for (const auto& [value, label] : kEmailHandlers)
    email_items.emplace_back(std::u16string(label));
  email_items.emplace_back(u"Custom compose URL…");
  auto email = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(email_items)));
  email->GetViewAccessibility().SetName(u"Email handler");
  email->SetCallback(base::BindRepeating(&KeyboardEditor::ChooseEmailHandler,
                                         weak_factory_.GetWeakPtr()));
  email_choice_ = email.get();
  auto email_box = std::make_unique<views::View>();
  email_box->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  email_box->AddChildView(std::move(email));
  email_handler_ = email_box->AddChildView(std::make_unique<views::Textfield>());
  email_handler_->GetViewAccessibility().SetName(u"Email compose URL");
  email_handler_->SetPlaceholderText(
      u"https://mail.example.com/compose?subject=%s&body=%b");
  static_cast<views::BoxLayout*>(email_box->GetLayoutManager())
      ->SetFlexForView(email_handler_, 1);
  email_box->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ApplyProductTexts,
                          weak_factory_.GetWeakPtr()),
      u"Save"));
  AddItem(sharing, u"Email handler",
          u"Native mail client or a webmail compose page for sharing links.",
          std::move(email_box), true);
  std::vector<ui::SimpleComboboxModel::Item> languages;
  for (const auto& [code, label] : kSpellcheckLanguages)
    languages.emplace_back(std::u16string(label));
  auto spellcheck = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(std::move(languages)));
  spellcheck->GetViewAccessibility().SetName(u"Spellchecker language");
  spellcheck->SetCallback(base::BindRepeating(
      [](base::WeakPtr<KeyboardEditor> editor) {
        if (!editor || !editor->active())
          return;
        const auto index = editor->spellcheck_choice_->GetSelectedIndex();
        if (index && *index < std::size(kSpellcheckLanguages))
          editor->preferences_.SetString(
              "spellchecker_language",
              std::string(kSpellcheckLanguages.at(*index).first));
      },
      weak_factory_.GetWeakPtr()));
  spellcheck_choice_ = spellcheck.get();
  AddItem(sharing, u"Spellchecker language",
          u"Language used to underline misspelled words in text fields.",
          std::move(spellcheck));

  auto* aliases = AddGroup(pane, u"CUSTOM SEARCH ALIASES");
  auto alias_box = std::make_unique<views::View>();
  alias_box->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  for (const auto& [key, label, width] :
       {std::tuple{"alias", u"Alias key", 120}, std::tuple{"name", u"Alias name", 180},
        std::tuple{"url", u"Alias search URL", 0}}) {
    auto* field = alias_box->AddChildView(std::make_unique<views::Textfield>());
    field->GetViewAccessibility().SetName(label);
    field->SetPlaceholderText(label);
    if (width)
      field->SetPreferredSize(gfx::Size(width, 28));
    else
      static_cast<views::BoxLayout*>(alias_box->GetLayoutManager())
          ->SetFlexForView(field, 1);
    search_settings_[key] = field;
  }
  alias_save_ = alias_box->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                          weak_factory_.GetWeakPtr(), "alias"),
      u"Add alias"));
  alias_remove_ = alias_box->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                          weak_factory_.GetWeakPtr(), "remove"),
      u"Remove"));
  AddItem(aliases, u"Add an alias",
          u"Type the key followed by a query in the address bar, for example "
          u"\"wk fly\". The URL replaces %s with the query.",
          std::move(alias_box), true);
  auto alias_info = std::make_unique<views::View>();
  alias_info->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 4));
  search_settings_status_ =
      alias_info->AddChildView(MakeEditorLabel());
  search_settings_status_->SetMultiLine(true);
  search_settings_status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  search_settings_status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  alias_list_ = alias_info->AddChildView(MakeEditorLabel());
  alias_list_->SetMultiLine(true);
  alias_list_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddItem(aliases, u"Your aliases", u"", std::move(alias_info), true);

  auto* builtin = AddGroup(pane, u"ADDRESS BAR ALIASES");
  for (const auto& [key, name] : kBuiltinAliases)
    AddItem(builtin, std::u16string(key), std::u16string(name), nullptr);
  auto* calculator = AddGroup(pane, u"ADDRESS BAR CALCULATOR");
  for (const auto& [input, output] : kCalculatorExamples)
    AddItem(calculator, std::u16string(input), u"= " + std::u16string(output),
            nullptr);
}

void KeyboardEditor::ChooseEmailHandler() {
  if (!active() || !email_choice_)
    return;
  const auto index = email_choice_->GetSelectedIndex();
  if (!index)
    return;
  if (*index < std::size(kEmailHandlers)) {
    preferences_.SetString("email_handler",
                           std::string(kEmailHandlers.at(*index).first));
    email_handler_->SetText(u"");
  } else {
    email_handler_->RequestFocus();
  }
  RefreshAppearance();
}

// Electron's Appearance section.
void KeyboardEditor::BuildAppearancePane() {
  auto* pane = AddPane(u"Appearance");
  auto checkbox = [&](const char* key, std::u16string label) {
    auto box = std::make_unique<views::Checkbox>(
        u"", base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                                 weak_factory_.GetWeakPtr(), std::string(key)));
    box->GetViewAccessibility().SetName(label);
    appearance_[key] = box.get();
    return box;
  };
  auto* size = AddGroup(pane, u"INTERFACE SIZE");
  auto scale = std::make_unique<views::Combobox>(
      std::make_unique<ui::SimpleComboboxModel>(
          std::vector<ui::SimpleComboboxModel::Item>{
              ui::SimpleComboboxModel::Item(u"Normal"),
              ui::SimpleComboboxModel::Item(u"Large (125%)")}));
  scale->GetViewAccessibility().SetName(u"Interface scale");
  scale->SetCallback(base::BindRepeating(
      [](base::WeakPtr<KeyboardEditor> editor) {
        if (editor && editor->active())
          editor->preferences_.SetString(
              "interface_scale",
              editor->scale_choice_->GetSelectedIndex() == 1u ? "large"
                                                              : "normal");
      },
      weak_factory_.GetWeakPtr()));
  scale_choice_ = scale.get();
  AddItem(size, u"Interface scale",
          u"Enlarges the sidebar, footer, overlays and Preferences. Web pages "
          u"keep their own zoom.",
          std::move(scale));

  auto* theme = AddGroup(pane, u"THEME");
  AddItem(theme, u"Dark interface",
          u"Use the dark palette for Fly's interface.",
          checkbox("theme", u"Dark interface"));
  AddItem(theme, u"Match websites to the interface",
          u"Ask websites for their dark or light scheme to follow Fly.",
          checkbox("sync_website_theme", u"Match websites to the interface"));
  for (const auto& [key, label] :
       {std::pair{"accent", u"Accent colour"},
        std::pair{"bg_sidebar", u"Sidebar background"},
        std::pair{"bg_main", u"Canvas background"},
        std::pair{"text", u"Text and icons"}}) {
    auto control = std::make_unique<views::View>();
    control->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    auto* swatch = control->AddChildView(std::make_unique<views::View>());
    swatch->SetPreferredSize(gfx::Size(22, 22));
    swatch->SetBorder(FlyBorder(FlyRole::kBorder, 1, 4));
    color_swatches_[key] = swatch;
    auto* field = control->AddChildView(std::make_unique<views::Textfield>());
    field->SetPreferredSize(gfx::Size(110, 28));
    field->GetViewAccessibility().SetName(std::u16string(label));
    field->set_controller(this);
    color_fields_[key] = field;
    AddItem(theme, label, u"#rrggbb; empty uses the theme default.",
            std::move(control));
  }
  for (const auto& [key, label] :
       {std::pair{"sidebar_opacity", u"Sidebar opacity"},
        std::pair{"overlay_opacity", u"Overlay opacity"}}) {
    auto control = std::make_unique<views::View>();
    control->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    auto* slider = control->AddChildView(std::make_unique<views::Slider>(this));
    slider->SetPreferredSize(gfx::Size(180, 24));
    slider->GetViewAccessibility().SetName(std::u16string(label));
    opacity_sliders_[key] = slider;
    opacity_labels_[key] = control->AddChildView(std::make_unique<FlyLabel>(
        u"100%", FlyFace::kMono, 11));
    AddItem(theme, label,
            std::string_view(key) == "sidebar_opacity"
                ? u"How much of the canvas shows through the sidebar."
                : u"How much of the page shows through overlays.",
            std::move(control));
  }
  auto colour_buttons = std::make_unique<views::View>();
  colour_buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  colour_buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ApplyProductTexts,
                          weak_factory_.GetWeakPtr()),
      u"Apply colours"));
  colour_buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active()) {
              editor->preferences_.ResetThemeColors();
              editor->RefreshAppearance();
            }
          },
          weak_factory_.GetWeakPtr()),
      u"Reset colours to default"));
  AddItem(theme, u"Colours", u"Changes apply to the current theme.",
          std::move(colour_buttons));

  auto* interface = AddGroup(pane, u"INTERFACE");
  AddItem(interface, u"Auto-collapse sidebar",
          u"Collapse the sidebar to a rail until the pointer or keyboard "
          u"needs it.",
          checkbox("sidebar_auto_collapse", u"Auto-collapse sidebar"));
  AddItem(interface, u"Status and mode bar",
          u"Show the footer with the keyboard mode, links, zoom and clock.",
          checkbox("show_status_bar", u"Status and mode bar"));
  AddItem(interface, u"Frequent sites on new tabs",
          u"Show your most visited sites with your bookmarks on new tabs.",
          checkbox("show_frequent_sites", u"Frequent sites on new tabs"));
  auto clock = std::make_unique<views::View>();
  clock->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  clock_format_ = clock->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::CycleClockFormat,
                          weak_factory_.GetWeakPtr()),
      u"Clock format"));
  clock_format_->SetTooltipText(u"Click to change the clock format");
  clock->AddChildView(checkbox("show_clock", u"Clock and date"));
  AddItem(interface, u"Clock and date",
          u"Show the clock in the sidebar and footer.", std::move(clock));
  AddItem(interface, u"Window controls",
          u"Show minimise, maximise and close buttons in the sidebar.",
          checkbox("show_window_controls", u"Window controls"));
  auto status = MakeEditorLabel();
  status->SetMultiLine(true);
  status->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  appearance_status_ = status.get();
  pane->AddChildView(std::move(status));
}

// Electron's Keyboard section: search, customised-only filter, numbered tab
// modifier, the rows, and save/discard/restore/export/import.
void KeyboardEditor::BuildKeyboardPane(bool readable) {
  auto* keyboard =
      sections_->AddTab(u"Keyboard", std::make_unique<views::View>());
  pane_titles_.push_back(u"Keyboard");
  auto* keyboard_layout =
      keyboard->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets::VH(4, 20), 8));
  keyboard_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* heading = keyboard->AddChildView(std::make_unique<FlyLabel>(
      u"Keyboard", FlyFace::kSans, 20, gfx::Font::Weight::BOLD));
  heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  auto* hint = keyboard->AddChildView(std::make_unique<FlyLabel>(
      u"One binding per line. Add key records a shortcut or a two-key "
      u"NORMAL sequence.",
      FlyFace::kSans, 11, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  auto* filters = keyboard->AddChildView(std::make_unique<views::View>());
  auto* filter_layout = filters->SetLayoutManager(
      std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 12));
  filter_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  search_ = filters->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(u"Search actions");
  search_->GetViewAccessibility().SetName(u"Search keyboard actions");
  search_->set_controller(this);
  filter_layout->SetFlexForView(search_, 1);
  modified_only_ = filters->AddChildView(std::make_unique<views::Checkbox>(
      u"Customised only",
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active())
              editor->Refresh();
          },
          weak_factory_.GetWeakPtr())));
  filters->AddChildView(MakeEditorLabel(u"Numbered tabs"));
  tabs_ = filters->AddChildView(std::make_unique<views::Textfield>());
  tabs_->SetText(tab_modifiers_);
  tabs_->SetPreferredSize(gfx::Size(110, 28));
  tabs_->GetViewAccessibility().SetName(u"Numbered tab modifier");
  tabs_->set_controller(this);
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
  status_ = keyboard->AddChildView(MakeEditorLabel(u""));
  status_->SetMultiLine(true);
  status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  auto* buttons = keyboard->AddChildView(std::make_unique<views::View>());
  buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  save_ = buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::Save, weak_factory_.GetWeakPtr()),
      u"Save bindings"));
  save_->SetStyle(ui::ButtonStyle::kProminent);
  static_cast<FlyButton*>(save_.get())->SetAccentText(true);
  save_->SetEnabled(commands_->CanEdit());
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::DiscardKeyboard,
                          weak_factory_.GetWeakPtr()),
      u"Discard changes"));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ResetSection,
                          weak_factory_.GetWeakPtr(), false),
      u"Restore browser shortcuts"));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ResetSection,
                          weak_factory_.GetWeakPtr(), true),
      u"Restore page keys"));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ResetAll,
                          weak_factory_.GetWeakPtr()),
      u"Restore defaults"));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ChooseKeyboardFile,
                          weak_factory_.GetWeakPtr(), true),
      u"Export"));
  buttons->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::ChooseKeyboardFile,
                          weak_factory_.GetWeakPtr(), false),
      u"Import"));
  if (!readable)
    status_->SetText(
        u"Existing keyboard settings need recovery. They have been preserved.");
  else if (!commands_->CanEdit())
    status_->SetText(u"Keyboard settings are managed.");
}

// Electron's Privacy & security section.
void KeyboardEditor::BuildPrivacyPane() {
  auto* pane = AddPane(u"Privacy & security");
  auto* browsing = AddGroup(pane, u"BROWSING");
  auto private_box = std::make_unique<views::Checkbox>(
      u"", base::BindRepeating(&KeyboardEditor::RunAction,
                               weak_factory_.GetWeakPtr(),
                               std::string("toggle-private-mode")));
  private_box->GetViewAccessibility().SetName(u"Global private browsing");
  private_mode_ = private_box.get();
  AddItem(browsing, u"Global private browsing",
          u"Open the current workspace in a private window. Its cookies, "
          u"history and storage are discarded when it closes.",
          std::move(private_box));
  const auto action_button = [&](std::u16string label, std::string action) {
    return std::make_unique<FlyButton>(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(), std::move(action)),
        std::move(label));
  };
  AddItem(browsing, u"Clear site cookies",
          u"Sign out of the current site in this workspace: removes its "
          u"cookies and site storage.",
          action_button(u"Clear site cookies", "clear-site-cookies"));
  AddItem(browsing, u"Clear cache",
          u"Delete the cached files of this workspace. Cookies and sign-ins "
          u"stay.",
          action_button(u"Clear cache", "clear-cache"));
  auto notifications = std::make_unique<views::Checkbox>(
      u"", base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                               weak_factory_.GetWeakPtr(),
                               std::string("web_notifications")));
  notifications->GetViewAccessibility().SetName(u"Website notifications");
  appearance_["web_notifications"] = notifications.get();
  AddItem(browsing, u"Website notifications",
          u"Let websites ask to show notifications.",
          std::move(notifications));
}

// Electron's Performance section.
void KeyboardEditor::BuildPerformancePane() {
  auto* pane = AddPane(u"Performance");
  auto* background = AddGroup(pane, u"BACKGROUND PAGES");
  auto suspension = std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::CycleSuspendTimeout,
                          weak_factory_.GetWeakPtr()),
      u"Suspend background pages");
  suspension->SetTooltipText(
      u"Click to change the delay. Eligible pages pause without unloading. "
      u"Visible, pinned and busy pages keep running.");
  suspension_timeout_ = suspension.get();
  AddItem(background, u"Tab suspension",
          u"Pause background pages after a while without unloading them. "
          u"Visible, pinned and busy pages keep running.",
          std::move(suspension));
  auto* engine = AddGroup(pane, u"ENGINE (RESTART REQUIRED)");
  const auto checkbox = [&](const char* key, std::u16string label) {
    auto box = std::make_unique<views::Checkbox>(
        u"", base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                                 weak_factory_.GetWeakPtr(), std::string(key)));
    box->GetViewAccessibility().SetName(label);
    appearance_[key] = box.get();
    return box;
  };
  AddItem(engine, u"Hardware GPU acceleration off",
          u"Draw pages without the GPU. Use it when the GPU driver misbehaves.",
          checkbox("disable_gpu", u"Disable GPU hardware acceleration"));
  AddItem(engine, u"Background throttling",
          u"Slow timers in pages you are not looking at to save power.",
          checkbox("background_throttling",
                   u"Throttle timers in background pages"));
  auto limit = std::make_unique<FlyButton>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (!editor || !editor->active())
              return;
            const int limit =
                editor->preferences_.ReadInt("process_limit").value_or(4);
            editor->preferences_.SetInt("process_limit", limit % 8 + 1);
            editor->RefreshAppearance();
          },
          weak_factory_.GetWeakPtr()),
      u"Renderer process limit");
  process_limit_ = limit.get();
  AddItem(engine, u"Renderer process limit",
          u"Upper bound for page processes. Lower values use less memory.",
          std::move(limit));
  AddItem(engine, u"Save & restart engine",
          u"Restart Fly to apply engine settings. Tabs are restored.",
          std::make_unique<FlyButton>(
              base::BindRepeating(&KeyboardEditor::RunAction,
                                  weak_factory_.GetWeakPtr(),
                                  std::string("restart")),
              u"Save & restart engine"));
  auto status = MakeEditorLabel();
  status->SetMultiLine(true);
  status->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  suspension_status_ = status.get();
  pane->AddChildView(std::move(status));
}

// Electron's Diagnostics section.
void KeyboardEditor::BuildDiagnosticsPane() {
  auto* pane = AddPane(u"Diagnostics");
  auto* group = AddGroup(pane, u"LOCAL DIAGNOSTICS");
  auto box = std::make_unique<views::Checkbox>(
      u"", base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                               weak_factory_.GetWeakPtr(),
                               std::string("enable_diagnostics")));
  box->GetViewAccessibility().SetName(u"Local diagnostics recording");
  appearance_["enable_diagnostics"] = box.get();
  AddItem(group, u"Local diagnostics recording",
          u"Keep a log of browser events in memory for bug reports. Nothing "
          u"leaves this computer unless you share a report.",
          std::move(box));
  AddItem(group, u"Diagnostics & Health Hub",
          u"System and runtime details, processes and memory, the event log "
          u"and a sanitised bug report.",
          std::make_unique<FlyButton>(
              base::BindRepeating(&KeyboardEditor::RunAction,
                                  weak_factory_.GetWeakPtr(),
                                  std::string("open-diagnostics")),
              u"Open Diagnostics Hub"));
}

// Electron's About section.
void KeyboardEditor::BuildAboutPane() {
  auto* pane = AddPane(u"About");
  auto* about = AddGroup(pane, u"FLY");
  auto version = std::make_unique<views::View>();
  version->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  for (const auto& [label, action] :
       {std::pair{u"What's new", "show-whats-new"},
        std::pair{u"Check for updates", "check-for-updates"}})
    version->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(), std::string(action)),
        label));
  update_action_ = version->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::RunAction,
                          weak_factory_.GetWeakPtr(),
                          std::string("install-update")),
      u"Install update"));
  update_action_->SetVisible(!update_available_.empty());
  if (!update_available_.empty())
    update_action_->SetText(u"Install " + base::UTF8ToUTF16(update_available_));
  AddItem(about,
          base::UTF8ToUTF16(std::string("Fly v") + kFlyVersion),
          u"Engine Chromium " +
              base::UTF8ToUTF16(std::string(version_info::GetVersionNumber())),
          std::move(version));
  auto status = std::make_unique<FlyLabel>(update_status_, FlyFace::kSans, 12);
  status->SetMultiLine(true);
  status->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  update_label_ = status.get();
  AddItem(about, u"Updates", u"", std::move(status), true);
  auto* desktop = AddGroup(pane, u"DESKTOP INTEGRATION");
  auto entry = std::make_unique<views::View>();
  entry->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  desktop_entry_label_ = entry->AddChildView(
      std::make_unique<FlyLabel>(desktop_entry_status_, FlyFace::kSans, 12));
  desktop_entry_label_->SetMultiLine(true);
  desktop_entry_label_->SetMaximumWidth(520);
  desktop_entry_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  desktop_entry_button_ = entry->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::RunAction,
                          weak_factory_.GetWeakPtr(),
                          std::string("desktop-entry")),
      desktop_entry_action_.empty() ? u"Add to menu" : desktop_entry_action_));
  desktop_entry_button_->SetVisible(!desktop_entry_action_.empty());
  AddItem(desktop, u"Applications menu", u"", std::move(entry), true);
  auto browser = std::make_unique<views::View>();
  browser->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  default_browser_label_ = browser->AddChildView(
      std::make_unique<FlyLabel>(default_browser_status_, FlyFace::kSans, 12));
  default_browser_label_->SetMultiLine(true);
  default_browser_label_->SetMaximumWidth(520);
  default_browser_label_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  default_browser_button_ = browser->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&KeyboardEditor::RunAction,
                          weak_factory_.GetWeakPtr(),
                          std::string("desktop-default")),
      u"Make default"));
  default_browser_button_->SetVisible(can_make_default_);
  AddItem(desktop, u"Default browser", u"", std::move(browser), true);
}

void KeyboardEditor::RunAction(std::string action) {
  if (!active())
    return;
  // Extension sources and tools act on the selected workspace from here.
  if (action == "extensions-install-package") {
    ChooseFile(FileChoice::kExtensionPackage);
    return;
  }
  if (action == "extensions-load-unpacked") {
    ChooseFile(FileChoice::kExtensionFolder);
    return;
  }
  if (action == "extensions-store") {
    OpenUrl("https://chromewebstore.google.com/");
    return;
  }
  if (action == "extensions-page") {
    OpenUrl("chrome://extensions/");
    return;
  }
  if (action.starts_with("extensions-store-link:")) {
    // Accept a detail link or a bare 32-letter extension id.
    const GURL link(action.substr(22));
    std::string id = action.substr(22);
    if (link.is_valid()) {
      const auto parts = base::SplitString(link.path(), "/",
                                           base::TRIM_WHITESPACE,
                                           base::SPLIT_WANT_NONEMPTY);
      id = parts.empty() ? std::string() : parts.back();
    }
    if (id.size() != 32 ||
        !std::ranges::all_of(id, [](char c) { return c >= 'a' && c <= 'p'; })) {
      SetExtensionUsage(u"Paste a Chrome Web Store extension link.");
      return;
    }
    OpenUrl("https://chromewebstore.google.com/detail/" + id);
    return;
  }
  if (extensions_ && extensions_browser_ &&
      (action == "extensions-usage" || action == "extensions-reload")) {
    auto* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
    if (action == "extensions-reload")
      SetExtensionUsage(extensions_->ReloadUnpacked()
                            ? u"Extensions reloaded. Reload open tabs to "
                              u"refresh their content scripts."
                            : u"No unpacked extensions to reload.");
    else if (profile)
      SetExtensionUsage(extensions_->Usage(profile));
    return;
  }
  if (run_action_)
    run_action_.Run(std::move(action));
}

void KeyboardEditor::OpenUrl(std::string url) {
  Cancel(true);
  if (run_action_)
    run_action_.Run("open-window:" + url);
}

void KeyboardEditor::SetUpdateStatus(std::u16string status,
                                     std::string available_version) {
  update_status_ = std::move(status);
  update_available_ = std::move(available_version);
  if (update_label_)
    update_label_->SetText(update_status_);
  if (update_action_) {
    update_action_->SetVisible(!update_available_.empty());
    update_action_->SetText(u"Install " +
                            base::UTF8ToUTF16(update_available_));
  }
}

void KeyboardEditor::SetDesktopIntegration(std::u16string entry_status,
                                           std::u16string entry_action,
                                           std::u16string default_status,
                                           bool can_make_default) {
  desktop_entry_status_ = std::move(entry_status);
  desktop_entry_action_ = std::move(entry_action);
  default_browser_status_ = std::move(default_status);
  can_make_default_ = can_make_default;
  if (desktop_entry_label_)
    desktop_entry_label_->SetText(desktop_entry_status_);
  if (desktop_entry_button_) {
    desktop_entry_button_->SetVisible(!desktop_entry_action_.empty());
    desktop_entry_button_->SetText(desktop_entry_action_);
  }
  if (default_browser_label_)
    default_browser_label_->SetText(default_browser_status_);
  if (default_browser_button_)
    default_browser_button_->SetVisible(can_make_default_);
}

void KeyboardEditor::SetPrivateMode(bool on) {
  private_on_ = on;
  if (private_mode_)
    private_mode_->SetChecked(on);
}

void KeyboardEditor::DiscardKeyboard() {
  if (!active())
    return;
  StopRecording();
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  tabs_->SetText(tab_modifiers_);
  status_->SetText(u"Changes discarded.");
  Refresh();
}

void KeyboardEditor::ResetSection(bool normal) {
  if (!active())
    return;
  StopRecording();
  const auto& defaults = DefaultKeyboardConfig();
  for (auto& [key, value] : draft_) {
    if (key.second != normal)
      continue;
    const auto& table = normal ? defaults.normal : defaults.shortcuts;
    const auto found = table.find(key.first);
    value = found == table.end()
                ? u""
                : base::UTF8ToUTF16(base::JoinString(found->second, "\n"));
  }
  if (!normal) {
    tab_modifiers_ = base::UTF8ToUTF16(defaults.tabs);
    tabs_->SetText(tab_modifiers_);
  }
  status_->SetText(normal ? u"Page keys restored in this draft. Save to apply."
                          : u"Browser shortcuts restored in this draft. Save "
                            u"to apply.");
  Refresh();
}

bool KeyboardEditor::IsCustomised(const std::string& action) const {
  const auto& defaults = DefaultKeyboardConfig();
  for (const auto normal : {false, true}) {
    const auto& table = normal ? defaults.normal : defaults.shortcuts;
    const auto found = table.find(action);
    const auto expected =
        found == table.end()
            ? std::u16string()
            : base::UTF8ToUTF16(base::JoinString(found->second, "\n"));
    const auto draft = draft_.find({action, normal});
    if (draft != draft_.end() && draft->second != expected)
      return true;
  }
  return false;
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
  sections_->SelectTabAt(kGeneralPane, false);
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
  sections_->SelectTabAt(kKeyboardPane, false);
  search_->RequestFocus();
}
void KeyboardEditor::ShowAppearance(bool return_to_page) {
  if (!active())
    return;
  restore_page_focus_ |= return_to_page;
  sections_->SelectTabAt(kAppearancePane, false);
  appearance_.at("theme")->RequestFocus();
}
void KeyboardEditor::SetExtensions(
    FlyExtensions* extensions,
    base::RepeatingCallback<BrowserWindowInterface*()> browser,
    base::RepeatingCallback<Profile*()> profile) {
  extensions_ = extensions;
  extensions_browser_ = std::move(browser);
  extensions_profile_ = std::move(profile);
}
void KeyboardEditor::ShowExtensions() {
  if (!active() || !extensions_list_)
    return;
  StopRecording();
  sections_->SelectTabAt(kExtensionsPane, false);
  extensions_enabled_->RequestFocus();
}
// Electron's Extensions section.
void KeyboardEditor::BuildExtensionsSection() {
  auto* pane = AddPane(u"Extensions");
  if (!extensions_) {
    pane->AddChildView(MakeEditorLabel(
        u"Extensions are not available."));
    return;
  }
  auto* general = AddGroup(pane, u"CHROME EXTENSIONS");
  auto enabled = std::make_unique<views::Checkbox>(
      u"",
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (!editor || !editor->active())
              return;
            editor->extensions_->SetEnabled(
                editor->extensions_enabled_->GetChecked());
            editor->RefreshExtensions();
          },
          weak_factory_.GetWeakPtr()));
  enabled->GetViewAccessibility().SetName(u"Enable Chrome extensions");
  extensions_enabled_ = enabled.get();
  AddItem(general, u"Enable Chrome extensions",
          u"Extensions belong to the current workspace. Included uBlock Origin "
          u"Lite runs in every workspace unless switched off here; Remove "
          u"takes it out of all workspaces.",
          std::move(enabled));
  auto* install = AddGroup(pane, u"FIND AND INSTALL");
  const auto action_button = [&](std::u16string label, std::string action) {
    return std::make_unique<FlyButton>(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(), std::move(action)),
        std::move(label));
  };
  auto sources = std::make_unique<views::View>();
  sources->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  sources->AddChildView(action_button(u"Browse Chrome Web Store",
                                      "extensions-store"));
  sources->AddChildView(action_button(u"Install ZIP or CRX",
                                      "extensions-install-package"));
  sources->AddChildView(action_button(u"Load unpacked folder",
                                      "extensions-load-unpacked"));
  sources->AddChildView(action_button(u"Manage in chrome://extensions",
                                      "extensions-page"));
  AddItem(install, u"Sources",
          u"Web Store installs use Chromium's prompt. Packages and folders "
          u"are copied into Fly's extensions folder and load in this "
          u"workspace.",
          std::move(sources), true);
  auto link = std::make_unique<views::View>();
  link->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  extensions_store_link_ = link->AddChildView(std::make_unique<views::Textfield>());
  extensions_store_link_->SetPlaceholderText(
      u"https://chromewebstore.google.com/detail/…");
  extensions_store_link_->GetViewAccessibility().SetName(u"Web Store link");
  static_cast<views::BoxLayout*>(link->GetLayoutManager())
      ->SetFlexForView(extensions_store_link_, 1);
  link->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active())
              editor->RunAction(
                  "extensions-store-link:" +
                  base::UTF16ToUTF8(editor->extensions_store_link_->GetText()));
          },
          weak_factory_.GetWeakPtr()),
      u"Open"));
  AddItem(install, u"Install from a Web Store link",
          u"Paste an extension's Chrome Web Store address.", std::move(link),
          true);
  auto* installed = AddGroup(pane, u"INSTALLED");
  auto list = std::make_unique<views::View>();
  list->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  extensions_list_ = list.get();
  AddItem(installed, u"", u"", std::move(list), true);
  auto* usage = AddGroup(pane, u"LIVE RESOURCE USAGE");
  auto usage_box = std::make_unique<views::View>();
  usage_box->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 8));
  auto* usage_buttons = usage_box->AddChildView(std::make_unique<views::View>());
  usage_buttons->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
  usage_buttons->AddChildView(action_button(u"Refresh usage",
                                            "extensions-usage"));
  usage_buttons->AddChildView(action_button(u"Reload extensions",
                                            "extensions-reload"));
  usage_buttons->AddChildView(action_button(u"Open extensions folder",
                                            "extensions-open-dir"));
  extensions_usage_ = usage_box->AddChildView(MakeEditorLabel(
      u"Refresh to see the memory each extension uses in this workspace."));
  extensions_usage_->SetMultiLine(true);
  extensions_usage_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  AddItem(usage, u"Memory per extension", u"", std::move(usage_box), true);
  RefreshExtensions();
}

void KeyboardEditor::SetExtensionUsage(std::u16string text) {
  if (extensions_usage_)
    extensions_usage_->SetText(std::move(text));
}

void KeyboardEditor::RefreshExtensions() {
  if (!extensions_list_ || !extensions_)
    return;
  Profile* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
  extensions_enabled_->SetChecked(extensions_->Enabled());
  extensions_enabled_->SetEnabled(extensions_->CanEdit());
  // Rows are rebuilt detached and attached once (see Refresh()).
  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 6));
  if (!profile || profile->IsOffTheRecord()) {
    rows->AddChildView(MakeEditorLabel(
        profile ? u"Private workspaces run no extensions."
                : u"Select a workspace to manage its extensions."));
  }
  const auto list = profile && !profile->IsOffTheRecord()
                        ? extensions_->List(profile)
                        : std::vector<FlyExtensionInfo>();
  if (profile && !profile->IsOffTheRecord() && list.empty())
    rows->AddChildView(
        MakeEditorLabel(u"No extensions in this workspace."));
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
          auto* profile = editor->extensions_profile_.Run();
          if (profile)
            editor->extensions_->SetExtensionEnabled(profile, id,
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
                editor->extensions_->OpenOptions(
                    browser, id, editor->extensions_profile_.Run());
              },
              weak_factory_.GetWeakPtr(), info.id),
          u"Options"));
    }
    auto* remove = row->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, std::string id) {
              if (!editor || !editor->active() || !editor->extensions_browser_)
                return;
              if (auto* profile = editor->extensions_profile_.Run())
                editor->extensions_->Remove(profile, id);
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
  DisableSubpixelText(rows.get());
  extensions_list_ = parent->AddChildViewAt(std::move(rows), index);
  parent->InvalidateLayout();
}
void KeyboardEditor::ShowPerformance() {
  if (!active())
    return;
  StopRecording();
  sections_->SelectTabAt(kPerformancePane, false);
  suspension_timeout_->RequestFocus();
}
void KeyboardEditor::TabSelectedAt(int index) {
  last_pane_ = index;
  if (index != kKeyboardPane)
    StopRecording();
}
void KeyboardEditor::ClearAppearance() {
  if (sections_) {
    last_pane_ = static_cast<int>(sections_->GetSelectedTabIndex());
    sections_->SetListener(nullptr);
  }
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
  color_swatches_.clear();
  opacity_sliders_.clear();
  opacity_labels_.clear();
  items_.clear();
  pane_titles_.clear();
  settings_search_ = nullptr;
  settings_search_status_ = nullptr;
  engine_choice_ = nullptr;
  email_choice_ = nullptr;
  spellcheck_choice_ = nullptr;
  scale_choice_ = nullptr;
  modified_only_ = nullptr;
  private_mode_ = nullptr;
  update_label_ = nullptr;
  update_action_ = nullptr;
  desktop_entry_label_ = nullptr;
  desktop_entry_button_ = nullptr;
  default_browser_label_ = nullptr;
  default_browser_button_ = nullptr;
  extensions_usage_ = nullptr;
  extensions_store_link_ = nullptr;
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
  scale_choice_->SetSelectedIndex(
      preferences_.ReadString("interface_scale").value_or("normal") == "large"
          ? 1u
          : 0u);
  scale_choice_->SetEnabled(editable);
  for (const char* key : {"web_notifications", "disable_gpu",
                          "background_throttling", "enable_diagnostics"})
    set(key, preferences_.ReadBool(key).value_or(false), editable);
  process_limit_->SetEnabled(editable);
  process_limit_->SetText(
      base::NumberToString16(preferences_.ReadInt("process_limit").value_or(4)) +
      u" processes");
  const auto language =
      preferences_.ReadString("spellchecker_language").value_or("en-GB");
  for (size_t i = 0; i < std::size(kSpellcheckLanguages); ++i)
    if (kSpellcheckLanguages.at(i).first == language)
      spellcheck_choice_->SetSelectedIndex(i);
  spellcheck_choice_->SetEnabled(editable);
  const auto email = preferences_.ReadString("email_handler").value_or("system");
  size_t email_index = std::size(kEmailHandlers);
  for (size_t i = 0; i < std::size(kEmailHandlers); ++i)
    if (kEmailHandlers.at(i).first == email)
      email_index = i;
  email_choice_->SetSelectedIndex(email_index);
  email_choice_->SetEnabled(editable);
  if (!email_handler_->HasFocus())
    email_handler_->SetText(email_index == std::size(kEmailHandlers)
                                ? base::UTF8ToUTF16(email)
                                : u"");
  email_handler_->SetEnabled(editable);
  if (const auto settings = search_preferences_.Read()) {
    size_t engine_index = kSearchEngines.size();
    for (size_t i = 0; i < kSearchEngines.size(); ++i)
      if (kSearchEngines.at(i) == settings->engine)
        engine_index = i;
    engine_choice_->SetSelectedIndex(engine_index);
  }
  engine_choice_->SetEnabled(search_preferences_.CanEdit());
  const bool dark = theme && theme->mode == "dark";
  const auto& palette = GetFlyColors();
  for (auto& [key, field] : color_fields_) {
    const auto stored = preferences_.ReadThemeColor(dark, key).value_or("");
    if (!field->HasFocus())
      field->SetText(base::UTF8ToUTF16(stored));
    field->SetPlaceholderText(dark ? u"dark default" : u"light default");
    field->SetEnabled(editable);
    const SkColor shown = key == "accent"       ? palette.accent
                          : key == "bg_main"    ? palette.bg_main
                          : key == "bg_sidebar" ? palette.bg_sidebar
                                                : palette.text;
    color_swatches_.at(key)->SetBackground(
        views::CreateRoundedRectBackground(shown, 4));
  }
  for (auto& [key, slider] : opacity_sliders_) {
    const int value = preferences_.ReadThemeOpacity(dark, key);
    slider->SetValue((value - 40) / 60.f);
    slider->SetEnabled(editable);
    opacity_labels_.at(key)->SetText(base::NumberToString16(value) + u"%");
  }
  if (private_mode_)
    private_mode_->SetChecked(private_on_);
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
  std::vector<std::u16string> refused;
  // A custom compose URL typed next to the email choices.
  const auto email = std::string(base::TrimWhitespaceASCII(
      base::UTF16ToUTF8(email_handler_->GetText()), base::TRIM_ALL));
  if (!email.empty() && !preferences_.SetString("email_handler", email))
    refused.push_back(u"the email handler (an http(s) compose URL)");
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
          ? u"Saved."
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
                       !(modified_only_ && modified_only_->GetChecked()) &&
                       matches.size() > kInitialRows;
  for (const auto& entry : matches) {
    if (modified_only_ && modified_only_->GetChecked() &&
        !IsCustomised(entry.action))
      continue;
    if (limited && rows->children().size() == kInitialRows)
      break;
    auto* row = rows->AddChildView(std::make_unique<views::View>());
    row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical));
    row->AddChildView(
        MakeEditorLabel(base::UTF8ToUTF16(entry.title)));
    for (const auto normal : {false, true}) {
      const Key key{entry.action, normal};
      auto* scope = row->AddChildView(std::make_unique<views::View>());
      auto* layout = scope->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
      auto* label = scope->AddChildView(
          MakeEditorLabel(normal ? u"NORMAL" : u"Global"));
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
  DisableSubpixelText(rows.get());
  rows_ = nullptr;
  rows_ = rows_scroll_->SetContents(std::move(rows));
  rows_scroll_->InvalidateLayout();
}
void KeyboardEditor::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& text) {
  if (!active())
    return;
  if (sender == settings_search_) {
    ApplySettingsSearch();
  } else if (sender == search_) {
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

void KeyboardEditor::SliderValueChanged(views::Slider* sender,
                                        float value,
                                        float old_value,
                                        views::SliderChangeReason reason) {
  if (!active() || reason != views::SliderChangeReason::kByUser)
    return;
  for (auto& [key, slider] : opacity_sliders_) {
    if (slider != sender)
      continue;
    const auto theme = preferences_.ReadTheme();
    const int percent = 40 + static_cast<int>(std::round(value * 60));
    preferences_.SetThemeOpacity(!theme || theme->mode == "dark", key,
                                 percent);
    opacity_labels_.at(key)->SetText(base::NumberToString16(percent) + u"%");
  }
}

// Electron's Export (only the changes, so later imports keep newer
// defaults) and Import (read as Electron loads keybinds.json).
void KeyboardEditor::ChooseKeyboardFile(bool save) {
  ChooseFile(save ? FileChoice::kExportKeyboard : FileChoice::kImportKeyboard);
}

void KeyboardEditor::ChooseFile(FileChoice choice) {
  if (!active() || !overlay_ || file_dialog_)
    return;
  StopRecording();
  file_choice_ = choice;
  file_dialog_ = ui::SelectFileDialog::Create(this, nullptr);
  ui::SelectFileDialog::FileTypeInfo types;
  switch (choice) {
    case FileChoice::kExportKeyboard:
    case FileChoice::kImportKeyboard:
      types.extensions = {{FILE_PATH_LITERAL("json")}};
      break;
    case FileChoice::kExtensionPackage:
      types.extensions = {{FILE_PATH_LITERAL("zip"), FILE_PATH_LITERAL("crx")}};
      break;
    case FileChoice::kExtensionFolder:
      break;
  }
  const auto type = choice == FileChoice::kExportKeyboard
                        ? ui::SelectFileDialog::SELECT_SAVEAS_FILE
                    : choice == FileChoice::kExtensionFolder
                        ? ui::SelectFileDialog::SELECT_EXISTING_FOLDER
                        : ui::SelectFileDialog::SELECT_OPEN_FILE;
  const std::u16string title =
      choice == FileChoice::kExportKeyboard   ? u"Export keyboard bindings"
      : choice == FileChoice::kImportKeyboard ? u"Import keyboard bindings"
      : choice == FileChoice::kExtensionPackage
          ? u"Select extension package"
          : u"Select unpacked Chrome extension";
  file_dialog_->SelectFile(
      type, title,
      choice == FileChoice::kExportKeyboard
          ? base::FilePath(FILE_PATH_LITERAL("fly-keybinds.json"))
          : base::FilePath(),
      types.extensions.empty() ? nullptr : &types, 1,
      choice == FileChoice::kExportKeyboard ? FILE_PATH_LITERAL("json")
                                            : FILE_PATH_LITERAL(""),
      desktop_->GetNativeWindow());
}

base::DictValue KeyboardEditor::DraftChanges() const {
  const auto& defaults = DefaultKeyboardConfig();
  base::DictValue changes;
  changes.Set("version", 2);
  changes.Set("changesOnly", true);
  for (const auto normal : {false, true}) {
    base::DictValue table;
    for (const auto& [key, text] : draft_) {
      if (key.second != normal)
        continue;
      const auto bindings =
          base::SplitString(base::UTF16ToUTF8(text), "\n",
                            base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      const auto& source = normal ? defaults.normal : defaults.shortcuts;
      const auto found = source.find(key.first);
      if (found != source.end() && found->second == bindings)
        continue;
      if (found == source.end() && bindings.empty())
        continue;
      base::ListValue list;
      for (const auto& binding : bindings)
        list.Append(binding);
      table.Set(key.first, std::move(list));
    }
    changes.Set(normal ? "normal" : "shortcuts", std::move(table));
  }
  if (base::UTF16ToUTF8(tab_modifiers_) != defaults.tabs)
    changes.Set("tabs", base::UTF16ToUTF8(tab_modifiers_));
  return changes;
}

void KeyboardEditor::FileSelected(const ui::SelectedFileInfo& file, int) {
  file_dialog_.reset();
  if (!active())
    return;
  const auto path = file.path();
  if (file_choice_ == FileChoice::kExtensionPackage ||
      file_choice_ == FileChoice::kExtensionFolder) {
    auto* browser = extensions_browser_ ? extensions_browser_.Run() : nullptr;
    auto* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
    if (!extensions_ || !profile)
      return;
    if (file_choice_ == FileChoice::kExtensionPackage)
      extensions_->InstallPackage(
          profile,
          browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr,
          path);
    else
      extensions_->LoadUnpacked(profile, path);
    SetExtensionUsage(u"Installing " + path.BaseName().LossyDisplayName() +
                      u"…");
    return;
  }
  if (file_choice_ == FileChoice::kExportKeyboard) {
    std::string json;
    base::JSONWriter::WriteWithOptions(
        DraftChanges(), base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
        base::BindOnce(
            [](base::FilePath path, std::string json) {
              return base::WriteFile(path, json);
            },
            path, std::move(json)),
        base::BindOnce(
            [](base::WeakPtr<KeyboardEditor> editor, bool ok) {
              if (editor && editor->active())
                editor->status_->SetText(
                    ok ? u"Exported the current bindings, including unsaved "
                         u"changes."
                       : u"The bindings could not be written.");
            },
            weak_factory_.GetWeakPtr()));
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath path) -> std::optional<std::string> {
            std::string text;
            if (!base::ReadFileToStringWithMaxSize(path, &text, 65536))
              return std::nullopt;
            return text;
          },
          path),
      base::BindOnce(&KeyboardEditor::ImportKeyboardText,
                     weak_factory_.GetWeakPtr()));
}

void KeyboardEditor::FileSelectionCanceled() {
  file_dialog_.reset();
}

void KeyboardEditor::ImportKeyboardText(std::optional<std::string> text) {
  if (!active())
    return;
  if (!text) {
    status_->SetText(u"The bindings file must be smaller than 64 KB.");
    return;
  }
  const auto raw = base::JSONReader::ReadDict(*text, base::JSON_PARSE_RFC);
  if (!raw) {
    status_->SetText(u"The file is not a keyboard bindings file.");
    return;
  }
  const auto resolved = ResolveLegacyKeyboard(*raw);
  if (!resolved.config) {
    status_->SetText(base::UTF8ToUTF16(resolved.error));
    return;
  }
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? resolved.config->normal : resolved.config->shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(resolved.config->tabs);
  tabs_->SetText(tab_modifiers_);
  Refresh();
  status_->SetText(u"Bindings imported. Review them, then save to apply.");
}

int KeyboardEditor::selected_pane_for_testing() const {
  return sections_ ? static_cast<int>(sections_->GetSelectedTabIndex()) : -1;
}

bool KeyboardEditor::setting_visible_for_testing(
    std::u16string_view label) const {
  const auto needle = base::ToLowerASCII(std::u16string(label));
  for (const auto& item : items_)
    if (item.text.starts_with(needle))
      return item.view->GetVisible() && item.group->GetVisible();
  return false;
}
}  // namespace fly
