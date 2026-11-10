// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_HINT_CONTROLLER_H_
#define FLY_UI_HINT_CONTROLLER_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class ScopedAccessibilityMode;
}
namespace input {
struct NativeWebKeyboardEvent;
}
namespace views {
class Widget;
}
namespace fly {
class HintLabel;

// Link hints from on-demand native accessibility targets, drawn in a
// non-activating Aura overlay. No page scripts, API shims, IPC bindings, or
// persistent accessibility mode.
//
// Like Vimium's, the hints stay until a label is typed, Escape or the hint
// key is pressed, or the page goes away. They follow their elements when the
// page scrolls or its layout moves, and hide while an element is off screen.
// Blink sends a newly enabled accessibility tree in several updates, so the
// targets are gathered again while updates arrive, until a label is typed.
class HintController : public content::WebContentsObserver,
                       public views::WidgetObserver {
 public:
  // What typing a label does.
  enum class Mode {
    kActivate,       // Click the element, or focus it for typing.
    kBackgroundTab,  // Open a link in a background tab; other elements as
                     // kActivate.
    kFocusInput,     // No labels: focus the first text field on screen.
  };
  // `open_link` opens a link of the page in a background tab.
  HintController(views::Widget* desktop,
                 base::RepeatingCallback<void(bool)> state_changed,
                 base::RepeatingCallback<void(content::WebContents*, GURL)>
                     open_link);
  ~HintController() override;
  void Toggle(content::WebContents* contents, Mode mode = Mode::kActivate);
  void Cancel();
  Mode mode() const { return mode_; }
  // Why the last session ended ("command", "navigation", "no-input"...).
  std::string_view cancellation() const { return cancellation_; }
  bool HandleKey(content::WebContents* contents,
                 const input::NativeWebKeyboardEvent& event);
  bool active() const { return active_; }
  bool truncated() const { return truncated_; }
  std::string_view cancellation_for_testing() const { return cancellation_; }
  size_t target_count_for_testing() const { return targets_.size(); }
  size_t visible_count_for_testing() const;
  // The label of the first target whose name contains `name`, if any.
  std::string code_for_testing(std::u16string_view name) const;
  gfx::Rect label_bounds_for_testing(std::u16string_view name) const;

  void AccessibilityEventReceived(const ui::AXUpdatesAndEvents&) override;
  void AccessibilityLocationChangesReceived(
      const ui::AXTreeID&,
      ui::AXLocationAndScrollUpdates&) override;
  void PrimaryPageChanged(content::Page&) override;
  void RenderFrameDeleted(content::RenderFrameHost*) override;
  void OnVisibilityChanged(content::Visibility visibility) override;
  void OnWebContentsLostFocus(content::RenderWidgetHost*) override;
  void WebContentsDestroyed() override;
  void OnWidgetBoundsChanged(views::Widget*, const gfx::Rect&) override;
  void OnWidgetActivationChanged(views::Widget*, bool active) override;
  void OnWidgetDestroying(views::Widget*) override;

 private:
  struct Target {
    ui::AXTreeID tree;
    ui::AXNodeID node;
    // Current screen bounds, clipped to the viewport; empty while hidden.
    gfx::Rect bounds;
    std::string code;
    raw_ptr<HintLabel> label = nullptr;
  };
  void CancelWithReason(std::string_view reason);
  void ScheduleUpdate();
  void Update();
  // Gathers the targets on screen. Returns false if the tree is not there.
  bool Collect(std::vector<Target>* targets) const;
  void Relabel(std::vector<Target> targets);
  // Moves every label to its element's current bounds.
  void Relayout();
  void Draw();
  gfx::Rect CurrentBounds(const Target& target) const;
  void Activate(const Target& target);
  // kFocusInput: focuses the first text field among `targets`.
  bool FocusFirstInput(const std::vector<Target>& targets);
  // The address of the link `target` is or sits in, if it is one Fly may
  // open in another tab.
  GURL LinkOf(const Target& target) const;

  raw_ptr<views::Widget> desktop_;
  base::RepeatingCallback<void(bool)> state_changed_;
  base::RepeatingCallback<void(content::WebContents*, GURL)> open_link_;
  Mode mode_ = Mode::kActivate;
  std::unique_ptr<content::ScopedAccessibilityMode> accessibility_;
  std::unique_ptr<views::Widget> overlay_;
  std::vector<Target> targets_;
  std::string prefix_;
  gfx::Rect viewport_;
  std::string_view cancellation_ = "not-started";
  bool active_ = false;
  bool ready_ = false;
  bool truncated_ = false;
  bool update_pending_ = false;
  // Tree updates since the last collection may have added targets.
  bool tree_changed_ = false;
  bool codes_frozen_ = false;
  base::OneShotTimer startup_timeout_;
  base::WeakPtrFactory<HintController> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_HINT_CONTROLLER_H_
