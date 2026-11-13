// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_BLOCKER_STARTUP_THROTTLE_H_
#define FLY_UI_BLOCKER_STARTUP_THROTTLE_H_

#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "content/public/browser/navigation_throttle.h"
#include "extensions/browser/api/declarative_net_request/rules_monitor_service.h"

namespace content {
class NavigationThrottleRegistry;
}

namespace fly {

// Holds a page's request while an extension's declarativeNetRequest rules
// are still loading, so that the first pages of a start (restored tabs, an
// address given on the command line) are filtered like every later one.
// Chromium only waits for extensions' content scripts at startup
// (extensions::UserScriptListener); its request rules apply from whenever
// they finish loading, and the requests sent before that are not filtered.
// It works for any blocker that uses declarativeNetRequest, including the
// bundled uBlock Origin Lite. Once every loaded extension's rules are in
// effect this adds nothing, and it never holds a page longer than kTimeout.
class BlockerStartupThrottle
    : public content::NavigationThrottle,
      public extensions::declarative_net_request::RulesMonitorService::
          InitialLoadObserver {
 public:
  static constexpr base::TimeDelta kTimeout = base::Seconds(5);

  // Adds the throttle while rules are loading in the navigation's profile.
  // First loads the bundled extensions that profile still lacks, so that
  // Chromium's user script listener, which runs next, also holds the page
  // for their content scripts.
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  BlockerStartupThrottle(
      content::NavigationThrottleRegistry& registry,
      extensions::declarative_net_request::RulesMonitorService& rules);
  ~BlockerStartupThrottle() override;
  BlockerStartupThrottle(const BlockerStartupThrottle&) = delete;
  BlockerStartupThrottle& operator=(const BlockerStartupThrottle&) = delete;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;

 private:
  // RulesMonitorService::InitialLoadObserver:
  void OnInitialRulesetsLoaded(const extensions::ExtensionId& id) override;
  // Resumes the page unless rules are loading again; `timed_out` always.
  void Release(bool timed_out);

  const raw_ref<extensions::declarative_net_request::RulesMonitorService>
      rules_;
  bool deferred_ = false;
  base::TimeTicks deferred_at_;
  base::OneShotTimer timeout_;
  base::WeakPtrFactory<BlockerStartupThrottle> weak_factory_{this};
};

}  // namespace fly

#endif  // FLY_UI_BLOCKER_STARTUP_THROTTLE_H_
