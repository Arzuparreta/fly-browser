// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_COMMAND_SERVICE_H_
#define FLY_COMMANDS_COMMAND_SERVICE_H_
#include <cstdint>
#include <optional>

#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/commands/keyboard_config.h"

class PrefRegistrySimple;
class PrefService;
namespace fly {
inline constexpr char kKeyboardPref[] = "fly.keyboard";
class CommandService {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  explicit CommandService(PrefService& local_state);
  ~CommandService();
  CommandService(const CommandService&) = delete;
  CommandService& operator=(const CommandService&) = delete;
  std::optional<KeyboardConfig> Read() const;
  // Trusted callers must send a complete editor projection. Validate the whole
  // transaction before PrefService persistence; only differences are stored.
  KeyboardValidation Save(const base::DictValue& config);
  uint64_t generation() const { return generation_; }

 private:
  void OnChanged();
  const raw_ref<PrefService> local_state_;
  PrefChangeRegistrar registrar_;
  uint64_t generation_ = 1;
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace fly
#endif  // FLY_COMMANDS_COMMAND_SERVICE_H_
