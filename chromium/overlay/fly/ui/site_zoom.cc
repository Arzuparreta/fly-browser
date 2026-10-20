// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/site_zoom.h"

#include <algorithm>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/page/page_zoom.h"

namespace fly {

SiteZoom::SiteZoom() = default;
SiteZoom::~SiteZoom() = default;

void SiteZoom::AddProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() ||
      subscriptions_.contains(profile))
    return;
  auto* map = content::HostZoomMap::GetDefaultForBrowserContext(profile);
  // The first workspace seeds the shared levels; a later one takes them and
  // shares the hosts only it had.
  for (const auto& entry : map->GetAllZoomLevels()) {
    if (entry.mode == content::HostZoomMap::ZOOM_CHANGED_FOR_HOST &&
        !levels_.contains(entry.host))
      levels_[entry.host] = entry.zoom_level;
  }
  subscriptions_[profile] = map->AddZoomLevelChangedCallback(
      base::BindRepeating(&SiteZoom::OnZoomChanged, base::Unretained(this),
                          profile));
  observations_.AddObservation(profile);
  for (const auto& [host, level] : levels_)
    for (const auto& [target, subscription] : subscriptions_)
      Apply(target, host, level);
}

void SiteZoom::OnProfileWillBeDestroyed(Profile* profile) {
  subscriptions_.erase(profile);
  observations_.RemoveObservation(profile);
}

void SiteZoom::OnZoomChanged(
    Profile* source,
    const content::HostZoomMap::ZoomLevelChange& change) {
  if (applying_ || change.mode != content::HostZoomMap::ZOOM_CHANGED_FOR_HOST ||
      change.host.empty())
    return;
  if (blink::ZoomValuesEqual(change.zoom_level, 0))
    levels_.erase(change.host);
  else
    levels_[change.host] = change.zoom_level;
  for (const auto& [target, subscription] : subscriptions_)
    if (target != source)
      Apply(target, change.host, change.zoom_level);
}

void SiteZoom::Apply(Profile* target, const std::string& host, double level) {
  auto* map = content::HostZoomMap::GetDefaultForBrowserContext(target);
  if (blink::ZoomValuesEqual(map->GetZoomLevelForHostAndScheme("https", host),
                             level))
    return;
  applying_ = true;
  map->SetZoomLevelForHost(host, level);
  applying_ = false;
}

// static
void SiteZoom::Change(content::WebContents* contents, std::string_view action) {
  auto* zoom = contents ? zoom::ZoomController::FromWebContents(contents)
                        : nullptr;
  if (!zoom)
    return;
  const double current = zoom->GetZoomLevel();
  const double level =
      action == "reset-zoom"
          ? 0
          : std::clamp(current + (action == "zoom-in" ? 0.5 : -0.5), -5.0,
                       5.0);
  zoom->SetZoomLevel(level);
}

}  // namespace fly
