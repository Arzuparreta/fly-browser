// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/import/legacy_import.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/process/process.h"
#include "base/strings/string_number_conversions.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/testing_pref_service.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/commands/generated_contract.h"
#include "fly/commands/command_service.h"
#include "fly/preferences/preferences_service.h"
#include "fly/workspaces/workspace_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class LegacyImportTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(legacy_.CreateUniqueTempDir());
    ASSERT_TRUE(native_.CreateUniqueTempDir());
    WorkspaceService::RegisterLocalStatePrefs(prefs_.registry());
    PreferencesService::RegisterLocalStatePrefs(prefs_.registry());
    BookmarkService::RegisterLocalStatePrefs(prefs_.registry());
    CommandService::RegisterLocalStatePrefs(prefs_.registry());
    prefs_.registry()->RegisterStringPref("profile.last_used", "");
    prefs_.registry()->RegisterIntegerPref("profile.profiles_created", 1);
  }
  void Write(std::string_view name, std::string_view text) {
    const auto path = legacy_.GetPath().AppendASCII(name);
    ASSERT_TRUE(base::CreateDirectory(path.DirName()));
    ASSERT_TRUE(base::WriteFile(path, text));
  }
  void WriteFixture() {
    Write("session.json", R"({
      "current_workspace": "Work",
      "workspaces": {"Zuhause": ["https://example.org/"],
                     "Work": ["https://example.com/a", "https://example.com/b"]},
      "workspace_partitions": {"Zuhause": "persist:workspace-1",
                               "Work": "persist:work"},
      "tab_pinned": {"Work": [true, false]}})");
    Write("Partitions/workspace-1/Cookies", "home-cookies");
    Write("Partitions/work/Cookies", "work-cookies");
    Write("Partitions/work/Local Storage/leveldb/000001.log", "storage");
    Write("Partitions/work/GPUCache/data_0", "cache");
    Write("config.json", R"({"theme": "light", "process_limit": 2,
      "show_clock": false, "custom_search_aliases": {"wk": "https://w.example/?q=%s"},
      "extensions_disabled": ["ublock-origin"], "future_setting": 1})");
    // Electron stores bookmarks newest first.
    Write("bookmarks.json",
          R"([{"title": "Fly", "url": "https://fly.example/", "timestamp": 1700000000000},
              {"title": "Old", "url": "https://old.example/", "timestamp": 1600000000000}])");
    Write("keybinds.json",
          R"({"version": 2, "changesOnly": true, "shortcuts": {"spawn-tab": ["Ctrl+Y"]}, "normal": {}})");
    Write("site-zoom.json", R"({"example.com": 1.5})");
    Write("history.json",
          R"([{"title": "A", "url": "https://example.com/a", "timestamp": 1700000000000}])");
    Write("scripts/a.user.js", "// script");
    Write("extensions/dark/manifest.json", R"({"name": "Dark Reader"})");
  }
  base::ScopedTempDir legacy_;
  base::ScopedTempDir native_;
  TestingPrefServiceSimple prefs_;
};

TEST_F(LegacyImportTest, ImportsWorkspacesSiteDataSettingsAndPendingState) {
  WriteFixture();
  ASSERT_EQ(LegacyImportResult::kImported,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
  WorkspaceService workspaces(prefs_);
  const auto catalog = workspaces.Read();
  ASSERT_TRUE(catalog);
  ASSERT_EQ(2u, catalog->items.size());
  EXPECT_EQ("Zuhause", catalog->items[0].name);
  EXPECT_EQ("Default", catalog->items[0].profile_directory);
  EXPECT_EQ("Work", catalog->items[1].name);
  EXPECT_EQ("Profile 1", catalog->items[1].profile_directory);
  EXPECT_EQ(catalog->items[1].id, catalog->active_id);
  EXPECT_EQ("Profile 1", prefs_.GetString("profile.last_used"));
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(
      native_.GetPath().AppendASCII("Profile 1").AppendASCII("Cookies"),
      &text));
  EXPECT_EQ("work-cookies", text);
  EXPECT_TRUE(base::PathExists(native_.GetPath()
                                   .AppendASCII("Profile 1")
                                   .AppendASCII("Local Storage")
                                   .AppendASCII("leveldb")));
  // Caches are not site data.
  EXPECT_FALSE(base::PathExists(
      native_.GetPath().AppendASCII("Profile 1").AppendASCII("GPUCache")));
  PreferencesService preferences(prefs_);
  EXPECT_EQ("light", preferences.ReadTheme()->mode);
  EXPECT_EQ(2, preferences.ReadInt("process_limit"));
  EXPECT_FALSE(preferences.ReadClock()->visible);
  const auto extensions = preferences.ReadNativeExtensions();
  ASSERT_TRUE(extensions && extensions->FindList("removed"));
  BookmarkService bookmarks(prefs_);
  const auto saved = bookmarks.Read();
  ASSERT_TRUE(saved);
  ASSERT_EQ(2u, saved->size());
  EXPECT_EQ("https://fly.example/", (*saved)[0].url);
  EXPECT_EQ("https://old.example/", (*saved)[1].url);
  CommandService commands(prefs_);
  ASSERT_TRUE(commands.Read());
  EXPECT_EQ(std::vector<std::string>{"Ctrl+Y"},
            commands.Read()->shortcuts.at("spawn-tab"));
  EXPECT_TRUE(base::PathExists(
      native_.GetPath().AppendASCII("scripts").AppendASCII("a.user.js")));
  ASSERT_TRUE(base::ReadFileToString(
      native_.GetPath().AppendASCII(kLegacyImportFile), &text));
  const auto pending = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
  ASSERT_TRUE(pending);
  const auto* work = pending->FindDictByDottedPath("profiles.Profile 1");
  ASSERT_TRUE(work);
  EXPECT_EQ(2u, work->FindList("tabs")->size());
  EXPECT_EQ(true, (*work->FindList("tabs"))[0].GetDict().FindBool("pinned"));
  EXPECT_EQ(1.5, work->FindDict("zoom")->FindDouble("example.com"));
  EXPECT_TRUE(pending->FindDictByDottedPath("profiles.Default")
                  ->FindList("history"));
  EXPECT_NE(std::string::npos,
            pending->FindString("summary")->find("Dark Reader"));
  // The legacy profile is only read; a second start never imports again.
  EXPECT_TRUE(base::PathExists(legacy_.GetPath().AppendASCII("config.json")));
  EXPECT_EQ(LegacyImportResult::kNotApplicable,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
}

TEST_F(LegacyImportTest, WaitsForElectronFlyToCloseThenAddsItsWorkspaces) {
  WriteFixture();
  const auto lock = legacy_.GetPath().AppendASCII("SingletonLock");
  ASSERT_TRUE(base::CreateSymbolicLink(
      base::FilePath("host-" +
                     base::NumberToString(base::Process::Current().Pid())),
      lock));
  EXPECT_EQ(LegacyImportResult::kSourceRunning,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
  WorkspaceService workspaces(prefs_);
  EXPECT_TRUE(workspaces.Read()->items.empty());
  EXPECT_TRUE(base::PathExists(
      native_.GetPath().AppendASCII(kLegacyImportWaitingFile)));
  // Native Fly starts anyway: its first workspace and a setting of its own.
  ASSERT_TRUE(base::CreateDirectory(native_.GetPath().AppendASCII("Default")));
  ASSERT_TRUE(workspaces.Add("Work", "Default"));
  PreferencesService preferences(prefs_);
  ASSERT_TRUE(preferences.SetClockVisible(true));
  EXPECT_EQ(LegacyImportResult::kSourceRunning,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
  // The next start after Electron Fly closed imports next to it.
  ASSERT_TRUE(base::DeleteFile(lock));
  ASSERT_EQ(LegacyImportResult::kImported,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
  const auto catalog = workspaces.Read();
  ASSERT_EQ(3u, catalog->items.size());
  EXPECT_EQ("Default", catalog->items[0].profile_directory);
  EXPECT_EQ("Zuhause", catalog->items[1].name);
  EXPECT_EQ("Profile 1", catalog->items[1].profile_directory);
  EXPECT_EQ("Work (Electron)", catalog->items[2].name);
  EXPECT_EQ("Profile 2", catalog->items[2].profile_directory);
  EXPECT_EQ(catalog->items[2].id, catalog->active_id);
  EXPECT_EQ("Profile 2", prefs_.GetString("profile.last_used"));
  EXPECT_EQ(3, prefs_.GetInteger("profile.profiles_created"));
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(
      native_.GetPath().AppendASCII("Profile 2").AppendASCII("Cookies"),
      &text));
  EXPECT_EQ("work-cookies", text);
  // Native choices win; settings native Fly never stored come from Electron.
  EXPECT_TRUE(preferences.ReadClock()->visible);
  EXPECT_EQ(2, preferences.ReadInt("process_limit"));
  EXPECT_FALSE(base::PathExists(
      native_.GetPath().AppendASCII(kLegacyImportWaitingFile)));
  EXPECT_EQ(LegacyImportResult::kNotApplicable,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
}

TEST_F(LegacyImportTest, RepeatsAnImportThatWasKilledHalfway) {
  WriteFixture();
  // A killed import left a partial copy and its marker; Local State was never
  // written, so the catalog is still empty.
  const auto stale = native_.GetPath().AppendASCII("Default");
  ASSERT_TRUE(base::CreateDirectory(stale.AppendASCII("IndexedDB")));
  ASSERT_TRUE(base::WriteFile(stale.AppendASCII("half-copied"), "x"));
  ASSERT_TRUE(base::WriteFile(
      native_.GetPath().AppendASCII("fly-legacy-import.partial"),
      "fly-legacy-import.json\nDefault\n"));
  ASSERT_EQ(LegacyImportResult::kImported,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
  EXPECT_FALSE(base::PathExists(stale.AppendASCII("half-copied")));
  std::string text;
  ASSERT_TRUE(base::ReadFileToString(stale.AppendASCII("Cookies"), &text));
  EXPECT_EQ("home-cookies", text);
  WorkspaceService workspaces(prefs_);
  EXPECT_EQ(2u, workspaces.Read()->items.size());
}

TEST_F(LegacyImportTest, KeyboardFilesResolveAsElectronFlyLoadsThem) {
  const auto cases =
      base::JSONReader::Read(kLegacyKeyboardCasesJSON, base::JSON_PARSE_RFC);
  ASSERT_TRUE(cases && cases->is_list());
  for (const auto& value : cases->GetList()) {
    const auto& row = value.GetDict();
    SCOPED_TRACE(*row.FindString("name"));
    const auto resolved = ResolveLegacyKeyboard(*row.FindDict("input"));
    ASSERT_EQ(*row.FindBool("ok"), resolved.config.has_value())
        << resolved.error;
    if (resolved.config)
      EXPECT_EQ(*row.FindDict("output"), resolved.config->ToValue());
  }
}

TEST_F(LegacyImportTest, LeavesUsedNativeDirectoriesAlone) {
  WriteFixture();
  ASSERT_TRUE(base::CreateDirectory(native_.GetPath().AppendASCII("Default")));
  EXPECT_EQ(LegacyImportResult::kNotApplicable,
            RunLegacyImport(prefs_, native_.GetPath(), legacy_.GetPath()));
}
}  // namespace
}  // namespace fly
