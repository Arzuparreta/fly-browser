// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/workspaces/workspace_service.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/strings/string_util.h"
#include "base/uuid.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace fly {
namespace {
constexpr int kSchema = 1;

bool ValidName(std::string_view name) {
  return !name.empty() && name.size() <= 4096 &&
         name.find('\0') == std::string_view::npos && base::IsStringUTF8(name);
}

bool ValidProfileDirectory(std::string_view directory) {
  // Chromium-generated Default/Profile N names fit this portable allowlist.
  // Do not accept paths, drive names, dot components or platform separators.
  return !directory.empty() && directory.size() <= 128 &&
         base::IsAsciiAlphaNumeric(directory.front()) &&
         directory.back() != ' ' && std::ranges::all_of(directory, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == ' ' || c == '_' ||
                  c == '-';
         });
}

base::DictValue* FindRow(base::DictValue& catalog, std::string_view id) {
  for (auto& value : *catalog.FindList("items")) {
    if (*value.GetDict().FindString("id") == id) {
      return &value.GetDict();
    }
  }
  return nullptr;
}

bool NameUsed(const base::DictValue& catalog,
              std::string_view name,
              std::string_view except_id = {}) {
  for (const auto& value : *catalog.FindList("items")) {
    const auto& row = value.GetDict();
    if (*row.FindString("name") == name && *row.FindString("id") != except_id) {
      return true;
    }
  }
  return false;
}
}  // namespace

void WorkspaceService::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kWorkspacesPref);
}

WorkspaceService::WorkspaceService(PrefService& local_state)
    : local_state_(local_state) {}
WorkspaceService::~WorkspaceService() = default;

std::optional<base::DictValue> WorkspaceService::ReadValidated() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto catalog = local_state_->GetDict(kWorkspacesPref).Clone();
  if (catalog.empty()) {
    catalog.Set("schema", kSchema);
    catalog.Set("items", base::ListValue());
    catalog.Set("active_id", "");
    catalog.Set("retired_profiles", base::ListValue());
    return catalog;
  }
  const auto* items = catalog.FindList("items");
  const auto* retired = catalog.FindList("retired_profiles");
  const auto* active = catalog.FindString("active_id");
  if (catalog.FindInt("schema") != kSchema || !items || !retired || !active) {
    return std::nullopt;
  }
  std::set<std::string> ids;
  std::set<std::string> names;
  std::set<std::string> directories;
  for (const auto& value : *items) {
    const auto* row = value.GetIfDict();
    if (!row) {
      return std::nullopt;
    }
    const auto* id = row->FindString("id");
    const auto* name = row->FindString("name");
    const auto* directory = row->FindString("profile_directory");
    if (!id || !base::Uuid::ParseLowercase(*id).is_valid() || !name ||
        !ValidName(*name) || !directory || !ValidProfileDirectory(*directory) ||
        !ids.insert(*id).second || !names.insert(*name).second ||
        !directories.insert(base::ToLowerASCII(*directory)).second) {
      return std::nullopt;
    }
  }
  for (const auto& value : *retired) {
    const auto* directory = value.GetIfString();
    if (!directory || !ValidProfileDirectory(*directory) ||
        !directories.insert(base::ToLowerASCII(*directory)).second) {
      return std::nullopt;
    }
  }
  if ((items->empty() && !active->empty()) ||
      (!items->empty() && !ids.contains(*active))) {
    return std::nullopt;
  }
  return catalog;
}

std::optional<WorkspaceCatalog> WorkspaceService::Read() const {
  auto catalog = ReadValidated();
  if (!catalog) {
    return std::nullopt;
  }
  WorkspaceCatalog result;
  result.active_id = *catalog->FindString("active_id");
  for (const auto& value : *catalog->FindList("items")) {
    const auto& row = value.GetDict();
    result.items.push_back({*row.FindString("id"), *row.FindString("name"),
                            *row.FindString("profile_directory")});
  }
  return result;
}

void WorkspaceService::Commit(base::DictValue catalog) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  local_state_->SetDict(kWorkspacesPref, std::move(catalog));
}

std::optional<Workspace> WorkspaceService::Add(
    std::string_view name,
    std::string_view profile_directory) {
  auto catalog = ReadValidated();
  if (!catalog || !ValidName(name) ||
      !ValidProfileDirectory(profile_directory) || NameUsed(*catalog, name)) {
    return std::nullopt;
  }
  const auto directory_key = base::ToLowerASCII(profile_directory);
  for (const auto& value : *catalog->FindList("items")) {
    if (base::ToLowerASCII(*value.GetDict().FindString("profile_directory")) ==
        directory_key) {
      return std::nullopt;
    }
  }
  for (const auto& value : *catalog->FindList("retired_profiles")) {
    if (base::ToLowerASCII(value.GetString()) == directory_key) {
      return std::nullopt;
    }
  }
  Workspace item{base::Uuid::GenerateRandomV4().AsLowercaseString(),
                 std::string(name), std::string(profile_directory)};
  catalog->FindList("items")->Append(
      base::DictValue()
          .Set("id", item.id)
          .Set("name", item.name)
          .Set("profile_directory", item.profile_directory));
  catalog->Set("active_id", item.id);
  Commit(std::move(*catalog));
  return item;
}

bool WorkspaceService::Rename(std::string_view id, std::string_view name) {
  auto catalog = ReadValidated();
  if (!catalog || !ValidName(name) || NameUsed(*catalog, name, id)) {
    return false;
  }
  auto* row = FindRow(*catalog, id);
  if (!row) {
    return false;
  }
  row->Set("name", name);
  Commit(std::move(*catalog));
  return true;
}

bool WorkspaceService::Activate(std::string_view id) {
  auto catalog = ReadValidated();
  if (!catalog || !FindRow(*catalog, id)) {
    return false;
  }
  catalog->Set("active_id", id);
  Commit(std::move(*catalog));
  return true;
}

bool WorkspaceService::Remove(std::string_view id) {
  auto catalog = ReadValidated();
  if (!catalog) {
    return false;
  }
  auto* row = FindRow(*catalog, id);
  if (!row) {
    return false;
  }
  catalog->FindList("retired_profiles")
      ->Append(*row->FindString("profile_directory"));
  auto* items = catalog->FindList("items");
  items->EraseIf([&](const base::Value& value) {
    return *value.GetDict().FindString("id") == id;
  });
  if (*catalog->FindString("active_id") == id) {
    catalog->Set("active_id", items->empty()
                                  ? std::string()
                                  : *items->front().GetDict().FindString("id"));
  }
  Commit(std::move(*catalog));
  return true;
}

bool WorkspaceService::Reorder(const std::vector<std::string>& ids) {
  auto catalog = ReadValidated();
  if (!catalog || ids.size() != catalog->FindList("items")->size()) {
    return false;
  }
  std::set<std::string> seen;
  base::ListValue ordered;
  for (const auto& id : ids) {
    auto* row = FindRow(*catalog, id);
    if (!row || !seen.insert(id).second) {
      return false;
    }
    ordered.Append(row->Clone());
  }
  catalog->Set("items", std::move(ordered));
  Commit(std::move(*catalog));
  return true;
}
}  // namespace fly
