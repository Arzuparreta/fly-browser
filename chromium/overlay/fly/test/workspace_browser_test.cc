// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/keyboard_editor.h"
#include "fly/bookmarks/bookmark_service.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "fly/sessions/tab_identity.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/tab_sidebar.h"
#include "fly/ui/status_bar.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/textfield/textfield.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/timer/timer.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"
#include "base/threading/thread_restrictions.h"
#include "base/task/single_thread_task_runner.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/bind.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/tabs/public/tab_interface.h"
#include "chrome/test/base/browser_created_waiter.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/favicon_status.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "ui/views/controls/button/image_button.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "components/prefs/pref_service.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/base/base_window.h"
#include "ui/events/test/event_generator.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/test/button_test_api.h"

namespace fly {
namespace {
class FlyWorkspaceBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch("fly-native");
  }
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
  }
  void TearDownOnMainThread() override {
    if (auto* window = FlyWindowController::GetIfCreated()) {
      window->desktop_widget()->Close();
      ASSERT_TRUE(base::test::RunUntil(
          [] { return FlyWindowController::GetIfCreated() == nullptr; }));
    }
  }

 protected:
  BrowserWindowInterface* SecondWorkspace() {
    auto* manager = g_browser_process->profile_manager();
    auto& profile = profiles::testing::CreateProfileSync(
        manager, manager->GenerateNextProfileDirectoryPath());
    base::test::TestFuture<BrowserWindowInterface*> future;
    BrowserWindowCreateParams params(profile, true);
    params.should_trigger_session_restore = false;
    CreateBrowserWindow(std::move(params), future.GetCallback());
    auto* second = future.Get();
    if (second) {
      chrome::NewTab(second, NewTabTypes::kNewTabCommand);
      FlyWindowController::Get().Select(second, true);
    }
    return second;
  }
  content::WebContents* Active(BrowserWindowInterface* window) {
    return window->GetTabStripModel()->GetActiveWebContents();
  }
  base::FilePath RestoreFixturePath() {
    return g_browser_process->profile_manager()->user_data_dir().AppendASCII(
        "fly-native-session-test.json");
  }
  base::DictValue SessionSnapshot() {
    base::ListValue windows;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          if (!FlyWindowController::ShouldEmbed(model) ||
              model->GetProfile()->IsOffTheRecord())
            return true;
          base::ListValue tabs;
          auto* strip = model->GetTabStripModel();
          for (int i = 0; i < strip->count(); ++i) {
            auto* contents = strip->GetWebContentsAt(i);
            tabs.Append(base::DictValue()
                            .Set("id", TabIdentity::Get(contents))
                            .Set("url", contents->GetVisibleURL().spec())
                            .Set("pinned", strip->IsTabPinned(i)));
          }
          windows.Append(
              base::DictValue()
                  .Set("directory",
                       model->GetProfile()->GetPath().BaseName().AsUTF8Unsafe())
                  .Set("tabs", std::move(tabs)));
          return true;
        });
    WorkspaceService service(*g_browser_process->local_state());
    const auto catalog = service.Read();
    EXPECT_TRUE(catalog);
    base::DictValue result;
    if (catalog) {
      base::ListValue workspaces;
      for (const auto& item : catalog->items)
        workspaces.Append(base::DictValue()
                              .Set("id", item.id)
                              .Set("directory", item.profile_directory)
                              .Set("name", item.name));
      result.Set("workspaces", std::move(workspaces));
      result.Set("active_id", catalog->active_id);
    }
    result.Set("windows", std::move(windows));
    return result;
  }
  void ExpectVisible(content::WebContents* contents, bool visible) {
    SCOPED_TRACE(contents->GetVisibleURL().spec());
    SCOPED_TRACE(visible ? "expected visible" : "expected hidden");
    ASSERT_TRUE(base::test::RunUntil([&] {
      return (contents->GetVisibility() == content::Visibility::VISIBLE) ==
             visible;
    }));
    EXPECT_EQ(visible ? "visible" : "hidden",
              content::EvalJs(contents, "document.visibilityState"));
  }
};

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       OneDesktopTwoProfilesAndNativeIds) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto* desktop = FlyWindowController::Get().desktop_widget();
  EXPECT_NE(first->GetProfile(), second->GetProfile());
  EXPECT_NE(first->GetSessionID(), second->GetSessionID());
  EXPECT_NE(extensions::ExtensionTabUtil::GetWindowId(first),
            extensions::ExtensionTabUtil::GetWindowId(second));
  EXPECT_EQ(desktop->GetNativeWindow()->GetHost(),
            first->GetWindow()->GetNativeWindow()->GetHost());
  EXPECT_EQ(desktop->GetNativeWindow()->GetHost(),
            second->GetWindow()->GetNativeWindow()->GetHost());
  EXPECT_NE(first->GetWindow()->GetNativeWindow(),
            second->GetWindow()->GetNativeWindow());
  EXPECT_EQ(extensions::ExtensionTabUtil::GetWindowId(first),
            extensions::ExtensionTabUtil::GetWindowIdOfTab(Active(first)));
  EXPECT_EQ(extensions::ExtensionTabUtil::GetWindowId(second),
            extensions::ExtensionTabUtil::GetWindowIdOfTab(Active(second)));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       CookiesAndLocalStorageDoNotCrossWorkspaces) {
  const auto url = embedded_test_server()->GetURL("/title1.html");
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, url));
  ASSERT_TRUE(content::ExecJs(Active(first),
                              "document.cookie='workspace=first;path=/"
                              "';localStorage.setItem('workspace','first')"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, url));
  EXPECT_EQ("", content::EvalJs(Active(second), "document.cookie"));
  EXPECT_EQ(true,
            content::EvalJs(Active(second),
                            "localStorage.getItem('workspace') === null"));
  ASSERT_TRUE(content::ExecJs(Active(second),
                              "document.cookie='workspace=second;path=/"
                              "';localStorage.setItem('workspace','second')"));
  FlyWindowController::Get().Select(first, true);
  EXPECT_EQ("workspace=first",
            content::EvalJs(Active(first), "document.cookie"));
  EXPECT_EQ("first", content::EvalJs(Active(first),
                                     "localStorage.getItem('workspace')"));
  EXPECT_EQ("workspace=second",
            content::EvalJs(Active(second), "document.cookie"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewNativeProfilesDoNotEnableBrowserAccountsOrVault) {
  auto check_defaults = [](Profile* profile) {
    auto* prefs = profile->GetPrefs();
    EXPECT_FALSE(prefs->GetBoolean(prefs::kSigninAllowed));
    EXPECT_FALSE(prefs->GetBoolean(prefs::kSigninAllowedOnNextStartup));
    for (const char* name : {
             password_manager::prefs::kCredentialsEnableService,
             password_manager::prefs::kCredentialsEnableAutosignin,
             password_manager::prefs::kCredentialsEnablePasskeys,
             password_manager::prefs::kAutomaticPasskeyUpgrades}) {
      EXPECT_FALSE(prefs->GetBoolean(name)) << name;
    }
  };
  check_defaults(browser()->GetProfile());
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  check_defaults(second->GetProfile());
  check_defaults(second->GetProfile()->GetPrimaryOTRProfile(true));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PrivateWorkspacesUseIsolatedEphemeralNativeProfiles) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  const auto url = embedded_test_server()->GetURL("/title1.html");
  window.Select(first, true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, url));
  ASSERT_TRUE(content::ExecJs(Active(first),
                              "document.cookie='normal=first;path=/'"));
  const auto catalog =
      g_browser_process->local_state()->GetDict(kWorkspacesPref).Clone();
  ASSERT_TRUE(NativeCommandAvailable(first, "toggle-private-mode"));
  ASSERT_TRUE(window.RunCommand(first, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != first;
  }));
  auto* private_first = window.selected_browser_for_testing();
  ASSERT_TRUE(private_first);
  ASSERT_TRUE(private_first->GetProfile()->IsOffTheRecord());
  EXPECT_EQ(first->GetProfile(),
            private_first->GetProfile()->GetOriginalProfile());
  EXPECT_EQ(window.desktop_widget()->GetNativeWindow()->GetHost(),
            private_first->GetWindow()->GetNativeWindow()->GetHost());
  EXPECT_EQ(catalog, g_browser_process->local_state()->GetDict(kWorkspacesPref));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_first, url));
  EXPECT_EQ("", content::EvalJs(Active(private_first), "document.cookie"));
  ASSERT_TRUE(content::ExecJs(
      Active(private_first),
      "document.cookie='private=first;path=/';localStorage.setItem('private','first')"));
  std::map<std::string, std::string> extra;
  TabIdentity::Populate(Active(private_first), &extra);
  EXPECT_TRUE(extra.empty());
  ExpectVisible(Active(first), false);
  window.Select(second, true);
  ASSERT_TRUE(window.RunCommand(second, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != second;
  }));
  auto* private_second = window.selected_browser_for_testing();
  ASSERT_TRUE(private_second);
  ASSERT_TRUE(private_second->GetProfile()->IsOffTheRecord());
  EXPECT_NE(private_first->GetProfile(), private_second->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_second, url));
  EXPECT_EQ("", content::EvalJs(Active(private_second), "document.cookie"));
  EXPECT_EQ(true, content::EvalJs(Active(private_second),
                                 "localStorage.getItem('private') === null"));
  ASSERT_TRUE(window.RunCommand(private_second, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !second->GetProfile()->HasPrimaryOTRProfile();
  }));
  EXPECT_EQ(second, window.selected_browser_for_testing());
  window.Select(private_first, true);
  ASSERT_TRUE(window.RunCommand(private_first, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !first->GetProfile()->HasPrimaryOTRProfile();
  }));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_EQ("normal=first", content::EvalJs(Active(first), "document.cookie"));
  ASSERT_TRUE(window.RunCommand(first, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != first;
  }));
  auto* fresh = window.selected_browser_for_testing();
  ASSERT_TRUE(fresh);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(fresh, url));
  EXPECT_EQ("", content::EvalJs(Active(fresh), "document.cookie"));
  EXPECT_EQ(true, content::EvalJs(Active(fresh),
                                 "localStorage.getItem('private') === null"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ProductNewTabsAreBlankInPersistentAndPrivateProfiles) {
  auto* original = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      original, embedded_test_server()->GetURL("/title1.html")));
  auto& window = FlyWindowController::Get();
  const int count = original->GetTabStripModel()->count();
  ASSERT_TRUE(InvokeNativeCommand(original, "spawn-tab"));
  ASSERT_EQ(count + 1, original->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(original)));
  EXPECT_EQ(GURL("about:blank"), Active(original)->GetLastCommittedURL());
  EXPECT_EQ(original->GetProfile(), Active(original)->GetBrowserContext());

  window.CreateTab();
  ASSERT_EQ(count + 2, original->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(original)));
  EXPECT_EQ(GURL("about:blank"), Active(original)->GetLastCommittedURL());
  ASSERT_TRUE(OpenNativeBlankTab(original, 1));
  auto* inserted = original->GetTabStripModel()->GetWebContentsAt(1);
  ASSERT_EQ(inserted, Active(original));
  ASSERT_TRUE(content::WaitForLoadStop(inserted));
  EXPECT_EQ(GURL("about:blank"), inserted->GetLastCommittedURL());

  ASSERT_TRUE(window.RunCommand(original, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != original;
  }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(private_browser);
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(content::WaitForLoadStop(Active(private_browser)));
  EXPECT_EQ(GURL("about:blank"), Active(private_browser)->GetLastCommittedURL());
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "spawn-tab"));
  ASSERT_TRUE(content::WaitForLoadStop(Active(private_browser)));
  EXPECT_EQ(GURL("about:blank"), Active(private_browser)->GetLastCommittedURL());
  EXPECT_EQ(private_browser->GetProfile(),
            Active(private_browser)->GetBrowserContext());

  window.Select(original, true);
  window.CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected != original && selected != private_browser &&
           selected->GetTabStripModel()->count() == 1;
  }));
  auto* created = window.selected_browser_for_testing();
  ASSERT_FALSE(created->GetProfile()->IsOffTheRecord());
  EXPECT_NE(original->GetProfile(), created->GetProfile());
  ASSERT_TRUE(content::WaitForLoadStop(Active(created)));
  EXPECT_EQ(GURL("about:blank"), Active(created)->GetLastCommittedURL());

  // Splitting a one-tab workspace must create the second pane through the
  // same profile-bound blank navigation, without opening Chromium's NTP.
  ASSERT_TRUE(ExecuteNativeCommand(created, "toggle-split"));
  auto* split_model = created->GetTabStripModel();
  ASSERT_EQ(2, split_model->count());
  ASSERT_TRUE(split_model->GetSplitForTab(split_model->active_index()));
  for (int index = 0; index < split_model->count(); ++index) {
    auto* pane = split_model->GetWebContentsAt(index);
    ASSERT_TRUE(content::WaitForLoadStop(pane));
    EXPECT_EQ(GURL("about:blank"), pane->GetLastCommittedURL());
    EXPECT_EQ(created->GetProfile(), pane->GetBrowserContext());
    ExpectVisible(pane, true);
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PrivateExitHonoursBeforeUnloadCancellation) {
  auto* normal = browser();
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(normal, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != normal;
  }));
  auto* private_model = window.selected_browser_for_testing();
  ASSERT_TRUE(private_model);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      private_model, embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(private_model);
  ASSERT_TRUE(content::ExecJs(contents,
                              "window.onbeforeunload = () => 'unsaved'"));
  content::PrepContentsForBeforeUnloadTest(contents);
  class CancellationObserver : public content::WebContentsObserver {
   public:
    explicit CancellationObserver(content::WebContents* contents)
        : content::WebContentsObserver(contents) {}
    void BeforeUnloadDialogCancelled() override { cancelled = true; }
    bool cancelled = false;
  } cancellation(contents);
  ASSERT_TRUE(window.RunCommand(private_model, "toggle-private-mode"));
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  ASSERT_TRUE(dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  EXPECT_EQ(private_model, window.selected_browser_for_testing());
  EXPECT_TRUE(normal->GetProfile()->HasPrimaryOTRProfile());
  ExpectVisible(contents, true);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null"));
  ASSERT_TRUE(window.RunCommand(private_model, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !normal->GetProfile()->HasPrimaryOTRProfile();
  }));
  EXPECT_EQ(normal, window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       DesktopQuitHonoursBeforeUnloadCancellation) {
  auto first = browser()->GetWeakPtr();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto second_alive = second->GetWeakPtr();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(second);
  ASSERT_TRUE(content::ExecJs(contents,
                             "window.onbeforeunload = () => 'unsaved'"));
  content::PrepContentsForBeforeUnloadTest(contents);
  class CancellationObserver : public content::WebContentsObserver {
   public:
    explicit CancellationObserver(content::WebContents* contents)
        : content::WebContentsObserver(contents) {}
    void BeforeUnloadDialogCancelled() override { cancelled = true; }
    bool cancelled = false;
  } cancellation(contents);
  auto& window = FlyWindowController::Get();
  window.desktop_widget()->Close();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  ASSERT_TRUE(dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second_alive);
  EXPECT_EQ(&window, FlyWindowController::GetIfCreated());
  EXPECT_EQ(second, window.selected_browser_for_testing());
  ExpectVisible(contents, true);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null"));
  const int count = second->GetTabStripModel()->count();
  ASSERT_TRUE(window.RunCommand(second, "spawn-tab"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return second->GetTabStripModel()->count() == count + 1; }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SwitchingWorkspacesUsesNativeVisibility) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* first_contents = Active(first);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title2.html")));
  ExpectVisible(first_contents, false);
  ExpectVisible(Active(second), true);
  FlyWindowController::Get().Select(first, true);
  ExpectVisible(first_contents, true);
  ExpectVisible(Active(second), false);
  EXPECT_TRUE(first->GetWindow()->IsActive());
  EXPECT_FALSE(second->GetWindow()->IsActive());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeSplitShowsBothPanesAndKeepsOneActiveTab) {
  auto* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* first = Active(browser());
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title2.html")));
  auto* second = Active(browser());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "toggle-split"));
  ASSERT_TRUE(model->GetSplitForTab(model->active_index()));
  ExpectVisible(first, true);
  ExpectVisible(second, true);
  const auto active_before = model->active_index();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "switch-split-focus"));
  EXPECT_NE(active_before, model->active_index());
  ExpectVisible(first, true);
  ExpectVisible(second, true);
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "cycle-split-orientation"));
  ExpectVisible(first, true);
  ExpectVisible(second, true);
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "cycle-split-orientation"));
  EXPECT_FALSE(model->GetSplitForTab(model->active_index()));
  ExpectVisible(Active(browser()), true);
}
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarUsesTabIdentityAfterMoveAndSwitch) {
  auto* model = browser()->GetTabStripModel();
  auto* first = Active(browser());
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar->row_count_for_testing() == 2; }));
  auto* original_row = sidebar->row_for_testing(first);
  ASSERT_TRUE(original_row);
  auto* select = static_cast<views::Button*>(original_row->children()[0]);
  auto* pin = static_cast<views::Button*>(original_row->children()[1]);
  model->MoveWebContentsAt(model->GetIndexOfWebContents(first), 1, false);
  // Click before the deferred projection update. A cached index would choose
  // the other tab; the row must resolve its WebContents in the current model.
  views::test::ButtonTestApi(select).NotifyDefaultMouseClick();
  EXPECT_EQ(first, Active(browser()));
  views::test::ButtonTestApi(pin).NotifyDefaultMouseClick();
  EXPECT_TRUE(model->IsTabPinned(model->GetIndexOfWebContents(first)));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->row_for_testing(first) == original_row &&
           sidebar->row_count_for_testing() == 2;
  }));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->row_count_for_testing() == 1 &&
           sidebar->row_for_testing(Active(second)) != nullptr;
  }));
  EXPECT_EQ(nullptr, sidebar->row_for_testing(first));
  FlyWindowController::Get().Select(browser(), true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->row_count_for_testing() == 2 &&
           sidebar->row_for_testing(first) != nullptr;
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BlankPopupRetainsOpenerOriginAndSidebarIdentity) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* opener = Active(browser());
  const std::string opener_id = TabIdentity::Get(opener);
  auto* strip = browser()->GetTabStripModel();
  const int count = strip->count();
  content::WebContentsAddedObserver added;
  ASSERT_TRUE(content::ExecJs(opener, "window.open('about:blank', '_blank')"));
  auto* blank = added.GetWebContents();
  ASSERT_TRUE(blank);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return strip->count() == count + 1 &&
           strip->GetIndexOfWebContents(blank) != TabStripModel::kNoTab;
  }));
  ASSERT_TRUE(content::WaitForLoadStop(blank));
  EXPECT_EQ(GURL("about:blank"), blank->GetLastCommittedURL());
  EXPECT_EQ(opener->GetBrowserContext(), blank->GetBrowserContext());
  EXPECT_EQ(opener->GetPrimaryMainFrame(), blank->GetOpener());
  EXPECT_EQ(opener->GetPrimaryMainFrame()->GetLastCommittedOrigin(),
            blank->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  EXPECT_EQ(opener->GetLastCommittedURL().spec(),
            content::EvalJs(blank, "window.opener.location.href"));
  const std::string blank_id = TabIdentity::Get(blank);
  const int native_id = extensions::ExtensionTabUtil::GetTabId(blank);
  EXPECT_NE(opener_id, blank_id);
  auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->row_for_testing(blank) &&
           sidebar->row_count_for_testing() == static_cast<size_t>(count + 1);
  }));
  auto* row = sidebar->row_for_testing(blank);
  // Navigating the existing WebContents must not reconstruct a tab or row.
  ASSERT_TRUE(content::NavigateToURL(
      blank, embedded_test_server()->GetURL("/title2.html")));
  EXPECT_EQ(blank_id, TabIdentity::Get(blank));
  EXPECT_EQ(native_id, extensions::ExtensionTabUtil::GetTabId(blank));
  EXPECT_EQ(row, sidebar->row_for_testing(blank));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ExtensionBlankIsNativeProfileScopedAndReopenable) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  const int second_count = second->GetTabStripModel()->count();
  FlyWindowController::Get().Select(browser(), true);
  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue()
          .Set("manifest_version", 3)
          .Set("name", "Fly blank tab probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("permissions", base::ListValue().Append("tabs")));
  directory.WriteFile(FILE_PATH_LITERAL("probe.html"),
                      "<!doctype html><title>Blank tab probe</title>");
  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  const auto extension = loader.LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(extension);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("probe.html")));
  auto* opener = Active(browser());
  auto* strip = browser()->GetTabStripModel();
  const int count = strip->count();
  auto result =
      base::JSONReader::ReadDict(content::EvalJs(opener, R"js((async () => {
        const current = await chrome.tabs.getCurrent();
        const tab = await chrome.tabs.create({url: 'about:blank', active: true,
          windowId: current.windowId, openerTabId: current.id});
        return JSON.stringify(tab);
      })())js")
                                     .ExtractString(),
                                 base::JSON_PARSE_RFC);
  ASSERT_TRUE(result);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return strip->count() == count + 1; }));
  auto* blank = Active(browser());
  ASSERT_NE(opener, blank);
  ASSERT_TRUE(content::WaitForLoadStop(blank));
  EXPECT_EQ(GURL("about:blank"), blank->GetLastCommittedURL());
  EXPECT_EQ(browser()->GetProfile(), blank->GetBrowserContext());
  EXPECT_EQ(extensions::ExtensionTabUtil::GetTabId(blank),
            result->FindInt("id"));
  EXPECT_EQ(extensions::ExtensionTabUtil::GetWindowId(browser()),
            result->FindInt("windowId"));
  EXPECT_EQ(extensions::ExtensionTabUtil::GetTabId(opener),
            result->FindInt("openerTabId"));
  EXPECT_EQ(strip->GetTabAtIndex(strip->GetIndexOfWebContents(opener)),
            strip->GetOpenerOfTabAt(strip->GetIndexOfWebContents(blank)));
  EXPECT_EQ(second_count, second->GetTabStripModel()->count());
  const std::string id = TabIdentity::Get(blank);
  EXPECT_NE(TabIdentity::Get(opener), id);
  auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return sidebar->row_for_testing(blank) &&
           sidebar->row_count_for_testing() == static_cast<size_t>(count + 1);
  }));
  auto original = blank->GetWeakPtr();
  strip->CloseWebContentsAt(strip->GetIndexOfWebContents(blank),
                            TabCloseTypes::CLOSE_USER_GESTURE |
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] { return !original; }));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "reopen-closed-tab"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return strip->count() == count + 1; }));
  EXPECT_EQ(GURL("about:blank"), Active(browser())->GetVisibleURL());
  EXPECT_EQ(id, TabIdentity::Get(Active(browser())));
  EXPECT_EQ(second_count, second->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarUsesNativeFaviconAndKeepsBlankTabRow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html")));
  auto* contents = Active(browser());
  auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* entry = contents->GetController().GetVisibleEntry();
    auto* row = sidebar->row_for_testing(contents);
    if (!entry || !entry->GetFavicon().valid ||
        entry->GetFavicon().image.IsEmpty() || !row)
      return false;
    const auto& image = static_cast<views::MdTextButton*>(row->children()[0])
                            ->GetImageModel(views::Button::STATE_NORMAL);
    return image && image->IsImage();
  }));
  auto* row = sidebar->row_for_testing(contents);
  auto* select = static_cast<views::MdTextButton*>(row->children()[0]);
  EXPECT_EQ(gfx::Size(16, 16), select->GetImage(views::Button::STATE_NORMAL).size());
  const std::string id = TabIdentity::Get(contents);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto& image = select->GetImageModel(views::Button::STATE_NORMAL);
    return image && image->IsVectorIcon();
  }));
  EXPECT_EQ(row, sidebar->row_for_testing(contents));
  EXPECT_EQ(id, TabIdentity::Get(contents));
  EXPECT_EQ(GURL("about:blank"), contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       DevToolsUsesTheSelectedNativeTabAndWorkspace) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* inspected = Active(first);
  const int tab_id = extensions::ExtensionTabUtil::GetTabId(inspected);
  ASSERT_TRUE(NativeCommandAvailable(first, "toggle-devtools"));
  ASSERT_TRUE(InvokeNativeCommand(first, "toggle-devtools"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DevToolsWindow::GetInTabWebContents(inspected, nullptr) != nullptr;
  }));
  auto* frontend = DevToolsWindow::GetInTabWebContents(inspected, nullptr);
  ASSERT_TRUE(frontend);
  EXPECT_EQ(first->GetProfile(),
            Profile::FromBrowserContext(frontend->GetBrowserContext())
                ->GetOriginalProfile());
  EXPECT_EQ(inspected, Active(first));
  EXPECT_EQ(tab_id, extensions::ExtensionTabUtil::GetTabId(inspected));
  ExpectVisible(inspected, true);
  ExpectVisible(frontend, true);
  ASSERT_TRUE(InvokeNativeCommand(first, "toggle-devtools"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DevToolsWindow::GetInstanceForInspectedWebContents(inspected) == nullptr;
  }));

  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  auto* other_tab = Active(other);
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-devtools"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DevToolsWindow::GetInTabWebContents(other_tab, nullptr) != nullptr;
  }));
  auto* other_frontend = DevToolsWindow::GetInTabWebContents(other_tab, nullptr);
  ASSERT_TRUE(other_frontend);
  EXPECT_EQ(other->GetProfile(),
            Profile::FromBrowserContext(other_frontend->GetBrowserContext())
                ->GetOriginalProfile());
  EXPECT_EQ(nullptr, DevToolsWindow::GetInstanceForInspectedWebContents(inspected));
  EXPECT_EQ(other, FlyWindowController::Get().selected_browser_for_testing());
  ExpectVisible(inspected, false);
  ExpectVisible(other_tab, true);
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-devtools"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return DevToolsWindow::GetInstanceForInspectedWebContents(other_tab) == nullptr;
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       GlobalMediaUsesNativeSessionHandlersInHiddenWorkspaces) {
  auto* playing_browser = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      playing_browser, embedded_test_server()->GetURL("/title1.html")));
  auto* playing = Active(playing_browser);
  ASSERT_TRUE(content::ExecJs(playing, R"(
    const audio = document.createElement('audio');
    audio.id = 'fly-media';
    audio.loop = true;
    audio.src = '/media/engagement/engagement_valid_audio.webm';
    document.body.append(audio);
    navigator.mediaSession.setActionHandler('pause', () => {
      audio.pause();
      document.title = 'paused-by-native-session';
    });
    navigator.mediaSession.setActionHandler('play', async () => {
      await audio.play();
      document.title = 'played-by-native-session';
    });
    audio.play();
  )"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* session = content::MediaSession::GetIfExists(playing);
    if (!session)
      return false;
    const auto info = session->GetMediaSessionInfoSync();
    return info && info->is_controllable &&
           info->playback_state ==
               media_session::mojom::MediaPlaybackState::kPlaying;
  }));
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  auto& window = FlyWindowController::Get();
  auto* empty_tab = Active(other);
  auto* empty_session = content::MediaSession::GetIfExists(empty_tab);
  ExpectVisible(playing, false);
  content::TitleWatcher paused(playing, u"paused-by-native-session");
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-global-media"));
  EXPECT_EQ(u"paused-by-native-session", paused.WaitAndGetTitle());
  EXPECT_EQ(true, content::EvalJs(playing,
                                 "document.getElementById('fly-media').paused"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto info = content::MediaSession::GetIfExists(playing)
                          ->GetMediaSessionInfoSync();
    return info && info->is_controllable &&
           info->playback_state ==
               media_session::mojom::MediaPlaybackState::kPaused;
  }));
  content::TitleWatcher resumed(playing, u"played-by-native-session");
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-global-media"));
  EXPECT_EQ(u"played-by-native-session", resumed.WaitAndGetTitle());
  EXPECT_EQ(false, content::EvalJs(playing,
                                  "document.getElementById('fly-media').paused"));
  EXPECT_EQ(other, window.selected_browser_for_testing());
  EXPECT_EQ(empty_tab, Active(other));
  EXPECT_EQ(empty_session, content::MediaSession::GetIfExists(empty_tab));
  ExpectVisible(playing, false);
  ExpectVisible(empty_tab, true);
  ASSERT_TRUE(content::ExecJs(playing,
                              "document.getElementById('fly-media').pause()"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       GlobalMediaFallsBackToNativePlayersWithoutSiteHandlers) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* playing = Active(first);
  ASSERT_TRUE(content::ExecJs(playing, R"(
    const audio = document.createElement('audio');
    audio.id = 'fly-media';
    audio.loop = true;
    audio.src = '/media/engagement/engagement_valid_audio.webm';
    document.body.append(audio);
    audio.play();
  )"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* session = content::MediaSession::GetIfExists(playing);
    if (!session)
      return false;
    const auto info = session->GetMediaSessionInfoSync();
    return info && info->is_controllable &&
           info->playback_state ==
               media_session::mojom::MediaPlaybackState::kPlaying;
  }));
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-global-media"));
  EXPECT_EQ(true, content::EvalJs(playing, R"(
    new Promise(resolve => {
      const audio = document.getElementById('fly-media');
      if (audio.paused) resolve(true);
      else audio.addEventListener('pause', () => resolve(audio.paused),
                                  {once: true});
    })
  )", content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto info = content::MediaSession::GetIfExists(playing)
                          ->GetMediaSessionInfoSync();
    return info && info->is_controllable &&
           info->playback_state ==
               media_session::mojom::MediaPlaybackState::kPaused;
  }));
  ASSERT_TRUE(InvokeNativeCommand(other, "toggle-global-media"));
  EXPECT_EQ(true, content::EvalJs(playing, R"(
    new Promise(resolve => {
      const audio = document.getElementById('fly-media');
      if (!audio.paused) resolve(true);
      else audio.addEventListener('playing', () => resolve(!audio.paused),
                                  {once: true});
    })
  )", content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ(other, FlyWindowController::Get().selected_browser_for_testing());
  ExpectVisible(playing, false);
  ExpectVisible(Active(other), true);
  ASSERT_TRUE(content::ExecJs(playing,
                              "document.getElementById('fly-media').pause()"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       GlobalMuteUsesNativeTabsAcrossHiddenAndPrivateWorkspaces) {
  auto* first = browser();
  auto& window = FlyWindowController::Get();
  auto* first_tab = Active(first);
  ASSERT_TRUE(OpenNativeBlankTab(first));
  auto* second_tab = Active(first);
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  auto* hidden_tab = Active(other);
  ASSERT_TRUE(window.RunCommand(other, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != other;
  }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(private_browser);
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  auto* private_tab = Active(private_browser);
  ASSERT_TRUE(private_tab);
  EXPECT_TRUE(NativeCommandAvailable(private_browser, "toggle-global-audio-mute"));

  // Mixed tab state always converges to muted, including nonaudible pages.
  SetTabAudioMuted(first_tab, true, TabMutedReason::kAudioIndicator, {});
  const std::vector<content::WebContents*> tabs = {
      first_tab, second_tab, hidden_tab, private_tab};
  const std::vector<int> ids = {
      extensions::ExtensionTabUtil::GetTabId(first_tab),
      extensions::ExtensionTabUtil::GetTabId(second_tab),
      extensions::ExtensionTabUtil::GetTabId(hidden_tab),
      extensions::ExtensionTabUtil::GetTabId(private_tab)};
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-global-audio-mute"));
  for (auto* tab : tabs) {
    EXPECT_TRUE(tab->IsAudioMuted());
    EXPECT_EQ(TabMutedReason::kAudioIndicator, GetTabAudioMutedReason(tab));
  }
  EXPECT_EQ(private_browser, window.selected_browser_for_testing());
  EXPECT_EQ(second_tab, Active(first));
  EXPECT_EQ(hidden_tab, Active(other));
  ExpectVisible(second_tab, false);
  ExpectVisible(hidden_tab, false);
  ExpectVisible(private_tab, true);

  ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-global-audio-mute"));
  for (size_t index = 0; index < tabs.size(); ++index) {
    EXPECT_FALSE(tabs[index]->IsAudioMuted());
    EXPECT_EQ(ids[index], extensions::ExtensionTabUtil::GetTabId(tabs[index]));
  }
  // This is an operation on existing tabs, not a persisted global mute policy.
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-global-audio-mute"));
  ASSERT_TRUE(OpenNativeBlankTab(private_browser));
  EXPECT_FALSE(Active(private_browser)->IsAudioMuted());
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-global-audio-mute"));
  EXPECT_TRUE(Active(private_browser)->IsAudioMuted());
  EXPECT_EQ(private_browser->GetProfile(),
            Active(private_browser)->GetBrowserContext());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarAndCommandsExposeNativeMuteReasonsToExtensions) {
  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue().Set("manifest_version", 3)
          .Set("name", "Fly native mute probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("permissions", base::ListValue().Append("tabs")));
  directory.WriteFile(FILE_PATH_LITERAL("probe.html"),
                      "<!doctype html><title>Mute probe</title>");
  extensions::ChromeTestExtensionLoader loader(browser()->GetProfile());
  auto extension = loader.LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(extension);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), extension->GetResourceURL("probe.html")));
  auto* contents = Active(browser());
  const int native_id = extensions::ExtensionTabUtil::GetTabId(contents);
  auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
  auto read_info = [&] {
    return base::JSONReader::ReadDict(content::EvalJs(contents, content::JsReplace(
        "(async () => JSON.stringify((await chrome.tabs.get($1)).mutedInfo))()",
        native_id)).ExtractString(), base::JSON_PARSE_RFC);
  };
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "toggle-tab-audio-mute"));
  auto info = read_info();
  ASSERT_TRUE(info);
  EXPECT_EQ(true, info->FindBool("muted"));
  ASSERT_TRUE(info->FindString("reason"));
  EXPECT_EQ("user", *info->FindString("reason"));
  EXPECT_FALSE(info->contains("extensionId"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* audio = sidebar->audio_for_testing(contents);
    return audio && audio->GetVisible();
  }));
  ASSERT_TRUE(content::ExecJs(contents, content::JsReplace(
      "chrome.tabs.update($1, {muted: false})", native_id)));
  info = read_info();
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("reason"));
  ASSERT_TRUE(info->FindString("extensionId"));
  EXPECT_EQ("extension", *info->FindString("reason"));
  EXPECT_EQ(extension->id(), *info->FindString("extensionId"));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "toggle-tab-audio-mute"));
  info = read_info();
  ASSERT_TRUE(info);
  EXPECT_EQ("user", *info->FindString("reason"));
  EXPECT_FALSE(info->contains("extensionId"));
  auto* audio = sidebar->audio_for_testing(contents);
  ASSERT_TRUE(audio);
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  auto* other = Active(browser());
  auto* strip = browser()->GetTabStripModel();
  strip->MoveWebContentsAt(strip->GetIndexOfWebContents(contents), 1, false);
  // A background row must still target its own contents before refresh.
  views::test::ButtonTestApi(audio).NotifyDefaultMouseClick();
  EXPECT_FALSE(contents->IsAudioMuted());
  EXPECT_FALSE(other->IsAudioMuted());
  EXPECT_EQ(other, Active(browser()));
  EXPECT_EQ(TabMutedReason::kAudioIndicator, GetTabAudioMutedReason(contents));
  EXPECT_EQ(native_id, extensions::ExtensionTabUtil::GetTabId(contents));
  info = read_info();
  ASSERT_TRUE(info);
  ASSERT_TRUE(info->FindString("reason"));
  EXPECT_EQ(false, info->FindBool("muted"));
  EXPECT_EQ("user", *info->FindString("reason"));
  EXPECT_FALSE(info->contains("extensionId"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-global-audio-mute"));
  info = read_info();
  ASSERT_TRUE(info);
  EXPECT_EQ(true, info->FindBool("muted"));
  ASSERT_TRUE(info->FindString("reason"));
  EXPECT_EQ("user", *info->FindString("reason"));
  EXPECT_FALSE(info->contains("extensionId"));
  EXPECT_TRUE(other->IsAudioMuted());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-global-audio-mute"));
  info = read_info();
  ASSERT_TRUE(info);
  EXPECT_EQ(false, info->FindBool("muted"));
  EXPECT_FALSE(other->IsAudioMuted());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeCloseAndReopenPreserveFlyIdentity) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* first = Active(browser());
  const std::string id = TabIdentity::Get(first);
  auto original = first->GetWeakPtr();
  EXPECT_TRUE(base::Uuid::ParseLowercase(id).is_valid());
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  EXPECT_NE(id, TabIdentity::Get(Active(browser())));
  auto* model = browser()->GetTabStripModel();
  model->CloseWebContentsAt(model->GetIndexOfWebContents(first),
                            TabCloseTypes::CLOSE_USER_GESTURE |
                                TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] { return !original; }));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "reopen-closed-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == 2; }));
  EXPECT_EQ(id, TabIdentity::Get(Active(browser())));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       TabIdentityRejectsLiveCollisionsAndPrivatePersistence) {
  const std::string id = TabIdentity::Get(Active(browser()));
  std::map<std::string, std::string> extra{{TabIdentity::kExtraDataKey, id}};
  auto restored = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  TabIdentity::Restore(restored.get(), extra);
  EXPECT_NE(id, TabIdentity::Get(restored.get()));
  const std::string replacement = TabIdentity::Get(restored.get());
  extra[TabIdentity::kExtraDataKey] =
      base::Uuid::GenerateRandomV4().AsLowercaseString();
  TabIdentity::Restore(restored.get(), extra);
  EXPECT_EQ(replacement, TabIdentity::Get(restored.get()));
  auto invalid = content::WebContents::Create(
      content::WebContents::CreateParams(browser()->GetProfile()));
  extra[TabIdentity::kExtraDataKey] = "../invalid-session-id";
  TabIdentity::Restore(invalid.get(), extra);
  EXPECT_TRUE(
      base::Uuid::ParseLowercase(TabIdentity::Get(invalid.get())).is_valid());
  auto private_tab =
      content::WebContents::Create(content::WebContents::CreateParams(
          browser()->GetProfile()->GetPrimaryOTRProfile(
              /*create_if_needed=*/true)));
  std::map<std::string, std::string> private_data;
  TabIdentity::Populate(private_tab.get(), &private_data);
  EXPECT_TRUE(private_data.empty());
  TabIdentity::Restore(private_tab.get(), extra);
  EXPECT_FALSE(TabIdentity::CanPersist(private_tab.get()));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ClosingSelectedWorkspaceReleasesSidebarObservation) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.IsSelected(second));
  second->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.IsSelected(first) &&
           window.tab_sidebar_for_testing()->row_for_testing(Active(first));
  }));
  EXPECT_EQ(first->GetTabStripModel()->count(),
            static_cast<int>(
                window.tab_sidebar_for_testing()->row_count_for_testing()));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeExtensionIdsEventsAndStorageAreProfileScoped) {
  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue()
          .Set("manifest_version", 3)
          .Set("name", "Fly native profile probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("permissions",
               base::ListValue().Append("tabs").Append("storage"))
          .Set("background",
               base::DictValue().Set("service_worker", "worker.js"))
          .Set("content_scripts",
               base::ListValue().Append(
                   base::DictValue()
                       .Set("matches",
                            base::ListValue().Append("http://127.0.0.1/*"))
                       .Set("js", base::ListValue().Append("probe.js")))));
  directory.WriteFile(FILE_PATH_LITERAL("worker.js"), R"js(
    let tail = Promise.resolve();
    function update(callback) {
      tail = tail.then(async () => {
        const state = await chrome.storage.local.get();
        state.identity ||= crypto.randomUUID();
        await callback(state);
        await chrome.storage.local.set(state);
        return state;
      });
      return tail;
    }
    chrome.runtime.onInstalled.addListener(event => update(state => {
      state.installed = event.reason;
    }));
    chrome.tabs.onActivated.addListener(event => update(state => {
      state.activated = event.tabId;
    }));
    chrome.runtime.onMessage.addListener((message, sender, reply) => {
      if (message !== 'fly-native-probe' || !sender.tab) return;
      update(async state => {
        const active = await chrome.tabs.query({active: true, currentWindow: true});
        const nativeWindow = await chrome.windows.get(sender.tab.windowId);
        reply({nativeWindow, identity: state.identity, installed: state.installed,
               tabId: sender.tab.id, windowId: sender.tab.windowId,
               activeTabId: active[0]?.id, activated: state.activated});
      });
      return true;
    });
  )js");
  directory.WriteFile(FILE_PATH_LITERAL("probe.js"), R"js(
    chrome.runtime.sendMessage('fly-native-probe').then(result => {
      document.documentElement.dataset.flyNativeProbe = JSON.stringify(result);
    }).catch(error => {
      document.documentElement.dataset.flyNativeProbe = JSON.stringify({error: error.message});
    });
  )js");
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  extensions::ChromeTestExtensionLoader first_loader(browser()->GetProfile());
  extensions::ChromeTestExtensionLoader second_loader(second->GetProfile());
  auto first_extension = first_loader.LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(first_extension);
  auto second_extension = second_loader.LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(second_extension);
  EXPECT_EQ(first_extension->id(), second_extension->id());
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  chrome::NewTab(second, NewTabTypes::kNewTabCommand);
  auto probe = [&](BrowserWindowInterface* target) {
    FlyWindowController::Get().Select(target, true);
    EXPECT_TRUE(ui_test_utils::NavigateToURL(
        target, embedded_test_server()->GetURL("/title1.html")));
    return base::JSONReader::ReadDict(
        content::EvalJs(Active(target), R"js(new Promise(resolve => {
          const read = () => {
            const value = document.documentElement.dataset.flyNativeProbe;
            if (value) { observer?.disconnect(); resolve(value); }
          };
          const observer = new MutationObserver(read);
          observer.observe(document.documentElement, {attributes: true});
          read();
        }))js")
            .ExtractString(),
        base::JSON_PARSE_RFC);
  };
  const auto first_data = probe(browser());
  const auto second_data = probe(second);
  ASSERT_TRUE(first_data);
  ASSERT_TRUE(second_data);
  ASSERT_TRUE(first_data->FindString("identity"));
  ASSERT_TRUE(second_data->FindString("identity"));
  EXPECT_NE(*first_data->FindString("identity"),
            *second_data->FindString("identity"));
  for (const auto& [target, data] :
       {std::pair<BrowserWindowInterface*, const base::DictValue*>(
            browser(), &*first_data),
        std::pair<BrowserWindowInterface*, const base::DictValue*>(
            second, &*second_data)}) {
    ASSERT_TRUE(data->FindString("installed"));
    EXPECT_EQ("install", *data->FindString("installed"));
    EXPECT_EQ(extensions::ExtensionTabUtil::GetTabId(Active(target)),
              data->FindInt("tabId"));
    EXPECT_EQ(extensions::ExtensionTabUtil::GetWindowId(target),
              data->FindInt("windowId"));
    EXPECT_EQ(data->FindInt("tabId"), data->FindInt("activeTabId"));
    EXPECT_EQ(data->FindInt("tabId"), data->FindInt("activated"));
    const auto* native_window = data->FindDict("nativeWindow");
    ASSERT_TRUE(native_window);
    const auto bounds =
        FlyWindowController::Get().desktop_widget()->GetWindowBoundsInScreen();
    EXPECT_EQ(bounds.x(), native_window->FindInt("left"));
    EXPECT_EQ(bounds.y(), native_window->FindInt("top"));
    EXPECT_EQ(bounds.width(), native_window->FindInt("width"));
    EXPECT_EQ(bounds.height(), native_window->FindInt("height"));
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BrowserBoundsRepresentSharedDesktopNotPageViewport) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  auto* desktop = window.desktop_widget();
  auto expect_shared_bounds = [&] {
    const auto bounds = desktop->GetWindowBoundsInScreen();
    EXPECT_EQ(bounds, first->GetWindow()->GetBounds());
    EXPECT_EQ(bounds, second->GetWindow()->GetBounds());
    EXPECT_EQ(desktop->GetRestoredBounds(),
              first->GetWindow()->GetRestoredBounds());
  };
  expect_shared_bounds();
  // Updating a background profile's native browser changes the one OS window.
  const gfx::Rect requested(64, 70, 1000, 700);
  first->GetWindow()->SetBounds(requested);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return desktop->GetWindowBoundsInScreen() == requested; }));
  expect_shared_bounds();
  window.Select(first, true);
  expect_shared_bounds();
  auto* view = Active(first)->GetRenderWidgetHostView();
  ASSERT_TRUE(view);
  EXPECT_LT(view->GetViewBounds().width(), requested.width());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspaceWindowChoicesRememberAndDropClosedModels) {
  auto* first = browser();
  auto& window = FlyWindowController::Get();
  base::test::TestFuture<BrowserWindowInterface*> future;
  BrowserWindowCreateParams params(*first->GetProfile(), true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params), future.GetCallback());
  auto* second = future.Get();
  ASSERT_TRUE(second);
  chrome::NewTab(second, NewTabTypes::kNewTabCommand);
  window.Select(second, true);
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  auto workspace_name = [&](BrowserWindowInterface* target) {
    const auto directory =
        target->GetProfile()->GetPath().BaseName().AsUTF8Unsafe();
    const auto item = std::ranges::find(catalog->items, directory,
                                        &Workspace::profile_directory);
    EXPECT_NE(item, catalog->items.end());
    return item == catalog->items.end() ? std::u16string()
                                        : base::UTF8ToUTF16(item->name);
  };
  const auto first_name = workspace_name(first);
  const auto other_name = workspace_name(other);
  auto click = [&](std::u16string_view text, bool prefix = false) {
    views::MdTextButton* target = nullptr;
    if (!base::test::RunUntil([&] {
          for (const auto& child :
               window.workspace_buttons_for_testing()->children()) {
            auto* button = static_cast<views::MdTextButton*>(child.get());
            if (prefix ? button->GetText().starts_with(text)
                       : button->GetText() == text) {
              target = button;
              return true;
            }
          }
          return false;
        }))
      return false;
    views::test::ButtonTestApi(target).NotifyDefaultMouseClick();
    return true;
  };
  ASSERT_TRUE(click(first_name));
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(second); }));
  ASSERT_TRUE(click(u"Window 1 ·", true));
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(first); }));
  ASSERT_TRUE(click(other_name));
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(other); }));
  ASSERT_TRUE(click(first_name));
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(first); }));
  first->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(second); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 2;
  }));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    ExtensionCreatedNormalWindowUsesExistingDesktopAndPicker) {
  auto* first = browser();
  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue()
          .Set("manifest_version", 3)
          .Set("name", "Fly native window creation probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("background",
               base::DictValue().Set("service_worker", "worker.js")));
  directory.WriteFile(
      FILE_PATH_LITERAL("worker.js"),
      content::JsReplace(
          "chrome.runtime.onInstalled.addListener(() => "
          "chrome.windows.create({url:$1}));",
          embedded_test_server()->GetURL("/title1.html").spec()));
  auto created = std::make_unique<BrowserCreatedWaiter>();
  extensions::ChromeTestExtensionLoader loader(first->GetProfile());
  ASSERT_TRUE(loader.LoadExtension(directory.UnpackedPath()));
  auto* second = created->Wait();
  created.reset();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(second); }));
  EXPECT_EQ(first->GetProfile(), second->GetProfile());
  EXPECT_NE(first->GetSessionID(), second->GetSessionID());
  EXPECT_EQ(window.desktop_widget()->GetNativeWindow()->GetHost(),
            second->GetWindow()->GetNativeWindow()->GetHost());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 3;
  }));
  auto* choice = static_cast<views::Button*>(
      window.workspace_buttons_for_testing()->children()[1]);
  views::test::ButtonTestApi(choice).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(first); }));
  second->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 1;
  }));
  EXPECT_TRUE(window.IsSelected(first));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    ExtensionBackgroundWindowDoesNotReplaceVisibleWorkspace) {
  auto* first = browser();
  auto& window = FlyWindowController::Get();
  window.Select(first, true);
  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue()
          .Set("manifest_version", 3)
          .Set("name", "Fly native background window probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("background",
               base::DictValue().Set("service_worker", "worker.js")));
  directory.WriteFile(
      FILE_PATH_LITERAL("worker.js"),
      content::JsReplace(
          "chrome.runtime.onInstalled.addListener(() => "
          "chrome.windows.create({url:$1,focused:false}));",
          embedded_test_server()->GetURL("/title1.html").spec()));
  auto created = std::make_unique<BrowserCreatedWaiter>();
  extensions::ChromeTestExtensionLoader loader(first->GetProfile());
  ASSERT_TRUE(loader.LoadExtension(directory.UnpackedPath()));
  auto* second = created->Wait();
  created.reset();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 3 &&
           second->GetTabStripModel()->count() == 1;
  }));
  EXPECT_TRUE(window.IsSelected(first));
  EXPECT_FALSE(second->GetWindow()->IsActive());
  ExpectVisible(Active(first), true);
  ExpectVisible(Active(second), false);
  auto* choice = static_cast<views::Button*>(
      window.workspace_buttons_for_testing()->children()[2]);
  views::test::ButtonTestApi(choice).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return window.IsSelected(second); }));
  ExpectVisible(Active(first), false);
  ExpectVisible(Active(second), true);
}

class FlySessionRestoreBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  FlySessionRestoreBrowserTest() {
    // Ordinary launch has no requested URL. A separate fixture below retains
    // the framework's explicit command-line about:blank and asserts that tab.
    set_open_about_blank_on_browser_launch(false);
  }

 protected:
  void SaveSessionFixture() {
    auto* first = browser();
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        first, embedded_test_server()->GetURL("/title1.html")));
    ASSERT_TRUE(content::ExecJs(
        Active(first),
        "document.cookie = 'fly_restart_profile=first; path=/'"));
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(first, GURL("about:blank#fly-first")));
    first->GetTabStripModel()->SetTabPinned(0, true);
    auto* second = SecondWorkspace();
    ASSERT_TRUE(second);
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        second, embedded_test_server()->GetURL("/title1.html")));
    ASSERT_TRUE(content::ExecJs(
        Active(second),
        "document.cookie = 'fly_restart_profile=second; path=/'"));
    ASSERT_TRUE(ui_test_utils::NavigateToURL(second, GURL("about:blank")));
    base::test::TestFuture<BrowserWindowInterface*> future;
    BrowserWindowCreateParams params(*first->GetProfile(), true);
    params.should_trigger_session_restore = false;
    CreateBrowserWindow(std::move(params), future.GetCallback());
    auto* third = future.Get();
    ASSERT_TRUE(third);
    chrome::NewTab(third, NewTabTypes::kNewTabCommand);
    ASSERT_TRUE(
        ui_test_utils::NavigateToURL(third, GURL("about:blank#fly-third")));
    FlyWindowController::Get().Select(second, true);
    auto state = SessionSnapshot();
    state.Set(
        "cookies",
        base::DictValue()
            .Set(first->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
                 "first")
            .Set(second->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
                 "second"));
    auto json = base::WriteJson(state);
    ASSERT_TRUE(json);
    {
    base::ScopedAllowBlockingForTesting allow_fixture_io;
    ASSERT_TRUE(base::WriteFile(RestoreFixturePath(), *json));
  }
  }
  void ExpectSessionFixture(bool explicit_blank) {
    std::string data;
    {
    base::ScopedAllowBlockingForTesting allow_fixture_io;
    ASSERT_TRUE(base::ReadFileToString(RestoreFixturePath(), &data));
  }
    const auto saved = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    ASSERT_TRUE(saved);
    ASSERT_TRUE(saved->FindString("active_id"));
    ASSERT_TRUE(saved->FindDict("cookies"));
    size_t windows = 0;
    bool restoring = false;
    std::string active_id;
    ASSERT_TRUE(base::test::RunUntil([&] {
      windows = 0;
      restoring = false;
      GlobalBrowserCollection::GetInstance()->ForEach(
          [&](BrowserWindowInterface* model) {
            if (FlyWindowController::ShouldEmbed(model) &&
                !model->GetProfile()->IsOffTheRecord()) {
              ++windows;
              restoring |= SessionRestore::IsRestoring(model->GetProfile());
            }
            return true;
          });
      WorkspaceService service(*g_browser_process->local_state());
      const auto catalog = service.Read();
      active_id = catalog ? catalog->active_id : "<unreadable>";
      return windows == 3 && !restoring && catalog &&
             catalog->active_id == *saved->FindString("active_id");
    })) << "windows=" << windows << " restoring=" << restoring
        << " active_id=" << active_id
        << " expected_active_id=" << *saved->FindString("active_id");
    const auto restored = SessionSnapshot();
    ASSERT_TRUE(saved->FindList("workspaces"));
    ASSERT_TRUE(restored.FindList("workspaces"));
    EXPECT_EQ(*saved->FindList("workspaces"), *restored.FindList("workspaces"));
    auto index = [](const base::DictValue& state) {
      std::map<std::string, base::DictValue> result;
      for (const auto& value : *state.FindList("windows")) {
        const auto& model = value.GetDict();
        for (const auto& tab : *model.FindList("tabs"))
          result.emplace(*model.FindString("directory") + ":" +
                             *tab.GetDict().FindString("id"),
                         tab.GetDict().Clone());
      }
      return result;
    };
    const auto expected_tabs = index(*saved);
    ASSERT_EQ(3u, expected_tabs.size());
    auto actual_tabs = index(restored);
    size_t requested_tabs = 0;
    for (auto it = actual_tabs.begin(); it != actual_tabs.end();) {
      if (expected_tabs.contains(it->first)) {
        ++it;
        continue;
      }
      // An explicit command-line URL is an additional native tab, even when
      // another restored tab already has the very same about:blank URL.
      EXPECT_EQ("about:blank", *it->second.FindString("url"));
      EXPECT_FALSE(*it->second.FindBool("pinned"));
      EXPECT_TRUE(
          base::Uuid::ParseLowercase(*it->second.FindString("id")).is_valid());
      ++requested_tabs;
      it = actual_tabs.erase(it);
    }
    EXPECT_EQ(explicit_blank ? 1u : 0u, requested_tabs);
    EXPECT_EQ(expected_tabs, actual_tabs);
    std::set<std::string> profiles;
    auto* desktop = FlyWindowController::Get().desktop_widget();
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          if (!FlyWindowController::ShouldEmbed(model) ||
              model->GetProfile()->IsOffTheRecord())
            return true;
          EXPECT_EQ(desktop->GetNativeWindow()->GetHost(),
                    model->GetWindow()->GetNativeWindow()->GetHost());
          const auto directory =
              model->GetProfile()->GetPath().BaseName().AsUTF8Unsafe();
          if (profiles.insert(directory).second) {
            FlyWindowController::Get().Select(model, true);
            EXPECT_TRUE(ui_test_utils::NavigateToURL(
                model, embedded_test_server()->GetURL("/title1.html")));
            const auto* cookie =
                saved->FindDict("cookies")->FindString(directory);
            EXPECT_TRUE(cookie);
            if (cookie)
              EXPECT_EQ("fly_restart_profile=" + *cookie,
                        content::EvalJs(Active(model), "document.cookie"));
          }
          return true;
        });
    EXPECT_EQ(2u, profiles.size());
  }
};

class FlyExplicitBlankRestoreBrowserTest : public FlySessionRestoreBrowserTest {
 public:
  FlyExplicitBlankRestoreBrowserTest() {
    set_open_about_blank_on_browser_launch(true);
  }
};

IN_PROC_BROWSER_TEST_F(
    FlySessionRestoreBrowserTest,
    PRE_NativeSessionRestoresProfilesWindowsIdsPinsAndCookies) {
  SaveSessionFixture();
}
IN_PROC_BROWSER_TEST_F(FlySessionRestoreBrowserTest,
                       NativeSessionRestoresProfilesWindowsIdsPinsAndCookies) {
  ExpectSessionFixture(false);
}
IN_PROC_BROWSER_TEST_F(FlyExplicitBlankRestoreBrowserTest,
                       PRE_ExplicitBlankAddsToRestoredNativeTabs) {
  SaveSessionFixture();
}
IN_PROC_BROWSER_TEST_F(FlyExplicitBlankRestoreBrowserTest,
                       ExplicitBlankAddsToRestoredNativeTabs) {
  ExpectSessionFixture(true);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeHintKeysActivateWithoutHidingOrBlurringPage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<button id="only">Hint fixture</button>' +
        '<iframe id="unrelated" src="about:blank" '
        + 'style="position:absolute;left:400px;top:0"></iframe>';
    document.querySelector('#only').onclick = () => document.title = 'hint activated';
    window.visibilityEvents = 0;
    document.addEventListener('visibilitychange', () => visibilityEvents++);
  )js"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  contents->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetRenderWidgetHostView()->HasFocus() &&
           content::EvalJs(contents, "document.hasFocus()") == true;
  }));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_F, ui::EF_ALT_DOWN);
  bool was_active = false;
  bool had_ax_root = false;
  size_t max_targets = 0;
  uint32_t observed_ax_flags = 0;
  ASSERT_TRUE(base::test::RunUntil([&] {
    was_active |= window.hints().active();
    observed_ax_flags |= contents->GetAccessibilityMode().flags();
    had_ax_root |= contents->GetAccessibilityRootNode() != nullptr;
    max_targets = std::max(max_targets, window.hints().target_count_for_testing());
    return window.hints().active() &&
           window.hints().target_count_for_testing() == 1;
  })) << "active=" << window.hints().active()
      << " was_active=" << was_active << " had_ax_root=" << had_ax_root
      << " max_targets=" << max_targets
      << " ax_flags=" << observed_ax_flags
      << " cancellation=" << window.hints().cancellation_for_testing()
      << " native_focus=" << contents->GetRenderWidgetHostView()->HasFocus();
  EXPECT_EQ("visible", content::EvalJs(contents, "document.visibilityState"));
  EXPECT_EQ(true, content::EvalJs(contents, "document.hasFocus()"));
  EXPECT_EQ(0, content::EvalJs(contents, "visibilityEvents"));
  auto* unrelated = content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(unrelated);
  content::RenderFrameDeletedObserver deleted(unrelated);
  ASSERT_TRUE(content::ExecJs(contents,
                            "document.querySelector('#unrelated').remove()"));
  deleted.WaitUntilDeleted();
  EXPECT_TRUE(window.hints().active());
  EXPECT_EQ(1u, window.hints().target_count_for_testing());
  content::TitleWatcher title(contents, u"hint activated");
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  EXPECT_EQ(u"hint activated", title.WaitAndGetTitle());
  EXPECT_FALSE(window.hints().active());
  EXPECT_EQ(0, content::EvalJs(contents, "visibilityEvents"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       EmptyDesktopSurvivesAndNewTabKeyReopensWorkspace) {
  auto& window = FlyWindowController::Get();
  auto original = browser()->GetWeakPtr();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !original; }));
  ASSERT_EQ(&window, FlyWindowController::GetIfCreated());
  EXPECT_EQ(nullptr, window.selected_browser_for_testing());
  EXPECT_TRUE(window.desktop_widget()->IsVisible());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_T, ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != nullptr; }));
  EXPECT_EQ(1,
            window.selected_browser_for_testing()->GetTabStripModel()->count());
  auto* reopened = window.selected_browser_for_testing();
  auto reopened_lifetime = reopened->GetWeakPtr();
  ASSERT_TRUE(ExecuteNativeCommand(reopened, "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !reopened_lifetime; }));
  auto* new_tab = static_cast<views::Button*>(
      window.tab_sidebar_for_testing()->children().front());
  views::test::ButtonTestApi(new_tab).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != nullptr; }));
  EXPECT_EQ(1,
            window.selected_browser_for_testing()->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeClockUsesLiveAppPreferencesWithoutTickWrites) {
  auto& window = FlyWindowController::Get();
  auto* clock = window.clock_for_testing();
  ASSERT_TRUE(clock);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return clock->GetVisible() && !clock->GetText().empty();
  }));
  auto* state = g_browser_process->local_state();
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  views::test::ButtonTestApi(clock).NotifyDefaultMouseClick();
  ASSERT_TRUE(window.preferences().ReadClock());
  EXPECT_EQ("time", window.preferences().ReadClock()->format);
  ASSERT_TRUE(NativeCommandAvailable(browser(), "cycle-clock-format"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "cycle-clock-format"));
  EXPECT_EQ("seconds", window.preferences().ReadClock()->format);
  ASSERT_TRUE(NativeCommandAvailable(browser(), "toggle-clock"));
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-clock"));
  EXPECT_FALSE(clock->GetVisible());
  EXPECT_FALSE(window.preferences().ReadClock()->visible);
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-clock"));
  EXPECT_TRUE(clock->GetVisible());
  const auto future = base::DictValue().Set("schema", 2)
      .Set("values", base::DictValue().Set("show_clock", true));
  state->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(clock->GetVisible());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-clock"));
  EXPECT_FALSE(window.RunCommand(browser(), "toggle-clock"));
  EXPECT_EQ(future, state->GetDict(kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PreferencesPointerControlsKeepKeyboardDraftAndPageFocus) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents,
      "document.body.innerHTML='<input id=entry>';"
      "document.querySelector('#entry').focus();"
      "window.visibilityEvents=0;"
      "document.addEventListener('visibilitychange',()=>visibilityEvents++)"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  const auto keyboard = window.commands().Read();
  ASSERT_TRUE(keyboard);
  auto* pointer = window.preferences_button_for_testing();
  ASSERT_TRUE(pointer && pointer->GetEnabled());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.MoveMouseTo(pointer->GetBoundsInScreen().CenterPoint());
  keys.ClickLeftButton();
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  EXPECT_TRUE(editor.appearance_for_testing("theme")->IsDrawn());
  EXPECT_FALSE(editor.search_for_testing()->IsDrawn());
  auto* state = g_browser_process->local_state();
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  auto* binding = editor.field_for_testing("spawn-tab", false);
  ASSERT_TRUE(binding);
  binding->SetText(u"Ctrl+Y");
  editor.ContentsChanged(binding, u"Ctrl+Y");
  for (const auto key : {"theme", "sync_website_theme", "show_clock",
                         "show_status_bar"}) {
    auto* checkbox = editor.appearance_for_testing(key);
    ASSERT_TRUE(checkbox && checkbox->GetEnabled());
    EXPECT_TRUE(checkbox->GetChecked());
    views::test::ButtonTestApi(checkbox).NotifyDefaultMouseClick();
    EXPECT_FALSE(checkbox->GetChecked());
  }
  EXPECT_EQ("light", window.preferences().ReadTheme()->mode);
  EXPECT_FALSE(window.preferences().ReadTheme()->sync_websites);
  EXPECT_FALSE(window.clock_for_testing()->GetVisible());
  EXPECT_FALSE(window.status_bar_for_testing()->GetVisible());
  views::test::ButtonTestApi(editor.clock_format_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ("time", window.preferences().ReadClock()->format);
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  ASSERT_TRUE(window.preferences().ToggleTheme());
  EXPECT_TRUE(editor.appearance_for_testing("theme")->GetChecked());
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  ASSERT_TRUE(NativeCommandAvailable(browser(), "open-keyboard-settings"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  EXPECT_TRUE(editor.active());
  EXPECT_TRUE(editor.search_for_testing()->IsDrawn());
  EXPECT_TRUE(editor.search_for_testing()->HasFocus());
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  EXPECT_TRUE(editor.active());
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  const auto appearance = state->GetDict(kPreferencesPref).Clone();
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  EXPECT_EQ(appearance, state->GetDict(kPreferencesPref));
  ASSERT_TRUE(window.commands().Read());
  EXPECT_EQ(keyboard->shortcuts, window.commands().Read()->shortcuts);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "document.hasFocus()").ExtractBool();
  }));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "document.querySelector('#entry').value")
               .ExtractString() == "j";
  }));
  EXPECT_EQ(0, content::EvalJs(contents, "visibilityEvents"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PreferencesRefuseFutureDataAndIgnoreCancelledCallbacks) {
  auto& window = FlyWindowController::Get();
  auto* state = g_browser_process->local_state();
  const auto future = base::DictValue().Set("schema", 2)
      .Set("values", base::DictValue().Set("show_clock", true));
  state->SetDict(kPreferencesPref, future.Clone());
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  for (const auto key : {"theme", "sync_website_theme", "show_clock",
                         "show_status_bar"})
    EXPECT_FALSE(editor.appearance_for_testing(key)->GetEnabled());
  EXPECT_FALSE(editor.clock_format_for_testing()->GetEnabled());
  EXPECT_EQ(future, state->GetDict(kPreferencesPref));
  state->ClearPref(kPreferencesPref);
  for (const auto key : {"theme", "sync_website_theme", "show_clock",
                         "show_status_bar"}) {
    EXPECT_TRUE(editor.appearance_for_testing(key)->GetEnabled());
    EXPECT_TRUE(editor.appearance_for_testing(key)->GetChecked());
  }
  EXPECT_TRUE(editor.clock_format_for_testing()->GetEnabled());
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  views::test::ButtonTestApi(editor.record_for_testing("spawn-tab", false))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(editor.recording());
  editor.ShowAppearance();
  EXPECT_FALSE(editor.recording());
  auto* checkbox = editor.appearance_for_testing("show_clock");
  editor.Cancel();
  views::test::ButtonTestApi(checkbox).NotifyDefaultMouseClick();
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  EXPECT_EQ(nullptr, editor.appearance_for_testing("show_clock"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecorderSavesWithoutExecutingItsShortcut) {
  auto* contents = Active(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML='<input id=editable>';"
                              "document.querySelector('#editable').focus()"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  for (auto key : {ui::VKEY_S, ui::VKEY_P, ui::VKEY_A, ui::VKEY_W, ui::VKEY_N})
    keys.PressKey(key, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return editor.search_for_testing()->GetText() == u"spawn";
  }));
  auto* field = editor.field_for_testing("spawn-tab", false);
  ASSERT_TRUE(field);
  field->RequestFocus();
  keys.PressKey(ui::VKEY_A, ui::EF_CONTROL_DOWN);
  keys.PressKey(ui::VKEY_BACK, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return field->GetText().empty(); }));
  auto* record = editor.record_for_testing("spawn-tab", false);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ASSERT_TRUE(editor.recording());
  const int tabs = browser()->GetTabStripModel()->count();
  keys.PressKey(ui::VKEY_Y, ui::EF_CONTROL_DOWN);
  EXPECT_FALSE(editor.recording());
  EXPECT_EQ(u"Ctrl+Y", field->GetText());
  EXPECT_EQ(tabs, browser()->GetTabStripModel()->count());
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  ASSERT_TRUE(window.commands().Read());
  EXPECT_EQ(std::vector<std::string>({"Ctrl+Y"}),
            window.commands().Read()->shortcuts.at("spawn-tab"));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.querySelector('#editable').value")
               .ExtractString() == "j";
  }));
  keys.PressKey(ui::VKEY_T, ui::EF_CONTROL_DOWN);
  EXPECT_EQ("j", content::EvalJs(contents,
                                "document.querySelector('#editable').value"));
  EXPECT_EQ(tabs, browser()->GetTabStripModel()->count());
  keys.PressKey(ui::VKEY_Y, ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return browser()->GetTabStripModel()->count() == tabs + 1; }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecorderProducesTwoStrokePageBindings) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML='<button id=focus>Page</button>';"
                              "document.body.style.height='5000px';"
                              "document.querySelector('#focus').focus()"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  EXPECT_TRUE(editor.recording());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  EXPECT_FALSE(editor.recording());
  EXPECT_EQ(u"J\nQ Q", editor.field_for_testing("scroll-down", true)->GetText());
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "Math.round(scrollY)").ExtractInt() == 80;
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecorderRetainsSemicolonBindings) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_OEM_1, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.recording(); }));
  EXPECT_EQ(u"J\n;", editor.field_for_testing("scroll-down", true)->GetText());
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  const auto config = window.commands().Read();
  ASSERT_TRUE(config);
  EXPECT_EQ((std::vector<std::string>{"J", ";"}), config->normal.at("scroll-down"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardEditorPreservesConflictsAndConcurrentEdits) {
  auto& window = FlyWindowController::Get();
  const auto before = window.commands().Read();
  ASSERT_TRUE(before);
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("go-back", false);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_T, ui::EF_CONTROL_DOWN);
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  EXPECT_TRUE(editor.active());
  EXPECT_NE(std::u16string::npos, editor.error_for_testing().find(u"Conflicting"));
  EXPECT_EQ(before, window.commands().Read());
  auto external = before->ToValue();
  external.FindDict("shortcuts")->Set("spawn-tab", base::ListValue().Append("Ctrl+Y"));
  ASSERT_TRUE(window.commands().Save(external).config);
  const auto newer = window.commands().Read();
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  EXPECT_TRUE(editor.active());
  EXPECT_NE(std::u16string::npos, editor.error_for_testing().find(u"changed"));
  EXPECT_EQ(newer, window.commands().Read());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecordingCancelsAcrossWorkspaceChanges) {
  auto& window = FlyWindowController::Get();
  const auto before = window.commands().Read();
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_FALSE(editor.recording());
  EXPECT_TRUE(editor.active());
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  EXPECT_FALSE(editor.recording());
  ASSERT_TRUE(window.RunCommand(second, "toggle-help"));
  EXPECT_EQ(u"J", editor.field_for_testing("scroll-down", true)->GetText());
  EXPECT_EQ(before, window.commands().Read());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativePaletteSearchExecutesInItsProfileAfterDismissal) {
  auto* contents = Active(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML = '<input id=editable>';"
                              "document.querySelector('#editable').focus()"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_P, ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  for (auto key : {ui::VKEY_D, ui::VKEY_U, ui::VKEY_P, ui::VKEY_L, ui::VKEY_I,
                   ui::VKEY_C, ui::VKEY_A, ui::VKEY_T, ui::VKEY_E})
    keys.PressKey(key, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.palette().search_for_testing()->GetText() == u"duplicate" &&
           window.palette().result_count_for_testing() == 1;
  }));
  EXPECT_EQ("", content::EvalJs(contents,
                                "document.querySelector('#editable').value"));
  const int before = browser()->GetTabStripModel()->count();
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return browser()->GetTabStripModel()->count() == before + 1; }));
  EXPECT_FALSE(window.palette().active());
  EXPECT_EQ(browser(), window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativePaletteCustomBindingCancelAndWorkspaceSwitch) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML = '<input id=editable>';"
                              "document.querySelector('#editable').focus()"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  auto config = DefaultKeyboardConfig().ToValue();
  config.FindDict("shortcuts")
      ->Set("toggle-palette", base::ListValue().Append("Alt+P"));
  ASSERT_TRUE(window.commands().Save(config).config);
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.palette().search_for_testing() == nullptr; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetRenderWidgetHostView()->HasFocus();
  })) << "The dismissed palette must restore native page focus";
  EXPECT_EQ("editable", content::EvalJs(contents, "document.activeElement.id"));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.querySelector('#editable').value")
               .ExtractString() == "j";
  }));
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-address"));
  auto* widget = views::Widget::GetWidgetForNativeView(
      browser()->GetWindow()->GetNativeWindow());
  ASSERT_TRUE(widget);
  auto* toolbar_focus = widget->GetFocusManager()->GetFocusedView();
  ASSERT_TRUE(toolbar_focus);
  ASSERT_FALSE(contents->GetRenderWidgetHostView()->HasFocus());
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.palette().search_for_testing() == nullptr; }));
  EXPECT_EQ(toolbar_focus, widget->GetFocusManager()->GetFocusedView());
  EXPECT_FALSE(contents->GetRenderWidgetHostView()->HasFocus());
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  ASSERT_TRUE(SecondWorkspace());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window.palette().active(); }));
  EXPECT_NE(browser(), window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeStatusTracksRealHoverAndPersistentVisibility) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  auto* status = window.status_bar_for_testing();
  ASSERT_TRUE(status->GetVisible());
  EXPECT_EQ(nullptr, g_browser_process->local_state()->GetUserPrefValue(kPreferencesPref));
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<a href="https://example.invalid/fixture" '
      + 'style="position:fixed;left:20px;top:20px;width:180px;height:40px;display:block">Link</a>';
  )js"));
  ui::test::EventGenerator pointer(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  const auto bounds = contents->GetRenderWidgetHostView()->GetViewBounds();
  pointer.MoveMouseTo(bounds.origin() + gfx::Vector2d(40, 35));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto& text = status->location_for_testing()->GetText();
    return text.starts_with(u"Link: ") && text.find(u"example.invalid/fixture") != std::u16string::npos;
  }));
  pointer.MoveMouseTo(window.workspace_buttons_for_testing()->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !status->location_for_testing()->GetText().starts_with(u"Link: ");
  }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-status-bar"));
  EXPECT_FALSE(status->GetVisible());
  EXPECT_EQ(false, window.preferences().ReadStatusBarVisible());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-status-bar"));
  EXPECT_TRUE(status->GetVisible());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  window.UpdateTargetURL(browser(), contents, GURL("https://hidden.invalid/"));
  EXPECT_EQ(std::u16string::npos, status->location_for_testing()->GetText().find(u"hidden.invalid"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  const std::u16string before(status->location_for_testing()->GetText());
  window.UpdateTargetURL(browser(), Active(browser()), GURL("https://other-profile.invalid/"));
  EXPECT_EQ(before, status->location_for_testing()->GetText());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeStatusUsesEngineSecurityAndPrivateWorkspaceState) {
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  https.ServeFilesFromSourceDirectory("chrome/test/data");
  ASSERT_TRUE(https.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), https.GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = FlyWindowController::Get();
  auto* status = window.status_bar_for_testing();
  ASSERT_EQ(security_state::SECURE, chrome_security_state::GetSecurityLevel(contents));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return status->security_for_testing()->GetText() == u"Page secure";
  }));
  {
    chrome_security_state::ScopedMaliciousContentStatusForTesting verdict(
        security_state::MALICIOUS_CONTENT_STATUS_MALWARE);
    status->DidChangeVisibleSecurityState();
    EXPECT_EQ(u"Page security warning", status->security_for_testing()->GetText());
  }
  status->DidChangeVisibleSecurityState();
  EXPECT_EQ(u"Page secure", status->security_for_testing()->GetText());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EXPECT_TRUE(status->security_for_testing()->GetText().empty());
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  EXPECT_EQ(u"Private", status->workspace_for_testing()->GetText());
  EXPECT_TRUE(status->security_for_testing()->GetText().empty());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeThemeUpdatesExistingHiddenAndPrivatePages) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* first = Active(browser());
  ASSERT_TRUE(content::ExecJs(first, R"js(
    document.body.innerHTML = '<input id=entry>';
    document.querySelector('#entry').focus();
    window.visibilityEvents = 0;
    document.addEventListener('visibilitychange', () => visibilityEvents++);
  )js"));
  auto& window = FlyWindowController::Get();
  const auto mode = [&] {
    return window.desktop_widget()->GetColorProviderKeyForTesting().color_mode;
  };
  const auto dark = [](content::WebContents* contents) {
    return content::EvalJs(contents, "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  };
  window.Select(browser(), true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first, "document.hasFocus()").ExtractBool();
  }));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, mode());
  ASSERT_TRUE(base::test::RunUntil([&] { return dark(first); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-theme"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !dark(first); }));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kLight, mode());
  EXPECT_EQ("entry", content::EvalJs(first, "document.activeElement.id"));
  EXPECT_EQ(true, content::EvalJs(first, "document.hasFocus()"));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(first, "document.querySelector('#entry').value")
               .ExtractString() == "j";
  }));
  EXPECT_EQ(0, content::EvalJs(first, "visibilityEvents"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second,
      embedded_test_server()->GetURL("/title1.html")));
  auto* second_contents = Active(second);
  ASSERT_TRUE(window.preferences().SetWebsiteThemeSync(false));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-theme"));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, mode());
  ASSERT_TRUE(base::test::RunUntil([&] { return !dark(first) && !dark(second_contents); }));
  ASSERT_TRUE(window.preferences().SetWebsiteThemeSync(true));
  ASSERT_TRUE(base::test::RunUntil([&] { return dark(first) && dark(second_contents); }));
  ASSERT_TRUE(window.RunCommand(second, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser,
      embedded_test_server()->GetURL("/title1.html")));
  auto* private_contents = Active(private_browser);
  ASSERT_TRUE(base::test::RunUntil([&] { return dark(private_contents); }));
  ASSERT_TRUE(window.preferences().SetWebsiteThemeSync(false));
  ASSERT_TRUE(base::test::RunUntil([&] { return !dark(private_contents); }));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, mode());
  const auto before = g_browser_process->local_state()->GetDict(kPreferencesPref).Clone();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(before, g_browser_process->local_state()->GetDict(kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SharedFlyBookmarksNavigateWithDestinationProfileIdentity) {
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* original = Active(browser());
  ASSERT_TRUE(content::ExecJs(original, "document.cookie='identity=first'"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "add-bookmark"));
  ASSERT_EQ(1u, window.bookmarks().Read()->size());
  const auto stored = g_browser_process->local_state()->GetDict(kBookmarksPref).Clone();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "add-bookmark"));
  EXPECT_EQ(stored, g_browser_process->local_state()->GetDict(kBookmarksPref));
  auto* first_model = BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
  ASSERT_TRUE(first_model);
  ASSERT_TRUE(base::test::RunUntil([&] { return first_model->loaded(); }));
  EXPECT_FALSE(first_model->IsBookmarked(url));
  const auto profile_only = embedded_test_server()->GetURL("/profile-only");
  ASSERT_TRUE(first_model->AddURL(first_model->other_node(), 0, u"Profile only", profile_only));
  EXPECT_FALSE(window.bookmarks().Contains(profile_only.spec()));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, url));
  auto* destination = Active(second);
  ASSERT_TRUE(content::ExecJs(destination, "document.cookie='identity=second'"));
  auto* second_model = BookmarkModelFactory::GetForBrowserContext(second->GetProfile());
  ASSERT_TRUE(second_model);
  ASSERT_TRUE(base::test::RunUntil([&] { return second_model->loaded(); }));
  EXPECT_FALSE(second_model->IsBookmarked(profile_only));
  ASSERT_EQ(1u, window.bookmarks().Read()->size());
  const int count = second->GetTabStripModel()->count();
  auto* pointer = window.bookmarks_button_for_testing();
  ASSERT_TRUE(pointer && pointer->GetEnabled());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.MoveMouseTo(pointer->GetBoundsInScreen().CenterPoint());
  keys.ClickLeftButton();
  auto& picker = window.bookmark_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  ASSERT_EQ(1u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() && second->GetTabStripModel()->count() == count + 1;
  }));
  auto* opened = Active(second);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(url, opened->GetLastCommittedURL());
  EXPECT_EQ(second->GetProfile(), opened->GetBrowserContext());
  EXPECT_EQ("identity=second", content::EvalJs(opened, "document.cookie"));
  EXPECT_EQ("identity=first", content::EvalJs(original, "document.cookie"));
  EXPECT_EQ(1u, window.bookmarks().Read()->size());
  const auto before_private = g_browser_process->local_state()->GetDict(kBookmarksPref).Clone();
  ASSERT_TRUE(window.RunCommand(second, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(window.selected_browser_for_testing(), url));
  EXPECT_EQ(before_private, g_browser_process->local_state()->GetDict(kBookmarksPref));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BookmarkDeletionKeepsNativeTextEditingAndCancelsLateClicks) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents,
      "document.body.innerHTML='<input id=entry>';"
      "document.querySelector('#entry').focus();"
      "window.visibilityEvents=0;"
      "document.addEventListener('visibilitychange',()=>visibilityEvents++)"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.bookmarks().Add("https://one.test/", "alpha one"));
  ASSERT_TRUE(window.bookmarks().Add("https://two.test/", "alpha two"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
  auto& picker = window.bookmark_picker();
  ASSERT_TRUE(picker.active());
  auto* search = picker.search_for_testing();
  search->SetText(u"alphaX");
  picker.ContentsChanged(search, std::u16string(search->GetText()));
  search->SetSelectedRange(gfx::Range(0));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_DELETE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return search->GetText() == u"lphaX"; }));
  EXPECT_EQ(2u, window.bookmarks().Read()->size());
  search->SetText(u"alpha");
  picker.ContentsChanged(search, std::u16string(search->GetText()));
  search->SetSelectedRange(gfx::Range(5));
  ASSERT_EQ(2u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_DELETE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.bookmarks().Read()->size() == 1 && picker.result_count_for_testing() == 1;
  }));
  EXPECT_EQ(u"alpha", search->GetText());
  auto* remove = picker.remove_for_testing(0);
  ASSERT_TRUE(remove && remove->GetEnabled());
  views::test::ButtonTestApi(remove).NotifyDefaultMouseClick();
  picker.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_EQ(1u, window.bookmarks().Read()->size());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "document.hasFocus()").ExtractBool();
  }));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "document.querySelector('#entry').value").ExtractString() == "j";
  }));
  EXPECT_EQ(0, content::EvalJs(contents, "visibilityEvents"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
  ASSERT_TRUE(picker.active());
  const auto future = base::DictValue().Set("schema", 2).Set("items", base::ListValue());
  g_browser_process->local_state()->SetDict(kBookmarksPref, future.Clone());
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_FALSE(window.bookmarks_button_for_testing()->GetEnabled());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-bookmarks"));
  EXPECT_EQ(future, g_browser_process->local_state()->GetDict(kBookmarksPref));
}

class FlyTabMenuBrowserTest : public FlyWorkspaceBrowserTest {
 protected:
  void Menu(content::WebContents* contents,
            base::OnceCallback<void(ui::SimpleMenuModel*)> inspect) {
    auto* sidebar = FlyWindowController::Get().tab_sidebar_for_testing();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return sidebar->row_for_testing(contents) != nullptr;
    }));
    auto* control = sidebar->row_for_testing(contents)->children().front().get();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return control->IsDrawn() && !control->GetVisibleBounds().IsEmpty();
    })) << "row=" << sidebar->row_for_testing(contents)->bounds().ToString()
        << " control=" << control->bounds().ToString();

    bool inspected = false;
    // MenuRunner can post its opening work. Inspect only after the real pointer
    // event has opened a menu, rather than assuming the next queued task does.
    base::RepeatingTimer inspect_menu;
    inspect_menu.Start(FROM_HERE, base::Milliseconds(1),
        base::BindLambdaForTesting([&] {
          auto* menu = sidebar->context_menu_for_testing();
          if (!menu)
            return;
          inspect_menu.Stop();
          std::move(inspect).Run(menu);
          sidebar->CloseContextMenu();
          inspected = true;
        }));
    ui::test::EventGenerator mouse(
        FlyWindowController::Get().desktop_widget()->GetNativeWindow()->GetRootWindow());
    auto point = control->GetVisibleBounds().CenterPoint();
    views::View::ConvertPointToScreen(control, &point);
    auto* root_window = FlyWindowController::Get()
                            .desktop_widget()->GetNativeWindow()->GetRootWindow();
    auto native_point = point;
    wm::ConvertPointFromScreen(root_window, &native_point);
    auto* native_target = root_window->GetEventHandlerForPoint(native_point);
    const std::string native_target_name =
        native_target ? native_target->GetName() : "<none>";
    auto* root_view = sidebar->GetWidget()->GetRootView();
    auto view_point = point;
    views::View::ConvertPointFromScreen(root_view, &view_point);
    auto* view_target = root_view->GetEventHandlerForPoint(view_point);
    const std::string view_target_name(
        view_target ? view_target->GetClassName() : "<none>");
    mouse.MoveMouseTo(point);
    mouse.ClickRightButton();
    ASSERT_TRUE(base::test::RunUntil([&] { return inspected; }))
        << "click=" << point.ToString()
        << " control=" << control->GetBoundsInScreen().ToString()
        << " visible=" << control->GetVisibleBounds().ToString()
        << " sidebar=" << sidebar->GetBoundsInScreen().ToString()
        << " native_target=" << native_target_name
        << " view_target=" << view_target_name;
  }
  static size_t Item(ui::SimpleMenuModel* menu, std::u16string_view label) {
    for (size_t i = 0; i < menu->GetItemCount(); ++i)
      if (menu->GetTypeAt(i) != ui::MenuModel::TYPE_SEPARATOR &&
          menu->GetLabelAt(i) == label)
        return i;
    ADD_FAILURE() << "Missing native product menu item";
    return menu->GetItemCount();
  }
  void Choose(content::WebContents* contents, std::u16string label) {
    Menu(contents, base::BindOnce(
        [](std::u16string label, ui::SimpleMenuModel* menu) {
          const auto item = Item(menu, label);
          if (item < menu->GetItemCount()) {
            EXPECT_TRUE(menu->IsEnabledAt(item));
            menu->ActivatedAt(item);
          }
        }, std::move(label)));
  }
};

IN_PROC_BROWSER_TEST_F(FlyTabMenuBrowserTest,
                       BackgroundPinMuteAndPinnedCloseProtection) {
  auto* target = Active(browser());
  auto* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* selected = Active(browser());
  ASSERT_NE(target, selected);
  Menu(target, base::BindOnce([](ui::SimpleMenuModel* menu) {
    EXPECT_EQ(12u, menu->GetItemCount());
    EXPECT_FALSE(menu->IsEnabledAt(Item(menu, u"Reopen closed tab")));
  }));
  EXPECT_EQ(selected, Active(browser()));
  Choose(target, u"Pin");
  ASSERT_TRUE(base::test::RunUntil([&] { return model->IsTabPinned(0); }));
  EXPECT_EQ(selected, Active(browser()));
  Choose(target, u"Mute tab");
  ASSERT_TRUE(base::test::RunUntil([&] { return target->IsAudioMuted(); }));
  EXPECT_EQ(TabMutedReason::kAudioIndicator, GetTabAudioMutedReason(target));
  EXPECT_EQ(selected, Active(browser()));
  Menu(target, base::BindOnce([](ui::SimpleMenuModel* menu) {
    EXPECT_FALSE(menu->IsEnabledAt(Item(menu, u"Close")));
    EXPECT_LT(Item(menu, u"Unpin"), menu->GetItemCount());
    EXPECT_LT(Item(menu, u"Unmute tab"), menu->GetItemCount());
  }));
  model->ActivateTabAt(0);
  EXPECT_FALSE(NativeCommandAvailable(browser(), "remove-tab"));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "remove-tab"));
  EXPECT_EQ(2, model->count());
  auto* row = FlyWindowController::Get().tab_sidebar_for_testing()->row_for_testing(target);
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->children()[2]->GetEnabled(); }));
}

IN_PROC_BROWSER_TEST_F(FlyTabMenuBrowserTest,
                       MenuTracksReordersAndRejectsClosedOrOtherProfileTargets) {
  // Profile creation posts non-nestable work. Prepare it before MenuRunner's
  // nested loop; the menu callback still switches the workspace synchronously.
  auto* other_workspace = SecondWorkspace();
  ASSERT_TRUE(other_workspace);
  FlyWindowController::Get().Select(browser(), true);
  auto* model = browser()->GetTabStripModel();
  auto* target = Active(browser());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* selected = Active(browser());
  Menu(target, base::BindLambdaForTesting([&](ui::SimpleMenuModel* menu) {
    model->MoveWebContentsAt(0, 1, false);
    menu->ActivatedAt(Item(menu, u"Mute tab"));
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return target->IsAudioMuted(); }));
  EXPECT_FALSE(selected->IsAudioMuted());
  EXPECT_EQ(selected, Active(browser()));
  Menu(target, base::BindLambdaForTesting([&](ui::SimpleMenuModel* menu) {
    const int index = model->GetIndexOfWebContents(target);
    model->CloseWebContentsAt(index, TabCloseTypes::CLOSE_USER_GESTURE |
                                      TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
    EXPECT_FALSE(menu->IsEnabledAt(Item(menu, u"Duplicate")));
    menu->ActivatedAt(Item(menu, u"Duplicate"));
  }));
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == 2; }));
  target = model->GetWebContentsAt(0);
  auto lifetime = target->GetWeakPtr();
  Menu(target, base::BindLambdaForTesting([&](ui::SimpleMenuModel* menu) {
    menu->ActivatedAt(Item(menu, u"Close"));
    FlyWindowController::Get().Select(other_workspace, true);
  }));
  // The queued command must not cross a workspace generation boundary.
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(lifetime);
  EXPECT_EQ(2, model->count());
}

IN_PROC_BROWSER_TEST_F(FlyTabMenuBrowserTest,
                       NativeCloseReopenRetainsStableIdentityAndAdjacentNewTab) {
  auto* target = Active(browser());
  auto* model = browser()->GetTabStripModel();
  const auto id = TabIdentity::Get(target);
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  Choose(target, u"New tab to the right");
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == 3; }));
  ASSERT_TRUE(content::WaitForLoadStop(model->GetWebContentsAt(1)));
  EXPECT_EQ(GURL("about:blank"),
            model->GetWebContentsAt(1)->GetLastCommittedURL());
  EXPECT_EQ(target, model->GetWebContentsAt(0));
  Choose(target, u"Close");
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == 2; }));
  Choose(Active(browser()), u"Reopen closed tab");
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == 3; }));
  EXPECT_EQ(id, TabIdentity::Get(Active(browser())));
}

class FlyPageScrollBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    FlyWorkspaceBrowserTest::SetUpCommandLine(command_line);
    command_line->AppendSwitch("site-per-process");
  }
  void SetUpOnMainThread() override {
    FlyWorkspaceBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
  }
};

IN_PROC_BROWSER_TEST_F(FlyPageScrollBrowserTest,
                       FocusedContainerPixelsHalfPageAndEditableKeys) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "zoom-in"));
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = `
      <input id="editable">
      <div id="box" tabindex="0" style="height:240px;width:240px;overflow:auto">
        <div style="height:3000px;width:2000px">Scroll fixture</div>
      </div>`;
    document.body.style.height = '10000px';
    window.scrollTo(0, 0);
    const box = document.querySelector('#box');
    box.focus();
    // A hostile page override cannot intercept the browser's native command.
    box.scrollBy = () => { throw new Error('Page scroll override called'); };
    box.scrollTo = box.scrollBy;
  )js"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  auto wait_offset = [&](const char* axis, int offset) {
    return base::test::RunUntil([&] {
      return content::EvalJs(
                 contents,
                 content::JsReplace(
                     "Math.round(document.querySelector('#box')[$1])", axis))
                 .ExtractInt() == offset;
    });
  };
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80));
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
  const int half_page =
      content::EvalJs(
          contents,
          "Math.floor(document.querySelector('#box').clientHeight / 2)")
          .ExtractInt();
  keys.PressKey(ui::VKEY_D, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80 + half_page));
  keys.PressKey(ui::VKEY_U, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80));
  keys.PressKey(ui::VKEY_L, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollLeft", 80));
  keys.PressKey(ui::VKEY_G, ui::EF_NONE);
  keys.PressKey(ui::VKEY_G, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 0));
  ASSERT_TRUE(wait_offset("scrollLeft", 0));
  keys.PressKey(ui::VKEY_G, ui::EF_SHIFT_DOWN);
  const int bottom =
      content::EvalJs(contents,
                      "document.querySelector('#box').scrollHeight - "
                      "document.querySelector('#box').clientHeight")
          .ExtractInt();
  ASSERT_TRUE(wait_offset("scrollTop", bottom));
  ASSERT_TRUE(
      content::ExecJs(contents, "document.querySelector('#editable').focus()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->IsFocusedElementEditable(); }));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.querySelector('#editable').value")
               .ExtractString() == "j";
  }));
  EXPECT_EQ(false, content::EvalJs(
                       contents, "document.execCommand('FlyScrollStepDown')"));
}

IN_PROC_BROWSER_TEST_F(FlyPageScrollBrowserTest,
                       NativeScrollUsesFocusedCrossOriginFrame) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  const auto child_url =
      embedded_test_server()->GetURL("frame.test", "/title1.html");
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<iframe id="frame" style="height:200px"></iframe>';
    document.body.style.height = '10000px';
  )js"));
  ASSERT_TRUE(content::NavigateIframeToURL(contents, "frame", child_url));
  auto* child = content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  ASSERT_NE(contents->GetPrimaryMainFrame()->GetProcess(), child->GetProcess());
  ASSERT_TRUE(content::ExecJs(child, R"js(
    document.body.innerHTML = '<button id="focus">Frame focus</button>';
    document.body.style.height = '5000px';
    document.querySelector('#focus').focus();
  )js"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetFocusedFrame() == child; }));
  ASSERT_TRUE(SecondWorkspace());
  window.Select(browser(), true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetFocusedFrame() == child; }));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(child, "Math.round(scrollY)").ExtractInt() == 80;
  }));
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
}

}  // namespace
}  // namespace fly
