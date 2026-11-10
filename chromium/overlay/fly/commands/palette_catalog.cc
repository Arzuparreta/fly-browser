// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/palette_catalog.h"

#include <algorithm>
#include <set>
#include <utility>
#include "base/check.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "fly/commands/generated_catalog.h"
#include "third_party/icu/source/common/unicode/unistr.h"

namespace fly {
namespace {
std::string Fold(std::string_view input) {
  std::string result;
  icu::UnicodeString::fromUTF8(input).foldCase().toUTF8String(result);
  return result;
}
}  // namespace
std::vector<PaletteEntry> PaletteCatalog(const KeyboardConfig& config) {
  auto metadata =
      base::JSONReader::ReadDict(kActionMetadataJSON, base::JSON_PARSE_RFC);
  auto explicit_entries =
      base::JSONReader::ReadList(kCommandDefinitionsJSON, base::JSON_PARSE_RFC);
  CHECK(metadata && explicit_entries);
  std::vector<PaletteEntry> result;
  std::set<std::string> ids;
  std::set<std::string> actions;
  auto append = [&](std::string id, std::string action, std::string title,
                    const base::DictValue* texts) {
    const auto* meta = metadata->FindDict(action);
    CHECK(meta);
    const auto* category = texts ? texts->FindString("category")
                                 : meta->FindString("category");
    const auto bindings = config.shortcuts.find(action);
    const auto normal = config.normal.find(action);
    result.push_back({id, action, title, category ? *category : "",
                      bindings == config.shortcuts.end()
                          ? std::vector<std::string>()
                          : bindings->second,
                      normal == config.normal.end() ? std::vector<std::string>()
                                                    : normal->second});
    auto& added = result.back();
    if (texts) {
      if (const auto* desc = texts->FindString("desc"))
        added.desc = *desc;
      if (const auto* keywords = texts->FindList("keywords"))
        for (const auto& keyword : *keywords)
          if (keyword.is_string())
            added.keywords.push_back(keyword.GetString());
    }
    actions.insert(std::move(action));
    ids.insert(std::move(id));
  };
  for (const auto& value : *explicit_entries) {
    const auto& entry = value.GetDict();
    append(*entry.FindString("id"), *entry.FindString("actionId"),
           *entry.FindString("title"), &entry);
  }
  for (const auto [id, value] : *metadata) {
    if (!actions.contains(id))
      append(ids.contains(id) ? "action:" + id : id, id,
             *value.GetDict().FindString("label"), nullptr);
  }
  return result;
}
std::vector<PaletteEntry> FilterPalette(
    const std::vector<PaletteEntry>& entries,
    std::u16string_view query) {
  const auto tokens =
      base::SplitString(Fold(base::UTF16ToUTF8(query)), " \t\r\n",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  std::vector<PaletteEntry> result;
  for (const auto& entry : entries) {
    const auto haystack = Fold(entry.title + " " + entry.category + " " +
                               entry.id + " " + entry.action);
    bool matches = true;
    for (const auto& token : tokens)
      matches &= haystack.find(token) != std::string::npos;
    if (matches)
      result.push_back(entry);
  }
  return result;
}
std::vector<PaletteEntry> CommandCentreCatalog(const KeyboardConfig& config) {
  auto entries = PaletteCatalog(config);
  const auto definitions =
      base::JSONReader::ReadList(kCommandDefinitionsJSON, base::JSON_PARSE_RFC);
  CHECK(definitions);
  entries.resize(std::min(entries.size(), definitions->size()));
  return entries;
}
std::vector<PaletteEntry> FilterCommandCentre(
    const std::vector<PaletteEntry>& entries,
    std::u16string_view query) {
  const auto words =
      base::SplitString(Fold(base::UTF16ToUTF8(query)), " \t\r\n",
                        base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  if (words.empty())
    return entries;
  // Every word of the search must be found. Where it is found ranks the
  // command: at the start of its title, at the start of a word of the title,
  // elsewhere in the title, in a keyword or a binding, and last in its
  // description or category. A command ranks as its worst word does, and
  // commands of the same rank keep the catalogue's order.
  constexpr int kNoMatch = 5;
  const auto rank = [](const PaletteEntry& entry, const std::string& word) {
    const auto title = Fold(entry.title);
    const auto at = title.find(word);
    if (at == 0)
      return 0;
    if (title.find(" " + word) != std::string::npos)
      return 1;
    if (at != std::string::npos)
      return 2;
    for (const auto* list : {&entry.keywords, &entry.shortcuts, &entry.normal})
      for (const auto& item : *list)
        if (Fold(item).find(word) != std::string::npos)
          return 3;
    for (const auto* text : {&entry.desc, &entry.details, &entry.category})
      if (Fold(*text).find(word) != std::string::npos)
        return 4;
    return kNoMatch;
  };
  std::vector<std::pair<int, const PaletteEntry*>> ranked;
  for (const auto& entry : entries) {
    int worst = 0;
    for (const auto& word : words)
      worst = std::max(worst, rank(entry, word));
    if (worst < kNoMatch)
      ranked.emplace_back(worst, &entry);
  }
  std::ranges::stable_sort(ranked, {}, &std::pair<int, const PaletteEntry*>::first);
  std::vector<PaletteEntry> result;
  for (const auto& [score, entry] : ranked)
    result.push_back(*entry);
  return result;
}
const std::vector<std::string>& CommandCentreCategories() {
  static const base::NoDestructor<std::vector<std::string>> categories(
      std::vector<std::string>{"All", "Tabs", "Page", "Workspaces",
                               "Split view", "Tools", "Extensions", "Fly"});
  return *categories;
}
}  // namespace fly
