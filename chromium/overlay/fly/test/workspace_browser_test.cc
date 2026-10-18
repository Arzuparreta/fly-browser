// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/sessions/tab_identity.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/tab_sidebar.h"
#include "ui/views/controls/textfield/textfield.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/run_until.h"
#include "base/test/test_future.h"
#include "base/uuid.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
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
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "components/prefs/pref_service.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
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
  BrowserCreatedWaiter created;
  extensions::ChromeTestExtensionLoader loader(first->GetProfile());
  ASSERT_TRUE(loader.LoadExtension(directory.UnpackedPath()));
  auto* second = created.Wait();
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
  BrowserCreatedWaiter created;
  extensions::ChromeTestExtensionLoader loader(first->GetProfile());
  ASSERT_TRUE(loader.LoadExtension(directory.UnpackedPath()));
  auto* second = created.Wait();
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
    ASSERT_TRUE(base::WriteFile(RestoreFixturePath(), *json));
  }
  void ExpectSessionFixture(bool explicit_blank) {
    std::string data;
    ASSERT_TRUE(base::ReadFileToString(RestoreFixturePath(), &data));
    const auto saved = base::JSONReader::ReadDict(data, base::JSON_PARSE_RFC);
    ASSERT_TRUE(saved);
    ASSERT_TRUE(saved->FindString("active_id"));
    ASSERT_TRUE(saved->FindDict("cookies"));
    ASSERT_TRUE(base::test::RunUntil([&] {
      size_t windows = 0;
      bool restoring = false;
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
      return windows == 3 && !restoring && catalog &&
             catalog->active_id == *saved->FindString("active_id");
    }));
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
    document.body.innerHTML = '<button id="only">Hint fixture</button>';
    document.querySelector('#only').onclick = () => document.title = 'hint activated';
    window.visibilityEvents = 0;
    document.addEventListener('visibilitychange', () => visibilityEvents++);
  )js"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
  keys.PressKey(ui::VKEY_F, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.hints().active() &&
           window.hints().target_count_for_testing() == 1;
  }));
  EXPECT_EQ("visible", content::EvalJs(contents, "document.visibilityState"));
  EXPECT_EQ(true, content::EvalJs(contents, "document.hasFocus()"));
  EXPECT_EQ(0, content::EvalJs(contents, "visibilityEvents"));
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
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
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
                       NativePaletteSearchExecutesInItsProfileAfterDismissal) {
  auto* contents = Active(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML = '<input id=editable>';"
                              "document.querySelector('#editable').focus()"));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
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
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.palette().search_for_testing() == nullptr; }));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.querySelector('#editable').value")
               .ExtractString() == "j";
  }));
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  ASSERT_TRUE(SecondWorkspace());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window.palette().active(); }));
  EXPECT_NE(browser(), window.selected_browser_for_testing());
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
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
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
  ui::test::EventGenerator keys(window.desktop_widget()->GetNativeWindow());
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(child, "Math.round(scrollY)").ExtractInt() == 80;
  }));
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
}

}  // namespace
}  // namespace fly
