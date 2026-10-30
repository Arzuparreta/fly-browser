// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/spellcheck_languages.h"

#include <set>
#include <string>
#include <utility>
#include <vector>

#include "base/files/file_path.h"
#include "components/spellcheck/common/spellcheck_common.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace fly {
namespace {
TEST(SpellcheckLanguagesTest, EveryChromiumDictionaryOnceUnderItsOwnCode) {
  std::set<std::string> files;
  std::string previous;
  for (const auto& language : SpellcheckLanguages()) {
    const std::string code(language.code);
    EXPECT_LT(previous, code) << "sorted by code";
    previous = code;
    EXPECT_FALSE(language.name.empty()) << code;
    // Chromium knows the code as one of its spell check languages and gives
    // it the dictionary file of `dictionary`.
    EXPECT_EQ(code, spellcheck::GetCorrespondingSpellCheckLanguage(code));
    const auto file =
        spellcheck::GetVersionedFileName(code, base::FilePath()).AsUTF8Unsafe();
    EXPECT_TRUE(file.starts_with(std::string(language.dictionary) + "-"))
        << code << " " << file;
    EXPECT_TRUE(files.insert(file).second) << "two entries for " << file;
    EXPECT_EQ(&language, FindSpellcheckLanguage(code));
    EXPECT_EQ(&language, FindSpellcheckLanguage(language.dictionary));
  }
  // No dictionary Chromium can download is missing.
  for (const auto& chromium : spellcheck::SpellCheckLanguages()) {
    const auto* language = FindSpellcheckLanguage(chromium);
    ASSERT_TRUE(language) << chromium;
    EXPECT_EQ(spellcheck::GetVersionedFileName(chromium, base::FilePath()),
              spellcheck::GetVersionedFileName(language->code,
                                               base::FilePath()))
        << chromium;
  }
}
TEST(SpellcheckLanguagesTest, OtherNamesFindTheirDictionary) {
  for (const auto& [name, code] :
       std::vector<std::pair<std::string, std::string>>{{"es-ES", "es"},
                                                        {"es-MX", "es"},
                                                        {"es-419", "es"},
                                                        {"de-DE", "de"},
                                                        {"pt", "pt-BR"},
                                                        {"en", "en-US"},
                                                        {"sv-SE", "sv"}}) {
    const auto* language = FindSpellcheckLanguage(name);
    ASSERT_TRUE(language) << name;
    EXPECT_EQ(code, language->code);
  }
  EXPECT_FALSE(FindSpellcheckLanguage(""));
  EXPECT_FALSE(FindSpellcheckLanguage("xx"));
  EXPECT_FALSE(FindSpellcheckLanguage("th"));
}
TEST(SpellcheckLanguagesTest, NormalizeKeepsOrderAndTheLimit) {
  EXPECT_EQ((std::vector<std::string>{"es", "en-GB", "pt-BR"}),
            NormalizeSpellcheckLanguages(
                {"es-ES", "xx", "en-GB", "es", "pt", "pt-BR"}));
  EXPECT_EQ((std::vector<std::string>{"en-GB", "es", "fr", "de", "it"}),
            NormalizeSpellcheckLanguages(
                {"en-GB", "es", "fr", "de", "it", "pt-BR", "nl"}));
  EXPECT_EQ(kMaxSpellcheckLanguages, 5u);
  EXPECT_TRUE(NormalizeSpellcheckLanguages({}).empty());
}
}  // namespace
}  // namespace fly
