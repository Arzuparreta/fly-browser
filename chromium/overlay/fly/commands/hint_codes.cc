// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
// Algorithm adapted from Vimium, Copyright (c) 2010 Phil Crosby, Ilya Sukhar.
// See fly/resources/licenses/vimium-MIT.txt for the MIT license.
#include "fly/commands/hint_codes.h"

#include <algorithm>
#include <deque>
#include <utility>

namespace fly {
namespace {
size_t Rank(char letter) {
  return kHintAlphabet.find(letter);
}
}  // namespace

std::vector<std::string> GenerateHintCodes(size_t count) {
  if (!count || count > kMaxHintTargets)
    return {};
  if (count <= kHintAlphabet.size()) {
    std::vector<std::string> result;
    for (const char letter : kHintAlphabet.substr(0, count))
      result.emplace_back(1, letter);
    return result;
  }
  // Vimium's scheme (link_hints.js, hintStrings): replace the oldest, so
  // shortest, label by its one-letter extensions until there are enough. A
  // replaced label never stays, so no label is a prefix of another.
  std::deque<std::string> codes{std::string()};
  while (codes.size() < count || codes.front().empty()) {
    const std::string code = std::move(codes.front());
    codes.pop_front();
    // Hardest letters first, so the easiest stay single letters longest.
    for (size_t i = kHintAlphabet.size(); i > 0; --i)
      codes.push_back(code + kHintAlphabet[i - 1]);
  }
  std::vector<std::string> result(codes.begin(), codes.begin() + count);
  std::ranges::sort(result, [](const std::string& a, const std::string& b) {
    if (a.size() != b.size())
      return a.size() < b.size();
    return std::ranges::lexicographical_compare(a, b, {}, Rank, Rank);
  });
  return result;
}
}  // namespace fly
