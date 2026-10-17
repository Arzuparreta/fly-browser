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
  EXPECT_EQ(82u, actions.size());
  EXPECT_EQ("spawn-tab", catalog.front().id);
}
TEST(PaletteCatalogTest, UsesChosenBindingsAndUnicodeMultiwordSearch) {
  auto config = DefaultKeyboardConfig();
  config.shortcuts["spawn-tab"] = {"Ctrl+Ñ"};
  config.shortcuts["reload-active-tab"] = {};
  const auto catalog = PaletteCatalog(config);
  const auto tabs = FilterPalette(catalog, u"new blank");
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
}  // namespace fly
