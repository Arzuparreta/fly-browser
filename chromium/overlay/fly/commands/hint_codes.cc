// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/hint_codes.h"
#include <utility>
#include "fly/commands/generated_contract.h"
namespace fly {
std::vector<std::string> GenerateHintCodes(size_t count) {
  if (!count || count > kMaxHintTargets)
    return {};
  const auto base = kHintAlphabet.size();
  size_t length = 1;
  for (size_t capacity = base; capacity < count; capacity *= base)
    ++length;
  std::vector<std::string> result;
  result.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    std::string code(length, kHintAlphabet[0]);
    size_t number = i;
    for (size_t digit = length; digit > 0; --digit) {
      code[digit - 1] = kHintAlphabet[number % base];
      number /= base;
    }
    result.push_back(std::move(code));
  }
  return result;
}
}  // namespace fly
