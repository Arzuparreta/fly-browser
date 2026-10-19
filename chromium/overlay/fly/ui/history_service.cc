// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/history_service.h"

#include <algorithm>
#include "base/functional/bind.h"
#include "base/task/single_thread_task_runner.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/browser/history_types.h"
#include "components/history/core/common/pref_names.h"
#include "components/prefs/pref_service.h"

namespace fly {
HistoryService::Source::Source() = default;
HistoryService::Source::~Source() = default;
HistoryService::Source::Source(Source&&) = default;
HistoryService::Source& HistoryService::Source::operator=(Source&&) = default;
HistoryService::HistoryService(ProfileManager& manager,
                               WorkspaceService& workspaces)
    : manager_(manager), workspaces_(workspaces) {}
HistoryService::~HistoryService() {
  Cancel();
}
void HistoryService::SetChangedCallback(base::RepeatingClosure callback) {
  changed_callback_ = std::move(callback);
}
void HistoryService::OnURLVisited(history::HistoryService*,
                                  const history::VisitedURLInfo&) {
  ScheduleChanged();
}
void HistoryService::OnURLsModified(history::HistoryService*,
                                    const history::URLRows&) {
  ScheduleChanged();
}
void HistoryService::OnHistoryDeletions(history::HistoryService*,
                                        const history::DeletionInfo&) {
  ScheduleChanged();
}
void HistoryService::HistoryServiceBeingDeleted(
    history::HistoryService* service) {
  observations_.RemoveObservation(service);
  ScheduleChanged();
}
void HistoryService::ScheduleChanged() {
  if (!changed_callback_ || change_pending_)
    return;
  change_pending_ = true;
  // Native deletion and profile teardown notify from their own dispatch stack.
  // Coalesce changes and let that stack finish before loading/querying again.
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostNonNestableTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<HistoryService> service) {
                       if (!service)
                         return;
                       service->change_pending_ = false;
                       if (service->changed_callback_)
                         service->changed_callback_.Run();
                     },
                     observer_weak_factory_.GetWeakPtr()));
}
void HistoryService::Cancel() {
  weak_factory_.InvalidateWeakPtrs();
  tracker_.TryCancelAll();
  deadline_.Stop();
  query_callback_.Reset();
  clear_callback_.Reset();
  sources_.clear();
  entries_.clear();
  pending_ = 0;
}
void HistoryService::Query(QueryCallback callback) {
  Cancel();
  query_callback_ = std::move(callback);
  Start(false);
}
void HistoryService::Clear(base::OnceCallback<void(bool)> callback) {
  Cancel();
  clear_callback_ = std::move(callback);
  Start(true);
}
void HistoryService::Start(bool clear) {
  clear_ = clear;
  const auto catalog = workspaces_->Read();
  if (!catalog) {
    Finish(false);
    return;
  }
  for (const auto& workspace : catalog->items) {
    Source source;
    source.workspace = workspace;
    sources_.push_back(std::move(source));
  }
  pending_ = sources_.size();
  if (!pending_) {
    Finish(true);
    return;
  }
  deadline_.Start(FROM_HERE, base::Seconds(30),
                  base::BindOnce(&HistoryService::Finish,
                                 weak_factory_.GetWeakPtr(), false));
  // Loading can call back synchronously. A weak token also protects against an
  // immediate failed load completing the operation and releasing all sources.
  const auto token = weak_factory_.GetWeakPtr();
  const size_t count = sources_.size();
  for (size_t i = 0; i < count && token; ++i) {
    const auto path = manager_->user_data_dir().AppendASCII(
        sources_[i].workspace.profile_directory);
    if (IsWorkspaceRetiring(path)) {
      Finish(false);
      return;
    }
    if (auto* profile = manager_->GetProfileByPath(path)) {
      Loaded(i, profile);
    } else {
      auto callback = base::BindOnce(&HistoryService::Loaded, token, i);
      if (!manager_->LoadProfileByPath(path, false, std::move(callback)) &&
          token) {
        Finish(false);
        return;
      }
    }
  }
}
bool HistoryService::Authorized() const {
  const auto catalog = workspaces_->Read();
  if (!catalog || catalog->items.size() != sources_.size())
    return false;
  for (const auto& source : sources_) {
    const auto item =
        std::ranges::find(catalog->items, source.workspace.id, &Workspace::id);
    if (item == catalog->items.end() ||
        item->profile_directory != source.workspace.profile_directory ||
        IsWorkspaceRetiring(manager_->user_data_dir().AppendASCII(
            source.workspace.profile_directory)))
      return false;
  }
  return true;
}
void HistoryService::Loaded(size_t index, Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || !profile->IsRegularProfile() ||
      !Authorized() ||
      profile->GetPath() != manager_->user_data_dir().AppendASCII(
                                sources_[index].workspace.profile_directory) ||
      workspaces_->IsProfileRetired(
          sources_[index].workspace.profile_directory) != false ||
      !HistoryServiceFactory::GetForProfile(
          profile, ServiceAccessType::EXPLICIT_ACCESS) ||
      (clear_ &&
       !profile->GetPrefs()->GetBoolean(prefs::kAllowDeletingBrowserHistory))) {
    Finish(false);
    return;
  }
  auto& source = sources_[index];
  source.profile = profile->GetWeakPtr();
  source.keep_alive = ScopedProfileKeepAlive::TryAcquire(
      profile, ProfileKeepAliveOrigin::kChromeViewsDelegate);
  if (!source.keep_alive) {
    Finish(false);
    return;
  }
  if (!--pending_)
    Run();
}
void HistoryService::Run() {
  if (!Authorized()) {
    Finish(false);
    return;
  }
  // Earlier loads can finish long before the final profile. Check all current
  // lifetimes and policies again before dispatching the first deletion.
  for (const auto& source : sources_) {
    if (!source.profile ||
        (clear_ && !source.profile->GetPrefs()->GetBoolean(
                       prefs::kAllowDeletingBrowserHistory))) {
      Finish(false);
      return;
    }
  }
  pending_ = sources_.size();
  for (size_t i = 0; i < sources_.size(); ++i) {
    auto* history = HistoryServiceFactory::GetForProfile(
        sources_[i].profile.get(), ServiceAccessType::EXPLICIT_ACCESS);
    if (!clear_ && !observations_.IsObservingSource(history))
      observations_.AddObservation(history);
    if (clear_) {
      history->ExpireHistoryBetween(
          {}, std::nullopt, base::Time(), base::Time(), true,
          base::BindOnce(&HistoryService::Cleared, weak_factory_.GetWeakPtr()),
          &tracker_);
    } else {
      history::QueryOptions options;
      options.max_count = 500;
      options.duplicate_policy = history::QueryOptions::KEEP_ALL_DUPLICATES;
      options.policy_for_404_visits =
          history::VisitQuery404sPolicy::kInclude404s;
      history->QueryHistory({}, options,
                            base::BindOnce(&HistoryService::Queried,
                                           weak_factory_.GetWeakPtr(), i),
                            &tracker_);
    }
  }
}
void HistoryService::Queried(size_t index, history::QueryResults results) {
  if (!Authorized()) {
    Finish(false);
    return;
  }
  for (const auto& row : results) {
    if (row.url().SchemeIsHTTPOrHTTPS())
      entries_.push_back({sources_[index].workspace.id, row.url(), row.title(),
                          row.visit_time()});
  }
  if (!--pending_) {
    std::ranges::stable_sort(entries_, std::greater<>(),
                             &HistoryEntry::visited);
    if (entries_.size() > 500)
      entries_.resize(500);
    Finish(true);
  }
}
void HistoryService::Cleared() {
  if (!--pending_)
    Finish(Authorized());
}
void HistoryService::Finish(bool success) {
  auto query = std::move(query_callback_);
  auto clear = std::move(clear_callback_);
  auto entries = std::move(entries_);
  Cancel();
  if (query)
    std::move(query).Run(success ? std::make_optional(std::move(entries))
                                 : std::nullopt);
  if (clear)
    std::move(clear).Run(success);
}
}  // namespace fly
