// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/palette_catalog.h"
#include <set>
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
TEST(PaletteCatalogTest, PreservesExplicitAliasesAndEveryAction) {
  const auto catalog = PaletteCatalog(DefaultKeyboardConfig());
  std::set<std::string> ids;
  std::set<std::string> actions;
  for (const auto& entry : catalog) {
    EXPECT_TRUE(ids.insert(entry.id).second);
    EXPECT_TRUE(IsKnownAction(entry.action));
    actions.insert(entry.action);
    if (entry.id == "toggle-split-vertical")
      EXPECT_EQ("toggle-split", entry.action);
    if (entry.id == "toggle-sidebar-pin")
      EXPECT_EQ("toggle-zen-mode", entry.action);
  }
  // An alias may target another action, but every action still has a route.
  EXPECT_EQ(99u, actions.size());
  EXPECT_EQ("spawn-tab", catalog.front().id);
}
TEST(PaletteCatalogTest, UsesChosenBindingsAndUnicodeMultiwordSearch) {
  auto config = DefaultKeyboardConfig();
  config.shortcuts["spawn-tab"] = {"Ctrl+Ñ"};
  config.shortcuts["reload-active-tab"] = {};
  const auto catalog = PaletteCatalog(config);
  const auto tabs = FilterPalette(catalog, u"spawn tab");
  ASSERT_EQ(1u, tabs.size());
  EXPECT_EQ(std::vector<std::string>({"Ctrl+Ñ"}), tabs[0].shortcuts);
  EXPECT_EQ(std::vector<std::string>({"T"}), tabs[0].normal);
  for (const auto& entry : catalog)
    if (entry.action == "reload-active-tab")
      EXPECT_TRUE(entry.shortcuts.empty());
  const std::vector<PaletteEntry> unicode = {
      {"example", "spawn-tab", "ÁRBOL Straße", "Test", {}, {}}};
  EXPECT_EQ(1u, FilterPalette(unicode, u"strasse árbol").size());
  EXPECT_TRUE(FilterPalette(catalog, u"no-such-action").empty());
  EXPECT_EQ(catalog.size(), FilterPalette(catalog, u" \t").size());
}
TEST(PaletteCatalogTest, CommandCentreRanksTitlesFirstAndNeedsEveryWord) {
  const auto catalog = CommandCentreCatalog(DefaultKeyboardConfig());
  // A word in the title comes before the same word in a description or a
  // keyword, whatever the catalogue's order.
  const auto split = FilterCommandCentre(catalog, u"split");
  ASSERT_GE(split.size(), 6u);
  for (size_t i = 0; i < 6; ++i)
    EXPECT_EQ("Split view", split[i].category) << split[i].title;
  const auto reload = FilterCommandCentre(catalog, u"reload");
  ASSERT_FALSE(reload.empty());
  EXPECT_EQ("reload-active-tab", reload.front().action);
  // Words match separately and in any order; all of them are required.
  const auto closed = FilterCommandCentre(catalog, u"tab closed");
  ASSERT_FALSE(closed.empty());
  EXPECT_EQ("reopen-closed-tab", closed.front().action);
  EXPECT_TRUE(FilterCommandCentre(catalog, u"split nonsense").empty());
  // Keywords and bindings find commands too.
  const auto keyword = FilterCommandCentre(catalog, u"incognito");
  ASSERT_EQ(1u, keyword.size());
  EXPECT_EQ("toggle-private-mode", keyword.front().action);
  const auto binding = FilterCommandCentre(catalog, u"ctrl+shift+b");
  ASSERT_EQ(1u, binding.size());
  EXPECT_EQ("toggle-bookmarks", binding.front().action);
  EXPECT_EQ(catalog.size(), FilterCommandCentre(catalog, u"  ").size());
  // Every command names its action in plain words and says what it does.
  for (const auto& entry : catalog) {
    EXPECT_FALSE(entry.desc.empty()) << entry.id;
    EXPECT_NE(std::ranges::find(CommandCentreCategories(), entry.category),
              CommandCentreCategories().end())
        << entry.id;
  }
}
}  // namespace fly
