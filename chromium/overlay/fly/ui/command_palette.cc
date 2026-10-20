// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/command_palette.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "components/input/native_web_keyboard_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/fly_theme.h"
#include "chrome/app/vector_icons/vector_icons.h"
#include "components/vector_icons/vector_icons.h"
#include "ui/aura/window.h"
#include "ui/color/color_id.h"
#include "ui/events/event.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"
#include "ui/wm/core/coordinate_conversion.h"

namespace fly {
namespace {
// One result row: title, optional secondary line (address, bookmark and
// history rows) and right-aligned tags (category and shortcuts).
class PickerButton : public FlyButton {
  METADATA_HEADER(PickerButton, FlyButton)

 public:
  PickerButton(PressedCallback callback,
               std::u16string_view title,
               std::u16string_view subtitle,
               const std::vector<std::u16string>& tags,
               bool first_tag_is_category)
      : FlyButton(std::move(callback), title, FlyFace::kSans, 13) {
    SetCustomPadding(gfx::Insets::VH(6, 10));
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    if (!subtitle.empty()) {
      subtitle_ = AddChildView(std::make_unique<FlyLabel>(
          std::u16string(subtitle), FlyFace::kMono, 10,
          gfx::Font::Weight::NORMAL, FlyLabel::Tone::kLink));
      subtitle_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
      subtitle_->SetElideBehavior(gfx::ELIDE_MIDDLE);
      subtitle_->SetCanProcessEventsWithinSubtree(false);
    }
    if (!tags.empty()) {
      tags_ = AddChildView(std::make_unique<views::View>());
      tags_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 4));
      tags_->SetCanProcessEventsWithinSubtree(false);
      for (size_t i = 0; i < tags.size(); ++i) {
        const bool category = first_tag_is_category && i == 0;
        auto* tag = tags_->AddChildView(std::make_unique<FlyLabel>(
            tags[i], FlyFace::kMono, category ? 9 : 10,
            gfx::Font::Weight::NORMAL,
            category ? FlyLabel::Tone::kAccent : FlyLabel::Tone::kText));
        tag->SetBorder(FlyBorder(FlyRole::kBorder, 1, 2,
                                 gfx::Insets::VH(1, 5)));
        if (!category)
          tag->SetBackground(FlyBackground(FlyRole::kCell, 2));
      }
    }
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    auto size = FlyButton::CalculatePreferredSize(available);
    size.set_height(subtitle_ ? 44 : 32);
    if (tags_)
      size.Enlarge(tags_->GetPreferredSize().width() + 12, 0);
    return size;
  }
  void SetHoverCallback(base::RepeatingClosure hover) {
    hover_ = std::move(hover);
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
    FlyButton::OnMouseEntered(event);
    if (hover_)
      hover_.Run();
  }
  void Layout(PassKey) override {
    LayoutSuperclass<FlyButton>(this);
    int right = width() - 10;
    if (tags_) {
      const auto size = tags_->GetPreferredSize();
      tags_->SetBounds(right - size.width(), (height() - size.height()) / 2,
                       size.width(), size.height());
      right = tags_->x() - 12;
    }
    auto title = label()->bounds();
    title.set_width(std::max(0, right - title.x()));
    if (subtitle_) {
      title.set_y(7);
      title.set_height(17);
      subtitle_->SetBounds(title.x(), 26, title.width(), 13);
    }
    label()->SetBoundsRect(title);
  }

 private:
  raw_ptr<FlyLabel> subtitle_ = nullptr;
  raw_ptr<views::View> tags_ = nullptr;
  base::RepeatingClosure hover_;
};
BEGIN_METADATA(PickerButton)
END_METADATA
// Fixed width; height follows the contents up to an optional maximum.
class SizedPanel : public views::View {
 public:
  SizedPanel(int width, int max_height) : width_(width), max_(max_height) {}
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds&) const override {
    const int height = GetLayoutManager()->GetPreferredHeightForWidth(
        this, width_);
    return gfx::Size(width_, max_ > 0 ? std::min(height, max_) : height);
  }

 private:
  const int width_;
  const int max_;
};
class DismissSurface : public views::View {
 public:
  explicit DismissSurface(base::RepeatingClosure dismiss)
      : dismiss_(std::move(dismiss)) {}
  bool OnMousePressed(const ui::MouseEvent& event) override {
    if (!children().empty() &&
        children().front()->bounds().Contains(event.location()))
      return false;
    dismiss_.Run();
    return true;
  }

 private:
  base::RepeatingClosure dismiss_;
};
}  // namespace
CommandPalette::CommandPalette(
    views::Widget* desktop,
    base::RepeatingCallback<bool(std::string_view)> available,
    base::RepeatingCallback<void(content::WebContents*, std::string)> execute,
    base::RepeatingCallback<void(content::WebContents*, std::string)> remove)
    : desktop_(desktop),
      available_(std::move(available)),
      execute_(std::move(execute)), remove_(std::move(remove)) {
  desktop_->AddObserver(this);
}
CommandPalette::~CommandPalette() {
  weak_factory_.InvalidateWeakPtrs();
  Observe(nullptr);
  buttons_.clear();
  remove_buttons_.clear();
  chips_.clear();
  count_ = nullptr;
  inspector_ = nullptr;
  search_ = nullptr;
  results_ = nullptr;
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void CommandPalette::Toggle(content::WebContents* contents,
                            const KeyboardConfig& config) {
  ToggleItems(contents, PaletteCatalog(config),
              config.shortcuts.at("toggle-palette"),
              style_.command_centre
                  ? u"Search commands, shortcuts, or type shell utility..."
                  : u"Search commands",
              false);
}
void CommandPalette::ToggleItems(content::WebContents* contents,
                                 std::vector<PaletteEntry> items,
                                 std::vector<std::string> close_bindings,
                                 std::u16string placeholder, bool removable,
                                 bool allow_empty_source) {
  if (overlay_) {
    Cancel(true);
    return;
  }
  if (!desktop_ || (!contents && !allow_empty_source))
    return;
  restore_page_focus_ = ShouldRestoreNativePageFocus(contents);
  Observe(contents);
  empty_source_ = !contents;
  if (contents)
    contents->StoreFocus();
  catalog_ = std::move(items);
  palette_bindings_ = std::move(close_bindings);
  removable_ = removable;
  closing_ = false;
  cancelled_ = false;
  overlay_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_CONTROL);
  params.parent = desktop_->GetNativeWindow();
  params.child = true;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.native_widget = new views::NativeWidgetAura(overlay_.get());
  params.name = "FlyCommandPalette";
  params.bounds = desktop_->GetContentsView()->GetBoundsInScreen();
  if (style_.layout == PaletteStyle::Layout::kPage && style_.page_insets)
    params.bounds.Inset(style_.page_insets.Run());
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &params.bounds);
  const auto bounds = params.bounds;
  overlay_->Init(std::move(params));
  auto root = std::make_unique<DismissSurface>(base::BindRepeating(
      &CommandPalette::Cancel, weak_factory_.GetWeakPtr(), true));
  const auto& colors = GetFlyColors();
  const bool top = style_.layout == PaletteStyle::Layout::kTop;
  const bool page = style_.layout == PaletteStyle::Layout::kPage;
  root->SetBackground(views::CreateSolidBackground(
      top ? SkColorSetARGB(1, 0, 0, 0)
          : SkColorSetA(colors.dark ? SK_ColorBLACK : colors.text,
                        page ? 0 : 150)));
  const int width = page ? std::max(200, std::min(style_.command_centre
                                                       ? style_.width
                                                       : bounds.width(),
                                                   bounds.width() - 32))
                         : std::min(style_.width, bounds.width() - 16);
  const int margin = std::max(8, (bounds.width() - width) / 2);
  const int vertical = page ? 16 : top ? 14 : 36;
  root->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      gfx::Insets::TLBR(vertical, margin, vertical, margin)));
  const int max_height = bounds.height() - 2 * vertical;
  std::unique_ptr<views::View> panel;
  if (top)
    panel = std::make_unique<SizedPanel>(width, max_height);
  else
    panel = std::make_unique<views::View>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical,
      page ? gfx::Insets::VH(18, 22) : gfx::Insets(12), 10));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  if (!top)
    panel->SetPreferredSize(gfx::Size(
        width, page ? max_height : std::max(80, std::min(640, max_height))));
  panel->SetBackground(FlyBackground(page ? FlyRole::kMain : FlyRole::kSidebar,
                                     4));
  panel->SetBorder(FlyBorder(FlyRole::kBorder, 1, 4));
  if (!style_.title.empty()) {
    auto* header = panel->AddChildView(std::make_unique<views::View>());
    auto* header_layout =
        header->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 8));
    header_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* titles = header->AddChildView(std::make_unique<views::View>());
    titles->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
    auto* title = titles->AddChildView(std::make_unique<FlyLabel>(
        style_.title, FlyFace::kSans, page ? 18 : 15,
        gfx::Font::Weight::SEMIBOLD,
        page ? FlyLabel::Tone::kAccent : FlyLabel::Tone::kText));
    title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    if (!style_.subtitle.empty()) {
      auto* subtitle = titles->AddChildView(std::make_unique<FlyLabel>(
          style_.subtitle, FlyFace::kSans, 11, gfx::Font::Weight::NORMAL,
          FlyLabel::Tone::kDim));
      subtitle->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    }
    header_layout->SetFlexForView(titles, 1);
    header->AddChildView(MakeFlyIconButton(
        base::BindRepeating(&CommandPalette::Cancel, weak_factory_.GetWeakPtr(),
                            true),
        vector_icons::kCloseIcon, u"Close", 26));
  }
  if (style_.command_centre) {
    search_ = nullptr;
    BuildCommandCentre(panel.get(), max_height);
    search_->SetPlaceholderText(placeholder);
    search_->GetViewAccessibility().SetName(placeholder);
    root->AddChildView(std::move(panel));
    overlay_->SetContentsView(std::move(root));
    Refresh();
    overlay_->ShowInactive();
    desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
    search_->RequestFocus();
    return;
  }
  search_ = panel->AddChildView(std::make_unique<views::Textfield>());
  search_->SetPlaceholderText(placeholder);
  search_->GetViewAccessibility().SetName(placeholder);
  search_->set_controller(this);
  search_->SetFontList(FlyFont(FlyFace::kSans, 13));
  search_->SetBackgroundColor(colors.bg_main);
  search_->SetBorder(FlyBorder(FlyRole::kAccent, 1, top ? 18 : 3,
                               gfx::Insets::VH(7, top ? 14 : 10)));
  search_->RemoveHoverEffect();
  auto* scroll = panel->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, page ? max_height : top ? 380 : 520);
  scroll->SetBackgroundColor(page ? colors.bg_main : colors.bg_sidebar);
  scroll->SetDrawOverflowIndicator(false);
  results_ = scroll->SetContents(std::make_unique<views::View>());
  results_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), 2));
  layout->SetFlexForView(scroll, 1);
  if (style_.layout == PaletteStyle::Layout::kCentre) {
    auto* hint = panel->AddChildView(std::make_unique<FlyLabel>(
        u"↑↓ Navigate    Enter Select    Esc Close", FlyFace::kMono,
        10, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
    hint->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  }
  root->AddChildView(std::move(panel));
  overlay_->SetContentsView(std::move(root));
  Refresh();
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  search_->RequestFocus();
}
void CommandPalette::UpdateItems(std::vector<PaletteEntry> items, bool removable) {
  if (!active())
    return;
  std::string selected;
  if (selected_ >= 0)
    selected = filtered_[selected_].action;
  catalog_ = std::move(items);
  removable_ = removable;
  Refresh();
  for (size_t i = 0; i < filtered_.size(); ++i) {
    if (filtered_[i].action == selected && buttons_[i]->GetEnabled()) {
      if (selected_ >= 0)
        buttons_[selected_]->SetIsDefault(false);
      selected_ = static_cast<int>(i);
      buttons_[i]->SetIsDefault(true);
      break;
    }
  }
}
void CommandPalette::ToggleQuery(content::WebContents* contents,
                                 QueryProvider provider,
                                 std::vector<std::string> close_bindings,
                                 std::u16string placeholder,
                                 std::u16string initial) {
  if (overlay_) {
    Cancel(true);
    return;
  }
  ToggleItems(contents, {}, std::move(close_bindings), std::move(placeholder),
              false);
  if (!active())
    return;
  query_provider_ = std::move(provider);
  search_->SetText(initial);
  search_->SelectAll(false);
  Refresh();
}
void CommandPalette::Refresh() {
  if (!active())
    return;
  if (style_.command_centre) {
    // Electron's filterPaletteCommands.
    const auto query = std::u16string(
        base::TrimWhitespace(search_->GetText(), base::TRIM_ALL));
    std::vector<PaletteEntry> candidates;
    for (const auto& entry : catalog_)
      if (category_ == "All" || entry.category == category_)
        candidates.push_back(entry);
    filtered_.clear();
    groups_.clear();
    if (query.empty() && category_ == "All") {
      for (const auto& id : recent_) {
        const auto found =
            std::ranges::find(catalog_, id, &PaletteEntry::action);
        if (found != catalog_.end()) {
          filtered_.push_back(*found);
          groups_.push_back("Recently Used");
        }
      }
    }
    for (auto& entry : FilterCommandCentre(candidates, query)) {
      groups_.push_back(entry.category);
      filtered_.push_back(std::move(entry));
    }
    if (filtered_.empty() && !query.empty()) {
      const auto text = base::UTF16ToUTF8(search_->GetText());
      PaletteEntry terminal{"terminal-fallback", "terminal:" + text,
                            "Run in terminal: \"" + text + "\"", "Terminal",
                            {"Enter"}, {}};
      terminal.desc = "Pass command to air-gapped terminal protocol on Enter";
      terminal.details =
          "Executes this input string in your external desktop terminal via "
          "the isolated oversight security protocol.";
      terminal.tip =
          "Press Enter or click Execute to copy and invoke the terminal "
          "command.";
      groups_.push_back("Terminal Oversight");
      filtered_.push_back(std::move(terminal));
    }
    const size_t count = filtered_.size();
    count_->SetText(query.empty()
                        ? base::NumberToString16(count) + u" commands"
                        : base::NumberToString16(count) +
                              (count == 1 ? u" match" : u" matches"));
    buttons_.clear();
    remove_buttons_.clear();
    results_->RemoveAllChildViews();
    selected_ = -1;
    std::string group;
    for (size_t i = 0; i < filtered_.size(); ++i) {
      const auto& entry = filtered_[i];
      if (groups_[i] != group) {
        group = groups_[i];
        auto* header = results_->AddChildView(std::make_unique<FlyLabel>(
            base::UTF8ToUTF16(group), FlyFace::kMono, 10,
            gfx::Font::Weight::SEMIBOLD, FlyLabel::Tone::kAccent));
        header->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        header->SetBorder(
            views::CreateEmptyBorder(gfx::Insets::TLBR(10, 10, 4, 10)));
      }
      std::vector<std::u16string> tags = {base::UTF8ToUTF16(entry.category)};
      for (const auto& shortcut : entry.shortcuts)
        tags.push_back(base::UTF8ToUTF16(shortcut));
      for (const auto& key : entry.normal)
        tags.push_back(base::UTF8ToUTF16(key) + u" (NORMAL)");
      auto* button = results_->AddChildView(std::make_unique<PickerButton>(
          base::BindRepeating(&CommandPalette::Activate,
                              weak_factory_.GetWeakPtr(), entry.action),
          base::UTF8ToUTF16(entry.title), std::u16string(), tags, true));
      button->SetHoverCallback(base::BindRepeating(
          &CommandPalette::Hover, weak_factory_.GetWeakPtr(), i));
      button->SetEnabled(entry.action.starts_with("terminal:") ||
                         available_.Run(entry.action));
      button->GetViewAccessibility().SetName(base::UTF8ToUTF16(entry.title));
      buttons_.push_back(button);
    }
    if (buttons_.empty()) {
      auto* empty = results_->AddChildView(std::make_unique<FlyLabel>(
          u"No matching results", FlyFace::kSans, 12,
          gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
      empty->SetBorder(views::CreateEmptyBorder(gfx::Insets(24)));
    }
    Select(1);
    UpdateInspector();
    return;
  }
  filtered_ = query_provider_ ? query_provider_.Run(search_->GetText())
                              : FilterPalette(catalog_, search_->GetText());
  buttons_.clear();
  remove_buttons_.clear();
  results_->RemoveAllChildViews();
  selected_ = -1;
  const bool secondary_line = remove_ || query_provider_;
  for (const auto& entry : filtered_) {
    auto text = entry.title;
    if (secondary_line && !entry.category.empty())
      text += "\n" + entry.category;
    if (!entry.shortcuts.empty())
      text += "    " + base::JoinString(entry.shortcuts, " / ");
    if (!entry.normal.empty())
      text += "    " + base::JoinString(entry.normal, " / ") + " (NORMAL)";
    std::vector<std::u16string> tags;
    if (!secondary_line && !entry.category.empty())
      tags.push_back(base::UTF8ToUTF16(entry.category));
    for (const auto& shortcut : entry.shortcuts)
      tags.push_back(base::UTF8ToUTF16(shortcut));
    for (const auto& key : entry.normal)
      tags.push_back(base::UTF8ToUTF16(key) + u" (NORMAL)");
    auto* row = results_->AddChildView(std::make_unique<views::View>());
    auto* row_layout = row->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
    row_layout->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kCenter);
    auto* button = row->AddChildView(std::make_unique<PickerButton>(
        base::BindRepeating(&CommandPalette::Activate,
                            weak_factory_.GetWeakPtr(), entry.action),
        base::UTF8ToUTF16(entry.title),
        secondary_line ? base::UTF8ToUTF16(entry.category) : std::u16string(),
        tags, !secondary_line && !entry.category.empty()));
    button->SetEnabled(available_.Run(entry.action));
    row_layout->SetFlexForView(button, 1);
    if (entry.indent > 0)
      row->SetBorder(views::CreateEmptyBorder(
          gfx::Insets::TLBR(0, 20 * entry.indent, 0, 0)));
    for (const auto& [action, label] : entry.row_actions) {
      auto* extra = row->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&CommandPalette::Activate,
                              weak_factory_.GetWeakPtr(), action),
          base::UTF8ToUTF16(label), FlyFace::kSans, 11));
      extra->SetOutlined(true);
      extra->SetEnabled(available_.Run(action));
      extra->GetViewAccessibility().SetName(base::UTF8ToUTF16(label) + u" " +
                                            base::UTF8ToUTF16(entry.title));
    }
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(text));
    buttons_.push_back(button);
    if (remove_) {
      auto* remove = row->AddChildView(std::make_unique<FlyButton>(
          base::BindRepeating(&CommandPalette::Remove, weak_factory_.GetWeakPtr(),
                              entry.action), u"Remove", FlyFace::kSans, 11));
      remove->SetOutlined(true);
      remove->SetEnabled(removable_ && available_.Run(entry.action));
      remove->GetViewAccessibility().SetName(u"Remove " + base::UTF8ToUTF16(text));
      remove_buttons_.push_back(remove);
    }
  }
  if (buttons_.empty()) {
    auto* empty = results_->AddChildView(std::make_unique<FlyLabel>(
        remove_ ? u"No bookmarks found." : u"No matching results",
        FlyFace::kSans, 12, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
    empty->SetBorder(views::CreateEmptyBorder(gfx::Insets(24)));
  }
  Select(1);
}
void CommandPalette::Select(int direction) {
  if (buttons_.empty())
    return;
  if (selected_ >= 0)
    buttons_[selected_]->SetIsDefault(false);
  const int count = static_cast<int>(buttons_.size());
  for (int i = 0; i < count; ++i) {
    selected_ = (selected_ + direction + count) % count;
    if (buttons_[selected_]->GetEnabled()) {
      buttons_[selected_]->SetIsDefault(true);
      buttons_[selected_]->ScrollViewToVisible();
      UpdateInspector();
      return;
    }
  }
  selected_ = -1;
  UpdateInspector();
}
void CommandPalette::RefreshQuery() {
  if (active() && query_provider_)
    UpdateItems(catalog_, removable_);
}
void CommandPalette::ContentsChanged(views::Textfield*, const std::u16string&) {
  Refresh();
}
bool CommandPalette::HandleKeyEvent(views::Textfield*,
                                    const ui::KeyEvent& event) {
  return HandleOverlayKey(event);
}
bool CommandPalette::HandleOverlayKey(const ui::KeyEvent& event) {
  if (event.type() != ui::EventType::kKeyPressed || search_->IsIMEComposing() ||
      event.IsAltGrDown())
    return false;
  const input::NativeWebKeyboardEvent native(event);
  const auto stroke = NativeEventStroke(native);
  if (event.key_code() == ui::VKEY_ESCAPE ||
      std::ranges::find(palette_bindings_, stroke) != palette_bindings_.end()) {
    Cancel(true);
    return true;
  }
  if (event.flags() &
      (ui::EF_CONTROL_DOWN | ui::EF_ALT_DOWN | ui::EF_COMMAND_DOWN))
    return false;
  if (remove_ && removable_ && selected_ >= 0 &&
      event.key_code() == ui::VKEY_DELETE &&
      (!search_->HasFocus() || (search_->GetSelectedRange().is_empty() &&
        search_->GetCursorPosition() == search_->GetText().size()))) {
    Remove(filtered_[selected_].action);
    return true;
  }
  if (!search_->HasFocus())
    return false;
  if (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP) {
    Select(event.key_code() == ui::VKEY_DOWN ? 1 : -1);
    return true;
  }
  if (remove_ && (event.key_code() == ui::VKEY_NEXT ||
                  event.key_code() == ui::VKEY_PRIOR)) {
    if (selected_ >= 0) {
      buttons_[selected_]->SetIsDefault(false);
      selected_ = std::clamp(selected_ + (event.key_code() == ui::VKEY_NEXT ? 5 : -5),
                             0, static_cast<int>(buttons_.size()) - 1);
      buttons_[selected_]->SetIsDefault(true);
      buttons_[selected_]->ScrollViewToVisible();
    }
    return true;
  }
  if (event.key_code() == ui::VKEY_RETURN) {
    if (selected_ >= 0)
      Activate(filtered_[selected_].action);
    return true;
  }
  return false;
}
void CommandPalette::Remove(std::string action) {
  if (!active() || !remove_ || !removable_ || !available_.Run(action))
    return;
  // An update may replace the row. Run only after the native input callback has
  // returned, then recheck surface lifetime and the current collection.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandPalette::RemoveAfterEvent,
                               weak_factory_.GetWeakPtr(), std::move(action)));
}
void CommandPalette::RemoveAfterEvent(std::string action) {
  if (active() && !cancelled_ && removable_ && web_contents() &&
      available_.Run(action))
    remove_.Run(web_contents(), std::move(action));
}
void CommandPalette::Activate(std::string action) {
  if (!active() ||
      (!action.starts_with("terminal:") && !available_.Run(action)))
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&CommandPalette::FinishClose, weak_factory_.GetWeakPtr(),
                     true, std::move(action)));
}
void CommandPalette::Cancel(bool restore_focus) {
  if (!overlay_)
    return;
  if (!restore_focus)
    cancelled_ = true;
  if (closing_)
    return;
  closing_ = true;
  overlay_->Hide();
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandPalette::FinishClose,
                                weak_factory_.GetWeakPtr(), restore_focus, ""));
}
void CommandPalette::FinishClose(bool restore_focus, std::string action) {
  auto contents = web_contents() ? web_contents()->GetWeakPtr() : nullptr;
  const bool source_was_empty = empty_source_;
  empty_source_ = false;
  Observe(nullptr);
  buttons_.clear();
  remove_buttons_.clear();
  chips_.clear();
  count_ = nullptr;
  inspector_ = nullptr;
  search_ = nullptr;
  results_ = nullptr;
  overlay_.reset();
  filtered_.clear();
  catalog_.clear();
  query_provider_.Reset();
  closing_ = false;
  if (restore_focus && contents && !cancelled_) {
    if (restore_page_focus_)
      contents->Focus();
    else
      contents->RestoreFocus();
  }
  if (!action.empty() && (contents || source_was_empty) && !cancelled_)
    execute_.Run(contents.get(), std::move(action));
}
void CommandPalette::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  Cancel(true);
}
void CommandPalette::OnWidgetActivationChanged(views::Widget*, bool active) {
  if (!active)
    Cancel();
}
void CommandPalette::OnWidgetDestroying(views::Widget*) {
  desktop_ = nullptr;
  Cancel();
}
void CommandPalette::PrimaryPageChanged(content::Page&) {
  Cancel();
}
void CommandPalette::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
void CommandPalette::WebContentsDestroyed() {
  Cancel();
}

// Electron's .palette-page: search with the match count, category chips, the
// grouped list and the inspector card side by side, and the footer.
void CommandPalette::BuildCommandCentre(views::View* panel, int max_height) {
  const auto& colors = GetFlyColors();
  auto* search_row = panel->AddChildView(std::make_unique<views::View>());
  auto* search_layout =
      search_row->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 10));
  search_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  search_ = search_row->AddChildView(std::make_unique<views::Textfield>());
  search_->set_controller(this);
  search_->SetFontList(FlyFont(FlyFace::kSans, 13));
  search_->SetBackgroundColor(colors.bg_main);
  search_->SetBorder(FlyBorder(FlyRole::kAccent, 1, 3, gfx::Insets::VH(7, 10)));
  search_->RemoveHoverEffect();
  search_layout->SetFlexForView(search_, 1);
  count_ = search_row->AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kMono, 11, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kDim));
  auto* chips = panel->AddChildView(std::make_unique<views::View>());
  chips->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 6));
  // Electron's chip labels; Performance & Memory shows as "Performance".
  for (const auto& category : CommandCentreCategories()) {
    std::u16string label = base::UTF8ToUTF16(category);
    if (category == "Performance & Memory")
      label = u"Performance";
    else if (category == "User Scripts & Styles")
      label = u"Scripts & Styles";
    else if (category == "Preferences & System")
      label = u"Preferences";
    auto* chip = chips->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(&CommandPalette::SetCategory,
                            weak_factory_.GetWeakPtr(), category),
        label, FlyFace::kSans, 11));
    chip->SetOutlined(true);
    chip->SetSelected(category == category_);
    chip->SetCustomPadding(gfx::Insets::VH(4, 12));
    chips_.push_back(chip);
  }
  auto* body = panel->AddChildView(std::make_unique<views::View>());
  auto* body_layout = body->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), 20));
  body_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  static_cast<views::BoxLayout*>(panel->GetLayoutManager())
      ->SetFlexForView(body, 1);
  auto* scroll = body->AddChildView(std::make_unique<views::ScrollView>());
  scroll->ClipHeightTo(0, max_height);
  scroll->SetBackgroundColor(colors.bg_sidebar);
  scroll->SetBorder(FlyBorder(FlyRole::kBorder, 1, 8));
  scroll->SetDrawOverflowIndicator(false);
  results_ = scroll->SetContents(std::make_unique<views::View>());
  results_->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(6), 2));
  body_layout->SetFlexForView(scroll, 125);
  inspector_ = body->AddChildView(std::make_unique<views::View>());
  inspector_->SetBackground(FlyBackground(FlyRole::kSidebar, 8));
  inspector_->SetBorder(FlyBorder(FlyRole::kBorder, 1, 8, gfx::Insets(22)));
  auto* inspector_layout =
      inspector_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical, gfx::Insets(), 16));
  inspector_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  body_layout->SetFlexForView(inspector_, 85);
  auto* footer = panel->AddChildView(std::make_unique<views::View>());
  auto* footer_layout =
      footer->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal,
          gfx::Insets::TLBR(10, 4, 4, 4), 16));
  footer_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  footer->SetBorder(FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0)));
  auto* keys = footer->AddChildView(std::make_unique<FlyLabel>(
      u"↑ ↓ Navigate    Enter Select    Esc Close", FlyFace::kMono, 11,
      gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  footer_layout->SetFlexForView(keys, 1);
  keys->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  footer->AddChildView(std::make_unique<FlyLabel>(
      u"Air-gapped terminal fallback enabled", FlyFace::kMono, 11,
      gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  auto* preferences = footer->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&CommandPalette::Activate, weak_factory_.GetWeakPtr(),
                          std::string("toggle-help")),
      u"Preferences", FlyFace::kSans, 11));
  preferences->SetOutlined(true);
}

void CommandPalette::SetCategory(std::string category) {
  if (!active())
    return;
  category_ = std::move(category);
  const auto& categories = CommandCentreCategories();
  for (size_t i = 0; i < chips_.size() && i < categories.size(); ++i)
    static_cast<FlyButton*>(chips_[i].get())
        ->SetSelected(categories[i] == category_);
  Refresh();
}

void CommandPalette::SetCategoryForTesting(std::string category) {
  SetCategory(std::move(category));
}

void CommandPalette::Hover(size_t index) {
  if (!active() || index >= buttons_.size() || static_cast<int>(index) == selected_)
    return;
  if (!buttons_[index]->GetEnabled())
    return;
  if (selected_ >= 0)
    buttons_[selected_]->SetIsDefault(false);
  selected_ = static_cast<int>(index);
  buttons_[selected_]->SetIsDefault(true);
  UpdateInspector();
}

// Electron's renderPaletteInspector.
void CommandPalette::UpdateInspector() {
  if (!inspector_)
    return;
  inspector_->RemoveAllChildViews();
  const auto section = [&](std::u16string label) {
    auto* caption = inspector_->AddChildView(std::make_unique<FlyLabel>(
        std::move(label), FlyFace::kMono, 10, gfx::Font::Weight::SEMIBOLD,
        FlyLabel::Tone::kDim));
    caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  };
  const auto text = [&](std::u16string value, FlyLabel::Tone tone) {
    auto* label = inspector_->AddChildView(std::make_unique<FlyLabel>(
        std::move(value), FlyFace::kSans, 12, gfx::Font::Weight::NORMAL,
        tone));
    label->SetMultiLine(true);
    label->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label->SetMaximumWidth(380);
    return label;
  };
  if (selected_ < 0 || selected_ >= static_cast<int>(filtered_.size())) {
    text(u"Select or hover over a command to view detailed feature "
         u"documentation, usage guidelines, and keyboard shortcuts.",
         FlyLabel::Tone::kDim);
    inspector_->InvalidateLayout();
    return;
  }
  const auto& entry = filtered_[selected_];
  const bool terminal = entry.action.starts_with("terminal:");
  auto* title = inspector_->AddChildView(std::make_unique<FlyLabel>(
      terminal ? u"Terminal Oversight" : base::UTF8ToUTF16(entry.title),
      FlyFace::kSans, 16, gfx::Font::Weight::BOLD));
  title->SetMultiLine(true);
  title->SetMaximumWidth(380);
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  auto* badge = inspector_->AddChildView(std::make_unique<FlyLabel>(
      terminal ? u"AIR-GAPPED SHELL"
               : base::UTF8ToUTF16(base::ToUpperASCII(
                     entry.category.empty() ? "General" : entry.category)),
      FlyFace::kMono, 10, gfx::Font::Weight::SEMIBOLD,
      FlyLabel::Tone::kAccent));
  badge->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  if (!terminal) {
    section(u"KEYBOARD SHORTCUT");
    std::vector<std::string> keys = entry.shortcuts;
    for (const auto& key : entry.normal)
      keys.push_back(key + " (NORMAL)");
    text(keys.empty() ? u"No shortcut assigned"
                      : base::UTF8ToUTF16(base::JoinString(keys, "  ·  ")),
         keys.empty() ? FlyLabel::Tone::kDim : FlyLabel::Tone::kAccent);
    text(base::UTF8ToUTF16(entry.action), FlyLabel::Tone::kDim);
  }
  section(u"FEATURE OVERVIEW");
  text(base::UTF8ToUTF16(!entry.details.empty() ? entry.details
                         : !entry.desc.empty()  ? entry.desc
                                                : std::string(
                                                      "No additional details "
                                                      "available.")),
       FlyLabel::Tone::kText);
  if (!entry.tip.empty()) {
    section(u"WORKFLOW TIP");
    text(base::UTF8ToUTF16(entry.tip), FlyLabel::Tone::kAccent);
  }
  auto* execute = inspector_->AddChildView(std::make_unique<FlyButton>(
      base::BindRepeating(&CommandPalette::Activate, weak_factory_.GetWeakPtr(),
                          entry.action),
      u"Execute Action", FlyFace::kSans, 12));
  execute->SetSelected(true);
  inspector_->InvalidateLayout();
}

std::u16string CommandPalette::inspector_title_for_testing() const {
  if (!inspector_ || inspector_->children().empty())
    return std::u16string();
  return std::u16string(
      static_cast<views::Label*>(inspector_->children().front())->GetText());
}

std::u16string CommandPalette::count_for_testing() const {
  return count_ ? std::u16string(count_->GetText()) : std::u16string();
}
}  // namespace fly
