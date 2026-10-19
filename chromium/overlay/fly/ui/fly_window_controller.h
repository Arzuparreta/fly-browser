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
#include "base/timer/timer.h"
#include "components/prefs/pref_change_registrar.h"
#include "fly/commands/command_service.h"
#include "fly/commands/palette_catalog.h"
#include "fly/preferences/preferences_service.h"
#include "fly/workspaces/workspace_service.h"
#include "ui/events/event_handler.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class Profile;
class ScopedKeepAlive;
class GURL;
namespace content {
class WebContents;
}
namespace views {
class NativeViewHost;
class MdTextButton;
class View;
class Label;
}  // namespace views

namespace fly {
class CommandPalette;
class BookmarkService;
class StatusBar;
class KeyboardEditor;
class TabSidebar;
class HintController;
class KeyboardRouter;
// One OS window. Each entry keeps an upstream BrowserView/BrowserWidget, native
// tab model, profile and extension window ID. Its NativeWidgetAura does not
// create an OS window. NativeViewHost controls actual clipping and visibility.
class FlyWindowController : public views::WidgetDelegate,
                            public views::WidgetObserver,
                            public ui::EventHandler {
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
  views::Widget* desktop_widget() const { return desktop_.get(); }
  CommandService& commands() { return commands_; }
  BookmarkService& bookmarks() { return *bookmarks_; }
  CommandPalette& bookmark_picker() { return *bookmark_picker_; }
  PreferencesService& preferences() { return preferences_; }
  views::MdTextButton* clock_for_testing() { return clock_; }
  views::MdTextButton* preferences_button_for_testing() { return preferences_button_; }
  views::MdTextButton* bookmarks_button_for_testing() { return bookmarks_button_; }
  BrowserWindowInterface* selected_browser_for_testing() { return selected_; }
  CommandPalette& palette() { return *palette_; }
  KeyboardEditor& keyboard_editor() { return *keyboard_editor_; }
  HintController& hints() { return *hints_; }
  void UpdateModeLabel();
  void UpdateTargetURL(BrowserWindowInterface* browser,
                       content::WebContents* source,
                       const GURL& url);
  StatusBar* status_bar_for_testing() { return status_bar_; }
  TabSidebar* tab_sidebar_for_testing() { return tab_sidebar_; }
  void CreateTab();
  void CreateWorkspace();
  void ToggleWorkspacePicker();
  bool CanShowWorkspacePicker() const { return workspaces_.Read().has_value(); }
  CommandPalette& workspace_picker() { return *workspace_picker_; }
  bool CanTogglePrivate(BrowserWindowInterface* browser) const;
  views::View* workspace_buttons_for_testing() { return workspace_buttons_; }
  bool RunCommand(BrowserWindowInterface* browser, std::string_view action);

  void OnKeyEvent(ui::KeyEvent* event) override;

  // WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

 private:
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
  bool WorkspaceChoiceAvailable(std::string_view action) const;
  void ChooseWorkspace(content::WebContents* source, std::string action);
  void OnWorkspacesChanged();
  void OnPreferencesChanged();
  std::vector<PaletteEntry> BookmarkEntries();
  void OnBookmarksChanged();
  void OpenBookmark(content::WebContents* source, std::string url);
  void RemoveBookmark(content::WebContents* source, std::string url);
  void UpdateClock();
  void CycleClockFormat();
  void OnWidgetVisibilityChanged(views::Widget*, bool visible) override;
  void ScheduleWorkspaceRefresh();
  void RebuildWorkspaceButtons();
  void SelectWindow(base::WeakPtr<BrowserWindowInterface> browser);
  void OpenWorkspace(std::string id);
  void OnProfileReady(std::string id, Profile* profile);
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
  std::set<std::string> bookmark_urls_;
  std::optional<ThemePreferences> applied_theme_;
  PrefChangeRegistrar preferences_registrar_;
  base::RepeatingTimer clock_timer_;
  std::unique_ptr<HintController> hints_;
  std::unique_ptr<CommandPalette> palette_;
  std::unique_ptr<CommandPalette> bookmark_picker_;
  std::unique_ptr<CommandPalette> workspace_picker_;
  std::map<std::string, base::WeakPtr<BrowserWindowInterface>> private_choices_;
  std::unique_ptr<KeyboardEditor> keyboard_editor_;
  std::unique_ptr<KeyboardRouter> empty_router_;
  uint64_t empty_generation_ = 0;
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<views::Widget> desktop_;
  raw_ptr<views::View> workspace_buttons_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  raw_ptr<StatusBar> status_bar_ = nullptr;
  raw_ptr<views::MdTextButton> clock_ = nullptr;
  raw_ptr<views::MdTextButton> preferences_button_ = nullptr;
  raw_ptr<views::MdTextButton> bookmarks_button_ = nullptr;
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
