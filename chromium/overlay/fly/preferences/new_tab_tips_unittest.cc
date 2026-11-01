// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/new_tab_tips.h"

#include <string>
#include <string_view>

#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
class NewTabTipsTest : public testing::Test {
 protected:
  NewTabTipsTest() { NewTabTips::RegisterLocalStatePrefs(prefs_.registry()); }
  std::string Next(bool (*usable)(std::string_view) = [](std::string_view) {
    return true;
  }) {
    const auto tip = tips_.Next(usable);
    return tip ? std::string(tip->action) : std::string();
  }
  TestingPrefServiceSimple prefs_;
  NewTabTips tips_{prefs_};
};

TEST_F(NewTabTipsTest, ConsecutiveNewTabsRotateWithoutWriting) {
  EXPECT_EQ("trigger-hints", Next());
  EXPECT_EQ("toggle-palette", Next());
  EXPECT_EQ("open-keyboard-settings", Next());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kNewTabTipsPref));
}

TEST_F(NewTabTipsTest, KeyboardUseRetiresATipAfterTwoUses) {
  tips_.RecordKeyboardUse("trigger-hints");
  EXPECT_TRUE(tips_.Pending("trigger-hints"));
  EXPECT_EQ("trigger-hints", Next());
  tips_.RecordKeyboardUse("trigger-hints");
  EXPECT_FALSE(tips_.Pending("trigger-hints"));
  // Learned tips never come back, and further use writes nothing.
  const auto stored = prefs_.GetDict(kNewTabTipsPref).Clone();
  tips_.RecordKeyboardUse("trigger-hints");
  EXPECT_EQ(stored, prefs_.GetDict(kNewTabTipsPref));
  for (int i = 0; i < 20; ++i)
    EXPECT_NE("trigger-hints", Next());
}

TEST_F(NewTabTipsTest, OtherActionsAreIgnored) {
  tips_.RecordKeyboardUse("scroll-down");
  tips_.RecordKeyboardUse("toggle-address");
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kNewTabTipsPref));
  EXPECT_FALSE(tips_.Pending("scroll-down"));
  tips_.Dismiss("scroll-down");
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kNewTabTipsPref));
}

TEST_F(NewTabTipsTest, SkipsTipsWithoutKeys) {
  EXPECT_EQ("toggle-palette", Next([](std::string_view action) {
              return action != "trigger-hints";
            }));
  EXPECT_EQ("", Next([](std::string_view) { return false; }));
}

TEST_F(NewTabTipsTest, DismissAndResetEveryTip) {
  std::string action;
  while (!(action = Next()).empty())
    tips_.Dismiss(action);
  EXPECT_EQ("", Next());
  tips_.Reset();
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kNewTabTipsPref));
  EXPECT_EQ("trigger-hints", Next());
}

TEST_F(NewTabTipsTest, MalformedCountsAreReadAsUnused) {
  prefs_.SetDict(kNewTabTipsPref, base::DictValue()
                                      .Set("trigger-hints", "two")
                                      .Set("toggle-palette", 99));
  EXPECT_TRUE(tips_.Pending("trigger-hints"));
  EXPECT_FALSE(tips_.Pending("toggle-palette"));
  EXPECT_TRUE(NewTabTips::Find("yank-url").has_value());
  EXPECT_FALSE(NewTabTips::Find("go-back").has_value());
}
}  // namespace
}  // namespace fly
