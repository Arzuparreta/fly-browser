// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_SITE_ZOOM_H_
#define FLY_UI_SITE_ZOOM_H_

#include <map>
#include <set>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "content/public/browser/host_zoom_map.h"

class Profile;
namespace content {
class WebContents;
}

namespace fly {
// Site zoom: one zoom level per hostname, shared by every normal workspace
// (private workspaces keep their own, unsaved), changed in steps of 0.5 levels
// between -5 and 5. Chromium stores each profile's host levels; this keeps the
// workspace profiles equal.
class SiteZoom : public ProfileObserver {
 public:
  SiteZoom();
  ~SiteZoom() override;
  SiteZoom(const SiteZoom&) = delete;
  SiteZoom& operator=(const SiteZoom&) = delete;

  void AddProfile(Profile* profile);
  // zoom-in, zoom-out or reset-zoom on the page.
  static void Change(content::WebContents* contents, std::string_view action);

  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  void OnZoomChanged(Profile* source,
                     const content::HostZoomMap::ZoomLevelChange& change);
  void Apply(Profile* target, const std::string& host, double level);

  std::map<std::string, double> levels_;
  std::map<raw_ptr<Profile>, base::CallbackListSubscription> subscriptions_;
  bool applying_ = false;
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> observations_{
      this};
};
}  // namespace fly

#endif  // FLY_UI_SITE_ZOOM_H_
