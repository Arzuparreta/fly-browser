// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/preferences_service.h"

#include "base/json/json_reader.h"
#include "components/prefs/testing_pref_service.h"
#include "fly/commands/generated_contract.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class PreferencesServiceTest : public testing::Test {
 protected:
  PreferencesServiceTest() : service_(prefs_) {
    PreferencesService::RegisterLocalStatePrefs(prefs_.registry());
  }
  TestingPrefServiceSimple prefs_;
  PreferencesService service_;
};
TEST_F(PreferencesServiceTest, SidebarAutoCollapseDefaultsWithoutWriting) {
  EXPECT_EQ(true, service_.ReadSidebarAutoCollapse());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  ASSERT_TRUE(service_.SetSidebarAutoCollapse(false));
  PreferencesService restarted(prefs_);
  EXPECT_EQ(false, restarted.ReadSidebarAutoCollapse());
  ASSERT_TRUE(restarted.SetSidebarAutoCollapse(true));
  EXPECT_EQ(true, service_.ReadSidebarAutoCollapse());
}
TEST_F(PreferencesServiceTest, SidebarChoicePreservesOtherSettingsAndMetadata) {
  ASSERT_TRUE(service_.SetWindowControlsVisible(true));
  auto stored = prefs_.GetDict(kPreferencesPref).Clone();
  stored.Set("source", "keep");
  stored.EnsureDict("values")->Set("future", base::ListValue().Append("keep"));
  prefs_.SetDict(kPreferencesPref, stored.Clone());
  ASSERT_TRUE(service_.SetSidebarAutoCollapse(false));
  stored.EnsureDict("values")->Set("sidebar_auto_collapse", false);
  EXPECT_EQ(stored, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest, SidebarChoiceRejectsRecoveryAndManagedWrites) {
  for (const auto& stored :
       {base::DictValue().Set("schema", 2).Set("values", base::DictValue()),
        base::DictValue()
            .Set("schema", 1)
            .Set("values",
                 base::DictValue().Set("sidebar_auto_collapse", "true"))}) {
    prefs_.SetDict(kPreferencesPref, stored.Clone());
    EXPECT_FALSE(service_.ReadSidebarAutoCollapse());
    EXPECT_FALSE(service_.SetSidebarAutoCollapse(false));
    EXPECT_EQ(stored, prefs_.GetDict(kPreferencesPref));
  }
  const auto policy =
      base::DictValue()
          .Set("schema", 1)
          .Set("values", base::DictValue().Set("sidebar_auto_collapse", true));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  EXPECT_EQ(true, service_.ReadSidebarAutoCollapse());
  EXPECT_FALSE(service_.CanEditSidebarAutoCollapse());
  EXPECT_FALSE(service_.SetSidebarAutoCollapse(false));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest, WindowControlsDefaultIsHiddenAndChoicePersists) {
  EXPECT_EQ(false, service_.ReadWindowControlsVisible());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  ASSERT_TRUE(service_.SetWindowControlsVisible(true));
  PreferencesService restarted(prefs_);
  EXPECT_EQ(true, restarted.ReadWindowControlsVisible());
  ASSERT_TRUE(restarted.SetWindowControlsVisible(false));
  EXPECT_EQ(false, service_.ReadWindowControlsVisible());
}
TEST_F(PreferencesServiceTest,
       WindowControlsPreserveOtherGroupsAndUnknownData) {
  ASSERT_TRUE(service_.SetStatusBarVisible(false));
  auto stored = prefs_.GetDict(kPreferencesPref).Clone();
  stored.Set("import", base::DictValue().Set("source", "preserved"));
  stored.EnsureDict("values")->Set("future", "keep");
  prefs_.SetDict(kPreferencesPref, stored.Clone());
  ASSERT_TRUE(service_.SetWindowControlsVisible(true));
  auto expected = stored.Clone();
  expected.EnsureDict("values")->Set("show_window_controls", true);
  EXPECT_EQ(expected, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest,
       WindowControlsCannotRewriteRecoveryOrManagedData) {
  for (const auto& stored :
       {base::DictValue().Set("schema", 2).Set("values", base::DictValue()),
        base::DictValue()
            .Set("schema", 1)
            .Set("values",
                 base::DictValue().Set("show_window_controls", "true"))}) {
    prefs_.SetDict(kPreferencesPref, stored.Clone());
    EXPECT_FALSE(service_.ReadWindowControlsVisible());
    EXPECT_FALSE(service_.SetWindowControlsVisible(true));
    EXPECT_EQ(stored, prefs_.GetDict(kPreferencesPref));
  }
  const auto policy =
      base::DictValue()
          .Set("schema", 1)
          .Set("values", base::DictValue().Set("show_window_controls", true));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  EXPECT_EQ(true, service_.ReadWindowControlsVisible());
  EXPECT_FALSE(service_.CanEditWindowControls());
  EXPECT_FALSE(service_.SetWindowControlsVisible(false));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest, SuspendTimeoutDefaultsAndDisabledChoicePersist) {
  EXPECT_EQ(15, service_.ReadSuspendTimeoutMinutes());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  ASSERT_TRUE(service_.SetSuspendTimeoutMinutes(0));
  PreferencesService restarted(prefs_);
  EXPECT_EQ(0, restarted.ReadSuspendTimeoutMinutes());
  for (int expected : {5, 15, 30, 60, 0}) {
    ASSERT_TRUE(restarted.CycleSuspendTimeout());
    EXPECT_EQ(expected, restarted.ReadSuspendTimeoutMinutes());
  }
}
TEST_F(PreferencesServiceTest,
       SuspendTimeoutRejectsInvalidAndPreservesMetadata) {
  prefs_.SetDict(kPreferencesPref,
                 base::DictValue()
                     .Set("schema", 1)
                     .Set("import", base::DictValue().Set("fixture", "keep"))
                     .Set("values", base::DictValue()
                                        .Set("show_clock", false)
                                        .Set("unknown", "preserved")));
  ASSERT_TRUE(service_.SetSuspendTimeoutMinutes(30));
  auto before = prefs_.GetDict(kPreferencesPref).Clone();
  EXPECT_FALSE(service_.SetSuspendTimeoutMinutes(-1));
  EXPECT_FALSE(service_.SetSuspendTimeoutMinutes(1));
  EXPECT_EQ(before, prefs_.GetDict(kPreferencesPref));
  ASSERT_TRUE(service_.SetSuspendTimeoutMinutes(60));
  const auto& stored = prefs_.GetDict(kPreferencesPref);
  EXPECT_EQ(*before.FindDict("import"), *stored.FindDict("import"));
  EXPECT_FALSE(*stored.FindDict("values")->FindBool("show_clock"));
  EXPECT_EQ("preserved", *stored.FindDict("values")->FindString("unknown"));
}
TEST_F(PreferencesServiceTest, SuspendTimeoutCannotOverwriteRecoveryData) {
  base::ListValue invalid;
  invalid.Append("15");
  invalid.Append(-1);
  invalid.Append(10);
  invalid.Append(true);
  for (const auto& value : invalid) {
    auto stored =
        base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set("tab_suspend_timeout_minutes",
                                                 value.Clone()));
    prefs_.SetDict(kPreferencesPref, stored.Clone());
    EXPECT_FALSE(service_.ReadSuspendTimeoutMinutes());
    EXPECT_FALSE(service_.CanEditSuspendTimeout());
    EXPECT_FALSE(service_.SetSuspendTimeoutMinutes(15));
    EXPECT_FALSE(service_.CycleSuspendTimeout());
    EXPECT_EQ(stored, prefs_.GetDict(kPreferencesPref));
  }
  auto future =
      base::DictValue().Set("schema", 99).Set("values", base::DictValue());
  prefs_.SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(service_.ReadSuspendTimeoutMinutes());
  EXPECT_FALSE(service_.SetSuspendTimeoutMinutes(0));
  EXPECT_EQ(future, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest, ManagedSuspendTimeoutReadsWithoutUserWrites) {
  auto policy = base::DictValue()
                    .Set("schema", 1)
                    .Set("values", base::DictValue().Set(
                                       "tab_suspend_timeout_minutes", 0));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  EXPECT_EQ(0, service_.ReadSuspendTimeoutMinutes());
  EXPECT_FALSE(service_.CanEditSuspendTimeout());
  EXPECT_FALSE(service_.SetSuspendTimeoutMinutes(5));
  EXPECT_FALSE(service_.CycleSuspendTimeout());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
}

TEST_F(PreferencesServiceTest, ReadAndClockTicksDoNotWriteBeforeImport) {
  ASSERT_TRUE(service_.ReadClock());
  EXPECT_EQ(ClockPreferences(), *service_.ReadClock());
  for (int i = 0; i < 5; ++i)
    EXPECT_FALSE(FormatClock(base::Time::Now() + base::Seconds(i),
                             service_.ReadClock()->format)
                     .label.empty());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
}
TEST_F(PreferencesServiceTest,
       ClockCyclesAndExplicitHiddenChoiceSurviveRestart) {
  ASSERT_TRUE(service_.CycleClockFormat());
  EXPECT_EQ("time", service_.ReadClock()->format);
  ASSERT_TRUE(service_.CycleClockFormat());
  EXPECT_EQ("seconds", service_.ReadClock()->format);
  ASSERT_TRUE(service_.SetClockVisible(false));
  PreferencesService restarted(prefs_);
  ASSERT_TRUE(restarted.ReadClock());
  EXPECT_FALSE(restarted.ReadClock()->visible);
  EXPECT_EQ("seconds", restarted.ReadClock()->format);
  ASSERT_TRUE(restarted.CycleClockFormat());
  EXPECT_EQ("datetime", restarted.ReadClock()->format);
  EXPECT_FALSE(restarted.ReadClock()->visible);
}
TEST_F(PreferencesServiceTest,
       ClockEditsPreserveOtherGroupsAndUnknownMetadata) {
  prefs_.SetDict(
      kPreferencesPref,
      base::DictValue()
          .Set("schema", 1)
          .Set("import", base::DictValue().Set("source", "fixture"))
          .Set("values",
               base::DictValue()
                   .Set("theme", "light")
                   .Set("future_setting", base::ListValue().Append("keep"))));
  const auto before = prefs_.GetDict(kPreferencesPref).Clone();
  ASSERT_TRUE(service_.SetClockVisible(false));
  ASSERT_TRUE(service_.CycleClockFormat());
  const auto& after = prefs_.GetDict(kPreferencesPref);
  EXPECT_EQ(*before.FindDict("import"), *after.FindDict("import"));
  EXPECT_EQ(*before.FindDict("values")->Find("future_setting"),
            *after.FindDict("values")->Find("future_setting"));
  EXPECT_EQ("light", *after.FindDict("values")->FindString("theme"));
}
TEST_F(PreferencesServiceTest, FutureAndMalformedDataCannotBeOverwritten) {
  for (const auto& invalid :
       {base::DictValue().Set("schema", 2).Set("values", base::DictValue()),
        base::DictValue().Set("schema", 1),
        base::DictValue().Set("schema", 1).Set("values", "not a map"),
        base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set("show_clock", 0)),
        base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set("clock_format", "unknown"))}) {
    prefs_.SetDict(kPreferencesPref, invalid.Clone());
    EXPECT_FALSE(service_.ReadClock());
    EXPECT_FALSE(service_.SetClockVisible(true));
    EXPECT_FALSE(service_.CycleClockFormat());
    EXPECT_EQ(invalid, prefs_.GetDict(kPreferencesPref));
  }
}
TEST_F(PreferencesServiceTest, NativePolicyPrecedenceDisablesClockEdits) {
  auto policy = base::DictValue()
                    .Set("schema", 1)
                    .Set("values", base::DictValue()
                                       .Set("show_clock", false)
                                       .Set("clock_format", "time"));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  ASSERT_TRUE(service_.ReadClock());
  EXPECT_FALSE(service_.ReadClock()->visible);
  EXPECT_FALSE(service_.CanEditClock());
  EXPECT_FALSE(service_.SetClockVisible(true));
  EXPECT_FALSE(service_.CycleClockFormat());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
  prefs_.RemoveManagedPref(kPreferencesPref);
  EXPECT_TRUE(service_.CanEditClock());
  EXPECT_TRUE(service_.SetClockVisible(true));
}
TEST_F(PreferencesServiceTest,
       FormatsUseLocalTwentyFourHourTimeAndKeepDateInSeconds) {
  base::Time::Exploded local = {2026, 10, 0, 18, 9, 8, 7, 0};
  base::Time time;
  ASSERT_TRUE(base::Time::FromLocalExploded(local, &time));
  EXPECT_EQ(u"Sun 18 Oct  09:08", FormatClock(time, "datetime").label);
  EXPECT_EQ(u"09:08", FormatClock(time, "time").label);
  EXPECT_EQ(u"Sun 18 Oct  09:08:07", FormatClock(time, "seconds").label);
  EXPECT_NE(std::u16string::npos,
            FormatClock(time, "time").tooltip.find(u"2026"));
  EXPECT_TRUE(FormatClock(time, "unknown").label.empty());
  EXPECT_TRUE(FormatClock(base::Time(), "time").label.empty());
}
TEST_F(PreferencesServiceTest, StatusChoicePreservesClockAndOtherMetadata) {
  ASSERT_EQ(true, service_.ReadStatusBarVisible());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  ASSERT_TRUE(service_.CycleClockFormat());
  auto stored = prefs_.GetDict(kPreferencesPref).Clone();
  stored.Set("future_metadata", "preserve");
  stored.EnsureDict("values")->Set("unknown", "preserve");
  prefs_.SetDict(kPreferencesPref, std::move(stored));
  ASSERT_TRUE(service_.SetStatusBarVisible(false));
  PreferencesService restarted(prefs_);
  ASSERT_EQ(false, restarted.ReadStatusBarVisible());
  ASSERT_EQ("time", restarted.ReadClock()->format);
  ASSERT_TRUE(restarted.CycleClockFormat());
  EXPECT_EQ(false, restarted.ReadStatusBarVisible());
  EXPECT_EQ("preserve",
            *prefs_.GetDict(kPreferencesPref).FindString("future_metadata"));
  EXPECT_EQ("preserve", *prefs_.GetDict(kPreferencesPref)
                             .FindDict("values")
                             ->FindString("unknown"));
}
TEST_F(PreferencesServiceTest,
       StatusEditsRespectPolicyAndRejectMalformedGroups) {
  prefs_.SetUserPref(kPreferencesPref, base::Value("preserve wrong root type"));
  EXPECT_FALSE(service_.ReadClock());
  EXPECT_FALSE(service_.ReadStatusBarVisible());
  EXPECT_FALSE(service_.SetStatusBarVisible(true));
  EXPECT_FALSE(service_.SetClockVisible(true));
  ASSERT_TRUE(prefs_.GetUserPref(kPreferencesPref));
  EXPECT_EQ("preserve wrong root type",
            prefs_.GetUserPref(kPreferencesPref)->GetString());
  const auto recommendation =
      base::DictValue().Set("schema", 1).Set("values", base::DictValue());
  prefs_.SetRecommendedPref(kPreferencesPref,
                            base::Value(recommendation.Clone()));
  EXPECT_FALSE(service_.ReadStatusBarVisible());
  EXPECT_FALSE(service_.SetStatusBarVisible(true));
  EXPECT_EQ("preserve wrong root type",
            prefs_.GetUserPref(kPreferencesPref)->GetString());
  prefs_.RemoveRecommendedPref(kPreferencesPref);
  const auto invalid =
      base::DictValue()
          .Set("schema", 1)
          .Set("values",
               base::DictValue().Set("show_status_bar", "not a boolean"));
  prefs_.SetDict(kPreferencesPref, invalid.Clone());
  EXPECT_FALSE(service_.ReadStatusBarVisible());
  EXPECT_FALSE(service_.ReadClock());
  EXPECT_FALSE(service_.SetStatusBarVisible(true));
  EXPECT_FALSE(service_.SetClockVisible(true));
  EXPECT_EQ(invalid, prefs_.GetDict(kPreferencesPref));
  const auto policy =
      base::DictValue()
          .Set("schema", 1)
          .Set("values", base::DictValue().Set("show_status_bar", false));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  EXPECT_EQ(false, service_.ReadStatusBarVisible());
  EXPECT_FALSE(service_.CanEditStatusBar());
  EXPECT_FALSE(service_.SetStatusBarVisible(true));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
}
TEST_F(PreferencesServiceTest, NativeWebsiteThemeMatchesRetainedRuntime) {
  ASSERT_EQ(ThemePreferences(), service_.ReadTheme());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kPreferencesPref));
  const auto cases =
      base::JSONReader::ReadList(kWebsiteThemeCasesJSON, base::JSON_PARSE_RFC);
  ASSERT_TRUE(cases);
  for (const auto& value : *cases) {
    const auto& item = value.GetDict();
    const auto& input = *item.FindDict("input");
    const ThemePreferences theme{*input.FindString("theme"),
                                 *input.FindBool("sync_website_theme")};
    EXPECT_EQ(*item.FindString("expected"),
              WebsiteUsesDarkTheme(theme) ? "dark" : "light");
  }
}
TEST_F(PreferencesServiceTest,
       ThemeChoicesPreserveOtherGroupsAndSurviveRestart) {
  ASSERT_TRUE(service_.CycleClockFormat());
  ASSERT_TRUE(service_.SetStatusBarVisible(false));
  auto stored = prefs_.GetDict(kPreferencesPref).Clone();
  stored.EnsureDict("values")->Set("theme_colors",
                                   base::DictValue().Set("future", "preserve"));
  prefs_.SetDict(kPreferencesPref, std::move(stored));
  ASSERT_TRUE(service_.ToggleTheme());
  ASSERT_TRUE(service_.SetWebsiteThemeSync(false));
  PreferencesService restarted(prefs_);
  ASSERT_TRUE(restarted.ReadTheme());
  EXPECT_EQ("light", restarted.ReadTheme()->mode);
  EXPECT_FALSE(restarted.ReadTheme()->sync_websites);
  ASSERT_TRUE(restarted.ToggleTheme());
  EXPECT_EQ("dark", restarted.ReadTheme()->mode);
  EXPECT_FALSE(WebsiteUsesDarkTheme(*restarted.ReadTheme()));
  EXPECT_EQ("time", restarted.ReadClock()->format);
  EXPECT_EQ(false, restarted.ReadStatusBarVisible());
  EXPECT_EQ("preserve", *prefs_.GetDict(kPreferencesPref)
                             .FindDict("values")
                             ->FindDict("theme_colors")
                             ->FindString("future"));
}
TEST_F(PreferencesServiceTest, MalformedFutureAndPolicyThemeDataIsPreserved) {
  for (const auto& invalid :
       {base::DictValue()
            .Set("schema", 1)
            .Set("values", base::DictValue().Set("theme", "unknown")),
        base::DictValue()
            .Set("schema", 1)
            .Set("values",
                 base::DictValue().Set("sync_website_theme", "false")),
        base::DictValue().Set("schema", 2).Set("values", base::DictValue())}) {
    prefs_.SetDict(kPreferencesPref, invalid.Clone());
    EXPECT_FALSE(service_.ReadTheme());
    EXPECT_FALSE(service_.ToggleTheme());
    EXPECT_FALSE(service_.SetWebsiteThemeSync(true));
    EXPECT_EQ(invalid, prefs_.GetDict(kPreferencesPref));
  }
  const auto policy =
      base::DictValue()
          .Set("schema", 1)
          .Set("values", base::DictValue().Set("theme", "light"));
  prefs_.SetManagedPref(kPreferencesPref, base::Value(policy.Clone()));
  ASSERT_TRUE(service_.ReadTheme());
  EXPECT_EQ("light", service_.ReadTheme()->mode);
  EXPECT_FALSE(service_.CanEditTheme());
  EXPECT_FALSE(service_.ToggleTheme());
  EXPECT_FALSE(service_.SetWebsiteThemeSync(false));
  EXPECT_EQ(policy, prefs_.GetDict(kPreferencesPref));
}
}  // namespace
}  // namespace fly
