// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_PAGE_SCROLLER_H_
#define FLY_UI_PAGE_SCROLLER_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "content/public/browser/render_widget_host.h"
#include "content/public/browser/render_widget_host_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "fly/commands/scroll_motion.h"
#include "ui/aura/window_observer.h"
#include "ui/compositor/compositor_animation_observer.h"
#include "ui/gfx/geometry/point_f.h"
#include "ui/gfx/geometry/vector2d_f.h"

namespace aura {
class Window;
}
namespace ui {
class Compositor;
}

namespace fly {

enum class PageScroll {
  kDown,
  kUp,
  kLeft,
  kRight,
  kHalfPageDown,
  kHalfPageUp,
  kTop,
  kBottom,
};
// The scroll an action names ("scroll-down", "page-top", ...), if any.
std::optional<PageScroll> PageScrollForAction(std::string_view action);

// Scrolls pages the way Vimium does (see ScrollMotion): a key press moves at
// once at a steady speed, holding the key keeps moving, and releasing it stops
// after at least one step. Half pages (d, u) and the ends (gg, G) are one
// jump per press, which the page's compositor animates over the size of the
// scroller it reaches. The keyboard's auto-repeat only confirms that the
// key is still down, so there is no pause before a held key starts moving and
// no burst of restarted animations afterwards.
//
// The motion is a synthetic autoscroll gesture sent to the page's compositor
// on every frame of the browser window. Eligible scrollers move on the
// compositor thread. It dispatches no key or wheel events to the page and
// does not call page-overridable JavaScript scrolling methods. Like Vimium,
// which scrolls the element the user last clicked, it scrolls the scroller
// under the last click on the page, or under the middle of the focused frame.
// When the clicked point can scroll nothing in that direction, it falls back to
// the middle of the frame.
class PageScroller : public ui::CompositorAnimationObserver,
                     public aura::WindowObserver,
                     public content::WebContentsObserver,
                     public content::RenderWidgetHostObserver,
                     public content::RenderWidgetHost::InputEventObserver {
 public:
  // Vimium's default step (scrollStepSize), in CSS pixels.
  static constexpr double kStep = 60;

  // `window` is the browser window whose compositor paces the motion.
  explicit PageScroller(aura::Window* window);
  ~PageScroller() override;
  PageScroller(const PageScroller&) = delete;
  PageScroller& operator=(const PageScroller&) = delete;

  // A key press bound to `scroll`. `key` (a ui::DomCode) is the physical key
  // whose release ends a held scroll.
  void Press(content::WebContents* contents,
             PageScroll scroll,
             uint32_t key,
             bool repeat);
  // Any key release.
  void Release(uint32_t key);
  // Any other key press ends a held scroll, as in Vimium.
  void OtherKey(uint32_t key);
  // One step without a key (Command Centre, menus).
  void Step(content::WebContents* contents, PageScroll scroll);
  // Ends any motion at once.
  void Stop();
  // Later scrolls of `contents` target the scroller under `screen_point`
  // (a click or an activated link hint), until its page changes.
  static void SetAnchor(content::WebContents* contents,
                        const gfx::PointF& screen_point);

  bool scrolling() const { return !!widget_; }
  bool held_for_testing() const { return held_; }

  // ui::CompositorAnimationObserver:
  void OnAnimationStep(base::TimeTicks timestamp) override;
  void OnCompositingShuttingDown(ui::Compositor* compositor) override;
  // aura::WindowObserver:
  void OnWindowDestroying(aura::Window* window) override;
  // content::WebContentsObserver:
  void PrimaryPageChanged(content::Page& page) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void OnWebContentsLostFocus(content::RenderWidgetHost*) override;
  void WebContentsDestroyed() override;
  // content::RenderWidgetHostObserver:
  void RenderWidgetHostDestroyed(content::RenderWidgetHost* widget) override;
  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEventAck(const content::RenderWidgetHost& widget,
                       blink::mojom::InputEventResultSource source,
                       blink::mojom::InputEventResultState state,
                       const blink::WebInputEvent& event) override;

 private:
  void Start(content::WebContents* contents,
             PageScroll scroll,
             std::optional<uint32_t> key);
  // Starts a gesture on `contents`' focused frame, at the anchor if
  // `use_anchor` and it lies in that frame, otherwise at its middle.
  bool Begin(content::WebContents* contents, bool use_anchor);
  void SendBegin();
  void SendUpdate(const gfx::Vector2dF& delta, bool whole_pages);
  // Ends the gesture; Stop() also forgets the motion.
  void End();
  void Animate(bool on);

  raw_ptr<aura::Window> window_;
  raw_ptr<ui::Compositor> compositor_ = nullptr;

  // The gesture in progress.
  raw_ptr<content::RenderWidgetHost> widget_ = nullptr;
  gfx::PointF point_;
  gfx::PointF screen_point_;
  // The direction of travel (a unit vector, in page scroll offsets).
  gfx::Vector2dF direction_;
  bool anchored_ = false;
  bool first_ack_pending_ = false;
  std::optional<gfx::Vector2dF> anchor_probe_;
  // d, u, gg and G: the pages of one animated update instead of a motion.
  float jump_ = 0;

  std::optional<ScrollMotion> motion_;
  double page_zoom_ = 1;
  PageScroll scroll_ = PageScroll::kDown;
  bool held_ = false;
  uint32_t key_ = 0;
  base::TimeTicks pressed_;
  base::TimeTicks last_frame_;
};

}  // namespace fly

#endif  // FLY_UI_PAGE_SCROLLER_H_
