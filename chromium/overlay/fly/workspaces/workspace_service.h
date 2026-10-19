// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_WORKSPACES_WORKSPACE_SERVICE_H_
#define FLY_WORKSPACES_WORKSPACE_SERVICE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "base/values.h"

class PrefRegistrySimple;
class PrefService;

namespace fly {

inline constexpr char kWorkspacesPref[] = "fly.workspaces";

struct Workspace {
  std::string id;
  std::string name;
  // A direct child of ProfileManager::user_data_dir(), allocated by Chromium.
  // Display names must never determine a profile's storage path.
  std::string profile_directory;
  bool operator==(const Workspace&) const = default;
};

struct WorkspaceCatalog {
  std::vector<Workspace> items;
  std::string active_id;
};

// A pending UI close must not race an extension opening another browser for
// that profile. This lease is process-local: a crash cancels an uncommitted
// retirement instead of persisting an incomplete deletion.
class ScopedWorkspaceRetirement {
 public:
  explicit ScopedWorkspaceRetirement(base::FilePath profile_path);
  ~ScopedWorkspaceRetirement();
  ScopedWorkspaceRetirement(const ScopedWorkspaceRetirement&) = delete;
  ScopedWorkspaceRetirement& operator=(const ScopedWorkspaceRetirement&) =
      delete;

 private:
  const base::FilePath profile_path_;
};
bool IsWorkspaceRetiring(const base::FilePath& original_profile_path);

// Application-wide metadata only. ProfileManager owns Profiles and all site
// state; this service never reads cookies or touches profile directories.
// Private workspaces live in the window controller and must never enter this
// catalog. Mutations preserve unknown fields and reject malformed/future data
// without overwriting it. PrefService owns atomic persistence of the whole
// dictionary.
class WorkspaceService {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  explicit WorkspaceService(PrefService& local_state);
  ~WorkspaceService();
  WorkspaceService(const WorkspaceService&) = delete;
  WorkspaceService& operator=(const WorkspaceService&) = delete;

  // nullopt means recovery is required, not an empty catalog.
  std::optional<WorkspaceCatalog> Read() const;
  bool CanEdit() const;
  // nullopt is unreadable/future metadata, never permission to rewrite it.
  std::optional<bool> IsProfileRetired(
      std::string_view profile_directory) const;
  std::optional<Workspace> Add(std::string_view name,
                               std::string_view profile_directory);
  bool Rename(std::string_view id, std::string_view name);
  bool Activate(std::string_view id);
  // Call only after Chromium has successfully closed this workspace's browsers
  // (including beforeunload). Never deletes files. Retired directories cannot
  // be rebound, so recreating a name cannot resurrect the old login state.
  bool Remove(std::string_view id);
  bool Reorder(const std::vector<std::string>& ids);

 private:
  std::optional<base::DictValue> ReadValidated(bool for_write = false) const;
  void Commit(base::DictValue catalog);

  const raw_ref<PrefService> local_state_;
  SEQUENCE_CHECKER(sequence_checker_);
};

}  // namespace fly
#endif  // FLY_WORKSPACES_WORKSPACE_SERVICE_H_
