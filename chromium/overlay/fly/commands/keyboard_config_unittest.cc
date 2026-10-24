// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/keyboard_config.h"
#include "fly/commands/keyboard_router.h"

#include <initializer_list>
#include <string_view>
#include <utility>

#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
TEST(KeyboardConfigTest, StrokesAreNormalised) {
  const std::pair<std::string_view, std::string_view> cases[] = {
      {"control+shift+a", "Ctrl+Shift+A"},
      {"Meta+Ctrl+return", "Ctrl+Meta+Enter"},
      {"Ctrl++", "Ctrl+Plus"},
      {"Ctrl+Shift+Plus", "Ctrl+Plus"},
      {"Ctrl+A+B", ""},
      {"Ctrl+Ctrl+A", ""},
      {"Ctrl+ñ", "Ctrl+Ñ"},
      {"ñ", "Ñ"},
      {"Shift+ñ", "Ñ"},
      {"Cmd+X", "Meta+X"},
      {"Super+F24", "Meta+F24"},
      {"F25", ""},
      {"F01", ""},
      {" ", ""},
      {"  Alt + PageDown ", "Alt+PageDown"},
      {"Ctrl+\\", "Ctrl+\\"},
      {"Shift+/", "/"},
      {"AltGraph+A", ""},
      {"", ""},
      {"Ctrl+", ""},
      {"Shift+Escape", "Shift+Escape"},
      {"Ctrl+💡", ""},
      {"Shift+ß", "Shift+SS"},
      {"   ", ""},
  };
  for (const auto& [input, output] : cases)
    EXPECT_EQ(output, NormalizeStroke(input)) << input;
}

// The default configuration with one edit applied; `edit` receives the
// shortcuts table, the page-key table and the whole configuration.
template <typename Edit>
base::DictValue Edited(Edit edit) {
  auto config = DefaultKeyboardConfig().ToValue();
  auto shortcuts = config.FindDict("shortcuts")->Clone();
  auto normal = config.FindDict("normal")->Clone();
  edit(shortcuts, normal, config);
  config.Set("shortcuts", std::move(shortcuts));
  config.Set("normal", std::move(normal));
  return config;
}

base::ListValue List(std::initializer_list<std::string_view> items) {
  base::ListValue list;
  for (const auto item : items)
    list.Append(item);
  return list;
}

TEST(KeyboardConfigTest, ValidEditsAreNormalised) {
  EXPECT_EQ(DefaultKeyboardConfig().ToValue(),
            ValidateKeyboardConfig(DefaultKeyboardConfig().ToValue())
                .config->ToValue());
  auto result = ValidateKeyboardConfig(Edited(
      [](auto&, auto&, auto& config) { config.Set("tabs", "shift+ctrl"); }));
  ASSERT_TRUE(result.config) << result.error;
  EXPECT_EQ("Ctrl+Shift", result.config->tabs);
  result = ValidateKeyboardConfig(
      Edited([](auto&, auto&, auto& config) { config.Set("tabs", ""); }));
  ASSERT_TRUE(result.config) << result.error;
  EXPECT_EQ("", result.config->tabs);
  // Equal bindings in another spelling are one binding.
  result = ValidateKeyboardConfig(Edited([](auto& shortcuts, auto&, auto&) {
    shortcuts.Set("spawn-tab", List({"Ctrl+T", "control+t"}));
  }));
  ASSERT_TRUE(result.config) << result.error;
  EXPECT_EQ(DefaultKeyboardConfig(), *result.config);
  // A single binding may be text; layout letters are kept.
  result = ValidateKeyboardConfig(Edited([](auto& shortcuts, auto&, auto&) {
    shortcuts.Set("spawn-tab", "Ctrl+ñ");
  }));
  ASSERT_TRUE(result.config) << result.error;
  EXPECT_EQ(std::vector<std::string>{"Ctrl+Ñ"},
            result.config->shortcuts.at("spawn-tab"));
}

TEST(KeyboardConfigTest, InvalidEditsAreRejected) {
  const struct {
    const char* name;
    base::DictValue config;
  } cases[] = {
      {"shortcut conflict", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("spawn-tab", "Ctrl+W");
       })},
      {"prefix conflict", Edited([](auto&, auto& normal, auto&) {
         normal.Set("scroll-up", "G");
       })},
      {"page key shadowed by a shortcut",
       Edited([](auto&, auto& normal, auto&) {
         normal.Set("spawn-tab", "Ctrl+T");
       })},
      {"shortcut without a modifier", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("spawn-tab", "A");
       })},
      {"reserved Escape", Edited([](auto&, auto& normal, auto&) {
         normal.Set("scroll-down", "Escape");
       })},
      {"reserved Tab", Edited([](auto&, auto& normal, auto&) {
         normal.Set("scroll-down", "Shift+Tab");
       })},
      {"unknown action", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("fake", "Ctrl+A");
       })},
      {"numeric binding", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("spawn-tab", base::ListValue().Append(42));
       })},
      {"tab modifier without Ctrl, Alt or Meta",
       Edited([](auto&, auto&, auto& config) { config.Set("tabs", "Shift"); })},
      {"tab modifier with a key",
       Edited([](auto&, auto&, auto& config) { config.Set("tabs", "Alt+5"); })},
      {"no way out of passthrough", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("toggle-passthrough", base::ListValue());
       })},
      {"page key on a tab-number chord", Edited([](auto&, auto& normal, auto&) {
         normal.Set("scroll-down", List({"Alt+1"}));
       })},
      {"sequence of three keys", Edited([](auto&, auto& normal, auto&) {
         normal.Set("page-top", "G G G");
       })},
      {"more than eight bindings", Edited([](auto& shortcuts, auto&, auto&) {
         shortcuts.Set("spawn-tab",
                       List({"Ctrl+T", "Ctrl+T", "Ctrl+T", "Ctrl+T", "Ctrl+T",
                             "Ctrl+T", "Ctrl+T", "Ctrl+T", "Ctrl+T"}));
       })},
  };
  for (const auto& test : cases)
    EXPECT_FALSE(ValidateKeyboardConfig(test.config).config) << test.name;
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

TEST(KeyboardConfigTest, ChangesWinAndDefaultsGiveUpCollidingChords) {
  // An exported file: only the actions that differ from the defaults.
  auto changes = base::DictValue()
                     .Set("shortcuts", base::DictValue().Set(
                                           "spawn-tab",
                                           base::ListValue().Append("Ctrl+W")))
                     .Set("normal", base::DictValue().Set(
                                        "scroll-down",
                                        base::ListValue().Append("K")));
  auto resolved = ResolveKeyboardChanges(changes);
  ASSERT_TRUE(resolved.config) << resolved.error;
  EXPECT_EQ(std::vector<std::string>{"Ctrl+W"},
            resolved.config->shortcuts.at("spawn-tab"));
  EXPECT_TRUE(resolved.config->shortcuts.at("remove-tab").empty());
  EXPECT_EQ(std::vector<std::string>{"K"},
            resolved.config->normal.at("scroll-down"));
  EXPECT_TRUE(resolved.config->normal.at("scroll-up").empty());
  EXPECT_EQ(DefaultKeyboardConfig().tabs, resolved.config->tabs);
  EXPECT_EQ(DefaultKeyboardConfig().shortcuts.at("toggle-address"),
            resolved.config->shortcuts.at("toggle-address"));

  // A choice that takes a numbered-tab chord turns the default modifier off,
  // unless the file names one.
  changes.Set("shortcuts", base::DictValue().Set(
                               "spawn-tab", base::ListValue().Append("Alt+1")));
  resolved = ResolveKeyboardChanges(changes);
  ASSERT_TRUE(resolved.config) << resolved.error;
  EXPECT_EQ("", resolved.config->tabs);
  changes.Set("tabs", "Ctrl");
  resolved = ResolveKeyboardChanges(changes);
  ASSERT_TRUE(resolved.config) << resolved.error;
  EXPECT_EQ("Ctrl", resolved.config->tabs);
}

TEST(KeyboardConfigTest, ChangesAreComparedInTheirNormalisedSpelling) {
  const auto changes =
      base::DictValue()
          .Set("shortcuts",
               base::DictValue()
                   .Set("spawn-tab", base::ListValue().Append("ctrl+w"))
                   .Set("toggle-address", base::ListValue().Append("alt+1")))
          .Set("normal", base::DictValue().Set(
                             "scroll-down", base::ListValue().Append("g  g")));
  const auto resolved = ResolveKeyboardChanges(changes);
  ASSERT_TRUE(resolved.config) << resolved.error;
  EXPECT_EQ(std::vector<std::string>{"Ctrl+W"},
            resolved.config->shortcuts.at("spawn-tab"));
  EXPECT_TRUE(resolved.config->shortcuts.at("remove-tab").empty());
  EXPECT_EQ("", resolved.config->tabs);
  EXPECT_EQ(std::vector<std::string>{"G G"},
            resolved.config->normal.at("scroll-down"));
  EXPECT_TRUE(resolved.config->normal.at("page-top").empty());
}

TEST(KeyboardConfigTest, APageSequenceStrokeTurnsTheDefaultTabNumbersOff) {
  const auto changes =
      base::DictValue()
          .Set("shortcuts", base::DictValue())
          .Set("normal", base::DictValue().Set(
                             "scroll-down", base::ListValue().Append("G Alt+1")));
  const auto resolved = ResolveKeyboardChanges(changes);
  ASSERT_TRUE(resolved.config) << resolved.error;
  EXPECT_EQ("", resolved.config->tabs);
  EXPECT_EQ(std::vector<std::string>{"G Alt+1"},
            resolved.config->normal.at("scroll-down"));
}

TEST(KeyboardConfigTest, MalformedChangesAreRejected) {
  EXPECT_FALSE(ResolveKeyboardChanges(base::DictValue()).config);
  const auto with = [](base::Value value) {
    return base::DictValue()
        .Set("shortcuts", base::DictValue().Set("spawn-tab", std::move(value)))
        .Set("normal", base::DictValue());
  };
  EXPECT_FALSE(ResolveKeyboardChanges(with(base::Value("Ctrl+T"))).config);
  EXPECT_FALSE(
      ResolveKeyboardChanges(with(base::Value(base::ListValue().Append(4))))
          .config);
  EXPECT_FALSE(ResolveKeyboardChanges(
                   base::DictValue()
                       .Set("shortcuts", base::DictValue().Set(
                                             "no-such-action",
                                             base::ListValue().Append("F2")))
                       .Set("normal", base::DictValue()))
                   .config);
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
