// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_KEYBOARD_CONFIG_H_
#define FLY_COMMANDS_KEYBOARD_CONFIG_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/values.h"

namespace fly {
using BindingTable = std::map<std::string, std::vector<std::string>>;
struct KeyboardConfig {
  BindingTable shortcuts;
  BindingTable normal;
  std::string tabs;
  bool operator==(const KeyboardConfig&) const = default;
  base::DictValue ToValue() const;
};
struct KeyboardValidation {
  std::optional<KeyboardConfig> config;
  std::string error;
};

std::string NormalizeStroke(std::string_view input);
KeyboardValidation ValidateKeyboardConfig(const base::DictValue& input);
const KeyboardConfig& DefaultKeyboardConfig();
bool IsKnownAction(std::string_view id);
std::string ActionLabel(std::string_view id);
bool IsRepeatableAction(std::string_view id);
std::map<std::string, std::string> CompileBindings(const BindingTable& table,
                                                   std::string_view tabs = {});
}  // namespace fly
#endif  // FLY_COMMANDS_KEYBOARD_CONFIG_H_
