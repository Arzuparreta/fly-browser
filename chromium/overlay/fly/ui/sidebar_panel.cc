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
#include "third_party/skia/include/core/SkPath.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia_rep.h"
#include "ui/gfx/scoped_canvas.h"
#include "ui/views/animation/animation_sequence_block.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/view_targeter.h"
#include "ui/views/view_utils.h"

namespace fly {
namespace {
// Gives `view` a transparent layer of its own, so it can fade.
void Layered(views::View* view) {
  view->SetPaintToLayer();
  view->layer()->SetFillsBoundsOpaquely(false);
}
void ClearPointerState(views::View* view) {
  if (auto* button = views::AsViewClass<views::Button>(view);
      button && (button->GetState() == views::Button::STATE_HOVERED ||
                 button->GetState() == views::Button::STATE_PRESSED))
    button->SetState(views::Button::STATE_NORMAL);
  for (views::View* child : view->children())
    ClearPointerState(child);
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
  // Faded controls ignore the pointer but stay focusable, so focusing one
  // still expands the sidebar.
  if (interactive) {
    // Disabling event dispatch while the pointer is on a control prevents
    // Views from delivering its mouse exit. Clear the transient state before
    // fading it out; persistent toggle selection and keyboard focus survive.
    if (opacity == 0)
      ClearPointerState(view);
    view->SetCanProcessEventsWithinSubtree(opacity > 0);
  }
}
}  // namespace

// The engraving, 230px wide and centred, ends 41px above the footer rule:
// the same clearance it had above the clock badge. Resizing never recentres it.
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
                         height() - FlyScaled(41) - size, size, size, true);
  }

 private:
  gfx::ImageSkia dark_;
  gfx::ImageSkia light_;
};
BEGIN_METADATA(SidebarPanel, Engraving)
END_METADATA

// A quiet divider above the footer.
class SidebarPanel::Rule : public views::View {
  METADATA_HEADER(Rule, views::View)

 public:
  Rule() { SetCanProcessEventsWithinSubtree(false); }
  void OnPaint(gfx::Canvas* canvas) override {
    canvas->FillRect(GetLocalBounds(), GetFlyColors().border);
  }
};
BEGIN_METADATA(SidebarPanel, Rule)
END_METADATA

SidebarPanel::SidebarPanel() {
  engraving_ = AddChildView(std::make_unique<Engraving>());
  footer_line_ = AddChildView(std::make_unique<Rule>());
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
  if (rail_ != rail) {
    if (parts_.top.header)
      ClearPointerState(parts_.top.header);
    InvalidateLayout();
  }
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

// Every length goes through the interface scale.
void SidebarPanel::Layout(PassKey) {
  const auto s = [](int pixels) { return FlyScaled(pixels); };
  const int content = width();
  const int x = s(4);
  const int row = s(245);
  int y = s(8);
  const int footer_y = height() - s(6) - s(34);
  engraving_->SetBounds(0, 0, content, std::max(0, footer_y));
  if (parts_.window_controls && parts_.window_controls->GetVisible()) {
    const int height = parts_.window_controls->GetPreferredSize().height();
    parts_.window_controls->SetBounds(x, y, row, height);
    y += height + s(6);
  }
  // Navigation takes three fifths of the usable row. A clear gap separates
  // its outlined buttons from the quieter, left-aligned workspace name.
  const int top = s(34);
  const int group_gap = s(12);
  const int navigation = (content - 2 * x - group_gap) * 3 / 5;
  const int new_tab_width = (navigation - s(6) + s(1)) / 3;
  const int history = navigation - s(6) - new_tab_width;
  const int back_width = (history + s(1)) / 2;
  const int new_tab_x = x + history + s(6);
  const int header_x = x + navigation + group_gap;
  const int header = content - x - header_x;
  if (parts_.top.header)
    parts_.top.header->SetBounds(rail_ ? x : header_x, y,
                                 rail_ ? s(28) : header, top);
  if (parts_.top.badge)
    parts_.top.badge->SetBounds(x, y + (top - s(28)) / 2, s(28), s(28));
  // Start the name at the workspace target's left edge, without adding more
  // whitespace to the gap between the two groups.
  // Long names elide within the same bounds as the button.
  if (parts_.top.label)
    parts_.top.label->SetBounds(header_x, y, header, top);
  if (parts_.top.new_tab)
    parts_.top.new_tab->SetBounds(new_tab_x, y + (top - s(30)) / 2,
                                  new_tab_width, s(30));
  if (parts_.top.back)
    parts_.top.back->SetBounds(x, y + (top - s(30)) / 2, back_width, s(30));
  if (parts_.top.forward)
    parts_.top.forward->SetBounds(x + back_width - s(1),
                                  y + (top - s(30)) / 2,
                                  history - back_width + s(1), s(30));
  y += top + s(6);
  if (parts_.workspaces && parts_.workspaces->GetVisible()) {
    const int height = parts_.workspaces->GetHeightForWidth(row);
    parts_.workspaces->SetBounds(x, y, row, height);
    y += height + s(4);
  }
  // The tab list fills the free height above the footer rule.
  y += s(4);
  if (parts_.tabs)
    parts_.tabs->SetBounds(0, y, content, std::max(0, footer_y - y));
  // .bottom-theme-layout: 6px above the bottom, 8px 4px 0 padding, buttons
  // spread across the row with the pin first, centred on the rail.
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

void SidebarPanel::ChildVisibilityChanged(views::View* child) {
  InvalidateLayout();
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
  footer_line_->SchedulePaint();
}
BEGIN_METADATA(SidebarPanel)
END_METADATA

// The shadow: 6px to the right, 28px blur, black at 65%, outside the panel
// only.
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

// The edge: its right border travels with it, outside the viewport.
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
