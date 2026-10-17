// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_WINDOW_CONTROLLER_H_
#define FLY_UI_FLY_WINDOW_CONTROLLER_H_

#include <map>
#include <memory>
#include <string>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "fly/commands/command_service.h"
#include "fly/workspaces/workspace_service.h"
#include "ui/events/event_handler.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "ui/views/widget/widget_observer.h"

class BrowserWindowInterface;
class Profile;
class ScopedKeepAlive;
namespace views {
class NativeViewHost;
class View;
class Label;
}  // namespace views

namespace fly {
// One OS window. Each entry keeps an upstream BrowserView/BrowserWidget, native
// tab model, profile and extension window ID. Its NativeWidgetAura does not
// create an OS window. NativeViewHost controls actual clipping and visibility.
class FlyWindowController : public views::WidgetDelegate,
                            public views::WidgetObserver,
                            public ui::EventHandler {
 public:
  static bool ShouldEmbed(BrowserWindowInterface* browser);
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
  views::Widget* desktop_widget() const { return desktop_.get(); }
  CommandService& commands() { return commands_; }
  void CreateWorkspace();
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
  };
  void RebuildWorkspaceButtons();
  void OpenWorkspace(std::string id);
  void OnProfileReady(std::string id, Profile* profile);
  void RequestClose(views::Widget::ClosedReason reason);
  void OnClosingAllBrowsers(bool closing);
  void FinishClose();
  void ArmCloseCallback();

  WorkspaceService workspaces_;
  CommandService commands_;
  std::unique_ptr<ScopedKeepAlive> keep_alive_;
  std::unique_ptr<views::Widget> desktop_;
  raw_ptr<views::View> workspace_buttons_ = nullptr;
  raw_ptr<views::View> content_ = nullptr;
  raw_ptr<views::Label> status_ = nullptr;
  std::map<BrowserWindowInterface*, Entry> entries_;
  raw_ptr<BrowserWindowInterface> selected_ = nullptr;
  bool closing_ = false;
  bool creating_profile_ = false;
  base::CallbackListSubscription close_subscription_;
  base::WeakPtrFactory<FlyWindowController> weak_factory_{this};
};
}  // namespace fly
#endif  // FLY_UI_FLY_WINDOW_CONTROLLER_H_
