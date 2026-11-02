// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/page_scroller.h"

#include <algorithm>
#include <utility>

#include "base/memory/weak_ptr.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/page.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_user_data.h"
#include "third_party/blink/public/common/input/synthetic_web_input_event_builders.h"
#include "third_party/blink/public/common/input/web_gesture_event.h"
#include "third_party/blink/public/common/page/page_zoom.h"
#include "third_party/blink/public/mojom/input/input_event_result.mojom-shared.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/compositor/compositor.h"
#include "ui/events/types/scroll_types.h"
#include "ui/gfx/geometry/rect_f.h"

namespace fly {
namespace {
using blink::WebInputEvent;
constexpr auto kDevice = blink::WebGestureDevice::kSyntheticAutoscroll;
// The first frame of a motion: the compositor may report the previous frame.
constexpr base::TimeDelta kMinFirstFrame = base::Milliseconds(4);
constexpr base::TimeDelta kMaxFrame = base::Milliseconds(50);
// Page-granularity deltas, which the compositor resolves against the size of
// the scroller it latches to and animates. It scales pages by 0.875
// (kMinFractionToStepWhenPaging), so this is exactly half of the scroller.
constexpr float kHalfPage = 0.5f / 0.875f;
// A document-sized delta, clamped to the target scroll node's bounds.
// Document granularity avoids per-page step limits in nested scrollers.
constexpr float kDocumentEnd = 1e9f;

// d, u, gg and G jump; the other scrolls move while their key is down.
std::optional<float> JumpAmount(PageScroll scroll) {
  switch (scroll) {
    case PageScroll::kHalfPageDown:
    case PageScroll::kHalfPageUp:
      return kHalfPage;
    case PageScroll::kTop:
    case PageScroll::kBottom:
      return kDocumentEnd;
    default:
      return std::nullopt;
  }
}

// The point a click or an activated hint last selected in a page, in the
// coordinates of the page's root view. It lasts as long as that page.
class ScrollAnchor : public content::WebContentsUserData<ScrollAnchor> {
 public:
  ~ScrollAnchor() override = default;
  base::WeakPtr<content::Page> page;
  gfx::PointF point;

 private:
  friend class content::WebContentsUserData<ScrollAnchor>;
  explicit ScrollAnchor(content::WebContents* contents)
      : content::WebContentsUserData<ScrollAnchor>(*contents) {}
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
WEB_CONTENTS_USER_DATA_KEY_IMPL(ScrollAnchor);

// Page scroll offsets grow downwards and rightwards.
gfx::Vector2dF DirectionOf(PageScroll scroll) {
  switch (scroll) {
    case PageScroll::kDown:
    case PageScroll::kHalfPageDown:
    case PageScroll::kBottom:
      return {0, 1};
    case PageScroll::kUp:
    case PageScroll::kHalfPageUp:
    case PageScroll::kTop:
      return {0, -1};
    case PageScroll::kLeft:
      return {-1, 0};
    case PageScroll::kRight:
      return {1, 0};
  }
}
}  // namespace

std::optional<PageScroll> PageScrollForAction(std::string_view action) {
  constexpr std::pair<std::string_view, PageScroll> kActions[] = {
      {"scroll-down", PageScroll::kDown},
      {"scroll-up", PageScroll::kUp},
      {"scroll-left", PageScroll::kLeft},
      {"scroll-right", PageScroll::kRight},
      {"half-page-down", PageScroll::kHalfPageDown},
      {"half-page-up", PageScroll::kHalfPageUp},
      {"page-top", PageScroll::kTop},
      {"page-bottom", PageScroll::kBottom}};
  for (const auto& [name, scroll] : kActions) {
    if (name == action)
      return scroll;
  }
  return std::nullopt;
}

PageScroller::PageScroller(aura::Window* window) : window_(window) {
  if (window_)
    window_->AddObserver(this);
  // A key may legitimately stay down for longer than the compositor's
  // runaway-animation check allows.
  set_check_active_duration(false);
}

PageScroller::~PageScroller() {
  Stop();
  Animate(false);
  if (window_)
    window_->RemoveObserver(this);
}

// static
void PageScroller::SetAnchor(content::WebContents* contents,
                             const gfx::PointF& screen_point) {
  auto* view = contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!view)
    return;
  const auto bounds = gfx::RectF(view->GetViewBounds());
  if (!bounds.Contains(screen_point))
    return;
  ScrollAnchor::CreateForWebContents(contents);
  auto* anchor = ScrollAnchor::FromWebContents(contents);
  anchor->page = contents->GetPrimaryPage().GetWeakPtr();
  anchor->point = screen_point - bounds.OffsetFromOrigin();
}

void PageScroller::Press(content::WebContents* contents,
                         PageScroll scroll,
                         uint32_t key,
                         bool repeat) {
  // A jump happens once per press.
  if (repeat && JumpAmount(scroll))
    return;
  if (repeat && motion_ && key == key_) {
    // Auto-repeat must not restart the animation or queue distance.
    return;
  }
  // A repeat without a motion: the key went down before the page had focus.
  Start(contents, scroll, key);
}

void PageScroller::Release(uint32_t key) {
  if (motion_ && key == key_)
    held_ = false;
}

void PageScroller::OtherKey(uint32_t key) {
  if (motion_ && key != key_)
    held_ = false;
}

void PageScroller::Step(content::WebContents* contents, PageScroll scroll) {
  Start(contents, scroll, std::nullopt);
}

void PageScroller::Start(content::WebContents* contents,
                         PageScroll scroll,
                         std::optional<uint32_t> key) {
  if (!contents)
    return;
  const auto direction = DirectionOf(scroll);
  if (const auto amount = JumpAmount(scroll)) {
    Stop();
    direction_ = direction;
    scroll_ = scroll;
    jump_ = *amount;
    if (Begin(contents, true))
      SendUpdate(gfx::ScaleVector2d(direction_, jump_), true);
    // The compositor animates the jump; the gesture can end now. A jump from
    // a clicked point that scrolls nothing is retried from the middle.
    if (!first_ack_pending_)
      End();
    return;
  }
  const auto now = base::TimeTicks::Now();
  // Another press of the same scroll adds a step to the running motion.
  if (motion_ && widget_ && scroll == scroll_ && web_contents() == contents) {
    motion_->Extend();
  } else {
    Stop();
    direction_ = direction;
    if (!Begin(contents, true))
      return;
    // Calibrate in CSS pixels, then scale the gesture's DIP delta. Zoom must
    // not change the held-key speed or the motion's timing.
    motion_.emplace(kStep);
    page_zoom_ = blink::ZoomLevelToZoomFactor(
        content::HostZoomMap::GetZoomLevel(contents));
    scroll_ = scroll;
    last_frame_ = {};
    Animate(true);
  }
  held_ = key.has_value();
  key_ = key.value_or(0);
  pressed_ = now;
}

bool PageScroller::Begin(content::WebContents* contents, bool use_anchor) {
  auto* root = contents->GetRenderWidgetHostView();
  if (!root || contents->GetVisibility() != content::Visibility::VISIBLE)
    return false;
  // Vimium scrolls in the frame that has focus; a frame in another process
  // is its own widget.
  auto* frame = contents->GetFocusedFrame();
  auto* widget =
      frame ? frame->GetRenderWidgetHost() : root->GetRenderWidgetHost();
  auto* view = widget ? widget->GetView() : nullptr;
  if (!view)
    return false;
  const auto bounds = gfx::RectF(view->GetViewBounds());
  point_ = gfx::PointF(bounds.width() / 2, bounds.height() / 2);
  anchored_ = false;
  auto* anchor = ScrollAnchor::FromWebContents(contents);
  if (use_anchor && anchor && anchor->page &&
      anchor->page.get() == &contents->GetPrimaryPage()) {
    auto point = anchor->point;
    if (view != root)
      point = view->TransformRootPointToViewCoordSpace(point);
    if (gfx::RectF(bounds.size()).Contains(point)) {
      point_ = point;
      anchored_ = true;
    }
  }
  screen_point_ = point_ + bounds.OffsetFromOrigin();
  Observe(contents);
  widget_ = widget;
  widget_->AddObserver(this);
  widget_->AddInputEventObserver(this);
  SendBegin();
  return true;
}

void PageScroller::SendBegin() {
  // Gesture deltas follow the finger: the opposite of the scroll offset.
  auto event = blink::SyntheticWebGestureEventBuilder::BuildScrollBegin(
      -direction_.x(), -direction_.y(), kDevice);
  event.data.scroll_begin.delta_hint_units =
      ui::ScrollGranularity::kScrollByPrecisePixel;
  event.SetPositionInWidget(point_);
  event.SetPositionInScreen(screen_point_);
  first_ack_pending_ = anchored_;
  widget_->ForwardGestureEvent(event);
}

void PageScroller::SendUpdate(const gfx::Vector2dF& delta, bool animated_jump) {
  if (first_ack_pending_)
    anchor_probe_ = delta;
  auto event = blink::SyntheticWebGestureEventBuilder::BuildScrollUpdate(
      -delta.x(), -delta.y(), 0, kDevice);
  // Jumps animate in the compositor. Document ends must not be interpreted
  // as page steps: nested scrollers can limit those to a single page.
  event.data.scroll_update.delta_units =
      ui::ScrollGranularity::kScrollByPrecisePixel;
  if (animated_jump) {
    event.data.scroll_update.delta_units =
        scroll_ == PageScroll::kTop || scroll_ == PageScroll::kBottom
            ? ui::ScrollGranularity::kScrollByDocument
            : ui::ScrollGranularity::kScrollByPage;
  }
  event.SetPositionInWidget(point_);
  event.SetPositionInScreen(screen_point_);
  widget_->ForwardGestureEvent(event);
}

void PageScroller::End() {
  if (!widget_)
    return;
  auto* widget = widget_.get();
  widget_ = nullptr;
  first_ack_pending_ = false;
  anchor_probe_.reset();
  auto event = blink::SyntheticWebGestureEventBuilder::BuildScrollEnd(kDevice);
  event.SetPositionInWidget(point_);
  event.SetPositionInScreen(screen_point_);
  widget->ForwardGestureEvent(event);
  widget->RemoveInputEventObserver(this);
  widget->RemoveObserver(this);
}

void PageScroller::Stop() {
  End();
  motion_.reset();
  held_ = false;
  jump_ = 0;
  Animate(false);
}

void PageScroller::Animate(bool on) {
  if (on) {
    if (compositor_)
      return;
    auto* host = window_ ? window_->GetHost() : nullptr;
    compositor_ = host ? host->compositor() : nullptr;
    if (compositor_)
      compositor_->AddAnimationObserver(this);
  } else if (compositor_) {
    compositor_->RemoveAnimationObserver(this);
    compositor_ = nullptr;
  }
}

void PageScroller::OnAnimationStep(base::TimeTicks timestamp) {
  if (!motion_ || !widget_) {
    Stop();
    return;
  }
  const auto now = base::TimeTicks::Now();
  // The first frame moves at once, as if one frame had passed since the
  // press: Vimium waits a frame here.
  const auto elapsed =
      last_frame_.is_null()
          ? std::clamp(timestamp - pressed_, kMinFirstFrame, kMaxFrame)
          : std::min(timestamp - last_frame_, kMaxFrame);
  if (!last_frame_.is_null() && timestamp <= last_frame_)
    return;
  last_frame_ = timestamp;
  // Resolve one anchored update before queueing more. If it cannot scroll,
  // replay its distance at the fallback point so a tap still moves one step.
  if (first_ack_pending_ && anchor_probe_)
    return;
  const double distance = motion_->Advance(
      elapsed.InSecondsF(), held_ ? (now - pressed_).InSecondsF() : -1);
  if (distance > 0)
    SendUpdate(gfx::ScaleVector2d(direction_, distance * page_zoom_), false);
  if (!held_ && motion_->Finished())
    Stop();
}

void PageScroller::OnInputEventAck(const content::RenderWidgetHost& widget,
                                   blink::mojom::InputEventResultSource,
                                   blink::mojom::InputEventResultState state,
                                   const blink::WebInputEvent& event) {
  if (!first_ack_pending_ || &widget != widget_.get() ||
      event.GetType() != WebInputEvent::Type::kGestureScrollUpdate ||
      static_cast<const blink::WebGestureEvent&>(event).SourceDevice() !=
          kDevice) {
    return;
  }
  first_ack_pending_ = false;
  auto* contents = web_contents();
  const float jump = jump_;
  const auto probe = anchor_probe_;
  anchor_probe_.reset();
  if (state == blink::mojom::InputEventResultState::kConsumed) {
    if (jump)
      End();
    return;
  }
  // Nothing under the clicked point scrolls this way, so scroll at the middle
  // of the frame, as Vimium falls back to the page's main scroller.
  End();
  if (!contents || !Begin(contents, false))
    return Stop();
  if (jump) {
    SendUpdate(gfx::ScaleVector2d(direction_, jump), true);
    End();
  } else if (probe) {
    SendUpdate(*probe, false);
  }
}

void PageScroller::OnCompositingShuttingDown(ui::Compositor* compositor) {
  Stop();
  if (compositor_ == compositor) {
    compositor_->RemoveAnimationObserver(this);
    compositor_ = nullptr;
  }
}

void PageScroller::PrimaryPageChanged(content::Page&) {
  Stop();
}

void PageScroller::OnWindowDestroying(aura::Window* window) {
  Stop();
  window->RemoveObserver(this);
  window_ = nullptr;
}

void PageScroller::OnVisibilityChanged(content::Visibility visibility) {
  if (visibility != content::Visibility::VISIBLE)
    Stop();
}

void PageScroller::OnWebContentsLostFocus(content::RenderWidgetHost*) {
  Stop();
}

void PageScroller::WebContentsDestroyed() {
  Stop();
  Observe(nullptr);
}

void PageScroller::RenderWidgetHostDestroyed(
    content::RenderWidgetHost* widget) {
  if (widget != widget_)
    return;
  // No gesture end can reach a destroyed widget.
  widget_->RemoveInputEventObserver(this);
  widget_->RemoveObserver(this);
  widget_ = nullptr;
  Stop();
}

}  // namespace fly
