// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/command_palette.h"
#include "fly/ui/new_tab_ui.h"

#include <algorithm>
#include <utility>
#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "cc/paint/paint_flags.h"
#include "components/input/native_web_keyboard_event.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/fly_menu.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/fly_theme.h"
#include "ui/accessibility/ax_enums.mojom.h"
#include "ui/aura/env.h"
#include "ui/aura/window.h"
#include "ui/events/event.h"
#include "ui/gfx/canvas.h"
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
#include "ui/wm/core/coordinate_conversion.h"

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
    return gfx::Size(S(200), S(subtitle_.empty() ? kMenuRowHeight
                                                 : kMenuTallRowHeight));
  }
  void SetHoverCallback(base::RepeatingClosure hover) {
    hover_ = std::move(hover);
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
      label()->SetBounds(left, S(5), std::max(0, right - left), line);
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
    const auto& colors = GetFlyColors();
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
          colors.dim, gfx::Rect(right - width - S(4), 0, width, height()),
          gfx::Canvas::TEXT_ALIGN_RIGHT | gfx::Canvas::NO_SUBPIXEL_RENDERING);
    }
    if (!subtitle_.empty()) {
      const auto title = label()->bounds();
      const int line = small.GetHeight();
      canvas->DrawStringRectWithFlags(
          gfx::ElideText(subtitle_, small, title.width(), gfx::ELIDE_MIDDLE),
          small, colors.dim,
          gfx::Rect(title.x(), title.bottom() + S(1), title.width(), line),
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
  gfx::Rect bounds = desktop_->GetContentsView()->GetBoundsInScreen();
  if (style_.layout == PaletteStyle::Layout::kPage && style_.page_insets)
    bounds.Inset(style_.page_insets.Run());
  wm::ConvertRectFromScreen(desktop_->GetNativeWindow(), &bounds);
  overlay_ = CreateMenuWidget(desktop_, bounds, "FlyCommandPalette");
  const auto& colors = GetFlyColors();
  const bool top = style_.layout == PaletteStyle::Layout::kTop;
  const bool page = style_.layout == PaletteStyle::Layout::kPage;
  auto root = std::make_unique<MenuScrim>(
      top ? SkColorSetARGB(1, 0, 0, 0)
          : SkColorSetA(colors.dark ? SK_ColorBLACK : colors.text,
                        colors.dark ? 120 : 60),
      base::BindRepeating(&CommandPalette::Cancel, weak_factory_.GetWeakPtr(),
                          true));
  // Panel geometry: compact cards grow with their results, managers and the
  // Command Centre keep one size so the list does not jump while filtering.
  const int width = std::min(S(style_.width), bounds.width() - S(24));
  const int x = (bounds.width() - width) / 2;
  const int top_margin = top ? S(12) : page ? S(20) : std::max(S(24), bounds.height() / 9);
  const int max_height = std::max(
      S(120), (page ? bounds.height() - 2 * top_margin
                    : std::min(bounds.height() - top_margin - S(24),
                               S(style_.command_centre ? 600 : 520))));
  auto panel = std::make_unique<MenuPanel>();
  auto* layout = panel->SetLayoutManager(std::make_unique<views::BoxLayout>(
      views::BoxLayout::Orientation::kVertical, gfx::Insets(S(10)), S(8)));
  layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kStretch);
  auto* header = panel->AddChildView(
      std::make_unique<MenuHeader>(style_.kind, style_.title));
  search_ = header->SetSearch(std::make_unique<FlyTextfield>(placeholder));
  search_->set_controller(this);
  count_ = style_.kind == MenuKind::kAddress ? nullptr : header->AddStatus();
  if (style_.layout != PaletteStyle::Layout::kTop)
    header->AddClose(base::BindRepeating(&CommandPalette::Cancel,
                                         weak_factory_.GetWeakPtr(), true));
  if (style_.command_centre)
    BuildCommandCentre(panel.get());
  auto* scroll = panel->AddChildView(std::make_unique<views::ScrollView>());
  scroll->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll->ClipHeightTo(0, max_height);
  scroll->SetBackgroundColor(std::nullopt);
  scroll->SetDrawOverflowIndicator(false);
  results_scroll_ = scroll;
  results_ = scroll->SetContents(std::make_unique<views::View>());
  // Rows are built for what the panel can show and a margin; scrolling and
  // keyboard selection build the rest as they reach it.
  initial_rows_ = static_cast<size_t>(max_height / S(kMenuRowHeight)) + 4;
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
  hints_ = panel->AddChildView(std::make_unique<KeyHints>(
      std::vector<KeyHints::Hint>()));
  hints_->SetBorder(views::CreateEmptyBorder(gfx::Insets::TLBR(0, S(2), 0, 0)));
  UpdateHints();
  root->SetPanel(std::move(panel));
  if (top || !(page || style_.command_centre))
    root->SetPanelGrowsTo(gfx::Rect(x, top_margin, width,
                                    max_height + S(140)));
  else
    root->SetPanelBounds(gfx::Rect(
        x, top_margin, width,
        page ? bounds.height() - 2 * top_margin
             : std::min(bounds.height() - top_margin - S(24),
                        max_height + S(150))));
  DisableSubpixelText(root.get());
  Refresh();
  overlay_->SetContentsView(std::move(root));
  overlay_->ShowInactive();
  desktop_->GetNativeWindow()->StackChildAtTop(overlay_->GetNativeWindow());
  search_->RequestFocus();
}
void CommandPalette::UpdateHints() {
  if (!hints_)
    return;
  std::vector<KeyHints::Hint> hints = {{u"↑↓", u"Move"},
                                       {u"Enter", style_.enter_label}};
  if (style_.command_centre)
    hints.push_back({u"Tab", u"Category"});
  if (remove_ && removable_)
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
    // Electron's filterPaletteCommands.
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
                        : base::NumberToString16(filtered_.size()));
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
        base::BindRepeating(&CommandPalette::Activate,
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
          base::BindRepeating(&CommandPalette::Remove,
                              weak_factory_.GetWeakPtr(), entry.action),
          u"Remove", MenuButtonStyle::kQuiet));
      remove->SetEnabled(removable_ && available_.Run(entry.action));
      remove->GetViewAccessibility().SetName(u"Remove " +
                                             base::UTF8ToUTF16(text));
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
      buttons_[selected_]->ScrollViewToVisible();
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
  row->GetViewAccessibility().SetIsSelected(selected);
  if (!search_)
    return;
  if (selected)
    search_->GetViewAccessibility().SetActiveDescendant(*row);
  else if (search_->GetViewAccessibility().GetActiveDescendantView() ==
           &row->GetViewAccessibility())
    search_->GetViewAccessibility().ClearActiveDescendant();
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
      buttons_[selected_]->ScrollViewToVisible();
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
  if (std::exchange(keep_initial_new_tab_, false) && web_contents() &&
      IsFlyNewTabPage(web_contents()->GetLastCommittedURL())) {
    if (active() && search_)
      search_->RequestFocus();
    return;
  }
  Cancel();
}
void CommandPalette::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Cancel();
}
void CommandPalette::WebContentsDestroyed() {
  Cancel();
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
