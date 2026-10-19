// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_ACTIONS_H_
#define FLY_UI_FLY_ACTIONS_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "fly/commands/palette_catalog.h"
#include "ui/gfx/geometry/point.h"
#include "ui/menus/simple_menu_model.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;
namespace content {
class WebContents;
}
namespace views {
class MenuRunner;
class Widget;
}  // namespace views

namespace fly {
class PreferencesService;

// The Electron "native settings menu" (toggle-menu-bar): startup settings,
// tab suspension, sidebar options, user content folders and app entries.
class SettingsMenu : public ui::SimpleMenuModel::Delegate {
 public:
  SettingsMenu(PreferencesService& preferences,
               base::RepeatingCallback<void(std::string)> run_action);
  ~SettingsMenu() override;
  void Show(views::Widget* parent, const gfx::Point& screen_point);
  void Cancel();
  ui::SimpleMenuModel* model_for_testing();
  bool IsCommandIdChecked(int command_id) const override;
  bool IsCommandIdEnabled(int command_id) const override;
  void ExecuteCommand(int command_id, int event_flags) override;

 private:
  void Build();
  raw_ref<PreferencesService> preferences_;
  base::RepeatingCallback<void(std::string)> run_action_;
  std::unique_ptr<ui::SimpleMenuModel> model_;
  std::unique_ptr<ui::SimpleMenuModel> process_menu_;
  std::unique_ptr<ui::SimpleMenuModel> suspension_menu_;
  std::unique_ptr<views::MenuRunner> runner_;
};

// Reader view on Chromium's DOM distiller: distils the page in place, or
// returns to the original article.
bool IsReaderView(content::WebContents* contents);
void ToggleReaderView(content::WebContents* contents,
                      base::OnceCallback<void(bool)> done);

// `scripts`, `styles` or `script-storage` below the user data directory,
// created on demand.
base::FilePath UserContentDirectory(std::string_view name);
void OpenUserContentDirectory(Profile* profile, std::string_view name);

// Applies product preferences stored per profile in Chromium: web
// notification permission default and the spellchecker language.
void ApplyProfilePreferences(Profile* profile,
                             const PreferencesService& preferences);

struct UpdateCheckResult {
  bool ok = false;
  bool newer = false;
  std::string latest;
  GURL page;
};
// Queries the latest published Fly release (GitHub, no credentials).
void CheckForUpdates(base::OnceCallback<void(UpdateCheckResult)> done);

// Local What's New page shipped next to the executable, or the online release
// list when it is missing. `from` is the previously run version.
GURL WhatsNewURL(std::string_view from);

// Downloads of the given profiles, newest first, as picker entries.
std::vector<PaletteEntry> DownloadEntries(
    const std::vector<Profile*>& profiles);
// Opens the download named by a DownloadEntries action, or shows the
// download page for "downloads-page". Returns false if it no longer exists.
bool OpenDownloadEntry(const std::vector<Profile*>& profiles,
                       BrowserWindowInterface* browser,
                       std::string_view action);
bool RemoveDownloadEntry(const std::vector<Profile*>& profiles,
                         std::string_view action);

// Plain-text report for the diagnostics dialog: versions, platform, workspace
// count and (when recording is enabled) recent Fly status messages. Paths
// below the home directory are redacted.
std::u16string DiagnosticsReport(size_t workspace_count,
                                 const std::vector<std::u16string>& events);
}  // namespace fly

#endif  // FLY_UI_FLY_ACTIONS_H_
