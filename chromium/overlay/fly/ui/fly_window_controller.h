// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_WINDOW_CONTROLLER_H_
#define FLY_UI_FLY_WINDOW_CONTROLLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <set>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/bookmarks/frequent_sites_service.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/preferences_service.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "fly/ui/sidebar_panel.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "fly/ui/diagnostics_hub.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/history_service.h"
#include "fly/ui/site_zoom.h"
#include "fly/workspaces/workspace_service.h"
#include "ui/aura/client/focus_change_observer.h"
#include "ui/events/event_handler.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class Profile;
class ScopedKeepAlive;
class GURL;
namespace aura {
class WindowTargeter;
}
namespace aura::client {
class FocusClient;
}
namespace content {
class WebContents;
}
namespace views {
class DialogDelegate;
class NativeViewHost;
class MdTextButton;
class ImageButton;
class View;
class Label;
}  // namespace views

namespace ui {
class SimpleMenuModel;
}

class ScopedProfileKeepAlive;
namespace fly {
class NewTabHistory;
class WelcomeView;
class CommandPalette;
class BookmarkService;
class SettingsMenu;
class FlyExtensions;
class UserContentService;
class FlyButton;
class FlyLabel;
class StatusBar;
class KeyboardEditor;
class TabSidebar;
class SidebarPanel;
class SidebarRoot;
class SidebarClock;
class DownloadShelf;
class HintController;
class KeyboardRouter;
class WorkspaceCloseTransaction;
class TabMoveTransaction;
// One OS window. Each entry keeps an upstream BrowserView/BrowserWidget, native
// tab model, profile and extension window ID. Its NativeWidgetAura does not
// create an OS window. NativeViewHost controls actual clipping and visibility.
class FlyWindowController : public views::WidgetDelegate,
                            public views::WidgetObserver,
                            public BrowserCollectionObserver,
                            public ui::EventHandler,
                            public views::FocusChangeListener,
                            public aura::client::FocusChangeObserver {
 public:
  static bool ShouldEmbed(const BrowserWindowInterface* browser);
  static FlyWindowController& Get();
  static FlyWindowController* GetIfCreated();

  FlyWindowController();
  ~FlyWindowController() override;
  FlyWindowController(const FlyWindowController&) = delete;
  FlyWindowController& operator=(const FlyWindowController&) = delete;

  void Prepare(BrowserWindowInterface* browser);
  void Attach(BrowserWindowInterface* browser, views::Widget* child);
  void Select(BrowserWindowInterface* browser, bool focus);
  bool IsSelected(const BrowserWindowInterface* browser) const;
  bool has_selected_browser() const { return selected_ != nullptr; }
  bool SidebarHasFocus() const;
  views::Widget* desktop_widget() const { return desktop_.get(); }
  CommandService& commands() { return commands_; }
  BookmarkService& bookmarks() { return *bookmarks_; }
  CommandPalette& bookmark_picker() { return *bookmark_picker_; }
  CommandPalette& address_picker() { return *address_picker_; }
  CommandPalette& history_picker() { return *history_picker_; }
  NewTabHistory& new_tab_history() { return *new_tab_history_; }
  bool CanShowHistory() const { return workspaces_.Read().has_value(); }
  views::Widget* history_clear_dialog_for_testing() const;
  PreferencesService& preferences() { return preferences_; }
  FlyButton* clock_for_testing() { return clock_; }
  FlyButton* preferences_button_for_testing() { return preferences_button_; }
  BrowserWindowInterface* selected_browser_for_testing() { return selected_; }
  CommandPalette& palette() { return *palette_; }
  CommandPalette& extensions_picker_for_testing() { return *extensions_picker_; }
  CommandPalette& downloads_picker_for_testing() { return *downloads_picker_; }
  views::View* window_controls_for_testing() { return window_controls_; }
  views::ImageButton* window_control_for_testing(std::string_view action) const;
  KeyboardEditor& keyboard_editor() { return *keyboard_editor_; }
  HintController& hints() { return *hints_; }
  void UpdateModeLabel();
  void UpdateTargetURL(BrowserWindowInterface* browser,
                       content::WebContents* source,
                       const GURL& url);
  StatusBar* status_bar_for_testing() { return status_bar_; }
  // Fly's new tab page: bookmarks and frequent sites (none in private
  // workspaces), and hiding a frequent site from its list.
  base::DictValue NewTabLists(
      bool off_the_record, const std::vector<HistoryEntry>& history) const;
  bool HideFrequentSite(const std::string& url);
  TabSidebar* tab_sidebar_for_testing() { return tab_sidebar_; }
  DownloadShelf* download_shelf_for_testing() { return download_shelf_; }
  SidebarRoot* sidebar_root_for_testing() { return sidebar_root_; }
  Profile* ActiveProfile() const;
  void OpenInNewWindow(GURL url);
  void CreateTab();
  void UpdateFullscreenChrome();
  void UpdateSplitButton();
  FlyButton* split_button_for_testing() { return split_button_; }
  // An empty name takes the next "Workspace N".
  void CreateWorkspace(std::string name = {});
  std::string NextWorkspaceName() const;
  void ToggleWorkspacePicker();
  bool CanShowWorkspacePicker() const { return workspaces_.Read().has_value(); }
  CommandPalette& workspace_picker() { return *workspace_picker_; }
  ui::SimpleMenuModel* workspace_context_menu_for_testing(std::string_view id) const;
  views::Widget* workspace_rename_dialog_for_testing() const;
  views::Widget* workspace_remove_dialog_for_testing() const;
  bool workspace_removal_pending_for_testing() const {
    return bool(workspace_close_);
  }
  bool CanTogglePrivate(BrowserWindowInterface* browser) const;
  views::View* workspace_buttons_for_testing() { return workspace_buttons_; }
  bool RunCommand(BrowserWindowInterface* browser, std::string_view action);
  bool CanMoveTab(BrowserWindowInterface* browser,
                  content::WebContents* contents) const;
  bool ShowTabMovePicker(BrowserWindowInterface* browser,
                         content::WebContents* contents);
  CommandPalette& tab_move_picker() { return *tab_move_picker_; }
  CommandPalette& downloads_picker() { return *downloads_picker_; }
  SettingsMenu& settings_menu() { return *settings_menu_; }
  FlyExtensions& extensions() { return *extensions_; }
  UserContentService& user_content() { return *user_content_; }
  CommandPalette& extensions_picker() { return *extensions_picker_; }
  views::View* extension_actions_for_testing() const;
  std::string extension_badge_for_testing(size_t index) const;
  DiagnosticsHub* diagnostics_hub_for_testing() const {
    return diagnostics_hub_.get();
  }
  // Fly status line (footer mode badge), also kept in the in-memory
  // diagnostics log while recording is enabled.
  void ShowStatus(std::u16string text);
  bool tab_move_pending_for_testing() const { return bool(tab_move_); }

  void OnKeyEvent(ui::KeyEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
  void OnDidChangeFocus(views::View* before, views::View* now) override;
  void OnWindowFocused(aura::Window* gained, aura::Window* lost) override;
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& bounds) override;
  void FocusSidebar();
  bool sidebar_expanded_for_testing() const;
  std::u16string workspace_header_for_testing() const;
  bool clock_timer_running_for_testing() const {
    return clock_timer_.IsRunning();
  }
  views::Widget* sidebar_widget_for_testing() { return sidebar_widget_.get(); }
  FlyButton* sidebar_pin_for_testing() { return sidebar_pin_button_; }
  FlyButton* reader_button_for_testing() { return reader_button_; }
  std::u16string status_text_for_testing() const;
  WelcomeView* welcome_for_testing() { return welcome_; }
  // The rename and new workspace name dialog, while it is open.
  views::DialogDelegate* workspace_name_dialog_for_testing() {
    return rename_widget_ ? rename_delegate_.get() : nullptr;
  }
  std::vector<PaletteEntry> dashboard_entries_for_testing() {
    return WorkspaceEntries();
  }
  void choose_workspace_for_testing(std::string action);
  FlyButton* new_tab_button_for_testing() { return new_tab_button_; }

  // WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetShowStateChanged(views::Widget* widget) override;

 private:
  void SetReaderActive(content::WebContents* contents, bool active);
  void UpdateReaderButton();
  void UpdateWelcome();
  void RunPreferencesAction(std::string action);
  // The in-place update: downloads the package of the newer release, checks its
  // SHA-256, replaces this installation and restarts.
  void InstallUpdate();
  // Desktop integration (menu entry and default browser).
  void RefreshDesktopIntegration();
  void ChangeDesktopIntegration(bool make_default);
  void MaybeOfferDesktopIntegration();
  std::unique_ptr<views::DialogDelegate> offer_delegate_;
  std::unique_ptr<views::Widget> offer_widget_;
  UpdateCheckResult update_;
  bool update_installing_ = false;
  std::vector<PaletteEntry> ExtensionCommandEntries(
      BrowserWindowInterface* browser);
  void RunFromEmptyWorkspace(std::string action);
  void RefreshReaderState();
  struct Entry {
    raw_ptr<BrowserWindowInterface> browser;
    raw_ptr<views::Widget> widget = nullptr;
    raw_ptr<views::NativeViewHost> host;
    std::string workspace_id;
    bool browser_closed = false;
    uint64_t order = 0;
    base::CallbackListSubscription closed_subscription;
  };
  std::vector<PaletteEntry> WorkspaceEntries();
  // Tabs listed by the workspace dashboard, by "<workspace id>/<n>".
  struct TabChoice {
    base::WeakPtr<BrowserWindowInterface> browser;
    base::WeakPtr<content::WebContents> contents;
  };
  std::map<std::string, TabChoice> tab_choices_;
  // Asks for the new workspace's name first.
  void PromptNewWorkspace();
  bool WorkspaceChoiceAvailable(std::string_view action) const;
  void ChooseWorkspace(content::WebContents* source, std::string action);
  void OnWorkspacesChanged();
  void ReorderWorkspace(std::string id, int direction);
  void RenameWorkspace(std::string id);
  void CancelWorkspaceRename();
  void RemoveWorkspace(std::string id);
  bool CanRemoveWorkspace(std::string_view id,
                          const base::FilePath& path) const;
  void BeginWorkspaceRemoval(std::string id, base::FilePath path);
  void FinishWorkspaceRemoval(std::string id,
                              base::FilePath path,
                              bool success);
  std::vector<PaletteEntry> TabMoveEntries() const;
  bool TabMoveDestinationAvailable(std::string_view action) const;
  void ChooseTabMoveDestination(content::WebContents* source,
                                std::string action);
  bool ValidateTabMove(base::WeakPtr<BrowserWindowInterface> source_browser,
                       base::WeakPtr<content::WebContents> source,
                       const std::string& destination_id,
                       const base::FilePath& destination_path,
                       bool require_selection) const;
  void FinishTabMove(bool success,
                     base::WeakPtr<BrowserWindowInterface> destination,
                     base::WeakPtr<content::WebContents> contents);
  void OnPreferencesChanged();
  void UpdateWindowControls();
  bool SidebarOverlayBlocked() const;
  void UpdateSidebarMode();
  void UpdateSidebarVisibility();
  void UpdateSidebarBounds();
  void UpdateSidebarHitArea();
  void ApplySidebarMotion(bool animate);
  float SidebarShiftNow() const;
  int VisibleSidebarWidth() const;
  void OnSidebarMotionEnded();
 public:
  // Tab numbers show while the modifier that selects tabs by number is held on
  // its own (after 150 ms); any other key event hides them. Page and interface
  // key events both report here.
  void TrackTabNumbers(bool key_down,
                       ui::KeyboardCode code,
                       ui::DomCode dom_code,
                       int flags,
                       bool repeat,
                       bool passthrough);
  bool tab_numbers_shown_for_testing() const { return tab_numbers_shown_; }

 private:
  void SetTabNumbers(bool shown);
  void ClickModeBadge();
  void DropDashboardTab(std::string source, std::string target);
 public:
  // Popup windows (sign-in popups, window.open with features, extension popups)
  // belong to Fly's window; tiling window managers float them.
  void OnBrowserCreated(BrowserWindowInterface* browser) override;

 private:
  void LoadSidebarArtwork();
  void StopSidebarFocusObservation();
  void ToggleSidebarAutoCollapse();
  void ReturnFocusFromSidebar();
  void ClearSidebarViews();
  void RunWindowControl(std::string action);
  void UpdateSuspensionPolicy();
  void SuspendIdleTabs();
  void ResumeAllTabs();
  std::vector<PaletteEntry> BookmarkEntries();
  void OnBookmarksChanged();
  void OpenBookmark(content::WebContents* source, std::string url);
  std::vector<PaletteEntry> AddressEntries(std::u16string_view query);
  void ExecuteAddress(content::WebContents* source, std::string action);
  void ToggleHistory(BrowserWindowInterface* browser);
  enum class HistorySurface { kAddress, kHistory };
  void LoadHistory(HistorySurface surface);
  void CancelHistory();
  std::vector<PaletteEntry> HistoryEntries();
  void OpenHistory(content::WebContents* source, std::string action);
  void ConfirmHistoryClear(content::WebContents* source);

  void ToggleAddress(BrowserWindowInterface* browser);
  bool RunProductAction(BrowserWindowInterface* browser,
                        std::string_view action);
  void ToggleDownloads(BrowserWindowInterface* browser);
  std::vector<Profile*> WorkspaceProfiles() const;
  void ShowDiagnostics();
  void CheckUpdates(bool manual);
  void MaybeShowWhatsNew();
  void OpenInNewTab(GURL url);
  void ApplyProfilePreferencesEverywhere();
  void ScheduleExtensionRefresh();
  void RefreshExtensionActions();
  void ToggleExtensionActions(BrowserWindowInterface* browser);
  void ChooseExtensionAction(content::WebContents* source, std::string action);
  void RemoveBookmark(content::WebContents* source, std::string url);
  void UpdateClock();
  void UpdateWorkspaceHeader();
  void ApplyFlyPalette();
  void CycleClockFormat();
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;
  void UpdateEmbeddedVisibility();
  void ScheduleWorkspaceRefresh();
  void RebuildWorkspaceButtons();
  void SelectWindow(base::WeakPtr<BrowserWindowInterface> browser);
  void OpenWorkspace(std::string id);
  void OnProfileReady(std::string id, Profile* profile);
  bool WorkspaceMatchesProfile(std::string_view id, Profile* profile) const;
  void OnBrowserDidClose(BrowserWindowInterface* browser);
  void TogglePrivate(BrowserWindowInterface* browser);
  void RequestClose(views::Widget::ClosedReason reason);
  void OnClosingAllBrowsers(bool closing);
  void FinishClose();
  void ArmCloseCallback();

  WorkspaceService workspaces_;
  CommandService commands_;
  PreferencesService preferences_;
  std::unique_ptr<BookmarkService> bookmarks_;
  std::unique_ptr<FrequentSitesService> frequent_sites_;
  std::unique_ptr<NewTabHistory> new_tab_history_;
  std::unique_ptr<HistoryService> history_;
  std::unique_ptr<HistoryService> history_clear_service_;
  bool history_clear_running_ = false;
  std::vector<HistoryEntry> history_entries_;
  std::set<std::string> history_choices_;
  uint64_t history_generation_ = 0;
  std::set<std::string> bookmark_urls_;
  std::map<std::string, std::string> address_choices_;
  std::optional<ThemePreferences> applied_theme_;
  PrefChangeRegistrar preferences_registrar_;
  base::OneShotTimer clock_timer_;
  base::RepeatingTimer suspension_timer_;
  std::unique_ptr<HintController> hints_;
  std::unique_ptr<CommandPalette> palette_;
  std::unique_ptr<CommandPalette> bookmark_picker_;
  std::unique_ptr<CommandPalette> address_picker_;
  std::unique_ptr<CommandPalette> history_picker_;
  std::unique_ptr<CommandPalette> workspace_picker_;
  std::unique_ptr<CommandPalette> tab_move_picker_;
  std::unique_ptr<CommandPalette> downloads_picker_;
  std::unique_ptr<SettingsMenu> settings_menu_;
  std::unique_ptr<FlyExtensions> extensions_;
  std::unique_ptr<UserContentService> user_content_;
  SiteZoom site_zoom_;
  std::unique_ptr<CommandPalette> extensions_picker_;
  bool extension_refresh_pending_ = false;
  std::unique_ptr<DiagnosticsHub> diagnostics_hub_;
  std::vector<DiagnosticEvent> diagnostics_events_;
  DiagnosticsSession CurrentDiagnostics();
  void RecordDiagnostic(std::string level,
                        std::string category,
                        std::u16string message);
  std::optional<std::string> applied_profile_preferences_;
  bool whats_new_checked_ = false;
  bool update_checked_ = false;
  base::WeakPtr<BrowserWindowInterface> move_source_browser_;
  base::WeakPtr<content::WebContents> move_source_;
  std::unique_ptr<TabMoveTransaction> tab_move_;
  std::map<std::string, base::WeakPtr<BrowserWindowInterface>> private_choices_;
  std::unique_ptr<KeyboardEditor> keyboard_editor_;
  std::unique_ptr<views::DialogDelegate> history_clear_delegate_;
  std::unique_ptr<views::Widget> history_clear_widget_;
  std::unique_ptr<views::DialogDelegate> rename_delegate_;
  std::unique_ptr<views::Widget> rename_widget_;
  std::unique_ptr<views::DialogDelegate> remove_delegate_;
  std::unique_ptr<views::Widget> remove_widget_;
  std::unique_ptr<WorkspaceCloseTransaction> workspace_close_;
  std::unique_ptr<KeyboardRouter> empty_router_;
  uint64_t empty_generation_ = 0;
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<views::Widget> desktop_;
  std::unique_ptr<views::Widget> sidebar_widget_;
  raw_ptr<views::View> sidebar_content_ = nullptr;
  raw_ptr<views::View> footer_ = nullptr;
  raw_ptr<FlyButton> split_button_ = nullptr;
  raw_ptr<views::View> sidebar_slot_ = nullptr;
  raw_ptr<views::NativeViewHost> sidebar_host_ = nullptr;
  raw_ptr<SidebarPanel> sidebar_panel_ = nullptr;
  raw_ptr<SidebarRoot> sidebar_root_ = nullptr;
  // Limits the sidebar host's hit area to the visible panel.
  raw_ptr<aura::WindowTargeter> sidebar_targeter_ = nullptr;
  bool sidebar_expanded_ = false;
  // The first state and mode changes apply without motion.
  bool sidebar_motion_ready_ = false;
  bool sidebar_motion_applied_ = false;
  float sidebar_motion_scale_ = 0;
  // The slide in progress, so the pointer and the hit area follow the edge the
  // user sees.
  float sidebar_shift_from_ = 0;
  float sidebar_shift_to_ = 0;
  base::TimeTicks sidebar_motion_start_;
  bool sidebar_motion_animating_ = false;
  // Width that takes the pointer: the panel until a collapse ends.
  int sidebar_hit_width_ = 0;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      browser_collection_{this};
  base::OneShotTimer tab_numbers_timer_;
  bool tab_numbers_shown_ = false;
  raw_ptr<FlyButton> sidebar_pin_button_ = nullptr;
  raw_ptr<FlyButton> reader_button_ = nullptr;
  // The active tab while it shows the reader overlay.
  base::WeakPtr<content::WebContents> reader_contents_;
  base::RepeatingTimer reader_poll_;
  raw_ptr<FlyButton> new_tab_button_ = nullptr;
  raw_ptr<FlyLabel> workspace_badge_ = nullptr;
  raw_ptr<FlyLabel> workspace_label_ = nullptr;
  raw_ptr<FlyButton> workspace_picker_button_ = nullptr;
  raw_ptr<aura::client::FocusClient> sidebar_focus_client_ = nullptr;
  bool observing_sidebar_focus_ = false;
  bool sidebar_auto_collapse_ = true;
  bool sidebar_pointer_inside_ = false;
  bool sidebar_focus_inside_ = false;
  bool sidebar_keyboard_expanded_ = false;
  raw_ptr<views::View> window_controls_ = nullptr;
  std::map<std::string, raw_ptr<views::ImageButton>> window_control_buttons_;
  raw_ptr<views::View> workspace_buttons_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  raw_ptr<WelcomeView> welcome_ = nullptr;
  std::unique_ptr<ScopedProfileKeepAlive> welcome_keep_alive_;
  // Run on the workspace's new tab once it opens from the empty view.
  std::string pending_empty_action_;
  std::string pending_shell_url_;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<StatusBar> status_bar_ = nullptr;
  raw_ptr<DownloadShelf> download_shelf_ = nullptr;
  raw_ptr<SidebarClock> clock_ = nullptr;
  raw_ptr<FlyButton> preferences_button_ = nullptr;
  raw_ptr<TabSidebar> tab_sidebar_ = nullptr;
  std::map<BrowserWindowInterface*, Entry> entries_;
  raw_ptr<BrowserWindowInterface> selected_ = nullptr;
  std::map<std::string, base::WeakPtr<BrowserWindowInterface>> last_windows_;
  // Memory only. OTR Profiles are never entered in the persistent catalog.
  std::map<Profile*, base::WeakPtr<BrowserWindowInterface>> private_returns_;
  uint64_t next_window_order_ = 0;
  bool workspace_refresh_pending_ = false;
  bool closing_ = false;
  bool creating_profile_ = false;
  base::CallbackListSubscription close_subscription_;
  base::WeakPtrFactory<FlyWindowController> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_FLY_WINDOW_CONTROLLER_H_
