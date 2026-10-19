// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/preferences_service.h"
#include "fly/prefs/dictionary_store.h"

#include <algorithm>
#include <array>

#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "components/prefs/pref_registry_simple.h"
#include "components/prefs/pref_service.h"

namespace fly {
namespace {
constexpr std::array kSuspendMinutes = {0, 5, 15, 30, 60};
bool ValidSuspendMinutes(int minutes) {
  return std::ranges::find(kSuspendMinutes, minutes) != kSuspendMinutes.end();
}
bool ValidFormat(std::string_view format) {
  return format == "datetime" || format == "time" || format == "seconds";
}
}  // namespace
bool WebsiteUsesDarkTheme(const ThemePreferences& theme) {
  return theme.sync_websites && theme.mode == "dark";
}
ClockText FormatClock(base::Time time, std::string_view format) {
  if (!ValidFormat(format) || time.is_null())
    return {};
  base::Time::Exploded local;
  time.LocalExplode(&local);
  if (!local.HasValidValues())
    return {};
  constexpr std::array days = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  constexpr std::array months = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  std::string label;
  if (format != "time")
    label = base::StringPrintf("%s %d %s  ", days.at(local.day_of_week),
                               local.day_of_month, months.at(local.month - 1));
  label += base::StringPrintf("%02d:%02d", local.hour, local.minute);
  if (format == "seconds")
    label += base::StringPrintf(":%02d", local.second);
  return {base::UTF8ToUTF16(label),
          base::UTF8ToUTF16(base::StringPrintf(
              "%s %d %s %d, %02d:%02d:%02d (Click to switch format)",
              days.at(local.day_of_week), local.day_of_month,
              months.at(local.month - 1), local.year, local.hour, local.minute,
              local.second))};
}
void PreferencesService::RegisterLocalStatePrefs(PrefRegistrySimple* registry) {
  registry->RegisterDictionaryPref(kPreferencesPref);
}
PreferencesService::PreferencesService(PrefService& local_state)
    : local_state_(local_state) {}
PreferencesService::~PreferencesService() = default;
bool PreferencesService::ValidStore() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  const auto* readable = ReadDictionaryStore(*local_state_, kPreferencesPref);
  if (!readable)
    return false;
  const auto& stored = *readable;
  if (stored.empty())
    return true;
  const auto* values = stored.FindDict("values");
  if (stored.FindInt("schema") != 1 || !values)
    return false;
  for (auto key : {"show_clock", "show_status_bar", "sync_website_theme",
                   "show_window_controls", "sidebar_auto_collapse"})
    if (const auto* value = values->Find(key); value && !value->is_bool())
      return false;
  if (const auto* format = values->Find("clock_format");
      format && (!format->is_string() || !ValidFormat(format->GetString())))
    return false;
  if (const auto* theme = values->Find("theme");
      theme && (!theme->is_string() || (theme->GetString() != "dark" &&
                                        theme->GetString() != "light")))
    return false;
  if (const auto* timeout = values->Find("tab_suspend_timeout_minutes");
      timeout &&
      (!timeout->is_int() || !ValidSuspendMinutes(timeout->GetInt())))
    return false;
  return true;
}
std::optional<int> PreferencesService::ReadSuspendTimeoutMinutes() const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  return values ? values->FindInt("tab_suspend_timeout_minutes").value_or(15)
                : 15;
}
bool PreferencesService::CanEditSuspendTimeout() const {
  return ReadSuspendTimeoutMinutes().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SetSuspendTimeoutMinutes(int minutes) {
  if (!CanEditSuspendTimeout() || !ValidSuspendMinutes(minutes))
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("tab_suspend_timeout_minutes", minutes);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
bool PreferencesService::CycleSuspendTimeout() {
  const auto minutes = ReadSuspendTimeoutMinutes();
  if (!minutes)
    return false;
  const auto current = std::ranges::find(kSuspendMinutes, *minutes);
  const auto index =
      (current - kSuspendMinutes.begin() + 1) % kSuspendMinutes.size();
  return SetSuspendTimeoutMinutes(kSuspendMinutes[index]);
}
std::optional<ThemePreferences> PreferencesService::ReadTheme() const {
  if (!ValidStore())
    return std::nullopt;
  ThemePreferences theme;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  if (values) {
    if (const auto* mode = values->FindString("theme"))
      theme.mode = *mode;
    theme.sync_websites = values->FindBool("sync_website_theme").value_or(true);
  }
  return theme;
}
bool PreferencesService::CanEditTheme() const {
  return ReadTheme().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SaveTheme(const ThemePreferences& theme) {
  if (!CanEditTheme() || (theme.mode != "dark" && theme.mode != "light"))
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  auto* values = stored.EnsureDict("values");
  values->Set("theme", theme.mode);
  values->Set("sync_website_theme", theme.sync_websites);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
bool PreferencesService::ToggleTheme() {
  auto theme = ReadTheme();
  if (!theme)
    return false;
  theme->mode = theme->mode == "dark" ? "light" : "dark";
  return SaveTheme(*theme);
}
bool PreferencesService::SetWebsiteThemeSync(bool sync) {
  auto theme = ReadTheme();
  if (!theme)
    return false;
  theme->sync_websites = sync;
  return SaveTheme(*theme);
}
std::optional<bool> PreferencesService::ReadSidebarAutoCollapse() const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  return values ? values->FindBool("sidebar_auto_collapse").value_or(true)
                : true;
}
bool PreferencesService::CanEditSidebarAutoCollapse() const {
  return ReadSidebarAutoCollapse().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SetSidebarAutoCollapse(bool collapse) {
  if (!CanEditSidebarAutoCollapse())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("sidebar_auto_collapse", collapse);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<bool> PreferencesService::ReadWindowControlsVisible() const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  return values ? values->FindBool("show_window_controls").value_or(false)
                : false;
}
bool PreferencesService::CanEditWindowControls() const {
  return ReadWindowControlsVisible().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SetWindowControlsVisible(bool visible) {
  if (!CanEditWindowControls())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("show_window_controls", visible);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<bool> PreferencesService::ReadStatusBarVisible() const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  return values ? values->FindBool("show_status_bar").value_or(true) : true;
}
bool PreferencesService::CanEditStatusBar() const {
  return ReadStatusBarVisible().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SetStatusBarVisible(bool visible) {
  if (!CanEditStatusBar())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("show_status_bar", visible);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<ClockPreferences> PreferencesService::ReadClock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ValidStore())
    return std::nullopt;
  const auto& stored = local_state_->GetDict(kPreferencesPref);
  ClockPreferences clock;
  if (stored.empty())
    return clock;
  const auto* values = stored.FindDict("values");
  if (stored.FindInt("schema") != 1 || !values)
    return std::nullopt;
  if (const auto* value = values->Find("show_clock")) {
    if (!value->is_bool())
      return std::nullopt;
    clock.visible = value->GetBool();
  }
  if (const auto* value = values->Find("clock_format")) {
    if (!value->is_string() || !ValidFormat(value->GetString()))
      return std::nullopt;
    clock.format = value->GetString();
  }
  return clock;
}
bool PreferencesService::CanEditClock() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return ReadClock().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SaveClock(const ClockPreferences& clock) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!CanEditClock() || !ValidFormat(clock.format))
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  auto* values = stored.EnsureDict("values");
  values->Set("show_clock", clock.visible);
  values->Set("clock_format", clock.format);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
bool PreferencesService::SetClockVisible(bool visible) {
  const auto current = ReadClock();
  if (!current)
    return false;
  auto next = *current;
  next.visible = visible;
  return SaveClock(next);
}
bool PreferencesService::CycleClockFormat() {
  const auto current = ReadClock();
  if (!current)
    return false;
  auto next = *current;
  next.format = current->format == "datetime" ? "time"
                : current->format == "time"   ? "seconds"
                                              : "datetime";
  return SaveClock(next);
}
}  // namespace fly
