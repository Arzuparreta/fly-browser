// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/hint_codes.h"
#include <set>
#include <string>
#include <vector>
#include "testing/gtest/include/gtest/gtest.h"
namespace fly {
TEST(HintCodesTest, LabelsGrowOneLetterAtEachPowerOfTheAlphabet) {
  ASSERT_EQ(12u, kHintAlphabet.size());
  EXPECT_EQ(std::vector<std::string>{"q"}, GenerateHintCodes(1));
  const auto single = GenerateHintCodes(12);
  ASSERT_EQ(12u, single.size());
  for (size_t i = 0; i < single.size(); ++i)
    EXPECT_EQ(std::string(1, kHintAlphabet[i]), single[i]);
  const struct {
    size_t count;
    size_t length;
    std::vector<std::string> first;
    std::vector<std::string> last;
  } cases[] = {
      {13, 2, {"qq", "qw", "qe"}, {"qv", "wq"}},
      {144, 2, {"qq", "qw", "qe"}, {"vf", "vv"}},
      {145, 3, {"qqq", "qqw", "qqe"}, {"qvv", "wqq"}},
      {500, 3, {"qqq", "qqw", "qqe"}, {"adz", "adx"}},
  };
  for (const auto& test : cases) {
    SCOPED_TRACE(test.count);
    const auto codes = GenerateHintCodes(test.count);
    ASSERT_EQ(test.count, codes.size());
    EXPECT_EQ(test.length, codes.front().size());
    EXPECT_EQ(test.first, std::vector<std::string>(codes.begin(),
                                                   codes.begin() + 3));
    EXPECT_EQ(test.last,
              std::vector<std::string>(codes.end() - 2, codes.end()));
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
