// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/keyboard_config.h"

#include <algorithm>
#include <array>
#include <set>
#include <utility>

#include "base/check.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "fly/commands/generated_contract.h"
#include "third_party/icu/source/common/unicode/locid.h"
#include "third_party/icu/source/common/unicode/unistr.h"

namespace fly {
namespace {
constexpr std::array<std::string_view, 4> kModifiers = {"Ctrl", "Alt", "Shift",
                                                        "Meta"};
constexpr std::pair<std::string_view, std::string_view> kModifierAliases[] = {
    {"control", "Ctrl"}, {"ctrl", "Ctrl"},   {"option", "Alt"},
    {"alt", "Alt"},      {"shift", "Shift"}, {"cmd", "Meta"},
    {"command", "Meta"}, {"super", "Meta"},  {"meta", "Meta"}};
constexpr std::pair<std::string_view, std::string_view> kNamedKeys[] = {
    {"esc", "Escape"},
    {"escape", "Escape"},
    {"return", "Enter"},
    {"enter", "Enter"},
    {"del", "Delete"},
    {"delete", "Delete"},
    {"tab", "Tab"},
    {"space", "Space"},
    {"plus", "Plus"},
    {"arrowleft", "ArrowLeft"},
    {"arrowright", "ArrowRight"},
    {"arrowup", "ArrowUp"},
    {"arrowdown", "ArrowDown"},
    {"pageup", "PageUp"},
    {"pagedown", "PageDown"},
    {"home", "Home"},
    {"end", "End"},
    {"backspace", "Backspace"},
    {"insert", "Insert"},
    {"contextmenu", "ContextMenu"}};

std::string Trim(std::string_view input) {
  std::u16string result;
  base::TrimWhitespace(base::UTF8ToUTF16(input), base::TRIM_ALL, &result);
  return base::UTF16ToUTF8(result);
}

template <size_t N>
std::string Lookup(
    const std::pair<std::string_view, std::string_view> (&table)[N],
    std::string_view value) {
  for (const auto& [key, result] : table) {
    if (key == value) {
      return std::string(result);
    }
  }
  return {};
}

bool FunctionKey(std::string_view key) {
  int number = 0;
  return key.size() >= 2 && key.size() <= 3 && key[0] == 'F' && key[1] != '0' &&
         base::StringToInt(key.substr(1), &number) && number >= 1 &&
         number <= 24;
}

bool Conflicts(std::string_view first, std::string_view second) {
  return first == second || first.starts_with(std::string(second) + " ") ||
         second.starts_with(std::string(first) + " ");
}

KeyboardValidation Invalid(std::string message) {
  return {std::nullopt, std::move(message)};
}

// A binding as ValidateKeyboardConfig will store it: each stroke normalised,
// strokes separated by one space.
std::string NormalizeSequence(std::string_view binding) {
  std::vector<std::string> strokes;
  for (const auto& stroke :
       base::SplitString(binding, base::kWhitespaceASCII,
                         base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
    strokes.push_back(NormalizeStroke(stroke));
  return base::JoinString(strokes, " ");
}

const base::DictValue& Metadata() {
  static const base::NoDestructor<base::DictValue> metadata([] {
    auto parsed =
        base::JSONReader::ReadDict(kActionMetadataJSON, base::JSON_PARSE_RFC);
    CHECK(parsed);
    return std::move(*parsed);
  }());
  return *metadata;
}
}  // namespace

std::string NormalizeStroke(std::string_view input) {
  if (!base::IsStringUTF8(input) || input.empty() ||
      base::UTF8ToUTF16(input).size() > 80) {
    return {};
  }
  auto trimmed = Trim(input);
  if (trimmed.empty()) {
    return {};
  }
  if (trimmed.ends_with("++")) {
    trimmed.replace(trimmed.size() - 1, 1, "Plus");
  }
  std::set<std::string> modifiers;
  std::string key;
  for (const auto& part : base::SplitString(trimmed, "+", base::KEEP_WHITESPACE,
                                            base::SPLIT_WANT_ALL)) {
    const auto token = Trim(part);
    const auto lower = base::ToLowerASCII(token);
    const auto modifier = Lookup(kModifierAliases, lower);
    if (!modifier.empty()) {
      if (!modifiers.insert(modifier).second) {
        return {};
      }
      continue;
    }
    if (!key.empty() || token.empty()) {
      return {};
    }
    key = Lookup(kNamedKeys, lower);
    if (key.empty()) {
      if (FunctionKey(base::ToUpperASCII(token))) {
        key = base::ToUpperASCII(token);
      } else {
        auto unicode = icu::UnicodeString::fromUTF8(token);
        if (unicode.length() != 1) {
          return {};
        }
        unicode.toUpper(icu::Locale::getRoot()).toUTF8String(key);
      }
    }
  }
  if (key.empty()) {
    return {};
  }
  if (key == "Plus" ||
      (base::UTF8ToUTF16(key).size() == 1 &&
       !(key.size() == 1 && base::IsAsciiAlphaNumeric(key.front())))) {
    modifiers.erase("Shift");
  }
  std::string result;
  for (const auto modifier : kModifiers) {
    if (modifiers.contains(std::string(modifier))) {
      result += std::string(modifier) + "+";
    }
  }
  return result + key;
}

bool IsKnownAction(std::string_view id) {
  return Metadata().contains(id);
}
std::string ActionLabel(std::string_view id) {
  const auto* row = Metadata().FindDict(id);
  const auto* label = row ? row->FindString("label") : nullptr;
  return label ? *label : std::string();
}
bool IsRepeatableAction(std::string_view id) {
  return id.starts_with("scroll-") || id.starts_with("half-page-") ||
         id.starts_with("zoom-") || id == "next-tab" || id == "previous-tab";
}

std::map<std::string, std::string> CompileBindings(const BindingTable& table,
                                                   std::string_view tabs) {
  std::map<std::string, std::string> result;
  if (!tabs.empty()) {
    for (int i = 1; i <= 9; ++i) {
      result.emplace(std::string(tabs) + "+" + base::NumberToString(i),
                     "tab-" + base::NumberToString(i));
    }
  }
  for (const auto& [action, bindings] : table) {
    for (const auto& binding : bindings) {
      result[binding] = action;
    }
  }
  return result;
}

KeyboardValidation ValidateKeyboardConfig(const base::DictValue& input) {
  if (input.FindInt("version") != 2) {
    return Invalid("Unsupported keyboard configuration.");
  }
  KeyboardConfig config;
  const auto* tabs = input.FindString("tabs");
  if (!tabs && input.contains("tabs")) {
    return Invalid("Invalid tab modifier.");
  }
  if (tabs && !tabs->empty()) {
    auto normalized = NormalizeStroke(*tabs + "+1");
    if (!normalized.ends_with("+1")) {
      return Invalid("Invalid tab modifier.");
    }
    config.tabs = normalized.substr(0, normalized.size() - 2);
    if (config.tabs.find("Ctrl") == std::string::npos &&
        config.tabs.find("Alt") == std::string::npos &&
        config.tabs.find("Meta") == std::string::npos) {
      return Invalid("Selecting tabs by number needs Ctrl, Alt or Meta.");
    }
  }
  for (const auto scope : {"shortcuts", "normal"}) {
    const bool global = std::string_view(scope) == "shortcuts";
    const auto* raw_table = input.FindDict(scope);
    if (!raw_table) {
      return Invalid("Invalid binding table.");
    }
    auto& table = global ? config.shortcuts : config.normal;
    auto occupied = CompileBindings({}, global ? config.tabs : "");
    for (const auto [action, raw] : *raw_table) {
      if (!IsKnownAction(action)) {
        return Invalid("Unknown action: " + action);
      }
      base::ListValue single;
      const base::ListValue* values = raw.GetIfList();
      if (!values) {
        if (!raw.is_none() && !(raw.is_string() && raw.GetString().empty())) {
          single.Append(raw.Clone());
        }
        values = &single;
      }
      if (values->size() > 8) {
        return Invalid("Use at most eight bindings per action.");
      }
      auto& bindings = table[action];
      for (const auto& raw_binding : *values) {
        if (!raw_binding.is_string() ||
            !base::IsStringUTF8(raw_binding.GetString())) {
          return Invalid("Invalid binding.");
        }
        const auto raw_strokes =
            base::SplitString(raw_binding.GetString(), base::kWhitespaceASCII,
                              base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
        if (raw_strokes.empty() || raw_strokes.size() > (global ? 1u : 2u)) {
          return Invalid("Invalid binding sequence.");
        }
        std::vector<std::string> strokes;
        for (const auto& stroke : raw_strokes) {
          auto normalized = NormalizeStroke(stroke);
          if (normalized.empty() || normalized == "Escape") {
            return Invalid("Invalid or reserved key.");
          }
          strokes.push_back(std::move(normalized));
        }
        const auto combo = base::JoinString(strokes, " ");
        if (combo == "Tab" || combo == "Shift+Tab") {
          return Invalid("Tab is reserved for focus navigation.");
        }
        if (global && !combo.starts_with("Ctrl+") &&
            !combo.starts_with("Alt+") && !combo.starts_with("Meta+") &&
            !FunctionKey(combo) && combo != "Shift+Escape") {
          return Invalid(
              "Browser shortcuts need Ctrl, Alt, Meta or a function key.");
        }
        if (std::ranges::find(bindings, combo) != bindings.end()) {
          continue;
        }
        for (const auto& [other, owner] : occupied) {
          if (Conflicts(other, combo)) {
            return Invalid("Conflicting binding: " + combo);
          }
        }
        bindings.push_back(combo);
        occupied.emplace(combo, action);
      }
    }
  }
  auto passthrough = config.shortcuts.find("toggle-passthrough");
  if (passthrough == config.shortcuts.end() || passthrough->second.empty()) {
    return Invalid("Keep a browser shortcut for passthrough.");
  }
  const auto globals = CompileBindings(config.shortcuts, config.tabs);
  for (const auto& [action, bindings] : config.normal) {
    for (const auto& binding : bindings) {
      for (const auto& stroke : base::SplitString(
               binding, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
        if (globals.contains(stroke)) {
          return Invalid("Page key is already a browser shortcut: " + stroke);
        }
      }
    }
  }
  return {std::move(config), {}};
}

base::DictValue KeyboardConfig::ToValue() const {
  base::DictValue result;
  result.Set("version", 2);
  result.Set("tabs", tabs);
  for (const auto scope : {"shortcuts", "normal"}) {
    base::DictValue table;
    for (const auto& [action, bindings] :
         std::string_view(scope) == "shortcuts" ? shortcuts : normal) {
      base::ListValue values;
      for (const auto& binding : bindings) {
        values.Append(binding);
      }
      table.Set(action, std::move(values));
    }
    result.Set(scope, std::move(table));
  }
  return result;
}

const KeyboardConfig& DefaultKeyboardConfig() {
  static const base::NoDestructor<KeyboardConfig> config([] {
    auto parsed =
        base::JSONReader::ReadDict(kDefaultKeyboardJSON, base::JSON_PARSE_RFC);
    CHECK(parsed);
    auto validated = ValidateKeyboardConfig(*parsed);
    CHECK(validated.config) << validated.error;
    return std::move(*validated.config);
  }());
  return *config;
}
KeyboardValidation ResolveKeyboardChanges(const base::DictValue& changes) {
  const auto* shortcuts = changes.FindDict("shortcuts");
  const auto* normal = changes.FindDict("normal");
  if (!shortcuts || !normal)
    return Invalid("Invalid binding tables.");
  auto candidate = DefaultKeyboardConfig().ToValue();
  if (const auto* tabs = changes.Find("tabs"))
    candidate.Set("tabs", tabs->Clone());
  // Explicit choices claim their chords first. Defaults yield conflicting
  // chords/sequences, preserving assignments across later default changes.
  std::set<std::string> global_choices;
  std::set<std::string> page_choices;
  for (const auto* table : {shortcuts, normal}) {
    for (const auto [action, value] : *table) {
      if (!IsKnownAction(action))
        return Invalid("Unknown action: " + action);
      if (!value.is_list())
        return Invalid("Bindings must be lists: " + action);
      for (const auto& binding : value.GetList()) {
        if (!binding.is_string())
          return Invalid("Bindings must be text: " + action);
        (table == shortcuts ? global_choices : page_choices)
            .insert(NormalizeSequence(binding.GetString()));
      }
    }
  }
  // A newly introduced default tab modifier cannot steal an explicit choice,
  // including any stroke of a page-key sequence.
  if (!changes.contains("tabs")) {
    const auto numbered = CompileBindings({}, DefaultKeyboardConfig().tabs);
    for (const auto& [chord, action] : numbered) {
      if (global_choices.contains(chord) ||
          std::ranges::any_of(page_choices, [&](const std::string& choice) {
            return std::ranges::contains(
                base::SplitString(choice, " ", base::KEEP_WHITESPACE,
                                  base::SPLIT_WANT_ALL),
                chord);
          })) {
        candidate.Set("tabs", "");
        break;
      }
    }
  }
  for (const auto scope : {"shortcuts", "normal"}) {
    auto* table = candidate.FindDict(scope);
    const bool global = std::string_view(scope) == "shortcuts";
    const auto& chosen = global ? *shortcuts : *normal;
    for (auto [action, value] : *table) {
      if (const auto* override = chosen.Find(action)) {
        value = override->Clone();
        continue;
      }
      value.GetList().EraseIf([&](const base::Value& binding) {
        const auto& chord = binding.GetString();
        if (global_choices.contains(chord))
          return true;
        if (!global) {
          for (const auto& stroke : base::SplitString(
                   chord, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL)) {
            if (global_choices.contains(stroke))
              return true;
          }
        }
        for (const auto& choice : page_choices) {
          if (Conflicts(chord, choice))
            return true;
          if (global) {
            const auto strokes = base::SplitString(
                choice, " ", base::KEEP_WHITESPACE, base::SPLIT_WANT_ALL);
            if (std::ranges::find(strokes, chord) != strokes.end())
              return true;
          }
        }
        return false;
      });
    }
  }
  return ValidateKeyboardConfig(candidate);
}
}  // namespace fly
