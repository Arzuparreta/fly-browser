// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_BOOKMARKS_FREQUENT_SITES_SERVICE_H_
#define FLY_BOOKMARKS_FREQUENT_SITES_SERVICE_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/raw_ref.h"
#include "base/time/time.h"

class PrefService;
namespace fly {
struct SiteVisit {
  std::string url;
  base::Time timestamp;
};
struct FrequentSite {
  std::string url;
  std::string title;
  bool operator==(const FrequentSite&) const = default;
};
// Derived from the bounded native history projection, with no extra visit log
// or polling timer. Explicit hidden origins live alongside shared bookmarks,
// preserving that collection's unknown metadata and native preference policy.
class FrequentSitesService {
 public:
  explicit FrequentSitesService(PrefService& local_state);
  ~FrequentSitesService();
  std::optional<std::vector<FrequentSite>> Read(
      const std::vector<SiteVisit>& visits,
      base::Time now = base::Time::Now()) const;
  bool CanHide() const;
  bool Hide(std::string_view url);

 private:
  std::optional<std::set<std::string>> Hidden() const;
  const raw_ref<PrefService> local_state_;
};
}  // namespace fly
#endif  // FLY_BOOKMARKS_FREQUENT_SITES_SERVICE_H_
