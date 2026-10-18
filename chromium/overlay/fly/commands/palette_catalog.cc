// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/palette_catalog.h"

#include <set>
#include "base/check.h"
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
      base::JSONReader::ReadList(kPaletteEntriesJSON, base::JSON_PARSE_RFC);
  CHECK(metadata && explicit_entries);
  std::vector<PaletteEntry> result;
  std::set<std::string> ids;
  std::set<std::string> actions;
  auto append = [&](std::string id, std::string action, std::string title) {
    const auto* meta = metadata->FindDict(action);
    CHECK(meta);
    const auto* category = meta->FindString("category");
    const auto bindings = config.shortcuts.find(action);
    const auto normal = config.normal.find(action);
    result.push_back({id, action, title, category ? *category : "",
                      bindings == config.shortcuts.end()
                          ? std::vector<std::string>()
                          : bindings->second,
                      normal == config.normal.end() ? std::vector<std::string>()
                                                    : normal->second});
    actions.insert(std::move(action));
    ids.insert(std::move(id));
  };
  for (const auto& value : *explicit_entries) {
    const auto& entry = value.GetDict();
    append(*entry.FindString("id"), *entry.FindString("actionId"),
           *entry.FindString("title"));
  }
  for (const auto [id, value] : *metadata) {
    if (!actions.contains(id))
      append(ids.contains(id) ? "action:" + id : id, id,
             *value.GetDict().FindString("label"));
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
}  // namespace fly
