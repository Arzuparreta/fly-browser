// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/palette_catalog.h"

#include <algorithm>
#include <set>
#include "base/check.h"
#include "base/no_destructor.h"
#include "base/strings/string_util.h"
#include "base/json/json_reader.h"
#include "base/strings/string_split.h"
#include "base/strings/utf_string_conversions.h"
#include "fly/commands/generated_contract.h"
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
      for (auto [key, field] :
           {std::pair{"desc", &added.desc}, std::pair{"details", &added.details},
            std::pair{"tip", &added.tip}})
        if (const auto* value = texts->FindString(key))
          *field = *value;
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
  const auto search = Fold(std::string(base::TrimWhitespaceASCII(
      base::UTF16ToUTF8(query), base::TRIM_ALL)));
  if (search.empty())
    return entries;
  std::vector<PaletteEntry> result;
  const auto has = [&](std::string_view text) {
    return Fold(text).find(search) != std::string::npos;
  };
  for (const auto& entry : entries) {
    bool matches = has(entry.title) || has(entry.desc) ||
                   has(entry.details) || has(entry.category);
    for (const auto& list : {&entry.keywords, &entry.shortcuts, &entry.normal})
      for (const auto& item : *list)
        matches = matches || has(item);
    if (matches)
      result.push_back(entry);
  }
  return result;
}
const std::vector<std::string>& CommandCentreCategories() {
  static const base::NoDestructor<std::vector<std::string>> categories(
      std::vector<std::string>{"All", "Tabs & Navigation", "Split View",
                               "Performance & Memory", "Tools",
                               "User Scripts & Styles", "Extensions",
                               "Preferences & System"});
  return *categories;
}
}  // namespace fly
