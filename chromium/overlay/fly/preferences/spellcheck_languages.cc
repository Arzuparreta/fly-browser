// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/spellcheck_languages.h"

#include <algorithm>
#include <array>
#include <utility>

namespace fly {
namespace {
// The dictionaries of Chromium's spell checker
// (components/spellcheck/common/spellcheck_common.cc).
constexpr auto kLanguages = std::to_array<SpellcheckLanguage>({
    {"af", "af-ZA", u"Afrikaans"},
    {"bg", "bg-BG", u"Български"},
    {"ca", "ca-ES", u"Català"},
    {"cs", "cs-CZ", u"Čeština"},
    {"cy", "cy-GB", u"Cymraeg"},
    {"da", "da-DK", u"Dansk"},
    {"de", "de-DE", u"Deutsch"},
    {"el", "el-GR", u"Ελληνικά"},
    {"en-AU", "en-AU", u"English (Australia)"},
    {"en-CA", "en-CA", u"English (Canada)"},
    {"en-GB", "en-GB", u"English (UK)"},
    {"en-GB-oxendict", "en-GB-oxendict", u"English (UK, Oxford spelling)"},
    {"en-US", "en-US", u"English (US)"},
    {"es", "es-ES", u"Español"},
    {"et", "et-EE", u"Eesti"},
    {"fa", "fa-IR", u"فارسی"},
    {"fo", "fo-FO", u"Føroyskt"},
    {"fr", "fr-FR", u"Français"},
    {"gl", "gl", u"Galego"},
    {"he", "he-IL", u"עברית"},
    {"hi", "hi-IN", u"हिन्दी"},
    {"hr", "hr-HR", u"Hrvatski"},
    {"hu", "hu-HU", u"Magyar"},
    {"hy", "hy", u"Հայերեն"},
    {"id", "id-ID", u"Bahasa Indonesia"},
    {"it", "it-IT", u"Italiano"},
    {"ko", "ko", u"한국어"},
    {"lt", "lt-LT", u"Lietuvių"},
    {"lv", "lv-LV", u"Latviešu"},
    {"nb", "nb-NO", u"Norsk bokmål"},
    {"nl", "nl-NL", u"Nederlands"},
    {"pl", "pl-PL", u"Polski"},
    {"pt-BR", "pt-BR", u"Português (Brasil)"},
    {"pt-PT", "pt-PT", u"Português (Portugal)"},
    {"ro", "ro-RO", u"Română"},
    {"ru", "ru-RU", u"Русский"},
    {"sh", "sh", u"Srpskohrvatski"},
    {"sk", "sk-SK", u"Slovenčina"},
    {"sl", "sl-SI", u"Slovenščina"},
    {"sq", "sq", u"Shqip"},
    {"sr", "sr", u"Српски"},
    {"sv", "sv-SE", u"Svenska"},
    {"ta", "ta-IN", u"தமிழ்"},
    {"tg", "tg-TG", u"Тоҷикӣ"},
    {"tr", "tr-TR", u"Türkçe"},
    {"uk", "uk-UA", u"Українська"},
    {"vi", "vi-VN", u"Tiếng Việt"},
});
// Chromium's names for a dictionary that are neither its code nor its file's
// language. "pt" was Fly's Portuguese before Brazil and Portugal were apart.
constexpr auto kAliases = std::to_array<std::pair<std::string_view,
                                                  std::string_view>>({
    {"en", "en-US"},
    {"es-419", "es"},
    {"es-AR", "es"},
    {"es-MX", "es"},
    {"es-US", "es"},
    {"pt", "pt-BR"},
});
}  // namespace

base::span<const SpellcheckLanguage> SpellcheckLanguages() {
  return kLanguages;
}

const SpellcheckLanguage* FindSpellcheckLanguage(std::string_view code) {
  for (const auto& [alias, target] : kAliases)
    if (code == alias)
      code = target;
  const auto found = std::ranges::find_if(
      kLanguages, [code](const SpellcheckLanguage& language) {
        return language.code == code || language.dictionary == code;
      });
  return found == kLanguages.end() ? nullptr : &*found;
}

std::vector<std::string> NormalizeSpellcheckLanguages(
    const std::vector<std::string>& codes) {
  std::vector<std::string> result;
  for (const auto& code : codes) {
    const auto* language = FindSpellcheckLanguage(code);
    if (!language || std::ranges::contains(result, language->code))
      continue;
    result.emplace_back(language->code);
    if (result.size() == kMaxSpellcheckLanguages)
      break;
  }
  return result;
}
}  // namespace fly
