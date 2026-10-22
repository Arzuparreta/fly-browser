// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/preferences/preferences_service.h"
#include "fly/prefs/dictionary_store.h"

#include <algorithm>
#include <array>

#include "base/command_line.h"
#include "base/containers/fixed_flat_map.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
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
constexpr auto kBoolDefaults = base::MakeFixedFlatMap<std::string_view, bool>({
    {"background_throttling", true},
    {"desktop_integration_offered", false},
    {"disable_gpu", false},
    {"enable_diagnostics", false},
    {"extensions_enabled", true},
    {"web_notifications", true},
});
constexpr auto kStringDefaults =
    base::MakeFixedFlatMap<std::string_view, std::string_view>({
        {"email_handler", "system"},
        {"interface_scale", "normal"},
        {"last_seen_version", ""},
        {"spellchecker_language", "en-GB"},
    });
constexpr int kDefaultProcessLimit = 4;
constexpr std::array kThemeColorKeys = {"accent", "bg_main", "bg_sidebar",
                                        "text"};
bool ValidProcessLimit(int value) {
  return value >= 1 && value <= 8;
}
bool ValidHexColor(std::string_view value) {
  return value.size() == 7 && value[0] == '#' &&
         std::ranges::all_of(value.substr(1),
                             [](char c) { return base::IsHexDigit(c); });
}
bool ValidString(std::string_view key, std::string_view value) {
  if (!base::IsStringASCII(value))
    return false;
  // Webmail compose URLs (Electron's Gmail, Zoho, Outlook choices) are longer
  // than other settings.
  if (key == "email_handler") {
    return value == "system" ||
           (value.size() <= 2048 && (value.starts_with("https://") ||
                                     value.starts_with("http://")));
  }
  if (value.size() > 64)
    return false;
  if (key == "interface_scale")
    return value == "normal" || value == "large";
  return true;
}
bool ValidThemeColors(const base::Value& colors) {
  if (!colors.is_dict())
    return false;
  // Unknown (future) entries are preserved untouched.
  for (const auto [mode, palette] : colors.GetDict()) {
    if (mode != "dark" && mode != "light")
      continue;
    if (!palette.is_dict())
      return false;
    for (const auto* key : kThemeColorKeys)
      if (const auto* value = palette.GetDict().Find(key);
          value && (!value->is_string() || !ValidHexColor(value->GetString())))
        return false;
  }
  return true;
}
const base::DictValue* StoredValues(const PrefService& local_state) {
  return local_state.GetDict(kPreferencesPref).FindDict("values");
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
  registry->RegisterListPref(kRecentCommandsPref);
}
std::vector<std::string> PreferencesService::ReadRecentCommands() const {
  std::vector<std::string> result;
  for (const auto& value : local_state_->GetList(kRecentCommandsPref))
    if (value.is_string() && result.size() < 4)
      result.push_back(value.GetString());
  return result;
}
void PreferencesService::RecordRecentCommand(std::string_view action) {
  if (action.empty() || action.size() > 128)
    return;
  base::ListValue list;
  list.Append(std::string(action));
  for (const auto& id : ReadRecentCommands())
    if (id != action && list.size() < 4)
      list.Append(id);
  local_state_->SetList(kRecentCommandsPref, std::move(list));
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
  for (auto key :
       {"show_clock", "show_status_bar", "sync_website_theme",
        "show_window_controls", "sidebar_auto_collapse", "show_frequent_sites"})
    if (const auto* value = values->Find(key); value && !value->is_bool())
      return false;
  if (const auto* format = values->Find("clock_format");
      format && (!format->is_string() || !ValidFormat(format->GetString())))
    return false;
  if (const auto* theme = values->Find("theme");
      theme && (!theme->is_string() || (theme->GetString() != "dark" &&
                                        theme->GetString() != "light")))
    return false;
  for (const auto& [key, fallback] : kBoolDefaults)
    if (const auto* value = values->Find(key); value && !value->is_bool())
      return false;
  for (const auto& [key, fallback] : kStringDefaults)
    if (const auto* value = values->Find(key);
        value && !value->is_none() &&
        (!value->is_string() || !ValidString(key, value->GetString())))
      return false;
  if (const auto* limit = values->Find("process_limit");
      limit && (!limit->is_int() || !ValidProcessLimit(limit->GetInt())))
    return false;
  if (const auto* colors = values->Find("theme_colors");
      colors && !ValidThemeColors(*colors))
    return false;
  if (const auto* native = values->Find("native_extensions");
      native && !native->is_dict())
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
std::optional<bool> PreferencesService::ReadFrequentSitesVisible() const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values =
      local_state_->GetDict(kPreferencesPref).FindDict("values");
  return values ? values->FindBool("show_frequent_sites").value_or(true) : true;
}
bool PreferencesService::CanEditFrequentSites() const {
  return ReadFrequentSitesVisible().has_value() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
bool PreferencesService::SetFrequentSitesVisible(bool visible) {
  if (!CanEditFrequentSites())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("show_frequent_sites", visible);
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
bool PreferencesService::SetClockFormat(const std::string& format) {
  const auto current = ReadClock();
  if (!current || !ValidFormat(format))
    return false;
  auto next = *current;
  next.format = format;
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
bool PreferencesService::CanEdit() const {
  return ValidStore() &&
         local_state_->FindPreference(kPreferencesPref)->IsUserModifiable();
}
std::optional<bool> PreferencesService::ReadBool(std::string_view key) const {
  const auto known = kBoolDefaults.find(key);
  if (known == kBoolDefaults.end() || !ValidStore())
    return std::nullopt;
  const auto* values = StoredValues(*local_state_);
  return values ? values->FindBool(key).value_or(known->second)
                : known->second;
}
bool PreferencesService::SetBool(std::string_view key, bool value) {
  if (!kBoolDefaults.contains(key) || !CanEdit())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set(key, value);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<int> PreferencesService::ReadInt(std::string_view key) const {
  if (key != "process_limit" || !ValidStore())
    return std::nullopt;
  const auto* values = StoredValues(*local_state_);
  return values ? values->FindInt(key).value_or(kDefaultProcessLimit)
                : kDefaultProcessLimit;
}
bool PreferencesService::SetInt(std::string_view key, int value) {
  if (key != "process_limit" || !ValidProcessLimit(value) || !CanEdit())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set(key, value);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<std::string> PreferencesService::ReadString(
    std::string_view key) const {
  const auto known = kStringDefaults.find(key);
  if (known == kStringDefaults.end() || !ValidStore())
    return std::nullopt;
  const auto* values = StoredValues(*local_state_);
  const auto* value = values ? values->FindString(key) : nullptr;
  return value ? *value : std::string(known->second);
}
bool PreferencesService::SetString(std::string_view key,
                                   const std::string& value) {
  if (!kStringDefaults.contains(key) || !ValidString(key, value) || !CanEdit())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set(key, value);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<std::string> PreferencesService::ReadThemeColor(
    bool dark,
    std::string_view key) const {
  if (!ValidStore() || std::ranges::find(kThemeColorKeys, key) ==
                           kThemeColorKeys.end())
    return std::nullopt;
  const auto* values = StoredValues(*local_state_);
  const auto* colors = values ? values->FindDict("theme_colors") : nullptr;
  const auto* palette = colors ? colors->FindDict(dark ? "dark" : "light")
                               : nullptr;
  const auto* value = palette ? palette->FindString(key) : nullptr;
  return value ? *value : std::string();
}
bool PreferencesService::SetThemeColor(bool dark,
                                       std::string_view key,
                                       const std::string& hex) {
  if (!CanEdit() || !ValidHexColor(hex) ||
      std::ranges::find(kThemeColorKeys, key) == kThemeColorKeys.end())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")
      ->EnsureDict("theme_colors")
      ->EnsureDict(dark ? "dark" : "light")
      ->Set(key, base::ToLowerASCII(hex));
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
int PreferencesService::ReadThemeOpacity(bool dark,
                                         std::string_view key) const {
  const auto* values = ValidStore() ? StoredValues(*local_state_) : nullptr;
  const auto* palette =
      values ? values->FindDictByDottedPath(
                   std::string("theme_colors.") + (dark ? "dark" : "light"))
             : nullptr;
  const auto value = palette ? palette->FindInt(key) : std::nullopt;
  return std::clamp(value.value_or(100), 40, 100);
}
bool PreferencesService::SetThemeOpacity(bool dark,
                                         std::string_view key,
                                         int percent) {
  if (!CanEdit() || (key != "sidebar_opacity" && key != "overlay_opacity") ||
      percent < 40 || percent > 100)
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")
      ->EnsureDict("theme_colors")
      ->EnsureDict(dark ? "dark" : "light")
      ->Set(key, percent);
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
bool PreferencesService::ResetThemeColors() {
  if (!CanEdit())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  if (auto* values = stored.FindDict("values"))
    values->Remove("theme_colors");
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
std::optional<base::DictValue> PreferencesService::ReadNativeExtensions()
    const {
  if (!ValidStore())
    return std::nullopt;
  const auto* values = StoredValues(*local_state_);
  const auto* state = values ? values->FindDict("native_extensions") : nullptr;
  return state ? state->Clone() : base::DictValue();
}
bool PreferencesService::SetNativeExtensions(base::DictValue state) {
  if (!CanEdit())
    return false;
  auto stored = local_state_->GetDict(kPreferencesPref).Clone();
  stored.Set("schema", 1);
  stored.EnsureDict("values")->Set("native_extensions", std::move(state));
  local_state_->SetDict(kPreferencesPref, std::move(stored));
  return true;
}
// static
void PreferencesService::ApplyStartupSwitches(const PrefService& local_state,
                                              base::CommandLine& command_line) {
  // A restart from Fly copies this process's switches. Drop the ones Fly
  // added last time (named by the marker) so changed settings take effect;
  // switches the user passed are never in the marker.
  constexpr char kMarker[] = "fly-startup-switches";
  for (const auto& name :
       base::SplitString(command_line.GetSwitchValueASCII(kMarker), ",",
                         base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY))
    command_line.RemoveSwitch(name);
  command_line.RemoveSwitch(kMarker);
  std::vector<std::string> added;
  const auto add = [&](const char* name, std::string value = std::string()) {
    if (command_line.HasSwitch(name))
      return;
    if (value.empty())
      command_line.AppendSwitch(name);
    else
      command_line.AppendSwitchASCII(name, value);
    added.push_back(name);
  };
  const auto* values = StoredValues(local_state);
  const auto flag = [&](std::string_view key) {
    const auto* value = values ? values->Find(key) : nullptr;
    return value && value->is_bool() ? value->GetBool()
                                     : kBoolDefaults.at(key);
  };
  if (flag("disable_gpu")) {
    add("disable-gpu");
    add("disable-gpu-compositing");
  }
  if (!flag("background_throttling")) {
    add("disable-background-timer-throttling");
    add("disable-renderer-backgrounding");
  }
  int limit = kDefaultProcessLimit;
  if (const auto* value = values ? values->Find("process_limit") : nullptr;
      value && value->is_int() && ValidProcessLimit(value->GetInt()))
    limit = value->GetInt();
  add("renderer-process-limit", base::NumberToString(limit));
  if (!added.empty())
    command_line.AppendSwitchASCII(kMarker, base::JoinString(added, ","));
}
bool PreferencesService::HasStoredValue(std::string_view key) const {
  const auto* values = ValidStore() ? StoredValues(*local_state_) : nullptr;
  return values && values->Find(key);
}
}  // namespace fly
