// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/bookmarks/bookmark_service.h"

#include "base/strings/string_util.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"
#include "fly/prefs/dictionary_store.h"
#include "url/gurl.h"
#include "url/origin.h"
#include <algorithm>
#include <cmath>

namespace fly {
namespace {
bool ValidURL(std::string_view value) {
  if (value.size() > 2 * 1024 * 1024 || value.find('\0') != std::string_view::npos ||
      !base::IsStringUTF8(value))
    return false;
  const GURL url(value);
  return url.is_valid() && value != "about:blank" &&
         (url.SchemeIsHTTPOrHTTPS() || url.SchemeIs("file") ||
          url.SchemeIs("chrome") || url.SchemeIs("chrome-extension"));
}
bool ValidTitle(std::string_view title) {
  return title.size() <= 65536 && title.find('\0') == std::string_view::npos &&
         base::IsStringUTF8(title);
}
}  // namespace
void BookmarkService::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kBookmarksPref);
}
BookmarkService::BookmarkService(PrefService& local_state) : local_state_(local_state) {}
BookmarkService::~BookmarkService() = default;
std::optional<base::DictValue> BookmarkService::ReadValidated(bool for_write) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto* readable = ReadDictionaryStore(*local_state_, kBookmarksPref);
  if (!readable || (for_write &&
      !local_state_->FindPreference(kBookmarksPref)->IsUserModifiable()))
    return std::nullopt;
  auto stored = readable->Clone();
  if (stored.empty()) {
    stored.Set("schema", 1);
    stored.Set("items", base::ListValue());
    return stored;
  }
  const auto* items = stored.FindList("items");
  if (stored.FindInt("schema") != 1 || !items)
    return std::nullopt;
  std::set<std::string> urls;
  for (const auto& item : *items) {
    const auto* row = item.GetIfDict();
    if (!row)
      return std::nullopt;
    const auto* url = row->FindString("url");
    const auto* title = row->FindString("title");
    const auto timestamp = row->FindDouble("timestamp");
    if (!url || !ValidURL(*url) || !title || !ValidTitle(*title) ||
        !timestamp || !std::isfinite(*timestamp) || *timestamp < 0 ||
        !urls.insert(*url).second)
      return std::nullopt;
  }
  return stored;
}
std::optional<std::vector<Bookmark>> BookmarkService::Read() const {
  const auto stored = ReadValidated();
  if (!stored)
    return std::nullopt;
  std::vector<Bookmark> result;
  for (const auto& item : *stored->FindList("items")) {
    const auto& row = item.GetDict();
    result.push_back({*row.FindString("url"), *row.FindString("title"),
                      *row.FindDouble("timestamp")});
  }
  return result;
}
bool BookmarkService::CanEdit() const {
  return ReadValidated(true).has_value();
}
bool BookmarkService::Contains(std::string_view url) const {
  const auto bookmarks = Read();
  return bookmarks && std::ranges::any_of(*bookmarks, [&](const Bookmark& item) {
    return item.url == url;
  });
}
bool BookmarkService::CanAdd(std::string_view url) const {
  return ValidURL(url) && CanEdit();
}
bool BookmarkService::Add(std::string_view url, std::string_view title,
                           base::Time added) {
  auto stored = ReadValidated(true);
  if (!stored || !ValidURL(url) || !ValidTitle(title) || added.is_null() ||
      added < base::Time::UnixEpoch() || added.is_max())
    return false;
  auto* items = stored->FindList("items");
  for (const auto& item : *items)
    if (*item.GetDict().FindString("url") == url)
      return true;
  items->Insert(items->begin(), base::Value(base::DictValue()
      .Set("url", std::string(url))
      .Set("title", title.empty() ? std::string(url) : std::string(title))
      .Set("timestamp", static_cast<double>(added.InMillisecondsSinceUnixEpoch()))));
  local_state_->SetDict(kBookmarksPref, std::move(*stored));
  return true;
}
bool BookmarkService::Remove(std::string_view url) {
  return RemoveWithUndo(url).has_value();
}
bool BookmarkService::Update(std::string_view original_url,
                             std::string_view url, std::string_view title,
                             std::optional<size_t> position) {
  auto stored = ReadValidated(true);
  if (!stored || !ValidURL(url) || !ValidTitle(title))
    return false;
  auto *items = stored->FindList("items");
  if (position && *position >= items->size())
    return false;
  if (url != original_url && std::ranges::any_of(*items, [&](const auto &item) {
        return *item.GetDict().FindString("url") == url;
      }))
    return false;
  for (size_t i = 0; i < items->size(); ++i) {
    auto& item = (*items)[i];
    if (*item.GetDict().FindString("url") != original_url)
      continue;
    item.GetDict().Set("url", std::string(url));
    item.GetDict().Set("title",
                       title.empty() ? std::string(url) : std::string(title));
    if (position && *position != i) {
      auto moved = std::move(item);
      items->erase(items->begin() + i);
      items->Insert(items->begin() + *position, std::move(moved));
    }
    local_state_->SetDict(kBookmarksPref, std::move(*stored));
    return true;
  }
  return false;
}
bool BookmarkService::MoveTo(std::string_view url, size_t index) {
  auto stored = ReadValidated(true);
  if (!stored)
    return false;
  auto* items = stored->FindList("items");
  if (index >= items->size())
    return false;
  for (size_t i = 0; i < items->size(); ++i) {
    if (*(*items)[i].GetDict().FindString("url") != url)
      continue;
    auto item = std::move((*items)[i]);
    items->erase(items->begin() + i);
    items->Insert(items->begin() + index, std::move(item));
    local_state_->SetDict(kBookmarksPref, std::move(*stored));
    return true;
  }
  return false;
}
std::optional<RemovedBookmark>
BookmarkService::RemoveWithUndo(std::string_view url) {
  auto stored = ReadValidated(true);
  if (!stored)
    return std::nullopt;
  auto *items = stored->FindList("items");
  for (size_t i = 0; i < items->size(); ++i) {
    if (*(*items)[i].GetDict().FindString("url") != url)
      continue;
    RemovedBookmark removed{(*items)[i].GetDict().Clone(), i};
    items->erase(items->begin() + i);
    local_state_->SetDict(kBookmarksPref, std::move(*stored));
    return removed;
  }
  return std::nullopt;
}
bool BookmarkService::Restore(const RemovedBookmark &removed) {
  auto stored = ReadValidated(true);
  const auto *url = removed.item.FindString("url");
  const auto *title = removed.item.FindString("title");
  const auto timestamp = removed.item.FindDouble("timestamp");
  if (!stored || !url || !ValidURL(*url) || !title || !ValidTitle(*title) ||
      !timestamp || !std::isfinite(*timestamp) || *timestamp < 0 ||
      Contains(*url))
    return false;
  auto *items = stored->FindList("items");
  items->Insert(items->begin() + std::min(removed.index, items->size()),
                base::Value(removed.item.Clone()));
  local_state_->SetDict(kBookmarksPref, std::move(*stored));
  return true;
}
std::optional<std::set<std::string>> BookmarkService::WebOrigins() const {
  const auto bookmarks = Read();
  if (!bookmarks)
    return std::nullopt;
  std::set<std::string> origins;
  for (const auto& bookmark : *bookmarks) {
    const GURL url(bookmark.url);
    if (url.SchemeIsHTTPOrHTTPS())
      origins.insert(url::Origin::Create(url).Serialize());
  }
  return origins;
}
}  // namespace fly
