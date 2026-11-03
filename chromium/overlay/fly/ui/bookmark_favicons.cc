// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/bookmark_favicons.h"

#include <algorithm>
#include <set>

#include "base/functional/bind.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "components/favicon/core/favicon_service.h"
#include "components/history/core/browser/history_service.h"
#include "fly/bookmarks/bookmark_service.h"
#include "ui/gfx/image/image.h"

namespace fly {
namespace {
const char kCacheKey = 0;
}

BookmarkFavicons* BookmarkFavicons::ForProfile(Profile* profile) {
  // Never let the favicon factory redirect an incognito request to its parent.
  if (!profile || profile->IsOffTheRecord())
    return nullptr;
  auto* cache =
      static_cast<BookmarkFavicons*>(profile->GetUserData(&kCacheKey));
  if (!cache) {
    auto owned = std::make_unique<BookmarkFavicons>(profile);
    cache = owned.get();
    profile->SetUserData(&kCacheKey, std::move(owned));
  }
  return cache;
}

void BookmarkFavicons::Prune(Profile* profile,
                             const BookmarkService& bookmarks) {
  auto* cache =
      static_cast<BookmarkFavicons*>(profile->GetUserData(&kCacheKey));
  if (!cache)
    return;
  std::set<std::string> urls;
  if (const auto items = bookmarks.Read()) {
    for (const auto& item : *items)
      urls.insert(item.url);
  }
  ++cache->generation_;  // Pending reads must not repopulate removed entries.
  for (auto it = cache->cache_.begin(); it != cache->cache_.end();) {
    if (!urls.contains(it->first.first.spec()))
      it = cache->cache_.Erase(it);
    else
      ++it;
  }
}

BookmarkFavicons::BookmarkFavicons(Profile* profile) : profile_(profile) {
  if (auto* service = HistoryServiceFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS))
    history_.Observe(service);
}
BookmarkFavicons::~BookmarkFavicons() = default;

void BookmarkFavicons::Get(const GURL& url,
                           int pixels,
                           Callback callback,
                           base::CancelableTaskTracker* tracker) {
  if (!url.SchemeIsHTTPOrHTTPS()) {
    std::move(callback).Run({});
    return;
  }
  Key key{url, std::clamp(pixels, 16, 64)};
  auto found = cache_.Get(key);
  if (found != cache_.end() && found->second.expires > base::TimeTicks::Now()) {
    std::move(callback).Run(found->second.image);
    return;
  }
  auto* service = FaviconServiceFactory::GetForProfile(
      profile_, ServiceAccessType::EXPLICIT_ACCESS);
  if (!service) {
    std::move(callback).Run({});
    return;
  }
  ++queries_;
  // This API reads Chromium's local history backend only. It never downloads
  // icons, visits pages, or writes bookmark data.
  service->GetRawFaviconForPageURL(
      url, {favicon_base::IconType::kFavicon}, key.second,
      /*fallback_to_host=*/true,
      base::BindOnce(&BookmarkFavicons::Loaded, weak_factory_.GetWeakPtr(), key,
                     generation_, std::move(callback)),
      tracker);
}

void BookmarkFavicons::Loaded(
    Key key,
    size_t generation,
    Callback callback,
    const favicon_base::FaviconRawBitmapResult& result) {
  gfx::ImageSkia image;
  if (generation == generation_ && result.is_valid())
    image = gfx::Image::CreateFrom1xPNGBytes(result.bitmap_data).AsImageSkia();
  if (generation == generation_) {
    // Misses expire quickly so visiting an imported bookmark can fill its
    // placeholder. Positive entries also refresh rather than going stale
    // forever.
    const auto lifetime = image.isNull() ? base::Seconds(30) : base::Minutes(5);
    cache_.Put(key, Entry{image, base::TimeTicks::Now() + lifetime});
  }
  std::move(callback).Run(std::move(image));
}

void BookmarkFavicons::OnHistoryDeletions(history::HistoryService*,
                                          const history::DeletionInfo&) {
  ++generation_;
  cache_.Clear();
}
void BookmarkFavicons::OnURLVisited(history::HistoryService*,
                                    const history::VisitedURLInfo& visit) {
  // Host fallback means another page on the host can supply an icon too.
  // Other hosts' in-flight reads remain valid during background navigation.
  for (auto it = cache_.begin(); it != cache_.end();) {
    if (it->first.first.host() == visit.url_row.url().host())
      it = cache_.Erase(it);
    else
      ++it;
  }
}
void BookmarkFavicons::HistoryServiceBeingDeleted(history::HistoryService*) {
  history_.Reset();
  weak_factory_.InvalidateWeakPtrs();
  cache_.Clear();
}
}  // namespace fly
