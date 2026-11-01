// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/new_tab_tips.h"

#include <algorithm>
#include <array>
#include <string>

#include "base/values.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace fly {
namespace {
// Search and Bookmarks already show their keys on the page.
constexpr std::array<NewTabTip, 11> kTips = {{
    {"trigger-hints", "Click links and buttons from the keyboard"},
    {"toggle-palette", "Find any command and its shortcut"},
    {"open-keyboard-settings", "See every shortcut and change it"},
    {"toggle-dashboard", "See every workspace and its tabs"},
    {"reopen-closed-tab", "Reopen the last tab you closed"},
    {"toggle-reader-view", "Read an article without clutter"},
    {"toggle-split", "Show two tabs side by side"},
    {"focus-sidebar", "Move through your tabs without the mouse"},
    {"toggle-history", "Search the pages you have visited"},
    {"yank-url", "Copy the address of the page"},
    {"toggle-passthrough", "Let a web app receive every key"},
}};
}  // namespace

void NewTabTips::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kNewTabTipsPref);
}

NewTabTips::NewTabTips(PrefService& local_state) : local_state_(local_state) {}
NewTabTips::~NewTabTips() = default;

std::optional<NewTabTip> NewTabTips::Find(std::string_view action) {
  const auto it = std::ranges::find(kTips, action, &NewTabTip::action);
  return it == kTips.end() ? std::nullopt : std::optional<NewTabTip>(*it);
}

int NewTabTips::Uses(std::string_view action) const {
  return std::clamp(
      local_state_->GetDict(kNewTabTipsPref).FindInt(action).value_or(0), 0,
      kNewTabTipUses);
}

void NewTabTips::SetUses(std::string_view action, int uses) {
  auto stored = local_state_->GetDict(kNewTabTipsPref).Clone();
  stored.Set(action, uses);
  local_state_->SetDict(kNewTabTipsPref, std::move(stored));
}

bool NewTabTips::Pending(std::string_view action) const {
  return Find(action) && Uses(action) < kNewTabTipUses;
}

std::optional<NewTabTip> NewTabTips::Next(
    base::FunctionRef<bool(std::string_view action)> usable) {
  for (size_t step = 0; step < kTips.size(); ++step) {
    const size_t index = (rotation_ + step) % kTips.size();
    const auto& tip = kTips[index];
    if (Uses(tip.action) < kNewTabTipUses && usable(tip.action)) {
      rotation_ = index + 1;
      return tip;
    }
  }
  return std::nullopt;
}

void NewTabTips::RecordKeyboardUse(std::string_view action) {
  if (!Find(action))
    return;
  if (const int uses = Uses(action); uses < kNewTabTipUses)
    SetUses(action, uses + 1);
}

void NewTabTips::Dismiss(std::string_view action) {
  if (Pending(action))
    SetUses(action, kNewTabTipUses);
}

void NewTabTips::Reset() {
  rotation_ = 0;
  local_state_->ClearPref(kNewTabTipsPref);
}
}  // namespace fly
