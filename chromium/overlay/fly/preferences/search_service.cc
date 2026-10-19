// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/search_service.h"

#include <algorithm>
#include "base/strings/escape.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "build/build_config.h"
#include "components/prefs/pref_service.h"
#include "components/url_formatter/url_fixer.h"
#include "fly/preferences/preferences_service.h"
#include "url/url_file.h"

namespace fly {
namespace {
constexpr char kDefaultEngine[] = "https://duckduckgo.com/?q=%s";
bool IsFilePath(std::string_view value) {
#if BUILDFLAG(IS_WIN)
  return url::DoesBeginWindowsDriveSpec(value, 0) ||
         url::DoesBeginUncPath(value, 0, true);
#else
  return value.starts_with('/') || value.starts_with("~/");
#endif
}
std::string Trim(std::string_view value) {
  return std::string(base::TrimWhitespaceASCII(value, base::TRIM_ALL));
}
std::string AliasKey(std::string_view key) {
  return base::ToLowerASCII(Trim(key));
}
bool ValidKey(std::string_view key) {
  return !key.empty() && key.size() <= 64 &&
         std::ranges::all_of(key, [](char c) {
           return base::IsAsciiAlphaNumeric(c) || c == '_' || c == '-';
         });
}
bool ValidText(std::string_view text, size_t limit) {
  return text.size() <= limit && text.find('\0') == std::string_view::npos &&
         base::IsStringUTF8(text);
}
std::string Expand(std::string_view pattern,
                   std::string_view query,
                   bool append_without_placeholder) {
  std::string result(pattern);
  const auto encoded = base::EscapeQueryParamValue(query, false);
  const auto position = result.find("%s");
  if (position != std::string::npos)
    result.replace(position, 2, encoded);
  else if (append_without_placeholder)
    result += encoded;
  return result;
}
bool ValidTemplate(std::string_view pattern) {
  if (!ValidText(pattern, 8192))
    return false;
  const GURL url(Expand(pattern, "query", false));
  return url.is_valid() && url.SchemeIsHTTPOrHTTPS() && !url.has_username() &&
         !url.has_password();
}
std::map<std::string, SearchAlias> Defaults() {
  return {
      {"g", {"Google", "https://www.google.com/search?q=%s"}},
      {"ddg", {"DuckDuckGo", kDefaultEngine}},
      {"yt", {"YouTube", "https://www.youtube.com/results?search_query=%s"}},
      {"gh", {"GitHub", "https://github.com/search?q=%s"}},
      {"a", {"ArchWiki", "https://wiki.archlinux.org/index.php?search=%s"}},
      {"aur",
       {"Arch User Repository", "https://aur.archlinux.org/packages?K=%s"}},
      {"pkg", {"Arch Packages", "https://archlinux.org/packages/?q=%s"}},
      {"so", {"Stack Overflow", "https://stackoverflow.com/search?q=%s"}},
      {"r", {"Reddit", "https://www.reddit.com/search/?q=%s"}},
      {"sp", {"Startpage", "https://www.startpage.com/sp/search?query=%s"}},
      {"b", {"Brave Search", "https://search.brave.com/search?q=%s"}},
      {"k", {"Kagi", "https://kagi.com/search?q=%s"}},
  };
}
std::optional<GURL> CheckedURL(std::string_view value) {
  const GURL url(value);
  return url.is_valid() ? std::make_optional(url) : std::nullopt;
}
}  // namespace
SearchService::SearchService(PrefService& local_state)
    : local_state_(local_state) {}
SearchService::~SearchService() = default;
std::optional<SearchPreferences> SearchService::Read() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!PreferencesService(*local_state_).ReadTheme())
    return std::nullopt;
  SearchPreferences result{kDefaultEngine, Defaults(), {}};
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  if (!values)
    return result;
  if (const auto* engine = values->Find("search_engine")) {
    if (!engine->is_string() || !ValidTemplate(engine->GetString()))
      return std::nullopt;
    result.engine = engine->GetString();
  }
  if (const auto* aliases = values->Find("custom_search_aliases")) {
    if (!aliases->is_dict() || aliases->GetDict().size() > 256)
      return std::nullopt;
    for (const auto [stored_key, value] : aliases->GetDict()) {
      const auto key = AliasKey(stored_key);
      if (!ValidKey(key) || !result.custom_aliases.insert(key).second)
        return std::nullopt;
      SearchAlias alias{base::ToUpperASCII(key), {}};
      if (value.is_string()) {
        alias.url = value.GetString();
      } else if (value.is_dict()) {
        const auto* url = value.GetDict().FindString("url");
        if (!url)
          return std::nullopt;
        alias.url = *url;
        if (const auto* name = value.GetDict().Find("name")) {
          if (!name->is_string() || !ValidText(name->GetString(), 256))
            return std::nullopt;
          alias.name = name->GetString();
        }
      } else {
        return std::nullopt;
      }
      if (!ValidTemplate(alias.url))
        return std::nullopt;
      result.aliases.insert_or_assign(key, std::move(alias));
    }
  }
  return result;
}
bool SearchService::CanEdit() const {
  return Read().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool SearchService::SetEngine(std::string_view url_template) {
  if (!CanEdit() || !ValidTemplate(url_template))
    return false;
  auto store = local_state_->GetDict(kPreferencesPref).Clone();
  store.Set("schema", 1);
  store.EnsureDict("values")->Set("search_engine", std::string(url_template));
  local_state_->SetDict(kPreferencesPref, std::move(store));
  return true;
}
bool SearchService::SetAlias(std::string_view input,
                             std::string_view name,
                             std::string_view url_template) {
  const auto key = AliasKey(input);
  if (!CanEdit() || !ValidKey(key) || !ValidText(name, 256) ||
      !ValidTemplate(url_template))
    return false;
  auto store = local_state_->GetDict(kPreferencesPref).Clone();
  store.Set("schema", 1);
  auto* aliases =
      store.EnsureDict("values")->EnsureDict("custom_search_aliases");
  std::string stored_key = key;
  base::DictValue row;
  for (const auto [existing_key, value] : *aliases) {
    if (AliasKey(existing_key) == key) {
      stored_key = existing_key;
      if (value.is_dict())
        row = value.GetDict().Clone();
      break;
    }
  }
  if (!aliases->contains(stored_key) && aliases->size() >= 256)
    return false;
  row.Set("name", name.empty() ? base::ToUpperASCII(key) : std::string(name));
  row.Set("url", std::string(url_template));
  aliases->Set(stored_key, std::move(row));
  local_state_->SetDict(kPreferencesPref, std::move(store));
  return true;
}
bool SearchService::RemoveAlias(std::string_view input) {
  if (!CanEdit())
    return false;
  auto store = local_state_->GetDict(kPreferencesPref).Clone();
  auto* values = store.FindDict("values");
  auto* aliases = values ? values->FindDict("custom_search_aliases") : nullptr;
  if (!aliases)
    return false;
  std::string stored_key;
  for (const auto [key, value] : *aliases)
    if (AliasKey(key) == AliasKey(input)) {
      stored_key = key;
      break;
    }
  if (stored_key.empty() || !aliases->Remove(stored_key))
    return false;
  local_state_->SetDict(kPreferencesPref, std::move(store));
  return true;
}
std::optional<GURL> SearchService::ResolveNavigation(
    std::string_view text) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto input = Trim(text);
  if (input.empty() || !ValidText(input, 2 * 1024 * 1024))
    return std::nullopt;
  const auto lower = base::ToLowerASCII(input);
  if (lower.starts_with("ws:") || lower.starts_with("ws "))
    return std::nullopt;
  if (IsFilePath(input) || lower.starts_with("file:") ||
      lower.starts_with("http:") || lower.starts_with("https:") ||
      lower.starts_with("chrome:") || lower.starts_with("chrome-extension:")) {
    const auto fixed = url_formatter::FixupURL(input, {});
    return fixed.is_valid() ? std::make_optional(fixed) : std::nullopt;
  }
  if (lower == "about:blank")
    return GURL("about:blank");
  const auto words =
      base::SplitStringPiece(input, base::kWhitespaceASCII,
                             base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
  for (const auto word : words)
    if (word.size() > 1 && word.front() == '!' &&
        std::ranges::all_of(word.substr(1), [](char c) {
          return base::IsAsciiAlphaNumeric(c);
        }))
      return CheckedURL(Expand(kDefaultEngine, input, false));
  const auto settings = Read();
  if (!settings)
    return std::nullopt;
  const auto separator = input.find_first_of(base::kWhitespaceASCII);
  if (separator != std::string::npos) {
    const auto found =
        settings->aliases.find(AliasKey(input.substr(0, separator)));
    if (found != settings->aliases.end())
      return CheckedURL(
          Expand(found->second.url, Trim(input.substr(separator)), true));
  } else {
    const bool local = lower == "localhost" || lower.starts_with("localhost:");
    // "host.tld:port" is an address; any other "scheme:..." word (mailto:,
    // ftp://, javascript:, ...) is not something this surface opens.
    if (const auto colon = input.find(':');
        !local && colon != std::string::npos &&
        input.substr(0, colon).find('.') == std::string::npos) {
      const auto tail = input.substr(colon + 1);
      size_t digits = 0;
      while (digits < tail.size() && base::IsAsciiDigit(tail[digits]))
        ++digits;
      if (digits == 0 || (digits < tail.size() && tail[digits] != '/'))
        return std::nullopt;
    }
    if (local || input.find('.') != std::string::npos)
      return CheckedURL(std::string(local ? "http://" : "https://") + input);
    if (input.find(':') != std::string::npos)
      return std::nullopt;
  }
  return CheckedURL(Expand(settings->engine, input, false));
}
}  // namespace fly
