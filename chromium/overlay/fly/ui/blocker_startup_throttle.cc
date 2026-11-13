// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/blocker_startup_throttle.h"

#include <memory>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "fly/ui/bundled_extensions.h"

namespace fly {

using extensions::declarative_net_request::RulesMonitorService;

// static
void BlockerStartupThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch("fly-native"))
    return;
  content::NavigationHandle& handle = registry.GetNavigationHandle();
  // Rules apply to network requests only.
  if (!handle.GetURL().SchemeIsHTTPOrHTTPS() || !handle.GetWebContents())
    return;
  Profile* profile =
      Profile::FromBrowserContext(handle.GetWebContents()->GetBrowserContext());
  if (!profile)
    return;
  // Normally done when the profile finished loading; this covers a profile
  // that starts a page before that.
  LoadWantedBundledExtensions(profile->GetOriginalProfile());
  // Shared by a profile and its private (off-the-record) profiles.
  auto* rules = RulesMonitorService::Get(profile);
  if (!rules || !rules->HasPendingInitialLoads())
    return;
  registry.AddThrottle(
      std::make_unique<BlockerStartupThrottle>(registry, *rules));
}

BlockerStartupThrottle::BlockerStartupThrottle(
    content::NavigationThrottleRegistry& registry,
    RulesMonitorService& rules)
    : content::NavigationThrottle(registry), rules_(rules) {}

BlockerStartupThrottle::~BlockerStartupThrottle() {
  if (deferred_)
    rules_->RemoveInitialLoadObserver(this);
}

content::NavigationThrottle::ThrottleCheckResult
BlockerStartupThrottle::WillStartRequest() {
  if (!rules_->HasPendingInitialLoads())
    return PROCEED;
  deferred_ = true;
  deferred_at_ = base::TimeTicks::Now();
  rules_->AddInitialLoadObserver(this);
  timeout_.Start(FROM_HERE, kTimeout,
                 base::BindOnce(&BlockerStartupThrottle::Release,
                                base::Unretained(this), true));
  return DEFER;
}

const char* BlockerStartupThrottle::GetNameForLogging() {
  return "FlyBlockerStartupThrottle";
}

void BlockerStartupThrottle::OnInitialRulesetsLoaded(
    const extensions::ExtensionId& id) {
  // Resume outside the rules service's notification.
  if (!rules_->HasPendingInitialLoads())
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&BlockerStartupThrottle::Release,
                                  weak_factory_.GetWeakPtr(), false));
}

void BlockerStartupThrottle::Release(bool timed_out) {
  if (!deferred_ || (!timed_out && rules_->HasPendingInitialLoads()))
    return;
  deferred_ = false;
  rules_->RemoveInitialLoadObserver(this);
  timeout_.Stop();
  const base::TimeDelta held = base::TimeTicks::Now() - deferred_at_;
  if (timed_out) {
    LOG(WARNING) << "Extension request rules still loading after "
                 << held.InMilliseconds() << " ms; page continues unfiltered";
  }
  VLOG(1) << "Fly held a page " << held.InMilliseconds()
          << " ms for extension request rules";
  // May delete this.
  Resume();
}

}  // namespace fly
