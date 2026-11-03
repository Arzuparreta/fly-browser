// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_BOOKMARK_FAVICONS_H_
#define FLY_UI_BOOKMARK_FAVICONS_H_

#include "base/containers/lru_cache.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/supports_user_data.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "components/history/core/browser/history_service_observer.h"
#include "ui/gfx/image/image_skia.h"
#include "url/gurl.h"

class Profile;
namespace favicon_base {
struct FaviconRawBitmapResult;
}
namespace fly {
class BookmarkService;

// Created only when a bookmark row needs an icon. Profile ownership shares the
// bounded RAM cache between windows without crossing workspace/private borders.
class BookmarkFavicons : public base::SupportsUserData::Data,
                         public history::HistoryServiceObserver {
 public:
  static BookmarkFavicons* ForProfile(Profile* profile);
  static void Prune(Profile* profile, const BookmarkService& bookmarks);
  explicit BookmarkFavicons(Profile* profile);
  ~BookmarkFavicons() override;
  using Callback = base::OnceCallback<void(gfx::ImageSkia)>;
  void Get(const GURL& url,
           int pixels,
           Callback callback,
           base::CancelableTaskTracker* tracker);
  size_t queries_for_testing() const { return queries_; }
  size_t size_for_testing() const { return cache_.size(); }

 private:
  using Key = std::pair<GURL, int>;
  struct Entry {
    gfx::ImageSkia image;
    base::TimeTicks expires;
  };
  void Loaded(Key key,
              size_t generation,
              Callback callback,
              const favicon_base::FaviconRawBitmapResult& result);
  void OnHistoryDeletions(history::HistoryService*,
                          const history::DeletionInfo&) override;
  void OnURLVisited(history::HistoryService*,
                    const history::VisitedURLInfo&) override;
  void HistoryServiceBeingDeleted(history::HistoryService*) override;
  raw_ptr<Profile> profile_;
  // At most 256 decoded bitmaps, each requested at no more than 64px.
  base::LRUCache<Key, Entry> cache_{256};
  size_t generation_ = 0;
  size_t queries_ = 0;
  base::ScopedObservation<history::HistoryService,
                          history::HistoryServiceObserver>
      history_{this};
  base::WeakPtrFactory<BookmarkFavicons> weak_factory_{this};
};
}  // namespace fly
#endif
