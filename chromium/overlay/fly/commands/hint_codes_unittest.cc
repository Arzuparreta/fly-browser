// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/hint_codes.h"
#include <set>
#include "base/json/json_reader.h"
#include "fly/commands/generated_contract.h"
#include "testing/gtest/include/gtest/gtest.h"
namespace fly {
TEST(HintCodesTest, MatchesRetainedProductAtWidthBoundaries) {
  const auto cases =
      base::JSONReader::ReadList(kHintCasesJSON, base::JSON_PARSE_RFC);
  ASSERT_TRUE(cases);
  for (const auto& item : *cases) {
    const auto& test = item.GetDict();
    const auto codes = GenerateHintCodes(*test.FindInt("count"));
    ASSERT_EQ(codes.size(), test.FindList("codes")->size());
    for (size_t i = 0; i < codes.size(); ++i)
      EXPECT_EQ(codes[i], (*test.FindList("codes"))[i].GetString());
  }
}
TEST(HintCodesTest, BoundedUniqueAndPrefixFree) {
  EXPECT_TRUE(GenerateHintCodes(0).empty());
  EXPECT_TRUE(GenerateHintCodes(10001).empty());
  const auto codes = GenerateHintCodes(10000);
  const std::set<std::string> unique(codes.begin(), codes.end());
  EXPECT_EQ(10000u, unique.size());
  ASSERT_FALSE(codes.empty());
  for (const auto& code : codes)
    EXPECT_EQ(codes[0].size(), code.size());
}
}  // namespace fly
