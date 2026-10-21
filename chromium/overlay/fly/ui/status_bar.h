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
#include "base/timer/timer.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"
#include "ui/views/view_tracker.h"
#include "url/gurl.h"

class BrowserWindowInterface;
namespace views {
class BubbleDialogDelegate;
class Widget;
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
    // zoom-in, zoom-out or reset-zoom from the zoom controls.
    base::RepeatingCallback<void(std::string)> zoom;
    // A click on the mode badge (Electron: cancel hints, leave or enter
    // passthrough).
    base::RepeatingClosure mode;
    base::RepeatingClosure notifications;
    base::RepeatingClosure workspace;
    base::RepeatingClosure clock;
    // The shown page changed (tab switch, navigation commit).
    base::RepeatingClosure contents_changed;
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
  // Electron's setTargetUrl(text, timeout): a short message in the link
  // area, replaced by the next hovered link.
  void ShowMessage(std::u16string text, base::TimeDelta timeout);
  std::u16string message_for_testing() const { return message_; }
  bool zoom_controls_open_for_testing() const;
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

  // Light mode lays Electron's paper grain over the bar.
  void SetPaperGrain(gfx::ImageSkia grain);
  void OnPaintBackground(gfx::Canvas* canvas) override;

 private:
  gfx::ImageSkia paper_grain_;
  std::u16string message_;
  base::OneShotTimer message_timer_;
  base::OneShotTimer zoom_flash_;
  base::RepeatingCallback<void(std::string)> zoom_action_;
  std::unique_ptr<views::BubbleDialogDelegate> zoom_delegate_;
  std::unique_ptr<views::Widget> zoom_widget_;
  void ToggleZoomControls();
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
  base::RepeatingClosure contents_changed_;
  bool private_ = false;
  GURL target_;
  std::unique_ptr<TargetObserver> target_observer_;
  base::ScopedObservation<zoom::ZoomController, zoom::ZoomObserver>
      zoom_observation_{this};
  base::WeakPtrFactory<StatusBar> weak_factory_{this};
};
}  // namespace fly
#endif
