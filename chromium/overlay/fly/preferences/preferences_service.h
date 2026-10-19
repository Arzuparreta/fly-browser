// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_PREFERENCES_PREFERENCES_SERVICE_H_
#define FLY_PREFERENCES_PREFERENCES_SERVICE_H_

#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ref.h"
#include "base/sequence_checker.h"
#include "base/time/time.h"

class PrefRegistrySimple;
class PrefService;
namespace fly {
inline constexpr char kPreferencesPref[] = "fly.preferences";
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

 private:
  bool ValidStore() const;
  bool SaveClock(const ClockPreferences& clock);
  bool SaveTheme(const ThemePreferences& theme);
  const raw_ref<PrefService> local_state_;
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace fly
#endif  // FLY_PREFERENCES_PREFERENCES_SERVICE_H_
