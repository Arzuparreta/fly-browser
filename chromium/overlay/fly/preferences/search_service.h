// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFERENCES_SEARCH_SERVICE_H_
#define FLY_PREFERENCES_SEARCH_SERVICE_H_

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "url/gurl.h"

class PrefService;
namespace fly {
struct SearchAlias {
  std::string name;
  std::string url;
};
struct SearchPreferences {
  std::string engine;
  std::map<std::string, SearchAlias> aliases;
  std::set<std::string> custom_aliases;
};
// App-wide Fly search choices. Resolving a query never reads workspace storage,
// performs network requests, or writes preferences (including private queries).
class SearchService {
 public:
  explicit SearchService(PrefService& local_state);
  ~SearchService();
  std::optional<SearchPreferences> Read() const;
  bool CanEdit() const;
  bool SetEngine(std::string_view url_template);
  bool SetAlias(std::string_view key,
                std::string_view name,
                std::string_view url_template);
  bool RemoveAlias(std::string_view key);
  // Workspace switching and calculator evaluation belong to the address UI.
  // Invalid explicit URLs/protocols and unreadable search settings return null.
  std::optional<GURL> ResolveNavigation(std::string_view input) const;

 private:
  const raw_ref<PrefService> local_state_;
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace fly
#endif
