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

// Electron Fly's reader view: modules/readerDistiller.js (Mozilla
// Readability with Fly's overlay, toolbar and per-site preferences), run in
// the page as Electron did. Running it again closes the overlay.
struct ReaderResult {
  bool active = false;
  std::u16string title;
  std::u16string error;
};
void ToggleReaderView(content::WebContents* contents,
                      base::OnceCallback<void(ReaderResult)> done);
// Whether the page currently shows the reader overlay (it also closes with
// Escape and its own close button).
void QueryReaderView(content::WebContents* contents,
                     base::OnceCallback<void(bool)> done);

// Electron's Privacy buttons: the cookies of `url`'s site, or the cache,
// cache storage and service workers of the workspace profile. `done` gets the
// message Electron showed.
void ClearSiteCookies(Profile* profile,
                      const GURL& url,
                      base::OnceCallback<void(std::u16string)> done);
void ClearWorkspaceCache(Profile* profile,
                         base::OnceCallback<void(std::u16string)> done);

// Electron's air-gapped terminal fallback: copies the text (prefixed with
// "oversight" for scripts, URLs and sudo/curl/wget) to the clipboard and opens
// the first terminal found (kitty, alacritty, foot, st, xterm, or
// xdg-terminal-exec); nothing runs until the user pastes it.
void OpenTerminalWithCommand(const std::string& text);

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
  // The release's native Linux package and its .sha256 file, when published.
  GURL package;
  GURL checksum;
};
// Queries the latest published Fly release (GitHub, no credentials).
void CheckForUpdates(base::OnceCallback<void(UpdateCheckResult)> done);

// Electron's appImageUpdate: downloads `update.package`, checks it against
// `update.checksum`, unpacks it and replaces this installation (the directory
// of the running executable) in one rename. `done` gets an empty string on
// success, otherwise the reason. The caller restarts.
void InstallNativeUpdate(const UpdateCheckResult& update,
                         base::OnceCallback<void(std::u16string)> done);

// Electron's desktopIntegration.js for the native package: the applications
// menu entry (fly-browser.desktop, starting the package's launcher)
// and the default browser.
struct DesktopIntegrationStatus {
  // Only a package (with its launcher next to the executable) can add itself.
  bool supported = false;
  base::FilePath entry;
  bool ours = false;
  // nullopt when xdg-utils is missing; empty when no default is set.
  std::optional<std::string> default_browser;
  bool is_default = false;
};
void ReadDesktopIntegration(
    base::OnceCallback<void(DesktopIntegrationStatus)> done);
enum class DesktopIntegrationChange { kAddEntry, kRemoveEntry, kMakeDefault };
// `done` gets an error ("" on success) and the new status.
void ChangeDesktopIntegration(
    DesktopIntegrationChange change,
    base::OnceCallback<void(std::u16string, DesktopIntegrationStatus)> done);

// Local What's New page shipped next to the executable, or the online release
// list when it is missing. `from` is the previously run version.
GURL WhatsNewURL(std::string_view from);
// Resolves WhatsNewURL off the UI thread, falling back to the release list
// when the page is not installed (development builds).
void ResolveWhatsNewURL(std::string from, base::OnceCallback<void(GURL)> done);

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
