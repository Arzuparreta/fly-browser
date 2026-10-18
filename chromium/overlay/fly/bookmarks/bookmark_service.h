// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_BOOKMARKS_BOOKMARK_SERVICE_H_
#define FLY_BOOKMARKS_BOOKMARK_SERVICE_H_

#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/values.h"

class PrefService;
class PrefRegistrySimple;
namespace fly {
inline constexpr char kBookmarksPref[] = "fly.bookmarks";
struct Bookmark {
  std::string url;
  std::string title;
  double timestamp_ms = 0;
  bool operator==(const Bookmark&) const = default;
};
// Fly's explicit app-wide flat collection. Profile-native extension bookmarks
// retain their own BookmarkModel and permission scope; neither store mirrors the
// other. No site state or automatic private browsing history enters this service.
class BookmarkService {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  explicit BookmarkService(PrefService& local_state);
  ~BookmarkService();
  std::optional<std::vector<Bookmark>> Read() const;
  bool CanEdit() const;
  bool Contains(std::string_view url) const;
  bool CanAdd(std::string_view url) const;
  bool Add(std::string_view url, std::string_view title,
           base::Time added = base::Time::Now());
  bool Remove(std::string_view url);
  // Exact web origins, including scheme and port, for frequent-site exclusion.
  // A malformed store yields nullopt, never a silently empty exclusion set.
  std::optional<std::set<std::string>> WebOrigins() const;

 private:
  std::optional<base::DictValue> ReadValidated(bool for_write = false) const;
  const raw_ref<PrefService> local_state_;
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace fly
#endif  // FLY_BOOKMARKS_BOOKMARK_SERVICE_H_
