// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_WINDOW_CONTROLLER_H_
#define FLY_UI_FLY_WINDOW_CONTROLLER_H_

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "fly/commands/palette_catalog.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/history_service.h"
#include "fly/ui/sidebar_panel.h"
#include "ui/aura/client/focus_change_observer.h"
#include "ui/events/event_handler.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class GURL;
class Profile;
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
class ImageButton;
class Label;
class NativeViewHost;
class View;
}  // namespace views
namespace ui {
class SimpleMenuModel;
}

namespace fly {
class CommandPalette;
class DiagnosticsHub;
class DownloadShelf;
class FlyBrowserFrame;
class FlyButton;
class FlyLabel;
class HintController;
class KeyboardEditor;
class SettingsMenu;
class SidebarClock;
class SidebarPanel;
class SidebarRoot;
class StatusBar;
class TabSidebar;

// Fly's interface inside one normal Chromium browser window: the sidebar
// (tabs, workspaces, navigation), the footer (download shelf and status bar)
// and the floating surfaces (palettes, hints, Preferences). The window is an
// ordinary top-level BrowserWidget; FlyBrowserFrame reserves the sidebar slot
// and the footer around its BrowserView. Every workspace opens its own
// windows. Application-wide state lives in FlyApp.
class FlyWindowController : public views::WidgetObserver,
                            public ui::EventHandler,
                            public views::FocusChangeListener,
                            public aura::client::FocusChangeObserver {
 public:
  // Normal browser windows of native Fly get Fly's interface. Popups, apps
  // and DevTools stay ordinary Chromium windows.
  static bool ShouldDecorate(const BrowserWindowInterface* browser);
  // The controller of a decorated browser window, or null.
  static FlyWindowController* FromBrowser(const BrowserWindowInterface* browser);
  // The controller of the window that shows `contents`, or null.
  static FlyWindowController* FromContents(content::WebContents* contents);

  FlyWindowController(BrowserWindowInterface* browser, views::Widget* widget);
  ~FlyWindowController() override;
  FlyWindowController(const FlyWindowController&) = delete;
  FlyWindowController& operator=(const FlyWindowController&) = delete;

  BrowserWindowInterface* browser() const { return browser_; }
  base::WeakPtr<FlyWindowController> weak_ptr() {
    return weak_factory_.GetWeakPtr();
  }
  views::Widget* widget() const { return widget_; }
  content::WebContents* ActiveContents() const;

  // FlyBrowserFrame: the frame shows the sidebar host and the footer. A
  // recreated frame takes them over from the previous one.
  void InstallInto(FlyBrowserFrame* frame);
  void FrameDestroying(FlyBrowserFrame* frame);
  // Width reserved left of the page and height reserved below it. Both are
  // zero in fullscreen.
  int SidebarSlotWidth() const;
  int FooterHeight() const;
  views::View* footer() const { return footer_; }
  views::NativeViewHost* sidebar_host() const { return sidebar_host_; }
  void LayoutFrameChrome();
  void UpdateFullscreenChrome();

  // Called by FlyApp when shared state changes.
  void OnPreferencesChanged(bool theme_changed);
  void OnPaletteChanged(bool scale_changed);
  void OnBookmarksChanged();
  void OnWorkspacesChanged();
  void OnHistoryChanged();
  void ScheduleWorkspaceRefresh();
  void ScheduleExtensionRefresh();
  void SetNotificationsEnabled(bool enabled);
  void SetUpdateStatus(std::u16string status, std::string version);
  void RefreshDesktopIntegration();
  void ShowDesktopIntegration(const DesktopIntegrationStatus& status,
                              std::u16string error);
  void CancelSurfaces();

  // Runs a Fly action in this window. Returns whether it was handled.
  bool RunCommand(std::string_view action);
  bool CanShowHistory() const;
  bool SidebarHasFocus() const;
  void FocusSidebar();
  void UpdateModeLabel();
  void UpdateTargetURL(content::WebContents* source, const GURL& url);
  void UpdateSplitButton();
  // Fly status line (footer mode badge), also kept in the in-memory
  // diagnostics log while recording is enabled.
  void ShowStatus(std::u16string text);
  bool ShowTabMovePicker(content::WebContents* contents);
  // Tab numbers show while the modifier that selects tabs by number is held on
  // its own (after 150 ms); any other key event hides them. Page and interface
  // key events both report here.
  void TrackTabNumbers(bool key_down,
                       ui::KeyboardCode code,
                       ui::DomCode dom_code,
                       int flags,
                       bool repeat,
                       bool passthrough);
  bool CreateTab();

  CommandPalette& palette() { return *palette_; }
  CommandPalette& address_picker() { return *address_picker_; }
  CommandPalette& history_picker() { return *history_picker_; }
  CommandPalette& bookmark_picker() { return *bookmark_picker_; }
  CommandPalette& workspace_picker() { return *workspace_picker_; }
  CommandPalette& tab_move_picker() { return *tab_move_picker_; }
  CommandPalette& downloads_picker() { return *downloads_picker_; }
  CommandPalette& extensions_picker() { return *extensions_picker_; }
  SettingsMenu& settings_menu() { return *settings_menu_; }
  KeyboardEditor& keyboard_editor() { return *keyboard_editor_; }
  HintController& hints() { return *hints_; }

  // Testing.
  TabSidebar* tab_sidebar_for_testing() { return tab_sidebar_; }
  StatusBar* status_bar_for_testing() { return status_bar_; }
  DownloadShelf* download_shelf_for_testing() { return download_shelf_; }
  SidebarRoot* sidebar_root_for_testing() { return sidebar_root_; }
  views::Widget* sidebar_widget_for_testing() { return sidebar_widget_.get(); }
  FlyButton* clock_for_testing() { return clock_; }
  FlyButton* preferences_button_for_testing() { return preferences_button_; }
  FlyButton* split_button_for_testing() { return split_button_; }
  FlyButton* sidebar_pin_for_testing() { return sidebar_pin_button_; }
  FlyButton* reader_button_for_testing() { return reader_button_; }
  FlyButton* new_tab_button_for_testing() { return new_tab_button_; }
  views::View* window_controls_for_testing() { return window_controls_; }
  views::ImageButton* window_control_for_testing(std::string_view action) const;
  views::View* workspace_buttons_for_testing() { return workspace_buttons_; }
  ui::SimpleMenuModel* workspace_context_menu_for_testing(
      std::string_view id) const;
  views::Widget* workspace_rename_dialog_for_testing() const;
  views::Widget* workspace_remove_dialog_for_testing() const;
  views::Widget* history_clear_dialog_for_testing() const;
  views::DialogDelegate* workspace_name_dialog_for_testing() {
    return rename_widget_ ? rename_delegate_.get() : nullptr;
  }
  CommandPalette& extensions_picker_for_testing() { return *extensions_picker_; }
  CommandPalette& downloads_picker_for_testing() { return *downloads_picker_; }
  DiagnosticsHub* diagnostics_hub_for_testing() const {
    return diagnostics_hub_.get();
  }
  views::View* extension_actions_for_testing() const;
  std::string extension_badge_for_testing(size_t index) const;
  bool sidebar_expanded_for_testing() const { return sidebar_expanded_; }
  std::u16string workspace_header_for_testing() const;
  bool clock_timer_running_for_testing() const {
    return clock_timer_.IsRunning();
  }
  bool tab_numbers_shown_for_testing() const { return tab_numbers_shown_; }
  std::u16string status_text_for_testing() const;
  std::vector<PaletteEntry> dashboard_entries_for_testing() {
    return WorkspaceEntries();
  }
  void choose_workspace_for_testing(std::string action);

  // ui::EventHandler:
  void OnKeyEvent(ui::KeyEvent* event) override;
  void OnMouseEvent(ui::MouseEvent* event) override;
  // views::FocusChangeListener:
  void OnDidChangeFocus(views::View* before, views::View* now) override;
  // aura::client::FocusChangeObserver:
  void OnWindowFocused(aura::Window* gained, aura::Window* lost) override;
  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& bounds) override;
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;
  void OnWidgetShowStateChanged(views::Widget* widget) override;
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

 private:
  // Tabs listed by the workspace dashboard, by "<workspace id>/<n>".
  struct TabChoice {
    base::WeakPtr<BrowserWindowInterface> browser;
    base::WeakPtr<content::WebContents> contents;
  };
  enum class HistorySurface { kAddress, kHistory };

  void BuildSidebar();
  void BuildFooter();
  void BuildSurfaces();
  void RunAction(std::string action);
  bool RunProductAction(std::string_view action);
  void OnBrowserDidClose(BrowserWindowInterface* browser);
  bool Closing() const;

  // Sidebar.
  void LoadSidebarArtwork();
  void ClearSidebarViews();
  void StopSidebarFocusObservation();
  bool SidebarOverlayBlocked() const;
  void UpdateSidebarMode();
  void UpdateSidebarVisibility();
  void UpdateSidebarBounds();
  void UpdateSidebarHitArea();
  void ApplySidebarMotion(bool animate);
  float SidebarShiftNow() const;
  int VisibleSidebarWidth() const;
  void OnSidebarMotionEnded();
  void ToggleSidebarAutoCollapse();
  void ReturnFocusFromSidebar();
  void UpdateWindowControls();
  void RunWindowControl(std::string action);
  void SetTabNumbers(bool shown);
  void ClickModeBadge();
  void UpdateWorkspaceHeader();
  void RebuildWorkspaceButtons();
  void UpdateClock();
  void CycleClockFormat();
  void ApplyColorMode();

  // Workspaces.
  std::vector<PaletteEntry> WorkspaceEntries();
  bool WorkspaceChoiceAvailable(std::string_view action) const;
  void ChooseWorkspace(content::WebContents* source, std::string action);
  void DropDashboardTab(std::string source, std::string target);
  void ToggleWorkspacePicker();
  void PromptNewWorkspace();
  void RenameWorkspace(std::string id);
  void RemoveWorkspace(std::string id);
  void CancelWorkspaceRename();
  void ActivateTab(BrowserWindowInterface* browser,
                   content::WebContents* contents);

  // Tab moves.
  void ChooseTabMoveDestination(content::WebContents* source,
                                std::string action);

  // Address, history and bookmarks.
  void ToggleAddress();
  std::vector<PaletteEntry> AddressEntries(std::u16string_view query);
  void ExecuteAddress(content::WebContents* source, std::string action);
  // The address bar's highlighted row: connect to its site early and, when
  // the preference allows, load the page it opens in the background.
  void PrepareAddressChoice(std::string_view action);
  // Opens `url` in the active tab as a navigation from the address bar.
  void NavigateFromAddressBar(const GURL& url);
  void ToggleHistory();
  void LoadHistory(HistorySurface surface);
  void CancelHistory();
  std::vector<PaletteEntry> HistoryEntries();
  void OpenHistory(content::WebContents* source, std::string action);
  void ConfirmHistoryClear(content::WebContents* source);
  std::vector<PaletteEntry> BookmarkEntries();
  void OpenBookmark(content::WebContents* source, std::string url);
  void RemoveBookmark(content::WebContents* source, std::string url);

  // Other surfaces.
  void ToggleDownloads();
  void ShowDiagnostics();
  void RefreshExtensionActions();
  void ToggleExtensionActions();
  void ChooseExtensionAction(content::WebContents* source, std::string action);
  std::vector<PaletteEntry> ExtensionCommandEntries();
  void RunPreferencesAction(std::string action);
  void SetReaderActive(content::WebContents* contents, bool active);
  void RefreshReaderState();
  void UpdateReaderButton();

  raw_ptr<BrowserWindowInterface> browser_;
  raw_ptr<views::Widget> widget_;
  raw_ptr<FlyBrowserFrame> frame_ = nullptr;
  base::CallbackListSubscription closed_subscription_;
  bool browser_closed_ = false;

  // Sidebar.
  std::unique_ptr<views::Widget> sidebar_widget_;
  raw_ptr<views::NativeViewHost> sidebar_host_ = nullptr;
  raw_ptr<views::View> sidebar_content_ = nullptr;
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
  bool sidebar_auto_collapse_ = true;
  bool sidebar_pointer_inside_ = false;
  bool sidebar_focus_inside_ = false;
  bool sidebar_keyboard_expanded_ = false;
  raw_ptr<aura::client::FocusClient> sidebar_focus_client_ = nullptr;
  bool observing_sidebar_focus_ = false;
  raw_ptr<views::View> window_controls_ = nullptr;
  std::map<std::string, raw_ptr<views::ImageButton>> window_control_buttons_;
  raw_ptr<FlyButton> workspace_picker_button_ = nullptr;
  raw_ptr<FlyLabel> workspace_badge_ = nullptr;
  raw_ptr<FlyLabel> workspace_label_ = nullptr;
  raw_ptr<FlyButton> new_tab_button_ = nullptr;
  raw_ptr<views::View> workspace_buttons_ = nullptr;
  raw_ptr<TabSidebar> tab_sidebar_ = nullptr;
  raw_ptr<SidebarClock> clock_ = nullptr;
  raw_ptr<FlyButton> sidebar_pin_button_ = nullptr;
  raw_ptr<FlyButton> split_button_ = nullptr;
  raw_ptr<FlyButton> reader_button_ = nullptr;
  raw_ptr<FlyButton> preferences_button_ = nullptr;
  base::OneShotTimer clock_timer_;
  base::OneShotTimer tab_numbers_timer_;
  bool tab_numbers_shown_ = false;
  bool workspace_refresh_pending_ = false;

  // Footer. The frame owns the footer and the sidebar host; these hold them
  // while no frame does.
  std::unique_ptr<views::View> owned_footer_;
  std::unique_ptr<views::NativeViewHost> owned_sidebar_host_;
  raw_ptr<views::View> footer_ = nullptr;
  raw_ptr<DownloadShelf> download_shelf_ = nullptr;
  raw_ptr<StatusBar> status_bar_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  std::u16string status_workspace_name_;
  bool extension_refresh_pending_ = false;

  // Floating surfaces.
  std::unique_ptr<HintController> hints_;
  std::unique_ptr<CommandPalette> palette_;
  std::unique_ptr<CommandPalette> bookmark_picker_;
  std::unique_ptr<CommandPalette> address_picker_;
  std::unique_ptr<CommandPalette> history_picker_;
  std::unique_ptr<CommandPalette> workspace_picker_;
  std::unique_ptr<CommandPalette> tab_move_picker_;
  std::unique_ptr<CommandPalette> downloads_picker_;
  std::unique_ptr<CommandPalette> extensions_picker_;
  std::unique_ptr<SettingsMenu> settings_menu_;
  std::unique_ptr<KeyboardEditor> keyboard_editor_;
  std::unique_ptr<DiagnosticsHub> diagnostics_hub_;
  std::unique_ptr<views::DialogDelegate> history_clear_delegate_;
  std::unique_ptr<views::Widget> history_clear_widget_;
  std::unique_ptr<views::DialogDelegate> rename_delegate_;
  std::unique_ptr<views::Widget> rename_widget_;
  std::unique_ptr<views::DialogDelegate> remove_delegate_;
  std::unique_ptr<views::Widget> remove_widget_;

  // Address and history.
  std::unique_ptr<HistoryService> history_;
  std::vector<HistoryEntry> history_entries_;
  std::set<std::string> history_choices_;
  uint64_t history_generation_ = 0;
  // What the loaded rows were read against: a deletion or a catalogue change
  // since then sends a chosen row through a fresh query.
  uint64_t history_rows_deletions_ = 0;
  std::vector<Workspace> history_rows_workspaces_;
  std::string prepared_origin_;
  GURL prerendered_url_;
  std::set<std::string> bookmark_urls_;
  std::map<std::string, std::string> address_choices_;

  // Dashboard and tab moves.
  std::map<std::string, TabChoice> tab_choices_;
  std::map<std::string, base::WeakPtr<BrowserWindowInterface>> window_choices_;
  base::WeakPtr<BrowserWindowInterface> move_source_browser_;
  base::WeakPtr<content::WebContents> move_source_;

  // Reader view: the active tab while it shows the reader overlay.
  base::WeakPtr<content::WebContents> reader_contents_;
  base::RepeatingTimer reader_poll_;

  base::WeakPtrFactory<FlyWindowController> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_FLY_WINDOW_CONTROLLER_H_
