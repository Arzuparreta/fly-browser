// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_NEW_TAB_UI_H_
#define FLY_UI_NEW_TAB_UI_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "fly/ui/history_service.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/geometry/rect_f.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}
namespace views {
class Widget;
}

namespace fly {
class NewTabArtworkView;
// The artwork belongs to the tab, not its outgoing renderer: Chromium can
// retain that renderer's last surface long after destroying the document.
// Only the small transparent artwork rectangle repaints, at pose boundaries.
class NewTabArtwork : public content::WebContentsObserver,
                      public content::WebContentsUserData<NewTabArtwork>,
                      public aura::WindowObserver {
 public:
  ~NewTabArtwork() override;
  void Update(const gfx::RectF& relative_bounds, bool reduced_motion);

  // The tab shows the new-tab fly: on its page or, after leaving it, until
  // the destination paints.
  bool active() const { return active_; }
  bool visible_for_testing() const;
  int pose_for_testing() const { return pose_; }
  bool timer_running_for_testing() const { return timer_.IsRunning(); }
  size_t ticks_for_testing() const { return ticks_; }
  gfx::Rect bounds_for_testing() const;
  std::optional<bool> painted_dark_for_testing() const;
  size_t paints_for_testing() const;

 private:
  friend class content::WebContentsUserData<NewTabArtwork>;
  explicit NewTabArtwork(content::WebContents* contents);
  void Sync();
  void Tick();
  void SetPose(int pose);
  void Clear();
  void DestroyOverlay();
  void DidStartNavigation(content::NavigationHandle*) override;
  void DidFinishNavigation(content::NavigationHandle*) override;
  void DidStopLoading() override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void OnFirstContentfulPaintInPrimaryMainFrame(base::TimeTicks) override;
  void OnVisibilityChanged(content::Visibility) override;
  void PrimaryMainFrameRenderProcessGone(base::TerminationStatus) override;
  void WebContentsDestroyed() override;
  void OnWindowBoundsChanged(aura::Window*,
                             const gfx::Rect&,
                             const gfx::Rect&,
                             ui::PropertyChangeReason) override;
  void OnWindowVisibilityChanged(aura::Window*, bool) override;
  void OnWindowAdded(aura::Window*) override;
  void OnWindowDestroying(aura::Window*) override;

  std::unique_ptr<views::Widget> overlay_;
  raw_ptr<NewTabArtworkView> artwork_ = nullptr;
  raw_ptr<aura::Window> parent_ = nullptr;
  gfx::RectF relative_bounds_;
  base::OneShotTimer timer_;
  base::TimeTicks started_;
  int64_t navigation_ = 0;
  int pose_ = 0;
  size_t ticks_ = 0;
  bool active_ = false;
  bool loading_ = false;
  bool committed_ = false;
  bool reduced_motion_ = false;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

// Test observation and a completion gate for deterministic theme-decode races.
struct NewTabImageCacheState {
  std::optional<bool> dark;
  size_t bytes;
  size_t decodes;
};
NewTabImageCacheState NewTabImageCacheStateForTesting();
void SetNewTabImageDecodeGateForTesting(
    base::RepeatingCallback<void(base::OnceClosure)> gate);

// One demand-driven history snapshot for all regular new tabs. Private pages
// never subscribe or receive it. Invalidation clears stale/deleted entries even
// while no page is visible; opening another tab reuses a valid snapshot.
class NewTabHistory {
 public:
  NewTabHistory(ProfileManager& manager,
                WorkspaceService& workspaces,
                PrefService& prefs);
  ~NewTabHistory();
  base::CallbackListSubscription Subscribe(base::RepeatingClosure changed);
  const std::vector<HistoryEntry>& entries() const { return entries_; }
  size_t query_count_for_testing() const { return query_count_; }

 private:
  void OnWorkspacesChanged();
  void Invalidate();
  void EnsureQuery();
  void OnQuery(std::optional<std::vector<HistoryEntry>> rows);
  const raw_ref<WorkspaceService> workspaces_;
  std::optional<std::vector<std::pair<std::string, std::string>>> catalog_;
  HistoryService history_;
  PrefChangeRegistrar registrar_;
  base::RepeatingClosureList changed_;
  std::vector<HistoryEntry> entries_;
  bool valid_ = false;
  bool querying_ = false;
  size_t query_count_ = 0;
  base::WeakPtrFactory<NewTabHistory> weak_factory_{this};
};

// Fly's quiet new tab page: artwork and entry points to native navigation.
// Served from
// <out>/fly_resources/new_tab.
inline constexpr char kFlyNewTabHost[] = "fly-new-tab";
inline constexpr char kFlyNewTabURL[] = "chrome://fly-new-tab/";

// New tabs and blank pages are both "new tab" in Fly's interface.
bool IsFlyNewTabPage(const GURL& url);
// Whether `contents` shows the new-tab fly, so the sidebar hides its engraved
// one. The fly stays after leaving the page until the destination paints.
bool ShowsNewTabFly(content::WebContents* contents);
// BrowserURLHandler rewrite: chrome://newtab (Chromium's new tab page, also
// the start page) becomes Fly's page in every profile.
bool RewriteNewTabURL(GURL* url, content::BrowserContext* browser_context);
// Adds the page to Chromium's WebUI registry; safe to call more than once.
void RegisterFlyNewTabPage();
}  // namespace fly

#endif  // FLY_UI_NEW_TAB_UI_H_
