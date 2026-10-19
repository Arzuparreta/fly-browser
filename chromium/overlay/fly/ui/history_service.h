// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_HISTORY_SERVICE_H_
#define FLY_UI_HISTORY_SERVICE_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>
#include "base/functional/callback.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "base/task/cancelable_task_tracker.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "components/history/core/browser/history_service_observer.h"
#include "fly/workspaces/workspace_service.h"
#include "url/gurl.h"

class Profile;
class ProfileManager;
class ScopedProfileKeepAlive;
namespace history {
class QueryResults;
}
namespace fly {
struct HistoryEntry {
  std::string workspace_id;
  GURL url;
  std::u16string title;
  base::Time visited;
};
// App-wide projection of the authorized workspaces' native history databases.
// It owns no second persistent history store. Extensions still query only their
// own profile service. OTR profiles are never loaded, queried or persisted
// here.
class HistoryService : public history::HistoryServiceObserver {
 public:
  using QueryCallback =
      base::OnceCallback<void(std::optional<std::vector<HistoryEntry>>)>;
  HistoryService(ProfileManager& manager, WorkspaceService& workspaces);
  ~HistoryService() override;
  // A new operation cancels the previous reply. Missing/unreadable profiles or
  // a changed workspace catalog fail closed instead of returning partial data.
  void Query(QueryCallback callback);
  // Clear native history in every authorized workspace after loading and
  // checking deletion policy in all of them. Cookies/site storage are
  // untouched.
  void Clear(base::OnceCallback<void(bool)> callback);
  void Cancel();
  void SetChangedCallback(base::RepeatingClosure callback);
  void OnURLVisited(history::HistoryService*,
                    const history::VisitedURLInfo&) override;
  void OnURLsModified(history::HistoryService*,
                      const history::URLRows&) override;
  void OnHistoryDeletions(history::HistoryService*,
                          const history::DeletionInfo&) override;
  void HistoryServiceBeingDeleted(history::HistoryService*) override;

 private:
  struct Source {
    Workspace workspace;
    std::unique_ptr<ScopedProfileKeepAlive> keep_alive;
    base::WeakPtr<Profile> profile;
    Source();
    ~Source();
    Source(Source&&);
    Source& operator=(Source&&);
  };
  void ScheduleChanged();
  void Start(bool clear);
  void Loaded(size_t index, Profile* profile);
  bool Authorized() const;
  void Run();
  void Queried(size_t index, history::QueryResults results);
  void Cleared();
  void Finish(bool success);
  const raw_ref<ProfileManager> manager_;
  const raw_ref<WorkspaceService> workspaces_;
  std::vector<Source> sources_;
  std::vector<HistoryEntry> entries_;
  QueryCallback query_callback_;
  base::OnceCallback<void(bool)> clear_callback_;
  base::RepeatingClosure changed_callback_;
  base::ScopedMultiSourceObservation<history::HistoryService,
                                     history::HistoryServiceObserver>
      observations_{this};
  base::CancelableTaskTracker tracker_;
  base::OneShotTimer deadline_;
  size_t pending_ = 0;
  bool clear_ = false;
  bool change_pending_ = false;
  base::WeakPtrFactory<HistoryService> observer_weak_factory_{this};
  base::WeakPtrFactory<HistoryService> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_HISTORY_SERVICE_H_
