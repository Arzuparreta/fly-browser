// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_NEW_TAB_UI_H_
#define FLY_UI_NEW_TAB_UI_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include "base/callback_list.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/ui/history_service.h"
#include "url/gurl.h"

namespace content {
class BrowserContext;
}

namespace fly {
// One demand-driven history snapshot for all regular new tabs. Private pages
// never subscribe or receive it. Invalidation clears stale/deleted entries even
// while no page is visible; opening another tab reuses a valid snapshot.
class NewTabHistory {
 public:
  NewTabHistory(ProfileManager& manager,
                WorkspaceService& workspaces,
                PrefService& prefs);
  ~NewTabHistory();
  base::CallbackListSubscription Subscribe(base::RepeatingClosure changed);
  const std::vector<HistoryEntry>& entries() const { return entries_; }
  size_t query_count_for_testing() const { return query_count_; }

 private:
  void OnWorkspacesChanged();
  void Invalidate();
  void EnsureQuery();
  void OnQuery(std::optional<std::vector<HistoryEntry>> rows);
  const raw_ref<WorkspaceService> workspaces_;
  std::optional<std::vector<std::pair<std::string, std::string>>> catalog_;
  HistoryService history_;
  PrefChangeRegistrar registrar_;
  base::RepeatingClosureList changed_;
  std::vector<HistoryEntry> entries_;
  bool valid_ = false;
  bool querying_ = false;
  size_t query_count_ = 0;
  base::WeakPtrFactory<NewTabHistory> weak_factory_{this};
};

// Fly's quiet new tab page: artwork and entry points to native navigation.
// Served from
// <out>/fly_resources/new_tab.
inline constexpr char kFlyNewTabHost[] = "fly-new-tab";
inline constexpr char kFlyNewTabURL[] = "chrome://fly-new-tab/";

// New tabs and blank pages are both "new tab" in Fly's interface.
bool IsFlyNewTabPage(const GURL& url);
// BrowserURLHandler rewrite: chrome://newtab (Chromium's new tab page, also
// the start page) becomes Fly's page in every profile.
bool RewriteNewTabURL(GURL* url, content::BrowserContext* browser_context);
// Adds the page to Chromium's WebUI registry; safe to call more than once.
void RegisterFlyNewTabPage();
}  // namespace fly

#endif  // FLY_UI_NEW_TAB_UI_H_
