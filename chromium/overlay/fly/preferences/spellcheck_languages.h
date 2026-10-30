// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFERENCES_SPELLCHECK_LANGUAGES_H_
#define FLY_PREFERENCES_SPELLCHECK_LANGUAGES_H_

#include <stddef.h>

#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"

namespace fly {
// A spell checking language: one Hunspell dictionary that Chromium downloads,
// named in its own language.
struct SpellcheckLanguage {
  // Stored by Fly and given to Chromium (spellcheck.dictionaries).
  std::string_view code;
  // The dictionary file's language, another name Chromium accepts.
  std::string_view dictionary;
  std::u16string_view name;
};

// Every language, sorted by code.
base::span<const SpellcheckLanguage> SpellcheckLanguages();

// The most languages checked at once. A word is correct when any of them knows
// it; each one more slows the suggestions on right-click, takes memory in every
// page process that checks text and lets more typos pass as words of another
// language. The context menu shows five suggestions, one per language at five.
// docs/guide/spell-checking.md has the measurements.
inline constexpr size_t kMaxSpellcheckLanguages = 5;

// The language of `code`, also by Chromium's other names for its dictionary
// ("es-ES", "es-MX", "pt"), or null.
const SpellcheckLanguage* FindSpellcheckLanguage(std::string_view code);

// The known languages of `codes`, by their own code, in order, without
// repeats and at most kMaxSpellcheckLanguages.
std::vector<std::string> NormalizeSpellcheckLanguages(
    const std::vector<std::string>& codes);
}  // namespace fly

#endif  // FLY_PREFERENCES_SPELLCHECK_LANGUAGES_H_
