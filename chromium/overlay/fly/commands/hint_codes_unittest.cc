// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/hint_codes.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
std::map<size_t, size_t> Lengths(const std::vector<std::string>& codes) {
  std::map<size_t, size_t> lengths;
  for (const auto& code : codes)
    ++lengths[code.size()];
  return lengths;
}
}  // namespace

TEST(HintCodesTest, SingleLettersInTheAlphabetsOrder) {
  ASSERT_EQ(12u, kHintAlphabet.size());
  EXPECT_EQ(std::vector<std::string>{"q"}, GenerateHintCodes(1));
  for (size_t count = 1; count <= kHintAlphabet.size(); ++count) {
    const auto single = GenerateHintCodes(count);
    ASSERT_EQ(count, single.size());
    for (size_t i = 0; i < single.size(); ++i)
      EXPECT_EQ(std::string(1, kHintAlphabet[i]), single[i]);
  }
}

TEST(HintCodesTest, LabelsStayAsShortAsVimiums) {
  // One more target than letters turns only the hardest letter into a prefix.
  EXPECT_EQ((std::vector<std::string>{"q", "w", "e", "a", "s", "d", "z", "x",
                                      "c", "r", "f", "vf", "vv"}),
            GenerateHintCodes(13));
  EXPECT_EQ((std::map<size_t, size_t>{{1, 7}, {2, 59}}),
            Lengths(GenerateHintCodes(66)));
  const auto doubles = GenerateHintCodes(144);
  EXPECT_EQ((std::map<size_t, size_t>{{2, 144}}), Lengths(doubles));
  EXPECT_EQ((std::vector<std::string>{"qq", "qw", "qe"}),
            std::vector<std::string>(doubles.begin(), doubles.begin() + 3));
  EXPECT_EQ((std::vector<std::string>{"vf", "vv"}),
            std::vector<std::string>(doubles.end() - 2, doubles.end()));
  EXPECT_EQ((std::map<size_t, size_t>{{2, 143}, {3, 2}}),
            Lengths(GenerateHintCodes(145)));
}

TEST(HintCodesTest, BoundedUniqueAndPrefixFree) {
  EXPECT_TRUE(GenerateHintCodes(0).empty());
  EXPECT_TRUE(GenerateHintCodes(10001).empty());
  for (size_t count : {13u, 100u, 145u, 500u, 10000u}) {
    SCOPED_TRACE(count);
    const auto codes = GenerateHintCodes(count);
    ASSERT_EQ(count, codes.size());
    const std::set<std::string> unique(codes.begin(), codes.end());
    EXPECT_EQ(count, unique.size());
    // Sorted, so a prefix would sit right before a label it starts.
    const std::set<std::string> sorted(codes.begin(), codes.end());
    for (auto it = sorted.begin(); std::next(it) != sorted.end(); ++it)
      EXPECT_FALSE(std::next(it)->starts_with(*it)) << *it;
  }
}
}  // namespace fly
