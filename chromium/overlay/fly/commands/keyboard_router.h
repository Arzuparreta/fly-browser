// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_COMMANDS_KEYBOARD_ROUTER_H_
#define FLY_COMMANDS_KEYBOARD_ROUTER_H_

#include <map>
#include <set>
#include <string>

#include "base/time/time.h"
#include "fly/commands/keyboard_config.h"

namespace fly {
enum class KeyboardMode { kNormal, kInsert, kPassthrough };
enum class KeyResultKind { kPass, kConsume, kAction, kExitInsert };
struct KeyResult {
  KeyResultKind kind = KeyResultKind::kPass;
  std::string action;
};
struct KeyContext {
  // A layout-derived canonical stroke, never a physical-key approximation.
  std::string stroke;
  base::TimeTicks time;
  bool repeat = false;
  bool composing = false;
  bool editable = false;
  bool page_keys_allowed = true;
  bool hints_active = false;
  bool recorder_active = false;
};

// One state per WebContents. Browser and renderer focus changes clear pending
// sequences, not the document's explicit INSERT/passthrough choice.
class KeyboardRouter {
 public:
  explicit KeyboardRouter(const KeyboardConfig& config);
  ~KeyboardRouter();
  KeyboardRouter(const KeyboardRouter&) = delete;
  KeyboardRouter& operator=(const KeyboardRouter&) = delete;
  void Configure(const KeyboardConfig& config);
  KeyResult Route(const KeyContext& context);
  // Trusted native UI actions share mode transitions with keyboard dispatch.
  KeyResult Invoke(std::string action);
  void ClearPending();
  KeyboardMode mode() const { return mode_; }

 private:
  KeyResult Run(std::string action, bool repeat);
  std::map<std::string, std::string> shortcuts_;
  std::map<std::string, std::string> normal_;
  std::set<std::string> prefixes_;
  std::string pending_;
  base::TimeTicks pending_time_;
  KeyboardMode mode_ = KeyboardMode::kNormal;
};
}  // namespace fly
#endif  // FLY_COMMANDS_KEYBOARD_ROUTER_H_
