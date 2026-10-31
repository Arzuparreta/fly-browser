// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/command_palette.h"
#include "fly/ui/new_tab_ui.h"

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/fly_menu.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/native_commands.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/text_elider.h"
#include "ui/gfx/text_utils.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/border.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/controls/textfield/textfield.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"
#include <algorithm>
#include <utility>

namespace fly {
namespace {
int S(int pixels) {
  return FlyScaled(pixels);
}

std::vector<std::u16string> ToUTF16(const std::vector<std::string>& values) {
  std::vector<std::u16string> result;
  for (const auto& value : values)
    result.push_back(base::UTF8ToUTF16(value));
  return result;
}

// One result row, painted in a single view: the title (the button's label),
// an optional second line, a dim detail and the key chips at the right.
class PickerButton : public FlyButton {
  METADATA_HEADER(PickerButton, FlyButton)

 public:
  PickerButton(PressedCallback callback,
               std::u16string_view title,
               std::u16string subtitle,
               std::u16string detail,
               std::vector<std::u16string> keys,
               std::vector<std::u16string> normal_keys,
               int indent)
      : FlyButton(std::move(callback), title, FlyFace::kSans, kMenuTextSize),
        subtitle_(std::move(subtitle)),
        detail_(std::move(detail)),
        keys_(std::move(keys)),
        normal_keys_(std::move(normal_keys)),
        indent_(indent) {
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    SetCustomPadding(gfx::Insets());
    SetCornerRadius(S(6));
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds&) const override {
    const int text_height = label()->GetPreferredSize().height();
    const int content_height =
        subtitle_.empty()
            ? text_height
            : text_height + S(3) +
                  FlyFont(FlyFace::kSans, kMenuSmallTextSize).GetHeight();
    return gfx::Size(S(200), std::max(S(subtitle_.empty() ? kMenuRowHeight
                                                 : kMenuTallRowHeight),
                                      content_height + S(16)));
  }
  void SetHoverCallback(base::RepeatingClosure hover) {
    hover_ = std::move(hover);
  }
  gfx::Rect TitleBoundsForTesting() const { return label()->bounds(); }
  SkColor SubtitleColor() const {
    const auto &colors = GetFlyColors();
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    const auto background =
        color_utils::AlphaBlend(colors.text, colors.bg_sidebar,
                                static_cast<SkAlpha>(selected() ? 22
                                : hot && GetEnabled() ? 12
                                                      : 0));
    return color_utils::BlendForMinContrast(colors.dim, background).color;
  }
  // Rows that can be dragged (dashboard tabs) report the pointer while
  // dragging and where they were dropped, in screen coordinates.
  void SetDragCallbacks(base::RepeatingCallback<void(gfx::Point)> dragging,
                        base::RepeatingCallback<void(gfx::Point)> dropped) {
    dragging_ = std::move(dragging);
    dropped_ = std::move(dropped);
  }
  bool OnMousePressed(const ui::MouseEvent& event) override {
    press_point_ = event.location();
    drag_moved_ = false;
    return FlyButton::OnMousePressed(event);
  }
  bool OnMouseDragged(const ui::MouseEvent& event) override {
    if (dropped_ && event.IsOnlyLeftMouseButton() &&
        (drag_moved_ ||
         views::View::ExceededDragThreshold(event.location() - press_point_))) {
      drag_moved_ = true;
      SetState(STATE_PRESSED);
      dragging_.Run(ScreenPoint(event));
      return true;
    }
    return FlyButton::OnMouseDragged(event);
  }
  void OnMouseReleased(const ui::MouseEvent& event) override {
    if (drag_moved_) {
      drag_moved_ = false;
      SetState(STATE_NORMAL);
      dropped_.Run(ScreenPoint(event));
      return;
    }
    FlyButton::OnMouseReleased(event);
  }
  void OnMouseCaptureLost() override {
    if (drag_moved_) {
      drag_moved_ = false;
      dropped_.Run(gfx::Point(-1, -1));
    }
    FlyButton::OnMouseCaptureLost();
  }
  void OnMouseEntered(const ui::MouseEvent& event) override {
    FlyButton::OnMouseEntered(event);
    if (hover_)
      hover_.Run();
  }
  void Layout(PassKey) override {
    LayoutSuperclass<FlyButton>(this);
    const int left = S(12) + S(20) * indent_;
    const int right = TrailingLeft() - S(12);
    const int line = label()->GetPreferredSize().height();
    if (subtitle_.empty()) {
      label()->SetBounds(left, (height() - line) / 2,
                         std::max(0, right - left), line);
    } else {
      label()->SetBounds(left, S(8), std::max(0, right - left), line);
    }
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    gfx::RectF bounds(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (selected()) {
      flags.setColor(SkColorSetA(colors.text, 22));
      canvas->DrawRoundRect(bounds, S(6), flags);
      flags.setColor(colors.accent);
      canvas->DrawRoundRect(
          gfx::RectF(0, S(7), S(3), std::max(0, height() - S(14))), S(2),
          flags);
    } else if (hot && GetEnabled()) {
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
    const int centre = height() / 2;
    int right = width() - S(12);
    if (!normal_keys_.empty())
      right = PaintKeyChipsRightAligned(canvas, width() / 2, right, centre,
                                        normal_keys_, ChipTone::kQuiet) -
              S(kKeyChipGap);
    if (!keys_.empty())
      right = PaintKeyChipsRightAligned(canvas, width() / 2, right, centre,
                                        keys_) -
              S(kKeyChipGap);
    const auto small = FlyFont(FlyFace::kSans, kMenuSmallTextSize);
    if (!detail_.empty()) {
      const int width = std::min(gfx::GetStringWidth(detail_, small),
                                 this->width() / 3);
      canvas->DrawStringRectWithFlags(
          gfx::ElideText(detail_, small, width, gfx::ELIDE_TAIL), small,
          SubtitleColor(), gfx::Rect(right - width - S(4), 0, width, height()),
          gfx::Canvas::TEXT_ALIGN_RIGHT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
    }
    if (!subtitle_.empty()) {
      const auto title = label()->bounds();
      const int line = small.GetHeight();
      canvas->DrawStringRectWithFlags(
          gfx::ElideText(subtitle_, small, title.width(), gfx::ELIDE_MIDDLE),
          small, SubtitleColor(),
          gfx::Rect(title.x(), title.bottom() + S(3), title.width(), line),
          gfx::Canvas::NO_SUBPIXEL_RENDERING);
    }
  }

 private:
  // Left edge of the detail and chips.
  int TrailingLeft() const {
    int used = 0;
    const int gap = S(kKeyChipGap);
    for (const auto* keys : {&keys_, &normal_keys_})
      for (const auto& key : *keys)
        used += KeyChipWidth(key) + gap;
    if (!detail_.empty())
      used += std::min(gfx::GetStringWidth(
                           detail_, FlyFont(FlyFace::kSans, kMenuSmallTextSize)),
                       width() / 3) +
              S(8);
    return std::max(width() / 2, width() - S(12) - used);
  }
  gfx::Point ScreenPoint(const ui::MouseEvent& event) {
    gfx::Point point = event.location();
    views::View::ConvertPointToScreen(this, &point);
    return point;
  }
  const std::u16string subtitle_;
  const std::u16string detail_;
  const std::vector<std::u16string> keys_;
  const std::vector<std::u16string> normal_keys_;
  const int indent_;
  base::RepeatingClosure hover_;
  base::RepeatingCallback<void(gfx::Point)> dragging_;
  base::RepeatingCallback<void(gfx::Point)> dropped_;
  gfx::Point press_point_;
  bool drag_moved_ = false;
};
BEGIN_METADATA(PickerButton)
END_METADATA

std::unique_ptr<views::View> MakeEmptyState(std::u16string text) {
  auto empty = std::make_unique<FlyLabel>(std::move(text), FlyFace::kSans,
                                          kMenuTextSize,
                                          gfx::Font::Weight::NORMAL,
                                          FlyLabel::Tone::kDim);
  empty->SetBorder(views::CreateEmptyBorder(gfx::Insets::VH(S(28), S(12))));
  return empty;
}
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
  button_actions_.clear();
  drop_target_ = -1;
  remove_buttons_.clear();
  chips_.clear();
  count_ = nullptr;
  hints_ = nullptr;
  inspector_ = nullptr;
  search_ = nullptr;
  results_ = nullptr;
  results_scroll_ = nullptr;
  scrim_ = nullptr;
  source_tab_.clear();
  ResetBookmarksUI();
  overlay_.reset();
  if (desktop_)
    desktop_->RemoveObserver(this);
}
void CommandPalette::Toggle(content::WebContents* contents,
                            const KeyboardConfig& config) {
  ToggleItems(contents, PaletteCatalog(config),
              config.shortcuts.at("toggle-palette"),
              style_.command_centre
                  ? u"Search commands and shortcuts"
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
  source_tab_ = WatchMenuSourceTab(
      contents, base::BindRepeating(&CommandPalette::Cancel,
                                    weak_factory_.GetWeakPtr(), false));
  empty_source_ = !contents;
  if (contents)
    contents->StoreFocus();
  catalog_ = std::move(items);
  palette_bindings_ = std::move(close_bindings);
  removable_ = removable;
  closing_ = false;
  cancelled_ = false;
  pointer_at_open_ = aura::Env::GetInstance()->last_mouse_location();
  pointer_moved_ = false;
  const gfx::Rect bounds = SurfaceBounds();
  overlay_ = CreateMenuWidget(desktop_, bounds, "FlyCommandPalette");
  const auto& colors = GetFlyColors();
  const bool top = style_.layout == PaletteStyle::Layout::kTop;
  auto root = std::make_unique<MenuScrim>(
      top ? SkColorSetARGB(1, 0, 0, 0)
          : SkColorSetA(colors.dark ? SK_ColorBLACK : colors.text,
                        colors.dark ? 120 : 60),
      base::BindRepeating(&CommandPalette::Cancel, weak_factory_.GetWeakPtr(),
                          true));
  auto panel = std::make_unique<MenuPanel>();
  if (bookmarks_) {
    panel->GetViewAccessibility().SetRole(ax::mojom::Role::kDialog);
    panel->GetViewAccessibility().SetName(u"Bookmarks");
    panel->GetViewAccessibility().SetIsModal(true);
  }
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(S(10)), S(8)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* header = panel->AddChildView(
      std::make_unique<MenuHeader>(style_.kind, style_.title));
  header_ = header;
  if (bookmarks_) {
    header->SetFlexibleTitle();
    count_ = header->AddStatus();
    bookmark_add_ = header->AddTrailing(
        MakeMenuButton(base::BindRepeating(&CommandPalette::EditBookmark,
                                           weak_factory_.GetWeakPtr(), ""),
                       u"Add"));
    bookmark_add_->GetViewAccessibility().SetName(u"Add bookmark (Ctrl+N)");
    bookmark_add_->SetTooltipText(u"Add bookmark (Ctrl+N)");
    bookmark_add_->SetEnabled(bookmarks_->CanEdit());
    search_ = panel->AddChildView(std::make_unique<FlyTextfield>(placeholder));
    static_cast<FlyTextfield *>(search_.get())->SetSearchStyle(true);
  } else {
    search_ = header->SetSearch(std::make_unique<FlyTextfield>(placeholder));
  }
  search_->set_controller(this);
  if (bookmarks_) {
    search_->GetViewAccessibility().SetRole(
        ax::mojom::Role::kTextFieldWithComboBox);
    search_->GetViewAccessibility().SetHasPopup(ax::mojom::HasPopup::kListbox);
    search_->GetViewAccessibility().SetAutoComplete("list");
    search_->GetViewAccessibility().SetIsExpanded();
  }
  if (!bookmarks_)
    count_ = style_.kind == MenuKind::kAddress ? nullptr : header->AddStatus();
  if (style_.layout != PaletteStyle::Layout::kTop)
    header->AddClose(base::BindRepeating(&CommandPalette::Cancel,
                                         weak_factory_.GetWeakPtr(), true));
  if (style_.command_centre)
    BuildCommandCentre(panel.get());
  auto* scroll = panel->AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetDrawOverflowIndicator(false);
  results_scroll_ = scroll;
  results_ = scroll->SetContents(std::make_unique<views::View>());
  scroll_subscription_ = scroll->AddContentsScrolledCallback(
      base::BindRepeating(&CommandPalette::OnResultsScrolled,
                          weak_factory_.GetWeakPtr()));
  layout->SetFlexForView(scroll, 1);
  if (style_.command_centre) {
    // The selected command's details sit under the list.
    inspector_ = panel->AddChildView(std::make_unique<views::View>());
    auto* detail = inspector_->SetLayoutManager(
        std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical,
            gfx::Insets::TLBR(S(8), S(12), S(2), S(12)), S(2)));
    detail->set_cross_axis_alignment(
        views::BoxLayout::CrossAxisAlignment::kStretch);
    inspector_->SetBorder(
        FlyEdgeBorder(FlyRole::kBorder, gfx::Insets::TLBR(1, 0, 0, 0)));
  }
  if (bookmarks_) {
    bookmark_editor_scroll_ =
        panel->AddChildView(std::make_unique<views::ScrollView>());
    bookmark_editor_scroll_->SetHorizontalScrollBarMode(
        views::ScrollView::ScrollBarMode::kDisabled);
    bookmark_editor_scroll_->SetBackgroundColor(std::nullopt);
    bookmark_editor_scroll_->SetDrawOverflowIndicator(false);
    bookmark_editor_ =
        bookmark_editor_scroll_->SetContents(std::make_unique<views::View>());
    bookmark_editor_scroll_->SetVisible(false);
    layout->SetFlexForView(bookmark_editor_scroll_, 1);
    auto *feedback = panel->AddChildView(std::make_unique<views::View>());
    auto *feedback_layout =
        feedback->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kHorizontal,
            gfx::Insets::VH(S(4), S(8)), S(8)));
    bookmark_status_ = feedback->AddChildView(std::make_unique<FlyLabel>(
        u"", FlyFace::kSans, kMenuSmallTextSize,
        gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
    bookmark_status_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    bookmark_status_->SetElideBehavior(gfx::ELIDE_TAIL);
    bookmark_status_->GetViewAccessibility().SetLiveRegionContainer(
        views::ViewAccessibility::LiveRegionStatus::kPolite);
    feedback_layout->SetFlexForView(bookmark_status_, 1);
    bookmark_undo_ = feedback->AddChildView(
        MakeMenuButton(base::BindRepeating(&CommandPalette::UndoBookmark,
                                           weak_factory_.GetWeakPtr()),
                       u"Undo delete", MenuButtonStyle::kQuiet));
    bookmark_undo_->SetVisible(false);
  }
  hints_ = panel->AddChildView(std::make_unique<KeyHints>(
      std::vector<KeyHints::Hint>()));
  hints_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, S(2), 0, 0)));
  UpdateHints();
  root->SetPanel(std::move(panel));
  scrim_ = root.get();
  PlacePanel(bounds);
  DisableSubpixelText(root.get());
  Refresh();
  overlay_->SetContentsView(std::move(root));
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  search_->RequestFocus();
}
gfx::Rect CommandPalette::SurfaceBounds() const {
  return MenuWidgetBounds(
      desktop_, style_.layout == PaletteStyle::Layout::kPage &&
                        style_.page_insets
                    ? style_.page_insets.Run()
                    : gfx::Insets());
}
// Panel geometry: compact cards grow with their results, managers and the
// Command Centre keep one size so the list does not jump while filtering.
void CommandPalette::PlacePanel(const gfx::Rect& bounds) {
  const bool top = style_.layout == PaletteStyle::Layout::kTop;
  const bool page = style_.layout == PaletteStyle::Layout::kPage;
  const int width = std::min(S(style_.width), bounds.width() - S(24));
  if (bookmarks_ && count_)
    count_->SetVisible(width >= S(430));
  const int x = (bounds.width() - width) / 2;
  const int top_margin = top ? S(12) : page ? S(20) : std::max(S(24), bounds.height() / 9);
  const int max_height = std::max(
      S(120), (page ? bounds.height() - 2 * top_margin
                    : std::min(bounds.height() - top_margin - S(24),
                               S(style_.command_centre ? 600 : 520))));
  results_scroll_->ClipHeightTo(0, max_height);
  if (bookmark_editor_scroll_)
    bookmark_editor_scroll_->ClipHeightTo(0, max_height);
  // Rows are built for what the panel can show and a margin; scrolling and
  // keyboard selection build the rest as they reach it.
  initial_rows_ = static_cast<size_t>(max_height / S(kMenuRowHeight)) + 4;
  if (top || !(page || style_.command_centre))
    scrim_->SetPanelGrowsTo(gfx::Rect(x, top_margin, width,
                                      max_height + S(140)));
  else
    scrim_->SetPanelBounds(gfx::Rect(
        x, top_margin, width,
        page ? bounds.height() - 2 * top_margin
             : std::min(bounds.height() - top_margin - S(24),
                        max_height + S(150))));
}
void CommandPalette::UpdateHints() {
  if (!hints_)
    return;
  if (editing_url_) {
    hints_->SetHints(
        {{u"Enter", u"Save"}, {u"Tab", u"Next field"}, {u"Esc", u"Cancel"}});
    return;
  }
  std::vector<KeyHints::Hint> hints = {{u"↑↓", u"Move"},
                                       {u"Enter", style_.enter_label}};
  if (style_.command_centre)
    hints.push_back({u"Tab", u"Category"});
  if (bookmarks_ && removable_) {
    hints.push_back({u"F2", u"Edit"});
    hints.push_back({u"Alt ↑↓", u"Reorder"});
  } else if (remove_ && removable_)
    hints.push_back({u"Del", u"Remove"});
  if (drop_ && draggable_)
    hints.push_back({u"Drag", u"Move tab"});
  hints.push_back({u"Esc", u"Close"});
  hints_->SetHints(std::move(hints));
}
void CommandPalette::UpdateItems(std::vector<PaletteEntry> items, bool removable) {
  if (!active())
    return;
  std::string selected;
  if (selected_ >= 0)
    selected = filtered_[selected_].action;
  catalog_ = std::move(items);
  removable_ = removable;
  if (bookmark_add_)
    bookmark_add_->SetEnabled(removable && !editing_url_);
  if (bookmark_save_)
    bookmark_save_->SetEnabled(removable);
  UpdateHints();
  Refresh();
  for (size_t i = 0; i < filtered_.size(); ++i) {
    if (filtered_[i].action != selected)
      continue;
    EnsureRows(i + 1);
    if (buttons_[i]->GetEnabled()) {
      if (selected_ >= 0)
        SetRowSelected(selected_, false);
      selected_ = static_cast<int>(i);
      SetRowSelected(selected_, true);
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
              false, true);
  if (!active())
    return;
  // An explicitly opened address field is usable during the initial new-tab
  // navigation. Preserve its draft when that particular page commits.
  keep_initial_new_tab_ = contents && contents->IsLoading() &&
                          IsFlyNewTabPage(contents->GetVisibleURL());
  query_provider_ = std::move(provider);
  search_->SetText(initial);
  search_->SelectAll(false);
  Refresh();
}
void CommandPalette::Refresh() {
  if (!active())
    return;
  buttons_.clear();
  button_actions_.clear();
  remove_buttons_.clear();
  drop_target_ = -1;
  selected_ = -1;
  last_group_.clear();
  // Build the rows off the widget and attach them once.
  auto rows = std::make_unique<views::View>();
  rows->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(), S(1)));
  rows->GetViewAccessibility().SetRole(ax::mojom::Role::kListBox);
  rows->GetViewAccessibility().SetName(style_.title.empty() ? u"Results"
                                                            : style_.title);
  if (search_)
    search_->GetViewAccessibility().ClearActiveDescendant();
  results_ = nullptr;
  results_scroll_->SetContents(std::make_unique<views::View>());
  results_ = rows.get();
  const auto query = std::u16string(
      base::TrimWhitespace(search_->GetText(), base::TRIM_ALL));
  if (style_.command_centre) {
    // The chosen category, then the query (FilterCommandCentre).
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
          groups_.push_back("Recently used");
        }
      }
    }
    auto matches = FilterCommandCentre(candidates, query);
    // Without a search, keep each category together (first appearance order);
    // a search keeps its ranking.
    if (query.empty()) {
      std::vector<std::string> order;
      for (const auto& entry : matches)
        if (std::ranges::find(order, entry.category) == order.end())
          order.push_back(entry.category);
      std::ranges::stable_sort(matches, {}, [&](const PaletteEntry& entry) {
        return std::ranges::find(order, entry.category) - order.begin();
      });
    }
    for (auto& entry : matches) {
      groups_.push_back(entry.category);
      filtered_.push_back(std::move(entry));
    }
    if (filtered_.empty() && !query.empty()) {
      const auto text = base::UTF16ToUTF8(search_->GetText());
      PaletteEntry terminal{"terminal-fallback", "terminal:" + text,
                            "Run in terminal: \"" + text + "\"", "Terminal",
                            {}, {}};
      terminal.desc = "Copies the command and runs it in your desktop terminal.";
      groups_.push_back("Terminal");
      filtered_.push_back(std::move(terminal));
    }
    const size_t count = filtered_.size();
    count_->SetText(query.empty()
                        ? base::NumberToString16(count) + u" commands"
                        : base::NumberToString16(count) +
                              (count == 1 ? u" match" : u" matches"));
    EnsureRows(initial_rows_);
    if (buttons_.empty())
      results_->AddChildView(MakeEmptyState(u"No commands match."));
    DisableSubpixelText(results_);
    results_scroll_->SetContents(std::move(rows));
    Select(1);
    UpdateInspector();
    return;
  }
  filtered_ = query_provider_ ? query_provider_.Run(search_->GetText())
                              : FilterPalette(catalog_, search_->GetText());
  EnsureRows(initial_rows_);
  if (count_)
    count_->SetText(filtered_.empty()
                        ? std::u16string()
                        : base::NumberToString16(filtered_.size()) +
                              (bookmarks_ ? u" saved" : u""));
  if (buttons_.empty() && !(style_.layout == PaletteStyle::Layout::kTop &&
                            query.empty() && query_provider_))
    results_->AddChildView(MakeEmptyState(query.empty() ? style_.empty_text
                                                        : u"No matches."));
  DisableSubpixelText(results_);
  results_scroll_->SetContents(std::move(rows));
  Select(1);
}
void CommandPalette::EnsureRows(size_t count) {
  if (!results_)
    return;
  count = std::min(count, filtered_.size());
  while (buttons_.size() < count)
    AppendRow(buttons_.size());
}
void CommandPalette::ScrollSelectedIntoView() {
  if (selected_ < 0 || static_cast<size_t>(selected_) >= buttons_.size())
    return;
  // A row built just now has no bounds until the next layout.
  if (auto* widget = buttons_[selected_]->GetWidget())
    widget->LayoutRootViewIfNecessary();
  buttons_[selected_]->ScrollViewToVisible();
}
void CommandPalette::OnResultsScrolled() {
  if (!results_ || buttons_.size() >= filtered_.size())
    return;
  const auto visible = results_scroll_->GetVisibleRect();
  if (visible.bottom() + 2 * S(kMenuTallRowHeight) >= results_->height())
    EnsureRows(buttons_.size() + initial_rows_);
}
void CommandPalette::AppendRow(size_t i) {
  const auto& entry = filtered_[i];
  views::View* row = nullptr;
  FlyButton* added = nullptr;
  if (style_.command_centre) {
    if (groups_[i] != last_group_) {
      last_group_ = groups_[i];
      auto* header = results_->AddChildView(
          MakeMenuCaption(base::UTF8ToUTF16(last_group_)));
      header->SetBorder(views::CreateEmptyBorder(
          gfx::Insets::TLBR(i ? S(10) : S(2), S(12), S(4), S(12))));
    }
    auto* button = results_->AddChildView(std::make_unique<PickerButton>(
        base::BindRepeating(&CommandPalette::ActivateRow,
                            weak_factory_.GetWeakPtr(), entry.action),
        base::UTF8ToUTF16(entry.title), std::u16string(), std::u16string(),
        ToUTF16(entry.shortcuts), ToUTF16(entry.normal), 0));
    button->SetHoverCallback(base::BindRepeating(
        &CommandPalette::Hover, weak_factory_.GetWeakPtr(), i));
    button->SetEnabled(entry.action.starts_with("terminal:") ||
                       available_.Run(entry.action));
    button->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    button->GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(entry.title));
    button_actions_.push_back(entry.action);
    buttons_.push_back(button);
    row = button;
    added = button;
  } else {
    const bool secondary_line = remove_ || query_provider_;
    auto text = entry.title;
    if (secondary_line && !entry.category.empty())
      text += "\n" + entry.category;
    if (!entry.shortcuts.empty())
      text += "    " + base::JoinString(entry.shortcuts, " / ");
    if (!entry.normal.empty())
      text += "    " + base::JoinString(entry.normal, " / ") + " (NORMAL)";
    const bool extras = !entry.row_actions.empty() || remove_;
    views::View* parent = results_;
    if (extras) {
      parent = results_->AddChildView(std::make_unique<views::View>());
      auto* row_layout =
          parent->SetLayoutManager(std::make_unique<views::BoxLayout>(
              views::BoxLayout::Orientation::kHorizontal, gfx::Insets(),
              S(4)));
      row_layout->set_cross_axis_alignment(
          views::BoxLayout::CrossAxisAlignment::kCenter);
    }
    auto* button = parent->AddChildView(std::make_unique<PickerButton>(
        base::BindRepeating(&CommandPalette::Activate,
                            weak_factory_.GetWeakPtr(), entry.action),
        base::UTF8ToUTF16(entry.title),
        secondary_line ? base::UTF8ToUTF16(entry.category) : std::u16string(),
        secondary_line ? std::u16string() : base::UTF8ToUTF16(entry.category),
        ToUTF16(entry.shortcuts), ToUTF16(entry.normal), entry.indent));
    if (bookmarks_)
      button->SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON |
                                       ui::EF_MIDDLE_MOUSE_BUTTON);
    button->SetEnabled(available_.Run(entry.action));
    button->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    button->SetHoverCallback(base::BindRepeating(
        &CommandPalette::Hover, weak_factory_.GetWeakPtr(), i));
    if (extras)
      static_cast<views::BoxLayout*>(parent->GetLayoutManager())
          ->SetFlexForView(button, 1);
    for (const auto& [action, label] : entry.row_actions) {
      auto* extra = parent->AddChildView(MakeMenuButton(
          base::BindRepeating(&CommandPalette::Activate,
                              weak_factory_.GetWeakPtr(), action),
          base::UTF8ToUTF16(label), MenuButtonStyle::kQuiet));
      extra->SetEnabled(available_.Run(action));
      extra->GetViewAccessibility().SetName(base::UTF8ToUTF16(label) + u" " +
                                            base::UTF8ToUTF16(entry.title));
    }
    button->GetViewAccessibility().SetRole(ax::mojom::Role::kListBoxOption);
    button->GetViewAccessibility().SetName(base::UTF8ToUTF16(text));
    button->SetTooltipText(base::UTF8ToUTF16(text));
    if (drop_ && draggable_ && draggable_.Run(entry.action)) {
      button->SetDragCallbacks(
          base::BindRepeating(&CommandPalette::DragOver,
                              weak_factory_.GetWeakPtr()),
          base::BindRepeating(&CommandPalette::Drop,
                              weak_factory_.GetWeakPtr(), entry.action));
    }
    button_actions_.push_back(entry.action);
    buttons_.push_back(button);
    if (remove_) {
      auto* remove = parent->AddChildView(MakeMenuButton(
          bookmarks_
              ? base::BindRepeating(&CommandPalette::EditBookmark,
                                    weak_factory_.GetWeakPtr(), entry.action)
              : base::BindRepeating(&CommandPalette::Remove,
                              weak_factory_.GetWeakPtr(), entry.action),
          bookmarks_ ? u"Edit" : u"Remove", MenuButtonStyle::kQuiet));
      remove->SetEnabled(removable_ && available_.Run(entry.action));
      remove->GetViewAccessibility().SetName(
          (bookmarks_ ? u"Edit " : u"Remove ") +
                                             base::UTF8ToUTF16(text));
      if (bookmarks_)
        remove->SetFocusBehavior(views::View::FocusBehavior::NEVER);
      remove_buttons_.push_back(remove);
    }
    row = parent;
    added = button;
  }
  // Rows that are not built yet still count for assistive technology.
  added->GetViewAccessibility().SetPosInSet(static_cast<int>(i) + 1);
  added->GetViewAccessibility().SetSetSize(static_cast<int>(filtered_.size()));
  DisableSubpixelText(row);
}
void CommandPalette::Select(int direction) {
  if (buttons_.empty())
    return;
  if (selected_ >= 0)
    SetRowSelected(selected_, false);
  const int count = static_cast<int>(filtered_.size());
  for (int i = 0; i < count; ++i) {
    selected_ = (selected_ + direction + count) % count;
    EnsureRows(static_cast<size_t>(selected_) + 1);
    if (buttons_[selected_]->GetEnabled()) {
      SetRowSelected(selected_, true);
      ScrollSelectedIntoView();
      UpdateInspector();
      return;
    }
  }
  selected_ = -1;
  UpdateInspector();
}
void CommandPalette::SetRowSelected(int index, bool selected) {
  if (index < 0 || index >= static_cast<int>(buttons_.size()))
    return;
  auto* row = buttons_[index].get();
  row->SetSelected(selected);
  if (bookmarks_ && static_cast<size_t>(index) < remove_buttons_.size())
    remove_buttons_[index]->SetFocusBehavior(
        selected ? views::View::FocusBehavior::ALWAYS
                 : views::View::FocusBehavior::NEVER);
  row->GetViewAccessibility().SetIsSelected(selected);
  if (search_) {
    if (selected)
      search_->GetViewAccessibility().SetActiveDescendant(*row);
    else if (search_->GetViewAccessibility().GetActiveDescendantView() ==
             &row->GetViewAccessibility())
      search_->GetViewAccessibility().ClearActiveDescendant();
  }
  if (selected && selection_callback_ &&
      index < static_cast<int>(filtered_.size()))
    selection_callback_.Run(filtered_[index].action);
}
void CommandPalette::RefreshQuery() {
  if (active() && query_provider_)
    UpdateItems(catalog_, removable_);
}
void CommandPalette::ContentsChanged(views::Textfield*field, const std::u16string&) {
  if (field == search_)
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
  if (editing_url_) {
    if ((bookmark_name_ && bookmark_name_->IsIMEComposing()) ||
        (bookmark_url_ && bookmark_url_->IsIMEComposing()))
      return false;
    if (event.key_code() == ui::VKEY_ESCAPE) {
      EndBookmarkEdit();
      return true;
    }
    const auto stroke = NativeEventStroke(input::NativeWebKeyboardEvent(event));
    if (std::ranges::find(palette_bindings_, stroke) !=
        palette_bindings_.end()) {
      Cancel(true);
      return true;
    }
    if (event.key_code() == ui::VKEY_TAB)
      return CycleMenuFocus(bookmark_editor_, event.IsShiftDown());
    if (event.key_code() == ui::VKEY_RETURN ||
        (event.IsControlDown() && event.key_code() == ui::VKEY_S)) {
      // Space/Enter on Cancel or Delete must retain that button's action.
      if (bookmark_name_->HasFocus() || bookmark_url_->HasFocus() ||
          (event.IsControlDown() && event.key_code() == ui::VKEY_S)) {
        SaveBookmark();
        return true;
      }
    }
    return false;
  }
  if (bookmarks_) {
    if (event.IsControlDown() && event.key_code() == ui::VKEY_RETURN &&
        selected_ >= 0) {
      OpenInBackground(filtered_[selected_].action);
      return true;
    }
    if (event.IsAltDown() && (event.key_code() == ui::VKEY_UP ||
                              event.key_code() == ui::VKEY_DOWN)) {
      MoveBookmark(event.key_code() == ui::VKEY_UP ? -1 : 1);
      return true;
    }
    if (event.IsControlDown() && event.key_code() == ui::VKEY_N) {
      EditBookmark("");
      return true;
    }
    if (event.key_code() == ui::VKEY_F2 && selected_ >= 0) {
      EditBookmark(filtered_[selected_].action);
      return true;
    }
    if (event.IsControlDown() && event.key_code() == ui::VKEY_Z &&
        removed_bookmark_) {
      UndoBookmark();
      return true;
    }
  }
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
  // Tab walks the Command Centre's categories; elsewhere it moves through
  // the menu's buttons.
  if (event.key_code() == ui::VKEY_TAB) {
    if (style_.command_centre && search_->HasFocus()) {
      const auto& categories = CommandCentreCategories();
      auto current = std::ranges::find(categories, category_);
      size_t index = current == categories.end()
                         ? 0
                         : static_cast<size_t>(current - categories.begin());
      index = (index + (event.IsShiftDown() ? categories.size() - 1 : 1)) %
              categories.size();
      SetCategory(categories[index]);
      return true;
    }
    return CycleMenuFocus(overlay_->GetContentsView(), event.IsShiftDown());
  }
  if (remove_ && removable_ && selected_ >= 0 &&
      event.key_code() == ui::VKEY_DELETE &&
      (!search_->HasFocus() || (search_->GetSelectedRange().is_empty() &&
        search_->GetCursorPosition() == search_->GetText().size()))) {
    Remove(filtered_[selected_].action);
    return true;
  }
  if (!search_->HasFocus()) {
    // Arrows and typing return to the list from a row button.
    if (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP) {
      search_->RequestFocus();
      Select(event.key_code() == ui::VKEY_DOWN ? 1 : -1);
      return true;
    }
    return false;
  }
  if (event.key_code() == ui::VKEY_DOWN || event.key_code() == ui::VKEY_UP) {
    Select(event.key_code() == ui::VKEY_DOWN ? 1 : -1);
    return true;
  }
  if (event.key_code() == ui::VKEY_NEXT || event.key_code() == ui::VKEY_PRIOR) {
    if (selected_ >= 0) {
      SetRowSelected(selected_, false);
      selected_ = std::clamp(selected_ + (event.key_code() == ui::VKEY_NEXT ? 8 : -8),
                             0, static_cast<int>(filtered_.size()) - 1);
      EnsureRows(static_cast<size_t>(selected_) + 1);
      SetRowSelected(selected_, true);
      ScrollSelectedIntoView();
      UpdateInspector();
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
  if (active() && !cancelled_ && removable_ && (web_contents() || empty_source_) &&
      available_.Run(action)) {
    if (bookmarks_) {
      EndBookmarkEdit();
      removed_bookmark_ = bookmarks_->RemoveWithUndo(action);
      if (removed_bookmark_) {
        bookmark_undo_->SetVisible(true);
        BookmarkMessage(u"Bookmark deleted. You can undo this.");
      } else {
        BookmarkMessage(u"Could not delete this bookmark.");
      }
    } else {
      remove_.Run(web_contents(), std::move(action));
    }
  }
}
void CommandPalette::ActivateRow(std::string action, const ui::Event &event) {
  if (bookmarks_ &&
      (event.IsControlDown() || (event.flags() & ui::EF_MIDDLE_MOUSE_BUTTON)))
    OpenInBackground(std::move(action));
  else
    Activate(std::move(action));
}
void CommandPalette::OpenInBackground(std::string action) {
  if (!active() || !background_open_ || !available_.Run(action))
    return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<CommandPalette> menu, std::string action) {
            if (menu && menu->active() && !menu->cancelled_ &&
                menu->available_.Run(action)) {
              menu->background_open_.Run(menu->web_contents(),
                                         std::move(action));
              menu->BookmarkMessage(u"Opened in a background tab.");
            }
          },
          weak_factory_.GetWeakPtr(), std::move(action)));
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
  keep_initial_new_tab_ = false;
  Observe(nullptr);
  buttons_.clear();
  button_actions_.clear();
  drop_target_ = -1;
  remove_buttons_.clear();
  chips_.clear();
  count_ = nullptr;
  hints_ = nullptr;
  inspector_ = nullptr;
  search_ = nullptr;
  results_ = nullptr;
  results_scroll_ = nullptr;
  scrim_ = nullptr;
  source_tab_.clear();
  ResetBookmarksUI();
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
// The window moved or was resized (a tiling window manager does both when
// windows open and close beside it): the surface follows it and keeps its
// text, selection and focus.
void CommandPalette::OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) {
  if (!active())
    return;
  const gfx::Rect bounds = SurfaceBounds();
  overlay_->SetBounds(bounds);
  PlacePanel(bounds);
  // A taller list shows more rows than were built for the old height, and
  // without overflow no scroll would build them.
  EnsureRows(initial_rows_);
}
void CommandPalette::OnWidgetDestroying(views::Widget*) {
  desktop_ = nullptr;
  Cancel();
}
void CommandPalette::PrimaryPageChanged(content::Page&) {
  if (std::exchange(keep_initial_new_tab_, false) && web_contents() &&
      IsFlyNewTabPage(web_contents()->GetLastCommittedURL())) {
    if (active() && search_)
      search_->RequestFocus();
    return;
  }
  Cancel();
}
void CommandPalette::WebContentsDestroyed() {
  Cancel(); }

void CommandPalette::ResetBookmarksUI() {
  header_ = nullptr;
  bookmark_add_ = nullptr;
  bookmark_editor_scroll_ = nullptr;
  bookmark_editor_ = nullptr;
  bookmark_name_ = nullptr;
  bookmark_url_ = nullptr;
  bookmark_save_ = nullptr;
  bookmark_undo_ = nullptr;
  bookmark_status_ = nullptr;
  bookmark_error_ = nullptr;
  editing_url_.reset();
  editing_position_.reset();
  removed_bookmark_.reset();
}
void CommandPalette::BookmarkMessage(std::u16string text) {
  if (!bookmark_status_)
    return;
  bookmark_status_->SetText(text);
  bookmark_status_->SetTooltipText(text);
}
void CommandPalette::EditBookmark(std::string url) {
  if (!active() || !bookmarks_ || !bookmarks_->CanEdit())
    return;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<CommandPalette> menu, std::string url) {
                       if (!menu || !menu->active() ||
                           !menu->bookmarks_->CanEdit() ||
                           (!url.empty() && !menu->bookmarks_->Contains(url)))
                         return;
                       menu->editing_url_ = std::move(url);
            menu->editing_position_.reset();
                       menu->BuildBookmarkEditor();
                     },
                     weak_factory_.GetWeakPtr(), std::move(url)));
}
void CommandPalette::BuildBookmarkEditor() {
  const bool adding = editing_url_->empty();
  std::u16string name, url;
  if (adding) {
    if (web_contents() &&
        !IsFlyNewTabPage(web_contents()->GetLastCommittedURL()) &&
        bookmarks_->CanAdd(web_contents()->GetLastCommittedURL().spec())) {
      name = web_contents()->GetTitle();
      url = base::UTF8ToUTF16(web_contents()->GetLastCommittedURL().spec());
    }
  } else {
    const auto items = bookmarks_->Read();
    if (!items)
      return;
    for (const auto &bookmark : *items) {
      if (bookmark.url == *editing_url_) {
        name = base::UTF8ToUTF16(bookmark.title);
        url = base::UTF8ToUTF16(bookmark.url);
        break;
      }
    }
  }
  bookmark_name_ = nullptr;
  bookmark_url_ = nullptr;
  bookmark_save_ = nullptr;
  bookmark_error_ = nullptr;
  bookmark_editor_->RemoveAllChildViews();
  bookmark_editor_->GetViewAccessibility().SetRole(ax::mojom::Role::kGroup);
  bookmark_editor_->GetViewAccessibility().SetName(adding ? u"Add bookmark"
                                                          : u"Edit bookmark");
  auto *layout =
      bookmark_editor_->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical,
          gfx::Insets::VH(S(16), S(12)), S(10)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto *title = bookmark_editor_->AddChildView(std::make_unique<FlyLabel>(
      adding ? u"Add bookmark" : u"Edit bookmark", FlyFace::kSans, 18,
      gfx::Font::Weight::MEDIUM));
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->GetViewAccessibility().SetRole(ax::mojom::Role::kHeading);
  title->GetViewAccessibility().SetHierarchicalLevel(2);
  const auto field = [&](std::u16string label, const std::u16string &value) {
    auto *caption = bookmark_editor_->AddChildView(MakeMenuCaption(label));
    caption->SetTone(FlyLabel::Tone::kText);
    caption->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    auto *input =
        bookmark_editor_->AddChildView(std::make_unique<FlyTextfield>(label));
    input->SetText(value);
    input->set_controller(this);
    return input;
  };
  bookmark_name_ = field(u"Name", name);
  bookmark_url_ = field(u"Address", url);
  auto *help = bookmark_editor_->AddChildView(std::make_unique<FlyLabel>(
      u"Use a full address, including https:// for a website.", FlyFace::kSans,
      kMenuSmallTextSize, gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  help->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  help->SetMultiLine(true);
  if (!adding) {
    auto *order =
        bookmark_editor_->AddChildView(std::make_unique<views::View>());
    order->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(8)));
    for (int direction : {-1, 1}) {
      order->AddChildView(MakeMenuButton(
          base::BindRepeating(&CommandPalette::MoveBookmark,
                              weak_factory_.GetWeakPtr(), direction),
          direction < 0 ? u"Move earlier" : u"Move later"));
    }
  }
  bookmark_error_ = bookmark_editor_->AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kSans, kMenuSmallTextSize, gfx::Font::Weight::NORMAL,
      FlyLabel::Tone::kText));
  bookmark_error_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  bookmark_error_->SetMultiLine(true);
  bookmark_error_->GetViewAccessibility().SetLiveRegionContainer(
      views::ViewAccessibility::LiveRegionStatus::kPolite);
  auto *actions =
      bookmark_editor_->AddChildView(std::make_unique<views::View>());
  auto *actions_layout =
      actions->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kHorizontal, gfx::Insets::VH(S(12), 0),
          S(8)));
  if (!adding) {
    actions->AddChildView(MakeMenuButton(
        base::BindRepeating(&CommandPalette::Remove, weak_factory_.GetWeakPtr(),
                            *editing_url_),
        u"Delete", MenuButtonStyle::kQuiet));
  }
  auto *space = actions->AddChildView(std::make_unique<views::View>());
  actions_layout->SetFlexForView(space, 1);
  actions->AddChildView(
      MakeMenuButton(base::BindRepeating(&CommandPalette::EndBookmarkEdit,
                                         weak_factory_.GetWeakPtr()),
                     u"Cancel"));
  bookmark_save_ = actions->AddChildView(
      MakeMenuButton(base::BindRepeating(&CommandPalette::SaveBookmark,
                                         weak_factory_.GetWeakPtr()),
                     u"Save", MenuButtonStyle::kPrimary));
  bookmark_save_->SetAccentText(false);
  search_->SetVisible(false);
  results_scroll_->SetVisible(false);
  bookmark_add_->SetVisible(false);
  bookmark_undo_->SetVisible(false);
  bookmark_editor_scroll_->SetVisible(true);
  bookmark_editor_->InvalidateLayout();
  DisableSubpixelText(bookmark_editor_);
  UpdateHints();
  bookmark_name_->RequestFocus();
  bookmark_name_->SelectAll(false);
  bookmark_editor_scroll_->ScrollToPosition(
      bookmark_editor_scroll_->vertical_scroll_bar(), 0);
}
void CommandPalette::EndBookmarkEdit() {
  if (!active() || !editing_url_)
    return;
  const std::string url = *editing_url_;
  editing_url_.reset();
  editing_position_.reset();
  bookmark_editor_scroll_->SetVisible(false);
  bookmark_add_->SetVisible(true);
  bookmark_add_->SetEnabled(bookmarks_->CanEdit());
  bookmark_undo_->SetVisible(removed_bookmark_.has_value());
  BookmarkMessage(u"");
  search_->SetVisible(true);
  results_scroll_->SetVisible(true);
  UpdateHints();
  SelectBookmark(url);
  search_->RequestFocus();
}
void CommandPalette::SelectBookmark(std::string_view url) {
  for (size_t i = 0; i < filtered_.size(); ++i) {
    if (filtered_[i].action != url)
      continue;
    if (selected_ >= 0)
      SetRowSelected(selected_, false);
    EnsureRows(i + 1);
    selected_ = static_cast<int>(i);
    SetRowSelected(selected_, true);
    ScrollSelectedIntoView();
    break;
  }
}
void CommandPalette::SaveBookmark() {
  if (!active() || !editing_url_)
    return;
  const auto name =
      base::TrimWhitespace(bookmark_name_->GetText(), base::TRIM_ALL);
  const GURL url(base::UTF16ToUTF8(
      base::TrimWhitespace(bookmark_url_->GetText(), base::TRIM_ALL)));
  std::u16string error;
  if (!bookmarks_->CanEdit())
    error =
        u"Bookmarks are read-only right now. Your changes have not been saved.";
  else if (name.empty())
    error = u"Give this bookmark a name.";
  else if (!url.is_valid() || !bookmarks_->CanAdd(url.spec()))
    error = u"Enter a valid website, file or browser-page address.";
  else if (url.spec() != *editing_url_ && bookmarks_->Contains(url.spec()))
    error =
        u"This address is already saved. Edit the existing bookmark instead.";
  if (error.empty()) {
    const bool saved =
        editing_url_->empty()
            ? bookmarks_->Add(url.spec(), base::UTF16ToUTF8(name))
            : bookmarks_->Update(*editing_url_, url.spec(),
                                 base::UTF16ToUTF8(name), editing_position_);
    if (!saved)
      error = u"This bookmark changed or could not be saved. Your draft is "
              u"still here.";
  }
  if (!error.empty()) {
    bookmark_error_->SetText(error);
    bookmark_error_->GetViewAccessibility().NotifyEvent(
        ax::mojom::Event::kAlert, true);
    return;
  }
  editing_url_ = url.spec();
  EndBookmarkEdit();
  if (std::ranges::none_of(filtered_, [&](const auto &item) {
        return item.action == url.spec();
      })) {
    search_->SetText(u"");
    Refresh();
  }
  SelectBookmark(url.spec());
  BookmarkMessage(u"Bookmark saved.");
}
void CommandPalette::UndoBookmark() {
  if (!active() || !removed_bookmark_)
    return;
  const std::string url = *removed_bookmark_->item.FindString("url");
  if (!bookmarks_->Restore(*removed_bookmark_)) {
    BookmarkMessage(u"Could not undo: the address may already be saved or "
                    u"bookmarks are read-only.");
    return;
  }
  removed_bookmark_.reset();
  bookmark_undo_->SetVisible(false);
  SelectBookmark(url);
  search_->RequestFocus();
  BookmarkMessage(u"Bookmark restored.");
}
void CommandPalette::MoveBookmark(int direction) {
  if (!active() || !bookmarks_ || !bookmarks_->CanEdit())
    return;
  const std::string url =
      editing_url_ ? *editing_url_ : selected_action_for_testing();
  const auto items = bookmarks_->Read();
  if (!items)
    return;
  const auto found = std::ranges::find(*items, url, &Bookmark::url);
  if (found == items->end())
    return;
  const int index = editing_position_ ? static_cast<int>(*editing_position_)
                                    : static_cast<int>(found - items->begin());
  const int target = index + direction;
  if (target < 0 || target >= static_cast<int>(items->size())) {
    BookmarkMessage(direction < 0 ? u"Already the first bookmark."
                                  : u"Already the last bookmark.");
    return;
  }
  if (editing_url_) {
    editing_position_ = static_cast<size_t>(target);
    BookmarkMessage(u"Position " + base::NumberToString16(target + 1) +
                    u" of " + base::NumberToString16(items->size()) + u". Save to apply.");
    return;
  }
  // Updating preferences replaces list rows. Defer beyond their input callback.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](base::WeakPtr<CommandPalette> menu, std::string url,
             size_t target) {
            if (menu && menu->active() &&
                menu->bookmarks_->MoveTo(url, target)) {
              menu->SelectBookmark(url);
              menu->BookmarkMessage(u"Bookmark order updated.");
            }
          },
          weak_factory_.GetWeakPtr(), url, static_cast<size_t>(target)));
}
void CommandPalette::DeleteSelectedBookmark() {
  if (selected_ >= 0)
    Remove(filtered_[selected_].action);
}
gfx::Rect CommandPalette::row_title_bounds_for_testing(size_t index) {
  EnsureRows(index + 1);
  return index < buttons_.size()
             ? static_cast<PickerButton *>(buttons_[index].get())
                   ->TitleBoundsForTesting()
             : gfx::Rect();
}
SkColor CommandPalette::row_subtitle_color_for_testing(size_t index) {
  EnsureRows(index + 1);
  return index < buttons_.size()
             ? static_cast<PickerButton *>(buttons_[index].get())
                   ->SubtitleColor()
             : SK_ColorTRANSPARENT;
}

// The Command Centre adds category chips above the list.
void CommandPalette::BuildCommandCentre(views::View* panel) {
  auto* chips = panel->AddChildView(std::make_unique<views::View>());
  chips->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kHorizontal, gfx::Insets(), S(4)));
  for (const auto& category : CommandCentreCategories()) {
    std::u16string label = base::UTF8ToUTF16(category);
    if (category == "Performance & Memory")
      label = u"Performance";
    else if (category == "User Scripts & Styles")
      label = u"Scripts";
    else if (category == "Preferences & System")
      label = u"Preferences";
    else if (category == "Tabs & Navigation")
      label = u"Tabs";
    auto* chip = chips->AddChildView(std::make_unique<FlyButton>(
        base::BindRepeating(&CommandPalette::SetCategory,
                            weak_factory_.GetWeakPtr(), category),
        label, FlyFace::kSans, kMenuSmallTextSize));
    chip->SetCustomPadding(gfx::Insets::VH(S(3), S(10)));
    chip->SetCornerRadius(S(12));
    chip->SetOutlined(true);
    chip->SetSelected(category == category_);
    chip->SetAccentText(category == category_);
    chip->SetFocusBehavior(views::View::FocusBehavior::NEVER);
    chip->GetViewAccessibility().SetName(base::UTF8ToUTF16(category));
    chips_.push_back(chip);
  }
}

void CommandPalette::SetCategory(std::string category) {
  if (!active())
    return;
  category_ = std::move(category);
  const auto& categories = CommandCentreCategories();
  for (size_t i = 0; i < chips_.size() && i < categories.size(); ++i) {
    auto* chip = static_cast<FlyButton*>(chips_[i].get());
    chip->SetSelected(categories[i] == category_);
    chip->SetAccentText(categories[i] == category_);
  }
  Refresh();
}

void CommandPalette::SetDragDrop(
    base::RepeatingCallback<bool(std::string_view)> draggable,
    base::RepeatingCallback<void(std::string, std::string)> drop) {
  draggable_ = std::move(draggable);
  drop_ = std::move(drop);
}
int CommandPalette::RowAt(const gfx::Point& screen) const {
  for (size_t i = 0; i < buttons_.size(); ++i) {
    // The whole row, its indent and row actions included.
    const auto* row = buttons_[i]->parent() == results_
                          ? static_cast<const views::View*>(buttons_[i].get())
                          : buttons_[i]->parent();
    if (row && row->GetBoundsInScreen().Contains(screen))
      return static_cast<int>(i);
  }
  return -1;
}
void CommandPalette::DragOver(gfx::Point screen) {
  const int target = RowAt(screen);
  if (target == drop_target_)
    return;
  if (drop_target_ >= 0 &&
      static_cast<size_t>(drop_target_) < buttons_.size())
    static_cast<FlyButton*>(buttons_[drop_target_].get())->SetSelected(false);
  drop_target_ = target;
  if (drop_target_ >= 0)
    static_cast<FlyButton*>(buttons_[drop_target_].get())->SetSelected(true);
}
void CommandPalette::Drop(std::string source, gfx::Point screen) {
  DragOver(gfx::Point(-1, -1));
  const int target = RowAt(screen);
  if (target < 0 || static_cast<size_t>(target) >= button_actions_.size() ||
      button_actions_[target] == source || !drop_)
    return;
  // Run after the dragged row's own event handling has returned; the drop
  // rebuilds the list.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(drop_, std::move(source),
                                button_actions_[target]));
}
void CommandPalette::SetCategoryForTesting(std::string category) {
  SetCategory(std::move(category));
}

void CommandPalette::Hover(size_t index) {
  if (!active() || index >= buttons_.size() || static_cast<int>(index) == selected_)
    return;
  // A row that opens under a resting pointer is not chosen by it: until the
  // pointer moves, Enter runs the first (or most recent) command.
  if (!pointer_moved_) {
    if (aura::Env::GetInstance()->last_mouse_location() == pointer_at_open_)
      return;
    pointer_moved_ = true;
  }
  if (!buttons_[index]->GetEnabled())
    return;
  if (selected_ >= 0)
    SetRowSelected(selected_, false);
  selected_ = static_cast<int>(index);
  SetRowSelected(selected_, true);
  UpdateInspector();
}

// The Command Centre's detail line: what the selected command does.
void CommandPalette::UpdateInspector() {
  if (!inspector_)
    return;
  inspector_->RemoveAllChildViews();
  if (selected_ < 0 || selected_ >= static_cast<int>(filtered_.size())) {
    auto* none = inspector_->AddChildView(std::make_unique<FlyLabel>(
        u"No command selected", FlyFace::kSans, kMenuSmallTextSize,
        gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
    none->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    inspector_->InvalidateLayout();
    return;
  }
  const auto& entry = filtered_[selected_];
  const bool terminal = entry.action.starts_with("terminal:");
  auto* title = inspector_->AddChildView(std::make_unique<FlyLabel>(
      terminal ? u"Run in terminal" : base::UTF8ToUTF16(entry.title),
      FlyFace::kSans, kMenuTextSize, gfx::Font::Weight::SEMIBOLD));
  title->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  title->SetElideBehavior(gfx::ELIDE_TAIL);
  const std::string text = !entry.desc.empty()      ? entry.desc
                           : !entry.details.empty() ? entry.details
                                                    : entry.category;
  auto* description = inspector_->AddChildView(std::make_unique<FlyLabel>(
      base::UTF8ToUTF16(text), FlyFace::kSans, kMenuSmallTextSize,
      gfx::Font::Weight::NORMAL, FlyLabel::Tone::kDim));
  description->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  description->SetElideBehavior(gfx::ELIDE_TAIL);
  description->SetTooltipText(base::UTF8ToUTF16(
      entry.details.empty() ? text : entry.details));
  DisableSubpixelText(inspector_);
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
