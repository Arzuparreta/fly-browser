// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/tab_sidebar.h"
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/new_tab_ui.h"

#include <algorithm>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/trace_event/trace_event.h"
#include "cc/paint/path_effect.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/recently_audible_helper.h"
#include "chrome/browser/ui/tabs/tab_enums.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/split_tab_data.h"
#include "components/vector_icons/vector_icons.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "skia/ext/image_operations.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/base/models/image_model.h"
#include "ui/color/color_provider.h"
#include "ui/compositor/layer.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/canvas_image_source.h"
#include "ui/gfx/image/image_skia_operations.h"
#include "ui/gfx/paint_vector_icon.h"
#include "ui/gfx/text_constants.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/image_view.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/controls/scroll_view.h"
#include "ui/views/vector_icons.h"
#include "url/url_constants.h"

namespace fly {
namespace {
// Row colours.
constexpr SkColor kHoverFill = SkColorSetARGB(115, 54, 74, 133);
constexpr SkColor kFocusFill = SkColorSetARGB(41, 122, 162, 247);
constexpr SkColor kPinnedFill = SkColorSetARGB(20, 224, 175, 104);
constexpr SkColor kPinnedBorder = SkColorSetARGB(64, 224, 175, 104);
constexpr SkColor kPinnedSelectedFill = SkColorSetARGB(51, 122, 162, 247);

int S(int pixels) {
  return FlyScaled(pixels);
}
// color-mix(in srgb, a amount%, b).
SkColor Mix(SkColor a, float amount, SkColor b) {
  return color_utils::AlphaBlend(a, b, amount);
}
SkColor WithAlpha(SkColor color, float alpha) {
  return SkColorSetA(color, static_cast<U8CPU>(alpha * 255));
}

// What a row shows; the surfaces and marks layers paint from it.
struct RowState {
  bool selected = false;
  bool pinned = false;
  bool audible = false;
  bool muted = false;
  int split_pane = 0;  // 1 or 2 inside a split view.
  // Closing, while the page's unload handlers run or its leave dialog shows.
  bool closing = false;
  bool HasSurface() const { return selected || pinned || split_pane; }
  bool operator==(const RowState&) const = default;
};

// Titles longer than 24 characters are cut there.
std::u16string RowTitle(std::u16string title) {
  return title.size() > 24 ? title.substr(0, 24) + u"..." : title;
}
// The row's second line: the host, "local page" without one, and nothing for
// a new tab (its title says it).
std::u16string RowDomain(const GURL& url) {
  if (IsFlyNewTabPage(url))
    return std::u16string();
  return url.has_host() ? base::UTF8ToUTF16(url.host()) : u"local page";
}

// The favicon in its 18px wrapper, and the split pane badge on the wrapper's
// upper right corner (.split-tab-badge). The image is 26px with the wrapper
// at (4, 4), so the badge fits inside it.
class RowIcon : public gfx::CanvasImageSource {
 public:
  RowIcon(gfx::ImageSkia favicon, int split_pane)
      : CanvasImageSource(gfx::Size(S(26), S(26))),
        favicon_(std::move(favicon)),
        split_pane_(split_pane) {}
  void Draw(gfx::Canvas* canvas) override {
    canvas->DrawImageInt(favicon_, S(4) + (S(18) - favicon_.width()) / 2,
                         S(4) + (S(18) - favicon_.height()) / 2);
    if (!split_pane_)
      return;
    const auto& colors = GetFlyColors();
    const float scale = GetFlyInterfaceScale();
    const gfx::PointF centre(19.5f * scale, 6.5f * scale);
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    flags.setColor(SkColorSetARGB(153, 0, 0, 0));
    canvas->DrawCircle(centre, 7.5f * scale, flags);
    flags.setColor(split_pane_ == 1 ? colors.accent : colors.text);
    canvas->DrawCircle(centre, 6.5f * scale, flags);
    canvas->DrawStringRectWithFlags(
        split_pane_ == 1 ? u"1" : u"2",
        FlyFont(FlyFace::kSans, 8, gfx::Font::Weight::BOLD),
        SkColorSetRGB(0x12, 0x12, 0x12), gfx::Rect(S(13), 0, S(13), S(13)),
        gfx::Canvas::TEXT_ALIGN_CENTER);
  }

 private:
  const gfx::ImageSkia favicon_;
  const int split_pane_;
};

// The badges at a row's right end: pin (.tab-pin-badge), audio
// (.tab-media-badge) and close (.tab-close-btn).
class RowBadge : public views::Button {
  METADATA_HEADER(RowBadge, views::Button)

 public:
  enum class Kind { kPin, kMedia, kClose };
  RowBadge(PressedCallback callback, Kind kind)
      : views::Button(std::move(callback)), kind_(kind) {
    SetFocusBehavior(FocusBehavior::NEVER);
    SetInstallFocusRingOnFocus(false);
  }
  void SetMuted(bool muted) {
    if (muted_ == muted)
      return;
    muted_ = muted;
    SchedulePaint();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds&) const override {
    return kind_ == Kind::kClose ? gfx::Size(S(20), S(20))
                                 : gfx::Size(S(18), S(14));
  }
  void StateChanged(ButtonState) override { SchedulePaint(); }
  void PaintButtonContents(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const bool hot = GetState() == STATE_HOVERED || GetState() == STATE_PRESSED;
    if (hot) {
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(kind_ == Kind::kPin     ? SkColorSetARGB(38, 224, 175, 104)
                     : kind_ == Kind::kClose ? SkColorSetARGB(46, 247, 118, 142)
                     : muted_                ? SkColorSetARGB(38, 247, 118, 142)
                                             : SkColorSetARGB(38, 122, 162, 247));
      canvas->DrawRoundRect(gfx::RectF(GetLocalBounds()), S(4), flags);
    }
    SkColor color;
    const gfx::VectorIcon* icon;
    int size = S(10);
    switch (kind_) {
      case Kind::kPin:
        color = colors.warning;
        icon = &views::kKeepFilledIcon;
        break;
      case Kind::kMedia:
        color = colors.accent;
        icon = muted_ ? &vector_icons::kVolumeOffIcon
                      : &vector_icons::kVolumeUpIcon;
        break;
      case Kind::kClose:
        color = hot ? colors.text : colors.dim;
        icon = &vector_icons::kCloseIcon;
        size = S(12);
        break;
    }
    // The pin and audio badges rest at 85% opacity.
    if (kind_ != Kind::kClose && !hot)
      color = WithAlpha(color, 0.85f);
    const auto image = gfx::CreateVectorIcon(*icon, size, color);
    canvas->DrawImageInt(image, (width() - size) / 2, (height() - size) / 2);
  }

 private:
  const Kind kind_;
  bool muted_ = false;
};
BEGIN_METADATA(RowBadge)
END_METADATA
}  // namespace

// The row's clickable body: favicon, then the title above a monospace domain
// in the link colour. Its surfaces are painted by the list's layers.
class FlyTabButton : public FlyButton {
  METADATA_HEADER(FlyTabButton, FlyButton)

 public:
  explicit FlyTabButton(PressedCallback callback)
      : FlyButton(std::move(callback), u"", FlyFace::kSans, 13) {
    SetCustomPadding(gfx::Insets());
    SetImageLabelSpacing(0);
    SetHorizontalAlignment(gfx::ALIGN_LEFT);
    label()->SetElideBehavior(gfx::ELIDE_TAIL);
    // The rows live on a transparent layer.
    label()->SetSubpixelRenderingEnabled(false);
    domain_ = AddChildView(std::make_unique<FlyLabel>(
        u"", FlyFace::kMono, 10, gfx::Font::Weight::NORMAL,
        FlyLabel::Tone::kLink));
    domain_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
    domain_->SetElideBehavior(gfx::ELIDE_TAIL);
    domain_->SetCanProcessEventsWithinSubtree(false);
    domain_->SetSubpixelRenderingEnabled(false);
  }
  void SetDomain(const std::u16string& domain) {
    if (domain_->GetText() == domain)
      return;
    domain_->SetText(domain);
    domain_->SetVisible(!domain.empty());
    InvalidateLayout();
  }
  // The text stops before the row's badges.
  void SetTextRight(int right) {
    if (text_right_ == right)
      return;
    text_right_ = right;
    InvalidateLayout();
  }
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds&) const override {
    return gfx::Size(S(245), S(40));
  }
  void Layout(PassKey) override {
    LayoutSuperclass<FlyButton>(this);
    // .tab-favicon-wrapper: 5px from the row's left edge, 18px square, so
    // the favicon's centre is the rail's centre line. The icon image adds
    // 4px around the wrapper for the split badge.
    image_container_view()->SetBounds(S(5) - S(4),
                                      (height() - S(18)) / 2 - S(4),
                                      S(26), S(26));
    // The text starts past the rail's edge: 5 + 18 + 2 + 8.
    const int left = S(33);
    const int right = std::max(left, text_right_ ? text_right_ : width());
    const int line = S(16);
    if (!domain_->GetVisible()) {
      label()->SetBounds(left, (height() - line) / 2, right - left, line);
      return;
    }
    const int domain = S(13);
    const int top = (height() - line - S(3) - domain) / 2;
    label()->SetBounds(left, top, right - left, line);
    domain_->SetBounds(left, top + line + S(3), right - left, domain);
  }
  // Hover, focus and selection belong to the row and the list's surfaces.
  void OnPaintBackground(gfx::Canvas*) override {}

 private:
  raw_ptr<FlyLabel> domain_ = nullptr;
  int text_right_ = 0;
};
BEGIN_METADATA(FlyTabButton)
END_METADATA

// One tab row. Child order (select, pin, close, audio) is part of the row
// contract.
class TabRowView : public views::View {
  METADATA_HEADER(TabRowView, views::View)

 public:
  explicit TabRowView(TabSidebar* list) : list_(list) {
    SetNotifyEnterExitOnChild(true);
  }
  void SetParts(FlyTabButton* select,
                RowBadge* pin,
                RowBadge* close,
                RowBadge* audio) {
    select_ = select;
    pin_ = pin;
    close_ = close;
    audio_ = audio;
  }
  FlyTabButton* select() { return select_; }
  views::Button* pin() { return pin_; }
  views::Button* close() { return close_; }
  views::Button* audio() { return audio_; }
  RowBadge* audio_badge() { return audio_; }
  const RowState& state() const { return state_; }
  bool hovered() const { return hovered_; }
  // Returns whether the row's look changed.
  bool SetState(const RowState& state) {
    if (has_state_ && state == state_)
      return false;
    has_state_ = true;
    state_ = state;
    pin_->SetVisible(state.pinned);
    audio_->SetVisible(state.audible || state.muted);
    audio_->SetMuted(state.muted);
    // Pinned tabs cannot be closed from the row.
    close_->SetEnabled(!state.pinned);
    // A closing row fades at once, so the click is answered even while the
    // page delays the close.
    if (state.closing) {
      if (!layer()) {
        SetPaintToLayer();
        layer()->SetFillsBoundsOpaquely(false);
      }
      layer()->SetOpacity(0.45f);
    } else if (layer()) {
      DestroyLayer();
    }
    UpdateClose();
    InvalidateLayout();
    SchedulePaint();
    return true;
  }
  void UpdateClose() {
    // .tab-close-btn keeps its space and shows on the hovered or selected
    // row; pinned and closing rows have none.
    close_->SetVisible(!state_.pinned && !state_.closing &&
                       (state_.selected || (hovered_ && !list_->rail())));
  }
  void OnMouseEntered(const ui::MouseEvent&) override {
    hovered_ = true;
    UpdateClose();
    SchedulePaint();
  }
  void OnMouseExited(const ui::MouseEvent&) override {
    hovered_ = false;
    UpdateClose();
    SchedulePaint();
  }
  void Layout(PassKey) override {
    select_->SetBoundsRect(GetLocalBounds());
    // An 8px gap adds to each control's left margin: close 2px, audio/pin 6px.
    // Reserve close space even while hidden.
    int x = width() - S(8);
    if (!state_.pinned) {
      x -= S(20);
      close_->SetBounds(x, (height() - S(20)) / 2, S(20), S(20));
      x -= S(10);
    }
    for (RowBadge* badge : {audio_.get(), pin_.get()}) {
      if (!badge->GetVisible())
        continue;
      const auto size = badge->GetPreferredSize();
      x -= size.width();
      badge->SetBounds(x, (height() - size.height()) / 2, size.width(),
                       size.height());
      x -= S(14);
    }
    select_->SetTextRight(x);
  }
  void OnPaintBackground(gfx::Canvas* canvas) override {
    const auto bounds = gfx::RectF(GetLocalBounds());
    cc::PaintFlags flags;
    flags.setAntiAlias(true);
    if (select_->HasFocus() && !state_.selected) {
      flags.setColor(kFocusFill);
      canvas->DrawRoundRect(bounds, S(2), flags);
      // outline: 1px dashed var(--accent), inside the row.
      const float dash[] = {static_cast<float>(S(3)), static_cast<float>(S(3))};
      cc::PaintFlags outline;
      outline.setStyle(cc::PaintFlags::kStroke_Style);
      outline.setStrokeWidth(1);
      outline.setColor(GetFlyColors().accent);
      outline.setPathEffect(cc::PathEffect::MakeDash(dash, 2, 0));
      auto inner = bounds;
      inner.Inset(0.5f);
      canvas->DrawRect(inner, outline);
      return;
    }
    // Rows with a surface hide the hover tint under it.
    if (hovered_ && !list_->rail() && !state_.HasSurface()) {
      flags.setColor(kHoverFill);
      canvas->DrawRoundRect(bounds, S(2), flags);
    }
  }

 private:
  const raw_ptr<TabSidebar> list_;
  raw_ptr<FlyTabButton> select_ = nullptr;
  raw_ptr<RowBadge> pin_ = nullptr;
  raw_ptr<RowBadge> close_ = nullptr;
  raw_ptr<RowBadge> audio_ = nullptr;
  RowState state_;
  bool has_state_ = false;
  bool hovered_ = false;
};
BEGIN_METADATA(TabRowView)
END_METADATA

namespace {
// The rows, 40px tall and 4px apart, 4px from the list's left edge and
// leaving the 10px scrollbar gutter on the right (259 - 4 - 10 = 245).
class RowsView : public views::View {
  METADATA_HEADER(RowsView, views::View)

 public:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds&) const override {
    const int count = static_cast<int>(children().size());
    return gfx::Size(S(259), count ? count * S(44) - S(4) : 0);
  }
  void Layout(PassKey) override {
    const int row_width = std::max(0, width() - S(14));
    int y = 0;
    for (views::View* child : children()) {
      child->SetBounds(S(4), y, row_width, S(40));
      y += S(44);
    }
  }
};
BEGIN_METADATA(RowsView)
END_METADATA

// The scrolled contents: surfaces below the rows, rail marks above them.
class ListContents : public views::View {
  METADATA_HEADER(ListContents, views::View)

 public:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override {
    for (views::View* child : children()) {
      if (views::IsViewClass<RowsView>(child))
        return child->GetPreferredSize(available);
    }
    return gfx::Size();
  }
  void Layout(PassKey) override {
    for (views::View* child : children())
      child->SetBoundsRect(GetLocalBounds());
  }
};
BEGIN_METADATA(ListContents)
END_METADATA
}  // namespace

// The expanded row surfaces (li::before: the whole row) or the rail's tiles
// (li::after: 28 x 40px at the row's left).
class TabSidebar::Surfaces : public views::View {
  METADATA_HEADER(Surfaces, views::View)

 public:
  Surfaces(TabSidebar* list, bool rail) : list_(list), rail_(rail) {
    SetCanProcessEventsWithinSubtree(false);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    const SkColor sidebar = colors.bg_sidebar;
    for (views::View* child : list_->rows_->children()) {
      const auto& state = static_cast<TabRowView*>(child)->state();
      if (!rail_ && !state.HasSurface())
        continue;
      gfx::RectF bounds(child->bounds());
      if (rail_)
        bounds.set_width(S(28));
      SkColor fill = Mix(colors.text, 0.04f, sidebar);
      SkColor border = Mix(colors.text, 0.09f, sidebar);
      if (state.pinned) {
        fill = kPinnedFill;
        border = kPinnedBorder;
      }
      if (state.split_pane) {
        border = WithAlpha(state.split_pane == 1 ? colors.accent : colors.text,
                           0.4f);
      }
      if (state.selected) {
        fill = state.pinned ? kPinnedSelectedFill : colors.panel;
        border = colors.accent;
      }
      if (!rail_ && !state.pinned && !state.selected)
        fill = SK_ColorTRANSPARENT;
      const float radius = rail_ ? S(4) : S(2);
      cc::PaintFlags flags;
      flags.setAntiAlias(true);
      flags.setColor(fill);
      canvas->DrawRoundRect(bounds, radius, flags);
      flags.setStyle(cc::PaintFlags::kStroke_Style);
      const float stroke = GetFlyInterfaceScale();
      flags.setStrokeWidth(stroke);
      flags.setColor(border);
      auto outline = bounds;
      outline.Inset(stroke / 2);
      canvas->DrawRoundRect(outline, radius, flags);
      // A selected row's left border is 2px wide.
      if (state.selected) {
        flags.setStyle(cc::PaintFlags::kFill_Style);
        canvas->DrawRect(gfx::RectF(bounds.x() + stroke, bounds.y() + stroke,
                                   stroke, bounds.height() - 2 * stroke), flags);
      }
    }
  }

 private:
  // The list outlives its children; the rows are a sibling destroyed first.
  const raw_ptr<TabSidebar> list_;
  const bool rail_;
};
BEGIN_METADATA(TabSidebar, Surfaces)
END_METADATA

// The rail's status marks: the pin at the favicon's upper left corner and
// audio below it (.tab-pin-dot, .tab-audio-dot).
class TabSidebar::RailMarks : public views::View {
  METADATA_HEADER(RailMarks, views::View)

 public:
  explicit RailMarks(TabSidebar* list) : list_(list) {
    SetCanProcessEventsWithinSubtree(false);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& colors = GetFlyColors();
    for (views::View* child : list_->rows_->children()) {
      const auto& state = static_cast<TabRowView*>(child)->state();
      // The favicon wrapper: 5px into the row, centred vertically.
      const gfx::Point wrapper(child->x() + S(5),
                               child->y() + (child->height() - S(18)) / 2);
      if (state.pinned) {
        const int size = S(9);
        cc::PaintFlags flags;
        flags.setAntiAlias(true);
        flags.setColor(colors.bg_sidebar);
        canvas->DrawCircle(
            gfx::PointF(wrapper.x() - S(3) + size / 2.f,
                        wrapper.y() - S(3) + size / 2.f),
            size / 2.f, flags);
        canvas->DrawImageInt(
            gfx::CreateVectorIcon(views::kKeepFilledIcon, S(7),
                                  colors.warning),
            wrapper.x() - S(2), wrapper.y() - S(2));
      }
      if (state.audible || state.muted) {
        const int size = S(8);
        canvas->DrawImageInt(
            gfx::CreateVectorIcon(state.muted ? vector_icons::kVolumeOffIcon
                                              : vector_icons::kVolumeUpIcon,
                                  size,
                                  state.muted ? colors.dim : colors.accent),
            wrapper.x() + S(3) + (S(12) - size) / 2,
            wrapper.y() + S(18) + (S(9) - size) / 2);
      }
    }
  }

 private:
  const raw_ptr<TabSidebar> list_;
};
BEGIN_METADATA(TabSidebar, RailMarks)
END_METADATA

// Product tab actions use native models and unload/restore services. This menu
// deliberately distinguishes tab muting from Chromium's site-wide mute command.
class SidebarTabMenu : public ui::SimpleMenuModel::Delegate {
 public:
  enum Action : int {
    kNew = 1,
    kReload,
    kDuplicate,
    kPin,
    kMute,
    kClose,
    kCloseOthers,
    kCloseRight,
    kReopen,
    kMoveWorkspace,
  };
  SidebarTabMenu(BrowserWindowInterface* browser,
                 base::WeakPtr<content::WebContents> contents,
                 base::RepeatingCallback<void(int)> execute)
      : browser_(browser->GetWeakPtr()),
        contents_(std::move(contents)),
        execute_(std::move(execute)),
        model_(this) {
    model_.AddItem(kNew, u"New tab to the right");
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
    model_.AddItem(kReload, u"Reload");
    model_.AddItem(kDuplicate, u"Duplicate");
    model_.AddItem(kMoveWorkspace, u"Move to workspace…");
    const auto* tabs = browser->GetTabStripModel();
    const int index = tabs->GetIndexOfWebContents(contents_.get());
    model_.AddItem(kPin, tabs->IsTabPinned(index) ? u"Unpin" : u"Pin");
    model_.AddItem(kMute,
                   contents_->IsAudioMuted() ? u"Unmute tab" : u"Mute tab");
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
    model_.AddItem(kClose, u"Close");
    model_.AddItem(kCloseOthers, u"Close other tabs");
    model_.AddItem(kCloseRight, u"Close tabs to the right");
    model_.AddSeparator(ui::NORMAL_SEPARATOR);
    model_.AddItem(kReopen, u"Reopen closed tab");
    runner_ = std::make_unique<views::MenuRunner>(
        &model_,
        views::MenuRunner::CONTEXT_MENU | views::MenuRunner::HAS_MNEMONICS);
  }
  ~SidebarTabMenu() override = default;
  void Run(const gfx::Point& point,
           ui::mojom::MenuSourceType source,
           views::Widget* widget) {
    runner_->RunMenuAt(widget, nullptr, gfx::Rect(point, gfx::Size()),
                       views::MenuAnchorPosition::kTopLeft, source);
  }
  void Cancel() { runner_->Cancel(); }
  ui::SimpleMenuModel* model() {
    return runner_->IsRunning() ? &model_ : nullptr;
  }
  bool IsCommandIdEnabled(int action) const override {
    return Enabled(browser_.get(), contents_.get(), action);
  }
  void ExecuteCommand(int action, int) override {
    if (!IsCommandIdEnabled(action))
      return;
    // Closing a tab can destroy the invoking row, model or entire browser. Run
    // only after native menu dispatch/nested loops unwind, using live
    // identities.
    auto execute = execute_;
    runner_->Cancel();
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
        FROM_HERE, base::BindOnce(std::move(execute), action));
  }
  static bool Enabled(BrowserWindowInterface* browser,
                      content::WebContents* contents,
                      int action) {
    if (!browser || !contents)
      return false;
    const auto* tabs = browser->GetTabStripModel();
    const int index = tabs->GetIndexOfWebContents(contents);
    if (index == TabStripModel::kNoTab)
      return false;
    switch (action) {
      case kNew:
        return tabs->IsContextMenuCommandEnabled(
            index, TabStripModel::CommandNewTabToRight);
      case kReload:
        return tabs->IsContextMenuCommandEnabled(index,
                                                 TabStripModel::CommandReload);
      case kDuplicate:
        return tabs->IsContextMenuCommandEnabled(
            index, TabStripModel::CommandDuplicate);
      case kPin:
        return tabs->IsContextMenuCommandEnabled(
            index, TabStripModel::CommandTogglePinned);
      case kMute:
        return true;
      case kMoveWorkspace: {
        const auto* app = FlyApp::GetIfCreated();
        return app && app->CanMoveTab(browser, contents);
      }
      case kClose:
        return !tabs->IsTabPinned(index) &&
               tabs->IsContextMenuCommandEnabled(
                   index, TabStripModel::CommandCloseTab);
      case kCloseOthers:
        return tabs->IsContextMenuCommandEnabled(
            index, TabStripModel::CommandCloseOtherTabs);
      case kCloseRight:
        return tabs->IsContextMenuCommandEnabled(
            index, TabStripModel::CommandCloseTabsToRight);
      case kReopen:
        return chrome::IsCommandEnabled(browser, IDC_RESTORE_TAB);
      default:
        return false;
    }
  }
  static void Execute(BrowserWindowInterface* browser,
                      content::WebContents* contents,
                      int action) {
    if (!Enabled(browser, contents, action))
      return;
    auto* tabs = browser->GetTabStripModel();
    const int index = tabs->GetIndexOfWebContents(contents);
    switch (action) {
      case kNew:
        OpenNativeBlankTab(browser, index + 1);
        return;
      case kReload:
        tabs->ExecuteContextMenuCommand(index, TabStripModel::CommandReload);
        return;
      case kDuplicate:
        tabs->ExecuteContextMenuCommand(index, TabStripModel::CommandDuplicate);
        return;
      case kPin:
        tabs->SetTabPinned(index, !tabs->IsTabPinned(index));
        return;
      case kMute:
        SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                         TabMutedReason::kAudioIndicator, std::string());
        return;
      case kMoveWorkspace:
        if (auto* window = FlyWindowController::FromBrowser(browser))
          window->ShowTabMovePicker(contents);
        return;
      case kClose:
        tabs->CloseWebContentsAt(
            index, TabCloseTypes::CLOSE_USER_GESTURE |
                       TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
        return;
      case kCloseOthers:
        tabs->ExecuteContextMenuCommand(index,
                                        TabStripModel::CommandCloseOtherTabs);
        return;
      case kCloseRight:
        tabs->ExecuteContextMenuCommand(index,
                                        TabStripModel::CommandCloseTabsToRight);
        return;
      case kReopen:
        chrome::ExecuteCommand(browser, IDC_RESTORE_TAB);
        return;
    }
  }

 private:
  base::WeakPtr<BrowserWindowInterface> browser_;
  base::WeakPtr<content::WebContents> contents_;
  base::RepeatingCallback<void(int)> execute_;
  ui::SimpleMenuModel model_;
  std::unique_ptr<views::MenuRunner> runner_;
};

// Tab numbers: 15px semibold digits over the favicon with a hairline of the
// sidebar colour, 1-8 and 9 on the last tab, while the selection modifier is
// held.
class TabSidebar::Numbers : public views::View {
  METADATA_HEADER(Numbers, views::View)

 public:
  explicit Numbers(TabSidebar* list) : list_(list) {
    SetCanProcessEventsWithinSubtree(false);
  }
  void OnPaint(gfx::Canvas* canvas) override {
    if (!list_->numbers_visible_)
      return;
    const auto& colors = GetFlyColors();
    const auto& rows = list_->rows_->children();
    const auto font =
        FlyFont(FlyFace::kSans, 15, gfx::Font::Weight::SEMIBOLD);
    for (size_t i = 0; i < rows.size(); ++i) {
      const bool last = i + 1 == rows.size();
      if (i >= 8 && !last)
        continue;
      const std::u16string digit =
          i >= 8 ? u"9" : base::NumberToString16(i + 1);
      const auto* row = rows[i].get();
      const gfx::Rect wrapper(row->x() + S(5),
                              row->y() + (row->height() - S(18)) / 2, S(18),
                              S(18));
      // -webkit-text-stroke: 3px var(--bg-sidebar), painted under the digit.
      for (const auto& [dx, dy] : std::initializer_list<std::pair<int, int>>{
               {-1, -1}, {0, -2}, {1, -1}, {-2, 0}, {2, 0}, {-1, 1}, {0, 2},
               {1, 1}}) {
        canvas->DrawStringRectWithFlags(digit, font, colors.bg_sidebar,
                                        wrapper + gfx::Vector2d(dx, dy),
                                        gfx::Canvas::TEXT_ALIGN_CENTER);
      }
      canvas->DrawStringRectWithFlags(digit, font, colors.text, wrapper,
                                      gfx::Canvas::TEXT_ALIGN_CENTER);
    }
  }

 private:
  const raw_ptr<TabSidebar> list_;
};
BEGIN_METADATA(TabSidebar, Numbers)
END_METADATA

void TabSidebar::SetNumbersVisible(bool visible) {
  if (numbers_visible_ == visible)
    return;
  numbers_visible_ = visible;
  numbers_->SchedulePaint();
}

TabSidebar::TabSidebar(base::RepeatingClosure new_tab)
    : new_tab_(std::move(new_tab)) {
  SetUseDefaultFillLayout(true);
  scroll_ = AddChildView(std::make_unique<views::ScrollView>());
  // A bounded viewport sizes the contents on both compositor scrolling
  // backends. Unbounded contents otherwise retain zero bounds.
  scroll_->ClipHeightTo(0, std::numeric_limits<int>::max());
  scroll_->SetHorizontalScrollBarMode(
      views::ScrollView::ScrollBarMode::kDisabled);
  scroll_->SetBackgroundColor(std::nullopt);
  scroll_->SetDrawOverflowIndicator(false);
  auto* contents = scroll_->SetContents(std::make_unique<ListContents>());
  // Layers in paint order: expanded surfaces, rail tiles, rows, rail marks.
  // The rows get a layer of their own so the surfaces stay under them.
  expanded_surfaces_ =
      contents->AddChildView(std::make_unique<Surfaces>(this, false));
  rail_surfaces_ =
      contents->AddChildView(std::make_unique<Surfaces>(this, true));
  rows_ = contents->AddChildView(std::make_unique<RowsView>());
  rows_->SetPaintToLayer();
  rows_->layer()->SetFillsBoundsOpaquely(false);
  rail_marks_ = contents->AddChildView(std::make_unique<RailMarks>(this));
  numbers_ = contents->AddChildView(std::make_unique<Numbers>(this));
  numbers_->SetPaintToLayer();
  numbers_->layer()->SetFillsBoundsOpaquely(false);
}
TabSidebar::~TabSidebar() {
  context_menu_.reset();
  if (model_)
    model_->RemoveObserver(this);
}
void TabSidebar::SetRail(bool rail) {
  if (rail_ == rail)
    return;
  rail_ = rail;
  // Only the hovered row looks different on the rail (no hover tint, no
  // close button). Repainting every row would delay the slide's first frame.
  for (views::View* child : rows_->children()) {
    auto* row = static_cast<TabRowView*>(child);
    if (!row->hovered())
      continue;
    row->UpdateClose();
    row->SchedulePaint();
  }
}
void TabSidebar::OnThemeChanged() {
  views::View::OnThemeChanged();
  for (auto& [contents, row] : tab_rows_)
    row.favicon_initialized = false;
  ScheduleRefresh();
  RepaintSurfaces();
}
void TabSidebar::RepaintSurfaces() {
  for (views::View* layer : {expanded_surfaces_.get(), rail_surfaces_.get(),
                             rail_marks_.get(), numbers_.get()})
    layer->SchedulePaint();
}
void TabSidebar::SetBrowser(BrowserWindowInterface* browser) {
  if (browser_ == browser)
    return;
  ++context_generation_;
  context_menu_.reset();
  if (model_)
    model_->RemoveObserver(this);
  browser_ = browser;
  model_ = browser ? browser->GetTabStripModel() : nullptr;
  if (model_)
    model_->AddObserver(this);
  // Defer rebuilding: a click may synchronously switch workspace or close a
  // tab. Deleting the originating button during its own callback is unsafe.
  ScheduleRefresh();
}
class TabSidebar::CloseWatch : public content::WebContentsObserver {
 public:
  CloseWatch(content::WebContents* contents, base::RepeatingClosure cancelled)
      : content::WebContentsObserver(contents),
        cancelled_(std::move(cancelled)) {}
  // The page answered "stay": Chromium clears the close mark in the same
  // call, before the posted refresh runs.
  void BeforeUnloadFired(bool proceed) override {
    if (!proceed)
      cancelled_.Run();
  }

 private:
  base::RepeatingClosure cancelled_;
};

void TabSidebar::ScheduleRefresh() {
  full_refresh_pending_ = true;
  dirty_rows_.clear();
  PostRefresh();
}
void TabSidebar::ScheduleRowRefresh(content::WebContents* contents) {
  if (!contents || full_refresh_pending_)
    return;
  if (!tab_rows_.contains(contents)) {
    ScheduleRefresh();
    return;
  }
  dirty_rows_.insert(contents);
  PostRefresh();
}
void TabSidebar::PostRefresh() {
  if (refresh_pending_)
    return;
  refresh_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&TabSidebar::Refresh, weak_factory_.GetWeakPtr()));
}
void TabSidebar::Refresh() {
  if (!refresh_pending_)
    return;
  TRACE_EVENT("browser", "Fly.Sidebar.Refresh", "full", full_refresh_pending_);
  refresh_pending_ = false;
  if (state_changed_)
    state_changed_.Run();
  if (!std::exchange(full_refresh_pending_, false)) {
    auto dirty = std::move(dirty_rows_);
    dirty_rows_.clear();
    bool changed = false;
    for (auto* contents : dirty) {
      auto it = tab_rows_.find(contents);
      if (model_ && it != tab_rows_.end() && it->second.contents) {
        const int index = it->second.index;
        if (index >= 0 && index < model_->count() &&
            model_->GetWebContentsAt(index) == contents)
          changed |= UpdateRow(contents, index);
      }
    }
    FinishRefresh(changed);
    return;
  }
  dirty_rows_.clear();
  std::set<content::WebContents*> live;
  if (model_) {
    for (int i = 0; i < model_->count(); ++i)
      live.insert(model_->GetWebContentsAt(i));
  }
  // Preserve existing row objects and focus through title/selection changes.
  // Remove obsolete rows before rebuilding; a reused allocation is not an
  // identity match when its original WeakPtr has expired.
  // Titles, favicons and loading change far more often than the rows'
  // number, order or state; only those relayout the list and repaint its
  // surfaces.
  bool changed = false;
  for (auto it = tab_rows_.begin(); it != tab_rows_.end();) {
    if (!live.contains(it->first) || !it->second.contents) {
      auto removed = rows_->RemoveChildViewT(it->second.view.get());
      it = tab_rows_.erase(it);
      changed = true;
    } else {
      ++it;
    }
  }
  if (!model_) {
    RepaintSurfaces();
    return;
  }
  for (int i = 0; i < model_->count(); ++i) {
    auto* contents = model_->GetWebContentsAt(i);
    if (!tab_rows_.contains(contents)) {
      changed = true;
      auto callback = [&](RowAction action) {
        return base::BindRepeating(&TabSidebar::Invoke,
                                   weak_factory_.GetWeakPtr(),
                                   contents->GetWeakPtr(), action);
      };
      auto row = std::make_unique<TabRowView>(this);
      // A middle click closes the tab, as in other browsers.
      auto* select = row->AddChildView(std::make_unique<FlyTabButton>(
          base::BindRepeating(
              [](base::WeakPtr<TabSidebar> list,
                 base::WeakPtr<content::WebContents> contents,
                 const ui::Event& event) {
                if (list)
                  list->Invoke(contents,
                               event.flags() & ui::EF_MIDDLE_MOUSE_BUTTON
                                   ? RowAction::kClose
                                   : RowAction::kSelect);
              },
              weak_factory_.GetWeakPtr(), contents->GetWeakPtr())));
      select->SetTriggerableEventFlags(ui::EF_LEFT_MOUSE_BUTTON |
                                       ui::EF_MIDDLE_MOUSE_BUTTON);
      auto* pin = row->AddChildView(std::make_unique<RowBadge>(
          callback(RowAction::kPin), RowBadge::Kind::kPin));
      auto* close = row->AddChildView(std::make_unique<RowBadge>(
          callback(RowAction::kClose), RowBadge::Kind::kClose));
      auto* audio = row->AddChildView(std::make_unique<RowBadge>(
          callback(RowAction::kMute), RowBadge::Kind::kMedia));
      row->SetParts(select, pin, close, audio);
      auto* view = rows_->AddChildView(std::move(row));
      auto& data = tab_rows_.try_emplace(contents).first->second;
      data.contents = contents->GetWeakPtr();
      data.view = view;
      for (auto* control :
           {static_cast<views::View*>(view), static_cast<views::View*>(select),
            static_cast<views::View*>(pin), static_cast<views::View*>(close),
            static_cast<views::View*>(audio)})
        control->set_context_menu_controller(this);
      if (auto* audible = RecentlyAudibleHelper::FromWebContents(contents)) {
        data.audible_subscription =
            audible->RegisterRecentlyAudibleChangedCallback(base::BindRepeating(
                [](base::WeakPtr<TabSidebar> sidebar,
                   base::WeakPtr<content::WebContents> contents, bool) {
                  if (sidebar && contents)
                    sidebar->ScheduleRowRefresh(contents.get());
                },
                weak_factory_.GetWeakPtr(), contents->GetWeakPtr()));
      }
    }
    auto& row = tab_rows_.at(contents);
    auto* view = row.view.get();
    if (rows_->GetIndexOf(view) != static_cast<size_t>(i)) {
      rows_->ReorderChildView(view, i);
      changed = true;
    }
    changed |= UpdateRow(contents, i);
  }
  FinishRefresh(changed);
}
bool TabSidebar::UpdateRow(content::WebContents* contents, int i) {
  ++row_updates_;
  auto& row = tab_rows_.at(contents);
  row.index = i;
  auto* view = row.view.get();
  std::u16string title = contents->GetTitle();
  if (title.empty())
    title = base::UTF8ToUTF16(contents->GetVisibleURL().spec());
  if (title.empty())
    title = u"New tab";
  const GURL& url = contents->GetVisibleURL();
  view->select()->SetText(RowTitle(title));
  view->select()->SetDomain(RowDomain(url));
  const auto* audible_helper = RecentlyAudibleHelper::FromWebContents(contents);
  RowState state;
  state.selected = i == model_->active_index();
  state.pinned = model_->IsTabPinned(i);
  state.audible = audible_helper ? audible_helper->WasRecentlyAudible()
                                 : contents->IsCurrentlyAudible();
  state.muted = contents->IsAudioMuted();
  // Chromium marks a tab closed by the user until it goes away, and clears
  // the mark when its page cancels the close.
  state.closing = contents->GetClosedByUserGesture();
  if (state.closing && !row.close_watch) {
    row.close_watch = std::make_unique<CloseWatch>(
        contents, base::BindRepeating(&TabSidebar::ScheduleRowRefresh,
                                      weak_factory_.GetWeakPtr(), contents));
  } else if (!state.closing) {
    row.close_watch.reset();
  }
  if (const auto split = model_->GetSplitForTab(i)) {
    const auto tabs = model_->GetSplitData(*split)->ListTabs();
    for (size_t pane = 0; pane < tabs.size(); ++pane) {
      if (tabs[pane]->GetContents() == contents)
        state.split_pane = static_cast<int>(pane) + 1;
    }
  }
  const int previous_split = view->state().split_pane;
  const bool changed = view->SetState(state);
  const auto* audio_status = state.muted     ? u", muted"
                             : state.audible ? u", playing audio"
                                             : u"";
  // The row's tooltip: the page title and its address.
  view->select()->SetTooltipText(title + u"\n" + base::UTF8ToUTF16(url.spec()));
  view->select()->GetViewAccessibility().SetName(
      base::NumberToString16(i + 1) + u". " + title +
      (state.selected ? u", selected" : u"") +
      (state.pinned ? u", pinned" : u"") + audio_status);
  auto* navigation = contents->GetController().GetVisibleEntry();
  const gfx::ImageSkia icon = navigation && navigation->GetFavicon().valid &&
                                      !navigation->GetFavicon().image.IsEmpty()
                                  ? navigation->GetFavicon().image.AsImageSkia()
                                  : gfx::ImageSkia();
  if (!row.favicon_initialized || !icon.BackedBySameObjectAs(row.favicon) ||
      previous_split != state.split_pane) {
    row.favicon = icon;
    row.favicon_initialized = true;
    // .tab-fallback-icon: a 12px globe in the accent colour.
    const auto favicon =
        icon.isNull() ? gfx::CreateVectorIcon(vector_icons::kGlobeIcon, S(12),
                                              GetFlyColors().accent)
                      : gfx::ImageSkiaOperations::CreateImageWithRoundRectClip(
                            S(2), gfx::ImageSkiaOperations::CreateResizedImage(
                                      icon, skia::ImageOperations::RESIZE_BEST,
                                      gfx::Size(S(16), S(16))));
    view->select()->SetImageModel(
        views::Button::STATE_NORMAL,
        ui::ImageModel::FromImageSkia(
            gfx::CanvasImageSource::MakeImageSkia<RowIcon>(favicon,
                                                           state.split_pane)));
  }
  const auto pin_label = u"Unpin " + title;
  view->pin()->GetViewAccessibility().SetName(pin_label);
  view->pin()->SetTooltipText(u"Pinned tab (click to unpin)");
  view->close()->GetViewAccessibility().SetName(u"Close " + title);
  view->close()->SetTooltipText(u"Close tab");
  const auto audio_label = (state.muted ? u"Unmute " : u"Mute ") + title;
  view->audio()->SetTooltipText(state.muted
                                    ? u"Tab audio muted (click to unmute)"
                                    : u"Tab playing audio (click to mute)");
  view->audio()->GetViewAccessibility().SetName(audio_label);
  return changed;
}
void TabSidebar::FinishRefresh(bool changed) {
  if (changed) {
    rows_->InvalidateLayout();
    RepaintSurfaces();
    // Rows moved under a still pointer. Lay them out now and update hover, so
    // the close button under the pointer belongs to the row now there.
    if (auto* widget = GetWidget(); widget && widget->IsVisible()) {
      widget->LayoutRootViewIfNecessary();
      widget->SynthesizeMouseMoveEvent();
    }
  }
}
void TabSidebar::Invoke(base::WeakPtr<content::WebContents> contents,
                        RowAction action) {
  if (!model_ || !contents)
    return;
  int index = model_->GetIndexOfWebContents(contents.get());
  if (index == TabStripModel::kNoTab)
    return;
  switch (action) {
    case RowAction::kSelect:
      model_->ActivateTabAt(index);
      contents->Focus();
      break;
    case RowAction::kMute:
      SetTabAudioMuted(contents.get(), !contents->IsAudioMuted(),
                       TabMutedReason::kAudioIndicator, std::string());
      break;
    case RowAction::kClose:
      if (model_->IsTabPinned(index))
        return;
      model_->CloseWebContentsAt(
          index, TabCloseTypes::CLOSE_USER_GESTURE |
                     TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
      ScheduleRefresh();
      break;
    case RowAction::kPin:
      model_->SetTabPinned(index, !model_->IsTabPinned(index));
      break;
  }
}
void TabSidebar::ShowContextMenuForViewImpl(
    views::View* source,
    const gfx::Point& point,
    ui::mojom::MenuSourceType source_type) {
  if (!model_ || !browser_)
    return;
  for (const auto& [contents, row] : tab_rows_) {
    if (!row.view->Contains(source) || !row.contents ||
        model_->GetIndexOfWebContents(row.contents.get()) ==
            TabStripModel::kNoTab)
      continue;
    context_menu_ = std::make_unique<SidebarTabMenu>(
        browser_, row.contents,
        base::BindRepeating(&TabSidebar::ExecuteMenuAction,
                            weak_factory_.GetWeakPtr(), context_generation_,
                            row.contents));
    context_menu_->Run(point, source_type, GetWidget());
    return;
  }
}
void TabSidebar::ExecuteMenuAction(uint64_t generation,
                                   base::WeakPtr<content::WebContents> contents,
                                   int action) {
  if (generation != context_generation_ || !browser_ || !contents)
    return;
  SidebarTabMenu::Execute(browser_, contents.get(), action);
  ScheduleRefresh();
}
ui::SimpleMenuModel* TabSidebar::context_menu_for_testing() const {
  return context_menu_ ? context_menu_->model() : nullptr;
}
void TabSidebar::CloseContextMenu() {
  if (context_menu_)
    context_menu_->Cancel();
}
void TabSidebar::FocusActiveTab() {
  if (refresh_pending_)
    Refresh();
  if (!model_ || model_->active_index() < 0 ||
      static_cast<size_t>(model_->active_index()) >= rows_->children().size())
    return;
  rows_->children()[model_->active_index()]->children().front()->RequestFocus();
}
size_t TabSidebar::row_count_for_testing() const {
  return rows_->children().size();
}
views::View* TabSidebar::row_for_testing(content::WebContents* contents) const {
  auto it = tab_rows_.find(contents);
  return it == tab_rows_.end() ? nullptr : it->second.view.get();
}
views::Button* TabSidebar::audio_for_testing(
    content::WebContents* contents) const {
  auto it = tab_rows_.find(contents);
  return it == tab_rows_.end() ? nullptr : it->second.view->audio();
}
gfx::ImageSkia TabSidebar::favicon_for_testing(
    content::WebContents* contents) const {
  auto it = tab_rows_.find(contents);
  return it == tab_rows_.end() ? gfx::ImageSkia() : it->second.favicon;
}
void TabSidebar::OnSplitTabChanged(const SplitTabChange&) {
  ScheduleRefresh();
}
void TabSidebar::OnTabStripModelChanged(
    TabStripModel*,
    const TabStripModelChange& change,
    const TabStripSelectionChange& selection) {
  if (change.type() == TabStripModelChange::kSelectionOnly) {
    ScheduleRowRefresh(selection.old_contents);
    ScheduleRowRefresh(selection.new_contents);
  } else {
    ScheduleRefresh();
  }
}
void TabSidebar::OnTabChangedAt(tabs::TabInterface* tab, TabChangeType) {
  ScheduleRowRefresh(tab->GetContents());
}
void TabSidebar::OnTabPinnedStateChanged(tabs::TabInterface*, int) {
  ScheduleRefresh();
}
void TabSidebar::OnTabStripModelDestroyed(TabStripModel*) {
  ++context_generation_;
  context_menu_.reset();
  // The observer base removes its registration before this notification.
  model_ = nullptr;
  browser_ = nullptr;
  ScheduleRefresh();
}
BEGIN_METADATA(TabSidebar)
END_METADATA
}  // namespace fly
