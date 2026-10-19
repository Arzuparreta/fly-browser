// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/command_service.h"

#include "base/task/single_thread_task_executor.h"
#include "components/prefs/testing_pref_service.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace fly {
namespace {
class CommandServiceTest : public testing::Test {
 protected:
  CommandServiceTest() {
    CommandService::RegisterLocalStatePrefs(prefs_.registry());
    service_ = std::make_unique<CommandService>(prefs_);
  }
  base::SingleThreadTaskExecutor executor_;
  TestingPrefServiceSimple prefs_;
  std::unique_ptr<CommandService> service_;
};
TEST_F(CommandServiceTest, ReadDefaultsDoesNotWrite) {
  ASSERT_TRUE(service_->Read());
  EXPECT_EQ(DefaultKeyboardConfig(), *service_->Read());
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kKeyboardPref));
}
TEST_F(CommandServiceTest, DisabledBindingsAndTabChoicesSurviveRestart) {
  auto config = DefaultKeyboardConfig().ToValue();
  config.FindDict("shortcuts")->Set("reload-active-tab", base::ListValue());
  config.Set("tabs", "");
  const auto generation = service_->generation();
  ASSERT_TRUE(service_->Save(config).config);
  EXPECT_GT(service_->generation(), generation);
  const auto& saved = prefs_.GetDict(kKeyboardPref);
  EXPECT_EQ(1u, saved.FindDict("shortcuts")->size());
  EXPECT_TRUE(saved.FindDict("normal")->empty());
  EXPECT_TRUE(saved.FindString("tabs")->empty());
  service_.reset();
  CommandService restarted(prefs_);
  ASSERT_TRUE(restarted.Read());
  EXPECT_EQ(config, restarted.Read()->ToValue());
}
TEST_F(CommandServiceTest, InvalidTransactionsPreserveAllData) {
  auto config = DefaultKeyboardConfig().ToValue();
  ASSERT_TRUE(service_->Save(config).config);
  const auto before = prefs_.GetDict(kKeyboardPref).Clone();
  config.FindDict("shortcuts")
      ->Set("spawn-tab", base::ListValue().Append("Ctrl+W"));
  EXPECT_FALSE(service_->Save(config).config);
  EXPECT_EQ(before, prefs_.GetDict(kKeyboardPref));
}
TEST_F(CommandServiceTest, PartialEditorProjectionCannotSilentlyResetBindings) {
  auto config = DefaultKeyboardConfig().ToValue();
  config.FindDict("shortcuts")->Remove("spawn-tab");
  EXPECT_FALSE(service_->Save(config).config);
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kKeyboardPref));
}

TEST_F(CommandServiceTest, ChosenChordRemovesDefaultOwner) {
  auto stored =
      base::DictValue()
          .Set("schema", 1)
          .Set("shortcuts",
               base::DictValue().Set("focus-sidebar",
                                     base::ListValue().Append("Ctrl+PageDown")))
          .Set("normal", base::DictValue().Set("trigger-hints",
                                               base::ListValue().Append("G")));
  prefs_.SetDict(kKeyboardPref, std::move(stored));
  const auto config = service_->Read();
  ASSERT_TRUE(config);
  EXPECT_EQ(std::vector<std::string>({"Ctrl+Tab"}),
            config->shortcuts.at("next-tab"));
  EXPECT_TRUE(config->normal.at("page-top").empty());
  EXPECT_EQ(std::vector<std::string>({"G"}),
            config->normal.at("trigger-hints"));
}
TEST_F(CommandServiceTest, NewTabDefaultYieldsToExplicitChoice) {
  prefs_.SetDict(
      kKeyboardPref,
      base::DictValue()
          .Set("schema", 1)
          .Set("shortcuts", base::DictValue().Set(
                                "spawn-tab", base::ListValue().Append("Alt+3")))
          .Set("normal", base::DictValue()));
  const auto config = service_->Read();
  ASSERT_TRUE(config);
  EXPECT_TRUE(config->tabs.empty());
  EXPECT_EQ(std::vector<std::string>({"Alt+3"}),
            config->shortcuts.at("spawn-tab"));
}
TEST_F(CommandServiceTest, FutureDataIsPreservedAndCannotBeSilentlySavedOver) {
  auto future = base::DictValue().Set("schema", 88).Set("unknown", "retain");
  prefs_.SetDict(kKeyboardPref, future.Clone());
  EXPECT_FALSE(service_->Read());
  EXPECT_FALSE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
  EXPECT_EQ(future, prefs_.GetDict(kKeyboardPref));
}
TEST_F(CommandServiceTest, ReturningToDefaultsRemovesOldChoices) {
  auto edited = DefaultKeyboardConfig().ToValue();
  edited.Set("tabs", "");
  ASSERT_TRUE(service_->Save(edited).config);
  ASSERT_TRUE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
  const auto& saved = prefs_.GetDict(kKeyboardPref);
  EXPECT_FALSE(saved.contains("tabs"));
  EXPECT_TRUE(saved.FindDict("shortcuts")->empty());
  EXPECT_TRUE(saved.FindDict("normal")->empty());
}
TEST_F(CommandServiceTest, WrongTypedUserValuesNeverBecomeEditableDefaults) {
  for (const auto& invalid : {base::Value("corrupt"), base::Value(false),
                              base::Value(base::ListValue())}) {
    prefs_.SetUserPref(kKeyboardPref, invalid.Clone());
    EXPECT_FALSE(service_->Read());
    EXPECT_FALSE(service_->CanEdit());
    EXPECT_FALSE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
    ASSERT_TRUE(prefs_.GetUserPref(kKeyboardPref));
    EXPECT_EQ(invalid, *prefs_.GetUserPref(kKeyboardPref));
    prefs_.SetRecommendedPref(kKeyboardPref, base::Value(base::DictValue()));
    EXPECT_FALSE(service_->Read());
    EXPECT_FALSE(service_->CanEdit());
    EXPECT_FALSE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
    EXPECT_EQ(invalid, *prefs_.GetUserPref(kKeyboardPref));
    prefs_.RemoveRecommendedPref(kKeyboardPref);
  }
  prefs_.SetDict(kKeyboardPref, base::DictValue());
  EXPECT_TRUE(service_->Read());
  EXPECT_TRUE(service_->CanEdit());
  EXPECT_TRUE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
}
TEST_F(CommandServiceTest, ManagedBindingsAreReadableWithoutCreatingUserWrites) {
  const auto policy = base::DictValue().Set("schema", 1)
      .Set("shortcuts", base::DictValue().Set("spawn-tab", base::ListValue()))
      .Set("normal", base::DictValue());
  prefs_.SetManagedPref(kKeyboardPref, base::Value(policy.Clone()));
  const auto keyboard = service_->Read();
  ASSERT_TRUE(keyboard);
  EXPECT_FALSE(service_->CanEdit());
  EXPECT_TRUE(keyboard->shortcuts.at("spawn-tab").empty());
  EXPECT_FALSE(service_->Save(DefaultKeyboardConfig().ToValue()).config);
  EXPECT_EQ(nullptr, prefs_.GetUserPref(kKeyboardPref));
  EXPECT_EQ(policy, prefs_.GetDict(kKeyboardPref));
}
}  // namespace
}  // namespace fly
