// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_SIDEBAR_PANEL_H_
#define FLY_UI_SIDEBAR_PANEL_H_

#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "ui/gfx/image/image_skia.h"
#include "fly/ui/fly_theme.h"
#include "ui/views/view.h"
#include "ui/views/view_targeter_delegate.h"
#include "ui/views/widget/widget.h"

namespace views {
class AnimationSequenceBlock;
}

namespace fly {
// Electron's sidebar motion (style.css, docs/performance-motion.md):
// --motion-normal with cubic-bezier(0.2, 0, 0, 1).
inline constexpr base::TimeDelta kSidebarMotion = base::Milliseconds(220);
// Width of the panel's shadow beyond its right edge: CSS 6px 0 28px is a
// 6px offset and a blur of sigma 14, which fades out within 3 sigma.
inline constexpr int kSidebarShadowExtent = 48;
// Electron's widths: the panel, its collapsed strip and the slide between.
inline constexpr int kSidebarWidth = 260;
inline constexpr int kSidebarRailWidth = 36;

// Electron's .sidebar-content: one layout for both sidebar states. The
// collapsed rail is the panel's left column, so rows, icons and text never
// move; rail-only and expanded-only details crossfade.
class SidebarPanel : public views::View {
  METADATA_HEADER(SidebarPanel, views::View)

 public:
  struct TopRow {
    raw_ptr<views::View> header = nullptr;  // #WorkspaceHeader (clickable)
    raw_ptr<views::View> badge = nullptr;   // #WorkspaceBadge (rail only)
    raw_ptr<views::View> label = nullptr;   // #WorkspaceLabel
    raw_ptr<views::View> new_tab = nullptr;
    raw_ptr<views::View> back = nullptr;
    raw_ptr<views::View> forward = nullptr;
  };
  struct Parts {
    raw_ptr<views::View> window_controls = nullptr;
    TopRow top;
    raw_ptr<views::View> workspaces = nullptr;
    raw_ptr<views::View> tabs = nullptr;
    raw_ptr<views::View> clock_row = nullptr;
    // .bottom-theme-layout buttons, left to right.
    std::vector<raw_ptr<views::View>> footer;
  };

  SidebarPanel();
  SidebarPanel(const SidebarPanel&) = delete;
  SidebarPanel& operator=(const SidebarPanel&) = delete;
  ~SidebarPanel() override;

  void SetParts(Parts parts);
  void SetArtwork(gfx::ImageSkia engraving_dark,
                  gfx::ImageSkia engraving_light,
                  gfx::ImageSkia paper_grain);
  // Parts shown only in one state fade with the edge. They get their own
  // layers; the panel's own decorations (engraving, footer line) are
  // registered here too.
  // `interactive` parts ignore the pointer while hidden (Electron's
  // pointer-events: none); decorations never take it.
  void AddExpandedOnly(views::View* view, bool interactive = true);
  void AddRailOnly(views::View* view, bool interactive = true);
  // Adds the crossfade to `block`, or applies it at once when null.
  void SetRail(bool rail, views::AnimationSequenceBlock* block);
  bool rail() const { return rail_; }

  // views::View:
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void Layout(PassKey) override;
  // Layout() skips hidden parts (window controls, workspaces), so showing or
  // hiding one lays the panel out again.
  void ChildVisibilityChanged(views::View* child) override;
  void OnPaintBackground(gfx::Canvas* canvas) override;
  void OnThemeChanged() override;

 private:
  class Engraving;
  class FooterLine;
  Parts parts_;
  raw_ptr<Engraving> engraving_ = nullptr;
  raw_ptr<FooterLine> footer_line_ = nullptr;
  gfx::ImageSkia paper_grain_;
  struct Faded {
    raw_ptr<views::View> view;
    bool interactive;
  };
  std::vector<Faded> expanded_only_;
  std::vector<Faded> rail_only_;
  bool rail_ = false;
};

// .sidebar-clock: the accent clock icon, the date at 80% and the time in the
// display face and the accent colour at the right. GetText() keeps the
// whole label.
class SidebarClock : public FlyButton {
  METADATA_HEADER(SidebarClock, FlyButton)

 public:
  explicit SidebarClock(PressedCallback callback);
  ~SidebarClock() override;
  void SetClock(const std::u16string& text,
                const std::u16string& date,
                const std::u16string& time);
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;
  void Layout(PassKey) override;
  void OnThemeChanged() override;

 private:
  raw_ptr<views::Label> date_ = nullptr;
  raw_ptr<views::Label> time_ = nullptr;
};

// The sidebar widget's contents: the shadow (#Sidebar::after) and the
// viewport that clips the panel (.sidebar-viewport), inside the edge
// (#Sidebar). The edge slides by -224px while the panel slides back by the
// same amount, so the compositor reveals content that stays put.
class SidebarRoot : public views::View, public views::ViewTargeterDelegate {
  METADATA_HEADER(SidebarRoot, views::View)

 public:
  explicit SidebarRoot(std::unique_ptr<SidebarPanel> panel);
  SidebarRoot(const SidebarRoot&) = delete;
  SidebarRoot& operator=(const SidebarRoot&) = delete;
  ~SidebarRoot() override;

  SidebarPanel* panel() { return panel_; }
  // #Sidebar: slides by -224px while collapsed.
  views::View* edge() { return edge_; }
  views::View* shadow() { return shadow_; }
  void Layout(PassKey) override;
  // The edge and panel transforms cancel out, and the compositor keeps a
  // layer's starting transform until a slide ends. Events go straight into
  // the panel; the host's hit area already limits them to what is visible.
  views::View* TargetForRect(views::View* root, const gfx::Rect& rect) override;

 private:
  class Edge;
  class Shadow;
  raw_ptr<views::View> edge_ = nullptr;
  raw_ptr<views::View> shadow_ = nullptr;
  raw_ptr<views::View> viewport_ = nullptr;
  raw_ptr<SidebarPanel> panel_ = nullptr;
};
// The sidebar's widget: only the visible panel is hit, so drops (found by
// point, not by event targeting) reach the page beside the rail and under
// the shadow.
class SidebarWidget : public views::Widget {
 public:
  explicit SidebarWidget(base::RepeatingCallback<int()> visible_width);
  ~SidebarWidget() override;
  bool HasHitTestMask() const override;
  void GetHitTestMask(SkPath* mask) const override;

 private:
  base::RepeatingCallback<int()> visible_width_;
};
}  // namespace fly

#endif  // FLY_UI_SIDEBAR_PANEL_H_
