// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/bookmarks/frequent_sites_service.h"
#include "fly/preferences/search_service.h"
#include "fly/sessions/tab_identity.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/fly_extensions.h"
#include "fly/ui/user_content.h"
#include "fly/ui/welcome_view.h"
#include "base/files/scoped_temp_dir.h"
#include "content/public/browser/host_zoom_map.h"
#include "chrome/common/chrome_paths.h"
#include "base/files/file_util.h"
#include "extensions/browser/extension_registry.h"
#include "base/path_service.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/history_service.h"
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/status_bar.h"
#include "fly/ui/tab_sidebar.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/textfield/textfield.h"
#include "url/origin.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/bind.h"
#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "base/test/test_future.h"
#include "base/threading/thread_restrictions.h"
#include "base/timer/timer.h"
#include "base/uuid.h"
#include "base/version_info/version_info.h"
#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/chrome_test_extension_loader.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/keep_alive/profile_keep_alive_types.h"
#include "chrome/browser/profiles/keep_alive/scoped_profile_keep_alive.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/sessions/session_restore.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/create_browser_window.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/browser_created_waiter.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/javascript_dialogs/app_modal_dialog_controller.h"
#include "components/javascript_dialogs/app_modal_dialog_view.h"
#include "components/performance_manager/public/features.h"
#include "components/performance_manager/public/graph/page_node.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/prefs/pref_service.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "ui/aura/client/focus_client.h"
#include "ui/aura/window.h"
#include "ui/aura/window_tree_host.h"
#include "ui/base/base_window.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/events/test/event_generator.h"
#include "ui/menus/simple_menu_model.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/button/image_button.h"
#include "ui/views/controls/button/md_text_button.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/test/button_test_api.h"
#include "ui/views/widget/widget.h"
#include "ui/views/test/views_test_utils.h"
#include "ui/views/window/dialog_delegate.h"
#include "ui/wm/core/coordinate_conversion.h"
#include "ui/wm/public/activation_client.h"

namespace fly {
namespace {
class FlyWorkspaceBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitch("fly-native");
    // Never import the Electron Fly profile of the machine running the tests
    // (~/.config/fly-browser); import cases pass their own directory.
    command_line->AppendSwitchPath(
        "fly-import-from",
        base::FilePath(FILE_PATH_LITERAL("/nonexistent-fly-import-source")));
  }
  void SetUpOnMainThread() override {
    ASSERT_TRUE(embedded_test_server()->Start());
    // Baseline fixtures exercise the pinned layout explicitly. Sidebar tests
    // opt into the product's default auto-collapse mode below.
    g_browser_process->local_state()->SetDefaultPrefValue(
        kPreferencesPref,
        base::Value(base::DictValue()
                        .Set("schema", 1)
                        .Set("values", base::DictValue().Set(
                                           "sidebar_auto_collapse", false))));
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
  void ChooseMoveDestination(BrowserWindowInterface* source,
                             const std::string& name,
                             bool pointer = false) {
    auto& window = FlyWindowController::Get();
    window.Select(source, true);
    if (pointer) {
      auto* contents = Active(source);
      ASSERT_TRUE(base::test::RunUntil([&] {
        return window.tab_sidebar_for_testing()->row_for_testing(contents);
      }));
      auto* row = window.tab_sidebar_for_testing()->row_for_testing(contents);
      auto* control = row->children().front().get();
      ASSERT_TRUE(base::test::RunUntil([&] {
        return control->IsDrawn() && !control->GetVisibleBounds().IsEmpty();
      }));
      bool menu_selected = false;
      base::RepeatingTimer choose;
      choose.Start(
          FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
            auto* menu =
                window.tab_sidebar_for_testing()->context_menu_for_testing();
            if (!menu)
              return;
            choose.Stop();
            for (size_t i = 0; i < menu->GetItemCount(); ++i) {
              if (menu->GetLabelAt(i) == u"Move to workspace…") {
                EXPECT_TRUE(menu->IsEnabledAt(i));
                menu->ActivatedAt(i);
                menu_selected = true;
                return;
              }
            }
            ADD_FAILURE() << "Native tab menu is missing Move to workspace";
            window.tab_sidebar_for_testing()->CloseContextMenu();
          }));
      ui::test::EventGenerator mouse(
          window.desktop_widget()->GetNativeWindow()->GetRootWindow());
      auto point = control->GetVisibleBounds().CenterPoint();
      views::View::ConvertPointToScreen(control, &point);
      mouse.MoveMouseTo(point);
      mouse.ClickRightButton();
      ASSERT_TRUE(base::test::RunUntil([&] { return menu_selected; }));
    } else {
      ASSERT_TRUE(InvokeNativeCommand(source, "move-tab-to-workspace"));
    }
    auto& picker = window.tab_move_picker();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return picker.active() && picker.search_for_testing()->HasFocus();
    }));
    auto* search = picker.search_for_testing();
    search->SetText(base::UTF8ToUTF16(name));
    picker.ContentsChanged(search, std::u16string(search->GetText()));
    ASSERT_EQ(1u, picker.result_count_for_testing());
    ui::test::EventGenerator keys(
        window.desktop_widget()->GetNativeWindow()->GetRootWindow());
    keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
    keys.ReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  }
  void OpenWorkspaceRemoval(const Workspace& item) {
    auto& window = FlyWindowController::Get();
    views::View* row = nullptr;
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (const auto& child :
           window.workspace_buttons_for_testing()->children()) {
        auto* button = views::AsViewClass<views::MdTextButton>(child.get());
        if (button && button->GetText() == base::UTF8ToUTF16(item.name) &&
            button->IsDrawn() && !button->GetVisibleBounds().IsEmpty()) {
          row = button;
          return true;
        }
      }
      return false;
    }));
    bool menu_selected = false;
    base::RepeatingTimer choose;
    choose.Start(
        FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
          auto* model = window.workspace_context_menu_for_testing(item.id);
          if (!model)
            return;
          choose.Stop();
          EXPECT_TRUE(model->IsEnabledAt(4));
          model->ActivatedAt(4);
          menu_selected = true;
        }));
    ui::test::EventGenerator mouse(
        window.desktop_widget()->GetNativeWindow()->GetRootWindow());
    // A catalog notification can replace the row before its first layout.
    // Target its visible hit area rather than an unlaid-out preferred rect.
    auto point = row->GetVisibleBounds().CenterPoint();
    views::View::ConvertPointToScreen(row, &point);
    mouse.MoveMouseTo(point);
    mouse.ClickRightButton();
    ASSERT_TRUE(base::test::RunUntil([&] { return menu_selected; }));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window.workspace_remove_dialog_for_testing(); }));
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

class FlySidebarBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  void SetUpOnMainThread() override {
    FlyWorkspaceBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(
        FlyWindowController::Get().preferences().SetSidebarAutoCollapse(true));
    // The footer clock ticks independently; these cases observe the sidebar's.
    ASSERT_TRUE(
        FlyWindowController::Get().preferences().SetStatusBarVisible(false));
  }

 protected:
  void MovePointer(gfx::Point point) {
    ui::test::EventGenerator mouse(FlyWindowController::Get()
                                       .desktop_widget()
                                       ->GetNativeWindow()
                                       ->GetRootWindow());
    mouse.MoveMouseTo(point);
  }
  void OpenByPointer() {
    auto& window = FlyWindowController::Get();
    const auto bounds =
        window.desktop_widget()->GetContentsView()->GetBoundsInScreen();
    MovePointer(gfx::Point(bounds.x() + 18, bounds.y() + 200));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window.sidebar_expanded_for_testing(); }));
  }
  void MoveToPage() {
    auto& window = FlyWindowController::Get();
    auto* contents = Active(window.selected_browser_for_testing());
    contents->Focus();
    MovePointer(
        contents->GetRenderWidgetHostView()->GetViewBounds().CenterPoint());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !window.sidebar_expanded_for_testing(); }));
  }
};

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       HoverKeepsViewportVisibilityAndBlocksPageClickThrough) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(content::ExecJs(page, R"js(
    document.body.innerHTML='<button id="covered" style="position:fixed;left:80px;top:400px;width:80px;height:32px">Target</button>';
    window.clicks=0; window.resizes=0; window.visibilityEvents=[];
    document.querySelector('#covered').onclick=()=>window.clicks++;
    onresize=()=>window.resizes++;
    document.addEventListener('visibilitychange',()=>visibilityEvents.push(document.visibilityState));
  )js"));
  auto& window = FlyWindowController::Get();
  MoveToPage();
  EXPECT_FALSE(window.clock_timer_running_for_testing());
  const int width = content::EvalJs(page, "innerWidth").ExtractInt();
  const auto page_bounds = page->GetRenderWidgetHostView()->GetViewBounds();
  EXPECT_EQ(true, content::EvalJs(page, "document.hasFocus()"));
  OpenByPointer();
  EXPECT_EQ(true, content::EvalJs(page, "document.hasFocus()"));
  EXPECT_TRUE(window.clock_timer_running_for_testing());
  EXPECT_EQ(width, content::EvalJs(page, "innerWidth"));
  EXPECT_EQ(page_bounds, page->GetRenderWidgetHostView()->GetViewBounds());
  ExpectVisible(page, true);
  const auto covered = page_bounds.origin() + gfx::Vector2d(100, 416);
  auto native_point = covered;
  auto* root = window.desktop_widget()->GetNativeWindow()->GetRootWindow();
  wm::ConvertPointFromScreen(root, &native_point);
  auto* hit = root->GetEventHandlerForPoint(native_point);
  ASSERT_TRUE(
      window.sidebar_widget_for_testing()->GetNativeWindow()->Contains(hit));
  ui::test::EventGenerator mouse(root);
  mouse.MoveMouseTo(covered);
  mouse.ClickLeftButton();
  EXPECT_EQ(0, content::EvalJs(page, "clicks"));
  MoveToPage();
  EXPECT_FALSE(window.clock_timer_running_for_testing());
  mouse.MoveMouseTo(covered);
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return content::EvalJs(page, "clicks").ExtractInt() == 1; }));
  EXPECT_EQ(width, content::EvalJs(page, "innerWidth"));
  EXPECT_EQ(0, content::EvalJs(page, "resizes"));
  EXPECT_EQ(0, content::EvalJs(page, "visibilityEvents.length"));
}

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       KeyboardFocusHoldsPanelAndEscapeRestoresEditablePage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(content::ExecJs(page, R"js(
    document.body.innerHTML='<input id="edit" style="position:fixed;left:400px;top:40px"><div style="height:6000px"></div>';
    document.querySelector('#edit').focus();
  )js"));
  auto& window = FlyWindowController::Get();
  MoveToPage();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.querySelector('#edit').value")
               .ExtractString() == "j";
  }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "focus-sidebar"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* row = window.tab_sidebar_for_testing()->row_for_testing(page);
    return window.sidebar_expanded_for_testing() && row &&
           row->children().front()->HasFocus();
  }));
  MovePointer(page->GetRenderWidgetHostView()->GetViewBounds().CenterPoint());
  EXPECT_TRUE(window.sidebar_expanded_for_testing());
  ExpectVisible(page, true);
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.sidebar_expanded_for_testing() &&
           content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.querySelector('#edit').value")
               .ExtractString() == "jj";
  }));
  EXPECT_EQ(0, content::EvalJs(page, "scrollY"));
}

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       RepeatedFocusAfterRowActivationMovesKeyboardFocus) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(content::ExecJs(page, R"js(
    document.body.innerHTML='<input id="edit" style="position:fixed;left:400px;top:40px">';
    document.querySelector('#edit').focus();
  )js"));
  auto& window = FlyWindowController::Get();
  MoveToPage();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  auto row_focused = [&] {
    auto* row = window.tab_sidebar_for_testing()->row_for_testing(page);
    return window.sidebar_expanded_for_testing() && row &&
           row->children().front()->HasFocus() &&
           !content::EvalJs(page, "document.hasFocus()").ExtractBool();
  };
  ASSERT_TRUE(InvokeNativeCommand(browser(), "focus-sidebar"));
  ASSERT_TRUE(base::test::RunUntil(row_focused));
  // Enter activates the row and returns keyboard input to the page.
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "focus-sidebar"));
  ASSERT_TRUE(base::test::RunUntil(row_focused));
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_J, ui::EF_NONE);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ("", content::EvalJs(page, "document.querySelector('#edit').value"));
}

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       NativePinAndAppearanceChangeOnlyReservedSidebarWidth) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  auto& window = FlyWindowController::Get();
  MoveToPage();
  const auto width = page->GetRenderWidgetHostView()->GetViewBounds().width();
  OpenByPointer();
  auto* pin = window.sidebar_pin_for_testing();
  views::test::RunScheduledLayout(pin->GetWidget());
  ui::test::EventGenerator mouse(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(pin->GetBoundsInScreen().CenterPoint());
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.preferences().ReadSidebarAutoCollapse() == false; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetRenderWidgetHostView()->GetViewBounds().width() ==
           width - 224;
  }));
  MovePointer(page->GetRenderWidgetHostView()->GetViewBounds().CenterPoint());
  EXPECT_TRUE(window.sidebar_expanded_for_testing());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  editor.ShowAppearance();
  auto* checkbox = editor.appearance_for_testing("sidebar_auto_collapse");
  ASSERT_TRUE(checkbox && !checkbox->GetChecked());
  views::test::ButtonTestApi(checkbox).NotifyDefaultMouseClick();
  EXPECT_EQ(true, window.preferences().ReadSidebarAutoCollapse());
  editor.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !editor.active() &&
           page->GetRenderWidgetHostView()->GetViewBounds().width() == width;
  }));
  MoveToPage();
  EXPECT_TRUE(NativeCommandAvailable(browser(), "toggle-sidebar-pin"));
  EXPECT_TRUE(NativeCommandAvailable(browser(), "toggle-zen-mode"));
  const auto future =
      base::DictValue().Set("schema", 2).Set("values", base::DictValue());
  g_browser_process->local_state()->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-sidebar-pin"));
  EXPECT_FALSE(window.RunCommand(browser(), "toggle-sidebar-pin"));
  EXPECT_FALSE(window.sidebar_pin_for_testing()->GetEnabled());
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       CompactRowsAndMenusKeepNativeIdentityAcrossProfiles) {
  auto* first = browser();
  const auto first_id = TabIdentity::Get(Active(first));
  ASSERT_TRUE(ExecuteNativeCommand(first, "spawn-tab"));
  auto* target = Active(first);
  const auto id = TabIdentity::Get(target);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  auto* rail = window.compact_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return rail->row_count_for_testing() == 1 &&
           rail->row_for_testing(Active(second));
  }));
  window.Select(first, true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return rail->row_count_for_testing() == 2 && rail->row_for_testing(target);
  }));
  EXPECT_EQ(first_id,
            TabIdentity::Get(first->GetTabStripModel()->GetWebContentsAt(0)));
  EXPECT_EQ(id, TabIdentity::Get(target));
  MoveToPage();
  auto* button = rail->row_for_testing(target)->children().front().get();
  auto* select = views::AsViewClass<views::MdTextButton>(button);
  ASSERT_TRUE(select);
  ASSERT_TRUE(select->GetImageModel(views::Button::STATE_NORMAL));
  const auto initial_icon = select->GetImageModel(views::Button::STATE_NORMAL)
                                ->Rasterize(select->GetColorProvider());
  EXPECT_EQ(gfx::Size(24, 24), initial_icon.size());
  SetTabAudioMuted(target, true, TabMutedReason::kAudioIndicator, {});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return select->GetTooltipText().find(u", muted") != std::u16string::npos;
  }));
  const auto muted_icon = select->GetImageModel(views::Button::STATE_NORMAL)
                              ->Rasterize(select->GetColorProvider());
  EXPECT_FALSE(initial_icon.BackedBySameObjectAs(muted_icon));
  SetTabAudioMuted(target, false, TabMutedReason::kAudioIndicator, {});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return select->GetTooltipText().find(u", muted") == std::u16string::npos;
  }));
  views::test::RunScheduledLayout(button->GetWidget());
  const auto point = button->GetBoundsInScreen().CenterPoint();
  bool inspected = false;
  base::RepeatingTimer menu;
  menu.Start(FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
               auto* model = rail->context_menu_for_testing();
               if (!model)
                 return;
               menu.Stop();
               for (size_t i = 0; i < model->GetItemCount(); ++i) {
                 if (model->GetLabelAt(i) == u"Pin") {
                   EXPECT_TRUE(model->IsEnabledAt(i));
                   model->ActivatedAt(i);
                   inspected = true;
                   return;
                 }
               }
               ADD_FAILURE() << "Missing native Pin command in compact rail";
               rail->CloseContextMenu();
             }));
  ui::test::EventGenerator mouse(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(point);
  mouse.ClickRightButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return inspected && first->GetTabStripModel()->IsTabPinned(0); }));
  EXPECT_EQ(id, TabIdentity::Get(target));
  EXPECT_EQ(first, window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WindowControlsFollowCommandSettingsAndRecoveryPolicy) {
  auto& window = FlyWindowController::Get();
  auto* controls = window.window_controls_for_testing();
  EXPECT_FALSE(controls->GetVisible());
  ASSERT_TRUE(NativeCommandAvailable(browser(), "toggle-window-controls"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-window-controls"));
  EXPECT_TRUE(controls->GetVisible());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  editor.ShowAppearance();
  auto* checkbox = editor.appearance_for_testing("show_window_controls");
  ASSERT_TRUE(checkbox && checkbox->GetChecked());
  views::test::ButtonTestApi(checkbox).NotifyDefaultMouseClick();
  EXPECT_FALSE(controls->GetVisible());
  EXPECT_EQ(false, window.preferences().ReadWindowControlsVisible());
  editor.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.active(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-window-controls"));
  auto* close = window.window_control_for_testing("close");
  ASSERT_TRUE(close);
  close->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] { return close->HasFocus(); }));
  auto browser_alive = browser()->GetWeakPtr();
  views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
  ASSERT_TRUE(window.preferences().SetWindowControlsVisible(false));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(browser_alive);
  EXPECT_FALSE(controls->GetVisible());
  EXPECT_FALSE(close->HasFocus());
  const auto future =
      base::DictValue().Set("schema", 2).Set("values", base::DictValue());
  g_browser_process->local_state()->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-window-controls"));
  EXPECT_FALSE(window.RunCommand(browser(), "toggle-window-controls"));
  EXPECT_FALSE(controls->GetVisible());
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WindowControlsDriveTheSharedNativeDesktopState) {
  auto& window = FlyWindowController::Get();
  auto* desktop = window.desktop_widget();
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(window.preferences().SetWindowControlsVisible(true));
  auto click = [&](std::string_view action) {
    auto* button = window.window_control_for_testing(action);
    ASSERT_TRUE(button);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return button->IsDrawn() && !button->GetVisibleBounds().IsEmpty();
    }));
    views::test::RunScheduledLayout(button->GetWidget());
    ui::test::EventGenerator mouse(desktop->GetNativeWindow()->GetRootWindow());
    mouse.MoveMouseTo(button->GetBoundsInScreen().CenterPoint());
    mouse.ClickLeftButton();
  };
  click("maximize");
  ASSERT_TRUE(base::test::RunUntil([&] { return desktop->IsMaximized(); }));
  EXPECT_TRUE(first->GetWindow()->IsMaximized());
  EXPECT_TRUE(second->GetWindow()->IsMaximized());
  click("maximize");
  ASSERT_TRUE(base::test::RunUntil([&] { return !desktop->IsMaximized(); }));
  EXPECT_FALSE(first->GetWindow()->IsMaximized());
  EXPECT_FALSE(second->GetWindow()->IsMaximized());
  click("minimize");
  ASSERT_TRUE(base::test::RunUntil([&] { return desktop->IsMinimized(); }));
  EXPECT_TRUE(first->GetWindow()->IsMinimized());
  EXPECT_TRUE(second->GetWindow()->IsMinimized());
  desktop->Restore();
  ASSERT_TRUE(base::test::RunUntil([&] { return !desktop->IsMinimized(); }));
  window.Select(second, true);
  ExpectVisible(Active(second), true);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WindowCloseButtonHonoursBeforeUnloadCancellation) {
  auto first = browser()->GetWeakPtr();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto second_alive = second->GetWeakPtr();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(second);
  ASSERT_TRUE(content::ExecJs(contents,
                              "window.unsavedControls='keep'; "
                              "window.onbeforeunload=()=> 'unsaved'"));
  content::PrepContentsForBeforeUnloadTest(contents);
  class CancellationObserver : public content::WebContentsObserver {
   public:
    explicit CancellationObserver(content::WebContents* contents)
        : content::WebContentsObserver(contents) {}
    void BeforeUnloadDialogCancelled() override { cancelled = true; }
    bool cancelled = false;
  } cancellation(contents);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.preferences().SetWindowControlsVisible(true));
  auto* close = window.window_control_for_testing("close");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return close->IsDrawn() && !close->GetVisibleBounds().IsEmpty();
  }));
  views::test::RunScheduledLayout(close->GetWidget());
  auto point = close->GetVisibleBounds().CenterPoint();
  views::View::ConvertPointToScreen(close, &point);
  auto* root = close->GetWidget()->GetRootView();
  auto view_point = point;
  views::View::ConvertPointFromScreen(root, &view_point);
  auto* hit = root->GetEventHandlerForPoint(view_point);
  ASSERT_TRUE(close->Contains(hit))
      << "close=" << close->GetBoundsInScreen().ToString()
      << " hit=" << (hit ? hit->GetClassName() : "none");
  ui::test::EventGenerator mouse(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(point);
  mouse.ClickLeftButton();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  ASSERT_TRUE(first && second_alive);
  EXPECT_EQ(&window, FlyWindowController::GetIfCreated());
  EXPECT_EQ("keep", content::EvalJs(contents, "window.unsavedControls"));
  EXPECT_TRUE(window.window_controls_for_testing()->GetVisible());
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload=null"));
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
  ASSERT_TRUE(InvokeNativeCommand(browser(), "hibernate-tabs"));
  EXPECT_FALSE(NativeTabSuspensionRequested(first));
  EXPECT_FALSE(NativeTabSuspensionRequested(second));
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
  EXPECT_FALSE(RequestNativeTabSuspension(playing));
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
  auto second_lifetime = second->GetWeakPtr();
  second->GetWindow()->Close();
  // Electron keeps the emptied workspace on screen with its welcome view; the
  // sidebar no longer observes the closed window.
  ASSERT_TRUE(base::test::RunUntil([&] { return !second_lifetime; }));
  EXPECT_EQ(nullptr, window.selected_browser_for_testing());
  EXPECT_TRUE(window.welcome_for_testing()->GetVisible());
  EXPECT_EQ(0u, window.tab_sidebar_for_testing()->row_count_for_testing());
  window.Select(first, true);
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

class FlyNativeFreezeBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  FlyNativeFreezeBrowserTest() {
    features_.InitWithFeaturesAndParameters(
        {{performance_manager::features::kCPUMeasurementInFreezingPolicy,
          {{"freezing_visible_protection_time", "0s"},
           {"freezing_audio_protection_time", "0s"}}}},
        {performance_manager::features::
             kDiscardFrozenBrowsingInstancesWithGrowingPMF,
         performance_manager::features::kInfiniteTabsFreezing});
  }

 private:
  base::test::ScopedFeatureList features_;
};

class SuspensionDebuggerClient : public content::DevToolsAgentHostClient {
 public:
  explicit SuspensionDebuggerClient(content::WebContents* contents)
      : host_(content::DevToolsAgentHost::GetOrCreateFor(contents)) {
    attached_ = host_->AttachClient(this);
  }
  ~SuspensionDebuggerClient() override {
    if (attached_)
      host_->DetachClient(this);
  }
  bool attached() const { return attached_; }
  void DispatchProtocolMessage(content::DevToolsAgentHost*,
                               base::span<const uint8_t>) override {}
  void AgentHostClosed(content::DevToolsAgentHost*) override {
    attached_ = false;
  }

 private:
  scoped_refptr<content::DevToolsAgentHost> host_;
  bool attached_ = false;
};

IN_PROC_BROWSER_TEST_F(FlyNativeFreezeBrowserTest,
                       RetainedFreezeProtectsPagesAndWakeUsesWorkspaceScope) {
  auto* first = browser();
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, url));
  auto* background = Active(first);
  const auto id = TabIdentity::Get(background);
  ASSERT_TRUE(content::ExecJs(
      background,
      "window.unsavedFlyDocument = {text: 'retained', edits: 42};"));
  ASSERT_TRUE(OpenNativeBlankTab(first));
  auto* visible = Active(first);
  ASSERT_TRUE(content::WaitForLoadStop(visible));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, url));
  auto* other = Active(second);
  ASSERT_TRUE(content::ExecJs(other, "window.otherWorkspaceDraft = 73;"));
  auto& window = FlyWindowController::Get();
  window.Select(first, true);
  auto frozen = [](content::WebContents* contents) {
    auto node = performance_manager::PerformanceManager::
        GetPrimaryPageNodeForWebContents(contents);
    return node && node->GetLifecycleState() ==
                       performance_manager::mojom::LifecycleState::kFrozen;
  };
  ASSERT_TRUE(NativeCommandAvailable(first, "hibernate-tabs"));
  ASSERT_TRUE(InvokeNativeCommand(first, "hibernate-tabs"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return frozen(background) && frozen(other); }));
  EXPECT_TRUE(NativeTabSuspensionRequested(background));
  EXPECT_TRUE(NativeTabSuspensionRequested(other));
  EXPECT_FALSE(NativeTabSuspensionRequested(visible));
  EXPECT_EQ(visible, Active(first));
  EXPECT_EQ(content::Visibility::VISIBLE, visible->GetVisibility());
  EXPECT_EQ(id, TabIdentity::Get(background));
  EXPECT_FALSE(background->WasDiscarded());
  // A workspace resume removes only this workspace's votes, with no reload.
  ASSERT_TRUE(InvokeNativeCommand(first, "wake-all-tabs"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(background); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_TRUE(frozen(other));
  EXPECT_EQ("retained",
            content::EvalJs(background, "window.unsavedFlyDocument.text",
                            content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ(42, content::EvalJs(background, "window.unsavedFlyDocument.edits",
                                content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ(false, content::EvalJs(background, "document.wasDiscarded"));

  ASSERT_TRUE(RequestNativeTabSuspension(background));
  ASSERT_TRUE(base::test::RunUntil([&] { return frozen(background); }));
  {
    SuspensionDebuggerClient debugger(background);
    ASSERT_TRUE(debugger.attached());
    ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(background); }));
    EXPECT_FALSE(NativeTabSuspensionRequested(background));
    EXPECT_FALSE(RequestNativeTabSuspension(background));
  }
  ASSERT_TRUE(RequestNativeTabSuspension(background));
  ASSERT_TRUE(base::test::RunUntil([&] { return frozen(background); }));
  first->GetTabStripModel()->SetTabPinned(0, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(background); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_FALSE(RequestNativeTabSuspension(background));
  EXPECT_FALSE(RequestNativeTabSuspension(visible));
  first->GetTabStripModel()->SetTabPinned(0, false);
  ASSERT_TRUE(RequestNativeTabSuspension(background));
  ASSERT_TRUE(base::test::RunUntil([&] { return frozen(background); }));
  first->GetTabStripModel()->ActivateTabAt(0);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(background); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_EQ(42, content::EvalJs(background, "window.unsavedFlyDocument.edits"));
  EXPECT_EQ(id, TabIdentity::Get(background));
  window.Select(second, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(other); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(other));
  EXPECT_EQ(73, content::EvalJs(other, "window.otherWorkspaceDraft"));
}

IN_PROC_BROWSER_TEST_F(
    FlyNativeFreezeBrowserTest,
    AutomaticSuspendUsesQuietDeadlineAndHonorsDisabledChoice) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(0));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* background = Active(browser());
  ASSERT_TRUE(content::ExecJs(background, "window.autoSuspendDraft = 99;"));
  ASSERT_TRUE(OpenNativeBlankTab(browser()));
  ASSERT_TRUE(content::WaitForLoadStop(Active(browser())));
  const auto now = base::TimeTicks::Now();
  const auto timeout = base::Minutes(5);
  auto frozen = [&] {
    auto node = performance_manager::PerformanceManager::
        GetPrimaryPageNodeForWebContents(background);
    return node && node->GetLifecycleState() ==
                       performance_manager::mojom::LifecycleState::kFrozen;
  };
  EXPECT_FALSE(MaybeAutoSuspendNativeTab(background, timeout, now));
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(4)));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_TRUE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(6)));
  ASSERT_TRUE(base::test::RunUntil(frozen));
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(15));
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(0));
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_EQ(99, content::EvalJs(background, "window.autoSuspendDraft"));
  // Disabling the automatic timeout preserves an explicit manual request.
  ASSERT_TRUE(RequestNativeTabSuspension(background));
  ASSERT_TRUE(base::test::RunUntil(frozen));
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(15));
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(0));
  EXPECT_TRUE(NativeTabSuspensionRequested(background));
  EXPECT_TRUE(frozen());
  ResumeNativeTab(background);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(); }));
  // Resume restarts the quiet interval instead of immediately refreezing.
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(9)));
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(13)));
  EXPECT_TRUE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(15)));
  ASSERT_TRUE(base::test::RunUntil(frozen));
  auto* model = browser()->GetTabStripModel();
  model->SetTabPinned(0, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(); }));
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(20)));
  model->SetTabPinned(0, false);
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(21)));
  EXPECT_FALSE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(25)));
  EXPECT_TRUE(
      MaybeAutoSuspendNativeTab(background, timeout, now + base::Minutes(27)));
  ASSERT_TRUE(base::test::RunUntil(frozen));
  model->ActivateTabAt(0);
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_EQ(99, content::EvalJs(background, "window.autoSuspendDraft"));
  EXPECT_EQ(false, content::EvalJs(background, "document.wasDiscarded"));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    PerformancePreferencesPreserveKeyboardDraftAndFutureData) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.preferences().SetSuspendTimeoutMinutes(0));
  const auto keyboard_before = window.commands().Read();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  auto* draft = editor.field_for_testing("spawn-tab", false);
  ASSERT_TRUE(draft);
  draft->SetText(u"Ctrl+Alt+Y");
  editor.ContentsChanged(draft, u"Ctrl+Alt+Y");
  views::test::ButtonTestApi(editor.record_for_testing("spawn-tab", false))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(editor.recording());
  editor.ShowPerformance();
  EXPECT_FALSE(editor.recording());
  auto* timeout = editor.suspension_timeout_for_testing();
  ASSERT_TRUE(timeout);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return timeout->IsDrawn() && !timeout->GetVisibleBounds().IsEmpty();
  }));
  EXPECT_TRUE(timeout->GetText().ends_with(u"Off"));
  ui::test::EventGenerator input(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  input.MoveMouseTo(timeout->GetBoundsInScreen().CenterPoint());
  input.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.preferences().ReadSuspendTimeoutMinutes() == 5; }));
  timeout->RequestFocus();
  ASSERT_TRUE(timeout->HasFocus());
  input.PressKey(ui::VKEY_SPACE, ui::EF_NONE);
  input.ReleaseKey(ui::VKEY_SPACE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.preferences().ReadSuspendTimeoutMinutes() == 15; }));
  EXPECT_EQ(keyboard_before, window.commands().Read());
  auto* prefs = g_browser_process->local_state();
  const auto valid = prefs->GetDict(kPreferencesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  prefs->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(timeout->GetEnabled());
  views::test::ButtonTestApi(timeout).NotifyDefaultMouseClick();
  EXPECT_EQ(future, prefs->GetDict(kPreferencesPref));
  prefs->SetDict(kPreferencesPref, valid.Clone());
  EXPECT_TRUE(timeout->GetEnabled());
  editor.ShowKeyboard();
  draft = editor.field_for_testing("spawn-tab", false);
  ASSERT_TRUE(draft);
  EXPECT_EQ(u"Ctrl+Alt+Y", draft->GetText());
  EXPECT_EQ(keyboard_before, window.commands().Read());
}

IN_PROC_BROWSER_TEST_F(FlyNativeFreezeBrowserTest,
                       GlobalMediaResumeWakesFrozenPausedNativeSession) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* playing = Active(first);
  ASSERT_TRUE(content::ExecJs(playing, R"js(
    const audio = document.createElement('audio');
    audio.id = 'freeze-player';
    audio.src = '/media/engagement/engagement_valid_audio.webm';
    audio.loop = true;
    document.body.appendChild(audio);
    audio.play();
  )js"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* session = content::MediaSession::GetIfExists(playing);
    if (!session)
      return false;
    const auto info = session->GetMediaSessionInfoSync();
    return info && info->is_controllable;
  }));
  ASSERT_TRUE(InvokeNativeCommand(first, "toggle-global-media"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto info = content::MediaSession::GetIfExists(playing)
                          ->GetMediaSessionInfoSync();
    return !playing->IsCurrentlyAudible() && info && info->is_controllable &&
           info->playback_state == media_session::mojom::MediaPlaybackState::kPaused;
  }));
  ASSERT_TRUE(RequestNativeTabSuspension(playing));
  auto node = performance_manager::PerformanceManager::
      GetPrimaryPageNodeForWebContents(playing);
  ASSERT_TRUE(node);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return node->GetLifecycleState() ==
           performance_manager::mojom::LifecycleState::kFrozen;
  }));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-global-media"));
  EXPECT_FALSE(NativeTabSuspensionRequested(playing));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return node->GetLifecycleState() ==
           performance_manager::mojom::LifecycleState::kRunning;
  }));
  EXPECT_EQ(false, content::EvalJs(playing,
      "new Promise(resolve => { const audio = document.querySelector('#freeze-player'); "
      "if (!audio.paused) resolve(audio.paused); "
      "else audio.addEventListener('playing', () => resolve(audio.paused), {once:true}); })",
      content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ(second, window.selected_browser_for_testing());
  ExpectVisible(playing, false);
}

class FlyReplacementDiscardBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  FlyReplacementDiscardBrowserTest() {
    features_.InitAndDisableFeature(features::kWebContentsDiscard);
  }
 private:
  base::test::ScopedFeatureList features_;
};
class FlyRetainedDiscardBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  FlyRetainedDiscardBrowserTest() {
    features_.InitAndEnableFeature(features::kWebContentsDiscard);
  }
 private:
  base::test::ScopedFeatureList features_;
};

void ExpectDiscardedTabKeepsFlyIdentity(BrowserWindowInterface* browser,
                                       const GURL& url, bool replacement) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser, url));
  auto* model = browser->GetTabStripModel();
  auto* original = model->GetActiveWebContents();
  const std::string id = TabIdentity::Get(original);
  auto* const profile = original->GetBrowserContext();
  ASSERT_TRUE(content::ExecJs(original, "document.cookie='discard=retained'"));
  // Discard a hidden, pinned tab, leaving another tab visible and focused.
  model->SetTabPinned(0, true);
  ASSERT_TRUE(OpenNativeBlankTab(browser));
  ASSERT_EQ(2, model->count());
  auto* selected = model->GetActiveWebContents();
  ASSERT_NE(original, selected);
  auto* lifecycle =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(original);
  ASSERT_TRUE(lifecycle);
  ASSERT_TRUE(lifecycle->DiscardTab(
      ::mojom::LifecycleUnitDiscardReason::EXTERNAL));
  auto* discarded = model->GetWebContentsAt(0);
  EXPECT_EQ(replacement, discarded != original);
  EXPECT_EQ(id, TabIdentity::Get(discarded));
  EXPECT_EQ(profile, discarded->GetBrowserContext());
  EXPECT_TRUE(model->IsTabPinned(0));
  EXPECT_EQ(selected, model->GetActiveWebContents());
  EXPECT_EQ(content::Visibility::HIDDEN, discarded->GetVisibility());
  EXPECT_TRUE(discarded->WasDiscarded());
  // The old contents' destructor must not make the ID available to a second
  // restored tab. Private metadata remains non-persistable as usual.
  auto probe = content::WebContents::Create(
      content::WebContents::CreateParams(profile));
  TabIdentity::Restore(probe.get(), {{TabIdentity::kExtraDataKey, id}});
  EXPECT_NE(id, TabIdentity::Get(probe.get()));
  std::map<std::string, std::string> metadata;
  TabIdentity::Populate(discarded, &metadata);
  EXPECT_EQ(id, metadata.at(TabIdentity::kExtraDataKey));
  content::TestNavigationObserver reload(discarded);
  model->ActivateTabAt(0);
  reload.Wait();
  EXPECT_EQ(discarded, model->GetActiveWebContents());
  EXPECT_EQ(id, TabIdentity::Get(discarded));
  EXPECT_EQ(true, content::EvalJs(discarded, "document.wasDiscarded"));
  EXPECT_EQ("discard=retained", content::EvalJs(discarded, "document.cookie"));
}

IN_PROC_BROWSER_TEST_F(FlyReplacementDiscardBrowserTest,
                       NativeReplacementDiscardPreservesFlyIdentity) {
  ExpectDiscardedTabKeepsFlyIdentity(
      browser(), embedded_test_server()->GetURL("/title1.html"), true);
}
IN_PROC_BROWSER_TEST_F(FlyRetainedDiscardBrowserTest,
                       NativeRetainedDiscardPreservesFlyIdentity) {
  ExpectDiscardedTabKeepsFlyIdentity(
      browser(), embedded_test_server()->GetURL("/title1.html"), false);
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
                       WorkspacePointerMenuReordersWithoutChangingProfileIdentity) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* original_page = Active(first);
  ASSERT_TRUE(content::ExecJs(original_page, "document.cookie='order=first'"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  window.CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected != first && selected != second;
  }));
  auto* third = window.selected_browser_for_testing();
  WorkspaceService service(*g_browser_process->local_state());
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(3u, before->items.size());
  const auto third_item = before->items.back();
  ASSERT_EQ(third->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
            third_item.profile_directory);
  window.Select(first, true);
  auto menu = [&](bool expect_down, base::OnceCallback<void(ui::SimpleMenuModel*)> choose) {
    views::View* control = nullptr;
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (const auto& child : window.workspace_buttons_for_testing()->children()) {
        auto* button = views::AsViewClass<views::MdTextButton>(child.get());
        if (button && button->GetText() == base::UTF8ToUTF16(third_item.name) &&
            button->IsDrawn() && !button->GetVisibleBounds().IsEmpty()) {
          control = button;
          return true;
        }
      }
      return false;
    }));
    bool inspected = false;
    base::RepeatingTimer inspect_menu;
    inspect_menu.Start(FROM_HERE, base::Milliseconds(1),
        base::BindLambdaForTesting([&] {
          auto* model = window.workspace_context_menu_for_testing(third_item.id);
          if (!model)
            return;
          inspect_menu.Stop();
          EXPECT_TRUE(model->IsEnabledAt(0));
          EXPECT_EQ(expect_down, model->IsEnabledAt(1));
          std::move(choose).Run(model);
          inspected = true;
        }));
    ui::test::EventGenerator mouse(
        window.desktop_widget()->GetNativeWindow()->GetRootWindow());
    mouse.MoveMouseTo(control->GetBoundsInScreen().CenterPoint());
    mouse.ClickRightButton();
    ASSERT_TRUE(base::test::RunUntil([&] { return inspected; }));
  };
  menu(false, base::BindOnce([](ui::SimpleMenuModel* model) {
    model->ActivatedAt(0);
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto current = service.Read();
    return current && current->items[1].id == third_item.id;
  }));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_EQ(before->items[0], service.Read()->items[0]);
  EXPECT_EQ(third_item, service.Read()->items[1]);
  EXPECT_EQ(before->items[1], service.Read()->items[2]);
  EXPECT_EQ(original_page, Active(first));
  EXPECT_EQ("order=first", content::EvalJs(original_page, "document.cookie"));
  ASSERT_TRUE(window.RunCommand(first, "next-workspace"));
  EXPECT_EQ(third, window.selected_browser_for_testing());
  window.Select(first, true);
  menu(true, base::BindOnce([](ui::SimpleMenuModel* model) {
    model->ActivatedAt(1);
  }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto current = service.Read();
    return current && current->items == before->items;
  }));

  // A policy/schema change inside the native menu loop must invalidate its
  // deferred edit, even though the invoking row has not yet been rebuilt.
  auto* prefs = g_browser_process->local_state();
  const auto valid = prefs->GetDict(kWorkspacesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  menu(false, base::BindOnce(
      [](PrefService* prefs, base::DictValue future,
         ui::SimpleMenuModel* model) {
        prefs->SetDict(kWorkspacesPref, std::move(future));
        model->ActivatedAt(0);
      }, base::Unretained(prefs), future.Clone()));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(future, prefs->GetDict(kWorkspacesPref));
  prefs->SetDict(kWorkspacesPref, valid.Clone());
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_EQ(original_page, Active(first));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspacePointerRenamePreservesProfileAndRejectsStaleEdits) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(first);
  ASSERT_TRUE(content::ExecJs(page, "document.cookie='rename=first'"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  window.Select(first, true);
  WorkspaceService service(*g_browser_process->local_state());
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(2u, before->items.size());
  const auto item = before->items.back();
  const auto directory = second->GetProfile()->GetPath();
  const auto tab_id = TabIdentity::Get(Active(second));
  auto open = [&] {
    views::View* row = nullptr;
    ASSERT_TRUE(base::test::RunUntil([&] {
      for (const auto& child : window.workspace_buttons_for_testing()->children()) {
        auto* button = views::AsViewClass<views::MdTextButton>(child.get());
        if (button && button->GetText() ==
                          base::UTF8ToUTF16(service.Read()->items.back().name)) {
          row = button;
          return true;
        }
      }
      return false;
    }));
    base::RepeatingTimer choose;
    choose.Start(FROM_HERE, base::Milliseconds(1),
        base::BindLambdaForTesting([&] {
          auto* model = window.workspace_context_menu_for_testing(item.id);
          if (!model)
            return;
          choose.Stop();
          EXPECT_TRUE(model->IsEnabledAt(3));
          model->ActivatedAt(3);
        }));
    ui::test::EventGenerator mouse(
        window.desktop_widget()->GetNativeWindow()->GetRootWindow());
    mouse.MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
    mouse.ClickRightButton();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return window.workspace_rename_dialog_for_testing();
    }));
  };
  open();
  auto* dialog = window.workspace_rename_dialog_for_testing();
  ASSERT_TRUE(dialog);
  auto* field = views::AsViewClass<views::Textfield>(
      dialog->GetContentsView()->GetViewByID(1));
  ASSERT_TRUE(field);
  ASSERT_TRUE(base::test::RunUntil([&] { return field->HasFocus(); }));
  ExpectVisible(page, true);
  EXPECT_EQ(first, window.selected_browser_for_testing());
  auto* delegate = dialog->widget_delegate()->AsDialogDelegate();
  ASSERT_TRUE(delegate);
  field->SetText(base::UTF8ToUTF16(before->items.front().name));
  delegate->AcceptDialog();
  EXPECT_FALSE(dialog->IsClosed());
  EXPECT_EQ(before->items, service.Read()->items);
  auto* error = views::AsViewClass<views::Label>(
      dialog->GetContentsView()->GetViewByID(2));
  ASSERT_TRUE(error);
  EXPECT_FALSE(error->GetText().empty());
  field->SetText(u"Diseño / 日本語");
  // Use the dialog's actual Return accelerator to submit the valid name.
  ui::test::EventGenerator keys(dialog->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_rename_dialog_for_testing();
  }));
  ASSERT_TRUE(service.Read());
  EXPECT_EQ("Diseño / 日本語", service.Read()->items.back().name);
  EXPECT_EQ(item.id, service.Read()->items.back().id);
  EXPECT_EQ(item.profile_directory, service.Read()->items.back().profile_directory);
  EXPECT_EQ(directory, second->GetProfile()->GetPath());
  EXPECT_EQ(tab_id, TabIdentity::Get(Active(second)));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_EQ("rename=first", content::EvalJs(page, "document.cookie"));

  open();
  dialog = window.workspace_rename_dialog_for_testing();
  ASSERT_TRUE(dialog);
  ui::test::EventGenerator cancel(dialog->GetNativeWindow()->GetRootWindow());
  cancel.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_rename_dialog_for_testing();
  }));
  EXPECT_EQ("Diseño / 日本語", service.Read()->items.back().name);

  open();
  dialog = window.workspace_rename_dialog_for_testing();
  ASSERT_TRUE(dialog);
  auto* prefs = g_browser_process->local_state();
  const auto valid = prefs->GetDict(kWorkspacesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  prefs->SetDict(kWorkspacesPref, future.Clone());
  dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  EXPECT_FALSE(dialog->IsClosed());
  EXPECT_EQ(future, prefs->GetDict(kWorkspacesPref));
  prefs->SetDict(kWorkspacesPref, valid.Clone());
  window.Select(second, true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_rename_dialog_for_testing();
  }));
  EXPECT_EQ(*valid.FindList("items"),
            *prefs->GetDict(kWorkspacesPref).FindList("items"));
  EXPECT_EQ(item.id, service.Read()->active_id);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeTabMoveUsesDestinationIdentityAndNativeRestore) {
  auto* destination = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      destination, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(destination),
                              "document.cookie='move=destination'; "
                              "localStorage.identity='destination'"));
  destination->GetTabStripModel()->SetTabPinned(0, true);
  auto* source = SecondWorkspace();
  ASSERT_TRUE(source);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      source, embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(source);
  ASSERT_TRUE(content::ExecJs(
      contents,
      "document.cookie='move=source'; localStorage.identity='source'; "
      "window.unsavedMove='source-only'"));
  source->GetTabStripModel()->SetTabPinned(0, true);
  SetTabAudioMuted(contents, true, TabMutedReason::kAudioIndicator, {});
  const auto id = TabIdentity::Get(contents);
  const auto native_id = extensions::ExtensionTabUtil::GetTabId(contents);
  const auto source_profile = source->GetProfile()->GetPath();
  const auto source_weak = contents->GetWeakPtr();
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog && catalog->items.size() == 2);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.ShowTabMovePicker(source, contents));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window.tab_move_picker().active(); }));
  window.ToggleWorkspacePicker();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.tab_move_picker().active() &&
           window.workspace_picker().active();
  }));
  window.workspace_picker().Cancel();
  ChooseMoveDestination(source, catalog->items.front().name, true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !source_weak && !window.tab_move_pending_for_testing() &&
           window.selected_browser_for_testing() == destination;
  }));
  auto* moved = Active(destination);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(content::WaitForLoadStop(moved));
  EXPECT_EQ(destination->GetProfile(), moved->GetBrowserContext());
  EXPECT_NE(source_profile, destination->GetProfile()->GetPath());
  EXPECT_EQ(id, TabIdentity::Get(moved));
  EXPECT_NE(native_id, extensions::ExtensionTabUtil::GetTabId(moved));
  EXPECT_TRUE(destination->GetTabStripModel()->IsTabPinned(1));
  EXPECT_EQ(1, destination->GetTabStripModel()->active_index());
  EXPECT_TRUE(moved->IsAudioMuted());
  EXPECT_EQ("move=destination", content::EvalJs(moved, "document.cookie"));
  EXPECT_EQ("destination", content::EvalJs(moved, "localStorage.identity"));
  EXPECT_EQ(true,
            content::EvalJs(moved, "typeof window.unsavedMove==='undefined'"));
  ExpectVisible(moved, true);
  auto moved_weak = moved->GetWeakPtr();
  destination->GetTabStripModel()->CloseWebContents(
      moved, TabCloseTypes::CLOSE_USER_GESTURE |
                 TabCloseTypes::CLOSE_CREATE_HISTORICAL_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] { return !moved_weak; }));
  ASSERT_TRUE(chrome::IsCommandEnabled(destination, IDC_RESTORE_TAB));
  chrome::ExecuteCommand(destination, IDC_RESTORE_TAB);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return destination->GetTabStripModel()->count() == 2 &&
           TabIdentity::Get(Active(destination)) == id;
  }));
  EXPECT_EQ(destination->GetProfile(),
            Active(destination)->GetBrowserContext());
  EXPECT_EQ(catalog->items, service.Read()->items);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeTabMoveCancellationAndLostDestinationKeepSource) {
  auto* source = SecondWorkspace();
  ASSERT_TRUE(source);
  auto* destination = SecondWorkspace();
  ASSERT_TRUE(destination);
  auto destination_weak = destination->GetWeakPtr();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      source, embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(source);
  auto source_weak = contents->GetWeakPtr();
  const auto id = TabIdentity::Get(contents);
  ASSERT_TRUE(
      content::ExecJs(contents,
                      "document.cookie='move=kept'; window.unsavedMove='kept'; "
                      "window.onbeforeunload=()=> 'unsaved'"));
  content::PrepContentsForBeforeUnloadTest(contents);
  WorkspaceService service(*g_browser_process->local_state());
  ASSERT_TRUE(service.Rename(service.Read()->items.back().id,
                             "Move cancellation destination"));
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog && catalog->items.size() == 3);
  const auto destination_name = catalog->items.back().name;
  FlyWindowController::Get().Select(source, true);
  const int destination_count = destination->GetTabStripModel()->count();
  ChooseMoveDestination(source, destination_name);
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  EXPECT_EQ(destination_count, destination->GetTabStripModel()->count());
  dialog->view()->CancelAppModalDialog();
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.tab_move_pending_for_testing(); }));
  EXPECT_TRUE(source_weak);
  EXPECT_EQ("kept", content::EvalJs(contents, "window.unsavedMove"));
  EXPECT_EQ(id, TabIdentity::Get(contents));
  EXPECT_EQ(destination_count, destination->GetTabStripModel()->count());
  ChooseMoveDestination(source, destination_name);
  dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  destination->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !destination_weak; }));
  // User consent cannot close the source when its prepared destination is gone.
  dialog->view()->AcceptAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.tab_move_pending_for_testing(); }));
  ASSERT_TRUE(source_weak);
  EXPECT_EQ(source, window.selected_browser_for_testing());
  EXPECT_EQ("kept", content::EvalJs(contents, "window.unsavedMove"));
  EXPECT_EQ("move=kept", content::EvalJs(contents, "document.cookie"));
  EXPECT_EQ(id, TabIdentity::Get(contents));
  EXPECT_EQ(catalog->items, service.Read()->items);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload=null"));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativeTabMoveKeepsPrivateDestinationAndPersistentStateSeparate) {
  auto* normal_source = SecondWorkspace();
  ASSERT_TRUE(normal_source);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      normal_source, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(normal_source),
                              "document.cookie='move=normal-source'"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(normal_source, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != normal_source; }));
  auto* private_source = window.selected_browser_for_testing();
  ASSERT_TRUE(private_source && private_source->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      private_source, embedded_test_server()->GetURL("/title1.html")));
  auto* source = Active(private_source);
  auto source_weak = source->GetWeakPtr();
  const auto id = TabIdentity::Get(source);
  ASSERT_TRUE(content::ExecJs(source, "document.cookie='move=private-source'"));
  window.Select(browser(), true);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(browser()),
                              "document.cookie='move=normal-destination'"));
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != browser(); }));
  auto* destination = window.selected_browser_for_testing();
  ASSERT_TRUE(destination && destination->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      destination, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(destination),
                              "document.cookie='move=private-destination'"));
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  window.Select(private_source, true);
  ChooseMoveDestination(private_source,
                        "Private · " + catalog->items.front().name);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !source_weak && !window.tab_move_pending_for_testing() &&
           window.selected_browser_for_testing() == destination;
  }));
  auto* moved = Active(destination);
  ASSERT_TRUE(content::WaitForLoadStop(moved));
  EXPECT_TRUE(destination->GetProfile()->IsOffTheRecord());
  EXPECT_EQ(destination->GetProfile(), moved->GetBrowserContext());
  EXPECT_EQ(id, TabIdentity::Get(moved));
  EXPECT_EQ("move=private-destination",
            content::EvalJs(moved, "document.cookie"));
  EXPECT_EQ("move=normal-source",
            content::EvalJs(Active(normal_source), "document.cookie"));
  EXPECT_EQ("move=normal-destination",
            content::EvalJs(Active(browser()), "document.cookie"));
  std::map<std::string, std::string> extra;
  TabIdentity::Populate(moved, &extra);
  EXPECT_TRUE(extra.empty());
  EXPECT_FALSE(TabIdentity::CanPersist(moved));
  EXPECT_EQ(catalog->items, service.Read()->items);
  EXPECT_EQ(catalog->active_id, service.Read()->active_id);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeTabMoveOpensPreviouslyUnloadedDestinationProfile) {
  auto* source = SecondWorkspace();
  ASSERT_TRUE(source);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      source, embedded_test_server()->GetURL("/title1.html")));
  auto source_weak = Active(source)->GetWeakPtr();
  const auto id = TabIdentity::Get(Active(source));
  ASSERT_TRUE(content::ExecJs(Active(source), "document.cookie='move=source'"));
  auto* manager = g_browser_process->profile_manager();
  const auto path = manager->GenerateNextProfileDirectoryPath();
  WorkspaceService service(*g_browser_process->local_state());
  const auto destination =
      service.Add("Dormant destination", path.BaseName().AsUTF8Unsafe());
  ASSERT_TRUE(destination);
  EXPECT_EQ(nullptr, manager->GetProfileByPath(path));
  const auto before = service.Read();
  ChooseMoveDestination(source, destination->name);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !source_weak && !window.tab_move_pending_for_testing(); }));
  auto* target = window.selected_browser_for_testing();
  ASSERT_TRUE(target);
  EXPECT_EQ(path, target->GetProfile()->GetPath());
  auto* moved = Active(target);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(content::WaitForLoadStop(moved));
  EXPECT_EQ(id, TabIdentity::Get(moved));
  EXPECT_EQ("", content::EvalJs(moved, "document.cookie"));
  EXPECT_EQ(before->items, service.Read()->items);
  EXPECT_EQ(destination->id, service.Read()->active_id);
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativePrivateTabMoveLoadsDestinationWithoutPersistingItsUrl) {
  auto* normal_source = SecondWorkspace();
  ASSERT_TRUE(normal_source);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(normal_source, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != normal_source; }));
  auto* source = window.selected_browser_for_testing();
  ASSERT_TRUE(source && source->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      source, embedded_test_server()->GetURL("/title1.html")));
  auto source_weak = Active(source)->GetWeakPtr();
  const auto id = TabIdentity::Get(Active(source));
  ASSERT_TRUE(
      content::ExecJs(Active(source), "document.cookie='move=private-source'"));
  auto* manager = g_browser_process->profile_manager();
  const auto path = manager->GenerateNextProfileDirectoryPath();
  WorkspaceService service(*g_browser_process->local_state());
  const auto target = service.Add("Dormant private destination",
                                  path.BaseName().AsUTF8Unsafe());
  ASSERT_TRUE(target);
  const auto before = service.Read();
  EXPECT_EQ(nullptr, manager->GetProfileByPath(path));
  ChooseMoveDestination(source, "Private · " + target->name);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !source_weak && !window.tab_move_pending_for_testing(); }));
  auto* destination = window.selected_browser_for_testing();
  ASSERT_TRUE(destination && destination->GetProfile()->IsOffTheRecord());
  EXPECT_EQ(path, destination->GetProfile()->GetOriginalProfile()->GetPath());
  auto* moved = Active(destination);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(content::WaitForLoadStop(moved));
  EXPECT_EQ(id, TabIdentity::Get(moved));
  EXPECT_EQ("", content::EvalJs(moved, "document.cookie"));
  EXPECT_FALSE(TabIdentity::CanPersist(moved));
  EXPECT_EQ(before->items, service.Read()->items);
  EXPECT_EQ(before->active_id, service.Read()->active_id);
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    WorkspaceRemovalCancelsBeforeUnloadThenRetiresAllWindows) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto* profile = second->GetProfile();
  ScopedProfileKeepAlive held(profile,
                              ProfileKeepAliveOrigin::kProfileDeletionProcess);
  auto second_weak = second->GetWeakPtr();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(second);
  ASSERT_TRUE(content::ExecJs(page,
                              "document.cookie='retirement=source'; "
                              "window.onbeforeunload=()=> 'unsaved'"));
  content::PrepContentsForBeforeUnloadTest(page);
  WorkspaceService service(*g_browser_process->local_state());
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(2u, before->items.size());
  const auto item = before->items.back();
  const auto path = profile->GetPath();
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(window.RunCommand(second, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != second; }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(private_browser &&
              private_browser->GetProfile()->IsOffTheRecord());
  auto private_weak = private_browser->GetWeakPtr();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      private_browser, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(private_browser),
                              "document.cookie='private=temporary'"));

  extensions::TestExtensionDir directory;
  directory.WriteManifest(
      base::DictValue()
          .Set("manifest_version", 3)
          .Set("name", "Fly retirement window probe")
          .Set("version", std::string(version_info::GetVersionNumber()))
          .Set("permissions", base::ListValue().Append("tabs"))
          .Set("background",
               base::DictValue().Set("service_worker", "worker.js")));
  directory.WriteFile(FILE_PATH_LITERAL("worker.js"),
                      "chrome.runtime.onInstalled.addListener(()=>{});");
  extensions::ChromeTestExtensionLoader loader(profile);
  const auto extension = loader.LoadExtension(directory.UnpackedPath());
  ASSERT_TRUE(extension);
  auto extension_attempt = [&](bool include_tabs) {
    return extensions::BackgroundScriptExecutor::ExecuteScript(
        profile, extension->id(),
        content::JsReplace(R"(
          (() => {
          const attempts = [
            new Promise(resolve => chrome.windows.create({url:'about:blank'}, value =>
              resolve({created:!!value, denied:!!chrome.runtime.lastError})))];
          if ($1) attempts.push(new Promise(resolve =>
            chrome.tabs.create({url:'about:blank'}, value =>
              resolve({created:!!value, denied:!!chrome.runtime.lastError}))));
          Promise.all(attempts).then(value => chrome.test.sendScriptResult(value));
          })();
        )",
                           include_tabs),
        extensions::BackgroundScriptExecutor::ResultCapture::kSendScriptResult);
  };
  window.Select(first, true);
  OpenWorkspaceRemoval(item);
  auto* confirmation = window.workspace_remove_dialog_for_testing();
  ASSERT_TRUE(confirmation);
  confirmation->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  EXPECT_TRUE(IsWorkspaceRetiring(path));
  EXPECT_FALSE(profile->AllowsBrowserWindows());
  const auto pending_attempt = extension_attempt(false);
  ASSERT_TRUE(pending_attempt.is_list());
  ASSERT_EQ(1u, pending_attempt.GetList().size());
  for (const auto& result : pending_attempt.GetList()) {
    EXPECT_EQ(false, result.GetDict().FindBool("created"));
    EXPECT_EQ(true, result.GetDict().FindBool("denied"));
  }
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.workspace_removal_pending_for_testing(); }));
  EXPECT_TRUE(second_weak && private_weak);
  EXPECT_TRUE(profile->AllowsBrowserWindows());
  EXPECT_FALSE(IsWorkspaceRetiring(path));
  EXPECT_EQ(before->items, service.Read()->items);
  EXPECT_EQ("retirement=source", content::EvalJs(page, "document.cookie"));
  ASSERT_TRUE(content::ExecJs(page, "window.onbeforeunload=null"));

  OpenWorkspaceRemoval(item);
  confirmation = window.workspace_remove_dialog_for_testing();
  ASSERT_TRUE(confirmation);
  confirmation->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_removal_pending_for_testing() &&
           service.Read()->items.size() == 1 && !second_weak && !private_weak;
  }));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_FALSE(IsWorkspaceRetiring(path));
  EXPECT_EQ(true, service.IsProfileRetired(item.profile_directory));
  EXPECT_FALSE(profile->AllowsBrowserWindows());
  EXPECT_EQ(BrowserWindowInterface::CreationStatus::kErrorProfileUnsuitable,
            GetBrowserWindowCreationStatusForProfile(*profile));
  const auto retired_attempt = extension_attempt(true);
  ASSERT_TRUE(retired_attempt.is_list());
  for (const auto& result : retired_attempt.GetList()) {
    EXPECT_EQ(false, result.GetDict().FindBool("created"));
    EXPECT_EQ(true, result.GetDict().FindBool("denied"));
  }
  {
    base::ScopedAllowBlockingForTesting allow_files;
    EXPECT_TRUE(base::DirectoryExists(path));
  }
  // Stale upstream startup metadata must not reload the retired profile.
  auto* local_state = g_browser_process->local_state();
  local_state->SetFilePath(prefs::kProfileLastUsed, path.BaseName());
  EXPECT_NE(path.BaseName(), ProfileManager::GetLastUsedProfileBaseName());
  local_state->SetList(
      prefs::kProfilesLastActive,
      base::ListValue()
          .Append(item.profile_directory)
          .Append(first->GetProfile()->GetPath().BaseName().AsUTF8Unsafe()));
  for (auto* restored : ProfileManager::GetLastOpenedProfiles())
    EXPECT_NE(path, restored->GetPath());
  auto* recreated = SecondWorkspace();
  ASSERT_TRUE(recreated);
  EXPECT_EQ(item.name, service.Read()->items.back().name);
  EXPECT_NE(item.id, service.Read()->items.back().id);
  EXPECT_NE(path, recreated->GetProfile()->GetPath());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      recreated, embedded_test_server()->GetURL("/title1.html")));
  EXPECT_EQ("", content::EvalJs(Active(recreated), "document.cookie"));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    WorkspaceRemovalDialogRejectsStaleCatalogAndLastWorkspace) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto second_weak = second->GetWeakPtr();
  auto& window = FlyWindowController::Get();
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog && catalog->items.size() == 2);
  const auto item = catalog->items.back();
  OpenWorkspaceRemoval(item);
  auto* dialog = window.workspace_remove_dialog_for_testing();
  ASSERT_TRUE(dialog);
  ui::test::EventGenerator keys(dialog->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.workspace_remove_dialog_for_testing(); }));
  EXPECT_EQ(catalog->items, service.Read()->items);
  EXPECT_TRUE(second_weak);
  OpenWorkspaceRemoval(item);
  dialog = window.workspace_remove_dialog_for_testing();
  auto* prefs = g_browser_process->local_state();
  const auto valid = prefs->GetDict(kWorkspacesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  prefs->SetDict(kWorkspacesPref, future.Clone());
  dialog->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  EXPECT_FALSE(dialog->IsClosed());
  EXPECT_EQ(future, prefs->GetDict(kWorkspacesPref));
  EXPECT_TRUE(second_weak);
  EXPECT_FALSE(window.workspace_removal_pending_for_testing());
  prefs->SetDict(kWorkspacesPref, valid.Clone());
  window.Select(browser(), true);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.workspace_remove_dialog_for_testing(); }));
  // The UI retains the legacy rule that the last workspace cannot be removed.
  second->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !second_weak; }));
  ASSERT_TRUE(service.Remove(item.id));
  views::View* row = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto& children = window.workspace_buttons_for_testing()->children();
    if (children.size() == 1) {
      row = children.front();
      return true;
    }
    return false;
  }));
  base::RepeatingTimer check;
  check.Start(
      FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
        auto* menu = window.workspace_context_menu_for_testing(
            service.Read()->items.front().id);
        if (!menu)
          return;
        check.Stop();
        EXPECT_FALSE(menu->IsEnabledAt(4));
        ui::test::EventGenerator escape(
            window.desktop_widget()->GetNativeWindow()->GetRootWindow());
        escape.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
      }));
  ui::test::EventGenerator mouse(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  mouse.ClickRightButton();
  EXPECT_EQ(1u, service.Read()->items.size());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspaceProfileCompletionRejectsRetiredAndFutureCatalogs) {
  auto* first = browser();
  auto& window = FlyWindowController::Get();
  auto* manager = g_browser_process->profile_manager();
  auto* prefs = g_browser_process->local_state();
  WorkspaceService service(*prefs);
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(1u, before->items.size());
  auto has_browser = [](Profile* profile) {
    bool found = false;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          found |= model->GetProfile() == profile;
          return true;
        });
    return found;
  };
  window.CreateWorkspace();
  const auto created = service.Read();
  ASSERT_TRUE(created);
  ASSERT_EQ(2u, created->items.size());
  const auto retired = created->items.back();
  const auto retired_path = manager->user_data_dir().AppendASCII(
      retired.profile_directory);
  // No browser exists yet: retire only the metadata while profile I/O is
  // pending, before allowing the already queued completion to run.
  ASSERT_TRUE(service.Remove(retired.id));
  base::test::TestFuture<Profile*> retired_ready;
  manager->CreateProfileAsync(retired_path, retired_ready.GetCallback());
  auto* retired_profile = retired_ready.Get();
  ASSERT_TRUE(retired_profile);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(has_browser(retired_profile));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  ASSERT_TRUE(service.Read());
  EXPECT_EQ(before->items, service.Read()->items);
  EXPECT_FALSE(service.Add(retired.name, retired.profile_directory));

  window.CreateWorkspace();
  const auto pending = service.Read();
  ASSERT_TRUE(pending);
  ASSERT_EQ(2u, pending->items.size());
  const auto pending_path = manager->user_data_dir().AppendASCII(
      pending->items.back().profile_directory);
  const auto valid = prefs->GetDict(kWorkspacesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  prefs->SetDict(kWorkspacesPref, future.Clone());
  base::test::TestFuture<Profile*> future_ready;
  manager->CreateProfileAsync(pending_path, future_ready.GetCallback());
  auto* future_profile = future_ready.Get();
  ASSERT_TRUE(future_profile);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(has_browser(future_profile));
  EXPECT_EQ(first, window.selected_browser_for_testing());
  EXPECT_EQ(future, prefs->GetDict(kWorkspacesPref));
  prefs->SetDict(kWorkspacesPref, valid.Clone());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspacePickerUsesNativeKeysAndRejectsStaleChoices) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* original_page = Active(first);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  const auto item = std::ranges::find(
      catalog->items, second->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
      &Workspace::profile_directory);
  ASSERT_NE(item, catalog->items.end());
  ASSERT_TRUE(service.Rename(item->id, "Trabajo"));
  window.Select(first, true);
  original_page->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return original_page->GetRenderWidgetHostView()->HasFocus() &&
           content::EvalJs(original_page, "document.hasFocus()",
                           content::EXECUTE_SCRIPT_NO_USER_GESTURE) == true;
  }));
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  auto& picker = window.workspace_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  ExpectVisible(original_page, true);
  EXPECT_TRUE(picker.search_for_testing()->HasFocus());
  auto* root = window.desktop_widget()->GetNativeWindow()->GetRootWindow();
  auto* activation = wm::GetActivationClient(root);
  auto* focus = aura::client::GetFocusClient(root);
  auto* active_window = activation ? activation->GetActiveWindow() : nullptr;
  auto* focused_window = focus ? focus->GetFocusedWindow() : nullptr;
  EXPECT_EQ(false, content::EvalJs(original_page, R"(
    new Promise(resolve => setTimeout(() => resolve(document.hasFocus()), 0))
  )", content::EXECUTE_SCRIPT_NO_USER_GESTURE))
      << "active=" << (active_window ? active_window->GetName() : "<none>")
      << " focused=" << (focused_window ? focused_window->GetName() : "<none>");
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_EQ(true, content::EvalJs(original_page, "document.hasFocus()",
                                 content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  keys.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  for (auto key : {ui::VKEY_T, ui::VKEY_R, ui::VKEY_A, ui::VKEY_B,
                   ui::VKEY_A, ui::VKEY_J, ui::VKEY_O})
    keys.PressKey(key, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.search_for_testing()->GetText() == u"trabajo" &&
           picker.result_count_for_testing() == 1;
  }));
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           window.selected_browser_for_testing() == second;
  }));
  ExpectVisible(original_page, false);
  EXPECT_EQ(original_page, Active(first));

  window.Select(first, true);
  window.ToggleWorkspacePicker();
  ASSERT_TRUE(picker.active());
  picker.search_for_testing()->SetText(u"Trabajo");
  picker.ContentsChanged(picker.search_for_testing(), u"Trabajo");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(picker.HandleOverlayKey(enter));
  window.Select(second, true);
  window.Select(first, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_EQ(first, window.selected_browser_for_testing());

  ASSERT_TRUE(window.RunCommand(first, "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() != first;
  }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  window.Select(second, true);
  window.ToggleWorkspacePicker();
  ASSERT_TRUE(picker.active());
  picker.search_for_testing()->SetText(u"Private");
  picker.ContentsChanged(picker.search_for_testing(), u"Private");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           window.selected_browser_for_testing() == private_browser;
  }));
  EXPECT_EQ(catalog->items.size(), service.Read()->items.size());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       EmptyDesktopWorkspacePickerCreatesWithoutPlaceholderTabs) {
  auto& window = FlyWindowController::Get();
  auto original = browser()->GetWeakPtr();
  const auto original_directory = browser()->GetProfile()->GetPath();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !original; }));
  ASSERT_EQ(nullptr, window.selected_browser_for_testing());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  auto& picker = window.workspace_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  EXPECT_EQ(nullptr, window.selected_browser_for_testing());
  picker.search_for_testing()->SetText(u"New workspace");
  picker.ContentsChanged(picker.search_for_testing(), u"New workspace");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  // Electron asks for the name first.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.workspace_name_dialog_for_testing() != nullptr; }));
  window.workspace_name_dialog_for_testing()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           window.selected_browser_for_testing() != nullptr;
  }));
  auto* created = window.selected_browser_for_testing();
  EXPECT_NE(original_directory, created->GetProfile()->GetPath());
  ASSERT_EQ(1, created->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(created)));
  EXPECT_EQ(GURL("about:blank"), Active(created)->GetLastCommittedURL());
  EXPECT_EQ(created->GetProfile(), Active(created)->GetBrowserContext());
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
  views::test::ButtonTestApi(window.new_tab_button_for_testing())
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != nullptr; }));
  EXPECT_EQ(1,
            window.selected_browser_for_testing()->GetTabStripModel()->count());
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    SharedHistoryQueriesNativeStoresAndExcludesPrivateVisits) {
  const auto first_url =
      embedded_test_server()->GetURL("/title1.html?history=first");
  const auto second_url =
      embedded_test_server()->GetURL("/title2.html?history=second");
  const auto private_url =
      embedded_test_server()->GetURL("/title1.html?history=private");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  ASSERT_TRUE(content::ExecJs(Active(browser()),
                              "document.cookie='history=retained';"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, second_url));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-private-mode"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing()
        ->GetProfile()
        ->IsOffTheRecord();
  }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser, private_url));
  WorkspaceService workspaces(*g_browser_process->local_state());
  HistoryService history(*g_browser_process->profile_manager(), workspaces);
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> query;
  history.Query(query.GetCallback());
  const auto result = query.Take();
  ASSERT_TRUE(result);
  auto find = [&](const GURL& url) {
    return std::ranges::find(*result, url, &HistoryEntry::url);
  };
  ASSERT_NE(result->end(), find(first_url));
  ASSERT_NE(result->end(), find(second_url));
  EXPECT_NE(find(first_url)->workspace_id, find(second_url)->workspace_id);
  EXPECT_EQ(result->end(), find(private_url));
  // An extension/native consumer in one profile still sees only that profile.
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<history::QueryResults> native_query;
  history::QueryOptions options;
  options.max_count = 500;
  HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                       ServiceAccessType::EXPLICIT_ACCESS)
      ->QueryHistory({}, options, native_query.GetCallback(), &tracker);
  const auto native = native_query.Take();
  EXPECT_TRUE(std::ranges::any_of(
      native, [&](const auto& row) { return row.url() == first_url; }));
  EXPECT_FALSE(std::ranges::any_of(native, [&](const auto& row) {
    return row.url() == second_url || row.url() == private_url;
  }));
  // Policy is checked in every profile before any history deletion starts.
  second->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kAllowDeletingBrowserHistory, false);
  base::test::TestFuture<bool> blocked;
  history.Clear(blocked.GetCallback());
  EXPECT_FALSE(blocked.Get());
  second->GetProfile()->GetPrefs()->SetBoolean(
      prefs::kAllowDeletingBrowserHistory, true);
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> retained;
  history.Query(retained.GetCallback());
  ASSERT_TRUE(retained.Get());
  EXPECT_EQ(result->size(), retained.Get()->size());
  base::test::TestFuture<bool> cleared;
  history.Clear(cleared.GetCallback());
  ASSERT_TRUE(cleared.Get());
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> empty;
  history.Query(empty.GetCallback());
  ASSERT_TRUE(empty.Get());
  EXPECT_TRUE(empty.Get()->empty());
  EXPECT_EQ("history=retained",
            content::EvalJs(Active(browser()), "document.cookie"));
  EXPECT_EQ(nullptr, g_browser_process->local_state()->GetUserPrefValue(
                         kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativeHistorySearchNavigatesInDestinationAndConfirmsClear) {
  const auto first_url =
      embedded_test_server()->GetURL("/title1.html?history=first");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title2.html?history=second")));
  auto* page = Active(second);
  ASSERT_TRUE(content::ExecJs(page, "document.cookie='history=destination';"));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(NativeCommandAvailable(second, "toggle-history"));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-history"));
  auto& picker = window.history_picker();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.search_for_testing()->HasFocus() &&
           picker.result_count_for_testing() >= 3;
  }));
  picker.search_for_testing()->SetText(u"history=first");
  picker.ContentsChanged(picker.search_for_testing(), u"history=first");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           page->GetLastCommittedURL() == first_url;
  }));
  EXPECT_EQ(second, window.selected_browser_for_testing());
  EXPECT_EQ("history=destination", content::EvalJs(page, "document.cookie"));
  auto open_clear = [&] {
    ASSERT_TRUE(InvokeNativeCommand(second, "toggle-history"));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return picker.active() && picker.result_count_for_testing() >= 3;
    }));
    picker.search_for_testing()->SetText(u"clear history");
    picker.ContentsChanged(picker.search_for_testing(), u"clear history");
    ASSERT_EQ(1u, picker.result_count_for_testing());
    keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window.history_clear_dialog_for_testing() != nullptr; }));
  };
  open_clear();
  auto* confirmation = window.history_clear_dialog_for_testing();
  ASSERT_TRUE(confirmation);
  EXPECT_EQ(static_cast<int>(ui::mojom::DialogButton::kCancel),
            confirmation->widget_delegate()
                ->AsDialogDelegate()
                ->GetDefaultDialogButton());
  confirmation->widget_delegate()->AsDialogDelegate()->CancelDialog();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.history_clear_dialog_for_testing(); }));
  open_clear();
  confirmation = window.history_clear_dialog_for_testing();
  ASSERT_TRUE(confirmation);
  confirmation->widget_delegate()->AsDialogDelegate()->AcceptDialog();
  WorkspaceService workspaces(*g_browser_process->local_state());
  HistoryService history(*g_browser_process->profile_manager(), workspaces);
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> empty;
  history.Query(empty.GetCallback());
  ASSERT_TRUE(empty.Get());
  EXPECT_TRUE(empty.Get()->empty());
  EXPECT_EQ("history=destination", content::EvalJs(page, "document.cookie"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeHistoryReflectsNativeDeletionsWhileOpen) {
  const auto first_url =
      embedded_test_server()->GetURL("/title1.html?history=deleted");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first_url));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title2.html?history=retained")));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-history"));
  auto& picker = window.history_picker();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.result_count_for_testing() == 3;
  }));
  HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                       ServiceAccessType::EXPLICIT_ACCESS)
      ->DeleteURLs({first_url});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.result_count_for_testing() == 2;
  }));
  picker.search_for_testing()->SetText(u"history=deleted");
  picker.ContentsChanged(picker.search_for_testing(), u"history=deleted");
  EXPECT_EQ(0u, picker.result_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeFrequentSitesNavigateHideAndHonorLiveChoices) {
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  ASSERT_TRUE(content::ExecJs(Active(browser()),
                              "document.cookie='frequent=retained';"));
  auto& window = FlyWindowController::Get();
  auto& picker = window.bookmark_picker();
  auto open_sites = [&] {
    ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return picker.active() && picker.search_for_testing()->HasFocus() &&
             picker.result_count_for_testing() == 1;
    }));
    picker.search_for_testing()->SetText(u"Frequent site");
    picker.ContentsChanged(picker.search_for_testing(), u"Frequent site");
    ASSERT_EQ(1u, picker.result_count_for_testing());
  };
  open_sites();
  ASSERT_TRUE(window.preferences().SetFrequentSitesVisible(false));
  EXPECT_EQ(0u, picker.result_count_for_testing());
  ASSERT_TRUE(window.preferences().SetFrequentSitesVisible(true));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.result_count_for_testing() == 1;
  }));
  ASSERT_TRUE(window.bookmarks().Add(url.spec(), "Saved visit"));
  EXPECT_EQ(0u, picker.result_count_for_testing());
  ASSERT_TRUE(window.bookmarks().Remove(url.spec()));
  EXPECT_EQ(1u, picker.result_count_for_testing());
  const int count = browser()->tab_strip_model()->count();
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           browser()->tab_strip_model()->count() == count + 1;
  }));
  auto* opened = Active(browser());
  // Frequent sites open the origin, which the fixture server answers with an
  // error page; only the committed destination and its profile matter here.
  content::WaitForLoadStop(opened);
  EXPECT_EQ(url::Origin::Create(url).GetURL(), opened->GetLastCommittedURL());
  EXPECT_EQ(browser()->GetProfile(), opened->GetBrowserContext());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  EXPECT_EQ("frequent=retained",
            content::EvalJs(Active(browser()), "document.cookie"));
  open_sites();
  ASSERT_TRUE(picker.remove_for_testing(0));
  views::test::ButtonTestApi(picker.remove_for_testing(0))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.result_count_for_testing() == 0;
  }));
  const auto* hidden = g_browser_process->local_state()
                           ->GetDict(kBookmarksPref)
                           .FindList("hidden_sites");
  ASSERT_TRUE(hidden);
  ASSERT_EQ(1u, hidden->size());
  EXPECT_EQ(url::Origin::Create(url).Serialize(), hidden->front().GetString());
  EXPECT_TRUE(window.bookmarks().Read()->empty());
  picker.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  FrequentSitesService restarted(*g_browser_process->local_state());
  const auto sites = restarted.Read({{url.spec(), base::Time::Now()}});
  ASSERT_TRUE(sites);
  EXPECT_TRUE(sites->empty());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeSettingsMenuEditsStartupAndSidebarSettings) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-menu-bar"));
  auto* menu = window.settings_menu().model_for_testing();
  ASSERT_TRUE(menu);
  EXPECT_GT(menu->GetItemCount(), 15u);
  EXPECT_FALSE(window.settings_menu().IsCommandIdChecked(1));
  window.settings_menu().ExecuteCommand(1, 0);
  EXPECT_EQ(true, window.preferences().ReadBool("disable_gpu"));
  window.settings_menu().ExecuteCommand(102, 0);
  EXPECT_EQ(2, window.preferences().ReadInt("process_limit"));
  EXPECT_TRUE(window.settings_menu().IsCommandIdChecked(102));
  window.settings_menu().ExecuteCommand(230, 0);
  EXPECT_EQ(30, window.preferences().ReadSuspendTimeoutMinutes());
  window.settings_menu().Cancel();
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeNotificationsToggleChangesEveryWorkspaceDefault) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto setting = [](BrowserWindowInterface* window) {
    return HostContentSettingsMapFactory::GetForProfile(window->GetProfile())
        ->GetDefaultContentSetting(ContentSettingsType::NOTIFICATIONS);
  };
  EXPECT_EQ(CONTENT_SETTING_ASK, setting(browser()));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-notifications"));
  EXPECT_EQ(false, FlyWindowController::Get().preferences().ReadBool(
                       "web_notifications"));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, setting(browser()));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, setting(second));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-notifications"));
  EXPECT_EQ(CONTENT_SETTING_ASK, setting(browser()));
  EXPECT_EQ(CONTENT_SETTING_ASK, setting(second));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeDownloadsAndDiagnosticsSurfacesOpenAndClose) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-downloads"));
  auto& picker = window.downloads_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  EXPECT_EQ(1u, picker.result_count_for_testing());
  picker.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  // Electron's Diagnostics & Health Hub: system, processes, log and report.
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-diagnostics"));
  auto* hub = window.diagnostics_hub_for_testing();
  ASSERT_TRUE(hub && hub->active());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return hub->has_snapshot_for_testing(); }));
  EXPECT_NE(std::u16string::npos,
            hub->tab_text_for_testing().find(u"Chromium Engine"));
  hub->SelectTabForTesting(1);
  EXPECT_NE(std::u16string::npos, hub->tab_text_for_testing().find(u"Renderer"));
  hub->SelectTabForTesting(2);
  EXPECT_NE(std::u16string::npos,
            hub->tab_text_for_testing().find(u"CURRENTLY DISABLED"));
  const auto report = hub->MarkdownReport();
  EXPECT_NE(std::string::npos, report.find("### Fly Browser Diagnostic Report"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-diagnostics"));
  EXPECT_FALSE(hub->active());
}

// Electron's dashboard: each workspace with its tabs (pinned marked) and
// rename/delete buttons; choosing a tab opens its workspace on that tab.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       DashboardListsWorkspaceTabsAndOpensThem) {
  auto& window = FlyWindowController::Get();
  auto* original = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      original, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      original, embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  original->GetTabStripModel()->SetTabPinned(1, true);
  window.CreateWorkspace("Second");
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected != original &&
           selected->GetTabStripModel()->count() == 1;
  }));
  const auto entries = window.dashboard_entries_for_testing();
  std::vector<std::string> titles;
  std::string pinned_action;
  for (const auto& entry : entries) {
    titles.push_back(entry.title);
    if (entry.title.find("\xF0\x9F\x93\x8C") != std::string::npos)
      pinned_action = entry.action;
  }
  ASSERT_GE(titles.size(), 5u);
  EXPECT_FALSE(titles[0].starts_with("- "));
  // Pinned tabs come first in the strip.
  EXPECT_EQ("- Title Of Awesomeness \xF0\x9F\x93\x8C", titles[1]);
  EXPECT_TRUE(titles[2].starts_with("- ") &&
              titles[2].find("title1.html") != std::string::npos)
      << titles[2];
  EXPECT_EQ("Second (Active)", titles[3]);
  EXPECT_EQ(entries[1].action, pinned_action);
  EXPECT_EQ(2u, entries[0].row_actions.size());
  EXPECT_EQ(1, entries[2].indent);
  window.choose_workspace_for_testing(entries[2].action);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() == original; }));
  EXPECT_EQ(1, original->GetTabStripModel()->active_index());
}

// Electron keeps a workspace whose last tab closed and shows its empty view;
// the view's New Tab card reopens that workspace.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       LastTabClosedShowsTheWorkspaceWelcomeView) {
  auto& window = FlyWindowController::Get();
  auto* original = browser();
  window.CreateWorkspace("Empty soon");
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected != original &&
           selected->GetTabStripModel()->count() == 1;
  }));
  auto second = window.selected_browser_for_testing()->GetWeakPtr();
  auto* second_profile = second->GetProfile();
  ASSERT_TRUE(ExecuteNativeCommand(second.get(), "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !second; }));
  EXPECT_EQ(nullptr, window.selected_browser_for_testing());
  auto* welcome = window.welcome_for_testing();
  ASSERT_TRUE(welcome);
  EXPECT_TRUE(welcome->GetVisible());
  EXPECT_EQ(u"Empty soon", welcome->workspace_for_testing());
  auto* new_tab = static_cast<views::Button*>(welcome->card_for_testing(0));
  ASSERT_TRUE(new_tab);
  views::test::ButtonTestApi(new_tab).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected->GetProfile() == second_profile;
  }));
  EXPECT_FALSE(welcome->GetVisible());
}

// Electron's siteZoom.js: zoom changes by 0.5 levels per step and one level
// per hostname is shared by every normal workspace, whatever path changed it.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SiteZoomUsesElectronStepsAndIsSharedByWorkspaces) {
  auto& window = FlyWindowController::Get();
  auto* original = browser();
  const GURL page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(original, page));
  const std::string host(page.host());
  const auto level = [&](BrowserWindowInterface* target) {
    return content::HostZoomMap::GetDefaultForBrowserContext(
               target->GetProfile())
        ->GetZoomLevelForHostAndScheme("http", host);
  };
  ASSERT_TRUE(InvokeNativeCommand(original, "zoom-in"));
  EXPECT_DOUBLE_EQ(0.5, level(original));
  ASSERT_TRUE(InvokeNativeCommand(original, "zoom-in"));
  EXPECT_DOUBLE_EQ(1.0, level(original));
  window.CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected != original &&
           selected->GetTabStripModel()->count() == 1;
  }));
  auto* second = window.selected_browser_for_testing();
  // A workspace created later takes the shared levels.
  EXPECT_DOUBLE_EQ(1.0, level(second));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, page));
  ASSERT_TRUE(InvokeNativeCommand(second, "zoom-out"));
  EXPECT_DOUBLE_EQ(0.5, level(second));
  EXPECT_DOUBLE_EQ(0.5, level(original));
  ASSERT_TRUE(InvokeNativeCommand(second, "reset-zoom"));
  EXPECT_DOUBLE_EQ(0.0, level(original));
}

// Electron Fly's reader (readerDistiller.js) opens over the article with its
// toolbar, lights the reader button, and closes on the second toggle and on
// Escape.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ElectronReaderViewOpensOverTheArticleAndCloses) {
  const GURL article =
      embedded_test_server()->GetURL("/dom_distiller/simple_article.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), article));
  auto* contents = Active(browser());
  const auto open = [&] {
    return content::EvalJs(contents,
                           "!!document.getElementById('fly-reader-overlay')")
        .ExtractBool();
  };
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-reader-view"));
  ASSERT_TRUE(base::test::RunUntil(open));
  EXPECT_EQ(article, contents->GetLastCommittedURL());
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.reader_button_for_testing()->selected(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-reader-view"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !open(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.reader_button_for_testing()->selected(); }));
  // Escape inside the reader closes it too; the button follows.
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-reader-view"));
  ASSERT_TRUE(base::test::RunUntil(open));
  content::SimulateKeyPress(contents, ui::DomKey::ESCAPE, ui::DomCode::ESCAPE,
                            ui::VKEY_ESCAPE, false, false, false, false);
  ASSERT_TRUE(base::test::RunUntil([&] { return !open(); }));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.reader_button_for_testing()->selected(); }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BundledBlockerFollowsWorkspaceRemovalAndGlobalSwitch) {
  constexpr char kId[] = "ddkjiahejlhfcafbddmgiahcphecmpfh";
  base::FilePath assets;
  ASSERT_TRUE(base::PathService::Get(base::DIR_ASSETS, &assets));
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!base::PathExists(assets.AppendASCII("fly_extensions").AppendASCII(kId)))
      GTEST_SKIP() << "bundled extensions are not staged in this build";
  }
  auto& window = FlyWindowController::Get();
  auto& extensions = window.extensions();
  auto loaded = [&](BrowserWindowInterface* browser) {
    return extensions::ExtensionRegistry::Get(browser->GetProfile())
               ->enabled_extensions()
               .GetByID(kId) != nullptr;
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded(browser()); }));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded(second); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.extension_actions_for_testing()->children().size() == 1u;
  }));
  // Switching it off in one workspace leaves the other.
  ASSERT_TRUE(extensions.SetExtensionEnabled(second->GetProfile(), kId, false));
  EXPECT_FALSE(loaded(second));
  EXPECT_TRUE(loaded(browser()));
  ASSERT_TRUE(extensions.SetExtensionEnabled(second->GetProfile(), kId, true));
  EXPECT_TRUE(loaded(second));
  // The global switch unloads it everywhere and restores it.
  ASSERT_TRUE(extensions.SetEnabled(false));
  EXPECT_FALSE(loaded(browser()));
  EXPECT_FALSE(loaded(second));
  ASSERT_TRUE(extensions.SetEnabled(true));
  EXPECT_TRUE(loaded(browser()));
  // Removal applies to every workspace and is listed no more.
  ASSERT_TRUE(extensions.Remove(browser()->GetProfile(), kId));
  EXPECT_FALSE(loaded(browser()));
  EXPECT_FALSE(loaded(second));
  EXPECT_TRUE(std::ranges::none_of(extensions.List(second->GetProfile()),
                                   [&](const FlyExtensionInfo& info) {
                                     return info.id == kId;
                                   }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       UserScriptsAndStylesRunWithPersistentGmValues) {
  base::FilePath user_data;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::CreateDirectory(user_data.AppendASCII("scripts")));
    ASSERT_TRUE(base::CreateDirectory(user_data.AppendASCII("styles")));
    ASSERT_TRUE(base::WriteFile(
        user_data.AppendASCII("scripts").AppendASCII("count.user.js"),
        "// ==UserScript==\n// @name Counter\n// @match http://*/*\n"
        "// @grant GM_getValue\n// @grant GM_setValue\n// ==/UserScript==\n"
        "GM_setValue('visits', GM_getValue('visits', 0) + 1);\n"
        "document.documentElement.dataset.flyVisits = GM_getValue('visits');\n"));
    ASSERT_TRUE(base::WriteFile(
        user_data.AppendASCII("styles").AppendASCII("color.user.css"),
        "/* ==UserStyle==\n@name Colour\n==/UserStyle== */\n"
        "@-moz-document domain(\"127.0.0.1\") {\n"
        "body { color: rgb(1, 2, 3) !important; }\n}\n"));
  }
  auto& window = FlyWindowController::Get();
  window.user_content().Reload();
  // A generation that started before both files existed can finish first.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.user_content().script_count() == 1u &&
           window.user_content().style_count() == 1u;
  }));
  const auto url = embedded_test_server()->GetURL("/title1.html");
  auto visits = [&] {
    return content::EvalJs(Active(browser()),
                           "document.documentElement.dataset.flyVisits || ''")
        .ExtractString();
  };
  auto color = [&] {
    return content::EvalJs(Active(browser()),
                           "getComputedStyle(document.body).color")
        .ExtractString();
  };
  // Registration is asynchronous in the extension worker; reload until both
  // injections appear.
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (!ui_test_utils::NavigateToURL(browser(), url))
      return false;
    return !visits().empty() && color() == "rgb(1, 2, 3)";
  }));
  const int first = std::stoi(visits());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  ASSERT_TRUE(base::test::RunUntil([&] { return !visits().empty(); }));
  EXPECT_EQ(first + 1, std::stoi(visits()));
}

class FlyGmRequestBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  void SetUpOnMainThread() override {
    embedded_test_server()->RegisterRequestMonitor(base::BindRepeating(
        &FlyGmRequestBrowserTest::Monitor, base::Unretained(this)));
    FlyWorkspaceBrowserTest::SetUpOnMainThread();
    host_resolver()->AddRule("*", "127.0.0.1");
  }

 protected:
  bool Requested(std::string_view url) {
    base::AutoLock lock(lock_);
    return std::ranges::find(urls_, url) != urls_.end();
  }

 private:
  void Monitor(const net::test_server::HttpRequest& request) {
    base::AutoLock lock(lock_);
    urls_.push_back(request.relative_url);
  }
  base::Lock lock_;
  std::vector<std::string> urls_ GUARDED_BY(lock_);
};

// Every redirect hop of GM_xmlhttpRequest is held to the script's @connect
// hosts and kept off local network addresses before it is sent, as Electron
// Fly checked each hop.
IN_PROC_BROWSER_TEST_F(FlyGmRequestBrowserTest, RedirectHopsAreCheckedBeforeSending) {
  base::FilePath user_data;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
  const auto script = [](std::string_view name, std::string_view connect,
                         std::string_view body) {
    return base::StrCat(
        {"// ==UserScript==\n// @name ", name,
         "\n// @match http://*/*\n// @grant GM_xmlhttpRequest\n// @connect ",
         connect,
         "\n// ==/UserScript==\n"
         "if (location.hostname === 'page.test') {\n"
         "  const base = 'http://allowed.test:' + location.port;\n"
         "  const send = (key, url) => GM_xmlhttpRequest({url,\n"
         "    onload: r => document.documentElement.dataset[key] = 'load ' + r.status,\n"
         "    onerror: e => document.documentElement.dataset[key] = 'error ' + e.error});\n",
         body, "}\n"});
  };
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    const auto scripts = user_data.AppendASCII("scripts");
    ASSERT_TRUE(base::CreateDirectory(scripts));
    ASSERT_TRUE(base::WriteFile(
        scripts.AppendASCII("connect.user.js"),
        script("Connect", "allowed.test",
               "  send('direct', base + '/echo?direct');\n"
               "  send('hop', base + '/server-redirect?http://other.test:' + "
               "location.port + '/echo?hop');\n")));
    ASSERT_TRUE(base::WriteFile(
        scripts.AppendASCII("open.user.js"),
        script("Open", "*",
               "  send('open', base + '/server-redirect?http://open.test:' + "
               "location.port + '/echo?open');\n"
               "  send('local', base + '/server-redirect?http://127.0.0.1:' + "
               "location.port + '/echo?local');\n")));
  }
  auto& window = FlyWindowController::Get();
  window.user_content().Reload();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.user_content().script_count() == 2u; }));
  const auto url = embedded_test_server()->GetURL("page.test", "/title1.html");
  const auto result = [&](std::string_view key) {
    return content::EvalJs(Active(browser()),
                           base::StrCat({"document.documentElement.dataset.",
                                         key, " || ''"}))
        .ExtractString();
  };
  ASSERT_TRUE(base::test::RunUntil([&] {
    if (result("direct").empty() &&
        !ui_test_utils::NavigateToURL(browser(), url)) {
      return false;
    }
    return !result("direct").empty() && !result("hop").empty() &&
           !result("open").empty() && !result("local").empty();
  }));
  EXPECT_EQ("load 200", result("direct"));
  EXPECT_EQ("load 200", result("open"));
  EXPECT_TRUE(result("hop").starts_with("error")) << result("hop");
  EXPECT_TRUE(result("local").starts_with("error")) << result("local");
  EXPECT_TRUE(Requested("/echo?direct"));
  EXPECT_TRUE(Requested("/echo?open"));
  EXPECT_FALSE(Requested("/echo?hop"));
  EXPECT_FALSE(Requested("/echo?local"));
}

class FlyLegacyImportBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    FlyWorkspaceBrowserTest::SetUpCommandLine(command_line);
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(legacy_.CreateUniqueTempDir());
    const auto write = [&](std::string_view name, std::string_view text) {
      const auto path = legacy_.GetPath().AppendASCII(name);
      ASSERT_TRUE(base::CreateDirectory(path.DirName()));
      ASSERT_TRUE(base::WriteFile(path, text));
    };
    write("session.json", R"({
      "current_workspace": "Work",
      "workspaces": {"Home": ["data:text/html,home"],
                     "Work": ["data:text/html,work-a", "data:text/html,work-b"]},
      "workspace_partitions": {"Home": "persist:workspace-1", "Work": "persist:work"},
      "tab_pinned": {"Work": [true, false]}})");
    write("Partitions/work/Preferences", "{}");
    write("config.json", R"({"show_clock": false})");
    write("site-zoom.json", R"({"zoom.example": 2})");
    command_line->AppendSwitchPath("fly-import-from", legacy_.GetPath());
  }

 private:
  base::ScopedTempDir legacy_;
};
IN_PROC_BROWSER_TEST_F(FlyLegacyImportBrowserTest,
                       ElectronWorkspacesTabsAndSettingsArriveOnFirstStart) {
  WorkspaceService workspaces(*g_browser_process->local_state());
  const auto catalog = workspaces.Read();
  ASSERT_TRUE(catalog);
  ASSERT_EQ(2u, catalog->items.size());
  EXPECT_EQ("Home", catalog->items[0].name);
  EXPECT_EQ("Work", catalog->items[1].name);
  EXPECT_EQ("Profile 1", browser()->GetProfile()->GetPath().BaseName().AsUTF8Unsafe());
  auto* model = browser()->GetTabStripModel();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->count() == 2 &&
           model->GetWebContentsAt(1)->GetVisibleURL() ==
               GURL("data:text/html,work-b");
  }));
  EXPECT_EQ(GURL("data:text/html,work-a"),
            model->GetWebContentsAt(0)->GetVisibleURL());
  EXPECT_TRUE(model->IsTabPinned(0));
  EXPECT_FALSE(model->IsTabPinned(1));
  EXPECT_EQ(2.0, content::HostZoomMap::GetDefaultForBrowserContext(
                     browser()->GetProfile())
                     ->GetZoomLevelForHostAndScheme("https", "zoom.example"));
  EXPECT_FALSE(FlyWindowController::Get().preferences().ReadClock()->visible);
}

class FlyHistoryRestoreBrowserTest : public FlyWorkspaceBrowserTest {};
IN_PROC_BROWSER_TEST_F(FlyHistoryRestoreBrowserTest,
                       PRE_SharedHistoryLoadsAnUnopenedWorkspaceAfterRestart) {
  FrequentSitesService sites(*g_browser_process->local_state());
  ASSERT_TRUE(sites.Hide("https://hidden.fixture.test/path"));
  auto* manager = g_browser_process->profile_manager();
  auto& profile = profiles::testing::CreateProfileSync(
      manager, manager->GenerateNextProfileDirectoryPath());
  WorkspaceService workspaces(*g_browser_process->local_state());
  ASSERT_TRUE(workspaces.Add("Dormant history fixture",
                             profile.GetPath().BaseName().AsUTF8Unsafe()));
  auto* native = HistoryServiceFactory::GetForProfile(
      &profile, ServiceAccessType::EXPLICIT_ACCESS);
  ASSERT_TRUE(native);
  const GURL url("https://history.fixture.test/dormant-workspace");
  native->AddPage(url, base::Time::Now(), history::SOURCE_BROWSED);
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<history::QueryResults> stored;
  native->QueryHistory({}, history::QueryOptions(), stored.GetCallback(),
                       &tracker);
  EXPECT_TRUE(std::ranges::any_of(
      stored.Get(), [&](const auto& row) { return row.url() == url; }));
}
IN_PROC_BROWSER_TEST_F(FlyHistoryRestoreBrowserTest,
                       SharedHistoryLoadsAnUnopenedWorkspaceAfterRestart) {
  FrequentSitesService sites(*g_browser_process->local_state());
  const auto hidden =
      sites.Read({{"https://hidden.fixture.test/another", base::Time::Now()}});
  ASSERT_TRUE(hidden);
  EXPECT_TRUE(hidden->empty());
  auto* manager = g_browser_process->profile_manager();
  WorkspaceService workspaces(*g_browser_process->local_state());
  const auto catalog = workspaces.Read();
  ASSERT_TRUE(catalog);
  const auto item = std::ranges::find(catalog->items, "Dormant history fixture",
                                      &Workspace::name);
  ASSERT_NE(catalog->items.end(), item);
  const auto path =
      manager->user_data_dir().AppendASCII(item->profile_directory);
  EXPECT_EQ(nullptr, manager->GetProfileByPath(path));
  const auto workspace_id = item->id;
  HistoryService history(*manager, workspaces);
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> loaded;
  history.Query(loaded.GetCallback());
  ASSERT_TRUE(loaded.Get());
  EXPECT_TRUE(std::ranges::any_of(*loaded.Get(), [&](const auto& row) {
    return row.url == GURL("https://history.fixture.test/dormant-workspace") &&
           row.workspace_id == workspace_id;
  }));
  // Querying history does not open a browser or change the selected workspace.
  EXPECT_EQ(browser(),
            FlyWindowController::Get().selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SharedHistoryRejectsChangedOrUnreadableAuthorization) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* state = g_browser_process->local_state();
  WorkspaceService workspaces(*state);
  HistoryService history(*g_browser_process->profile_manager(), workspaces);
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> pending;
  history.Query(pending.GetCallback());
  const auto future =
      base::DictValue().Set("schema", 2).Set("items", base::ListValue());
  state->SetDict(kWorkspacesPref, future.Clone());
  EXPECT_FALSE(pending.Get());
  base::test::TestFuture<std::optional<std::vector<HistoryEntry>>> unreadable;
  history.Query(unreadable.GetCallback());
  EXPECT_FALSE(unreadable.Get());
  base::test::TestFuture<bool> clear;
  history.Clear(clear.GetCallback());
  EXPECT_FALSE(clear.Get());
  EXPECT_EQ(future, state->GetDict(kWorkspacesPref));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativeSearchPreferencesSaveWithoutChangingKeyboardDraft) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  auto* binding = editor.field_for_testing("spawn-tab", false);
  ASSERT_TRUE(binding);
  binding->SetText(u"Ctrl+Y");
  editor.ContentsChanged(binding, u"Ctrl+Y");
  editor.ShowSearch();
  auto* engine = editor.search_setting_for_testing("engine");
  ASSERT_TRUE(engine && engine->IsDrawn());
  engine->SetText(u"https://example.test/?q=%s");
  views::test::ButtonTestApi(editor.search_save_for_testing())
      .NotifyDefaultMouseClick();
  SearchService search(*g_browser_process->local_state());
  ASSERT_TRUE(search.Read());
  EXPECT_EQ("https://example.test/?q=%s", search.Read()->engine);
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  editor.search_setting_for_testing("alias")->SetText(u"GH");
  editor.search_setting_for_testing("name")->SetText(u"Custom code search");
  editor.search_setting_for_testing("url")->SetText(
      u"https://example.test/code?q=%s");
  views::test::ButtonTestApi(editor.alias_save_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(GURL("https://example.test/code?q=c%2B%2B"),
            search.ResolveNavigation("gh c++"));
  editor.search_setting_for_testing("url")->SetText(u"javascript:alert(%s)");
  const auto valid =
      g_browser_process->local_state()->GetDict(kPreferencesPref).Clone();
  views::test::ButtonTestApi(editor.alias_save_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(valid, g_browser_process->local_state()->GetDict(kPreferencesPref));
  views::test::ButtonTestApi(editor.alias_remove_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(GURL("https://github.com/search?q=c%2B%2B"),
            search.ResolveNavigation("gh c++"));
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  const auto future =
      base::DictValue().Set("schema", 2).Set("values", base::DictValue());
  g_browser_process->local_state()->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(engine->GetEnabled());
  EXPECT_FALSE(editor.search_save_for_testing()->GetEnabled());
  EXPECT_FALSE(editor.alias_save_for_testing()->GetEnabled());
  EXPECT_FALSE(editor.alias_remove_for_testing()->GetEnabled());
  views::test::ButtonTestApi(editor.search_save_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
  EXPECT_EQ(u"Ctrl+Y", binding->GetText());
  editor.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !editor.search_for_testing(); }));
  EXPECT_EQ(nullptr, editor.search_setting_for_testing("engine"));
  EXPECT_EQ(nullptr, editor.alias_save_for_testing());
  EXPECT_EQ(nullptr, editor.alias_remove_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeAddressAliasesNavigateWithinSelectedProfile) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(second);
  ASSERT_TRUE(content::ExecJs(page, "document.cookie='address=destination';"));
  SearchService search(*g_browser_process->local_state());
  const auto pattern =
      embedded_test_server()->GetURL("/title2.html?q=%s").spec();
  ASSERT_TRUE(search.SetAlias("fixture", "Local fixture", pattern));
  const auto saved =
      g_browser_process->local_state()->GetDict(kPreferencesPref).Clone();
  const auto id = TabIdentity::Get(page);
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-address"));
  auto& address = window.address_picker();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return address.active() && address.search_for_testing()->HasFocus();
  }));
  address.search_for_testing()->SetText(u"fixture c++ & rust");
  address.ContentsChanged(address.search_for_testing(), u"fixture c++ & rust");
  ASSERT_EQ(1u, address.result_count_for_testing());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return page->GetLastCommittedURL() ==
           embedded_test_server()->GetURL(
               "/title2.html?q=c%2B%2B%20%26%20rust");
  }));
  EXPECT_EQ(page, Active(second));
  EXPECT_EQ(id, TabIdentity::Get(page));
  EXPECT_EQ("address=destination", content::EvalJs(page, "document.cookie"));
  EXPECT_EQ(saved, g_browser_process->local_state()->GetDict(kPreferencesPref));
  EXPECT_EQ(second, window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativeAddressPrivateNavigationKeepsNormalStateUntouched) {
  auto& window = FlyWindowController::Get();
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* normal = Active(browser());
  ASSERT_TRUE(
      content::ExecJs(normal, "document.cookie='normal=retained;path=/';"));
  const auto preferences =
      g_browser_process->local_state()->GetDict(kPreferencesPref).Clone();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.selected_browser_for_testing() != browser(); }));
  auto* private_browser = window.selected_browser_for_testing();
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  auto* page = Active(private_browser);
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-address"));
  auto& address = window.address_picker();
  ASSERT_TRUE(address.active());
  const auto input = base::UTF8ToUTF16(url.spec());
  address.search_for_testing()->SetText(input);
  address.ContentsChanged(address.search_for_testing(), input);
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return page->GetLastCommittedURL() == url; }));
  EXPECT_EQ("", content::EvalJs(page, "document.cookie"));
  EXPECT_EQ("normal=retained", content::EvalJs(normal, "document.cookie"));
  EXPECT_EQ(url, normal->GetLastCommittedURL());
  EXPECT_EQ(preferences,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
  std::map<std::string, std::string> metadata;
  TabIdentity::Populate(page, &metadata);
  EXPECT_TRUE(metadata.empty());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeAddressWorkspaceAndBookmarkChoicesUseNativeState) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(content::ExecJs(page, "document.cookie='address=original';"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title2.html?workspace=second")));
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  const auto directory =
      browser()->GetProfile()->GetPath().BaseName().AsUTF8Unsafe();
  const auto item = std::ranges::find(catalog->items, directory,
                                      &Workspace::profile_directory);
  ASSERT_NE(item, catalog->items.end());
  ASSERT_TRUE(service.Rename(item->id, "Address workspace fixture"));
  auto& window = FlyWindowController::Get();
  auto& address = window.address_picker();
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return address.active() && address.search_for_testing()->HasFocus();
  }));
  address.search_for_testing()->SetText(u"ws:Address workspace fixture");
  address.ContentsChanged(address.search_for_testing(),
                          u"ws:Address workspace fixture");
  ASSERT_EQ(1u, address.result_count_for_testing());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.selected_browser_for_testing() == browser() &&
           !address.search_for_testing();
  }));
  const auto target = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(
      window.bookmarks().Add(target.spec(), "Address bookmark fixture"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return address.active() && address.search_for_testing()->HasFocus();
  }));
  address.search_for_testing()->SetText(u"Address bookmark fixture");
  address.ContentsChanged(address.search_for_testing(),
                          u"Address bookmark fixture");
  ASSERT_EQ(2u, address.result_count_for_testing());
  keys.PressKey(ui::VKEY_DOWN, ui::EF_NONE);
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return page->GetLastCommittedURL() == target; }));
  EXPECT_EQ(page, Active(browser()));
  EXPECT_EQ("address=original", content::EvalJs(page, "document.cookie"));
  EXPECT_NE(target, Active(second)->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeAddressCancelAndWorkspaceChangeDiscardInput) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(
      content::ExecJs(page,
                      "document.body.innerHTML='<input "
                      "id=entry>';document.querySelector('#entry').focus();"));
  auto& window = FlyWindowController::Get();
  const auto original = page->GetLastCommittedURL();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  auto& address = window.address_picker();
  ASSERT_TRUE(address.active());
  address.search_for_testing()->SetText(u"never-persist-this-query");
  address.ContentsChanged(address.search_for_testing(),
                          u"never-persist-this-query");
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !address.search_for_testing() &&
           content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  EXPECT_EQ(original, page->GetLastCommittedURL());
  EXPECT_EQ(nullptr, g_browser_process->local_state()->GetUserPrefValue(
                         kPreferencesPref));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(address.active());
  address.search_for_testing()->SetText(u"1+2*3");
  address.ContentsChanged(address.search_for_testing(), u"1+2*3");
  EXPECT_EQ(2u, address.result_count_for_testing());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !address.search_for_testing(); }));
  EXPECT_EQ(original, page->GetLastCommittedURL());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(SecondWorkspace());
  ASSERT_TRUE(base::test::RunUntil([&] { return !address.active(); }));
  EXPECT_EQ(original, page->GetLastCommittedURL());
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
  // Preferences opens on General, as Electron's.
  editor.ShowKeyboard();
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

// Electron's Preferences: General first, the settings search filters every
// section and jumps to the one with matches.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PreferencesFollowElectronSectionsAndSearch) {
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  EXPECT_EQ(KeyboardEditor::kGeneralPane, editor.selected_pane_for_testing());
  auto* search = editor.settings_search_for_testing();
  ASSERT_TRUE(search);
  search->SetText(u"cookies");
  editor.ContentsChanged(search, u"cookies");
  EXPECT_EQ(KeyboardEditor::kPrivacyPane, editor.selected_pane_for_testing());
  EXPECT_TRUE(editor.setting_visible_for_testing(u"Clear site cookies"));
  EXPECT_FALSE(editor.setting_visible_for_testing(u"Interface scale"));
  search->SetText(u"");
  editor.ContentsChanged(search, u"");
  EXPECT_TRUE(editor.setting_visible_for_testing(u"Interface scale"));
  // The section shown last comes back next time.
  editor.ShowPerformance();
  editor.Cancel();
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.active(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  EXPECT_EQ(KeyboardEditor::kPerformancePane,
            editor.selected_pane_for_testing());
}

// Electron's Command Centre: count, category chips, inspector card,
// recently used commands and the terminal fallback.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       CommandCentreFiltersInspectsAndRemembersCommands) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = FlyWindowController::Get();
  window.Select(browser(), true);
  auto& palette = window.palette();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-palette"));
  ASSERT_TRUE(base::test::RunUntil([&] { return palette.active(); }));
  EXPECT_TRUE(palette.count_for_testing().ends_with(u" commands"));
  EXPECT_EQ(u"New Blank Tab", palette.inspector_title_for_testing());
  palette.SetCategoryForTesting("Split View");
  EXPECT_EQ(u"6 commands", palette.count_for_testing());
  palette.SetCategoryForTesting("All");
  palette.search_for_testing()->SetText(u"zz-no-command");
  palette.ContentsChanged(palette.search_for_testing(), u"zz-no-command");
  EXPECT_EQ(u"1 match", palette.count_for_testing());
  EXPECT_EQ("terminal:zz-no-command", palette.selected_action_for_testing());
  EXPECT_EQ(u"Terminal Oversight", palette.inspector_title_for_testing());
  palette.search_for_testing()->SetText(u"reader");
  palette.ContentsChanged(palette.search_for_testing(), u"reader");
  ASSERT_EQ("toggle-reader-view", palette.selected_action_for_testing());
  ui::test::EventGenerator keys(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !palette.active(); }));
  EXPECT_EQ(std::vector<std::string>{"toggle-reader-view"},
            window.preferences().ReadRecentCommands());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-palette"));
  ASSERT_TRUE(base::test::RunUntil([&] { return palette.active(); }));
  // Recently used commands come first.
  EXPECT_EQ("toggle-reader-view", palette.selected_action_for_testing());
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
  // Electron's Command Centre lists New Blank Tab (not Duplicate Tab).
  for (auto key : {ui::VKEY_N, ui::VKEY_E, ui::VKEY_W, ui::VKEY_SPACE,
                   ui::VKEY_B, ui::VKEY_L, ui::VKEY_A, ui::VKEY_N, ui::VKEY_K})
    keys.PressKey(key, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.palette().search_for_testing()->GetText() == u"new blank" &&
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
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  ASSERT_TRUE(SecondWorkspace());
  ASSERT_TRUE(base::test::RunUntil([&] { return !window.palette().active(); }));
  EXPECT_NE(browser(), window.selected_browser_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativePointerFocusTypesAndReturnsFromWorkspaceOverlay) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = FlyWindowController::Get();
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<input id="native-edit" '
      + 'style="position:fixed;left:40px;top:40px;width:250px;height:24px">';
  )js"));
  window.Select(browser(), true);
  window.clock_for_testing()->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.clock_for_testing()->HasFocus();
  }));
  ui::test::EventGenerator input(
      window.desktop_widget()->GetNativeWindow()->GetRootWindow());
  const auto bounds = contents->GetRenderWidgetHostView()->GetViewBounds();
  const auto click_point = bounds.origin() + gfx::Vector2d(70, 52);
  auto native_point = click_point;
  auto* root = window.desktop_widget()->GetNativeWindow()->GetRootWindow();
  wm::ConvertPointFromScreen(root, &native_point);
  auto* hit = root->GetEventHandlerForPoint(native_point);
  EXPECT_TRUE(contents->GetNativeView()->Contains(hit))
      << "hit=" << (hit ? hit->GetName() : "none") << " sidebar="
      << window.sidebar_widget_for_testing()
             ->GetWindowBoundsInScreen()
             .ToString();
  input.MoveMouseTo(click_point);
  input.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetRenderWidgetHostView()->HasFocus() &&
           content::EvalJs(contents,
               "document.hasFocus() && document.activeElement.id === 'native-edit'",
               content::EXECUTE_SCRIPT_NO_USER_GESTURE) == true;
  })) << "render_view=" << bounds.ToString()
      << " desktop=" << window.desktop_widget()->GetWindowBoundsInScreen().ToString();
  input.PressKey(ui::VKEY_A, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelector('#native-edit').value",
        content::EXECUTE_SCRIPT_NO_USER_GESTURE) == "a";
  }));
  input.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_picker().active() &&
           window.workspace_picker().search_for_testing()->HasFocus();
  }));
  EXPECT_EQ(false, content::EvalJs(contents, "document.hasFocus()",
                                  content::EXECUTE_SCRIPT_NO_USER_GESTURE));
  EXPECT_EQ(false, content::EvalJs(contents, "document.hidden"));
  input.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_picker().search_for_testing() &&
           content::EvalJs(contents, "document.hasFocus()",
                           content::EXECUTE_SCRIPT_NO_USER_GESTURE) == true;
  }));
  input.PressKey(ui::VKEY_B, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelector('#native-edit').value",
        content::EXECUTE_SCRIPT_NO_USER_GESTURE) == "ab";
  }));
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
    return text.find(u"example.invalid/fixture") != std::u16string::npos;
  }));
  pointer.MoveMouseTo(window.workspace_buttons_for_testing()->GetBoundsInScreen().CenterPoint());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return status->location_for_testing()->GetText().empty();
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
    return status->security_for_testing()->GetText() == u"HTTPS" &&
           status->security_for_testing()->GetTooltipText().starts_with(
               u"Page secure");
  }));
  {
    chrome_security_state::ScopedMaliciousContentStatusForTesting verdict(
        security_state::MALICIOUS_CONTENT_STATUS_MALWARE);
    status->DidChangeVisibleSecurityState();
    EXPECT_TRUE(status->security_for_testing()->GetTooltipText().starts_with(
        u"Page security warning"));
  }
  status->DidChangeVisibleSecurityState();
  EXPECT_TRUE(status->security_for_testing()->GetTooltipText().starts_with(
      u"Page secure"));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  EXPECT_EQ(u"BLANK", status->security_for_testing()->GetText());
  ASSERT_TRUE(window.RunCommand(browser(), "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = window.selected_browser_for_testing();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  EXPECT_EQ(u"PRIVATE PROFILE", status->workspace_for_testing()->GetText());
  EXPECT_FALSE(status->security_for_testing()->GetTooltipText().starts_with(
      u"Page secure"));
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
    const auto desktop_mode =
        window.desktop_widget()->GetColorProviderKeyForTesting().color_mode;
    EXPECT_EQ(desktop_mode, window.sidebar_widget_for_testing()
                                ->GetColorProviderKeyForTesting()
                                .color_mode);
    return desktop_mode;
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
         EXPECT_EQ(13u, menu->GetItemCount());
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
  // Electron's zoom step (factor 1.095) leaves fractional device pixels, so
  // offsets may round one pixel either way.
  auto wait_offset = [&](const char* axis, int offset) {
    return base::test::RunUntil([&] {
      return std::abs(content::EvalJs(
                          contents,
                          content::JsReplace(
                              "Math.round(document.querySelector('#box')[$1])",
                              axis))
                          .ExtractInt() -
                      offset) <= 1;
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
