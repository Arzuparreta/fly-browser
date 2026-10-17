// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_HINT_CODES_H_
#define FLY_COMMANDS_HINT_CODES_H_
#include <cstddef>
#include <string>
#include <vector>
namespace fly {
inline constexpr size_t kMaxHintTargets = 10000;
// Fixed-width, prefix-free labels, using the retained product's generated
// alphabet. The UI bounds the number of visible targets before calling this.
std::vector<std::string> GenerateHintCodes(size_t count);
}  // namespace fly
#endif  // FLY_COMMANDS_HINT_CODES_H_
