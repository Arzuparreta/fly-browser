// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_HINT_CODES_H_
#define FLY_COMMANDS_HINT_CODES_H_
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>
namespace fly {
inline constexpr size_t kMaxHintTargets = 10000;
// The letters of link hint labels, easiest to reach first.
inline constexpr std::string_view kHintAlphabet = "qweasdzxcrfv";
// Prefix-free labels, as short as Vimium's: "q", "w", … for up to 12
// targets; past that, the hardest letters become prefixes of two-letter
// labels one at a time, so 13 targets get "q" … "f" plus "vf" and "vv". Labels
// are ordered shortest first, then by the alphabet's order of ease. The UI
// bounds the number of visible targets before calling this.
std::vector<std::string> GenerateHintCodes(size_t count);
}  // namespace fly
#endif  // FLY_COMMANDS_HINT_CODES_H_
