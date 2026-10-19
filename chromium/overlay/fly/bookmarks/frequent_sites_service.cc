// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/bookmarks/frequent_sites_service.h"

#include <algorithm>
#include <cmath>
#include <map>
#include "components/prefs/pref_service.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/preferences/preferences_service.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace fly {
namespace {
std::optional<std::string> WebOrigin(std::string_view value) {
  const GURL url(value);
  if (!url.is_valid() || !url.SchemeIsHTTPOrHTTPS())
    return std::nullopt;
  return url::Origin::Create(url).Serialize();
}
int64_t Day(base::Time time) {
  return std::max(int64_t{0}, time.InMillisecondsSinceUnixEpoch()) / 86400000;
}
}  // namespace
FrequentSitesService::FrequentSitesService(PrefService& local_state)
    : local_state_(local_state) {}
FrequentSitesService::~FrequentSitesService() = default;
std::optional<std::set<std::string>> FrequentSitesService::Hidden() const {
  if (!BookmarkService(*local_state_).Read())
    return std::nullopt;
  std::set<std::string> result;
  const auto* hidden =
      local_state_->GetDict(kBookmarksPref).Find("hidden_sites");
  if (!hidden)
    return result;
  const auto* list = hidden->GetIfList();
  if (!list || list->size() > 10000)
    return std::nullopt;
  for (const auto& value : *list) {
    const auto* text = value.GetIfString();
    if (!text || WebOrigin(*text) != *text || !result.insert(*text).second)
      return std::nullopt;
  }
  return result;
}
bool FrequentSitesService::CanHide() const {
  return Hidden().has_value() && BookmarkService(*local_state_).CanEdit();
}
bool FrequentSitesService::Hide(std::string_view url) {
  const auto origin = WebOrigin(url);
  auto hidden = Hidden();
  if (!origin || !hidden || !CanHide())
    return false;
  if (hidden->contains(*origin))
    return true;
  if (hidden->size() >= 10000)
    return false;
  auto stored = local_state_->GetDict(kBookmarksPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureList("items");
  auto* list = stored.EnsureList("hidden_sites");
  list->Append(*origin);
  local_state_->SetDict(kBookmarksPref, std::move(stored));
  return true;
}
std::optional<std::vector<FrequentSite>> FrequentSitesService::Read(
    const std::vector<SiteVisit>& visits,
    base::Time now) const {
  const auto visible =
      PreferencesService(*local_state_).ReadFrequentSitesVisible();
  if (!visible || visits.size() > 500 || now.is_null() || now.is_max())
    return std::nullopt;
  if (!*visible)
    return std::vector<FrequentSite>();
  const auto hidden = Hidden();
  const auto bookmarks = BookmarkService(*local_state_).WebOrigins();
  if (!hidden || !bookmarks)
    return std::nullopt;
  struct Rank {
    std::string url;
    double score = 0;
    base::Time last_visit;
  };
  std::map<std::string, Rank> ranks;
  for (const auto& visit : visits) {
    const auto origin = WebOrigin(visit.url);
    if (!origin || hidden->contains(*origin) || bookmarks->contains(*origin))
      continue;
    auto& rank = ranks[*origin];
    rank.url = *origin;
    rank.score += std::exp2(-static_cast<double>(std::max(
                                int64_t{0}, Day(now) - Day(visit.timestamp))) /
                            7);
    rank.last_visit = std::max(rank.last_visit, visit.timestamp);
  }
  std::vector<Rank> sorted;
  for (auto& [origin, rank] : ranks)
    sorted.push_back(std::move(rank));
  std::ranges::sort(sorted, [](const Rank& a, const Rank& b) {
    if (a.score != b.score)
      return a.score > b.score;
    if (a.last_visit != b.last_visit)
      return a.last_visit > b.last_visit;
    return a.url < b.url;
  });
  std::vector<FrequentSite> result;
  for (const auto& rank : sorted) {
    if (result.size() == 5)
      break;
    const GURL url(rank.url);
    std::string title(url.host());
    if (url.has_port())
      title += ":" + std::string(url.port());
    result.push_back({rank.url, std::move(title)});
  }
  return result;
}
}  // namespace fly
