// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_APP_H_
#define FLY_UI_FLY_APP_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/files/file_path.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/preferences_service.h"
#include "fly/ui/diagnostics_hub.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/history_service.h"
#include "fly/ui/site_zoom.h"
#include "fly/workspaces/workspace_service.h"
#include "url/gurl.h"

class BrowserWindowInterface;
class Profile;
class ScopedKeepAlive;
namespace content {
class WebContents;
}
namespace views {
class DialogDelegate;
class Widget;
}

namespace fly {
class BookmarkService;
class FlyExtensions;
class FlyWindowController;
class FrequentSitesService;
class NewTabHistory;
class TabMoveTransaction;
class UserContentService;
class WorkspaceCloseTransaction;

// Application-wide state of native Fly: the product services every window
// shares, the workspace catalogue, and the operations that span windows
// (opening a workspace, moving a tab to another workspace, removing one).
// A workspace is a Chromium profile and opens in its own windows; each
// decorated window has a FlyWindowController, owned here.
class FlyApp : public BrowserCollectionObserver {
 public:
  static FlyApp& Get();
  static FlyApp* GetIfCreated();

  FlyApp();
  ~FlyApp() override;
  FlyApp(const FlyApp&) = delete;
  FlyApp& operator=(const FlyApp&) = delete;

  WorkspaceService& workspaces() { return workspaces_; }
  const WorkspaceService& workspaces() const { return workspaces_; }
  CommandService& commands() { return commands_; }
  PreferencesService& preferences() { return preferences_; }
  const PreferencesService& preferences() const { return preferences_; }
  BookmarkService& bookmarks() { return *bookmarks_; }
  NewTabHistory& new_tab_history() { return *new_tab_history_; }
  // Clearing history runs once for all normal workspaces.
  void ClearHistory();
  bool history_clear_running() const { return history_clear_running_; }
  FlyExtensions& extensions() { return *extensions_; }
  UserContentService& user_content() { return *user_content_; }
  bool closing() const { return closing_; }

  // Windows. Attach runs once the window's BrowserWidget exists.
  void PrepareProfile(Profile* profile);
  FlyWindowController* AttachWindow(BrowserWindowInterface* browser,
                                    views::Widget* widget);
  // Forgets a controller whose window is going away and deletes it later.
  void ReleaseWindow(FlyWindowController* window);
  FlyWindowController* WindowFor(const BrowserWindowInterface* browser) const;
  std::vector<FlyWindowController*> Windows() const;
  // Decorated browser windows, most recently active first.
  std::vector<BrowserWindowInterface*> BrowsersByActivation() const;
  FlyWindowController* LastActiveWindow() const;
  void BroadcastWorkspaceRefresh();
  void BroadcastExtensionRefresh();

  // The catalogue entry of a profile's original profile, if any.
  std::optional<Workspace> WorkspaceFor(const Profile* profile) const;
  Profile* ActiveProfile() const;
  std::vector<Profile*> WorkspaceProfiles() const;

  // Workspaces. Opening one activates its most recent window, or opens a
  // window when it has none.
  void OpenWorkspace(std::string id);
  void OpenAdjacentWorkspace(const BrowserWindowInterface* from, int step);
  void CreateWorkspace(std::string name = {});
  std::string NextWorkspaceName() const;
  void ReorderWorkspace(std::string id, int direction);
  bool CanRemoveWorkspace(std::string_view id,
                          const base::FilePath& path) const;
  void BeginWorkspaceRemoval(std::string id, base::FilePath path);
  bool workspace_removal_pending() const { return bool(workspace_close_); }
  bool creating_profile() const { return creating_profile_; }
  bool CanTogglePrivate(BrowserWindowInterface* browser) const;
  void TogglePrivate(BrowserWindowInterface* browser);

  // Moving a tab to another workspace.
  bool CanMoveTab(BrowserWindowInterface* browser,
                  content::WebContents* contents) const;
  std::vector<PaletteEntry> TabMoveEntries(
      BrowserWindowInterface* source) const;
  bool MoveTab(BrowserWindowInterface* source,
               content::WebContents* contents,
               std::string_view action);
  bool tab_move_pending() const { return bool(tab_move_); }

  // Pages opened by Fly itself (What's New, downloads, tool pages).
  void OpenInNewTab(GURL url, BrowserWindowInterface* browser = nullptr);
  void OpenInNewWindow(GURL url, Profile* profile = nullptr);

  // Fly's new tab page: bookmarks and frequent sites (none in private
  // workspaces), and hiding a frequent site from its list.
  base::DictValue NewTabLists(bool off_the_record,
                              const std::vector<HistoryEntry>& history) const;
  bool HideFrequentSite(const std::string& url);

  // Status and diagnostics.
  void ShowStatus(std::u16string text);
  void RecordDiagnostic(std::string level,
                        std::string category,
                        std::u16string message);
  DiagnosticsSession CurrentDiagnostics();
  void ClearDiagnostics() { diagnostics_events_.clear(); }

  // Updates and desktop integration.
  void CheckUpdates(bool manual, BrowserWindowInterface* origin = nullptr);
  void InstallUpdate();
  void UpdateDesktopIntegration(bool make_default);

  // Background tabs.
  void ResumeAllTabs();

  // BrowserCollectionObserver:
  void OnBrowserCreated(BrowserWindowInterface* browser) override;
  // After private mode is turned off, brings back the workspace window.
  void OnBrowserClosed(BrowserWindowInterface* browser) override;
  // The active workspace is the one of the Fly window used last. Chromium
  // also counts a window as used when it is shown, before the window
  // manager activates it.
  void OnBrowserActivated(BrowserWindowInterface* browser) override;
  // Startup restore shows the window of every restored workspace. The
  // workspace used last before quitting stays active and in front.
  void OnSessionRestored(Profile* profile, int tabs);

 private:
  void OnPreferencesChanged();
  void OnBookmarksChanged();
  void OnWorkspacesChanged();
  void ApplyFlyPalette();
  void ApplyProfilePreferencesEverywhere();
  void UpdateSuspensionPolicy();
  void SuspendIdleTabs();
  void OnProfileReady(std::string id, Profile* profile);
  bool WorkspaceMatchesProfile(std::string_view id, Profile* profile) const;
  void FinishWorkspaceRemoval(std::string id,
                              base::FilePath path,
                              bool success);
  bool ValidateTabMove(base::WeakPtr<BrowserWindowInterface> source_browser,
                       base::WeakPtr<content::WebContents> source,
                       const std::string& destination_id,
                       const base::FilePath& destination_path,
                       bool require_selection) const;
  void FinishTabMove(bool success,
                     base::WeakPtr<BrowserWindowInterface> destination,
                     base::WeakPtr<content::WebContents> contents);
  void OnClosingAllBrowsers(bool closing);
  void RunFirstWindowTasks();
  void MaybeShowWhatsNew();
  void MaybeOfferDesktopIntegration();
  // Keeps the application running while windows are replaced.
  void HoldKeepAlive();
  void ReleaseKeepAliveIfIdle();

  WorkspaceService workspaces_;
  CommandService commands_;
  PreferencesService preferences_;
  std::unique_ptr<BookmarkService> bookmarks_;
  std::unique_ptr<FrequentSitesService> frequent_sites_;
  std::unique_ptr<NewTabHistory> new_tab_history_;
  std::unique_ptr<HistoryService> history_clear_service_;
  std::unique_ptr<FlyExtensions> extensions_;
  std::unique_ptr<UserContentService> user_content_;
  SiteZoom site_zoom_;
  PrefChangeRegistrar preferences_registrar_;
  std::optional<ThemePreferences> applied_theme_;
  std::optional<std::string> applied_profile_preferences_;
  base::RepeatingTimer suspension_timer_;
  std::vector<DiagnosticEvent> diagnostics_events_;
  UpdateCheckResult update_;
  bool update_installing_ = false;
  bool history_clear_running_ = false;
  bool update_checked_ = false;
  bool whats_new_checked_ = false;
  std::unique_ptr<views::DialogDelegate> offer_delegate_;
  std::unique_ptr<views::Widget> offer_widget_;
  std::unique_ptr<WorkspaceCloseTransaction> workspace_close_;
  std::unique_ptr<TabMoveTransaction> tab_move_;
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  bool creating_profile_ = false;
  bool closing_ = false;
  bool terminating_ = false;
  std::map<const BrowserWindowInterface*, std::unique_ptr<FlyWindowController>>
      windows_;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_{this};
  base::CallbackListSubscription close_subscription_;
  base::CallbackListSubscription terminating_subscription_;
  base::CallbackListSubscription session_restored_subscription_;
  // Private profiles being closed by "leave private mode", and the workspace
  // whose window comes back once they have closed. Keys are compared only.
  std::map<const Profile*, std::string> private_returns_;
  base::WeakPtrFactory<FlyApp> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_FLY_APP_H_
