// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/command_service.h"
#include "fly/prefs/dictionary_store.h"

#include <algorithm>
#include <set>
#include <utility>

#include "base/functional/bind.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace fly {
namespace {
bool Conflicts(std::string_view a, std::string_view b) {
  return a == b || a.starts_with(std::string(b) + " ") ||
         b.starts_with(std::string(a) + " ");
}
}  // namespace
void CommandService::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kKeyboardPref);
}
CommandService::CommandService(PrefService& local_state)
    : local_state_(local_state) {
  registrar_.Init(&local_state);
  registrar_.Add(kKeyboardPref, base::BindRepeating(&CommandService::OnChanged,
                                                    base::Unretained(this)));
}
CommandService::~CommandService() = default;
void CommandService::OnChanged() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ++generation_;
}
std::optional<KeyboardConfig> CommandService::Read() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto* readable = ReadDictionaryStore(*local_state_, kKeyboardPref);
  if (!readable)
    return std::nullopt;
  const auto& stored = *readable;
  if (stored.empty())
    return DefaultKeyboardConfig();
  if (stored.FindInt("schema") != 1 || !stored.FindDict("shortcuts") ||
      !stored.FindDict("normal"))
    return std::nullopt;
  auto candidate = DefaultKeyboardConfig().ToValue();
  if (const auto* tabs = stored.Find("tabs"))
    candidate.Set("tabs", tabs->Clone());
  // Explicit choices claim their chords first. Defaults yield conflicting
  // chords/sequences, preserving assignments across later default changes.
  std::set<std::string> global_choices;
  std::set<std::string> page_choices;
  for (const auto scope : {"shortcuts", "normal"}) {
    for (const auto [action, value] : *stored.FindDict(scope)) {
      if (!IsKnownAction(action) || !value.is_list())
        return std::nullopt;
      for (const auto& binding : value.GetList()) {
        if (!binding.is_string())
          return std::nullopt;
        auto& choices = std::string_view(scope) == "shortcuts" ? global_choices
                                                               : page_choices;
        choices.insert(binding.GetString());
      }
    }
  }
  // A newly introduced default tab modifier cannot steal an explicit choice.
  if (!stored.contains("tabs")) {
    const auto numbered = CompileBindings({}, DefaultKeyboardConfig().tabs);
    for (const auto& [chord, action] : numbered) {
      if (global_choices.contains(chord) ||
          std::ranges::any_of(page_choices, [&](const std::string& choice) {
            return Conflicts(choice, chord);
          })) {
        candidate.Set("tabs", "");
        break;
      }
    }
  }
  for (const auto scope : {"shortcuts", "normal"}) {
    auto* table = candidate.FindDict(scope);
    const auto& chosen = *stored.FindDict(scope);
    const bool global = std::string_view(scope) == "shortcuts";
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
  auto result = ValidateKeyboardConfig(candidate);
  return std::move(result.config);
}
bool CommandService::CanEdit() const {
  return Read().has_value() &&
         local_state_->FindPreference(kKeyboardPref)->IsUserModifiable();
}
KeyboardValidation CommandService::Save(const base::DictValue& config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto result = ValidateKeyboardConfig(config);
  if (!result.config)
    return result;
  if (result.config->shortcuts.size() !=
          DefaultKeyboardConfig().shortcuts.size() ||
      result.config->normal.size() != DefaultKeyboardConfig().normal.size()) {
    return {std::nullopt, "Send the complete keyboard editor configuration."};
  }
  // Never overwrite a future/corrupt native store through an ordinary edit.
  // Recovery/reset is a separate user-visible operation.
  if (!Read())
    return {std::nullopt, "Existing keyboard data needs recovery."};
  if (!local_state_->FindPreference(kKeyboardPref)->IsUserModifiable())
    return {std::nullopt, "Keyboard settings are managed."};
  auto stored = local_state_->GetDict(kKeyboardPref).Clone();
  stored.Set("schema", 1);
  const auto& defaults = DefaultKeyboardConfig();
  if (result.config->tabs != defaults.tabs)
    stored.Set("tabs", result.config->tabs);
  else
    stored.Remove("tabs");
  for (const auto scope : {"shortcuts", "normal"}) {
    const auto& table = std::string_view(scope) == "shortcuts"
                            ? result.config->shortcuts
                            : result.config->normal;
    const auto& baseline = std::string_view(scope) == "shortcuts"
                               ? defaults.shortcuts
                               : defaults.normal;
    base::DictValue changes;
    for (const auto& [action, bindings] : table) {
      auto previous = baseline.find(action);
      if (previous != baseline.end() && previous->second == bindings)
        continue;
      base::ListValue values;
      for (const auto& binding : bindings)
        values.Append(binding);
      changes.Set(action, std::move(values));
    }
    stored.Set(scope, std::move(changes));
  }
  local_state_->SetDict(kKeyboardPref, std::move(stored));
  return result;
}
}  // namespace fly
