// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/import/legacy_import.h"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/command_line.h"
#include "base/containers/adapters.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/no_destructor.h"
#include "base/strings/strcat.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/time/time.h"
#include "base/values.h"
#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/bookmarks/frequent_sites_service.h"
#include "fly/commands/command_service.h"
#include "fly/commands/generated_contract.h"
#include "fly/commands/keyboard_config.h"
#include "fly/preferences/preferences_service.h"
#include "fly/preferences/search_service.h"
#include "fly/workspaces/workspace_service.h"

namespace fly {
namespace {
constexpr char kBundledBlocker[] = "ddkjiahejlhfcafbddmgiahcphecmpfh";
constexpr char kLegacyBlockerFolder[] = "ublock-origin";
// Site data an Electron partition shares with a Chromium profile directory.
constexpr const char* kSiteData[] = {
    "Cookies",        "Cookies-journal", "Local Storage",  "Session Storage",
    "IndexedDB",      "WebStorage",      "Service Worker", "File System",
    "databases",      "Shared Storage",
};

// Keys of the top-level object `field`, in file order. base::DictValue sorts
// keys, but Electron's workspace order is the object's insertion order.
std::vector<std::string> OrderedKeys(std::string_view text,
                                     std::string_view field) {
  std::vector<std::string> keys;
  int depth = 0;
  bool in_field = false;
  int field_depth = -1;
  std::string token;
  bool expect_key = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '"') {
      std::string value;
      for (++i; i < text.size() && text[i] != '"'; ++i) {
        if (text[i] == '\\' && i + 1 < text.size())
          ++i;
        value += text[i];
      }
      size_t j = i + 1;
      while (j < text.size() && base::IsAsciiWhitespace(text[j]))
        ++j;
      const bool is_key = j < text.size() && text[j] == ':';
      if (is_key && depth == 1 && value == field)
        in_field = true;
      else if (is_key && field_depth >= 0 && depth == field_depth)
        keys.push_back(value);
      continue;
    }
    if (c == '{' || c == '[') {
      ++depth;
      if (in_field && c == '{') {
        field_depth = depth;
        in_field = false;
      }
    } else if (c == '}' || c == ']') {
      if (depth == field_depth)
        return keys;
      --depth;
    }
  }
  (void)token;
  (void)expect_key;
  return keys;
}
std::optional<base::Value> ReadJson(const base::FilePath& file) {
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(file, &text, 64 * 1024 * 1024))
    return std::nullopt;
  return base::JSONReader::Read(text, base::JSON_PARSE_RFC);
}
bool SourceRunning(const base::FilePath& legacy) {
#if BUILDFLAG(IS_POSIX)
  base::FilePath target;
  if (!base::ReadSymbolicLink(legacy.AppendASCII("SingletonLock"), &target))
    return false;
  // "<hostname>-<pid>": a live process means Electron Fly is open.
  const auto value = target.value();
  const auto dash = value.rfind('-');
  int pid = 0;
  if (dash == std::string::npos ||
      !base::StringToInt(value.substr(dash + 1), &pid) || pid <= 0)
    return false;
  return base::PathExists(
      base::FilePath("/proc").AppendASCII(base::NumberToString(pid)));
#else
  return false;
#endif
}
constexpr char kPartialFile[] = "fly-legacy-import.partial";
// An import killed before it finished (for example while copying a large
// IndexedDB) left the files it lists; remove them so the import runs again.
void RemovePartialImport(const base::FilePath& user_data_dir) {
  const auto marker = user_data_dir.AppendASCII(kPartialFile);
  std::string text;
  if (!base::ReadFileToStringWithMaxSize(marker, &text, 64 * 1024))
    return;
  for (const auto& name : base::SplitString(text, "\n", base::TRIM_WHITESPACE,
                                            base::SPLIT_WANT_NONEMPTY)) {
    unsigned number = 0;
    const bool profile =
        name == "Default" ||
        (name.starts_with("Profile ") &&
         base::StringToUint(std::string_view(name).substr(8), &number));
    const bool owned = profile || name == "scripts" || name == "styles" ||
                       name == "script-storage" || name == kLegacyImportFile;
    if (owned)
      base::DeletePathRecursively(user_data_dir.AppendASCII(name));
  }
  base::DeleteFile(marker);
}
bool CopySiteData(const base::FilePath& partition,
                  const base::FilePath& profile) {
  if (!base::CreateDirectory(profile))
    return false;
  for (const char* name : kSiteData) {
    const auto source = partition.AppendASCII(name);
    if (!base::PathExists(source) || base::IsLink(source))
      continue;
    const auto target = profile.AppendASCII(name);
    const bool ok = base::DirectoryExists(source)
                        ? base::CopyDirectory(source, profile, true)
                        : base::CopyFile(source, target);
    if (!ok)
      return false;
  }
  return true;
}
void CopyFolder(const base::FilePath& from, const base::FilePath& to) {
  if (!base::DirectoryExists(from) || base::IsLink(from))
    return;
  base::CreateDirectory(to);
  base::FileEnumerator files(from, false, base::FileEnumerator::FILES);
  for (auto path = files.Next(); !path.empty(); path = files.Next()) {
    const auto target = to.Append(path.BaseName());
    if (!base::IsLink(path) && !base::PathExists(target))
      base::CopyFile(path, target);
  }
}

// `merge`: native Fly was already used; settings it stores keep their value.
void ImportSettings(const base::DictValue& config,
                    PrefService& local_state,
                    bool merge,
                    std::vector<std::string>& report) {
  PreferencesService prefs(local_state);
  const auto importable = [&](std::string_view key) {
    return !merge || !prefs.HasStoredValue(key);
  };
  for (const char* key : {"disable_gpu", "background_throttling",
                          "enable_diagnostics", "desktop_integration_offered",
                          "extensions_enabled"})
    if (const auto value = config.FindBool(key); value && importable(key))
      prefs.SetBool(key, *value);
  if (const auto limit = config.FindInt("process_limit");
      limit && importable("process_limit"))
    prefs.SetInt("process_limit", std::clamp(*limit, 1, 8));
  for (const char* key : {"spellchecker_language", "interface_scale",
                          "email_handler", "last_seen_version"})
    if (const auto* value = config.FindString(key); value && importable(key))
      prefs.SetString(key, *value);
  if (const auto* theme = config.FindString("theme");
      theme && *theme == "light" && importable("theme"))
    prefs.ToggleTheme();
  if (const auto sync = config.FindBool("sync_website_theme");
      sync && importable("sync_website_theme"))
    prefs.SetWebsiteThemeSync(*sync);
  auto minutes = config.FindInt("tab_suspend_timeout_minutes");
  if (!minutes)
    minutes = config.FindInt("tab_sleep_timeout_minutes");
  if (minutes && importable("tab_suspend_timeout_minutes"))
    prefs.SetSuspendTimeoutMinutes(*minutes);
  if (const auto value = config.FindBool("sidebar_auto_collapse"); value && importable("sidebar_auto_collapse"))
    prefs.SetSidebarAutoCollapse(*value);
  if (const auto value = config.FindBool("show_status_bar"); value && importable("show_status_bar"))
    prefs.SetStatusBarVisible(*value);
  if (const auto value = config.FindBool("show_clock"); value && importable("show_clock"))
    prefs.SetClockVisible(*value);
  if (const auto value = config.FindBool("show_frequent_sites"); value && importable("show_frequent_sites"))
    prefs.SetFrequentSitesVisible(*value);
  if (const auto value = config.FindBool("show_window_controls"); value && importable("show_window_controls"))
    prefs.SetWindowControlsVisible(*value);
  if (const auto* format = config.FindString("clock_format");
      format && importable("clock_format")) {
    for (int i = 0; i < 3 && prefs.ReadClock() &&
                    prefs.ReadClock()->format != *format;
         ++i)
      prefs.CycleClockFormat();
  }
  // Only colours that differ from the defaults become explicit choices.
  const std::map<std::string, std::map<std::string, std::string>> defaults = {
      {"dark",
       {{"accent", "#e0261c"}, {"bg_main", "#121211"},
        {"bg_sidebar", "#0b0b0a"}, {"text", "#e7e1d3"}}},
      {"light",
       {{"accent", "#e0261c"}, {"bg_main", "#ece6d8"},
        {"bg_sidebar", "#e3dccb"}, {"text", "#1a1814"}}}};
  if (const auto* colors = config.FindDict("theme_colors");
      colors && importable("theme_colors")) {
    for (const auto& [mode, keys] : defaults) {
      const auto* palette = colors->FindDict(mode);
      for (const auto& [key, fallback] : keys) {
        const auto* value = palette ? palette->FindString(key) : nullptr;
        if (value && base::ToLowerASCII(*value) != fallback)
          prefs.SetThemeColor(mode == "dark", key, *value);
      }
    }
  }
  SearchService search(local_state);
  if (const auto* engine = config.FindString("search_engine");
      engine && *engine != "https://duckduckgo.com/?q=%s" &&
      importable("search_engine") &&
      !search.SetEngine(*engine))
    report.push_back("Search engine not imported (unsupported template).");
  if (const auto* aliases = config.FindDict("custom_search_aliases");
      aliases && importable("custom_search_aliases")) {
    for (const auto [key, value] : *aliases)
      if (value.is_string() && !search.SetAlias(key, key, value.GetString()))
        report.push_back("Search alias \"" + key + "\" not imported.");
  }
}

// keybinds.js helpers: a value is a combination or a list of them.
std::vector<const base::Value*> ListOf(const base::Value& value) {
  std::vector<const base::Value*> values;
  if (value.is_none() || (value.is_string() && value.GetString().empty()))
    return values;
  if (const auto* list = value.GetIfList()) {
    for (const auto& item : *list)
      values.push_back(&item);
  } else {
    values.push_back(&value);
  }
  return values;
}
std::string Sequence(std::string_view value) {
  std::vector<std::string> strokes;
  for (const auto& part : base::SplitString(value, base::kWhitespaceASCII,
                                            base::TRIM_WHITESPACE,
                                            base::SPLIT_WANT_NONEMPTY))
    strokes.push_back(NormalizeStroke(part));
  if (strokes.empty())
    strokes.push_back(std::string());
  return base::JoinString(strokes, " ");
}
std::set<std::string> CombosOf(const base::DictValue& table) {
  std::set<std::string> combos;
  for (const auto [id, value] : table)
    for (const auto* item : ListOf(value))
      if (item->is_string())
        combos.insert(Sequence(item->GetString()));
  return combos;
}
std::set<std::string> TabChords(const base::Value* tabs) {
  std::set<std::string> chords;
  if (tabs && tabs->is_string() && !tabs->GetString().empty())
    for (int i = 1; i <= 9; ++i)
      chords.insert(
          NormalizeStroke(tabs->GetString() + "+" + base::NumberToString(i)));
  return chords;
}
const base::DictValue& StoredDefaults() {
  static const base::NoDestructor<base::DictValue> stored([] {
    auto parsed = base::JSONReader::ReadDict(kStoredKeyboardDefaultsJSON,
                                             base::JSON_PARSE_RFC);
    CHECK(parsed);
    return std::move(*parsed);
  }());
  return *stored;
}
// True when an earlier Electron version wrote this value without the user
// choosing it (keybinds.js storedDefault).
bool StoredDefault(std::string_view scope,
                   const std::string& id,
                   const base::Value& value,
                   bool legacy) {
  std::set<std::string> combos;
  for (const auto* item : ListOf(value)) {
    if (!item->is_string())
      return false;
    const auto combo = Sequence(item->GetString());
    for (const auto& stroke : base::SplitString(
             combo, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL))
      if (stroke.empty())
        return false;
    combos.insert(combo);
  }
  const std::vector<std::string> key(combos.begin(), combos.end());
  std::vector<const base::ListValue*> candidates;
  const auto add = [&](const base::DictValue* table) {
    const auto* entries = table ? table->FindList(id) : nullptr;
    if (entries)
      for (const auto& entry : *entries)
        if (entry.is_list())
          candidates.push_back(&entry.GetList());
  };
  const auto* legacy_table = StoredDefaults().FindDict("legacy");
  if (legacy) {
    add(legacy_table);
  } else {
    add(StoredDefaults().FindDictByDottedPath(base::StrCat({"saved.", scope})));
    if (scope == "shortcuts")
      add(legacy_table);
  }
  return std::ranges::any_of(candidates, [&](const base::ListValue* entry) {
    if (entry->size() != key.size())
      return false;
    for (size_t i = 0; i < key.size(); ++i)
      if (!(*entry)[i].is_string() || (*entry)[i].GetString() != key[i])
        return false;
    return true;
  });
}
}  // namespace

// keybinds.js initialiseCandidate and withDefaults: the user's choices win;
// defaults only give up the combinations that would collide with them.
KeyboardValidation ResolveLegacyKeyboard(const base::DictValue& raw) {
  const auto* version = raw.Find("version");
  if (version && !(version->is_int() && version->GetInt() == 2))
    return {std::nullopt, "Unsupported keyboard configuration version."};
  const bool legacy = !version;
  const base::DictValue empty;
  const base::DictValue* shortcuts = legacy ? &raw : raw.FindDict("shortcuts");
  const base::DictValue* normal = legacy ? &empty : raw.FindDict("normal");
  if (!shortcuts || !normal)
    return {std::nullopt, "Invalid binding tables."};
  const bool recorded = legacy || raw.FindBool("changesOnly") != true;
  const auto chosen = [&](const base::DictValue& table, std::string_view scope) {
    base::DictValue result;
    for (const auto [id, value] : table)
      if (IsKnownAction(id) &&
          !(recorded && StoredDefault(scope, id, value, legacy)))
        result.Set(id, value.Clone());
    return result;
  };
  auto own_shortcuts = chosen(*shortcuts, "shortcuts");
  auto own_normal = chosen(*normal, "normal");
  const base::Value* own_tabs = legacy ? nullptr : raw.Find("tabs");

  const auto defaults = DefaultKeyboardConfig();
  auto taken = CombosOf(own_shortcuts);
  for (const auto& chord : TabChords(own_tabs))
    taken.insert(chord);
  std::set<std::string> page_strokes;
  for (const auto& combo : CombosOf(own_normal))
    for (const auto& stroke : base::SplitString(
             combo, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL))
      page_strokes.insert(stroke);
  base::Value tabs;
  if (own_tabs) {
    tabs = own_tabs->Clone();
  } else {
    const base::Value default_tabs(defaults.tabs);
    const auto chords = TabChords(&default_tabs);
    tabs = std::ranges::any_of(chords,
                               [&](const std::string& chord) {
                                 return taken.contains(chord) ||
                                        page_strokes.contains(chord);
                               })
               ? base::Value("")
               : default_tabs.Clone();
  }
  base::DictValue all_shortcuts = own_shortcuts.Clone();
  for (const auto& [id, combos] : defaults.shortcuts) {
    if (all_shortcuts.contains(id))
      continue;
    base::ListValue kept;
    for (const auto& combo : combos)
      if (!taken.contains(Sequence(combo)) &&
          !page_strokes.contains(Sequence(combo)))
        kept.Append(combo);
    all_shortcuts.Set(id, std::move(kept));
  }
  auto globals = CombosOf(all_shortcuts);
  for (const auto& chord : TabChords(&tabs))
    globals.insert(chord);
  const auto pages = CombosOf(own_normal);
  const auto collides = [&](const std::string& combo) {
    for (const auto& stroke : base::SplitString(
             combo, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL))
      if (globals.contains(stroke))
        return true;
    return std::ranges::any_of(pages, [&](const std::string& other) {
      return other == combo || other.starts_with(combo + " ") ||
             combo.starts_with(other + " ");
    });
  };
  base::DictValue all_normal = own_normal.Clone();
  for (const auto& [id, combos] : defaults.normal) {
    if (all_normal.contains(id))
      continue;
    base::ListValue kept;
    for (const auto& combo : combos)
      if (!collides(Sequence(combo)))
        kept.Append(combo);
    all_normal.Set(id, std::move(kept));
  }
  return ValidateKeyboardConfig(base::DictValue()
                                    .Set("version", 2)
                                    .Set("shortcuts", std::move(all_shortcuts))
                                    .Set("normal", std::move(all_normal))
                                    .Set("tabs", std::move(tabs)));
}

namespace {
void ImportKeyboard(const base::FilePath& file,
                    PrefService& local_state,
                    std::vector<std::string>& report) {
  auto stored = ReadJson(file);
  if (!stored)
    return;
  if (!stored->is_dict()) {
    report.push_back("Keyboard settings not imported (unreadable file).");
    return;
  }
  const auto resolved = ResolveLegacyKeyboard(stored->GetDict());
  if (!resolved.config) {
    report.push_back("Keyboard settings not imported: " + resolved.error);
    return;
  }
  CommandService commands(local_state);
  const auto result = commands.Save(resolved.config->ToValue());
  if (!result.config)
    report.push_back("Keyboard settings not imported: " + result.error);
}

void ImportBookmarks(const base::FilePath& legacy,
                     PrefService& local_state,
                     std::vector<std::string>& report) {
  BookmarkService bookmarks(local_state);
  size_t count = 0;
  if (auto list = ReadJson(legacy.AppendASCII("bookmarks.json"));
      list && list->is_list()) {
    // Electron lists the newest first and Add() puts each one first, so add
    // the oldest first.
    for (const auto& item : base::Reversed(list->GetList())) {
      const auto* dict = item.GetIfDict();
      const auto* url = dict ? dict->FindString("url") : nullptr;
      if (!url)
        continue;
      const auto* title = dict->FindString("title");
      const auto stamp = dict->FindDouble("timestamp");
      if (bookmarks.Add(*url, title ? *title : *url,
                        stamp ? base::Time::FromMillisecondsSinceUnixEpoch(
                                    *stamp)
                              : base::Time::Now()))
        ++count;
    }
  }
  if (count)
    report.push_back(base::NumberToString(count) + " bookmarks");
  FrequentSitesService frequent(local_state);
  if (auto list = ReadJson(legacy.AppendASCII("hidden-sites.json"));
      list && list->is_list())
    for (const auto& origin : list->GetList())
      if (origin.is_string())
        frequent.Hide(origin.GetString());
}
}  // namespace

base::FilePath LegacyProfileDirectory() {
  const auto* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch("fly-import-from"))
    return command_line->GetSwitchValuePath("fly-import-from");
#if BUILDFLAG(IS_LINUX)
  // Electron Fly's CONFIG_DIR is always ~/.config/fly-browser (main.js), even
  // when XDG_CONFIG_HOME points elsewhere.
  return base::GetHomeDir().AppendASCII(".config").AppendASCII("fly-browser");
#else
  return base::FilePath();
#endif
}

LegacyImportResult RunLegacyImport(PrefService& local_state,
                                   const base::FilePath& user_data_dir,
                                   const base::FilePath& legacy) {
  if (legacy.empty() || user_data_dir.empty() || legacy == user_data_dir)
    return LegacyImportResult::kNotApplicable;
  RemovePartialImport(user_data_dir);
  // Done once: the pending file stays after it has been applied.
  if (base::PathExists(user_data_dir.AppendASCII(kLegacyImportFile)))
    return LegacyImportResult::kNotApplicable;
  const auto waiting = user_data_dir.AppendASCII(kLegacyImportWaitingFile);
  WorkspaceService workspaces(local_state);
  const auto catalog = workspaces.Read();
  if (!catalog)
    return LegacyImportResult::kNotApplicable;
  const bool fresh = catalog->items.empty() &&
                     !base::PathExists(user_data_dir.AppendASCII("Default"));
  // A native directory used before is only imported into when the import
  // waited for Electron Fly to close.
  if (!fresh && !base::PathExists(waiting))
    return LegacyImportResult::kNotApplicable;
  if (!base::PathExists(legacy.AppendASCII("session.json"))) {
    base::DeleteFile(waiting);
    return LegacyImportResult::kNotApplicable;
  }
  if (SourceRunning(legacy)) {
    base::WriteFile(waiting, "Electron Fly was open; import when it closes.\n");
    return LegacyImportResult::kSourceRunning;
  }
  const bool merge = !fresh;
  PreferencesService prefs(local_state);

  std::vector<std::string> report;
  std::string session_text;
  if (!base::ReadFileToStringWithMaxSize(legacy.AppendASCII("session.json"),
                                         &session_text, 64 * 1024 * 1024))
    return LegacyImportResult::kFailed;
  const auto session =
      base::JSONReader::Read(session_text, base::JSON_PARSE_RFC);
  if (!session || !session->is_dict())
    return LegacyImportResult::kFailed;
  const auto& state = session->GetDict();
  const auto* order = state.FindDict("workspaces");
  const auto* partitions = state.FindDict("workspace_partitions");
  const auto* pinned = state.FindDict("tab_pinned");
  const auto* current = state.FindString("current_workspace");

  base::DictValue pending;
  base::DictValue profiles;
  std::string current_dir;
  std::string first_dir;
  size_t index = 0;
  size_t tabs_total = 0;
  std::map<std::string, std::string> directory_of;
  // Directories this import creates; a killed import removes them on the
  // next start and runs again (RemovePartialImport).
  std::set<std::string> used_directories;
  for (const auto& item : catalog->items)
    used_directories.insert(base::ToLowerASCII(item.profile_directory));
  int next_profile =
      merge ? std::max(1, local_state.GetInteger("profile.profiles_created"))
            : 1;
  const auto allocate = [&]() -> std::string {
    if (!merge && index == 0)
      return "Default";
    for (;; ++next_profile) {
      const auto name = "Profile " + base::NumberToString(next_profile);
      if (!used_directories.contains(base::ToLowerASCII(name)) &&
          !base::PathExists(user_data_dir.AppendASCII(name))) {
        ++next_profile;
        return name;
      }
    }
  };
  std::vector<std::string> created;
  const auto mark = [&](const std::string& name) {
    created.push_back(name);
    base::WriteFile(user_data_dir.AppendASCII(kPartialFile),
                    base::JoinString(created, "\n") + "\n");
  };
  mark(kLegacyImportFile);
  for (const char* folder : {"scripts", "styles", "script-storage"})
    if (!base::PathExists(user_data_dir.AppendASCII(folder)))
      mark(folder);
  if (order) {
    auto names = OrderedKeys(session_text, "workspaces");
    if (names.size() != order->size()) {
      names.clear();
      for (const auto [name, urls] : *order)
        names.push_back(name);
    }
    for (const auto& name : names) {
      const auto* found = order->Find(name);
      if (!found)
        continue;
      const auto& urls = *found;
      const std::string directory = allocate();
      ++index;
      mark(directory);
      const auto* partition =
          partitions ? partitions->FindString(name) : nullptr;
      std::string folder = partition ? *partition : "";
      if (folder.starts_with("persist:"))
        folder = folder.substr(8);
      const auto partition_dir = legacy.AppendASCII("Partitions")
                                     .AppendASCII(folder);
      if (!folder.empty() && folder.find('/') == std::string::npos &&
          folder != ".." && base::DirectoryExists(partition_dir)) {
        if (!CopySiteData(partition_dir, user_data_dir.AppendASCII(directory)))
          report.push_back("Site data of \"" + name + "\" was not copied.");
      } else {
        base::CreateDirectory(user_data_dir.AppendASCII(directory));
      }
      // Names already used by native workspaces get a suffix.
      std::string workspace_name = name;
      auto added = workspaces.Add(workspace_name, directory);
      for (int copy = 1; !added && merge && copy < 10; ++copy) {
        workspace_name = name + (copy == 1 ? " (Electron)"
                                           : " (Electron " +
                                                 base::NumberToString(copy) +
                                                 ")");
        added = workspaces.Add(workspace_name, directory);
      }
      if (!added) {
        report.push_back("Workspace \"" + name + "\" could not be added.");
        continue;
      }
      directory_of[name] = directory;
      if (first_dir.empty())
        first_dir = directory;
      if (current && *current == name) {
        workspaces.Activate(added->id);
        current_dir = directory;
      }
      base::ListValue tabs;
      const auto* pins = pinned ? pinned->FindList(name) : nullptr;
      if (const auto* list = urls.GetIfList()) {
        for (size_t i = 0; i < list->size(); ++i) {
          if (!(*list)[i].is_string())
            continue;
          tabs.Append(base::DictValue()
                          .Set("url", (*list)[i].GetString())
                          .Set("pinned", pins && i < pins->size() &&
                                             (*pins)[i].is_bool() &&
                                             (*pins)[i].GetBool()));
          ++tabs_total;
        }
      }
      profiles.Set(directory,
                   base::DictValue().Set("tabs", std::move(tabs)));
    }
  }
  if (first_dir.empty())
    return LegacyImportResult::kFailed;
  if (current_dir.empty())
    current_dir = first_dir;
  // Start in the workspace Electron Fly was showing.
  local_state.SetString("profile.last_used", current_dir);
  local_state.SetInteger(
      "profile.profiles_created",
      merge ? next_profile : static_cast<int>(index) + 1);
  report.insert(report.begin(), base::NumberToString(index) +
                                    " workspaces with " +
                                    base::NumberToString(tabs_total) + " tabs");

  // Site zoom applies to every workspace; history (not per workspace in
  // Electron) goes to the first one.
  base::Value zoom = base::Value(base::DictValue());
  if (auto stored = ReadJson(legacy.AppendASCII("site-zoom.json"));
      stored && stored->is_dict())
    zoom = std::move(*stored);
  for (auto [directory, entry] : profiles)
    entry.GetDict().Set("zoom", zoom.Clone());
  if (auto history = ReadJson(legacy.AppendASCII("history.json"));
      history && history->is_list()) {
    report.push_back(base::NumberToString(history->GetList().size()) +
                     " history entries");
    profiles.FindDict(first_dir)->Set("history", std::move(*history));
  }

  if (auto config = ReadJson(legacy.AppendASCII("config.json"));
      config && config->is_dict()) {
    ImportSettings(config->GetDict(), local_state, merge, report);
    // The included Electron blocker (uBlock Origin) becomes uBlock Origin
    // Lite; keep the user's removal or per-workspace choices.
    const auto& cfg = config->GetDict();
    base::DictValue extensions;
    const auto* bundled = cfg.FindDict("extensions_bundled");
    const auto* entry = bundled ? bundled->FindDict(kLegacyBlockerFolder)
                                : nullptr;
    const auto* disabled = cfg.FindList("extensions_disabled");
    if ((entry && entry->FindBool("removed").value_or(false)) ||
        (disabled && std::ranges::find(*disabled, base::Value(
                                                      kLegacyBlockerFolder)) !=
                         disabled->end()))
      extensions.Set("removed", base::ListValue().Append(kBundledBlocker));
    if (const auto* by_workspace = cfg.FindDict("extensions_by_workspace")) {
      base::DictValue off;
      for (const auto [name, list] : *by_workspace) {
        const auto found = directory_of.find(name);
        if (found == directory_of.end() || !list.is_list())
          continue;
        if (std::ranges::find(list.GetList(),
                              base::Value(kLegacyBlockerFolder)) ==
            list.GetList().end())
          off.Set(found->second,
                  base::ListValue().Append(kBundledBlocker));
      }
      if (!off.empty())
        extensions.Set("workspace_off", std::move(off));
    }
    if (!extensions.empty() &&
        (!merge || !prefs.HasStoredValue("native_extensions")))
      prefs.SetNativeExtensions(std::move(extensions));
  } else {
    report.push_back("Settings file not found; defaults kept.");
  }
  if (!merge || local_state.FindPreference(kKeyboardPref)->IsDefaultValue())
    ImportKeyboard(legacy.AppendASCII("keybinds.json"), local_state, report);
  ImportBookmarks(legacy, local_state, report);
  for (const char* folder : {"scripts", "styles", "script-storage"})
    CopyFolder(legacy.AppendASCII(folder), user_data_dir.AppendASCII(folder));

  // Extensions installed by hand in Electron cannot move: native Chromium
  // needs Manifest V3 and its own installation.
  std::vector<std::string> manual;
  base::FileEnumerator extensions(legacy.AppendASCII("extensions"), false,
                                  base::FileEnumerator::DIRECTORIES);
  for (auto path = extensions.Next(); !path.empty(); path = extensions.Next()) {
    if (path.BaseName().value() == kLegacyBlockerFolder)
      continue;
    auto manifest = ReadJson(path.AppendASCII("manifest.json"));
    const auto* name = manifest && manifest->is_dict()
                           ? manifest->GetDict().FindString("name")
                           : nullptr;
    manual.push_back(name && !name->starts_with("__MSG_")
                         ? *name
                         : path.BaseName().AsUTF8Unsafe());
  }
  if (!manual.empty())
    report.push_back("Reinstall from the Chrome Web Store: " +
                     base::JoinString(manual, ", "));
  report.push_back(
      "Saved passwords are not part of Fly; sites keep you signed in where "
      "their cookies were imported.");

  pending.Set("version", 1);
  pending.Set("source", legacy.AsUTF8Unsafe());
  pending.Set("profiles", std::move(profiles));
  pending.Set("summary", "Imported from Electron Fly: " +
                             base::JoinString(report, "; ") + ".");
  std::string json;
  base::JSONWriter::WriteWithOptions(
      pending, base::JSONWriter::OPTIONS_PRETTY_PRINT, &json);
  if (!base::ImportantFileWriter::WriteFileAtomically(
          user_data_dir.AppendASCII(kLegacyImportFile), json))
    return LegacyImportResult::kFailed;
  base::DeleteFile(waiting);
  // The import is complete once Local State holds the new workspaces; until
  // then the marker makes the next start undo and repeat it.
  local_state.CommitPendingWrite(
      base::OnceClosure(),
      base::BindOnce(
          [](base::FilePath marker) { base::DeleteFile(marker); },
          user_data_dir.AppendASCII(kPartialFile)));
  return LegacyImportResult::kImported;
}
}  // namespace fly
