// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/keyboard_config.h"
#include "fly/commands/keyboard_router.h"

#include "base/json/json_reader.h"
#include "fly/commands/generated_contract.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
TEST(KeyboardConfigTest, MatchesCurrentJavaScriptGrammarAndDefaults) {
  const auto cases =
      base::JSONReader::ReadDict(kKeyboardCasesJSON, base::JSON_PARSE_RFC);
  ASSERT_TRUE(cases);
  for (const auto& value : *cases->FindList("normalize")) {
    const auto& row = value.GetDict();
    SCOPED_TRACE(value.DebugString());
    EXPECT_EQ(*row.FindString("output"),
              NormalizeStroke(*row.FindString("input")))
        << *row.FindString("input");
  }
  for (const auto& value : *cases->FindList("configs")) {
    const auto& row = value.GetDict();
    SCOPED_TRACE(*row.FindString("name"));
    const auto validated = ValidateKeyboardConfig(*row.FindDict("input"));
    EXPECT_EQ(*row.FindBool("ok"), validated.config.has_value())
        << validated.error;
    if (validated.config) {
      ASSERT_TRUE(row.FindDict("output"));
      EXPECT_EQ(*row.FindDict("output"), validated.config->ToValue());
    }
  }
}

TEST(KeyboardConfigTest, DefaultsAreCopiedAndExplicitDisablesSurvive) {
  auto edited = DefaultKeyboardConfig().ToValue();
  edited.FindDict("shortcuts")->Set("reload-active-tab", base::ListValue());
  auto config = ValidateKeyboardConfig(edited);
  ASSERT_TRUE(config.config);
  EXPECT_TRUE(config.config->shortcuts.at("reload-active-tab").empty());
  EXPECT_FALSE(
      DefaultKeyboardConfig().shortcuts.at("reload-active-tab").empty());
  EXPECT_EQ("Alt", DefaultKeyboardConfig().tabs);
}

class KeyboardRouterTest : public testing::Test {
 protected:
  KeyResult Press(std::string stroke,
                  bool editable = false,
                  bool repeat = false,
                  int milliseconds = 1) {
    now_ += base::Milliseconds(milliseconds);
    return router_.Route({.stroke = std::move(stroke),
                          .time = now_,
                          .repeat = repeat,
                          .editable = editable});
  }
  KeyboardRouter router_{DefaultKeyboardConfig()};
  base::TimeTicks now_ = base::TimeTicks() + base::Seconds(10);
};

TEST_F(KeyboardRouterTest, EditablesKeepPageKeysButReceiveBrowserShortcuts) {
  EXPECT_EQ(KeyResultKind::kPass, Press("J", true).kind);
  EXPECT_EQ(KeyResultKind::kPass, Press("T", true).kind);
  EXPECT_EQ("spawn-tab", Press("Ctrl+T", true).action);
  EXPECT_EQ("scroll-down", Press("J").action);
  EXPECT_EQ("tab-9", Press("Alt+9", true).action);
  EXPECT_EQ(KeyResultKind::kPass, Press("Ctrl+1", true).kind);
}

TEST_F(KeyboardRouterTest, PassthroughRetainsOnlyItsEscapeShortcut) {
  EXPECT_EQ("toggle-passthrough", Press("Shift+Escape").action);
  EXPECT_EQ(KeyboardMode::kPassthrough, router_.mode());
  EXPECT_EQ(KeyResultKind::kPass, Press("Ctrl+T").kind);
  EXPECT_EQ(KeyResultKind::kPass, Press("J").kind);
  EXPECT_EQ(KeyResultKind::kPass, Press("Escape").kind);
  EXPECT_EQ(KeyResultKind::kConsume, Press("Shift+Escape", false, true).kind);
  EXPECT_EQ(KeyboardMode::kPassthrough, router_.mode());
  EXPECT_EQ("toggle-passthrough", Press("Shift+Escape").action);
  EXPECT_EQ(KeyboardMode::kNormal, router_.mode());
}

TEST_F(KeyboardRouterTest, ExplicitInsertExitsOnEscape) {
  EXPECT_EQ("enter-insert", Press("I").action);
  EXPECT_EQ(KeyResultKind::kPass, Press("J").kind);
  EXPECT_EQ("spawn-tab", Press("Ctrl+T").action);
  EXPECT_EQ(KeyResultKind::kExitInsert, Press("Escape").kind);
  EXPECT_EQ("scroll-down", Press("J").action);
  EXPECT_EQ(KeyResultKind::kExitInsert, Press("Escape", true).kind);
  EXPECT_EQ(KeyResultKind::kPass, Press("Escape").kind);
}

TEST_F(KeyboardRouterTest, PrefixTimeoutAndFocusReset) {
  EXPECT_EQ(KeyResultKind::kConsume, Press("G").kind);
  EXPECT_EQ("page-top", Press("G", false, false, 699).action);
  Press("G");
  EXPECT_EQ(KeyResultKind::kConsume, Press("G", false, false, 700).kind);
  router_.ClearPending();
  EXPECT_EQ(KeyResultKind::kConsume, Press("G").kind);
  EXPECT_EQ("scroll-down", Press("J").action);
  EXPECT_EQ(KeyResultKind::kConsume, Press("G").kind);
  Press("X", true);
  EXPECT_EQ(KeyResultKind::kConsume, Press("G").kind);
}

TEST_F(KeyboardRouterTest, AutoRepeatConsumesNonRepeatableActions) {
  EXPECT_EQ(KeyResultKind::kConsume, Press("Ctrl+W", false, true).kind);
  EXPECT_EQ("next-tab", Press("Ctrl+Tab", false, true).action);
  EXPECT_EQ("scroll-down", Press("J", false, true).action);
  EXPECT_EQ("zoom-in", Press("Ctrl+Plus", false, true).action);
}

TEST_F(KeyboardRouterTest,
       CompositionRecorderAndOverlaysDoNotDispatchPageActions) {
  for (const auto& context :
       {KeyContext{.stroke = "Ctrl+T", .composing = true},
        KeyContext{.stroke = "Ctrl+T", .recorder_active = true},
        KeyContext{.stroke = "T", .page_keys_allowed = false},
        KeyContext{.stroke = "T", .hints_active = true}}) {
    EXPECT_EQ(KeyResultKind::kPass, router_.Route(context).kind);
  }
  EXPECT_EQ("spawn-tab",
            router_.Route({.stroke = "Ctrl+T", .hints_active = true}).action);
}
TEST_F(KeyboardRouterTest, NativeActionsShareModesAndClearPendingSequences) {
  KeyboardRouter router(DefaultKeyboardConfig());
  const auto now = base::TimeTicks::Now();
  EXPECT_EQ(KeyResultKind::kConsume,
            router.Route({.stroke = "G", .time = now}).kind);
  EXPECT_EQ(KeyResultKind::kAction, router.Invoke("enter-insert").kind);
  EXPECT_EQ(KeyboardMode::kInsert, router.mode());
  EXPECT_EQ(KeyResultKind::kPass,
            router.Route({.stroke = "J", .time = now}).kind);
  EXPECT_EQ(KeyResultKind::kExitInsert,
            router.Route({.stroke = "Escape", .time = now}).kind);
  router.Invoke("toggle-passthrough");
  EXPECT_EQ(KeyboardMode::kPassthrough, router.mode());
  router.Route({.stroke = "Shift+Escape", .time = now});
  EXPECT_EQ(KeyboardMode::kNormal, router.mode());
  EXPECT_EQ(KeyResultKind::kPass, router.Invoke("unknown-action").kind);
  EXPECT_EQ(KeyResultKind::kConsume,
            router.Route({.stroke = "G", .time = now}).kind);
}
}  // namespace
}  // namespace fly
