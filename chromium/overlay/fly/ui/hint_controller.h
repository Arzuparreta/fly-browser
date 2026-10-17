// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_HINT_CONTROLLER_H_
#define FLY_UI_HINT_CONTROLLER_H_

#include <memory>
#include <string>
#include <vector>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/accessibility/ax_node_id_forward.h"
#include "ui/accessibility/ax_tree_id.h"
#include "ui/gfx/geometry/rect.h"
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
// On-demand native accessibility targets and a non-activating Aura overlay.
// No page scripts, API shims, IPC bindings, or persistent accessibility mode.
class HintController : public content::WebContentsObserver,
                       public views::WidgetObserver {
 public:
  HintController(views::Widget* desktop,
                 base::RepeatingCallback<void(bool)> state_changed);
  ~HintController() override;
  void Toggle(content::WebContents* contents);
  void Cancel();
  bool HandleKey(content::WebContents* contents,
                 const input::NativeWebKeyboardEvent& event);
  bool active() const { return active_; }
  bool truncated() const { return truncated_; }
  size_t target_count_for_testing() const { return targets_.size(); }

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
    gfx::Rect bounds;
    std::string code;
  };
  void ScheduleCollect();
  void Collect();
  void Draw();
  void ScheduleValidation();
  void ValidateLocations();
  bool Resolve(const Target& target, gfx::Rect* bounds) const;
  void Activate(const Target& target);

  raw_ptr<views::Widget> desktop_;
  base::RepeatingCallback<void(bool)> state_changed_;
  std::unique_ptr<content::ScopedAccessibilityMode> accessibility_;
  std::unique_ptr<views::Widget> overlay_;
  std::vector<Target> targets_;
  std::string prefix_;
  gfx::Rect viewport_;
  bool active_ = false;
  bool ready_ = false;
  bool truncated_ = false;
  bool collect_pending_ = false;
  bool validation_pending_ = false;
  base::OneShotTimer startup_timeout_;
  base::WeakPtrFactory<HintController> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_HINT_CONTROLLER_H_
