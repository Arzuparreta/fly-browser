// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/sidebar_panel.h"

#include <algorithm>
#include <utility>

#include "cc/paint/draw_looper.h"
#include "cc/paint/paint_flags.h"
#include "fly/ui/fly_theme.h"
#include "third_party/skia/include/core/SkClipOp.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "third_party/skia/include/core/SkPath.h"
#include "ui/views/animation/animation_sequence_block.h"
#include "ui/views/view_targeter.h"

namespace fly {
namespace {
// Gives `view` a transparent layer of its own, so it can fade.
void Layered(views::View* view) {
  view->SetPaintToLayer();
  view->layer()->SetFillsBoundsOpaquely(false);
}
void FadeTo(views::View* view,
            float opacity,
            bool interactive,
            views::AnimationSequenceBlock* block) {
  if (!view || !view->layer())
    return;
  if (block) {
    block->SetOpacity(view->layer(), opacity, gfx::Tween::FAST_OUT_SLOW_IN_3);
  } else {
    view->layer()->GetAnimator()->StopAnimatingProperty(
        ui::LayerAnimationElement::OPACITY);
    view->layer()->SetOpacity(opacity);
  }
  // Faded controls ignore the pointer but stay focusable (Electron's
  // pointer-events: none), so focusing one still expands the sidebar.
  if (interactive)
    view->SetCanProcessEventsWithinSubtree(opacity > 0);
}
}  // namespace

// .sidebar-content::before: the engraving, 230px wide, centred, its bottom
// anchored to the bottom as in Electron. The native panel gives it another
// 24px of clearance above the bottom controls; resizing never recentres it.
class SidebarPanel::Engraving : public views::View {
  METADATA_HEADER(Engraving, views::View)

 public:
  Engraving() { SetCanProcessEventsWithinSubtree(false); }
  void SetImages(gfx::ImageSkia dark, gfx::ImageSkia light) {
    dark_ = std::move(dark);
    light_ = std::move(light);
    SchedulePaint();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& image = GetFlyColors().dark ? dark_ : light_;
    if (image.isNull())
      return;
    const int size = FlyScaled(230);
    canvas->DrawImageInt(image, 0, 0, image.width(), image.height(),
                         (width() - size) / 2,
                         height() - FlyScaled(124) - size, size, size, true);
  }

 private:
  gfx::ImageSkia dark_;
  gfx::ImageSkia light_;
};
BEGIN_METADATA(SidebarPanel, Engraving)
END_METADATA

// .bottom-theme-layout::before: the footer's top rule.
class SidebarPanel::FooterLine : public views::View {
  METADATA_HEADER(FooterLine, views::View)

 public:
  FooterLine() { SetCanProcessEventsWithinSubtree(false); }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(GetLocalBounds(), GetFlyColors().border);
  }
};
BEGIN_METADATA(SidebarPanel, FooterLine)
END_METADATA

SidebarPanel::SidebarPanel() {
  engraving_ = AddChildView(std::make_unique<Engraving>());
  footer_line_ = AddChildView(std::make_unique<FooterLine>());
  AddExpandedOnly(engraving_, false);
  AddExpandedOnly(footer_line_, false);
}
SidebarPanel::~SidebarPanel() = default;

void SidebarPanel::SetParts(Parts parts) {
  parts_ = std::move(parts);
  InvalidateLayout();
}
void SidebarPanel::SetArtwork(gfx::ImageSkia engraving_dark,
                              gfx::ImageSkia engraving_light,
                              gfx::ImageSkia paper_grain) {
  engraving_->SetImages(std::move(engraving_dark), std::move(engraving_light));
  paper_grain_ = std::move(paper_grain);
  SchedulePaint();
}
void SidebarPanel::AddExpandedOnly(views::View* view, bool interactive) {
  if (!view)
    return;
  Layered(view);
  DisableSubpixelText(view);
  expanded_only_.push_back({view, interactive});
  FadeTo(view, rail_ ? 0 : 1, interactive, nullptr);
}
void SidebarPanel::AddRailOnly(views::View* view, bool interactive) {
  if (!view)
    return;
  Layered(view);
  DisableSubpixelText(view);
  rail_only_.push_back({view, interactive});
  FadeTo(view, rail_ ? 1 : 0, interactive, nullptr);
}
void SidebarPanel::SetRail(bool rail, views::AnimationSequenceBlock* block) {
  rail_ = rail;
  for (auto& faded : expanded_only_)
    FadeTo(faded.view, rail ? 0 : 1, faded.interactive, block);
  for (auto& faded : rail_only_)
    FadeTo(faded.view, rail ? 1 : 0, faded.interactive, block);
}

gfx::Size SidebarPanel::CalculatePreferredSize(
    const views::SizeBounds&) const {
  return gfx::Size(FlyScaled(kSidebarWidth - 1), 0);
}

// Electron's geometry (style.css), every length through the interface scale.
void SidebarPanel::Layout(PassKey) {
  const auto s = [](int pixels) { return FlyScaled(pixels); };
  const int content = width();
  const int x = s(4);
  const int row = s(245);
  int y = s(8);
  engraving_->SetBoundsRect(GetLocalBounds());
  if (parts_.window_controls && parts_.window_controls->GetVisible()) {
    const int height = parts_.window_controls->GetPreferredSize().height();
    parts_.window_controls->SetBounds(x, y, row, height);
    y += height + s(6);
  }
  // .sidebar-top-row: the workspace header, then New tab and the history
  // pill, 8px apart.
  const int top = s(34);
  const int pill = s(34);
  const int nav = s(28) + s(8) + pill * 2 - s(1);
  const int header = row - s(8) - nav;
  if (parts_.top.header)
    parts_.top.header->SetBounds(x, y, header, top);
  if (parts_.top.badge)
    parts_.top.badge->SetBounds(x, y + (top - s(28)) / 2, s(28), s(28));
  if (parts_.top.label)
    parts_.top.label->SetBounds(x + s(5), y, header - s(10), top);
  int nav_x = x + header + s(8);
  if (parts_.top.new_tab)
    parts_.top.new_tab->SetBounds(nav_x, y + (top - s(28)) / 2, s(28), s(28));
  nav_x += s(28) + s(8);
  if (parts_.top.back)
    parts_.top.back->SetBounds(nav_x, y + (top - s(30)) / 2, pill, s(30));
  if (parts_.top.forward)
    parts_.top.forward->SetBounds(nav_x + pill - s(1), y + (top - s(30)) / 2,
                                  pill, s(30));
  y += top + s(6);
  if (parts_.workspaces && parts_.workspaces->GetVisible()) {
    const int height = parts_.workspaces->GetHeightForWidth(row);
    parts_.workspaces->SetBounds(x, y, row, height);
    y += height + s(4);
  }
  // #TabList fills the free height above the clock row; the content keeps
  // 42px at the bottom for the footer.
  y += s(4);
  const int clock_height =
      parts_.clock_row ? parts_.clock_row->GetPreferredSize().height() : 0;
  const int clock_y = std::max(y, height() - s(42) - clock_height);
  if (parts_.tabs)
    parts_.tabs->SetBounds(0, y, content, clock_y - y);
  if (parts_.clock_row)
    parts_.clock_row->SetBounds(x, clock_y, row, clock_height);
  // .bottom-theme-layout: 6px above the bottom, 8px 4px 0 padding, buttons
  // spread across the row with the pin first, centred on the rail.
  const int footer_y = height() - s(6) - s(34);
  footer_line_->SetBounds(0, footer_y, content, 1);
  const int count = static_cast<int>(parts_.footer.size());
  if (!count)
    return;
  const int button_width = s(28);
  const int span = content - s(8);
  const int gap =
      count > 1 ? (span - count * button_width) / (count - 1) : 0;
  for (int i = 0; i < count; ++i) {
    parts_.footer[i]->SetBounds(x + i * (button_width + gap),
                                footer_y + s(8), button_width, s(26));
  }
}

void SidebarPanel::OnPaintBackground(gfx::Canvas* canvas) {
  canvas->DrawColor(FlyRoleColor(FlyRole::kSidebarSurface));
  // Light mode lays paper grain over the sidebar colour.
  if (!GetFlyColors().dark && !paper_grain_.isNull())
    canvas->TileImageInt(paper_grain_, 0, 0, width(), height());
}
void SidebarPanel::OnThemeChanged() {
  views::View::OnThemeChanged();
  SchedulePaint();
  engraving_->SchedulePaint();
}
BEGIN_METADATA(SidebarPanel)
END_METADATA

SidebarClock::SidebarClock(PressedCallback callback)
    : FlyButton(std::move(callback), u"", FlyFace::kMono, 13) {
  SetCustomPadding(gfx::Insets::VH(6, 10));
  SetOutlined(true);
  SetCornerRadius(6);
  SetRestingFill(SkColorSetARGB(8, 255, 255, 255));
  label()->SetVisible(false);
  date_ = AddChildView(std::make_unique<FlyLabel>(u"", FlyFace::kMono, 13));
  date_->SetHorizontalAlignment(gfx::ALIGN_LEFT);
  date_->SetElideBehavior(gfx::ELIDE_TAIL);
  time_ = AddChildView(std::make_unique<FlyLabel>(
      u"", FlyFace::kDisplay, 13, gfx::Font::Weight::EXTRA_BOLD,
      FlyLabel::Tone::kAccent));
  time_->SetHorizontalAlignment(gfx::ALIGN_RIGHT);
  for (views::Label* label : {date_.get(), time_.get()}) {
    label->SetCanProcessEventsWithinSubtree(false);
    label->SetSubpixelRenderingEnabled(false);
  }
}
SidebarClock::~SidebarClock() = default;
void SidebarClock::SetClock(const std::u16string& text,
                            const std::u16string& date,
                            const std::u16string& time) {
  if (GetText() != text)
    SetText(text);
  label()->SetVisible(false);
  date_->SetText(date);
  time_->SetText(time);
  InvalidateLayout();
}
gfx::Size SidebarClock::CalculatePreferredSize(
    const views::SizeBounds&) const {
  // 6px padding, a 13px monospace line, 1px border on each side.
  return gfx::Size(FlyScaled(245),
                   FlyScaled(12) + std::max(date_->GetPreferredSize().height(),
                                            FlyScaled(17)) + 2);
}
void SidebarClock::Layout(PassKey) {
  LayoutSuperclass<FlyButton>(this);
  // The icon keeps FlyButton's place; the date follows it after 8px and the
  // time takes the right end.
  const int right = width() - FlyScaled(10);
  const int left = image_container_view()->bounds().right() + FlyScaled(8);
  const int time_width = time_->GetPreferredSize().width();
  time_->SetBounds(right - time_width, 0, time_width, height());
  date_->SetBounds(left, 0, std::max(0, right - time_width - FlyScaled(8) - left),
                   height());
}
void SidebarClock::OnThemeChanged() {
  FlyButton::OnThemeChanged();
  // .sidebar-clock-date: opacity 0.8.
  date_->SetEnabledColor(
      SkColorSetA(GetFlyColors().text, static_cast<U8CPU>(0.8f * 255)));
}
BEGIN_METADATA(SidebarClock)
END_METADATA

// #Sidebar::after: box-shadow 6px 0 28px rgba(0, 0, 0, 0.65), outside the
// panel only.
class SidebarRoot::Shadow : public views::View {
  METADATA_HEADER(Shadow, views::View)

 public:
  Shadow() { SetCanProcessEventsWithinSubtree(false); }
  void OnPaint(gfx::Canvas* canvas) override {
    const gfx::Rect panel(0, 0, FlyScaled(kSidebarWidth), height());
    gfx::ScopedCanvas scoped(canvas);
    canvas->sk_canvas()->clipRect(gfx::RectToSkRect(panel),
                                  SkClipOp::kDifference);
    cc::DrawLooperBuilder looper;
    looper.AddShadow({6, 0}, 14,
                     SkColor4f::FromColor(SkColorSetARGB(166, 0, 0, 0)),
                     cc::DrawLooper::kOverrideAlphaFlag);
    cc::PaintFlags flags;
    flags.setLooper(looper.Detach());
    flags.setColor(SK_ColorBLACK);
    canvas->DrawRect(panel, flags);
  }
};
BEGIN_METADATA(SidebarRoot, Shadow)
END_METADATA

// #Sidebar: its border-right travels with the edge, outside the viewport.
class SidebarRoot::Edge : public views::View {
  METADATA_HEADER(Edge, views::View)

 public:
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(gfx::Rect(FlyScaled(kSidebarWidth - 1), 0, 1, height()),
                     GetFlyColors().border);
  }
};
BEGIN_METADATA(SidebarRoot, Edge)
END_METADATA

SidebarRoot::SidebarRoot(std::unique_ptr<SidebarPanel> panel) {
  edge_ = AddChildView(std::make_unique<Edge>());
  Layered(edge_);
  shadow_ = edge_->AddChildView(std::make_unique<Shadow>());
  Layered(shadow_);
  shadow_->layer()->SetOpacity(0);
  viewport_ = edge_->AddChildView(std::make_unique<views::View>());
  viewport_->SetPaintToLayer();
  viewport_->layer()->SetMasksToBounds(true);
  viewport_->layer()->SetFillsBoundsOpaquely(false);
  panel_ = viewport_->AddChildView(std::move(panel));
  panel_->SetPaintToLayer();
  SetEventTargeter(std::make_unique<views::ViewTargeter>(this));
}
SidebarRoot::~SidebarRoot() = default;
views::View* SidebarRoot::TargetForRect(views::View* root,
                                        const gfx::Rect& rect) {
  // The panel's own coordinates equal the root's at rest.
  if (auto* target = panel_->GetEventHandlerForRect(rect))
    return target;
  return this;
}

void SidebarRoot::Layout(PassKey) {
  edge_->SetBoundsRect(GetLocalBounds());
  shadow_->SetBoundsRect(GetLocalBounds());
  const int content = FlyScaled(kSidebarWidth - 1);
  viewport_->SetBounds(0, 0, content, height());
  panel_->SetBounds(0, 0, content, height());
}
BEGIN_METADATA(SidebarRoot)
END_METADATA

SidebarWidget::SidebarWidget(base::RepeatingCallback<int()> visible_width)
    : visible_width_(std::move(visible_width)) {}
SidebarWidget::~SidebarWidget() = default;
bool SidebarWidget::HasHitTestMask() const {
  return true;
}
void SidebarWidget::GetHitTestMask(SkPath* mask) const {
  const auto size = GetWindowBoundsInScreen().size();
  *mask = SkPath::Rect(SkRect::MakeWH(
      std::min(visible_width_.Run(), size.width()), size.height()));
}
}  // namespace fly
