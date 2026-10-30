// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFERENCES_PREFERENCES_SERVICE_H_
#define FLY_PREFERENCES_PREFERENCES_SERVICE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"
#include "base/values.h"

class PrefRegistrySimple;
class PrefService;
namespace base {
class CommandLine;
}
namespace fly {
inline constexpr char kPreferencesPref[] = "fly.preferences";
inline constexpr char kRecentCommandsPref[] = "fly.recent_commands";
struct ClockPreferences {
  bool visible = true;
  std::string format = "datetime";
  bool operator==(const ClockPreferences&) const = default;
};
struct ClockText {
  std::u16string label;
  std::u16string tooltip;
};
ClockText FormatClock(base::Time time, std::string_view format);
struct ThemePreferences {
  std::string mode = "dark";
  bool sync_websites = true;
  bool operator==(const ThemePreferences&) const = default;
};
bool WebsiteUsesDarkTheme(const ThemePreferences& theme);
struct SpellcheckPreferences {
  bool enabled = true;
  // Codes of SpellcheckLanguages(), the language checked first first; at most
  // kMaxSpellcheckLanguages.
  std::vector<std::string> languages;
  bool operator==(const SpellcheckPreferences&) const = default;
};

// App-wide product preferences, separate from each Profile's native site state.
// Clock, footer, frequent sites, window controls, theme and suspension timeout
// are implemented groups. Other
// settings remain pending; unknown fields are preserved for later groups
// and importer versions. A malformed/future store requires recovery, never an
// implicit default write.
class PreferencesService {
 public:
  static void RegisterLocalStatePrefs(PrefRegistrySimple* registry);
  explicit PreferencesService(PrefService& local_state);
  ~PreferencesService();
  PreferencesService(const PreferencesService&) = delete;
  PreferencesService& operator=(const PreferencesService&) = delete;
  std::optional<ClockPreferences> ReadClock() const;
  bool CanEditClock() const;
  bool SetClockVisible(bool visible);
  bool CycleClockFormat();
  // "datetime", "time" or "seconds".
  bool SetClockFormat(const std::string& format);
  std::optional<bool> ReadSidebarAutoCollapse() const;
  bool CanEditSidebarAutoCollapse() const;
  bool SetSidebarAutoCollapse(bool collapse);
  std::optional<bool> ReadWindowControlsVisible() const;
  bool CanEditWindowControls() const;
  bool SetWindowControlsVisible(bool visible);
  std::optional<bool> ReadFrequentSitesVisible() const;
  bool CanEditFrequentSites() const;
  bool SetFrequentSitesVisible(bool visible);
  std::optional<bool> ReadStatusBarVisible() const;
  bool CanEditStatusBar() const;
  bool SetStatusBarVisible(bool visible);
  std::optional<ThemePreferences> ReadTheme() const;
  bool CanEditTheme() const;
  bool ToggleTheme();
  bool SetWebsiteThemeSync(bool sync);
  std::optional<int> ReadSuspendTimeoutMinutes() const;
  bool CanEditSuspendTimeout() const;
  bool SetSuspendTimeoutMinutes(int minutes);
  bool CycleSuspendTimeout();

  // Spell checking chosen in Fly: spellcheck_enabled and the list
  // spellcheck_languages, or before those existed the single
  // spellchecker_language ("disabled" when off). nullopt when Fly has no
  // choice (each workspace keeps Chromium's) or the store needs recovery.
  // Unknown or extra languages are left out.
  std::optional<SpellcheckPreferences> ReadSpellcheck() const;
  // Stores both keys and drops spellchecker_language. The languages must be
  // known, without repeats and at most kMaxSpellcheckLanguages.
  bool SetSpellcheck(const SpellcheckPreferences& spellcheck);

  // Remaining product settings, validated per key, with their defaults:
  // bools disable_gpu, background_throttling, enable_diagnostics,
  // web_notifications, desktop_integration_offered, preload_address_results;
  // int process_limit (1-8);
  // strings interface_scale (normal|large), email_handler, last_seen_version.
  std::optional<bool> ReadBool(std::string_view key) const;
  bool SetBool(std::string_view key, bool value);
  std::optional<int> ReadInt(std::string_view key) const;
  bool SetInt(std::string_view key, int value);
  std::optional<std::string> ReadString(std::string_view key) const;
  bool SetString(std::string_view key, const std::string& value);
  bool CanEdit() const;
  // True when `key` is explicitly stored (not just its default).
  bool HasStoredValue(std::string_view key) const;
  // theme_colors.{dark,light}.{accent,bg_main,bg_sidebar,text}: "#rrggbb".
  // Missing entries use the product defaults (empty result).
  std::optional<std::string> ReadThemeColor(bool dark,
                                            std::string_view key) const;
  bool SetThemeColor(bool dark, std::string_view key, const std::string& hex);
  bool ResetThemeColors();
  // sidebar_opacity and overlay_opacity (40-100, default 100),
  // stored with the colours of each theme.
  int ReadThemeOpacity(bool dark, std::string_view key) const;
  bool SetThemeOpacity(bool dark, std::string_view key, int percent);
  // Native extension state ("native_extensions"): bundled removals and
  // per-workspace switches, extensions suspended by the global switch.
  std::optional<base::DictValue> ReadNativeExtensions() const;
  bool SetNativeExtensions(base::DictValue state);
  // The Command Centre's four most recently used commands, newest
  // first, in their own Local State entry.
  std::vector<std::string> ReadRecentCommands() const;
  void RecordRecentCommand(std::string_view action);
  // Applies settings that only take effect at process start (GPU, background
  // throttling, renderer process limit). Called before child processes start.
  static void ApplyStartupSwitches(const PrefService& local_state,
                                   base::CommandLine& command_line);

 private:
  bool ValidStore() const;
  bool SaveClock(const ClockPreferences& clock);
  bool SaveTheme(const ThemePreferences& theme);
  const raw_ref<PrefService> local_state_;
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace fly
#endif  // FLY_PREFERENCES_PREFERENCES_SERVICE_H_
