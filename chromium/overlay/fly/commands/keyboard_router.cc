// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/commands/keyboard_router.h"

#include <utility>

namespace fly {
KeyboardRouter::KeyboardRouter(const KeyboardConfig& config) {
  Configure(config);
}
KeyboardRouter::~KeyboardRouter() = default;
void KeyboardRouter::Configure(const KeyboardConfig& config) {
  shortcuts_ = CompileBindings(config.shortcuts, config.tabs);
  normal_ = CompileBindings(config.normal);
  prefixes_.clear();
  for (const auto& [combo, action] : normal_) {
    const auto separator = combo.find(' ');
    if (separator != std::string::npos) {
      prefixes_.insert(combo.substr(0, separator));
    }
  }
  ClearPending();
}
void KeyboardRouter::ClearPending() {
  pending_.clear();
  pending_time_ = {};
}
KeyResult KeyboardRouter::Run(std::string action, bool repeat) {
  if (repeat && !IsRepeatableAction(action)) {
    return {KeyResultKind::kConsume, {}};
  }
  if (action == "toggle-passthrough") {
    mode_ = mode_ == KeyboardMode::kPassthrough ? KeyboardMode::kNormal
                                                : KeyboardMode::kPassthrough;
  } else if (action == "enter-insert") {
    mode_ = KeyboardMode::kInsert;
  }
  return {KeyResultKind::kAction, std::move(action)};
}
KeyResult KeyboardRouter::Route(const KeyContext& context) {
  if (context.recorder_active || context.composing || context.stroke.empty()) {
    ClearPending();
    return {};
  }
  const auto global = shortcuts_.find(context.stroke);
  if (global != shortcuts_.end() && global->second == "toggle-passthrough") {
    ClearPending();
    return Run(global->second, context.repeat);
  }
  if (mode_ == KeyboardMode::kPassthrough) {
    ClearPending();
    return {};
  }
  if (global != shortcuts_.end()) {
    ClearPending();
    return Run(global->second, context.repeat);
  }
  if (context.hints_active) {
    ClearPending();
    return {};
  }
  if (context.stroke == "Escape") {
    ClearPending();
    const bool was_insert = mode_ == KeyboardMode::kInsert || context.editable;
    mode_ = KeyboardMode::kNormal;
    return {was_insert ? KeyResultKind::kExitInsert : KeyResultKind::kPass, {}};
  }
  if (!context.page_keys_allowed || context.editable ||
      mode_ == KeyboardMode::kInsert) {
    ClearPending();
    return {};
  }
  auto action = normal_.end();
  if (!pending_.empty() &&
      context.time - pending_time_ < base::Milliseconds(700)) {
    action = normal_.find(pending_ + " " + context.stroke);
  }
  ClearPending();
  if (action == normal_.end()) {
    action = normal_.find(context.stroke);
  }
  if (action != normal_.end()) {
    return Run(action->second, context.repeat);
  }
  if (prefixes_.contains(context.stroke)) {
    if (!context.repeat) {
      pending_ = context.stroke;
      pending_time_ = context.time;
    }
    return {KeyResultKind::kConsume, {}};
  }
  return {};
}
}  // namespace fly
