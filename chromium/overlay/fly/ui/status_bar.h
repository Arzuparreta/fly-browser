// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_STATUS_BAR_H_
#define FLY_UI_STATUS_BAR_H_

#include <memory>
#include <string>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/zoom/zoom_controller.h"
#include "components/zoom/zoom_observer.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/views/view.h"
#include "url/gurl.h"

class BrowserWindowInterface;
namespace views {
class Label;
}
namespace fly {
class FlyButton;
class FlyLabel;
class StatusBadge;
// Read-only presentation of native page/profile state, laid out like the
// Electron footer: mode and hovered link on the left; zoom, extension
// actions, notifications, connection, workspace and clock badges on the right.
// Link targets arrive from the browser delegate; page code never receives a
// privileged UI binding.
class StatusBar : public views::View,
                  public TabStripModelObserver,
                  public content::WebContentsObserver,
                  public zoom::ZoomObserver {
 public:
  METADATA_HEADER(StatusBar, views::View)

 public:
  struct Actions {
    base::RepeatingClosure zoom;
    base::RepeatingClosure notifications;
    base::RepeatingClosure workspace;
    base::RepeatingClosure clock;
  };
  explicit StatusBar(Actions actions = {});
  ~StatusBar() override;
  void SetBrowser(BrowserWindowInterface* browser, std::u16string workspace);
  void SetTargetURL(content::WebContents* source, const GURL& url);
  void SetClock(const std::u16string& date,
                const std::u16string& time,
                const std::u16string& tooltip,
                bool visible);
  void SetClockVisible(bool visible);
  void SetNotificationsEnabled(bool enabled);
  // Container for per-profile extension action buttons.
  views::View* extensions_container() const { return extensions_; }
  views::Label* mode_label() const;
  views::Label* location_for_testing() const;
  FlyButton* security_for_testing() const;
  FlyButton* workspace_for_testing() const;
  FlyButton* zoom_for_testing() const;

  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;
  void DidFinishNavigation(content::NavigationHandle*) override;
  void DidChangeVisibleSecurityState() override;
  void WebContentsDestroyed() override;
  void OnZoomControllerDestroyed(zoom::ZoomController*) override;
  void OnZoomChanged(const zoom::ZoomController::ZoomChangedEventData&) override;

 private:
  class TargetObserver;
  void ClearTarget();
  void SelectContents();
  void Update();
  void UpdateZoom();
  base::WeakPtr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<FlyLabel> mode_;
  raw_ptr<FlyLabel> location_;
  raw_ptr<FlyButton> zoom_;
  raw_ptr<views::View> extensions_;
  raw_ptr<FlyButton> notifications_;
  raw_ptr<StatusBadge> security_;
  raw_ptr<StatusBadge> workspace_;
  raw_ptr<StatusBadge> clock_;
  raw_ptr<FlyLabel> clock_time_;
  bool private_ = false;
  GURL target_;
  std::unique_ptr<TargetObserver> target_observer_;
  base::ScopedObservation<zoom::ZoomController, zoom::ZoomObserver>
      zoom_observation_{this};
};
}  // namespace fly
#endif
