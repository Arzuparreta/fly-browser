// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/preferences_service.h"

#include "components/prefs/testing_pref_service.h"
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
}  // namespace
}  // namespace fly
