// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license.
#ifndef FLY_UI_STARTUP_MAINTENANCE_H_
#define FLY_UI_STARTUP_MAINTENANCE_H_

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/widget/widget_observer.h"

class TabStripModel;
namespace views {
class Widget;
}

namespace fly {
// Once per process: wait for the selected page to paint and for a subsequent
// window frame to be presented. A bounded fallback also covers blank pages,
// slow servers, crashed renderers and minimised windows. Does not poll, load
// background tabs or keep the application alive.
class StartupMaintenance : public content::WebContentsObserver,
                           public TabStripModelObserver,
                           public views::WidgetObserver {
 public:
  explicit StartupMaintenance(base::OnceClosure tasks,
                              base::TimeDelta max_wait = base::Seconds(5));
  ~StartupMaintenance() override;

  bool pending() const { return bool(tasks_); }
  void SetWindow(TabStripModel* tabs, views::Widget* widget);
  // A cancelled quit resumes pending work; an actual shutdown destroys it.
  void SetPaused(bool paused);

 private:
  void ObserveSelectedPage();
  void MaybeRequestPresentation();
  bool HasPaintedPage() const;
  void OnPresented();
  void OnDeadline();
  void Run(const char* reason);
  void StopObserving();

  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void OnFirstContentfulPaintInPrimaryMainFrame(base::TimeTicks) override;
  void PrimaryPageChanged(content::Page&) override;
  void OnVisibilityChanged(content::Visibility) override;
  void WebContentsDestroyed() override;
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;
  void OnWidgetDestroying(views::Widget*) override;

  base::OnceClosure tasks_;
  base::OneShotTimer deadline_;
  bool expired_ = false;
  bool paused_ = false;
  bool presentation_pending_ = false;
  bool content_painted_ = false;
  raw_ptr<TabStripModel> tabs_ = nullptr;
  base::ScopedObservation<views::Widget, views::WidgetObserver> widget_{this};
  base::WeakPtrFactory<StartupMaintenance> presentation_weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_STARTUP_MAINTENANCE_H_
