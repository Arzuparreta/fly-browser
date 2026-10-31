// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/keyboard_editor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/version_info/version_info.h"
#include "cc/paint/paint_flags.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/spellchecker/spellcheck_factory.h"
#include "chrome/browser/spellchecker/spellcheck_service.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "fly/fly_version.h"
#include "fly/preferences/spellcheck_languages.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/fly_extensions.h"
#include "fly/ui/fly_menu.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/native_commands.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/shell_dialogs/select_file_policy.h"
#include "ui/shell_dialogs/selected_file_info.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/view_utils.h"
#include "ui/views/widget/widget.h"

namespace fly {
namespace {
int S(int pixels) {
  return FlyScaled(pixels);
}

// The search engines Preferences offers.
constexpr std::array<std::string_view, 5> kSearchEngines = {
    "https://duckduckgo.com/?q=%s", "https://www.google.com/search?q=%s",
    "https://www.startpage.com/sp/search?query=%s",
    "https://search.brave.com/search?q=%s", "https://kagi.com/search?q=%s"};
constexpr auto kEmailHandlers = std::to_array<std::pair<std::string_view, std::u16string_view>>({
    {"system", u"System mail app"},
    {"https://mail.zoho.eu/zm/comp.do?originalMode=compose&app=6&subject=%s&body=%b",
     u"Zoho Mail (EU)"},
    {"https://mail.google.com/mail/u/0/#inbox?compose=new&subject=%s&body=%b",
     u"Gmail"},
    {"https://outlook.live.com/mail/0/deeplink/compose?subject=%s&body=%b",
     u"Outlook"},
    {"https://mail.proton.me/u/0/compose?subject=%s&body=%b", u"Proton Mail"},
    {"https://app.fastmail.com/mail/compose?subject=%s&body=%b", u"Fastmail"}});
constexpr auto kBuiltinAliases = std::to_array<std::pair<std::u16string_view, std::u16string_view>>({
        {u"!w", u"Wikipedia"},       {u"!gh", u"GitHub"},
        {u"!yt", u"YouTube"},        {u"!a", u"Arch Wiki"},
        {u"!aur", u"AUR"},           {u"!so", u"Stack Overflow"},
        {u"!bangs", u"DuckDuckGo bangs"}, {u"g", u"Google"},
        {u"ddg", u"DuckDuckGo"},     {u"pkg", u"Arch packages"},
        {u"r", u"Reddit"},           {u"b", u"Brave Search"},
        {u"k", u"Kagi"},             {u"sp", u"Startpage"}});
constexpr std::array<int, 5> kSuspendMinutes = {0, 5, 15, 30, 60};
constexpr std::array<std::string_view, 3> kClockFormats = {"datetime", "time",
                                                           "seconds"};
constexpr std::array<std::u16string_view, KeyboardEditor::kPaneCount>
    kPaneTitles = {u"General",  u"Appearance",  u"Keyboard", u"Privacy",
                   u"Extensions", u"Performance", u"About"};

// The More menu of the Keyboard section.
enum MoreCommand {
  kRestoreDefaults,
  kRestoreShortcuts,
  kRestoreNormal,
  kImportBindings,
  kExportBindings,
};
// The Spelling section's language menu: this plus the index of the language.
constexpr int kSpellingLanguageCommand = 1000;

std::vector<std::u16string> SplitBindings(std::u16string_view text) {
  return base::SplitString(text, u"\n", base::TRIM_WHITESPACE,
                           base::SPLIT_WANT_NONEMPTY);
}

std::unique_ptr<FlyLabel> Text(std::u16string text,
                               int size = kMenuTextSize,
                               FlyLabel::Tone tone = FlyLabel::Tone::kText) {
  auto label = std::make_unique<FlyLabel>(std::move(text), FlyFace::kSans,
                                          size, gfx::Font::Weight::NORMAL,
                                          tone);
  label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  return label;
}
}  // namespace

// A section in the rail: the current one is filled with an accent bar, like
// the selected row of every menu.
class KeyboardEditor::RailItem : public FlyButton {
  METADATA_HEADER(RailItem, FlyButton)

 public:
  RailItem(PressedCallback callback, std::u16string title)
      : FlyButton(std::move(callback), title, FlyFace::kSans, kMenuTextSize) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetCustomPadding(gfx::Insets::VH(0, S(14)));
    SetMinSize(gfx::Size(0, S(32)));
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetRole(ax::mojom::Role::kTab);
  }
  void SetCurrent(bool current) {
    if (current_ == current)
      return;
    current_ = current;
    SetFlyFont(FlyFace::kSans, kMenuTextSize,
               current ? gfx::Font::Weight::SEMIBOLD
                       : gfx::Font::Weight::NORMAL);
    GetViewAccessibility().SetIsSelected(current);
    // Only the current section takes focus from Tab; arrows move between them.
    SetFocusBehavior(current ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
    UpdateColors();
  }
  // Matches in this section while searching; -1 hides the count.
  void SetCount(int count) {
    count_ = count;
    UpdateColors();
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (current_) {
      flags.setColor(SkColorSetA(colors.text, 22));
      canvas->DrawRoundRect(bounds, S(6), flags);
      flags.setColor(colors.accent);
      canvas->DrawRoundRect(
          gfx::RectF(0, S(8), S(3), std::max(0, height() - S(16))), S(2),
          flags);
    } else if (hot) {
      flags.setColor(SkColorSetA(colors.text, 12));
      canvas->DrawRoundRect(bounds, S(6), flags);
    }
    if (HasFocus()) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(1);
      flags.setColor(colors.accent);
      bounds.Inset(0.5f);
      canvas->DrawRoundRect(bounds, S(6), flags);
    }
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    if (count_ < 0)
      return;
    const auto font = FlyFont(FlyFace::kMono, kMenuCaptionSize);
    canvas->DrawStringRectWithFlags(
        count_ ? base::NumberToString16(count_) : u"–", font,
        GetFlyColors().dim, gfx::Rect(0, 0, width() - S(12), height()),
        gfx::Canvas::TEXT_ALIGN_RIGHT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
  }

 protected:
  void UpdateColors() override {
    FlyButton::UpdateColors();
    const auto& colors = GetFlyColors();
    const SkColor rest = current_ ? colors.text : colors.dim;
    SetTextColor(STATE_NORMAL, count_ == 0 ? colors.border : rest);
  }

 private:
  bool current_ = false;
  int count_ = -1;
};
BEGIN_METADATA(KeyboardEditor, RailItem)
END_METADATA

// One cell of the shortcut table: the bindings of one action in one scope.
class KeyboardEditor::KeyCell : public views::Button {
  METADATA_HEADER(KeyCell, views::Button)

 public:
  KeyCell(KeyboardEditor* editor, Key key, std::u16string action_title)
      : views::Button(base::BindRepeating(&KeyboardEditor::CellPressed,
                                          base::Unretained(editor), key)),
        editor_(editor),
        key_(std::move(key)),
        action_title_(std::move(action_title)) {
    SetInstallFocusRingOnFocus(false);
    SetFocusBehavior(FocusBehavior::NEVER);
    GetViewAccessibility().SetRole(ax::mojom::Role::kGridCell);
    UpdateName();
  }
  const Key& key() const { return key_; }
  void SetSelected(bool selected) {
    if (selected_ == selected)
      return;
    selected_ = selected;
    GetViewAccessibility().SetIsSelected(selected);
    SetFocusBehavior(selected ? FocusBehavior::ALWAYS : FocusBehavior::NEVER);
    SchedulePaint();
  }
  void UpdateName() {
    const auto bindings = SplitBindings(editor_->DraftText(key_));
    GetViewAccessibility().SetName(
        action_title_ + (key_.second ? u", NORMAL key: " : u", shortcut: ") +
        (bindings.empty() ? u"none" : base::JoinString(bindings, u", ")));
    SchedulePaint();
  }
  void OnFocus() override {
    views::Button::OnFocus();
    SchedulePaint();
  }
  void OnBlur() override {
    views::Button::OnBlur();
    SchedulePaint();
  }
  void StateChanged(ButtonState old_state) override {
    views::Button::StateChanged(old_state);
    SchedulePaint();
  }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool recording = editor_->recording_ == key_;
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    gfx::RectF box(GetLocalBounds());
    box.Inset(gfx::InsetsF::VH(S(3), 0));
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (selected_ || hot) {
      flags.setColor(SkColorSetA(colors.text, selected_ ? 22 : 12));
      canvas->DrawRoundRect(box, S(5), flags);
    }
    if (recording || HasFocus()) {
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      flags.setStrokeWidth(recording ? 1.5f : 1.f);
      flags.setColor(colors.accent);
      auto outline = box;
      outline.Inset(0.75f);
      canvas->DrawRoundRect(outline, S(5), flags);
    }
    const int centre = height() / 2;
    const auto font = FlyFont(FlyFace::kSans, kMenuSmallTextSize);
    if (recording) {
      canvas->DrawStringRectWithFlags(
          editor_->chord_.empty()
              ? u"Press keys…"
              : base::UTF8ToUTF16(editor_->chord_) + u" …",
          font, colors.accent, gfx::Rect(S(10), 0, width() - S(20), height()),
          gfx::Canvas::NO_SUBPIXEL_RENDERING);
      return;
    }
    const auto bindings = SplitBindings(editor_->DraftText(key_));
    if (bindings.empty()) {
      canvas->DrawStringRectWithFlags(
          u"—", font, colors.border,
          gfx::Rect(S(10), 0, width() - S(20), height()),
          gfx::Canvas::NO_SUBPIXEL_RENDERING);
      return;
    }
    PaintKeyChipsLeftAligned(canvas, S(8), width() - S(8), centre, bindings,
                             editor_->IsCustomised(key_)
                                 ? ChipTone::kAccent
                                 : (key_.second ? ChipTone::kQuiet
                                                : ChipTone::kNormal));
  }

 private:
  const raw_ptr<KeyboardEditor> editor_;
  const Key key_;
  const std::u16string action_title_;
  bool selected_ = false;
};
BEGIN_METADATA(KeyboardEditor, KeyCell)
END_METADATA

// One action of the shortcut table: its title, its shortcut cell and its
// NORMAL key cell.
class KeyboardEditor::KeyRow : public views::View {
  METADATA_HEADER(KeyRow, views::View)

 public:
  KeyRow(KeyboardEditor* editor, const PaletteEntry& entry)
      : title_(base::UTF8ToUTF16(entry.title)),
        category_(base::UTF8ToUTF16(entry.category)) {
    global_ = AddChildView(std::make_unique<KeyCell>(
        editor, Key{entry.action, false}, title_));
    normal_ = AddChildView(std::make_unique<KeyCell>(
        editor, Key{entry.action, true}, title_));
    GetViewAccessibility().SetRole(ax::mojom::Role::kRow);
    GetViewAccessibility().SetName(title_);
    SetTooltipText(title_ + (category_.empty() ? u"" : u" · " + category_));
  }
  KeyCell* global() { return global_; }
  KeyCell* normal() { return normal_; }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(S(400), S(34));
  }
  // Columns: action 40%, shortcut and NORMAL key 30% each.
  static int TitleWidth(int width) { return width * 2 / 5; }
  void Layout(PassKey) override {
    const int title = TitleWidth(width());
    const int cell = (width() - title) / 2;
    global_->SetBounds(title, 0, cell, height());
    normal_->SetBounds(title + cell, 0, width() - title - cell, height());
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const auto font = FlyFont(FlyFace::kSans, kMenuTextSize);
    const int title = TitleWidth(width()) - S(16);
    canvas->DrawStringRectWithFlags(
        gfx::ElideText(title_, font, title, gfx::ELIDE_TAIL), font,
        colors.text, gfx::Rect(S(10), 0, title, height()),
        gfx::Canvas::NO_SUBPIXEL_RENDERING);
    canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1),
                     SkColorSetA(colors.border, 160));
  }

 private:
  const std::u16string title_;
  const std::u16string category_;
  raw_ptr<KeyCell> global_ = nullptr;
  raw_ptr<KeyCell> normal_ = nullptr;
};
BEGIN_METADATA(KeyboardEditor, KeyRow)
END_METADATA

namespace {
// The table's column titles, aligned with KeyRow.
class KeyTableHeader : public views::View {
  METADATA_HEADER(KeyTableHeader, views::View)

 public:
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(S(400), S(26));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const auto font = FlyFont(FlyFace::kMono, kMenuCaptionSize,
                              gfx::Font::Weight::MEDIUM);
    const int title = width() * 2 / 5;
    const int cell = (width() - title) / 2;
    const int flags = gfx::Canvas::NO_SUBPIXEL_RENDERING;
    canvas->DrawStringRectWithFlags(u"ACTION", font, colors.dim,
                                    gfx::Rect(S(10), 0, title, height()),
                                    flags);
    canvas->DrawStringRectWithFlags(u"SHORTCUT", font, colors.dim,
                                    gfx::Rect(title + S(10), 0, cell, height()),
                                    flags);
    canvas->DrawStringRectWithFlags(
        u"NORMAL MODE", font, colors.dim,
        gfx::Rect(title + cell + S(10), 0, cell, height()), flags);
    canvas->FillRect(gfx::Rect(0, height() - 1, width(), 1), colors.border);
  }
};

// A small colour sample next to a colour field.
class Swatch : public views::View {
  METADATA_HEADER(Swatch, views::View)

 public:
  void SetColor(SkColor color) {
    color_ = color;
    SchedulePaint();
  }
  gfx::Size CalculatePreferredSize(const views::SizeBounds&) const override {
    return gfx::Size(S(22), S(22));
  }
  void OnPaint(gfx::Canvas* canvas) override {
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(color_);
    gfx::RectF bounds(GetLocalBounds());
    canvas->DrawRoundRect(bounds, S(5), flags);
    flags.setStyle(cc::PaintFlags::kStroke_Style);
    flags.setStrokeWidth(1);
    flags.setColor(GetFlyColors().border);
    bounds.Inset(0.5f);
    canvas->DrawRoundRect(bounds, S(5), flags);
  }

 private:
  SkColor color_ = SK_ColorTRANSPARENT;
};
BEGIN_METADATA(KeyTableHeader)
END_METADATA
BEGIN_METADATA(Swatch)
END_METADATA
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
  if (more_runner_)
    more_runner_->Cancel();
  if (spelling_runner_)
    spelling_runner_->Cancel();
  ClearContent();
  rail_.clear();
  settings_search_ = nullptr;
  content_host_ = nullptr;
  status_ = nullptr;
  hints_ = nullptr;
  root_ = nullptr;
  source_tab_.clear();
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}

void KeyboardEditor::Toggle(content::WebContents* contents) {
  if (overlay_) {
    RequestClose();
    return;
  }
  if (!desktop_)
    return;
  restore_page_focus_ = ShouldRestoreNativePageFocus(contents);
  Observe(contents);
  source_tab_ = WatchMenuSourceTab(
      contents, base::BindRepeating(&KeyboardEditor::Cancel,
                                    weak_factory_.GetWeakPtr(), false));
  if (contents)
    contents->StoreFocus();
  const auto current = commands_->Read();
  keyboard_readable_ = current.has_value();
  original_ = current.value_or(DefaultKeyboardConfig());
  generation_ = commands_->generation();
  catalog_ = PaletteCatalog(original_);
  std::set<std::string> actions;
  std::erase_if(catalog_, [&](const PaletteEntry& entry) {
    return !actions.insert(entry.action).second;
  });
  close_bindings_ = original_.shortcuts.at("toggle-help");
  draft_.clear();
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  closing_ = cancelled_ = false;
  confirm_discard_ = false;
  query_.clear();
  keyboard_query_.clear();
  modified_only_checked_ = false;
  selected_cell_.reset();
  status_text_.clear();

  const gfx::Rect bounds = MenuWidgetBounds(desktop_);
  overlay_ = CreateMenuWidget(desktop_, bounds, "FlyKeyboardPreferences");
  const auto& colors = GetFlyColors();
  auto root = std::make_unique<MenuScrim>(
      SkColorSetA(colors.dark ? SK_ColorBLACK : colors.text,
                  colors.dark ? 120 : 60),
      base::BindRepeating(&KeyboardEditor::RequestClose,
                          weak_factory_.GetWeakPtr()));
  auto panel = std::make_unique<MenuPanel>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* header = panel->AddChildView(
      std::make_unique<MenuHeader>(MenuKind::kPreferences, u"Preferences"));
  header->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(S(10), S(12))));
  settings_search_ = header->SetSearch(
      std::make_unique<FlyTextfield>(u"Search settings"));
  settings_search_->set_controller(this);
  header->AddClose(base::BindRepeating(&KeyboardEditor::RequestClose,
                                       weak_factory_.GetWeakPtr()));
  auto* body = panel->AddChildView(std::make_unique<views::View>());
  body->SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 1, 0)));
  auto* body_layout = body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 0));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  layout->SetFlexForView(body, 1);
  auto* rail = body->AddChildView(std::make_unique<views::View>());
  rail->SetPreferredSize(gfx::Size(S(176), 0));
  rail->SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(0, 0, 0, 1),
                                gfx::Insets::VH(S(10), S(8))));
  auto* rail_layout = rail->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(2)));
  rail_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  rail->GetViewAccessibility().SetRole(ax::mojom::Role::kTabList);
  rail->GetViewAccessibility().SetName(u"Sections");
  rail_.clear();
  for (int pane = 0; pane < kPaneCount; ++pane) {
    rail_.push_back(rail->AddChildView(std::make_unique<RailItem>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, int pane) {
              if (!editor || !editor->active())
                return;
              if (!editor->query_.empty()) {
                editor->query_.clear();
                editor->settings_search_->SetText(u"");
              }
              editor->SelectPane(pane, true);
            },
            weak_factory_.GetWeakPtr(), pane),
        std::u16string(kPaneTitles[pane]))));
  }
  content_host_ = body->AddChildView(std::make_unique<views::View>());
  content_host_->SetUseDefaultFillLayout(true);
  body_layout->SetFlexForView(content_host_, 1);
  auto* footer = panel->AddChildView(std::make_unique<views::View>());
  auto* footer_layout =
      footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::VH(S(8), S(14)), S(12)));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  hints_ = footer->AddChildView(
      std::make_unique<KeyHints>(std::vector<KeyHints::Hint>()));
  status_ = footer->AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kSans, kMenuSmallTextSize, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kDim));
  status_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  status_->SetElideBehavior(gfx::ELIDE_TAIL);
  status_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  footer_layout->SetFlexForView(status_, 1);
  root->SetPanel(std::move(panel));
  root_ = root.get();
  PlacePanel(bounds);
  overlay_->SetContentsView(std::move(root));
  if (!keyboard_readable_)
    status_text_ =
        u"Keyboard settings need recovery. They have been preserved.";
  // Preferences reopens the section shown last in this session.
  SelectPane(pane_, false);
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  if (pane_ == kKeyboardPane && search_)
    search_->RequestFocus();
  else
    settings_search_->RequestFocus();
}

// A compact panel: the content keeps a readable width on wide windows.
void KeyboardEditor::PlacePanel(const gfx::Rect& bounds) {
  const int width = std::min(S(960), bounds.width() - S(32));
  const int height = std::min(S(720), bounds.height() - S(32));
  root_->SetPanelBounds(gfx::Rect((bounds.width() - width) / 2,
                                  (bounds.height() - height) / 2, width,
                                  height));
}

void KeyboardEditor::SelectPane(int pane, bool focus_rail) {
  if (!active() || pane < 0 || pane >= kPaneCount)
    return;
  if (pane != kKeyboardPane)
    StopRecording();
  pane_ = pane;
  for (int i = 0; i < kPaneCount; ++i)
    rail_[i]->SetCurrent(i == pane_);
  ShowContent();
  if (focus_rail)
    rail_[pane_]->RequestFocus();
}

void KeyboardEditor::ShowPane(Pane pane) {
  if (!active())
    return;
  if (!query_.empty()) {
    query_.clear();
    settings_search_->SetText(u"");
  }
  SelectPane(pane, false);
}

void KeyboardEditor::ClearContent() {
  search_settings_.clear();
  search_save_ = nullptr;
  alias_save_ = nullptr;
  alias_remove_.clear();
  aliases_box_ = nullptr;
  appearance_.clear();
  engine_choice_ = nullptr;
  email_choice_ = nullptr;
  email_custom_row_ = nullptr;
  email_handler_ = nullptr;
  StopObservingDictionaries();
  spelling_list_ = nullptr;
  spelling_add_ = nullptr;
  spelling_buttons_.clear();
  spelling_names_.clear();
  spelling_shown_.clear();
  scale_choice_ = nullptr;
  clock_format_ = nullptr;
  suspension_timeout_ = nullptr;
  process_limit_ = nullptr;
  private_mode_ = nullptr;
  color_fields_.clear();
  color_swatches_.clear();
  opacity_sliders_.clear();
  opacity_labels_.clear();
  update_label_ = nullptr;
  update_action_ = nullptr;
  desktop_entry_label_ = nullptr;
  desktop_entry_button_ = nullptr;
  default_browser_label_ = nullptr;
  default_browser_button_ = nullptr;
  extensions_list_ = nullptr;
  extensions_enabled_ = nullptr;
  extensions_usage_ = nullptr;
  extensions_store_link_ = nullptr;
  search_ = nullptr;
  tabs_ = nullptr;
  modified_only_ = nullptr;
  rows_scroll_ = nullptr;
  key_rows_ = nullptr;
  cells_.clear();
  row_actions_.clear();
  cell_title_ = nullptr;
  record_ = nullptr;
  clear_ = nullptr;
  default_ = nullptr;
  save_ = nullptr;
  discard_ = nullptr;
  more_ = nullptr;
  rows_.clear();
  building_parent_ = nullptr;
  group_ = nullptr;
  if (content_host_)
    content_host_->RemoveAllChildViews();
}

// Builds the shown section, or every matching row while searching. Only this
// content exists: the rest of Preferences costs nothing to lay out.
void KeyboardEditor::ShowContent() {
  if (!content_host_)
    return;
  ClearContent();
  matches_per_pane_.assign(kPaneCount, 0);
  if (query_.empty() && pane_ == kKeyboardPane) {
    auto keyboard = std::make_unique<views::View>();
    building_parent_ = keyboard.get();
    BuildKeyboard();
    building_parent_ = nullptr;
    DisableSubpixelText(keyboard.get());
    content_host_->AddChildView(std::move(keyboard));
  } else {
    auto scroll = std::make_unique<views::ScrollView>();
    scroll->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    scroll->SetBackgroundColor(std::nullopt);
    scroll->SetDrawOverflowIndicator(false);
    // A bounded viewport gives the BoxLayout contents real bounds.
    scroll->ClipHeightTo(0, std::numeric_limits<int>::max());
    auto contents = std::make_unique<views::View>();
    auto* layout = contents->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical,
        gfx::Insets::TLBR(S(14), S(20), S(20), S(20)), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    building_parent_ = contents.get();
    const auto build = [&](int pane) {
      building_pane_ = pane;
      group_ = nullptr;
      pending_group_.clear();
      section_heading_shown_ = false;
      switch (pane) {
        case kGeneralPane:
          BuildGeneral();
          break;
        case kAppearancePane:
          BuildAppearance();
          break;
        case kKeyboardPane:
          // Searching lists the Keyboard section as one row.
          BeginGroup(u"Keyboard");
          AddRow(u"Keyboard shortcuts",
                 u"Shortcuts and NORMAL mode keys for every action.",
                 u"keys keybinds bindings hotkeys vim normal mode record",
                 MakeMenuButton(base::BindRepeating(
                                    &KeyboardEditor::ShowPane,
                                    weak_factory_.GetWeakPtr(), kKeyboardPane),
                                u"Edit shortcuts"));
          break;
        case kPrivacyPane:
          BuildPrivacy();
          break;
        case kExtensionsPane:
          BuildExtensions();
          break;
        case kPerformancePane:
          BuildPerformance();
          break;
        case kAboutPane:
          BuildAbout();
          break;
      }
    };
    if (query_.empty()) {
      auto* title = contents->AddChildView(std::make_unique<FlyLabel>(
          std::u16string(kPaneTitles[pane_]), FlyFace::kSans, 18,
          gfx::Font::Weight::SEMIBOLD));
      title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      title->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, 0, S(4), 0)));
      build(pane_);
    } else {
      for (int pane = 0; pane < kPaneCount; ++pane)
        build(pane);
      if (rows_.empty())
        contents->AddChildView(Text(u"No settings match “" + query_ + u"”.",
                                    kMenuTextSize, FlyLabel::Tone::kDim));
    }
    building_parent_ = nullptr;
    group_ = nullptr;
    DisableSubpixelText(contents.get());
    scroll->SetContents(std::move(contents));
    content_host_->AddChildView(std::move(scroll));
  }
  for (int pane = 0; pane < kPaneCount; ++pane)
    rail_[pane]->SetCount(query_.empty() ? -1 : matches_per_pane_[pane]);
  RefreshAppearance();
  if (pane_ == kKeyboardPane && query_.empty())
    RefreshKeyboardRows();
  if (hints_) {
    if (pane_ == kKeyboardPane && query_.empty())
      hints_->SetHints({{u"↑↓←→", u"Move"},
                        {u"Enter", u"Record"},
                        {u"Del", u"Clear"},
                        {u"Ctrl+S", u"Save"},
                        {u"Esc", u"Close"}});
    else
      hints_->SetHints({{u"Tab", u"Next"},
                        {u"Ctrl+PgDn", u"Next section"},
                        {u"Esc", u"Close"}});
  }
  SetStatus(status_text_);
  content_host_->InvalidateLayout();
}

bool KeyboardEditor::Matches(std::u16string_view text) const {
  if (query_.empty())
    return true;
  const auto haystack = base::ToLowerASCII(text);
  for (const auto& term :
       base::SplitString(base::ToLowerASCII(query_), u" \t",
                         base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
    if (haystack.find(term) == std::u16string::npos)
      return false;
  return true;
}

// Groups appear with their first row, so searches never show empty ones.
void KeyboardEditor::BeginGroup(std::u16string caption) {
  pending_group_ = std::move(caption);
  group_ = nullptr;
}

views::View* KeyboardEditor::AddRow(std::u16string label,
                                    std::u16string description,
                                    std::u16string keywords,
                                    std::unique_ptr<views::View> control,
                                    bool wide) {
  const std::u16string text = label + u" " + description + u" " + keywords +
                              u" " + pending_group_ + u" " +
                              std::u16string(kPaneTitles[building_pane_]);
  if (!building_parent_ || !Matches(text)) {
    // Switches registered by MakeToggle go away with their unmatched row.
    if (control)
      std::erase_if(appearance_, [&](const auto& entry) {
        return control->Contains(entry.second.get());
      });
    return nullptr;
  }
  if (!query_.empty() && !section_heading_shown_) {
    section_heading_shown_ = true;
    auto* heading = building_parent_->AddChildView(std::make_unique<FlyLabel>(
        std::u16string(kPaneTitles[building_pane_]), FlyFace::kSans, 15,
        gfx::Font::Weight::SEMIBOLD));
    heading->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    heading->SetBorder(views::CreateEmptyBorder(
        gfx::Insets::TLBR(rows_.empty() ? 0 : S(14), 0, S(2), 0)));
  }
  if (!group_) {
    auto* caption = building_parent_->AddChildView(
        MakeMenuCaption(base::ToUpperASCII(pending_group_)));
    caption->SetBorder(
        views::CreateEmptyBorder(gfx::Insets::TLBR(S(14), S(2), S(6), 0)));
    group_ = building_parent_->AddChildView(std::make_unique<views::View>());
    group_->SetBackground(FlyBackground(FlyRole::kOverlayMain, S(8)));
    group_->SetBorder(FlyBorder(FlyRole::kBorder, 1, S(8)));
    auto* layout = group_->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
  }
  auto* row = group_->AddChildView(std::make_unique<views::View>());
  const bool first = group_->children().size() == 1;
  row->SetBorder(FlyEdgeBorder(FlyRole::kBorder,
                               gfx::Insets::TLBR(first ? 0 : 1, 0, 0, 0),
                               gfx::Insets::VH(S(8), S(14))));
  auto* layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(16)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  layout->set_minimum_cross_axis_size(S(kMenuControlHeight + 6));
  // A row without a label gives its control the whole width.
  views::View* info = nullptr;
  if (!label.empty()) {
    info = row->AddChildView(std::make_unique<views::View>());
    auto* info_layout =
        info->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(1)));
    info_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* name = info->AddChildView(Text(label));
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    if (!description.empty()) {
      auto* detail = info->AddChildView(
          Text(description, kMenuSmallTextSize, FlyLabel::Tone::kDim));
      detail->SetElideBehavior(gfx::ELIDE_TAIL);
      detail->SetTooltipText(description);
    }
  }
  if (control) {
    auto* added = row->AddChildView(std::move(control));
    if (wide || !info) {
      layout->SetFlexForView(added, 1);
      // Text fields of a wide row take its spare width.
      if (auto* box = views::AsViewClass<views::View>(added);
          box && box->GetLayoutManager())
        for (views::View* child : box->children())
          if (views::IsViewClass<views::Textfield>(child))
            static_cast<views::BoxLayout*>(box->GetLayoutManager())
                ->SetFlexForView(child, 1);
    } else {
      layout->SetFlexForView(info, 1);
    }
    // Unnamed controls take the row's name.
    auto& accessibility = added->GetViewAccessibility();
    if (accessibility.GetCachedName().empty() && !label.empty()) {
      if (accessibility.GetCachedRole() == ax::mojom::Role::kNone ||
          accessibility.GetCachedRole() == ax::mojom::Role::kUnknown)
        accessibility.SetRole(ax::mojom::Role::kGroup);
      accessibility.SetName(label);
    }
  } else if (info) {
    layout->SetFlexForView(info, 1);
  }
  if (wide && info)
    info->SetPreferredSize(gfx::Size(S(150), info->GetPreferredSize().height()));
  rows_.push_back({building_pane_, base::ToLowerASCII(label), row});
  ++matches_per_pane_[building_pane_];
  return row;
}

// Text that belongs to a group but is not a setting.
views::View* KeyboardEditor::AddNote(std::u16string text) {
  if (!group_)
    return nullptr;
  auto* note = group_->AddChildView(
      Text(std::move(text), kMenuSmallTextSize, FlyLabel::Tone::kDim));
  note->SetMultiLine(true);
  note->SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0),
                                gfx::Insets::VH(S(8), S(14))));
  return note;
}

std::unique_ptr<views::View> KeyboardEditor::Controls(
    std::vector<std::unique_ptr<views::View>> views) {
  auto box = std::make_unique<views::View>();
  auto* layout = box->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(8)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  for (auto& view : views) {
    if (!view)
      continue;
    box->AddChildView(std::move(view));
  }
  return box;
}

std::unique_ptr<FlyToggle> KeyboardEditor::MakeToggle(std::string key,
                                                      std::u16string name) {
  auto toggle = std::make_unique<FlyToggle>(
      base::BindRepeating(&KeyboardEditor::ChangeAppearance,
                          weak_factory_.GetWeakPtr(), key),
      std::move(name));
  appearance_[key] = toggle.get();
  return toggle;
}

// General: search, sharing and address bar aliases.
void KeyboardEditor::BuildGeneral() {
  const auto settings = search_preferences_.Read();
  BeginGroup(u"Search");
  {
    auto select = std::make_unique<FlySelect>(
        std::vector<std::u16string>{u"DuckDuckGo", u"Google", u"Startpage",
                                    u"Brave Search", u"Kagi", u"Custom"},
        base::BindRepeating(&KeyboardEditor::ChooseEngine,
                            weak_factory_.GetWeakPtr()),
        u"Search engine");
    auto* choice = select.get();
    if (AddRow(u"Search engine", u"", u"default address bar google kagi brave",
               std::move(select)))
      engine_choice_ = choice;
  }
  {
    auto field = std::make_unique<FlyTextfield>(
        u"Search URL", u"https://example.com/search?q=%s");
    field->set_controller(this);
    field->SetPreferredSize(gfx::Size(S(300), S(kMenuControlHeight)));
    if (settings)
      field->SetText(base::UTF8ToUTF16(settings->engine));
    auto save = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                            weak_factory_.GetWeakPtr(), "engine"),
        u"Save");
    save->GetViewAccessibility().SetName(u"Save search URL");
    auto* field_ptr = field.get();
    auto* save_ptr = save.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(field));
    controls.push_back(std::move(save));
    if (AddRow(u"Search URL", u"%s is replaced by what you type.",
               u"engine custom", Controls(std::move(controls)))) {
      search_settings_["engine"] = field_ptr;
      search_save_ = save_ptr;
    }
  }
  BuildSpelling();
  BeginGroup(u"Sharing");
  {
    std::vector<std::u16string> handlers;
    for (const auto& [value, label] : kEmailHandlers)
      handlers.emplace_back(label);
    handlers.emplace_back(u"Custom");
    auto select = std::make_unique<FlySelect>(
        std::move(handlers),
        base::BindRepeating(&KeyboardEditor::ChooseEmailHandler,
                            weak_factory_.GetWeakPtr()),
        u"Email links with");
    auto* choice = select.get();
    if (AddRow(u"Email links with", u"", u"mail mailto share webmail gmail",
               std::move(select)))
      email_choice_ = choice;
  }
  {
    auto field = std::make_unique<FlyTextfield>(
        u"Compose URL", u"https://mail.example.com/compose?subject=%s&body=%b");
    field->set_controller(this);
    field->SetPreferredSize(gfx::Size(S(300), S(kMenuControlHeight)));
    auto* field_ptr = field.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(field));
    controls.push_back(MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::ApplyProductTexts,
                            weak_factory_.GetWeakPtr()),
        u"Save"));
    if (auto* row = AddRow(u"Compose URL", u"%s is the subject, %b the link.",
                           u"mail email custom", Controls(std::move(controls)))) {
      email_handler_ = field_ptr;
      email_custom_row_ = row;
    }
  }
  BuildAliases();
}

void KeyboardEditor::BuildAliases() {
  BeginGroup(u"Search aliases");
  {
    auto list = std::make_unique<views::View>();
    auto* layout = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(4)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    // The group's caption names the list, so the row has no label.
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    list->GetViewAccessibility().SetName(u"Your aliases");
    auto* list_ptr = list.get();
    if (AddRow(u"", u"", u"your aliases alias shortcut keyword",
               std::move(list), true))
      aliases_box_ = list_ptr;
  }
  {
    std::vector<std::unique_ptr<views::View>> controls;
    std::map<std::string, views::Textfield*> fields;
    for (const auto& [key, label, placeholder, width] :
         {std::tuple{"alias", u"Alias", u"gh", 64},
          std::tuple{"name", u"Name", u"GitHub", 130},
          std::tuple{"url", u"Search URL", u"https://github.com/search?q=%s",
                     0}}) {
      auto field = std::make_unique<FlyTextfield>(label, placeholder);
      field->set_controller(this);
      if (width)
        field->SetPreferredSize(gfx::Size(S(width), S(kMenuControlHeight)));
      fields[key] = field.get();
      controls.push_back(std::move(field));
    }
    auto add = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::SaveSearchSetting,
                            weak_factory_.GetWeakPtr(), "alias"),
        u"Add");
    add->GetViewAccessibility().SetName(u"Add alias");
    auto* add_ptr = add.get();
    controls.push_back(std::move(add));
    if (AddRow(u"Add alias", u"Type the alias, a space and your search.",
               u"new custom", Controls(std::move(controls)), true)) {
      for (const auto& [key, field] : fields)
        search_settings_[key] = field;
      alias_save_ = add_ptr;
    }
  }
  std::vector<std::u16string> keys;
  std::u16string names;
  for (const auto& [key, name] : kBuiltinAliases) {
    keys.emplace_back(key);
    names += std::u16string(key) + u"  " + std::u16string(name) + u"\n";
  }
  auto builtin = Text(base::JoinString(keys, u"  "), kMenuSmallTextSize,
                      FlyLabel::Tone::kDim);
  builtin->SetFontList(FlyFont(FlyFace::kMono, kMenuSmallTextSize));
  builtin->SetElideBehavior(gfx::ELIDE_TAIL);
  builtin->SetTooltipText(names);
  AddRow(u"Built-in", u"", u"aliases bangs wikipedia github youtube",
         std::move(builtin), true);
}

// Appearance: theme, colours and the parts of the window.
void KeyboardEditor::BuildAppearance() {
  BeginGroup(u"Theme");
  AddRow(u"Dark interface", u"", u"theme light mode colour scheme",
         MakeToggle("theme", u"Dark interface"));
  AddRow(u"Match websites to the theme",
         u"Asks websites for their dark or light look.",
         u"dark mode web pages sync",
         MakeToggle("sync_website_theme", u"Match websites to the theme"));
  {
    auto select = std::make_unique<FlySelect>(
        std::vector<std::u16string>{u"Normal", u"Large (125%)"},
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, size_t index) {
              if (editor && editor->active())
                editor->preferences_.SetString(
                    "interface_scale", index == 1 ? "large" : "normal");
            },
            weak_factory_.GetWeakPtr()),
        u"Interface scale");
    auto* choice = select.get();
    if (AddRow(u"Interface scale", u"Web pages keep their own zoom.",
               u"size large zoom text bigger", std::move(select)))
      scale_choice_ = choice;
  }
  BeginGroup(u"Colours");
  bool first = true;
  for (const auto& [key, label] :
       {std::pair{"accent", u"Accent"}, std::pair{"bg_sidebar", u"Sidebar"},
        std::pair{"bg_main", u"Canvas"}, std::pair{"text", u"Text"}}) {
    auto swatch = std::make_unique<Swatch>();
    auto field = std::make_unique<FlyTextfield>(
        std::u16string(label) + u" colour", u"#rrggbb");
    field->set_controller(this);
    field->SetPreferredSize(gfx::Size(S(96), S(kMenuControlHeight)));
    field->SetFontList(FlyFont(FlyFace::kMono, kMenuSmallTextSize));
    auto* swatch_ptr = swatch.get();
    auto* field_ptr = field.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(swatch));
    controls.push_back(std::move(field));
    if (AddRow(label,
               first ? u"Type #rrggbb and press Enter. Empty uses the theme."
                     : u"",
               u"colour color hex custom", Controls(std::move(controls)))) {
      color_swatches_[key] = swatch_ptr;
      color_fields_[key] = field_ptr;
    }
    first = false;
  }
  for (const auto& [key, label, description] :
       {std::tuple{"sidebar_opacity", u"Sidebar opacity",
                   u"How much of the page shows through the sidebar."},
        std::tuple{"overlay_opacity", u"Menu opacity",
                   u"How much of the page shows through menus."}}) {
    auto slider = std::make_unique<FlySlider>(this);
    slider->SetPreferredSize(gfx::Size(S(150), S(24)));
    slider->GetViewAccessibility().SetName(label);
    auto value = std::make_unique<FlyLabel>(u"100%", FlyFace::kMono,
                                            kMenuSmallTextSize);
    value->SetPreferredSize(gfx::Size(S(40), S(20)));
    value->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    auto* slider_ptr = slider.get();
    auto* value_ptr = value.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(slider));
    controls.push_back(std::move(value));
    if (AddRow(label, description, u"transparency translucent overlay",
               Controls(std::move(controls)))) {
      opacity_sliders_[key] = slider_ptr;
      opacity_labels_[key] = value_ptr;
    }
  }
  AddRow(u"Reset colours", u"Returns to the theme's colours and opacity.",
         u"default restore",
         MakeMenuButton(
             base::BindRepeating(
                 [](base::WeakPtr<KeyboardEditor> editor) {
                   if (editor && editor->active()) {
                     editor->preferences_.ResetThemeColors();
                     editor->SetStatus(u"Colours reset.");
                     editor->RefreshAppearance();
                   }
                 },
                 weak_factory_.GetWeakPtr()),
             u"Reset"));
  BeginGroup(u"Window");
  AddRow(u"Collapse the sidebar",
         u"Shows a rail until the pointer or keyboard needs the sidebar.",
         u"auto-collapse zen rail pin",
         MakeToggle("sidebar_auto_collapse", u"Collapse the sidebar"));
  AddRow(u"Status bar", u"Mode, links, zoom and clock at the bottom.",
         u"footer mode bar",
         MakeToggle("show_status_bar", u"Status bar"));
  AddRow(u"Window controls", u"Minimise, maximise and close in the sidebar.",
         u"buttons titlebar",
         MakeToggle("show_window_controls", u"Window controls"));
  {
    auto select = std::make_unique<FlySelect>(
        std::vector<std::u16string>{u"Date and time", u"Time",
                                    u"Time with seconds"},
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, size_t index) {
              if (editor && editor->active() && index < kClockFormats.size())
                editor->preferences_.SetClockFormat(
                    std::string(kClockFormats[index]));
            },
            weak_factory_.GetWeakPtr()),
        u"Clock format");
    auto* choice = select.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(select));
    controls.push_back(MakeToggle("show_clock", u"Clock"));
    if (AddRow(u"Clock", u"In the sidebar and the status bar.",
               u"date time seconds format", Controls(std::move(controls))))
      clock_format_ = choice;
  }
}

void KeyboardEditor::BuildPrivacy() {
  const auto action = [&](std::u16string label, std::string action) {
    return MakeMenuButton(base::BindRepeating(&KeyboardEditor::RunAction,
                                              weak_factory_.GetWeakPtr(),
                                              std::move(action)),
                          std::move(label));
  };
  BeginGroup(u"Browsing");
  {
    auto toggle = std::make_unique<FlyToggle>(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(),
                            std::string("toggle-private-mode")),
        u"Private browsing");
    auto* toggle_ptr = toggle.get();
    if (AddRow(u"Private browsing",
               u"Reopens this workspace in a window that keeps nothing.",
               u"incognito private mode", std::move(toggle)))
      private_mode_ = toggle_ptr;
  }
  AddRow(u"Website notifications", u"Lets websites ask to notify you.",
         u"permission alerts",
         MakeToggle("web_notifications", u"Website notifications"));
  BeginGroup(u"Site data");
  AddRow(u"Site cookies",
         u"Signs you out of the current site in this workspace.",
         u"cookies storage sign out clear",
         action(u"Clear site cookies", "clear-site-cookies"));
  AddRow(u"Cache", u"Deletes cached files of this workspace. Sign-ins stay.",
         u"clear cache files", action(u"Clear cache", "clear-cache"));
  BeginGroup(u"Diagnostics");
  AddRow(u"Record diagnostics",
         u"Keeps an event log in memory for bug reports. Nothing leaves this "
         u"computer.",
         u"log events debug",
         MakeToggle("enable_diagnostics", u"Record diagnostics"));
  AddRow(u"Diagnostics hub",
         u"System details, processes, the event log and a bug report.",
         u"health report bug processes memory",
         action(u"Open hub", "open-diagnostics"));
}

void KeyboardEditor::BuildExtensions() {
  const auto action = [&](std::u16string label, std::string action) {
    return MakeMenuButton(base::BindRepeating(&KeyboardEditor::RunAction,
                                              weak_factory_.GetWeakPtr(),
                                              std::move(action)),
                          std::move(label));
  };
  BeginGroup(u"Extensions");
  if (!extensions_) {
    AddRow(u"Extensions", u"Not available in this build.", u"", nullptr);
    return;
  }
  {
    auto toggle = std::make_unique<FlyToggle>(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor) {
              if (!editor || !editor->active() || !editor->extensions_enabled_)
                return;
              editor->extensions_->SetEnabled(
                  editor->extensions_enabled_->GetChecked());
              editor->RefreshExtensions();
            },
            weak_factory_.GetWeakPtr()),
        u"Use extensions");
    auto* toggle_ptr = toggle.get();
    if (AddRow(u"Use extensions",
               u"Included uBlock Origin Lite runs in every workspace unless "
               u"turned off.",
               u"enable chrome ublock adblock", std::move(toggle)))
      extensions_enabled_ = toggle_ptr;
  }
  {
    auto list = std::make_unique<views::View>();
    auto* layout = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(4)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    auto* list_ptr = list.get();
    if (AddRow(u"Installed", u"In this workspace.",
               u"remove options disable", std::move(list), true))
      extensions_list_ = list_ptr;
  }
  BeginGroup(u"Add extensions");
  AddRow(u"Chrome Web Store", u"Installing asks for confirmation.",
         u"install store", action(u"Browse Chrome Web Store", "extensions-store"));
  {
    auto field = std::make_unique<FlyTextfield>(
        u"Web Store link", u"https://chromewebstore.google.com/detail/…");
    field->set_controller(this);
    auto* field_ptr = field.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(field));
    controls.push_back(MakeMenuButton(
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor) {
              if (editor && editor->active() && editor->extensions_store_link_)
                editor->RunAction(
                    "extensions-store-link:" +
                    base::UTF16ToUTF8(
                        editor->extensions_store_link_->GetText()));
            },
            weak_factory_.GetWeakPtr()),
        u"Open"));
    if (AddRow(u"Store link", u"", u"install url detail",
               Controls(std::move(controls)), true))
      extensions_store_link_ = field_ptr;
  }
  {
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(
        action(u"Install ZIP or CRX", "extensions-install-package"));
    controls.push_back(
        action(u"Load unpacked folder", "extensions-load-unpacked"));
    AddRow(u"From a file", u"Copied into Fly's extensions folder.",
           u"install package crx zip unpacked developer",
           Controls(std::move(controls)));
  }
  AddRow(u"Chromium manager", u"", u"advanced chrome://extensions",
         action(u"Manage in chrome://extensions", "extensions-page"));
  BeginGroup(u"Resources");
  {
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(action(u"Refresh usage", "extensions-usage"));
    controls.push_back(action(u"Reload extensions", "extensions-reload"));
    controls.push_back(action(u"Open extensions folder", "extensions-open-dir"));
    if (AddRow(u"Memory and reload", u"", u"usage ram resources folder",
               Controls(std::move(controls)))) {
      if (auto* note = AddNote(
              extensions_usage_text_.empty()
                  ? u"Refresh to see the memory each extension uses in this "
                    u"workspace."
                  : extensions_usage_text_))
        extensions_usage_ = static_cast<views::Label*>(note);
    }
  }
}

void KeyboardEditor::BuildPerformance() {
  BeginGroup(u"Background tabs");
  {
    auto select = std::make_unique<FlySelect>(
        std::vector<std::u16string>{u"Off", u"After 5 minutes",
                                    u"After 15 minutes", u"After 30 minutes",
                                    u"After 1 hour"},
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, size_t index) {
              if (editor && editor->active() && index < kSuspendMinutes.size())
                editor->preferences_.SetSuspendTimeoutMinutes(
                    kSuspendMinutes[index]);
            },
            weak_factory_.GetWeakPtr()),
        u"Suspend background tabs");
    auto* choice = select.get();
    if (AddRow(u"Suspend background tabs",
               u"Pauses idle tabs without unloading them. Pinned, visible "
               u"and busy tabs keep running.",
               u"suspension sleep hibernate memory timeout",
               std::move(select)))
      suspension_timeout_ = choice;
  }
  BeginGroup(u"Page loading");
  AddRow(u"Preload the address bar's result",
         u"Loads the page an address or a history or bookmark result opens "
         u"while you choose it. Pages open faster; the site sees the visit "
         u"even if you do not open it. Searches are never preloaded.",
         u"prerender preload prefetch speed faster",
         MakeToggle("preload_address_results",
                    u"Preload the address bar's result"));
  BeginGroup(u"Engine · restart required");
  AddRow(u"Turn off GPU acceleration",
         u"Use it when the graphics driver misbehaves.",
         u"hardware graphics disable",
         MakeToggle("disable_gpu", u"Turn off GPU acceleration"));
  AddRow(u"Throttle background timers",
         u"Saves power in pages you are not looking at.",
         u"battery power throttling",
         MakeToggle("background_throttling", u"Throttle background timers"));
  {
    std::vector<std::u16string> limits;
    for (int i = 1; i <= 8; ++i)
      limits.push_back(base::NumberToString16(i));
    auto select = std::make_unique<FlySelect>(
        std::move(limits),
        base::BindRepeating(
            [](base::WeakPtr<KeyboardEditor> editor, size_t index) {
              if (editor && editor->active())
                editor->preferences_.SetInt("process_limit",
                                            static_cast<int>(index) + 1);
            },
            weak_factory_.GetWeakPtr()),
        u"Renderer process limit");
    auto* choice = select.get();
    if (AddRow(u"Renderer process limit",
               u"Fewer processes use less memory.", u"memory processes",
               std::move(select)))
      process_limit_ = choice;
  }
  AddRow(u"Restart Fly", u"Applies engine settings. Tabs are restored.",
         u"relaunch apply",
         MakeMenuButton(base::BindRepeating(&KeyboardEditor::RunAction,
                                            weak_factory_.GetWeakPtr(),
                                            std::string("restart")),
                        u"Restart now", MenuButtonStyle::kPrimary));
}

void KeyboardEditor::BuildAbout() {
  BeginGroup(u"Fly");
  {
    std::vector<std::unique_ptr<views::View>> controls;
    for (const auto& [label, action] :
         {std::pair{u"What's new", "show-whats-new"},
          std::pair{u"Check for updates", "check-for-updates"}})
      controls.push_back(MakeMenuButton(
          base::BindRepeating(&KeyboardEditor::RunAction,
                              weak_factory_.GetWeakPtr(), std::string(action)),
          label));
    auto install = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(),
                            std::string("install-update")),
        u"Install update", MenuButtonStyle::kPrimary);
    auto* install_ptr = install.get();
    controls.push_back(std::move(install));
    if (AddRow(base::UTF8ToUTF16(std::string("Fly ") + kFlyVersion),
               u"Chromium " + base::UTF8ToUTF16(std::string(
                                  version_info::GetVersionNumber())),
               u"version about release update", Controls(std::move(controls))))
      update_action_ = install_ptr;
  }
  {
    auto status = Text(update_status_.empty() ? u"Not checked in this session."
                                              : update_status_,
                       kMenuSmallTextSize, FlyLabel::Tone::kDim);
    status->SetMaximumWidthSingleLine(S(420));
    status->SetElideBehavior(gfx::ELIDE_TAIL);
    status->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
    auto* status_ptr = status.get();
    if (AddRow(u"Updates", u"", u"upgrade download version", std::move(status)))
      update_label_ = status_ptr;
  }
  BeginGroup(u"Desktop");
  {
    auto label = Text(desktop_entry_status_, kMenuSmallTextSize,
                      FlyLabel::Tone::kDim);
    label->SetMaximumWidthSingleLine(S(320));
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    auto button = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(),
                            std::string("desktop-entry")),
        desktop_entry_action_.empty() ? u"Add to menu" : desktop_entry_action_);
    auto* label_ptr = label.get();
    auto* button_ptr = button.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(label));
    controls.push_back(std::move(button));
    if (AddRow(u"Applications menu", u"", u"desktop entry launcher",
               Controls(std::move(controls)))) {
      desktop_entry_label_ = label_ptr;
      desktop_entry_button_ = button_ptr;
    }
  }
  {
    auto label = Text(default_browser_status_, kMenuSmallTextSize,
                      FlyLabel::Tone::kDim);
    label->SetMaximumWidthSingleLine(S(320));
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    auto button = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::RunAction,
                            weak_factory_.GetWeakPtr(),
                            std::string("desktop-default")),
        u"Make default");
    auto* label_ptr = label.get();
    auto* button_ptr = button.get();
    std::vector<std::unique_ptr<views::View>> controls;
    controls.push_back(std::move(label));
    controls.push_back(std::move(button));
    if (AddRow(u"Default browser", u"", u"system links open",
               Controls(std::move(controls)))) {
      default_browser_label_ = label_ptr;
      default_browser_button_ = button_ptr;
    }
  }
}

// Keyboard: a table of every action with its shortcut and its NORMAL key.
void KeyboardEditor::BuildKeyboard() {
  auto* parent = building_parent_.get();
  auto* layout = parent->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(S(14), S(20), S(12), S(20)), S(10)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* title = parent->AddChildView(std::make_unique<FlyLabel>(
      u"Keyboard", FlyFace::kSans, 18, gfx::Font::Weight::SEMIBOLD));
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);

  auto* toolbar = parent->AddChildView(std::make_unique<views::View>());
  auto* toolbar_layout =
      toolbar->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(10)));
  toolbar_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  search_ = toolbar->AddChildView(std::make_unique<FlyTextfield>(
      u"Filter actions", u"Filter actions or keys"));
  search_->set_controller(this);
  search_->SetText(keyboard_query_);
  toolbar_layout->SetFlexForView(search_, 1);
  modified_only_ = toolbar->AddChildView(std::make_unique<FlyToggle>(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (!editor || !editor->active() || !editor->modified_only_)
              return;
            editor->modified_only_checked_ =
                editor->modified_only_->GetChecked();
            editor->RefreshKeyboardRows();
          },
          weak_factory_.GetWeakPtr()),
      u"Changed only"));
  modified_only_->SetChecked(modified_only_checked_);
  toolbar->AddChildView(Text(u"Changed only", kMenuSmallTextSize));
  auto* tabs_label = toolbar->AddChildView(Text(u"Tab numbers",
                                                kMenuSmallTextSize));
  tabs_label->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, S(8), 0, 0)));
  tabs_ = toolbar->AddChildView(std::make_unique<FlyTextfield>(
      u"Tab number modifier", u"Alt"));
  tabs_->SetTooltipText(
      u"Hold this modifier to show tab numbers. Modifier+1–9 jumps to a tab.");
  tabs_->SetPreferredSize(gfx::Size(S(96), S(kMenuControlHeight)));
  tabs_->SetText(tab_modifiers_);
  tabs_->set_controller(this);

  auto* table = parent->AddChildView(std::make_unique<views::View>());
  table->SetBackground(FlyBackground(FlyRole::kOverlayMain, S(8)));
  table->SetBorder(FlyBorder(FlyRole::kBorder, 1, S(8), gfx::Insets::VH(0, S(4))));
  auto* table_layout = table->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  table_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  table->AddChildView(std::make_unique<KeyTableHeader>());
  rows_scroll_ = table->AddChildView(std::make_unique<views::ScrollView>());
  rows_scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  rows_scroll_->SetBackgroundColor(std::nullopt);
  rows_scroll_->SetDrawOverflowIndicator(false);
  rows_scroll_->ClipHeightTo(0, std::numeric_limits<int>::max());
  key_rows_ = rows_scroll_->SetContents(std::make_unique<views::View>());
  table_layout->SetFlexForView(rows_scroll_, 1);
  layout->SetFlexForView(table, 1);

  // The selected cell and what can be done with it.
  auto* detail = parent->AddChildView(std::make_unique<views::View>());
  auto* detail_layout =
      detail->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(8)));
  detail_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  cell_title_ = detail->AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kSans, kMenuTextSize, gfx::Font::Weight::MEDIUM));
  cell_title_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  cell_title_->SetElideBehavior(gfx::ELIDE_TAIL);
  detail_layout->SetFlexForView(cell_title_, 1);
  record_ = detail->AddChildView(MakeMenuButton(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active() && editor->selected_cell_)
              editor->StartRecording(*editor->selected_cell_);
          },
          weak_factory_.GetWeakPtr()),
      u"Record key"));
  clear_ = detail->AddChildView(MakeMenuButton(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active() && editor->selected_cell_)
              editor->WriteField(*editor->selected_cell_, u"");
          },
          weak_factory_.GetWeakPtr()),
      u"Clear"));
  default_ = detail->AddChildView(MakeMenuButton(
      base::BindRepeating(
          [](base::WeakPtr<KeyboardEditor> editor) {
            if (editor && editor->active() && editor->selected_cell_)
              editor->ResetField(*editor->selected_cell_);
          },
          weak_factory_.GetWeakPtr()),
      u"Default"));

  auto* actions = parent->AddChildView(std::make_unique<views::View>());
  actions->SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0),
                                   gfx::Insets::TLBR(S(10), 0, 0, 0)));
  auto* actions_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(8)));
  actions_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  more_ = actions->AddChildView(MakeMenuButton(
      base::BindRepeating(&KeyboardEditor::ShowMoreMenu,
                          weak_factory_.GetWeakPtr()),
      u"Restore, import, export…"));
  auto* spacer = actions->AddChildView(std::make_unique<views::View>());
  actions_layout->SetFlexForView(spacer, 1);
  discard_ = actions->AddChildView(MakeMenuButton(
      base::BindRepeating(&KeyboardEditor::DiscardKeyboard,
                          weak_factory_.GetWeakPtr()),
      u"Discard changes"));
  save_ = actions->AddChildView(MakeMenuButton(
      base::BindRepeating(&KeyboardEditor::Save, weak_factory_.GetWeakPtr()),
      u"Save shortcuts", MenuButtonStyle::kPrimary));
  UpdateKeyboardState();
}

void KeyboardEditor::RefreshKeyboardRows() {
  if (!key_rows_ || !rows_scroll_)
    return;
  StopRecording();
  const bool had_focus =
      views::IsViewClass<KeyCell>(FocusedMenuView(rows_scroll_));
  cells_.clear();
  row_actions_.clear();
  auto rows = std::make_unique<views::View>();
  auto* layout = rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 0));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  rows->GetViewAccessibility().SetRole(ax::mojom::Role::kGrid);
  rows->GetViewAccessibility().SetName(u"Keyboard shortcuts");
  // Search titles, categories and the bindings shown in the draft.
  const auto query = base::ToLowerASCII(std::u16string(
      base::TrimWhitespace(keyboard_query_, base::TRIM_ALL)));
  const auto terms = base::SplitString(query, u" \t", base::TRIM_WHITESPACE,
                                       base::SPLIT_WANT_NONEMPTY);
  for (const auto& entry : catalog_) {
    const Key global{entry.action, false};
    const Key normal{entry.action, true};
    if (modified_only_checked_ && !IsCustomised(global) && !IsCustomised(normal))
      continue;
    if (!terms.empty()) {
      const auto find = [&](const Key& key) { return DraftText(key); };
      const auto text = base::ToLowerASCII(
          base::UTF8ToUTF16(entry.title + " " + entry.category + " " +
                            entry.action) +
          u" " + find(global) + u" " + find(normal));
      bool match = true;
      for (const auto& term : terms)
        match &= text.find(term) != std::u16string::npos;
      if (!match)
        continue;
    }
    auto* row = rows->AddChildView(std::make_unique<KeyRow>(this, entry));
    cells_[global] = row->global();
    cells_[normal] = row->normal();
    row_actions_.push_back(entry.action);
  }
  if (row_actions_.empty())
    rows->AddChildView(Text(modified_only_checked_
                                ? u"No changed shortcuts match."
                                : u"No actions match.",
                            kMenuTextSize, FlyLabel::Tone::kDim))
        ->SetBorder(views::CreateEmptyBorder(gfx::Insets(S(16))));
  DisableSubpixelText(rows.get());
  key_rows_ = nullptr;
  key_rows_ = rows_scroll_->SetContents(std::move(rows));
  rows_scroll_->InvalidateLayout();
  // Keep the selection on the same cell when it is still listed.
  std::optional<Key> keep;
  if (selected_cell_ && cells_.contains(*selected_cell_))
    keep = selected_cell_;
  else if (!row_actions_.empty())
    keep = Key{row_actions_.front(), selected_cell_ && selected_cell_->second};
  selected_cell_.reset();
  if (keep)
    SelectCell(*keep, had_focus);
  UpdateCellDetails();
}

void KeyboardEditor::SelectCell(Key key, bool focus) {
  if (!cells_.contains(key))
    return;
  if (selected_cell_ && *selected_cell_ != key)
    if (auto it = cells_.find(*selected_cell_); it != cells_.end())
      it->second->SetSelected(false);
  selected_cell_ = key;
  auto* cell = cells_.at(key).get();
  cell->SetSelected(true);
  if (focus) {
    cell->RequestFocus();
    cell->ScrollViewToVisible();
  }
  UpdateCellDetails();
}

void KeyboardEditor::CellPressed(Key key, const ui::Event& event) {
  if (!active())
    return;
  // A click selects; Enter, Space or a double click records.
  const bool record =
      event.IsKeyEvent() ||
      (event.IsMouseEvent() && (event.flags() & ui::EF_IS_DOUBLE_CLICK));
  SelectCell(key, true);
  if (record)
    StartRecording(key);
}

void KeyboardEditor::MoveCell(int rows, int columns) {
  if (row_actions_.empty())
    return;
  Key key = selected_cell_.value_or(Key{row_actions_.front(), false});
  auto index = std::ranges::find(row_actions_, key.first) - row_actions_.begin();
  if (index >= static_cast<long>(row_actions_.size()))
    index = 0;
  index = std::clamp<long>(index + rows, 0,
                           static_cast<long>(row_actions_.size()) - 1);
  bool normal = key.second;
  if (columns)
    normal = columns > 0;
  SelectCell(Key{row_actions_[index], normal}, true);
}

void KeyboardEditor::UpdateCellDetails() {
  if (!cell_title_)
    return;
  const bool selected = selected_cell_ && cells_.contains(*selected_cell_);
  std::u16string title;
  if (selected) {
    for (const auto& entry : catalog_)
      if (entry.action == selected_cell_->first) {
        title = base::UTF8ToUTF16(entry.title);
        break;
      }
    title += selected_cell_->second ? u" · NORMAL mode key" : u" · Shortcut";
  }
  cell_title_->SetText(selected ? title : u"Select a cell to edit it.");
  cell_title_->SetTone(selected ? FlyLabel::Tone::kText : FlyLabel::Tone::kDim);
  const bool editable = commands_->CanEdit();
  record_->SetEnabled(selected && editable);
  clear_->SetEnabled(selected && editable &&
                     !SplitBindings(DraftText(*selected_cell_)).empty());
  default_->SetEnabled(selected && editable && IsCustomised(*selected_cell_));
}

bool KeyboardEditor::KeyboardDirty() const {
  if (base::UTF16ToUTF8(tab_modifiers_) != original_.tabs)
    return true;
  for (const auto& [key, text] : draft_) {
    const auto& table = key.second ? original_.normal : original_.shortcuts;
    const auto found = table.find(key.first);
    const std::vector<std::string> saved =
        found == table.end() ? std::vector<std::string>() : found->second;
    std::vector<std::string> drafted;
    for (const auto& binding : SplitBindings(text))
      drafted.push_back(base::UTF16ToUTF8(binding));
    if (saved != drafted)
      return true;
  }
  return false;
}

void KeyboardEditor::UpdateKeyboardState() {
  const bool dirty = KeyboardDirty();
  if (save_)
    save_->SetEnabled(commands_->CanEdit() && dirty);
  if (discard_)
    discard_->SetEnabled(dirty);
  if (!dirty)
    confirm_discard_ = false;
  if (save_ && !keyboard_readable_)
    SetStatus(u"Keyboard settings need recovery. They have been preserved.");
  else if (save_ && !commands_->CanEdit())
    SetStatus(u"Keyboard settings are managed.");
}

std::u16string KeyboardEditor::DraftText(const Key& key) const {
  const auto found = draft_.find(key);
  return found == draft_.end() ? std::u16string() : found->second;
}

std::u16string KeyboardEditor::DefaultBindings(const Key& key) const {
  const auto& defaults = DefaultKeyboardConfig();
  const auto& table = key.second ? defaults.normal : defaults.shortcuts;
  const auto found = table.find(key.first);
  return found == table.end()
             ? std::u16string()
             : base::UTF8ToUTF16(base::JoinString(found->second, "\n"));
}

bool KeyboardEditor::IsCustomised(const Key& key) const {
  return SplitBindings(DraftText(key)) != SplitBindings(DefaultBindings(key));
}

void KeyboardEditor::WriteField(Key key, std::u16string value) {
  StopRecording();
  draft_[key] = value;
  if (auto it = cells_.find(key); it != cells_.end())
    it->second->UpdateName();
  UpdateCellDetails();
  UpdateKeyboardState();
}

void KeyboardEditor::ResetField(Key key) {
  WriteField(key, DefaultBindings(key));
}

void KeyboardEditor::DiscardKeyboard() {
  if (!active())
    return;
  StopRecording();
  draft_.clear();
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  if (tabs_)
    tabs_->SetText(tab_modifiers_);
  SetStatus(u"Changes discarded.");
  RefreshKeyboardRows();
  UpdateKeyboardState();
}

void KeyboardEditor::ResetSection(bool normal) {
  if (!active())
    return;
  StopRecording();
  const auto& defaults = DefaultKeyboardConfig();
  for (const auto& entry : catalog_) {
    const Key key{entry.action, normal};
    // Actions without a stored entry and without a default stay absent.
    if (draft_.contains(key) || !DefaultBindings(key).empty())
      draft_[key] = DefaultBindings(key);
  }
  if (!normal) {
    tab_modifiers_ = base::UTF8ToUTF16(defaults.tabs);
    if (tabs_)
      tabs_->SetText(tab_modifiers_);
  }
  SetStatus(normal ? u"NORMAL keys restored. Save to apply."
                   : u"Shortcuts restored. Save to apply.");
  RefreshKeyboardRows();
  UpdateKeyboardState();
}

void KeyboardEditor::ResetAll() {
  StopRecording();
  for (auto& [key, value] : draft_)
    value = DefaultBindings(key);
  for (const auto& entry : catalog_)
    for (const auto normal : {false, true})
      if (!DefaultBindings({entry.action, normal}).empty())
        draft_[{entry.action, normal}] = DefaultBindings({entry.action, normal});
  tab_modifiers_ = base::UTF8ToUTF16(DefaultKeyboardConfig().tabs);
  if (tabs_)
    tabs_->SetText(tab_modifiers_);
  SetStatus(u"Defaults restored. Save to apply.");
  RefreshKeyboardRows();
  UpdateKeyboardState();
}

void KeyboardEditor::ShowMoreMenu() {
  if (!active() || !more_)
    return;
  more_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  more_model_->AddItem(kRestoreDefaults, u"Restore all defaults");
  more_model_->AddItem(kRestoreShortcuts, u"Restore shortcuts only");
  more_model_->AddItem(kRestoreNormal, u"Restore NORMAL keys only");
  more_model_->AddSeparator(ui::NORMAL_SEPARATOR);
  more_model_->AddItem(kImportBindings, u"Import from file…");
  more_model_->AddItem(kExportBindings, u"Export to file…");
  more_runner_ = std::make_unique<views::MenuRunner>(
      more_model_.get(), views::MenuRunner::HAS_MNEMONICS);
  more_runner_->RunMenuAt(more_->GetWidget(), nullptr,
                          more_->GetBoundsInScreen(),
                          views::MenuAnchorPosition::kBubbleTopLeft,
                          ui::mojom::MenuSourceType::kNone);
}

void KeyboardEditor::ExecuteCommand(int command_id, int) {
  if (!active())
    return;
  if (command_id >= kSpellingLanguageCommand) {
    const size_t index =
        static_cast<size_t>(command_id - kSpellingLanguageCommand);
    if (index < spelling_menu_codes_.size())
      AddSpellingLanguage(spelling_menu_codes_[index]);
    return;
  }
  switch (command_id) {
    case kRestoreDefaults:
      ResetAll();
      break;
    case kRestoreShortcuts:
      ResetSection(false);
      break;
    case kRestoreNormal:
      ResetSection(true);
      break;
    case kImportBindings:
      ChooseKeyboardFile(false);
      break;
    case kExportBindings:
      ChooseKeyboardFile(true);
      break;
  }
}

void KeyboardEditor::StartRecording(Key key) {
  if (!active() || !commands_->CanEdit())
    return;
  StopRecording();
  if (cells_.contains(key))
    SelectCell(key, true);
  recording_ = std::move(key);
  if (auto it = cells_.find(*recording_); it != cells_.end())
    it->second->SchedulePaint();
  SetStatus(recording_->second
                ? u"Press one key or two keys in a row. Esc cancels."
                : u"Press a shortcut. Esc cancels.");
}

void KeyboardEditor::StopRecording() {
  recording_timer_.Stop();
  const auto previous = recording_;
  recording_.reset();
  chord_.clear();
  if (previous)
    if (auto it = cells_.find(*previous); it != cells_.end())
      it->second->SchedulePaint();
}

void KeyboardEditor::FinishRecording() {
  if (!recording_ || chord_.empty())
    return;
  const auto key = *recording_;
  const auto chord = base::UTF8ToUTF16(chord_);
  auto bindings = SplitBindings(DraftText(key));
  if (std::ranges::find(bindings, chord) == bindings.end())
    bindings.push_back(chord);
  WriteField(key, base::JoinString(bindings, u"\n"));
  SetStatus(u"Added " + chord + u". Save to apply.");
}

void KeyboardEditor::Save() {
  if (!active())
    return;
  StopRecording();
  if (commands_->generation() != generation_) {
    SetStatus(u"Keyboard settings changed while Preferences was open. Close "
              u"and reopen it to edit the latest settings.");
    return;
  }
  auto config = original_.ToValue();
  for (const auto& [key, text] : draft_) {
    base::ListValue bindings;
    for (const auto& chord : SplitBindings(text))
      bindings.Append(base::UTF16ToUTF8(chord));
    config.FindDict(key.second ? "normal" : "shortcuts")
        ->Set(key.first, std::move(bindings));
  }
  config.Set("tabs", base::UTF16ToUTF8(tab_modifiers_));
  const auto saved = commands_->Save(config);
  if (!saved.config) {
    SetStatus(base::UTF8ToUTF16(saved.error));
    return;
  }
  original_ = *saved.config;
  generation_ = commands_->generation();
  close_bindings_ = original_.shortcuts.at("toggle-help");
  draft_.clear();
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? original_.normal : original_.shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(original_.tabs);
  SetStatus(u"Shortcuts saved.");
  UpdateKeyboardState();
  RefreshKeyboardRows();
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
      SetStatus(u"Paste a Chrome Web Store extension link.");
      return;
    }
    OpenUrl("https://chromewebstore.google.com/detail/" + id);
    return;
  }
  if (extensions_ && extensions_browser_ &&
      (action == "extensions-usage" || action == "extensions-reload")) {
    auto* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
    if (action == "extensions-reload")
      SetStatus(extensions_->ReloadUnpacked()
                    ? u"Extensions reloaded. Reload open tabs to refresh "
                      u"their content scripts."
                    : u"No unpacked extensions to reload.");
    else if (profile)
      SetExtensionUsage(extensions_->Usage(profile));
    return;
  }
  if (action == "clear-site-cookies")
    SetStatus(u"Site cookies cleared.");
  else if (action == "clear-cache")
    SetStatus(u"Cache cleared.");
  if (run_action_)
    run_action_.Run(std::move(action));
}

void KeyboardEditor::OpenUrl(std::string url) {
  Cancel(true);
  if (run_action_)
    run_action_.Run("open-window:" + url);
}

void KeyboardEditor::SetStatus(std::u16string text) {
  status_text_ = std::move(text);
  if (status_) {
    status_->SetText(status_text_);
    status_->SetTooltipText(status_text_);
  }
}

void KeyboardEditor::SetUpdateStatus(std::u16string status,
                                     std::string available_version) {
  update_status_ = std::move(status);
  update_available_ = std::move(available_version);
  if (update_label_)
    update_label_->SetText(update_status_);
  if (update_action_) {
    update_action_->SetVisible(!update_available_.empty());
    update_action_->SetText(u"Install " + base::UTF8ToUTF16(update_available_));
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

void KeyboardEditor::ChooseEngine(size_t index) {
  if (!active() || !search_preferences_.CanEdit())
    return;
  if (index >= kSearchEngines.size()) {
    // Custom: the URL field below takes the address.
    if (auto it = search_settings_.find("engine"); it != search_settings_.end()) {
      it->second->RequestFocus();
      it->second->SelectAll(false);
    }
    return;
  }
  const std::string url(kSearchEngines.at(index));
  if (auto it = search_settings_.find("engine"); it != search_settings_.end())
    it->second->SetText(base::UTF8ToUTF16(url));
  SetStatus(search_preferences_.SetEngine(url) ? u"Search engine saved."
                                               : u"Cannot save this engine.");
}

void KeyboardEditor::ChooseEmailHandler(size_t index) {
  if (!active())
    return;
  if (index < std::size(kEmailHandlers)) {
    preferences_.SetString("email_handler",
                           std::string(kEmailHandlers.at(index).first));
    if (email_handler_)
      email_handler_->SetText(u"");
    SetStatus(u"Email choice saved.");
  } else if (email_custom_row_) {
    email_custom_row_->SetVisible(true);
    email_handler_->RequestFocus();
  }
  RefreshAppearance();
}

void KeyboardEditor::RefreshSearchSection() {
  const auto settings = search_preferences_.Read();
  const bool editable = search_preferences_.CanEdit();
  for (auto& [key, field] : search_settings_)
    field->SetEnabled(editable);
  for (auto* button : {search_save_.get(), alias_save_.get()})
    if (button)
      button->SetEnabled(editable);
  if (engine_choice_) {
    size_t engine_index = kSearchEngines.size();
    if (settings)
      for (size_t i = 0; i < kSearchEngines.size(); ++i)
        if (kSearchEngines.at(i) == settings->engine)
          engine_index = i;
    engine_choice_->SetSelectedIndex(engine_index);
    engine_choice_->SetEnabled(editable);
  }
  if (!aliases_box_)
    return;
  alias_remove_.clear();
  aliases_box_->RemoveAllChildViews();
  if (!settings || settings->aliases.empty()) {
    aliases_box_->AddChildView(Text(
        !settings ? u"Search settings need recovery. Saved choices have been "
                    u"preserved."
                  : u"No aliases of your own yet.",
        kMenuSmallTextSize, FlyLabel::Tone::kDim));
  } else {
    for (const auto& [key, alias] : settings->aliases) {
      auto* line = aliases_box_->AddChildView(std::make_unique<views::View>());
      auto* layout = line->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(10)));
      layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
      auto* name = line->AddChildView(std::make_unique<FlyLabel>(
          base::UTF8ToUTF16(key), FlyFace::kMono, kMenuSmallTextSize,
          gfx::Font::Weight::MEDIUM));
      name->SetPreferredSize(gfx::Size(S(64), name->GetPreferredSize().height()));
      name->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      auto* detail = line->AddChildView(
          Text(base::UTF8ToUTF16(alias.name + " · " + alias.url),
               kMenuSmallTextSize, FlyLabel::Tone::kDim));
      detail->SetElideBehavior(gfx::ELIDE_MIDDLE);
      layout->SetFlexForView(detail, 1);
      auto* remove = line->AddChildView(MakeMenuButton(
          base::BindRepeating(&KeyboardEditor::RemoveAlias,
                              weak_factory_.GetWeakPtr(), key),
          u"Remove", MenuButtonStyle::kQuiet));
      remove->GetViewAccessibility().SetName(u"Remove alias " +
                                             base::UTF8ToUTF16(key));
      remove->SetEnabled(editable);
      alias_remove_[base::ToLowerASCII(key)] = remove;
    }
  }
  DisableSubpixelText(aliases_box_);
  aliases_box_->InvalidateLayout();
}

views::Textfield* KeyboardEditor::search_setting_for_testing(
    std::string_view key) {
  const auto found = search_settings_.find(std::string(key));
  return found == search_settings_.end() ? nullptr : found->second.get();
}

void KeyboardEditor::ShowSearch() {
  if (!active())
    return;
  ShowPane(kGeneralPane);
  if (auto it = search_settings_.find("engine"); it != search_settings_.end())
    it->second->RequestFocus();
}

void KeyboardEditor::SaveSearchSetting(std::string action) {
  if (!active() || !search_preferences_.CanEdit())
    return;
  auto value = [&](std::string key) {
    const auto it = search_settings_.find(key);
    return it == search_settings_.end()
               ? std::string()
               : std::string(base::TrimWhitespaceASCII(
                     base::UTF16ToUTF8(it->second->GetText()),
                     base::TRIM_ALL));
  };
  const bool saved =
      action == "engine"
          ? search_preferences_.SetEngine(value("engine"))
          : search_preferences_.SetAlias(value("alias"), value("name"),
                                         value("url"));
  if (saved && action == "alias")
    for (const auto* key : {"alias", "name", "url"})
      if (auto it = search_settings_.find(key); it != search_settings_.end())
        it->second->SetText(u"");
  SetStatus(saved ? (action == "engine" ? u"Search URL saved."
                                        : u"Alias added.")
                  : u"Not saved. Check the alias and use an http(s) URL "
                    u"with %s.");
  RefreshSearchSection();
}

void KeyboardEditor::RemoveAlias(std::string alias) {
  if (!active() || !search_preferences_.CanEdit())
    return;
  // The list is rebuilt; finish the button's own event first.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<KeyboardEditor> editor, std::string alias) {
                       if (!editor || !editor->active())
                         return;
                       editor->SetStatus(
                           editor->search_preferences_.RemoveAlias(alias)
                               ? u"Alias removed."
                               : u"The alias could not be removed.");
                       editor->RefreshSearchSection();
                     },
                     weak_factory_.GetWeakPtr(), std::move(alias)));
}

// Spelling: the languages words are checked in. A word is correct when any
// of them knows it; the first is tried first and leads the suggestions.
void KeyboardEditor::BuildSpelling() {
  BeginGroup(u"Spelling");
  AddRow(u"Check spelling", u"In the text fields of web pages.",
         u"spellcheck spell checker typos",
         MakeToggle("spellcheck_enabled", u"Check spelling"));
  {
    auto list = std::make_unique<views::View>();
    auto* layout = list->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(4)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    // The group's caption names the list, so the row has no label.
    list->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
    list->GetViewAccessibility().SetName(u"Spelling languages");
    auto* list_ptr = list.get();
    if (AddRow(u"", u"",
               u"spelling languages spellcheck dictionaries order first",
               std::move(list), true))
      spelling_list_ = list_ptr;
  }
  {
    auto add = MakeMenuButton(
        base::BindRepeating(&KeyboardEditor::ShowSpellingMenu,
                            weak_factory_.GetWeakPtr()),
        u"Add…");
    add->GetViewAccessibility().SetName(u"Add a spelling language");
    auto* add_ptr = add.get();
    if (AddRow(u"Add language",
               base::StrCat({u"Up to ",
                             base::NumberToString16(kMaxSpellcheckLanguages),
                             u". Dictionaries come from Google."}),
               u"spelling spellcheck dictionary language download",
               std::move(add)))
      spelling_add_ = add_ptr;
  }
  AddNote(
      u"A word is correct when any of these languages has it. The first is "
      u"checked first and leads the suggestions. Each extra language makes "
      u"suggestions slower and lets more typos pass as words of another one.");
}

Profile* KeyboardEditor::SpellingProfile() const {
  auto* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
  return profile ? profile->GetOriginalProfile() : nullptr;
}

void KeyboardEditor::RefreshSpelling() {
  if (!active())
    return;
  auto* profile = SpellingProfile();
  const auto spelling = ReadEffectiveSpellcheck(profile, preferences_);
  const bool editable = preferences_.CanEdit();
  if (auto it = appearance_.find("spellcheck_enabled");
      it != appearance_.end()) {
    it->second->SetChecked(spelling.enabled);
    it->second->SetEnabled(editable);
  }
  if (spelling_add_)
    spelling_add_->SetEnabled(editable && spelling.languages.size() <
                                              kMaxSpellcheckLanguages);
  if (!spelling_list_)
    return;
  // The download state of each dictionary in the selected workspace; the
  // dictionary files are shared by every workspace.
  StopObservingDictionaries();
  std::map<std::string, std::u16string> states;
  std::set<std::string> failed;
  if (auto* service =
          profile ? SpellcheckServiceFactory::GetForContext(profile) : nullptr) {
    for (const auto& dictionary : service->GetHunspellDictionaries()) {
      dictionary->AddObserver(this);
      observed_dictionaries_.push_back(dictionary->AsWeakPtr());
      const auto* language = FindSpellcheckLanguage(dictionary->GetLanguage());
      if (!language)
        continue;
      const std::string code(language->code);
      if (dictionary->IsDownloadInProgress()) {
        states[code] = u"Downloading…";
      } else if (dictionary->IsDownloadFailure()) {
        states[code] = u"Download failed";
        failed.insert(code);
      }
    }
  }
  std::string shown =
      base::JoinString(spelling.languages, ",") + (editable ? "+" : "-");
  for (const auto& [code, state] : states)
    shown += "|" + code + base::UTF16ToUTF8(state);
  if (shown == spelling_shown_)
    return;
  spelling_shown_ = shown;
  spelling_buttons_.clear();
  spelling_names_.clear();
  spelling_list_->RemoveAllChildViews();
  if (spelling.languages.empty())
    spelling_list_->AddChildView(
        Text(u"No languages yet. Add one to check spelling.",
             kMenuSmallTextSize, FlyLabel::Tone::kDim));
  for (size_t i = 0; i < spelling.languages.size(); ++i) {
    const std::string& code = spelling.languages[i];
    const auto* language = FindSpellcheckLanguage(code);
    const std::u16string name =
        language ? std::u16string(language->name) : base::UTF8ToUTF16(code);
    spelling_names_.push_back(name);
    auto* line = spelling_list_->AddChildView(std::make_unique<views::View>());
    auto* layout = line->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(10)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    // The position is the order the languages are checked in.
    auto* position = line->AddChildView(std::make_unique<FlyLabel>(
        base::NumberToString16(i + 1), FlyFace::kMono, kMenuSmallTextSize,
        gfx::Font::Weight::MEDIUM));
    position->SetPreferredSize(
        gfx::Size(S(16), position->GetPreferredSize().height()));
    position->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto* label = line->AddChildView(Text(name, kMenuSmallTextSize));
    label->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(label, 1);
    if (const auto state = states.find(code); state != states.end())
      line->AddChildView(
          Text(state->second, kMenuSmallTextSize, FlyLabel::Tone::kDim));
    const auto add_button = [&](std::string action, std::u16string text,
                                std::u16string accessible_name, bool enabled) {
      auto* button = line->AddChildView(MakeMenuButton(
          base::BindRepeating(&KeyboardEditor::ChangeSpellingLanguage,
                              weak_factory_.GetWeakPtr(), action, code),
          std::move(text), MenuButtonStyle::kQuiet));
      button->GetViewAccessibility().SetName(accessible_name);
      button->SetEnabled(enabled);
      spelling_buttons_[action + ":" + code] = button;
    };
    if (failed.contains(code))
      add_button("retry", u"Retry", u"Download the " + name + u" dictionary again",
                 true);
    add_button("up", u"Up", u"Move " + name + u" up", editable && i > 0);
    add_button("remove", u"Remove", u"Remove " + name, editable);
  }
  DisableSubpixelText(spelling_list_);
  spelling_list_->InvalidateLayout();
}

void KeyboardEditor::StopObservingDictionaries() {
  for (const auto& dictionary : observed_dictionaries_)
    if (dictionary)
      dictionary->RemoveObserver(this);
  observed_dictionaries_.clear();
}

void KeyboardEditor::SaveSpelling(SpellcheckPreferences spelling,
                                  std::u16string done) {
  if (!active())
    return;
  SetStatus(preferences_.SetSpellcheck(spelling)
                ? std::move(done)
                : u"Spelling settings were not saved.");
  RefreshSpelling();
  // The workspaces load the new dictionaries once every observer of the
  // change has run; their download states are read after that.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshSpelling,
                                weak_factory_.GetWeakPtr()));
}

void KeyboardEditor::ShowSpellingMenu() {
  if (!active() || !spelling_add_)
    return;
  const auto spelling = ReadEffectiveSpellcheck(SpellingProfile(), preferences_);
  spelling_menu_codes_.clear();
  spelling_model_ = std::make_unique<ui::SimpleMenuModel>(this);
  for (const auto& language : SpellcheckLanguages()) {
    if (std::ranges::contains(spelling.languages, language.code))
      continue;
    spelling_model_->AddItem(
        kSpellingLanguageCommand + static_cast<int>(spelling_menu_codes_.size()),
        std::u16string(language.name));
    spelling_menu_codes_.emplace_back(language.code);
  }
  spelling_runner_ =
      std::make_unique<views::MenuRunner>(spelling_model_.get(), 0);
  spelling_runner_->RunMenuAt(spelling_add_->GetWidget(), nullptr,
                              spelling_add_->GetBoundsInScreen(),
                              views::MenuAnchorPosition::kBubbleTopLeft,
                              ui::mojom::MenuSourceType::kNone);
}

void KeyboardEditor::AddSpellingLanguage(const std::string& code) {
  if (!active())
    return;
  auto spelling = ReadEffectiveSpellcheck(SpellingProfile(), preferences_);
  const auto* language = FindSpellcheckLanguage(code);
  if (!language || std::ranges::contains(spelling.languages, language->code))
    return;
  if (spelling.languages.size() >= kMaxSpellcheckLanguages) {
    SetStatus(base::StrCat({u"Up to ",
                            base::NumberToString16(kMaxSpellcheckLanguages),
                            u" languages: remove one first."}));
    return;
  }
  spelling.languages.emplace_back(language->code);
  SaveSpelling(std::move(spelling),
               std::u16string(language->name) + u" added.");
}

void KeyboardEditor::ChangeSpellingLanguage(std::string action,
                                            std::string code) {
  // The list is rebuilt; finish the button's own event first.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<KeyboardEditor> editor, std::string action,
             std::string code) {
            if (!editor || !editor->active())
              return;
            auto* profile = editor->SpellingProfile();
            if (action == "retry") {
              if (auto* service = profile ? SpellcheckServiceFactory::
                                                GetForContext(profile)
                                          : nullptr)
                for (const auto& dictionary : service->GetHunspellDictionaries())
                  if (const auto* language =
                          FindSpellcheckLanguage(dictionary->GetLanguage());
                      language && language->code == code &&
                      dictionary->IsDownloadFailure())
                    dictionary->RetryDownloadDictionary(profile);
              editor->RefreshSpelling();
              return;
            }
            auto spelling =
                ReadEffectiveSpellcheck(profile, editor->preferences_);
            const auto found = std::ranges::find(spelling.languages, code);
            if (found == spelling.languages.end())
              return;
            const auto* language = FindSpellcheckLanguage(code);
            const std::u16string name = language
                                            ? std::u16string(language->name)
                                            : base::UTF8ToUTF16(code);
            if (action == "up" && found != spelling.languages.begin()) {
              std::iter_swap(found, std::prev(found));
              editor->SaveSpelling(std::move(spelling), name + u" moved up.");
            } else if (action == "remove") {
              spelling.languages.erase(found);
              editor->SaveSpelling(std::move(spelling), name + u" removed.");
            }
          },
          weak_factory_.GetWeakPtr(), std::move(action), std::move(code)));
}

void KeyboardEditor::OnHunspellDictionaryInitialized(const std::string&) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshSpelling,
                                weak_factory_.GetWeakPtr()));
}

void KeyboardEditor::OnHunspellDictionaryDownloadBegin(const std::string&) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshSpelling,
                                weak_factory_.GetWeakPtr()));
}

void KeyboardEditor::OnHunspellDictionaryDownloadSuccess(const std::string&) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshSpelling,
                                weak_factory_.GetWeakPtr()));
}

void KeyboardEditor::OnHunspellDictionaryDownloadFailure(const std::string&) {
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshSpelling,
                                weak_factory_.GetWeakPtr()));
}

std::vector<std::u16string> KeyboardEditor::spelling_languages_for_testing()
    const {
  return spelling_names_;
}

views::Button* KeyboardEditor::spelling_button_for_testing(
    std::string_view action,
    std::string_view code) {
  const auto found = spelling_buttons_.find(base::StrCat({action, ":", code}));
  return found == spelling_buttons_.end() ? nullptr : found->second.get();
}

views::Button* KeyboardEditor::spelling_add_for_testing() {
  return spelling_add_;
}

void KeyboardEditor::AddSpellingLanguageForTesting(const std::string& code) {
  AddSpellingLanguage(code);
}

void KeyboardEditor::ShowKeyboard() {
  if (!active())
    return;
  ShowPane(kKeyboardPane);
  if (search_)
    search_->RequestFocus();
}

void KeyboardEditor::ShowAppearance(bool return_to_page) {
  if (!active())
    return;
  restore_page_focus_ |= return_to_page;
  ShowPane(kAppearancePane);
  if (auto it = appearance_.find("theme"); it != appearance_.end())
    it->second->RequestFocus();
}

void KeyboardEditor::ShowPerformance() {
  if (!active())
    return;
  ShowPane(kPerformancePane);
  if (suspension_timeout_)
    suspension_timeout_->RequestFocus();
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
  if (!active())
    return;
  ShowPane(kExtensionsPane);
  if (extensions_enabled_)
    extensions_enabled_->RequestFocus();
}

void KeyboardEditor::SetExtensionUsage(std::u16string text) {
  extensions_usage_text_ = std::move(text);
  if (extensions_usage_) {
    extensions_usage_->SetText(extensions_usage_text_);
    extensions_usage_->InvalidateLayout();
  }
}

void KeyboardEditor::RefreshExtensions() {
  if (!extensions_list_ || !extensions_)
    return;
  Profile* profile = extensions_profile_ ? extensions_profile_.Run() : nullptr;
  if (extensions_enabled_) {
    extensions_enabled_->SetChecked(extensions_->Enabled());
    extensions_enabled_->SetEnabled(extensions_->CanEdit());
  }
  extensions_list_->RemoveAllChildViews();
  if (!profile || profile->IsOffTheRecord()) {
    extensions_list_->AddChildView(Text(
        profile ? u"Private workspaces run no extensions."
                : u"Select a workspace to manage its extensions.",
        kMenuSmallTextSize, FlyLabel::Tone::kDim));
  }
  const auto list = profile && !profile->IsOffTheRecord()
                        ? extensions_->List(profile)
                        : std::vector<FlyExtensionInfo>();
  if (profile && !profile->IsOffTheRecord() && list.empty())
    extensions_list_->AddChildView(
        Text(u"None in this workspace.", kMenuSmallTextSize,
             FlyLabel::Tone::kDim));
  for (const auto& info : list) {
    auto* line = extensions_list_->AddChildView(std::make_unique<views::View>());
    auto* layout = line->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(8)));
    layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* enabled = line->AddChildView(std::make_unique<FlyToggle>(
        views::Button::PressedCallback(), u"Enable " + info.name));
    enabled->SetChecked(info.enabled);
    enabled->SetEnabled(info.removable && extensions_->Enabled());
    enabled->SetCallback(base::BindRepeating(
        [](base::WeakPtr<KeyboardEditor> editor, std::string id,
           FlyToggle* toggle) {
          if (!editor || !editor->active() || !editor->extensions_browser_)
            return;
          if (auto* profile = editor->extensions_profile_.Run())
            editor->extensions_->SetExtensionEnabled(profile, id,
                                                     toggle->GetChecked());
          base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
              FROM_HERE, base::BindOnce(&KeyboardEditor::RefreshExtensions,
                                        editor));
        },
        weak_factory_.GetWeakPtr(), info.id, enabled));
    auto* name = line->AddChildView(Text(info.name));
    name->SetElideBehavior(gfx::ELIDE_TAIL);
    layout->SetFlexForView(name, 1);
    line->AddChildView(Text(base::UTF8ToUTF16(info.version) +
                                (info.bundled ? u" · included" : u""),
                            kMenuSmallTextSize, FlyLabel::Tone::kDim));
    if (info.has_options) {
      line->AddChildView(MakeMenuButton(
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
          u"Options", MenuButtonStyle::kQuiet));
    }
    auto* remove = line->AddChildView(MakeMenuButton(
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
        u"Remove", MenuButtonStyle::kQuiet));
    remove->GetViewAccessibility().SetName(u"Remove " + info.name);
    remove->SetEnabled(info.removable);
  }
  DisableSubpixelText(extensions_list_);
  extensions_list_->InvalidateLayout();
}

FlyToggle* KeyboardEditor::appearance_for_testing(std::string_view key) {
  auto it = appearance_.find(std::string(key));
  return it == appearance_.end() ? nullptr : it->second.get();
}

void KeyboardEditor::RefreshAppearance() {
  if (!active())
    return;
  RefreshSearchSection();
  RefreshSpelling();
  RefreshExtensions();
  const auto theme = preferences_.ReadTheme();
  const auto clock = preferences_.ReadClock();
  const auto frequent = preferences_.ReadFrequentSitesVisible();
  const auto footer = preferences_.ReadStatusBarVisible();
  const auto controls = preferences_.ReadWindowControlsVisible();
  const auto collapse = preferences_.ReadSidebarAutoCollapse();
  const auto timeout = preferences_.ReadSuspendTimeoutMinutes();
  const bool editable = preferences_.CanEdit();
  const auto set = [&](const char* key, bool checked, bool enabled) {
    const auto it = appearance_.find(key);
    if (it == appearance_.end())
      return;
    it->second->SetChecked(checked);
    it->second->SetEnabled(enabled);
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
  for (const char* key :
       {"web_notifications", "disable_gpu", "background_throttling",
        "enable_diagnostics", "preload_address_results"})
    set(key, preferences_.ReadBool(key).value_or(false), editable);
  if (suspension_timeout_) {
    std::optional<size_t> index;
    if (timeout)
      for (size_t i = 0; i < kSuspendMinutes.size(); ++i)
        if (kSuspendMinutes[i] == *timeout)
          index = i;
    suspension_timeout_->SetSelectedIndex(index);
    suspension_timeout_->SetEnabled(timeout &&
                                    preferences_.CanEditSuspendTimeout());
  }
  if (clock_format_) {
    std::optional<size_t> index;
    if (clock)
      for (size_t i = 0; i < kClockFormats.size(); ++i)
        if (kClockFormats[i] == clock->format)
          index = i;
    clock_format_->SetSelectedIndex(index);
    clock_format_->SetEnabled(clock && preferences_.CanEditClock());
  }
  if (scale_choice_) {
    scale_choice_->SetSelectedIndex(
        preferences_.ReadString("interface_scale").value_or("normal") == "large"
            ? 1u
            : 0u);
    scale_choice_->SetEnabled(editable);
  }
  if (process_limit_) {
    process_limit_->SetSelectedIndex(static_cast<size_t>(
        std::clamp(preferences_.ReadInt("process_limit").value_or(4), 1, 8) -
        1));
    process_limit_->SetEnabled(editable);
  }
  if (email_choice_) {
    const auto email =
        preferences_.ReadString("email_handler").value_or("system");
    size_t email_index = std::size(kEmailHandlers);
    for (size_t i = 0; i < std::size(kEmailHandlers); ++i)
      if (kEmailHandlers.at(i).first == email)
        email_index = i;
    email_choice_->SetSelectedIndex(email_index);
    email_choice_->SetEnabled(editable);
    const bool custom = email_index == std::size(kEmailHandlers);
    if (email_custom_row_ && query_.empty())
      email_custom_row_->SetVisible(custom || (email_handler_ &&
                                               email_handler_->HasFocus()));
    if (email_handler_ && !email_handler_->HasFocus())
      email_handler_->SetText(custom ? base::UTF8ToUTF16(email) : u"");
    if (email_handler_)
      email_handler_->SetEnabled(editable);
  }
  const bool dark = theme && theme->mode == "dark";
  const auto& palette = GetFlyColors();
  for (auto& [key, field] : color_fields_) {
    const auto stored = preferences_.ReadThemeColor(dark, key).value_or("");
    if (!field->HasFocus())
      field->SetText(base::UTF8ToUTF16(stored));
    field->SetPlaceholderText(u"Default");
    field->SetEnabled(editable);
    const SkColor shown = key == "accent"       ? palette.accent
                          : key == "bg_main"    ? palette.bg_main
                          : key == "bg_sidebar" ? palette.bg_sidebar
                                                : palette.text;
    if (auto it = color_swatches_.find(key); it != color_swatches_.end())
      static_cast<Swatch*>(it->second.get())->SetColor(shown);
  }
  for (auto& [key, slider] : opacity_sliders_) {
    const int value = preferences_.ReadThemeOpacity(dark, key);
    slider->SetValue((value - 40) / 60.f);
    slider->SetEnabled(editable);
    opacity_labels_.at(key)->SetText(base::NumberToString16(value) + u"%");
  }
  if (private_mode_)
    private_mode_->SetChecked(private_on_);
  if (update_action_) {
    update_action_->SetVisible(!update_available_.empty());
    if (!update_available_.empty())
      update_action_->SetText(u"Install " +
                              base::UTF8ToUTF16(update_available_));
  }
  if (desktop_entry_button_)
    desktop_entry_button_->SetVisible(!desktop_entry_action_.empty());
  if (default_browser_button_)
    default_browser_button_->SetVisible(can_make_default_);
  if (!theme || !clock || !footer || !controls || !collapse || !frequent)
    SetStatus(u"Some settings need recovery. Saved data has been preserved.");
  else if (!preferences_.CanEditTheme() && !appearance_.empty())
    SetStatus(u"These settings are managed.");
}

void KeyboardEditor::ApplyProductTexts() {
  if (!active())
    return;
  std::vector<std::u16string> refused;
  // A custom compose URL typed next to the email choices.
  if (email_handler_) {
    const auto email = std::string(base::TrimWhitespaceASCII(
        base::UTF16ToUTF8(email_handler_->GetText()), base::TRIM_ALL));
    if (!email.empty() && !preferences_.SetString("email_handler", email))
      refused.push_back(u"the compose URL (use http or https)");
  }
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
    refused.push_back(u"colours (use #rrggbb)");
  SetStatus(refused.empty()
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
  } else if (key == "spellcheck_enabled") {
    auto spelling = ReadEffectiveSpellcheck(SpellingProfile(), preferences_);
    spelling.enabled = checked;
    SaveSpelling(std::move(spelling), checked ? u"Spelling is checked."
                                              : u"Spelling is not checked.");
  } else {
    preferences_.SetBool(key, checked);
  }
  RefreshAppearance();
}

void KeyboardEditor::ContentsChanged(views::Textfield* sender,
                                     const std::u16string& text) {
  if (!active())
    return;
  if (sender == settings_search_) {
    query_ = std::u16string(base::TrimWhitespace(text, base::TRIM_ALL));
    ShowContent();
    // Searching shows the first section with matches when it is cleared.
    if (!query_.empty())
      for (int pane = 0; pane < kPaneCount; ++pane)
        if (matches_per_pane_[pane] > 0) {
          pane_ = pane;
          for (int i = 0; i < kPaneCount; ++i)
            rail_[i]->SetCurrent(i == pane_);
          break;
        }
  } else if (sender == search_) {
    keyboard_query_ = text;
    RefreshKeyboardRows();
  } else if (sender == tabs_) {
    tab_modifiers_ = text;
    UpdateKeyboardState();
  }
}

bool KeyboardEditor::HandleKeyEvent(views::Textfield* sender,
                                    const ui::KeyEvent& event) {
  if (!active() || event.type() != ui::EventType::kKeyPressed ||
      event.key_code() != ui::VKEY_RETURN)
    return false;
  // Look fields up without inserting: only the shown section's fields exist.
  const auto setting = [&](const char* key) -> views::Textfield* {
    const auto found = search_settings_.find(key);
    return found == search_settings_.end() ? nullptr : found->second.get();
  };
  if (sender == setting("engine")) {
    SaveSearchSetting("engine");
  } else if (sender == email_handler_) {
    ApplyProductTexts();
  } else if (sender == setting("alias") || sender == setting("name") ||
             sender == setting("url")) {
    SaveSearchSetting("alias");
  } else if (sender == extensions_store_link_) {
    RunAction("extensions-store-link:" +
              base::UTF16ToUTF8(extensions_store_link_->GetText()));
  } else if (sender == search_) {
    MoveCell(0, 0);
  } else if (sender == settings_search_) {
    CycleMenuFocus(content_host_, false);
  } else {
    for (auto& [key, field] : color_fields_)
      if (field == sender) {
        ApplyProductTexts();
        return true;
      }
    return false;
  }
  return true;
}

bool KeyboardEditor::HandleOverlayKey(const ui::KeyEvent& event) {
  if (!active())
    return false;
  if (event.type() != ui::EventType::kKeyPressed || event.is_char())
    return recording();
  const input::NativeWebKeyboardEvent native(event);
  const auto stroke = NativeEventStroke(native);
  const auto code = event.key_code();
  if (code == ui::VKEY_ESCAPE) {
    if (recording()) {
      StopRecording();
      SetStatus(u"Recording cancelled.");
    } else if (settings_search_ && !settings_search_->GetText().empty()) {
      // Escape clears a search first, then closes.
      settings_search_->SetText(u"");
      ContentsChanged(settings_search_, u"");
      settings_search_->RequestFocus();
    } else {
      RequestClose();
    }
    return true;
  }
  if (recording()) {
    if (native.skip_if_unhandled ||
        (native.GetModifiers() & (blink::WebInputEvent::kIsComposing |
                                  blink::WebInputEvent::kAltGrKey))) {
      StopRecording();
      SetStatus(u"Composition keys are not recorded.");
      return false;
    }
    if (event.is_repeat() || stroke.empty())
      return true;
    if (chord_.empty()) {
      chord_ = stroke;
      if (!recording_->second) {
        FinishRecording();
      } else {
        if (auto it = cells_.find(*recording_); it != cells_.end())
          it->second->SchedulePaint();
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
    RequestClose();
    return true;
  }
  const bool control = event.IsControlDown();
  const bool plain =
      !(event.flags() & (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN |
                         ui::EF_COMMAND_DOWN));
  // Sections: Ctrl+PgDn / Ctrl+PgUp and Ctrl+Tab / Ctrl+Shift+Tab.
  if (control && (code == ui::VKEY_NEXT || code == ui::VKEY_PRIOR ||
                  code == ui::VKEY_TAB)) {
    const bool back = code == ui::VKEY_PRIOR ||
                      (code == ui::VKEY_TAB && event.IsShiftDown());
    if (!query_.empty()) {
      query_.clear();
      settings_search_->SetText(u"");
    }
    SelectPane((pane_ + (back ? kPaneCount - 1 : 1)) % kPaneCount, true);
    return true;
  }
  if (control && code == ui::VKEY_F) {
    settings_search_->RequestFocus();
    settings_search_->SelectAll(false);
    return true;
  }
  if (control && code == ui::VKEY_S && save_) {
    Save();
    return true;
  }
  if (code == ui::VKEY_TAB && !control) {
    CycleMenuFocus(root_, event.IsShiftDown());
    return true;
  }
  auto* focused = FocusedMenuView(root_);
  // "/" searches the settings from anywhere but a text field.
  if (plain && event.GetCharacter() == u'/' &&
      !(focused && views::IsViewClass<views::Textfield>(focused))) {
    settings_search_->RequestFocus();
    settings_search_->SelectAll(false);
    return true;
  }
  if (!focused || !plain)
    return false;
  // Section rail: arrows move between sections.
  for (int pane = 0; pane < kPaneCount; ++pane) {
    if (focused != rail_[pane])
      continue;
    if (code == ui::VKEY_UP || code == ui::VKEY_DOWN) {
      SelectPane((pane + (code == ui::VKEY_UP ? kPaneCount - 1 : 1)) %
                     kPaneCount,
                 true);
      return true;
    }
    if (code == ui::VKEY_HOME || code == ui::VKEY_END) {
      SelectPane(code == ui::VKEY_HOME ? 0 : kPaneCount - 1, true);
      return true;
    }
    if (code == ui::VKEY_RIGHT || code == ui::VKEY_RETURN) {
      CycleMenuFocus(content_host_, false);
      return true;
    }
    return false;
  }
  if (focused == settings_search_ && code == ui::VKEY_DOWN) {
    CycleMenuFocus(content_host_, false);
    return true;
  }
  if (focused == search_ && code == ui::VKEY_DOWN) {
    MoveCell(0, 0);
    return true;
  }
  // Shortcut table.
  if (views::IsViewClass<KeyCell>(focused)) {
    switch (code) {
      case ui::VKEY_UP:
        MoveCell(-1, 0);
        return true;
      case ui::VKEY_DOWN:
        MoveCell(1, 0);
        return true;
      case ui::VKEY_LEFT:
        MoveCell(0, -1);
        return true;
      case ui::VKEY_RIGHT:
        MoveCell(0, 1);
        return true;
      case ui::VKEY_PRIOR:
        MoveCell(-10, 0);
        return true;
      case ui::VKEY_NEXT:
        MoveCell(10, 0);
        return true;
      case ui::VKEY_HOME:
        MoveCell(-static_cast<int>(row_actions_.size()), 0);
        return true;
      case ui::VKEY_END:
        MoveCell(static_cast<int>(row_actions_.size()), 0);
        return true;
      case ui::VKEY_DELETE:
      case ui::VKEY_BACK:
        if (selected_cell_ && commands_->CanEdit())
          WriteField(*selected_cell_, u"");
        return true;
      default:
        break;
    }
    // Typing filters the table; Space records like Enter.
    const char16_t character = event.GetCharacter();
    if (character > 0x20 && character != 0x7f && search_) {
      search_->RequestFocus();
      search_->SetSelectedRange(gfx::Range(search_->GetText().size()));
      // The key was aimed at the cell, which takes no text: type it here.
      search_->InsertOrReplaceText(std::u16string(1, character));
      return true;
    }
  }
  return false;
}

void KeyboardEditor::RequestClose() {
  if (!active())
    return;
  if (recording()) {
    StopRecording();
    SetStatus(u"Recording cancelled.");
    return;
  }
  if (KeyboardDirty() && commands_->CanEdit() && !confirm_discard_) {
    confirm_discard_ = true;
    SetStatus(u"Shortcut changes are not saved. Close again to discard "
              u"them, or press Ctrl+S.");
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
  if (more_runner_)
    more_runner_->Cancel();
  if (spelling_runner_)
    spelling_runner_->Cancel();
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&KeyboardEditor::FinishClose,
                                weak_factory_.GetWeakPtr(), restore_focus));
}

void KeyboardEditor::FinishClose(bool restore_focus) {
  auto contents = web_contents() ? web_contents()->GetWeakPtr() : nullptr;
  Observe(nullptr);
  ClearContent();
  rail_.clear();
  settings_search_ = nullptr;
  content_host_ = nullptr;
  status_ = nullptr;
  hints_ = nullptr;
  root_ = nullptr;
  source_tab_.clear();
  more_runner_.reset();
  more_model_.reset();
  spelling_runner_.reset();
  spelling_model_.reset();
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

// The window moved or was resized: Preferences follows it and keeps its
// section, scroll position, search, draft and focus.
void KeyboardEditor::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  if (!active())
    return;
  const gfx::Rect bounds = MenuWidgetBounds(desktop_);
  overlay_->SetBounds(bounds);
  PlacePanel(bounds);
}
// Preferences stays open while another application has the keyboard. Only
// the recorder stops: its keys would go to that application.
void KeyboardEditor::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active && recording()) {
    StopRecording();
    SetStatus(u"Recording cancelled.");
  }
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

// Export writes only the changes from the defaults, so a later import keeps
// newer defaults; Import reads that file back into the draft.
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
      std::vector<std::string> bindings;
      for (const auto& binding : SplitBindings(text))
        bindings.push_back(base::UTF16ToUTF8(binding));
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
    SetStatus(u"Installing " + path.BaseName().LossyDisplayName() + u"…");
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
                editor->SetStatus(
                    ok ? u"Exported, including unsaved changes."
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
    SetStatus(u"The bindings file must be smaller than 64 KB.");
    return;
  }
  const auto raw = base::JSONReader::ReadDict(*text, base::JSON_PARSE_RFC);
  if (!raw) {
    SetStatus(u"The file is not a keyboard bindings file.");
    return;
  }
  if (raw->FindInt("version") != 2) {
    SetStatus(u"Unsupported keyboard configuration version.");
    return;
  }
  const auto resolved = ResolveKeyboardChanges(*raw);
  if (!resolved.config) {
    SetStatus(base::UTF8ToUTF16(resolved.error));
    return;
  }
  for (const auto normal : {false, true})
    for (const auto& [action, bindings] :
         normal ? resolved.config->normal : resolved.config->shortcuts)
      draft_[{action, normal}] =
          base::UTF8ToUTF16(base::JoinString(bindings, "\n"));
  tab_modifiers_ = base::UTF8ToUTF16(resolved.config->tabs);
  if (tabs_)
    tabs_->SetText(tab_modifiers_);
  RefreshKeyboardRows();
  UpdateKeyboardState();
  SetStatus(u"Bindings imported. Review them, then save.");
}

// Testing.
views::View* KeyboardEditor::root_for_testing() {
  return root_;
}
views::Textfield* KeyboardEditor::settings_search_for_testing() {
  return settings_search_;
}
views::Textfield* KeyboardEditor::search_for_testing() {
  return search_;
}
views::Button* KeyboardEditor::search_save_for_testing() {
  return search_save_;
}
views::Button* KeyboardEditor::alias_save_for_testing() {
  return alias_save_;
}
views::Button* KeyboardEditor::alias_remove_for_testing(std::string_view alias) {
  const auto found = alias_remove_.find(base::ToLowerASCII(alias));
  return found == alias_remove_.end() ? nullptr : found->second.get();
}
views::Textfield* KeyboardEditor::color_field_for_testing(std::string_view key) {
  const auto found = color_fields_.find(std::string(key));
  return found == color_fields_.end() ? nullptr : found->second.get();
}
views::Button* KeyboardEditor::save_for_testing() {
  return save_;
}
FlySelect* KeyboardEditor::clock_format_for_testing() {
  return clock_format_;
}
FlySelect* KeyboardEditor::suspension_timeout_for_testing() {
  return suspension_timeout_;
}
std::u16string KeyboardEditor::bindings_for_testing(std::string_view action,
                                                    bool normal) {
  return DraftText({std::string(action), normal});
}
void KeyboardEditor::SetBindingsForTesting(std::string_view action,
                                           bool normal,
                                           std::u16string text) {
  WriteField({std::string(action), normal}, std::move(text));
}
views::Button* KeyboardEditor::record_for_testing(std::string_view action,
                                                  bool normal) {
  if (!active())
    return nullptr;
  const Key key{std::string(action), normal};
  if (pane_ != kKeyboardPane || !query_.empty())
    ShowPane(kKeyboardPane);
  if (!cells_.contains(key) && search_) {
    search_->SetText(u"");
    ContentsChanged(search_, u"");
  }
  SelectCell(key, true);
  return record_;
}
std::u16string KeyboardEditor::error_for_testing() const {
  return status_text_;
}
int KeyboardEditor::selected_pane_for_testing() const {
  return active() ? pane_ : -1;
}
bool KeyboardEditor::setting_visible_for_testing(
    std::u16string_view label) const {
  const auto needle = base::ToLowerASCII(std::u16string(label));
  for (const auto& row : rows_)
    if (row.text.starts_with(needle))
      return row.view->IsDrawn();
  return false;
}
}  // namespace fly
