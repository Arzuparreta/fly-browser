// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_STATUS_BAR_H_
#define FLY_UI_STATUS_BAR_H_

#include <cstdint>
#include <memory>
#include <string>
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "chrome/browser/ui/tabs/tab_strip_model_observer.h"
#include "components/zoom/zoom_controller.h"
#include "components/zoom/zoom_observer.h"
#include "content/public/browser/web_contents_observer.h"
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
// Page and profile state, laid out like i3bar. On the left the keyboard mode
// block, the keys of an unfinished sequence, stop/reload and the address line
// (the page's address, or a hovered link, the loading destination or a short
// message). On the right extension actions, notifications and the segments:
// tab position, zoom, connection, workspace and clock. Link targets arrive from the browser
// delegate; page code never receives a privileged UI binding.
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
    // A click on the mode badge: cancel hints, leave or enter passthrough.
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
  void SetClock(const std::u16string& text,
                const std::u16string& tooltip,
                bool visible);
  void SetClockVisible(bool visible);
  void SetNotificationsEnabled(bool enabled);
  // A short message in the link area, replaced by the next hovered link.
  void ShowMessage(std::u16string text, base::TimeDelta timeout);
  // The keys typed so far of a NORMAL-mode sequence ("g" of "g g"), shown
  // beside the mode until the sequence ends or lapses.
  void SetPendingKeys(std::u16string keys);
  std::u16string pending_keys_for_testing() const;
  // The page's own address as the bar shows it, or nothing while a link, a
  // loading destination or a message takes its place.
  std::u16string address_for_testing() const;
  FlyButton* tabs_for_testing() const;
  std::u16string message_for_testing() const { return message_; }
  bool zoom_controls_open_for_testing() const;
  // Container for per-profile extension action buttons.
  views::View* extensions_container() const { return extensions_; }
  views::Label* mode_label() const;
  views::Label* location_for_testing() const;
  FlyButton* security_for_testing() const;
  FlyButton* workspace_for_testing() const;
  FlyButton* zoom_for_testing() const;
  FlyButton* clock_for_testing() const;
  FlyButton* reload_for_testing() const { return reload_; }

  void OnTabStripModelChanged(TabStripModel*,
                              const TabStripModelChange&,
                              const TabStripSelectionChange&) override;
  void OnTabStripModelDestroyed(TabStripModel*) override;
  void DidFinishNavigation(content::NavigationHandle*) override;
  void DidStartNavigation(content::NavigationHandle*) override;
  void DidRedirectNavigation(content::NavigationHandle*) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void DidChangeVisibleSecurityState() override;
  void WebContentsDestroyed() override;
  void OnZoomControllerDestroyed(zoom::ZoomController*) override;
  void OnZoomChanged(const zoom::ZoomController::ZoomChangedEventData&) override;

  // Light mode lays the paper grain over the bar.
  void SetPaperGrain(gfx::ImageSkia grain);
  void OnPaintBackground(gfx::Canvas* canvas) override;
  gfx::Size CalculatePreferredSize(
      const views::SizeBounds& available) const override;

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
  void UpdateTabPosition();
  void ReloadOrStop();
  base::WeakPtr<BrowserWindowInterface> browser_;
  raw_ptr<TabStripModel> model_ = nullptr;
  raw_ptr<FlyLabel> mode_;
  raw_ptr<FlyLabel> keys_;
  base::OneShotTimer keys_timer_;
  raw_ptr<FlyLabel> site_;
  raw_ptr<FlyLabel> path_ = nullptr;
  raw_ptr<FlyLabel> location_ = nullptr;
  raw_ptr<FlyButton> reload_;
  raw_ptr<StatusBadge> tabs_;
  raw_ptr<StatusBadge> zoom_;
  raw_ptr<views::View> extensions_;
  raw_ptr<FlyButton> notifications_;
  raw_ptr<StatusBadge> security_;
  raw_ptr<StatusBadge> workspace_;
  raw_ptr<StatusBadge> clock_;
  base::RepeatingClosure contents_changed_;
  bool private_ = false;
  GURL target_;
  // Only the selected tab's pending primary navigation, before commit. A
  // superseded request finishing must not erase the next request's feedback.
  GURL pending_url_;
  int64_t pending_navigation_id_ = 0;
  std::unique_ptr<TargetObserver> target_observer_;
  base::ScopedObservation<zoom::ZoomController, zoom::ZoomObserver>
      zoom_observation_{this};
  base::WeakPtrFactory<StatusBar> weak_factory_{this};
};
}  // namespace fly
#endif
