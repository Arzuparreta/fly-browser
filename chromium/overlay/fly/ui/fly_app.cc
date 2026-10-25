// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_app.h"

#include <algorithm>
#include <utility>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/no_destructor.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/version.h"
#include "build/build_config.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/lifetime/termination_notification.h"
#include "chrome/browser/prefs/incognito_mode_prefs.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_attributes_storage.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/common/pref_names.h"
#include "components/keep_alive_registry/keep_alive_types.h"
#include "components/keep_alive_registry/scoped_keep_alive.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/bookmarks/frequent_sites_service.h"
#include "fly/fly_version.h"
#include "fly/ui/fly_extensions.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/new_tab_ui.h"
#include "fly/ui/tab_move_transaction.h"
#include "fly/ui/user_content.h"
#include "fly/ui/workspace_close_transaction.h"
#include "ui/base/base_window.h"
#include "ui/native_theme/native_theme.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/dialog_delegate.h"

#if BUILDFLAG(IS_LINUX)
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/gfx/x/connection.h"
#include "ui/gfx/x/xproto.h"
#include "ui/ozone/public/ozone_platform.h"
#endif

namespace fly {
namespace {
std::unique_ptr<FlyApp>& Instance() {
  static base::NoDestructor<std::unique_ptr<FlyApp>> instance;
  return *instance;
}

void RaiseBrowserWindow(BrowserWindowInterface* browser) {
  auto* window = browser->GetWindow();
  if (window->IsMinimized())
    window->Restore();
  window->Show();
  window->Activate();
}
}  // namespace

FlyApp& FlyApp::Get() {
  if (!Instance())
    Instance() = std::make_unique<FlyApp>();
  return *Instance();
}
FlyApp* FlyApp::GetIfCreated() {
  return Instance().get();
}

FlyApp::FlyApp()
    : workspaces_(*g_browser_process->local_state()),
      commands_(*g_browser_process->local_state()),
      preferences_(*g_browser_process->local_state()),
      bookmarks_(
          std::make_unique<BookmarkService>(*g_browser_process->local_state())) {
  auto& local_state = *g_browser_process->local_state();
  auto& profiles = *g_browser_process->profile_manager();
  frequent_sites_ = std::make_unique<FrequentSitesService>(local_state);
  RegisterFlyNewTabPage();
  RegisterFlyFonts();
  new_tab_history_ =
      std::make_unique<NewTabHistory>(profiles, workspaces_, local_state);
  history_clear_service_ = std::make_unique<HistoryService>(profiles, workspaces_);
  extensions_ = std::make_unique<FlyExtensions>(
      preferences_,
      base::BindRepeating(&FlyApp::BroadcastExtensionRefresh,
                          weak_factory_.GetWeakPtr()),
      base::BindRepeating(&FlyApp::ShowStatus, weak_factory_.GetWeakPtr()));
  user_content_ = std::make_unique<UserContentService>(
      base::BindRepeating(&FlyApp::ShowStatus, weak_factory_.GetWeakPtr()));
  browser_collection_.Observe(GlobalBrowserCollection::GetInstance());
  preferences_registrar_.Init(&local_state);
  preferences_registrar_.Add(
      kPreferencesPref, base::BindRepeating(&FlyApp::OnPreferencesChanged,
                                            weak_factory_.GetWeakPtr()));
  preferences_registrar_.Add(
      kBookmarksPref, base::BindRepeating(&FlyApp::OnBookmarksChanged,
                                          weak_factory_.GetWeakPtr()));
  preferences_registrar_.Add(
      kWorkspacesPref, base::BindRepeating(&FlyApp::OnWorkspacesChanged,
                                           weak_factory_.GetWeakPtr()));
  close_subscription_ = chrome::AddClosingAllBrowsersCallback(
      base::BindRepeating(&FlyApp::OnClosingAllBrowsers,
                          weak_factory_.GetWeakPtr()));
  session_restored_subscription_ =
      SessionRestore::RegisterOnSessionRestoredCallback(base::BindRepeating(
          &FlyApp::OnSessionRestored, base::Unretained(this)));
  // Release Local State and profile observers before Chromium tears them
  // down; the controllers of the last windows are already gone or going.
  terminating_subscription_ =
      browser_shutdown::AddAppTerminatingCallback(base::BindOnce(
          [](base::WeakPtr<FlyApp> app) {
            if (!app)
              return;
            app->closing_ = true;
            app->terminating_ = true;
            app->suspension_timer_.Stop();
            app->preferences_registrar_.RemoveAll();
            app->tab_move_.reset();
            app->workspace_close_.reset();
            app->keep_alive_.reset();
            if (app->windows_.empty()) {
              base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
                  FROM_HERE, base::BindOnce([] { Instance().reset(); }));
            }
          },
          weak_factory_.GetWeakPtr()));
  OnPreferencesChanged();
}

FlyApp::~FlyApp() {
  closing_ = true;
  browser_collection_.Reset();
  weak_factory_.InvalidateWeakPtrs();
  tab_move_.reset();
  workspace_close_.reset();
  offer_widget_.reset();
  offer_delegate_.reset();
  windows_.clear();
  suspension_timer_.Stop();
  preferences_registrar_.RemoveAll();
  keep_alive_.reset();
  extensions_.reset();
  user_content_.reset();
  new_tab_history_.reset();
  history_clear_service_.reset();
}

void FlyApp::PrepareProfile(Profile* profile) {
  if (!profile->IsOffTheRecord()) {
    // Incoming native startup and extension windows keep their real Profile.
    // Never bind a new display name to an already retired directory.
    const auto directory = profile->GetPath().BaseName().AsUTF8Unsafe();
    if (const auto catalog = workspaces_.Read();
        catalog && std::ranges::find(catalog->items, directory,
                                     &Workspace::profile_directory) ==
                       catalog->items.end()) {
      workspaces_.Add(
          "Workspace " + base::NumberToString(catalog->items.size() + 1),
          directory);
    }
  }
  ApplyProfilePreferences(profile, preferences_);
  if (!profile->IsOffTheRecord()) {
    extensions_->AddProfile(profile);
    user_content_->AddProfile(profile);
    site_zoom_.AddProfile(profile);
  }
}

FlyWindowController* FlyApp::AttachWindow(BrowserWindowInterface* browser,
                                          views::Widget* widget) {
  CHECK(!windows_.contains(browser));
  PrepareProfile(browser->GetProfile());
  auto window = std::make_unique<FlyWindowController>(browser, widget);
  auto* raw = window.get();
  windows_.emplace(browser, std::move(window));
  BroadcastWorkspaceRefresh();
  // Session restore is still running; act once it has finished.
  if (!update_checked_ &&
      !base::CommandLine::ForCurrentProcess()->HasSwitch("test-type")) {
    update_checked_ = true;
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce(&FlyApp::RunFirstWindowTasks,
                                  weak_factory_.GetWeakPtr()));
  }
  return raw;
}

void FlyApp::ReleaseWindow(FlyWindowController* window) {
  for (auto it = windows_.begin(); it != windows_.end(); ++it) {
    if (it->second.get() != window)
      continue;
    // Called from the window's own destruction notification: forget it now
    // (a new Browser may reuse the address) and delete it afterwards.
    base::SingleThreadTaskRunner::GetCurrentDefault()->DeleteSoon(
        FROM_HERE, std::move(it->second));
    windows_.erase(it);
    break;
  }
  BroadcastWorkspaceRefresh();
  if ((closing_ || terminating_) && windows_.empty()) {
    base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
        FROM_HERE, base::BindOnce([] { Instance().reset(); }));
  }
}

FlyWindowController* FlyApp::WindowFor(
    const BrowserWindowInterface* browser) const {
  const auto it = windows_.find(browser);
  return it == windows_.end() ? nullptr : it->second.get();
}

std::vector<FlyWindowController*> FlyApp::Windows() const {
  std::vector<FlyWindowController*> result;
  for (const auto& [browser, window] : windows_)
    result.push_back(window.get());
  return result;
}

std::vector<BrowserWindowInterface*> FlyApp::BrowsersByActivation() const {
  std::vector<BrowserWindowInterface*> result;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* browser) {
        if (windows_.contains(browser) && !browser->IsDeleteScheduled())
          result.push_back(browser);
        return true;
      },
      BrowserCollection::Order::kActivation);
  return result;
}

FlyWindowController* FlyApp::LastActiveWindow() const {
  const auto browsers = BrowsersByActivation();
  return browsers.empty() ? nullptr : WindowFor(browsers.front());
}

void FlyApp::OnBrowserActivated(BrowserWindowInterface* browser) {
  if (closing_ || !windows_.contains(browser) ||
      browser->GetProfile()->IsOffTheRecord() ||
      std::ranges::any_of(windows_, [](const auto& entry) {
        return SessionRestore::IsRestoring(entry.first->GetProfile());
      }))
    return;
  const auto workspace = WorkspaceFor(browser->GetProfile());
  const auto catalog = workspaces_.Read();
  if (workspace && catalog && catalog->active_id != workspace->id)
    workspaces_.Activate(workspace->id);
}

void FlyApp::OnSessionRestored(Profile* profile, int tabs) {
  const auto catalog = workspaces_.Read();
  if (closing_ || !catalog)
    return;
  const auto browsers = BrowsersByActivation();
  for (auto* browser : browsers) {
    const auto workspace = WorkspaceFor(browser->GetProfile());
    if (browser->GetProfile()->IsOffTheRecord() || !workspace ||
        workspace->id != catalog->active_id)
      continue;
    if (browser != browsers.front())
      RaiseBrowserWindow(browser);
    return;
  }
}

void FlyApp::BroadcastWorkspaceRefresh() {
  for (const auto& [browser, window] : windows_)
    window->ScheduleWorkspaceRefresh();
}

void FlyApp::BroadcastExtensionRefresh() {
  for (const auto& [browser, window] : windows_)
    window->ScheduleExtensionRefresh();
}

std::optional<Workspace> FlyApp::WorkspaceFor(const Profile* profile) const {
  if (!profile)
    return std::nullopt;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return std::nullopt;
  const auto original = profile->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  for (const auto& item : catalog->items) {
    if (root.AppendASCII(item.profile_directory) == original)
      return item;
  }
  return std::nullopt;
}

Profile* FlyApp::ActiveProfile() const {
  if (auto* window = LastActiveWindow(); window && window->browser())
    return window->browser()->GetProfile();
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return nullptr;
  const auto item =
      std::ranges::find(catalog->items, catalog->active_id, &Workspace::id);
  auto* manager = g_browser_process->profile_manager();
  return item == catalog->items.end()
             ? nullptr
             : manager->GetProfileByPath(manager->user_data_dir().AppendASCII(
                   item->profile_directory));
}

std::vector<Profile*> FlyApp::WorkspaceProfiles() const {
  std::vector<Profile*> profiles;
  if (const auto catalog = workspaces_.Read()) {
    auto* manager = g_browser_process->profile_manager();
    for (const auto& item : catalog->items) {
      if (auto* profile = manager->GetProfileByPath(
              manager->user_data_dir().AppendASCII(item.profile_directory)))
        profiles.push_back(profile);
    }
  }
  for (const auto& [browser, window] : windows_) {
    auto* profile = const_cast<BrowserWindowInterface*>(browser)->GetProfile();
    if (std::ranges::find(profiles, profile) == profiles.end())
      profiles.push_back(profile);
  }
  return profiles;
}

void FlyApp::OpenWorkspace(std::string id) {
  if (closing_ || creating_profile_)
    return;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  auto* manager = g_browser_process->profile_manager();
  const auto path =
      manager->user_data_dir().AppendASCII(item->profile_directory);
  for (auto* browser : BrowsersByActivation()) {
    if (!browser->GetProfile()->IsOffTheRecord() &&
        browser->GetProfile()->GetPath() == path) {
      RaiseBrowserWindow(browser);
      return;
    }
  }
  creating_profile_ = true;
  // Opening a workspace can be what replaces the last window.
  HoldKeepAlive();
  manager->CreateProfileAsync(
      path, base::BindOnce(&FlyApp::OnProfileReady, weak_factory_.GetWeakPtr(),
                           std::move(id)));
}

void FlyApp::OpenAdjacentWorkspace(const BrowserWindowInterface* from,
                                   int step) {
  const auto catalog = workspaces_.Read();
  if (!catalog || catalog->items.empty())
    return;
  std::string current = catalog->active_id;
  if (from) {
    if (const auto workspace =
            WorkspaceFor(const_cast<BrowserWindowInterface*>(from)
                             ->GetProfile()))
      current = workspace->id;
  }
  const auto found =
      std::ranges::find(catalog->items, current, &Workspace::id);
  const size_t count = catalog->items.size();
  const size_t index =
      found == catalog->items.end() ? 0 : found - catalog->items.begin();
  OpenWorkspace(
      catalog->items[(index + (step < 0 ? count - 1 : 1)) % count].id);
}

std::string FlyApp::NextWorkspaceName() const {
  const auto catalog = workspaces_.Read();
  std::string name;
  size_t number = catalog ? catalog->items.size() + 1 : 1;
  do {
    name = "Workspace " + base::NumberToString(number++);
  } while (catalog &&
           std::ranges::any_of(catalog->items, [&](const Workspace& item) {
             return item.name == name;
           }));
  return name;
}

void FlyApp::CreateWorkspace(std::string name) {
  if (closing_ || creating_profile_ || !workspaces_.CanEdit())
    return;
  if (!workspaces_.Read())
    return;
  auto* manager = g_browser_process->profile_manager();
  const auto path = manager->GenerateNextProfileDirectoryPath();
  // Reserve a unique directory before asynchronous creation. A failed load
  // retains the entry for retry; it must not silently attach another Profile.
  if (name.empty())
    name = NextWorkspaceName();
  const auto added = workspaces_.Add(name, path.BaseName().AsUTF8Unsafe());
  if (!added)
    return;
  OpenWorkspace(added->id);
}

bool FlyApp::WorkspaceMatchesProfile(std::string_view id,
                                     Profile* profile) const {
  const auto catalog = workspaces_.Read();
  if (!profile || profile->IsOffTheRecord() || !catalog ||
      !profile->AllowsBrowserWindows())
    return false;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  return item != catalog->items.end() &&
         profile->GetPath() == g_browser_process->profile_manager()
                                   ->user_data_dir()
                                   .AppendASCII(item->profile_directory);
}

void FlyApp::OnProfileReady(std::string id, Profile* profile) {
  creating_profile_ = false;
  if (closing_ || !WorkspaceMatchesProfile(id, profile)) {
    ReleaseKeepAliveIfIdle();
    return;
  }
  BrowserWindowCreateParams params(*profile, true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(
      std::move(params),
      base::BindOnce(
          [](base::WeakPtr<FlyApp> app, std::string id,
             BrowserWindowInterface* browser) {
            if (!browser) {
              if (app)
                app->ReleaseKeepAliveIfIdle();
              return;
            }
            if (!app || app->closing_ ||
                !app->WorkspaceMatchesProfile(id, browser->GetProfile())) {
              browser->GetWindow()->Close();
              return;
            }
            OpenNativeBlankTab(browser);
            RaiseBrowserWindow(browser);
            app->ReleaseKeepAliveIfIdle();
          },
          weak_factory_.GetWeakPtr(), std::move(id)));
}

void FlyApp::ReorderWorkspace(std::string id, int direction) {
  if (closing_ || !workspaces_.CanEdit() || (direction != -1 && direction != 1))
    return;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  if (item == catalog->items.end())
    return;
  const auto index = item - catalog->items.begin();
  const auto target = index + direction;
  if (target < 0 ||
      target >= static_cast<std::ptrdiff_t>(catalog->items.size()))
    return;
  std::vector<std::string> order;
  for (const auto& workspace : catalog->items)
    order.push_back(workspace.id);
  std::swap(order[index], order[target]);
  workspaces_.Reorder(order);
}

bool FlyApp::CanRemoveWorkspace(std::string_view id,
                                const base::FilePath& path) const {
  if (closing_ || creating_profile_ || tab_move_ || !workspaces_.CanEdit())
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog || catalog->items.size() < 2)
    return false;
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  return item != catalog->items.end() &&
         path ==
             g_browser_process->profile_manager()->user_data_dir().AppendASCII(
                 item->profile_directory);
}

void FlyApp::BeginWorkspaceRemoval(std::string id, base::FilePath path) {
  if (workspace_close_ || !CanRemoveWorkspace(id, path))
    return;
  // Its windows may be the last ones open.
  HoldKeepAlive();
  workspace_close_ = std::make_unique<WorkspaceCloseTransaction>(
      path,
      base::BindRepeating(
          [](base::WeakPtr<FlyApp> app, const std::string& id,
             const base::FilePath& path) {
            return app && app->CanRemoveWorkspace(id, path);
          },
          weak_factory_.GetWeakPtr(), id, path),
      base::BindOnce(&FlyApp::FinishWorkspaceRemoval,
                     weak_factory_.GetWeakPtr(), id, path));
  BroadcastWorkspaceRefresh();
  workspace_close_->Start();
}

void FlyApp::FinishWorkspaceRemoval(std::string id,
                                    base::FilePath path,
                                    bool success) {
  const bool removed =
      success && CanRemoveWorkspace(id, path) && workspaces_.Remove(id);
  if (removed) {
    auto* local_state = g_browser_process->local_state();
    const auto directory = path.BaseName().AsUTF8Unsafe();
    auto active = local_state->GetList(prefs::kProfilesLastActive).Clone();
    active.EraseIf([&](const base::Value& value) {
      return value.is_string() &&
             base::EqualsCaseInsensitiveASCII(value.GetString(), directory);
    });
    local_state->SetList(prefs::kProfilesLastActive, std::move(active));
    const auto catalog = workspaces_.Read();
    for (const auto& item : catalog->items)
      if (item.id == catalog->active_id &&
          local_state->GetFilePath(prefs::kProfileLastUsed) == path.BaseName())
        local_state->SetFilePath(
            prefs::kProfileLastUsed,
            base::FilePath::FromUTF8Unsafe(item.profile_directory));
    // Remove only the profile picker's metadata. ProfileManager retains native
    // lifetime ownership; neither this operation nor retirement deletes files.
    g_browser_process->profile_manager()
        ->GetProfileAttributesStorage()
        .RemoveProfile(path);
  }
  workspace_close_.reset();
  BroadcastWorkspaceRefresh();
  if (!removed) {
    ShowStatus(u"Workspace kept. Closing was cancelled or its data changed.");
    ReleaseKeepAliveIfIdle();
    return;
  }
  // Without any window left, open the active workspace instead of quitting.
  const auto catalog = workspaces_.Read();
  if (!closing_ && BrowsersByActivation().empty() && catalog &&
      !catalog->active_id.empty())
    OpenWorkspace(catalog->active_id);
  ReleaseKeepAliveIfIdle();
}

bool FlyApp::CanTogglePrivate(BrowserWindowInterface* browser) const {
  auto* profile = browser ? browser->GetProfile() : ActiveProfile();
  return profile && profile->AllowsBrowserWindows() &&
         (profile->IsOffTheRecord() ||
          (browser ? chrome::IsCommandEnabled(browser, IDC_NEW_INCOGNITO_WINDOW)
                   : IncognitoModePrefs::IsIncognitoAllowed(profile)));
}

void FlyApp::TogglePrivate(BrowserWindowInterface* browser) {
  if (!CanTogglePrivate(browser))
    return;
  auto* profile = browser ? browser->GetProfile() : ActiveProfile();
  if (profile->IsOffTheRecord()) {
    // Leaving private mode returns to the workspace's own window.
    auto* original = profile->GetOriginalProfile();
    const auto browsers = BrowsersByActivation();
    if (std::ranges::none_of(browsers, [&](BrowserWindowInterface* other) {
          return other->GetProfile() == original;
        })) {
      if (const auto workspace = WorkspaceFor(original))
        OpenWorkspace(workspace->id);
    }
    // The native close coordinator resolves all beforeunload decisions before
    // closing this OTR profile's windows. Cancellation leaves them intact.
    chrome::CloseAllBrowsersWithIncognitoProfile(profile,
                                                 /*skip_beforeunload=*/false);
    return;
  }
  auto* private_profile = profile->GetPrimaryOTRProfile(true);
  for (auto* other : BrowsersByActivation()) {
    if (other->GetProfile() == private_profile) {
      RaiseBrowserWindow(other);
      return;
    }
  }
  // Standard Chromium creation owns OTR lifetime, extension incognito access,
  // permissions, downloads and shutdown. No normal navigation/state is copied.
  BrowserWindowCreateParams params(*private_profile, true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params),
                      base::BindOnce(
                          [](base::WeakPtr<FlyApp> app,
                             BrowserWindowInterface* private_browser) {
                            if (!app || app->closing_ || !private_browser)
                              return;
                            OpenNativeBlankTab(private_browser);
                            RaiseBrowserWindow(private_browser);
                          },
                          weak_factory_.GetWeakPtr()));
}

bool FlyApp::CanMoveTab(BrowserWindowInterface* browser,
                        content::WebContents* contents) const {
  if (!browser || !contents || closing_ || creating_profile_ || tab_move_ ||
      workspace_close_ || contents->GetController().GetPendingEntry() ||
      !windows_.contains(browser) || browser->IsDeleteScheduled() ||
      !TabMoveTransaction::CanMoveUrl(contents->GetLastCommittedURL()))
    return false;
  auto* tabs = browser->GetTabStripModel();
  const auto index = tabs->GetIndexOfWebContents(contents);
  if (index == TabStripModel::kNoTab ||
      !tabs->IsContextMenuCommandEnabled(index, TabStripModel::CommandCloseTab))
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return false;
  const auto original = browser->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  const bool mapped =
      std::ranges::any_of(catalog->items, [&](const Workspace& item) {
        return root.AppendASCII(item.profile_directory) == original;
      });
  return mapped && !IsWorkspaceRetiring(original) &&
         std::ranges::any_of(catalog->items, [&](const Workspace& item) {
           const auto path = root.AppendASCII(item.profile_directory);
           return path != original && !IsWorkspaceRetiring(path);
         });
}

std::vector<PaletteEntry> FlyApp::TabMoveEntries(
    BrowserWindowInterface* source) const {
  std::vector<PaletteEntry> result;
  if (!source)
    return result;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return result;
  const auto original = source->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  for (const auto& item : catalog->items) {
    const auto path = root.AppendASCII(item.profile_directory);
    if (path != original && !IsWorkspaceRetiring(path)) {
      const auto action = "move:" + item.id;
      const auto label =
          (source->GetProfile()->IsOffTheRecord() ? "Private · " : "") +
          item.name;
      result.push_back({action, action, label, "Workspaces", {}, {}});
    }
  }
  return result;
}

bool FlyApp::ValidateTabMove(
    base::WeakPtr<BrowserWindowInterface> source_browser,
    base::WeakPtr<content::WebContents> source,
    const std::string& destination_id,
    const base::FilePath& destination_path,
    bool require_selection) const {
  if (closing_ || workspace_close_ || !source_browser || !source ||
      !windows_.contains(source_browser.get()) ||
      source_browser->IsDeleteScheduled() ||
      source_browser->GetTabStripModel()->GetIndexOfWebContents(source.get()) ==
          TabStripModel::kNoTab ||
      (require_selection &&
       source_browser->GetTabStripModel()->GetActiveWebContents() !=
           source.get()))
    return false;
  const auto catalog = workspaces_.Read();
  if (!catalog)
    return false;
  const auto original =
      source_browser->GetProfile()->GetOriginalProfile()->GetPath();
  const auto root = g_browser_process->profile_manager()->user_data_dir();
  if (IsWorkspaceRetiring(original) || IsWorkspaceRetiring(destination_path) ||
      destination_path == original ||
      std::ranges::none_of(catalog->items, [&](const Workspace& item) {
        return root.AppendASCII(item.profile_directory) == original;
      }))
    return false;
  const auto destination =
      std::ranges::find(catalog->items, destination_id, &Workspace::id);
  return destination != catalog->items.end() &&
         destination_path == root.AppendASCII(destination->profile_directory);
}

bool FlyApp::MoveTab(BrowserWindowInterface* source,
                     content::WebContents* contents,
                     std::string_view action) {
  if (!CanMoveTab(source, contents) ||
      std::ranges::none_of(TabMoveEntries(source),
                           [&](const PaletteEntry& entry) {
                             return entry.action == action;
                           }))
    return false;
  const auto id = std::string(action.substr(5));
  const auto catalog = workspaces_.Read();
  const auto item = std::ranges::find(catalog->items, id, &Workspace::id);
  const auto path =
      g_browser_process->profile_manager()->user_data_dir().AppendASCII(
          item->profile_directory);
  auto source_browser = source->GetWeakPtr();
  auto weak_contents = contents->GetWeakPtr();
  tab_move_ = std::make_unique<TabMoveTransaction>(
      source, contents, path,
      base::BindRepeating(
          [](base::WeakPtr<FlyApp> app,
             base::WeakPtr<BrowserWindowInterface> browser,
             base::WeakPtr<content::WebContents> contents,
             const std::string& id, const base::FilePath& path,
             bool require_selection) {
            return app && app->ValidateTabMove(browser, contents, id, path,
                                               require_selection);
          },
          weak_factory_.GetWeakPtr(), source_browser, weak_contents, id, path),
      base::BindOnce(&FlyApp::FinishTabMove, weak_factory_.GetWeakPtr()));
  if (auto* window = WindowFor(source))
    window->ShowStatus(u"Preparing destination workspace…");
  tab_move_->Start();
  return true;
}

void FlyApp::FinishTabMove(bool success,
                           base::WeakPtr<BrowserWindowInterface> destination,
                           base::WeakPtr<content::WebContents> contents) {
  tab_move_.reset();
  BroadcastWorkspaceRefresh();
  if (success && !closing_ && destination && contents &&
      !destination->IsDeleteScheduled()) {
    auto* tabs = destination->GetTabStripModel();
    const auto index = tabs->GetIndexOfWebContents(contents.get());
    if (index != TabStripModel::kNoTab) {
      tabs->ActivateTabAt(index);
      RaiseBrowserWindow(destination.get());
    }
  } else if (!closing_) {
    ShowStatus(u"Tab move cancelled. The destination or source changed.");
  }
  ReleaseKeepAliveIfIdle();
}

void FlyApp::OpenInNewTab(GURL url, BrowserWindowInterface* browser) {
  if (closing_)
    return;
  if (!browser) {
    if (auto* window = LastActiveWindow())
      browser = window->browser();
  }
  if (browser) {
    NavigateParams params(browser, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    params.window_action = NavigateParams::WindowAction::kShowWindow;
    Navigate(&params);
    return;
  }
  auto* profile = ActiveProfile();
  if (!profile)
    return;
  NavigateParams params(profile, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
}

void FlyApp::OpenInNewWindow(GURL url, Profile* profile) {
  if (closing_)
    return;
  if (!profile)
    profile = ActiveProfile();
  if (!profile) {
    ShowStatus(u"Open a workspace before opening this page.");
    return;
  }
  NavigateParams params(profile, url, ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
  // Tool pages open in a popup window of the workspace profile.
  params.disposition = WindowOpenDisposition::NEW_POPUP;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  Navigate(&params);
}

base::DictValue FlyApp::NewTabLists(
    bool off_the_record,
    const std::vector<HistoryEntry>& history) const {
  // Only web pages are listed.
  const auto link = [](const std::string& url, const std::string& title) {
    const GURL target(url);
    if (!target.is_valid() || !target.SchemeIsHTTPOrHTTPS())
      return std::optional<base::DictValue>();
    return std::optional<base::DictValue>(
        base::DictValue()
            .Set("url", target.spec())
            .Set("title", title.empty() ? target.host() : title));
  };
  base::ListValue bookmarks;
  base::ListValue sites;
  const bool show_sites =
      !off_the_record && preferences_.ReadFrequentSitesVisible().value_or(true);
  if (!off_the_record) {
    if (const auto saved = bookmarks_->Read()) {
      for (const auto& bookmark : *saved) {
        if (auto entry = link(bookmark.url, bookmark.title))
          bookmarks.Append(std::move(*entry));
      }
    }
  }
  if (show_sites) {
    std::vector<SiteVisit> visits;
    for (const auto& row : history)
      visits.push_back({row.url.spec(), row.visited});
    if (const auto frequent = frequent_sites_->Read(visits)) {
      for (const auto& site : *frequent) {
        if (auto entry = link(site.url, site.title))
          sites.Append(std::move(*entry));
      }
    }
  }
  return base::DictValue()
      .Set("showBookmarks", !off_the_record)
      .Set("showSites", show_sites)
      .Set("bookmarks", std::move(bookmarks))
      .Set("sites", std::move(sites));
}

bool FlyApp::HideFrequentSite(const std::string& url) {
  return !closing_ && frequent_sites_->Hide(url);
}

void FlyApp::ClearHistory() {
  if (history_clear_running_ || closing_)
    return;
  history_clear_running_ = true;
  history_clear_service_->Clear(base::BindOnce(
      [](base::WeakPtr<FlyApp> app, bool cleared) {
        if (!app)
          return;
        // A cancelled quit clears `closing_` again; the lock must not
        // outlive the operation.
        app->history_clear_running_ = false;
        if (app->closing_)
          return;
        app->ShowStatus(cleared
                            ? u"Browsing history cleared."
                            : u"History could not be cleared in every "
                              u"workspace.");
        for (auto* window : app->Windows())
          window->OnHistoryChanged();
      },
      weak_factory_.GetWeakPtr()));
}

void FlyApp::ShowStatus(std::u16string text) {
  if (auto* window = LastActiveWindow())
    window->ShowStatus(std::move(text));
  else
    RecordDiagnostic("info", "status", text);
}

void FlyApp::RecordDiagnostic(std::string level,
                              std::string category,
                              std::u16string message) {
  if (!preferences_.ReadBool("enable_diagnostics").value_or(false))
    return;
  diagnostics_events_.push_back({base::Time::Now(), std::move(level),
                                 std::move(category), std::move(message)});
  if (diagnostics_events_.size() > 500)
    diagnostics_events_.erase(diagnostics_events_.begin());
}

DiagnosticsSession FlyApp::CurrentDiagnostics() {
  DiagnosticsSession session;
  if (const auto catalog = workspaces_.Read()) {
    session.workspaces = catalog->items.size();
    for (const auto& item : catalog->items)
      if (item.id == catalog->active_id)
        session.active_workspace = base::UTF8ToUTF16(item.name);
  }
  for (auto* browser : BrowsersByActivation()) {
    auto* model = browser->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      auto* contents = model->GetWebContentsAt(i);
      ++session.tabs;
      if (NativeTabSuspensionRequested(contents))
        ++session.suspended;
      else if (!contents->WasDiscarded())
        ++session.loaded;
    }
  }
  session.recording =
      preferences_.ReadBool("enable_diagnostics").value_or(false);
  session.events = diagnostics_events_;
  return session;
}

void FlyApp::CheckUpdates(bool manual, BrowserWindowInterface* origin) {
  if (manual)
    ShowStatus(u"Checking for updates…");
  CheckForUpdates(base::BindOnce(
      [](base::WeakPtr<FlyApp> app, bool manual,
         base::WeakPtr<BrowserWindowInterface> origin,
         UpdateCheckResult result) {
        if (!app || app->closing_)
          return;
        const auto status = [&](std::u16string text, std::string version) {
          for (auto* window : app->Windows())
            window->SetUpdateStatus(text, version);
        };
        if (!result.ok) {
          status(u"Could not check for updates.", "");
          if (manual)
            app->ShowStatus(u"Could not check for updates.");
          return;
        }
        app->update_ = result;
        const auto current = base::UTF8ToUTF16(std::string_view(kFlyVersion));
        if (!result.newer) {
          status(u"Fly " + current + u" is the latest version.", "");
          if (manual)
            app->ShowStatus(u"Fly " + current + u" is the latest version.");
          return;
        }
        status(u"Fly " + base::UTF8ToUTF16(result.latest) +
                   (result.package.is_valid()
                        ? u" is available. Install it here; Fly restarts "
                          u"with your tabs."
                        : u" is available as a download."),
               result.latest);
        app->ShowStatus(u"Fly " + base::UTF8ToUTF16(result.latest) +
                        u" is available. Check for Updates opens it.");
        if (manual)
          app->OpenInNewTab(result.page, origin.get());
      },
      weak_factory_.GetWeakPtr(), manual,
      origin ? origin->GetWeakPtr() : base::WeakPtr<BrowserWindowInterface>()));
}

void FlyApp::InstallUpdate() {
  if (update_installing_ || closing_)
    return;
  const auto status = [&](std::u16string text, std::string version) {
    for (auto* window : Windows())
      window->SetUpdateStatus(text, version);
  };
  if (!update_.newer) {
    status(u"Check for updates first.", "");
    return;
  }
  if (!update_.package.is_valid()) {
    // No package for this system in the release: open its page.
    if (auto* window = LastActiveWindow())
      window->CancelSurfaces();
    OpenInNewTab(update_.page);
    return;
  }
  update_installing_ = true;
  status(u"Downloading Fly " + base::UTF8ToUTF16(update_.latest) + u"…", "");
  InstallNativeUpdate(
      update_,
      base::BindOnce(
          [](base::WeakPtr<FlyApp> app, std::u16string error) {
            if (!app)
              return;
            app->update_installing_ = false;
            if (!error.empty()) {
              app->RecordDiagnostic("error", "update", error);
              for (auto* window : app->Windows())
                window->SetUpdateStatus(u"Update failed: " + error,
                                        app->update_.latest);
              app->ShowStatus(u"Update failed: " + error);
              return;
            }
            for (auto* window : app->Windows())
              window->SetUpdateStatus(
                  u"Fly " + base::UTF8ToUTF16(app->update_.latest) +
                      u" is installed. Restarting…",
                  "");
            chrome::AttemptRestart();
          },
          weak_factory_.GetWeakPtr()));
}

void FlyApp::UpdateDesktopIntegration(bool make_default) {
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyApp> app, bool make_default,
         DesktopIntegrationStatus status) {
        if (!app)
          return;
        fly::ChangeDesktopIntegration(
            make_default ? DesktopIntegrationChange::kMakeDefault
            : status.ours ? DesktopIntegrationChange::kRemoveEntry
                          : DesktopIntegrationChange::kAddEntry,
            base::BindOnce(
                [](base::WeakPtr<FlyApp> app, std::u16string error,
                   DesktopIntegrationStatus status) {
                  if (!app)
                    return;
                  for (auto* window : app->Windows())
                    window->ShowDesktopIntegration(status, error);
                },
                app));
      },
      weak_factory_.GetWeakPtr(), make_default));
}

void FlyApp::RunFirstWindowTasks() {
  if (closing_)
    return;
  MaybeShowWhatsNew();
  CheckUpdates(false);
  MaybeOfferDesktopIntegration();
}

void FlyApp::MaybeShowWhatsNew() {
  auto* window = LastActiveWindow();
  if (whats_new_checked_ || !window || !window->browser() ||
      window->browser()->GetProfile()->IsOffTheRecord())
    return;
  whats_new_checked_ = true;
  const auto seen = preferences_.ReadString("last_seen_version");
  if (!seen || !preferences_.CanEdit() || *seen == kFlyVersion)
    return;
  preferences_.SetString("last_seen_version", kFlyVersion);
  // A new profile has nothing to catch up on; an upgraded one does.
  if (seen->empty() || base::Version(*seen) >= base::Version(kFlyVersion))
    return;
  ResolveWhatsNewURL(
      *seen, base::BindOnce(
                 [](base::WeakPtr<FlyApp> app, GURL url) {
                   if (app)
                     app->OpenInNewTab(url);
                 },
                 weak_factory_.GetWeakPtr()));
}

// Offers desktop integration once, on the first start of a package
// that no menu entry lists yet.
void FlyApp::MaybeOfferDesktopIntegration() {
  if (closing_ || !preferences_.CanEdit() ||
      preferences_.ReadBool("desktop_integration_offered").value_or(false))
    return;
  ReadDesktopIntegration(base::BindOnce(
      [](base::WeakPtr<FlyApp> app, DesktopIntegrationStatus status) {
        auto* window = app ? app->LastActiveWindow() : nullptr;
        if (!app || app->closing_ || !window || !status.supported ||
            !status.entry.empty())
          return;
        app->preferences_.SetBool("desktop_integration_offered", true);
        auto delegate = std::make_unique<views::DialogDelegate>();
        delegate->SetOwnershipOfNewWidget(
            views::Widget::InitParams::CLIENT_OWNS_WIDGET);
        delegate->SetModalType(ui::mojom::ModalType::kWindow);
        delegate->SetTitle(u"Add Fly to your applications");
        delegate->SetButtonLabel(ui::mojom::DialogButton::kOk, u"Add to menu");
        delegate->SetButtonLabel(ui::mojom::DialogButton::kCancel, u"Not now");
        auto body = std::make_unique<views::View>();
        body->SetLayoutManager(std::make_unique<views::BoxLayout>(
            views::BoxLayout::Orientation::kVertical, gfx::Insets(16), 10));
        auto* message = body->AddChildView(std::make_unique<views::Label>(
            u"Add Fly to your applications menu?\n\nFly is running from its "
            u"package folder, so Rofi and your desktop menu do not list it "
            u"yet. Fly can add an entry that starts this installation.\n\nYou "
            u"can change this later in Preferences → About."));
        message->SetMultiLine(true);
        message->SetMaximumWidth(420);
        message->SetHorizontalAlignment(gfx::ALIGN_LEFT);
        auto* make_default = body->AddChildView(std::make_unique<views::Checkbox>(
            u"Also make Fly the default browser"));
        delegate->SetContentsView(std::move(body));
        delegate->SetAcceptCallback(base::BindOnce(
            [](base::WeakPtr<FlyApp> app, views::Checkbox* make_default) {
              if (!app)
                return;
              fly::ChangeDesktopIntegration(
                  make_default->GetChecked()
                      ? DesktopIntegrationChange::kMakeDefault
                      : DesktopIntegrationChange::kAddEntry,
                  base::BindOnce(
                      [](base::WeakPtr<FlyApp> app, std::u16string error,
                         DesktopIntegrationStatus) {
                        if (app && !error.empty())
                          app->ShowStatus(error);
                      },
                      app));
            },
            app, make_default));
        app->offer_widget_.reset(views::DialogDelegate::CreateDialogWidget(
            delegate.get(), window->widget()->GetNativeWindow(),
            window->widget()->GetNativeView()));
        app->offer_delegate_ = std::move(delegate);
        app->offer_widget_->Show();
      },
      weak_factory_.GetWeakPtr()));
}

void FlyApp::OnPreferencesChanged() {
  const auto theme = preferences_.ReadTheme();
  const bool theme_changed = theme && theme != applied_theme_;
  if (theme_changed) {
    applied_theme_ = theme;
    ApplyFlyPalette();
  }
  ApplyFlyPalette();
  ApplyProfilePreferencesEverywhere();
  if (user_content_)
    user_content_->SetEmailHandler(
        preferences_.ReadString("email_handler").value_or("system"));
  for (auto* window : Windows())
    window->OnPreferencesChanged(theme_changed);
  if (theme_changed) {
    // Native WebContents observers recompute actual renderer preferences,
    // including hidden/split/private/extension pages. No web API is replaced.
    ui::NativeTheme::GetInstanceForWeb()->NotifyOnNativeThemeUpdated();
  }
  UpdateSuspensionPolicy();
}

void FlyApp::ApplyFlyPalette() {
  const auto theme = preferences_.ReadTheme();
  const bool dark = !theme || theme->mode != "light";
  auto colors = DefaultFlyColors(dark);
  const auto custom = [&](std::string_view key, SkColor& target) {
    const auto hex = preferences_.ReadThemeColor(dark, key);
    uint32_t rgb = 0;
    if (hex && hex->size() == 7 && base::HexStringToUInt(hex->substr(1), &rgb))
      target = SkColorSetRGB(rgb >> 16, (rgb >> 8) & 0xff, rgb & 0xff);
  };
  custom("accent", colors.accent);
  custom("bg_main", colors.bg_main);
  custom("bg_sidebar", colors.bg_sidebar);
  custom("text", colors.text);
  colors.sidebar_opacity =
      preferences_.ReadThemeOpacity(dark, "sidebar_opacity");
  colors.overlay_opacity =
      preferences_.ReadThemeOpacity(dark, "overlay_opacity");
  const float scale =
      preferences_.ReadString("interface_scale").value_or("normal") == "large"
          ? 1.25f
          : 1.0f;
  const bool scale_changed = scale != GetFlyInterfaceScale();
  SetFlyInterfaceScale(scale);
  if (!SetFlyColors(colors) && !scale_changed)
    return;
  for (auto* window : Windows())
    window->OnPaletteChanged(scale_changed);
}

void FlyApp::ApplyProfilePreferencesEverywhere() {
  // Only rewrite per-profile state when the relevant product settings move.
  const std::string key =
      (preferences_.ReadBool("web_notifications").value_or(true) ? "1" : "0") +
      preferences_.ReadString("spellchecker_language").value_or("");
  if (applied_profile_preferences_ == key)
    return;
  applied_profile_preferences_ = key;
  for (auto* profile : WorkspaceProfiles())
    ApplyProfilePreferences(profile, preferences_);
  const bool notifications =
      preferences_.ReadBool("web_notifications").value_or(true);
  for (auto* window : Windows())
    window->SetNotificationsEnabled(notifications);
}

void FlyApp::OnBookmarksChanged() {
  for (auto* window : Windows())
    window->OnBookmarksChanged();
}

void FlyApp::OnWorkspacesChanged() {
  for (auto* window : Windows())
    window->OnWorkspacesChanged();
}

void FlyApp::UpdateSuspensionPolicy() {
  SuspendIdleTabs();
  const auto minutes = preferences_.ReadSuspendTimeoutMinutes();
  if (closing_ || !minutes || *minutes == 0) {
    suspension_timer_.Stop();
  } else if (!suspension_timer_.IsRunning()) {
    suspension_timer_.Start(FROM_HERE, base::Seconds(30),
                            base::BindRepeating(&FlyApp::SuspendIdleTabs,
                                                weak_factory_.GetWeakPtr()));
  }
}

void FlyApp::SuspendIdleTabs() {
  if (closing_)
    return;
  std::vector<base::WeakPtr<content::WebContents>> pages;
  for (auto* browser : BrowsersByActivation()) {
    auto* model = browser->GetTabStripModel();
    for (int index = 0; index < model->count(); ++index)
      pages.push_back(model->GetWebContentsAt(index)->GetWeakPtr());
  }
  const auto timeout =
      base::Minutes(preferences_.ReadSuspendTimeoutMinutes().value_or(0));
  const auto now = base::TimeTicks::Now();
  for (const auto& page : pages)
    if (page)
      MaybeAutoSuspendNativeTab(page.get(), timeout, now);
}

void FlyApp::ResumeAllTabs() {
  std::vector<base::WeakPtr<content::WebContents>> pages;
  for (auto* browser : BrowsersByActivation()) {
    auto* model = browser->GetTabStripModel();
    for (int index = 0; index < model->count(); ++index)
      pages.push_back(model->GetWebContentsAt(index)->GetWeakPtr());
  }
  for (const auto& page : pages)
    if (page)
      ResumeNativeTab(page.get());
}

void FlyApp::OnClosingAllBrowsers(bool closing) {
  closing_ = closing;
  if (closing) {
    tab_move_.reset();
    workspace_close_.reset();
    keep_alive_.reset();
    suspension_timer_.Stop();
    ResumeAllTabs();
    for (auto* window : Windows())
      window->CancelSurfaces();
  } else {
    UpdateSuspensionPolicy();
  }
}

void FlyApp::HoldKeepAlive() {
  if (!keep_alive_ && !closing_)
    keep_alive_ = std::make_unique<ScopedKeepAlive>(
        KeepAliveOrigin::FLY_WINDOW, KeepAliveRestartOption::DISABLED);
}

void FlyApp::ReleaseKeepAliveIfIdle() {
  if (!workspace_close_ && !creating_profile_ && !tab_move_)
    keep_alive_.reset();
}

void FlyApp::OnBrowserCreated(BrowserWindowInterface* browser) {
#if BUILDFLAG(IS_LINUX)
  // Popup windows (sign-in popups, window.open with features, extension
  // popups) belong to the Fly window they came from; tiling window managers
  // float them.
  auto* window = LastActiveWindow();
  if (!browser || FlyWindowController::ShouldDecorate(browser) || !window ||
      (browser->GetType() != BrowserWindowInterface::TYPE_POPUP &&
       browser->GetType() != BrowserWindowInterface::TYPE_APP_POPUP) ||
      ui::OzonePlatform::GetPlatformNameForTest() != "x11")
    return;
  auto* owner = window->widget()->GetNativeWindow();
  auto* view = BrowserView::GetBrowserViewForBrowser(browser);
  auto* popup = view && view->GetWidget() ? view->GetWidget()->GetNativeWindow()
                                          : nullptr;
  if (!popup || !popup->GetHost() || !owner || !owner->GetHost())
    return;
  // WM_TRANSIENT_FOR, set before the popup is mapped.
  x11::Connection::Get()->SetProperty(
      static_cast<x11::Window>(popup->GetHost()->GetAcceleratedWidget()),
      x11::Atom::WM_TRANSIENT_FOR, x11::Atom::WINDOW,
      static_cast<x11::Window>(owner->GetHost()->GetAcceleratedWidget()));
#endif
}
}  // namespace fly
