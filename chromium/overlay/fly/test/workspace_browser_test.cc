// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "chrome/browser/bookmarks/bookmark_model_factory.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "chrome/browser/history/history_service_factory.h"
#include "components/history/core/test/history_service_test_util.h"
#include "content/public/test/prerender_test_util.h"
#include "ui/base/page_transition_types.h"
#include "ui/color/color_id.h"
#include "ui/color/color_provider.h"
#include "chrome/browser/lifetime/application_lifetime_desktop.h"
#include "chrome/browser/ssl/chrome_security_state_util.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface_iterator.h"
#include "chrome/browser/sessions/exit_type_service.h"
#include "chrome/browser/sessions/session_service.h"
#include "chrome/browser/sessions/session_service_factory.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "chrome/browser/ui/views/frame/browser_view.h"
#include "chrome/browser/ui/views/location_bar/location_bar_view.h"
#include "chrome/browser/ui/views/toolbar/toolbar_view.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_action.h"
#include "chrome/browser/ui/views/session_crashed_bubble_view.h"
#include "chrome/common/chrome_paths.h"
#include "components/bookmarks/browser/bookmark_model.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/download/public/common/download_item.h"
#include "components/history/core/browser/history_service.h"
#include "components/history/core/common/pref_names.h"
#include "components/password_manager/core/common/password_manager_pref_names.h"
#include "components/signin/public/base/signin_pref_names.h"
#include "components/spellcheck/browser/pref_names.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/host_zoom_map.h"
#include "crypto/sha2.h"
#include "extensions/browser/extension_registry.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/bookmarks/frequent_sites_service.h"
#include "fly/preferences/search_service.h"
#include "fly/sessions/tab_identity.h"
#include "fly/ui/command_palette.h"
#include "fly/ui/download_shelf.h"
#include "fly/ui/fly_actions.h"
#include "fly/ui/fly_extensions.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/hint_controller.h"
#include "fly/ui/history_service.h"
#include "fly/ui/keyboard_editor.h"
#include "fly/ui/native_commands.h"
#include "fly/ui/new_tab_ui.h"
#include "fly/ui/sidebar_panel.h"
#include "fly/ui/status_bar.h"
#include "fly/ui/startup_maintenance.h"
#include "fly/ui/tab_sidebar.h"
#include "fly/ui/user_content.h"
#include "third_party/zlib/google/zip.h"
#include "ui/aura/window_tree_host.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animator.h"
#include "ui/gfx/x/connection.h"
#include "ui/gfx/x/xproto.h"
#include "ui/ozone/public/ozone_platform.h"
#include "ui/views/accessibility/view_accessibility.h"
#include "ui/views/controls/button/checkbox.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/tabbed_pane/tabbed_pane.h"
#include "ui/views/controls/textfield/textfield.h"
#include "url/origin.h"

#include <algorithm>
#include <map>
#include <set>

#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/field_trial_params.h"
#include "base/run_loop.h"
#include "base/synchronization/lock.h"
#include "base/thread_annotations.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "components/services/storage/dom_storage/features.h"
#include "content/public/common/content_features.h"
#include "net/base/network_anonymization_key.h"
#include "net/http/http_cache.h"
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
#include "content/public/browser/browser_accessibility_state.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_client.h"
#include "content/public/browser/favicon_status.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/scoped_accessibility_mode.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/content_features.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "extensions/browser/background_script_executor.h"
#include "extensions/test/test_extension_dir.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/http_request.h"
#include "ui/accessibility/ax_mode.h"
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
 protected:
  FlyApp& App() { return FlyApp::Get(); }
  // The Fly interface of a browser window (the test's own by default).
  FlyWindowController& Window(BrowserWindowInterface* model = nullptr) {
    auto* window = FlyWindowController::FromBrowser(model ? model : browser());
    CHECK(window);
    return *window;
  }
  bool IsActive(BrowserWindowInterface* model) { return LastActive() == model; }
  // The Fly window used last.
  BrowserWindowInterface* LastActive() {
    const auto browsers = FlyApp::Get().BrowsersByActivation();
    return browsers.empty() ? nullptr : browsers.front();
  }
  // The Fly window opened last.
  BrowserWindowInterface* Newest() {
    BrowserWindowInterface* newest = nullptr;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          if (FlyWindowController::FromBrowser(model) &&
              !model->IsDeleteScheduled())
            newest = model;
          return true;
        },
        BrowserCollection::Order::kCreation);
    return newest;
  }
  // Activates a window as the window manager would, and waits until it has
  // the keyboard.
  void Activate(BrowserWindowInterface* model) {
    model->GetWindow()->Show();
    model->GetWindow()->Activate();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return LastActive() == model && model->GetWindow()->IsActive();
    }));
  }
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
      Activate(second);
    }
    return second;
  }
  void ChooseMoveDestination(BrowserWindowInterface* source,
                             const std::string& name,
                             bool pointer = false) {
    auto& window = Window(source);
    Activate(source);
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
          window.widget()->GetNativeWindow()->GetRootWindow());
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
        window.widget()->GetNativeWindow()->GetRootWindow());
    keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
    keys.ReleaseKey(ui::VKEY_RETURN, ui::EF_NONE);
  }
  void OpenWorkspaceRemoval(const Workspace& item) {
    auto& window = Window();
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
        window.widget()->GetNativeWindow()->GetRootWindow());
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
  // Preferences builds one section at a time: look in the shown section,
  // then in each of the others.
  void ClickPreferencesButton(std::u16string_view text) {
    auto& editor = Window().keyboard_editor();
    auto find = [&](auto&& self, views::View* view) -> views::MdTextButton* {
      if (auto* button = views::AsViewClass<views::MdTextButton>(view);
          button && button->GetText() == text)
        return button;
      for (views::View* child : view->children()) {
        if (auto* found = self(self, child))
          return found;
      }
      return nullptr;
    };
    for (int pane = -1; pane < KeyboardEditor::kPaneCount; ++pane) {
      if (pane >= 0)
        editor.ShowPane(static_cast<KeyboardEditor::Pane>(pane));
      auto* root = editor.root_for_testing();
      ASSERT_TRUE(root);
      if (auto* button = find(find, root)) {
        views::test::ButtonTestApi(button).NotifyDefaultMouseClick();
        return;
      }
    }
    FAIL() << base::UTF16ToUTF8(text);
  }
  base::FilePath RestoreFixturePath() {
    return g_browser_process->profile_manager()->user_data_dir().AppendASCII(
        "fly-native-session-test.json");
  }
  base::DictValue SessionSnapshot() {
    base::ListValue windows;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          if (!FlyWindowController::ShouldDecorate(model) ||
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

class FlyStartupMaintenanceBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  FlyStartupMaintenanceBrowserTest() {
    EnablePixelOutput();
    UseSoftwareCompositing();
  }
  void SetUpOnMainThread() override {
    FlyWorkspaceBrowserTest::SetUpOnMainThread();
    response_ = std::make_unique<net::test_server::ControllableHttpResponse>(
        &server_, "/waiting");
    ASSERT_TRUE(server_.Start());
    Activate(browser());
  }

 protected:
  void NavigateUnpainted() {
    auto* contents = browser()->GetTabStripModel()->GetActiveWebContents();
    const auto url = server_.GetURL("/waiting");
    contents->GetController().LoadURL(url, content::Referrer(),
                                      ui::PAGE_TRANSITION_TYPED, "");
    response_->WaitForRequest();
    response_->Send(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!doctype html><body>" +
        std::string(4096, ' '));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return contents->GetLastCommittedURL() == url; }));
    ASSERT_FALSE(contents->CompletedFirstVisuallyNonEmptyPaint());
  }
  void SelectWindow(StartupMaintenance& maintenance) {
    maintenance.SetWindow(browser()->GetTabStripModel(), Window().widget());
  }
  void Wait(base::TimeDelta delay) {
    base::RunLoop loop;
    base::OneShotTimer timer;
    timer.Start(FROM_HERE, delay, loop.QuitClosure());
    loop.Run();
  }
  net::EmbeddedTestServer server_;
  std::unique_ptr<net::test_server::ControllableHttpResponse> response_;
};

IN_PROC_BROWSER_TEST_F(FlyStartupMaintenanceBrowserTest,
                       RunsAfterPresentedContentWithoutWaitingForLoad) {
  NavigateUnpainted();
  base::test::TestFuture<void> tasks;
  StartupMaintenance maintenance(tasks.GetCallback(), base::Seconds(30));
  SelectWindow(maintenance);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(tasks.IsReady());
  const auto painted_at = base::TimeTicks::Now();
  response_->Send("<h1>The page is visible</h1>");
  ASSERT_TRUE(tasks.Wait());
  EXPECT_LT(base::TimeTicks::Now() - painted_at, base::Seconds(5));
  EXPECT_TRUE(
      browser()->GetTabStripModel()->GetActiveWebContents()->IsLoading());
  // Further window/tab notifications cannot run the once-only work again.
  SelectWindow(maintenance);
  response_->Done();
}

IN_PROC_BROWSER_TEST_F(FlyStartupMaintenanceBrowserTest,
                       UnpaintedPageHasBoundedFallback) {
  NavigateUnpainted();
  base::test::TestFuture<void> tasks;
  StartupMaintenance maintenance(tasks.GetCallback(), base::Milliseconds(100));
  SelectWindow(maintenance);
  ASSERT_TRUE(tasks.Wait());
  EXPECT_FALSE(browser()
                   ->GetTabStripModel()
                   ->GetActiveWebContents()
                   ->CompletedFirstVisuallyNonEmptyPaint());
  response_->Done();
}

IN_PROC_BROWSER_TEST_F(FlyStartupMaintenanceBrowserTest,
                       SwitchingToAlreadyPaintedTabReleasesWork) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* painted = browser()->GetTabStripModel()->GetActiveWebContents();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return painted->CompletedFirstVisuallyNonEmptyPaint(); }));
  chrome::NewTab(browser(), NewTabTypes::kNewTabCommand);
  NavigateUnpainted();
  base::test::TestFuture<void> tasks;
  StartupMaintenance maintenance(tasks.GetCallback(), base::Seconds(30));
  SelectWindow(maintenance);
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(tasks.IsReady());
  browser()->GetTabStripModel()->ActivateTabAt(0);
  ASSERT_TRUE(tasks.Wait());
  EXPECT_EQ(painted, browser()->GetTabStripModel()->GetActiveWebContents());
  response_->Done();
}

IN_PROC_BROWSER_TEST_F(FlyStartupMaintenanceBrowserTest,
                       CancelledQuitResumesExpiredWork) {
  NavigateUnpainted();
  base::test::TestFuture<void> tasks;
  StartupMaintenance maintenance(tasks.GetCallback(), base::Milliseconds(100));
  SelectWindow(maintenance);
  maintenance.SetPaused(true);
  Wait(base::Milliseconds(200));
  EXPECT_FALSE(tasks.IsReady());
  maintenance.SetPaused(false);
  EXPECT_TRUE(tasks.IsReady());
  response_->Done();
}

IN_PROC_BROWSER_TEST_F(FlyStartupMaintenanceBrowserTest,
                       DestructionCancelsDeadlineAndObservers) {
  NavigateUnpainted();
  base::test::TestFuture<void> tasks;
  auto maintenance = std::make_unique<StartupMaintenance>(
      tasks.GetCallback(), base::Milliseconds(100));
  SelectWindow(*maintenance);
  maintenance.reset();
  response_->Send("<h1>Paint after cancellation</h1>");
  response_->Done();
  Wait(base::Milliseconds(200));
  EXPECT_FALSE(tasks.IsReady());
}

// After a crash or forced exit Chromium anchored its restore bubble to the
// app menu, which a Fly window never draws, and ended the browser process on
// every following start. Native Fly restores the interrupted session instead,
// as the bubble's Restore button does, and then saves sessions again.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, PRE_StartsAfterUncleanExit) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  ExitTypeService::GetInstanceForProfile(browser()->GetProfile())
      ->SetWaitingForUserToAckCrashForTest(true);
}
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, StartsAfterUncleanExit) {
  auto* profile = browser()->GetProfile();
  EXPECT_EQ(ExitType::kCrashed, ExitTypeService::GetLastSessionExitType(profile));
  EXPECT_EQ(nullptr, SessionCrashedBubbleView::GetInstanceForTest());
  const auto restored = [&](std::string_view path) {
    auto* model = browser()->GetTabStripModel();
    for (int i = 0; i < model->count(); ++i) {
      if (model->GetWebContentsAt(i)->GetVisibleURL().path() == path)
        return true;
    }
    return false;
  };
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return restored("/title1.html") && restored("/title2.html"); }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return SessionServiceFactory::GetForProfile(profile)->is_saving_enabled();
  }));
  EXPECT_FALSE(ExitTypeService::GetInstanceForProfile(profile)
                   ->waiting_for_user_to_ack_crash());
}

// Native Fly runs Chromium's default features, not the experiments of
// testing/variations/fieldtrial_testing_config.json. Storage keeps the SQLite
// stages earlier releases wrote, Privacy Sandbox APIs stay off and the HTTP
// cache and connections stay partitioned by site.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       RunsChromiumDefaultsWithoutTestingExperiments) {
  EXPECT_FALSE(base::FieldTrialList::TrialExists("WebUIReloadButtonStudy"));
  EXPECT_FALSE(base::FieldTrialList::TrialExists("PartitionAllocMiracleObject"));
  EXPECT_EQ(storage::DomStorageSqliteRolloutStage::kUseSqliteForNewDatabases,
            storage::GetSqliteRolloutStage(/*in_memory=*/false));
  EXPECT_TRUE(base::FeatureList::IsEnabled(features::kIdbSqliteOnDiskRollout));
  EXPECT_EQ("UseSqliteForNewStores",
            base::GetFieldTrialParamValueByFeature(
                features::kIdbSqliteOnDiskRollout, "stage"));
  for (const char* feature :
       {"BrowsingTopics", "ConversionMeasurement", "Fledge",
        "AdInterestGroupAPI", "SharedStorageAPI"}) {
    SCOPED_TRACE(feature);
    EXPECT_TRUE(base::FeatureList::GetInstance()->IsFeatureOverriddenFromCommandLine(
        feature, base::FeatureList::OVERRIDE_DISABLE_FEATURE));
  }
  EXPECT_TRUE(net::HttpCache::IsSplitCacheEnabled());
  EXPECT_TRUE(net::NetworkAnonymizationKey::IsPartitioningEnabled());
}

// Every workspace opens its own OS window with its own Fly interface; native
// window and tab IDs stay Chromium's.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       EachWorkspaceHasItsOwnWindowAndNativeIds) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  EXPECT_NE(first->GetProfile(), second->GetProfile());
  EXPECT_NE(first->GetSessionID(), second->GetSessionID());
  EXPECT_NE(extensions::ExtensionTabUtil::GetWindowId(first),
            extensions::ExtensionTabUtil::GetWindowId(second));
  EXPECT_NE(first->GetWindow()->GetNativeWindow()->GetHost(),
            second->GetWindow()->GetNativeWindow()->GetHost());
  auto& first_window = Window(first);
  auto& second_window = Window(second);
  EXPECT_NE(&first_window, &second_window);
  for (auto* model : {first, second}) {
    auto& window = Window(model);
    EXPECT_EQ(model->GetWindow()->GetNativeWindow(),
              window.widget()->GetNativeWindow());
    EXPECT_TRUE(window.widget()->GetNativeWindow()->Contains(
        window.sidebar_widget_for_testing()->GetNativeWindow()));
    EXPECT_EQ(extensions::ExtensionTabUtil::GetWindowId(model),
              extensions::ExtensionTabUtil::GetWindowIdOfTab(Active(model)));
  }
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
  Activate(first);
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
  const auto url = embedded_test_server()->GetURL("/title1.html");
  Activate(first);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(first, url));
  ASSERT_TRUE(content::ExecJs(Active(first),
                              "document.cookie='normal=first;path=/'"));
  const auto catalog =
      g_browser_process->local_state()->GetDict(kWorkspacesPref).Clone();
  const auto private_of = [&](BrowserWindowInterface* normal) {
    auto* newest = Newest();
    return newest && newest->GetProfile()->IsOffTheRecord() &&
                   newest->GetProfile()->GetOriginalProfile() ==
                       normal->GetProfile()
               ? newest
               : nullptr;
  };
  ASSERT_TRUE(NativeCommandAvailable(first, "toggle-private-mode"));
  ASSERT_TRUE(Window(first).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] { return private_of(first); }));
  auto* private_first = private_of(first);
  // A private workspace is its own window too.
  EXPECT_NE(first->GetWindow()->GetNativeWindow()->GetHost(),
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
  // Toggling again from the normal window returns to the existing private one.
  Activate(first);
  ASSERT_TRUE(Window(first).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(private_first); }));
  Activate(second);
  ASSERT_TRUE(Window(second).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] { return private_of(second); }));
  auto* private_second = private_of(second);
  EXPECT_NE(private_first->GetProfile(), private_second->GetProfile());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_second, url));
  EXPECT_EQ("", content::EvalJs(Active(private_second), "document.cookie"));
  EXPECT_EQ(true, content::EvalJs(Active(private_second),
                                  "localStorage.getItem('private') === null"));
  ASSERT_TRUE(Window(private_second).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !second->GetProfile()->HasPrimaryOTRProfile();
  }));
  Activate(private_first);
  ASSERT_TRUE(Window(private_first).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !first->GetProfile()->HasPrimaryOTRProfile();
  }));
  EXPECT_EQ("normal=first", content::EvalJs(Active(first), "document.cookie"));
  Activate(first);
  ASSERT_TRUE(Window(first).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] { return private_of(first); }));
  auto* fresh = private_of(first);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(fresh, url));
  EXPECT_EQ("", content::EvalJs(Active(fresh), "document.cookie"));
  EXPECT_EQ(true, content::EvalJs(Active(fresh),
                                  "localStorage.getItem('private') === null"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ProductNewTabsShowFlysPageInPersistentAndPrivateProfiles) {
  auto* original = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      original, embedded_test_server()->GetURL("/title1.html")));
  auto& window = Window();
  const int count = original->GetTabStripModel()->count();
  // Like Chrome, a new tab opens with the address bar ready for typing.
  ASSERT_TRUE(InvokeNativeCommand(original, "spawn-tab"));
  ASSERT_EQ(count + 1, original->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(original)));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(window.address_picker().active());
  EXPECT_TRUE(window.address_picker().search_for_testing()->HasFocus());
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(original)->GetLastCommittedURL());
  EXPECT_EQ(original->GetProfile(), Active(original)->GetBrowserContext());

  window.CreateTab();
  ASSERT_EQ(count + 2, original->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(original)));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(window.address_picker().active());
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(original)->GetLastCommittedURL());
  ASSERT_TRUE(OpenNativeBlankTab(original, 1));
  auto* inserted = original->GetTabStripModel()->GetWebContentsAt(1);
  ASSERT_EQ(inserted, Active(original));
  ASSERT_TRUE(content::WaitForLoadStop(inserted));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(window.address_picker().active());
  EXPECT_EQ(GURL(kFlyNewTabURL), inserted->GetLastCommittedURL());

  ASSERT_TRUE(Window(original).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() != original;
  }));
  auto* private_browser = LastActive();
  ASSERT_TRUE(private_browser);
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(content::WaitForLoadStop(Active(private_browser)));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(Window(private_browser).address_picker().active());
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(private_browser)->GetLastCommittedURL());
  ASSERT_TRUE(InvokeNativeCommand(private_browser, "spawn-tab"));
  ASSERT_TRUE(content::WaitForLoadStop(Active(private_browser)));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(Window(private_browser).address_picker().active());
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(private_browser)->GetLastCommittedURL());
  EXPECT_EQ(private_browser->GetProfile(),
            Active(private_browser)->GetBrowserContext());

  Activate(original);
  App().CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected != original && selected != private_browser &&
           selected->GetTabStripModel()->count() == 1;
  }));
  auto* created = LastActive();
  ASSERT_FALSE(created->GetProfile()->IsOffTheRecord());
  EXPECT_NE(original->GetProfile(), created->GetProfile());
  ASSERT_TRUE(content::WaitForLoadStop(Active(created)));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(Window(created).address_picker().active());
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(created)->GetLastCommittedURL());

  // Splitting a one-tab workspace must create the second pane through the
  // same profile-bound blank navigation, without opening Chromium's NTP.
  ASSERT_TRUE(ExecuteNativeCommand(created, "toggle-split"));
  auto* split_model = created->GetTabStripModel();
  ASSERT_EQ(2, split_model->count());
  ASSERT_TRUE(split_model->GetSplitForTab(split_model->active_index()));
  for (int index = 0; index < split_model->count(); ++index) {
    auto* pane = split_model->GetWebContentsAt(index);
    ASSERT_TRUE(content::WaitForLoadStop(pane));
    base::RunLoop().RunUntilIdle();
    EXPECT_FALSE(Window(created).address_picker().active());
    EXPECT_EQ(GURL(kFlyNewTabURL), pane->GetLastCommittedURL());
    EXPECT_EQ(created->GetProfile(), pane->GetBrowserContext());
    ExpectVisible(pane, true);
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PrivateExitHonoursBeforeUnloadCancellation) {
  auto* normal = browser();
  ASSERT_TRUE(Window(normal).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() != normal;
  }));
  auto* private_model = LastActive();
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
  ASSERT_TRUE(Window(private_model).RunCommand("toggle-private-mode"));
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  ASSERT_TRUE(dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  EXPECT_EQ(private_model, LastActive());
  EXPECT_TRUE(normal->GetProfile()->HasPrimaryOTRProfile());
  ExpectVisible(contents, true);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null"));
  // Leaving brings the workspace's own window back, even from minimised.
  normal->GetWindow()->Minimize();
  ASSERT_TRUE(
      base::test::RunUntil([&] { return normal->GetWindow()->IsMinimized(); }));
  ASSERT_TRUE(Window(private_model).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !normal->GetProfile()->HasPrimaryOTRProfile() &&
           LastActive() == normal && !normal->GetWindow()->IsMinimized();
  }));
}

class FlySidebarBrowserTest : public FlyWorkspaceBrowserTest {
 public:
  void SetUpOnMainThread() override {
    FlyWorkspaceBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(
        FlyApp::Get().preferences().SetSidebarAutoCollapse(true));
    // Sidebar expansion never starts the clock when the footer is hidden.
    ASSERT_TRUE(
        FlyApp::Get().preferences().SetStatusBarVisible(false));
  }

 protected:
  void MovePointer(gfx::Point point) {
    ui::test::EventGenerator mouse(Window().widget()
                                       ->GetNativeWindow()
                                       ->GetRootWindow());
    mouse.MoveMouseTo(point);
  }
  void OpenByPointer() {
    auto& window = Window();
    const auto bounds =
        window.widget()->GetContentsView()->GetBoundsInScreen();
    MovePointer(gfx::Point(bounds.x() + 18, bounds.y() + 200));
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window.sidebar_expanded_for_testing(); }));
  }
  void MoveToPage() {
    auto& window = Window();
    auto* contents = Active(LastActive());
    contents->Focus();
    MovePointer(
        contents->GetRenderWidgetHostView()->GetViewBounds().CenterPoint());
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return !window.sidebar_expanded_for_testing(); }));
  }
  // Waits for the slide to end: until then the visible panel takes clicks.
  void WaitForSidebarMotion() {
    auto* root = Window().sidebar_root_for_testing();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !root->edge()->layer()->GetAnimator()->is_animating() &&
             !root->panel()->layer()->GetAnimator()->is_animating();
    }));
    base::RunLoop().RunUntilIdle();
  }
};

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       TopRowSeparatesWebNavigationFromWorkspace) {
  auto& window = Window();
  auto* panel = window.sidebar_root_for_testing()->panel();
  const auto& top = panel->top_for_testing();
  auto* separator = panel->top_separator_for_testing();
  const GURL first = embedded_test_server()->GetURL("/title1.html");
  const GURL second = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), first));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), second));

  for (const char* scale : {"normal", "large"}) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(App().preferences().SetString("interface_scale", scale));
    MoveToPage();
    WaitForSidebarMotion();
    views::test::RunScheduledLayout(top.header->GetWidget());
    EXPECT_EQ(1.f, top.badge->layer()->opacity());
    EXPECT_EQ(0.f, top.label->layer()->opacity());
    EXPECT_TRUE(top.header->bounds().Contains(top.badge->bounds()));

    OpenByPointer();
    WaitForSidebarMotion();
    views::test::RunScheduledLayout(top.header->GetWidget());
    EXPECT_EQ(0.f, top.badge->layer()->opacity());
    EXPECT_EQ(1.f, top.label->layer()->opacity());
    EXPECT_EQ(1.f, separator->layer()->opacity());
    EXPECT_EQ(FlyScaled(4), top.back->x());
    EXPECT_LT(top.back->x(), top.forward->x());
    EXPECT_LT(top.forward->bounds().right(), top.new_tab->x());
    EXPECT_LT(top.new_tab->bounds().right(), separator->x());
    EXPECT_LT(separator->bounds().right(), top.header->x());
    EXPECT_TRUE(top.header->bounds().Contains(top.label->bounds()));
    EXPECT_LE(top.header->bounds().right(), panel->width());
    auto* label = views::AsViewClass<views::Label>(top.label);
    EXPECT_EQ(label->GetText(), label->GetDisplayTextForTesting());
    EXPECT_EQ(top.back->GetNextFocusableView(), top.forward);
    EXPECT_EQ(top.forward->GetNextFocusableView(), top.new_tab);
    EXPECT_EQ(top.new_tab->GetNextFocusableView(), top.header);
    // Moving straight up within the rail reaches Back without crossing the
    // workspace target or the page, and therefore keeps the sidebar open.
    const auto back = top.back->GetBoundsInScreen();
    MovePointer(gfx::Point(back.x() + FlyScaled(14), back.CenterPoint().y()));
    EXPECT_TRUE(window.sidebar_expanded_for_testing());
    EXPECT_EQ(views::Button::STATE_HOVERED,
              views::AsViewClass<FlyButton>(top.back)->GetState());
  }

  ui::test::EventGenerator pointer(
      window.widget()->GetNativeWindow()->GetRootWindow());
  pointer.MoveMouseTo(top.back->GetBoundsInScreen().CenterPoint());
  pointer.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Active(browser())->GetLastCommittedURL() == first; }));
  pointer.MoveMouseTo(top.forward->GetBoundsInScreen().CenterPoint());
  pointer.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return Active(browser())->GetLastCommittedURL() == second; }));
  const int tabs = browser()->GetTabStripModel()->count();
  pointer.MoveMouseTo(top.new_tab->GetBoundsInScreen().CenterPoint());
  pointer.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return browser()->GetTabStripModel()->count() == tabs + 1; }));
  EXPECT_EQ(u"New tab", window.new_tab_button_for_testing()->GetTooltipText());
  EXPECT_FALSE(window.workspace_picker().active());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.address_picker().active(); }));
  window.address_picker().Cancel();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.address_picker().showing(); }));
  OpenByPointer();
  WaitForSidebarMotion();
  views::test::RunScheduledLayout(top.header->GetWidget());
  pointer.MoveMouseTo(top.header->GetBoundsInScreen().CenterPoint());
  pointer.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.workspace_picker().active(); }));
}

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
  auto& window = Window();
  MoveToPage();
  EXPECT_FALSE(window.clock_timer_running_for_testing());
  const int width = content::EvalJs(page, "innerWidth").ExtractInt();
  const auto page_bounds = page->GetRenderWidgetHostView()->GetViewBounds();
  EXPECT_EQ(true, content::EvalJs(page, "document.hasFocus()"));
  OpenByPointer();
  EXPECT_EQ(true, content::EvalJs(page, "document.hasFocus()"));
  EXPECT_FALSE(window.clock_timer_running_for_testing());
  EXPECT_EQ(width, content::EvalJs(page, "innerWidth"));
  EXPECT_EQ(page_bounds, page->GetRenderWidgetHostView()->GetViewBounds());
  ExpectVisible(page, true);
  const auto covered = page_bounds.origin() + gfx::Vector2d(100, 416);
  auto native_point = covered;
  auto* root = window.widget()->GetNativeWindow()->GetRootWindow();
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
  WaitForSidebarMotion();
  mouse.MoveMouseTo(covered);
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return content::EvalJs(page, "clicks").ExtractInt() == 1; }));
  EXPECT_EQ(width, content::EvalJs(page, "innerWidth"));
  EXPECT_EQ(0, content::EvalJs(page, "resizes"));
  EXPECT_EQ(0, content::EvalJs(page, "visibilityEvents.length"));
}

// Opening the sidebar over a row shows that row's hover look and close
// button; the other rows keep theirs. Hover switches without a fade, so no
// button repaints on every frame of the slide.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest, SlideUpdatesOnlyTheHoveredRow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* first = Active(browser());
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  auto* second = Active(browser());
  auto& window = Window();
  auto* list = window.tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return list->row_for_testing(first) && list->row_for_testing(second);
  }));
  MoveToPage();
  WaitForSidebarMotion();
  auto* row = list->row_for_testing(first);
  auto* select = static_cast<views::Button*>(row->children()[0].get());
  auto* close = row->children()[2].get();
  auto* other_close = list->row_for_testing(second)->children()[2].get();
  EXPECT_FALSE(select->GetAnimateOnStateChange());
  EXPECT_FALSE(close->GetVisible());
  EXPECT_TRUE(other_close->GetVisible());
  // The row reaches under the rail, so pointing at the rail opens the
  // sidebar with the pointer already on the row.
  const auto bounds = row->GetBoundsInScreen();
  MovePointer(gfx::Point(bounds.x() + FlyScaled(18), bounds.CenterPoint().y()));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.sidebar_expanded_for_testing(); }));
  EXPECT_TRUE(close->GetVisible());
  EXPECT_TRUE(other_close->GetVisible());
  WaitForSidebarMotion();
  EXPECT_TRUE(close->GetVisible());
  MoveToPage();
  WaitForSidebarMotion();
  EXPECT_FALSE(close->GetVisible());
  EXPECT_TRUE(other_close->GetVisible());
}

// Rows and badges are refreshed with unchanged text often (titles, loading,
// hovered links); an unchanged text must not repaint them.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest, UnchangedButtonTextKeepsBorder) {
  auto* list = Window().tab_sidebar_for_testing();
  auto* contents = Active(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return list->row_for_testing(contents); }));
  auto* select =
      static_cast<views::LabelButton*>(list->row_for_testing(contents)->children()[0].get());
  // MdTextButton::SetText() replaces the border, which repaints the button.
  const auto* border = select->GetBorder();
  select->SetText(std::u16string(select->GetText()));
  EXPECT_EQ(border, select->GetBorder());
  select->SetText(u"Changed");
  EXPECT_NE(border, select->GetBorder());
}

// The sidebar: the collapsed rail is the panel's left column. The edge slides
// 224px and the panel slides back by the same amount on the compositor, so rows
// and favicons never move.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       CollapsedRailIsThePanelsLeftColumn) {
  auto& window = Window();
  auto* root = window.sidebar_root_for_testing();
  auto* list = window.tab_sidebar_for_testing();
  ASSERT_TRUE(root);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return list->row_count_for_testing() == 1; }));
  auto settled = [&] {
    return !root->edge()->layer()->GetAnimator()->is_animating() &&
           !root->panel()->layer()->GetAnimator()->is_animating() &&
           !root->shadow()->layer()->GetAnimator()->is_animating();
  };
  MoveToPage();
  ASSERT_TRUE(base::test::RunUntil(settled));
  const float shift = FlyScaled(224);
  EXPECT_EQ(-shift, root->edge()->layer()->transform().To2dTranslation().x());
  EXPECT_EQ(shift, root->panel()->layer()->transform().To2dTranslation().x());
  EXPECT_EQ(0.f, root->shadow()->layer()->opacity());
  EXPECT_TRUE(list->rail());
  EXPECT_EQ(1.f, list->rail_surfaces()->layer()->opacity());
  EXPECT_EQ(0.f, list->expanded_surfaces()->layer()->opacity());
  auto* row = list->row_for_testing(Active(browser()));
  ASSERT_TRUE(row);
  const auto collapsed = row->GetBoundsInScreen();
  const auto origin =
      window.widget()->GetContentsView()->GetBoundsInScreen().origin();
  // Rows are 40px tall, 4px into the list, the favicon centred at 18px.
  EXPECT_EQ(FlyScaled(40), collapsed.height());
  EXPECT_EQ(origin.x() + FlyScaled(4), collapsed.x());
  OpenByPointer();
  ASSERT_TRUE(base::test::RunUntil(settled));
  EXPECT_TRUE(root->edge()->layer()->transform().IsIdentity());
  EXPECT_TRUE(root->panel()->layer()->transform().IsIdentity());
  EXPECT_EQ(1.f, root->shadow()->layer()->opacity());
  EXPECT_FALSE(list->rail());
  EXPECT_EQ(0.f, list->rail_surfaces()->layer()->opacity());
  EXPECT_EQ(1.f, list->expanded_surfaces()->layer()->opacity());
  EXPECT_EQ(collapsed, row->GetBoundsInScreen());
  // Drops find their target by point: only the visible panel is hit, never
  // its shadow or the hidden part of the collapsed panel.
  auto* host_root = window.widget()->GetNativeWindow()->GetRootWindow();
  auto* sidebar = window.sidebar_widget_for_testing()->GetNativeWindow();
  auto* clip = sidebar->parent();
  auto sidebar_at = [&](int x) {
    gfx::Point point(origin.x() + x, origin.y() + 300);
    wm::ConvertPointFromScreen(host_root, &point);
    auto* hit = host_root->GetEventHandlerForPoint(point);
    return hit && clip->Contains(hit);
  };
  EXPECT_TRUE(sidebar_at(100));
  EXPECT_FALSE(sidebar_at(FlyScaled(260) + 20));
  // Collapsing again animates back to the rail.
  MoveToPage();
  ASSERT_TRUE(base::test::RunUntil(settled));
  EXPECT_EQ(-shift, root->edge()->layer()->transform().To2dTranslation().x());
  EXPECT_EQ(collapsed, row->GetBoundsInScreen());
  EXPECT_TRUE(sidebar_at(18));
  EXPECT_FALSE(sidebar_at(100));
  // A larger interface while collapsed moves the edge by the larger width.
  ASSERT_TRUE(App().preferences().SetString("interface_scale", "large"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return root->edge()->layer()->transform().To2dTranslation().x() ==
           -static_cast<float>(FlyScaled(224));
  }));
  EXPECT_GT(FlyScaled(224), 224);
  EXPECT_EQ(static_cast<float>(FlyScaled(224)),
            root->panel()->layer()->transform().To2dTranslation().x());
  ASSERT_TRUE(App().preferences().SetString("interface_scale", "normal"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return root->edge()->layer()->transform().To2dTranslation().x() == -shift;
  }));
}

// Leaving the window (the screen's left edge, the window border) keeps the
// sidebar open; entering the page closes it.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest, LeavingTheWindowKeepsTheSidebar) {
  auto& window = Window();
  OpenByPointer();
  const auto bounds =
      window.widget()->GetContentsView()->GetBoundsInScreen();
  ui::MouseEvent exited(ui::EventType::kMouseExited,
                        gfx::PointF(bounds.x() - 2, bounds.y() + 200),
                        gfx::PointF(bounds.x() - 2, bounds.y() + 200),
                        base::TimeTicks::Now(), ui::EF_NONE, ui::EF_NONE);
  window.OnMouseEvent(&exited);
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(window.sidebar_expanded_for_testing());
  MoveToPage();
  EXPECT_FALSE(window.sidebar_expanded_for_testing());
}

// Tab rows: a middle click closes the tab; holding the selection modifier on
// its own shows the tab numbers, any other key hides them.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       MiddleClickClosesAndModifierShowsTabNumbers) {
  auto& window = Window();
  auto* model = browser()->GetTabStripModel();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* target = Active(browser());
  const int count = model->count();
  auto* list = window.tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] { return list->row_for_testing(target); }));
  // A newly created tab must leave search closed, including after load.
  ASSERT_TRUE(content::WaitForLoadStop(target));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(window.address_picker().active());
  OpenByPointer();
  views::test::RunScheduledLayout(list->GetWidget());
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(
      list->row_for_testing(target)->GetBoundsInScreen().CenterPoint());
  mouse.PressButton(ui::EF_MIDDLE_MOUSE_BUTTON);
  mouse.ReleaseButton(ui::EF_MIDDLE_MOUSE_BUTTON);
  ASSERT_TRUE(base::test::RunUntil([&] { return model->count() == count - 1; }));
  EXPECT_EQ(TabStripModel::kNoTab, model->GetIndexOfWebContents(target));
  // Alt (the default selection modifier) held alone.
  window.TrackTabNumbers(true, ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
                         ui::EF_ALT_DOWN, false, false);
  EXPECT_FALSE(window.tab_numbers_shown_for_testing());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.tab_numbers_shown_for_testing(); }));
  EXPECT_TRUE(list->numbers_visible());
  window.TrackTabNumbers(false, ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
                         ui::EF_NONE, false, false);
  EXPECT_FALSE(window.tab_numbers_shown_for_testing());
  // AltGr and passthrough never show them.
  window.TrackTabNumbers(true, ui::VKEY_MENU, ui::DomCode::ALT_RIGHT,
                         ui::EF_ALT_DOWN, false, false);
  window.TrackTabNumbers(true, ui::VKEY_MENU, ui::DomCode::ALT_LEFT,
                         ui::EF_ALT_DOWN, false, true);
  base::RunLoop loop;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostDelayedTask(
      FROM_HERE, loop.QuitClosure(), base::Milliseconds(300));
  loop.Run();
  EXPECT_FALSE(window.tab_numbers_shown_for_testing());
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
  auto& window = Window();
  MoveToPage();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
                       FocusShortcutUnpinsAndRestoresEditablePage) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(content::ExecJs(page, R"js(
    document.body.innerHTML='<input id="edit" style="position:fixed;left:400px;top:40px">';
    document.querySelector('#edit').focus();
  )js"));
  auto& window = Window();
  MoveToPage();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  auto press = [&](ui::KeyboardCode key, int modifiers) {
    keys.PressKey(key, modifiers);
    keys.ReleaseKey(key, modifiers);
  };
  press(ui::VKEY_Z, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return App().preferences().ReadSidebarAutoCollapse() == false &&
           window.sidebar_expanded_for_testing();
  }));
  press(ui::VKEY_S, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* row = window.tab_sidebar_for_testing()->row_for_testing(page);
    return App().preferences().ReadSidebarAutoCollapse() == true &&
           window.sidebar_expanded_for_testing() && row &&
           row->children().front()->HasFocus();
  }));
  EXPECT_FALSE(window.sidebar_pin_for_testing()->selected());
  press(ui::VKEY_S, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.sidebar_expanded_for_testing() &&
           content::EvalJs(page, "document.hasFocus()").ExtractBool();
  }));
  press(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(page, "document.querySelector('#edit').value")
               .ExtractString() == "j";
  }));
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
  auto& window = Window();
  MoveToPage();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  auto& window = Window();
  MoveToPage();
  const auto width = page->GetRenderWidgetHostView()->GetViewBounds().width();
  OpenByPointer();
  auto* pin = window.sidebar_pin_for_testing();
  views::test::RunScheduledLayout(pin->GetWidget());
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(pin->GetBoundsInScreen().CenterPoint());
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return App().preferences().ReadSidebarAutoCollapse() == false; }));
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
  EXPECT_EQ(true, App().preferences().ReadSidebarAutoCollapse());
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
  EXPECT_FALSE(Window(browser()).RunCommand("toggle-sidebar-pin"));
  EXPECT_FALSE(window.sidebar_pin_for_testing()->GetEnabled());
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
}

IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       RailRowsAndMenusKeepNativeIdentityAcrossProfiles) {
  auto* first = browser();
  const auto first_id = TabIdentity::Get(Active(first));
  ASSERT_TRUE(ExecuteNativeCommand(first, "spawn-tab"));
  auto* target = Active(first);
  const auto id = TabIdentity::Get(target);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  // Each window lists its own tabs.
  auto* second_list = Window(second).tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return second_list->row_count_for_testing() == 1 &&
           second_list->row_for_testing(Active(second));
  }));
  Activate(first);
  auto& window = Window(first);
  auto* list = window.tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return list->row_count_for_testing() == 2 && list->row_for_testing(target);
  }));
  EXPECT_EQ(first_id,
            TabIdentity::Get(first->GetTabStripModel()->GetWebContentsAt(0)));
  EXPECT_EQ(id, TabIdentity::Get(target));
  MoveToPage();
  EXPECT_TRUE(list->rail());
  auto* row = list->row_for_testing(target);
  auto* select =
      views::AsViewClass<views::MdTextButton>(row->children().front().get());
  ASSERT_TRUE(select);
  ASSERT_TRUE(select->GetImageModel(views::Button::STATE_NORMAL));
  // The favicon in its 18px wrapper with room for the split badge.
  EXPECT_EQ(gfx::Size(26, 26),
            select->GetImageModel(views::Button::STATE_NORMAL)
                ->Rasterize(select->GetColorProvider())
                .size());
  SetTabAudioMuted(target, true, TabMutedReason::kAudioIndicator, {});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::u16string(select->GetViewAccessibility().GetCachedName())
               .find(u", muted") != std::u16string::npos;
  }));
  SetTabAudioMuted(target, false, TabMutedReason::kAudioIndicator, {});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::u16string(select->GetViewAccessibility().GetCachedName())
               .find(u", muted") == std::u16string::npos;
  }));
  views::test::RunScheduledLayout(row->GetWidget());
  // The favicon is on the rail's centre line, 18px from the sidebar's edge.
  const auto origin =
      window.widget()->GetContentsView()->GetBoundsInScreen().origin();
  const gfx::Point point(origin.x() + 18,
                         row->GetBoundsInScreen().CenterPoint().y());
  bool inspected = false;
  base::RepeatingTimer menu;
  menu.Start(FROM_HERE, base::Milliseconds(1), base::BindLambdaForTesting([&] {
               auto* model = list->context_menu_for_testing();
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
               ADD_FAILURE() << "Missing native Pin command in the rail";
               list->CloseContextMenu();
             }));
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(point);
  mouse.ClickRightButton();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return inspected && first->GetTabStripModel()->IsTabPinned(0); }));
  EXPECT_EQ(id, TabIdentity::Get(target));
  EXPECT_EQ(first, LastActive());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WindowControlsFollowCommandSettingsAndRecoveryPolicy) {
  auto& window = Window();
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
  EXPECT_EQ(false, App().preferences().ReadWindowControlsVisible());
  editor.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.active(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-window-controls"));
  auto* close = window.window_control_for_testing("close");
  ASSERT_TRUE(close);
  close->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] { return close->HasFocus(); }));
  auto browser_alive = browser()->GetWeakPtr();
  views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
  ASSERT_TRUE(App().preferences().SetWindowControlsVisible(false));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(browser_alive);
  EXPECT_FALSE(controls->GetVisible());
  EXPECT_FALSE(close->HasFocus());
  const auto future =
      base::DictValue().Set("schema", 2).Set("values", base::DictValue());
  g_browser_process->local_state()->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-window-controls"));
  EXPECT_FALSE(Window(browser()).RunCommand("toggle-window-controls"));
  EXPECT_FALSE(controls->GetVisible());
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
}

// The sidebar's window controls act on their own window only.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WindowControlsActOnTheirOwnWindow) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(App().preferences().SetWindowControlsVisible(true));
  auto& window = Window(second);
  auto* widget = window.widget();
  auto click = [&](std::string_view action) {
    auto* button = window.window_control_for_testing(action);
    ASSERT_TRUE(button);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return button->IsDrawn() && !button->GetVisibleBounds().IsEmpty();
    }));
    views::test::RunScheduledLayout(button->GetWidget());
    ui::test::EventGenerator mouse(widget->GetNativeWindow()->GetRootWindow());
    mouse.MoveMouseTo(button->GetBoundsInScreen().CenterPoint());
    mouse.ClickLeftButton();
  };
  click("maximize");
  ASSERT_TRUE(base::test::RunUntil([&] { return widget->IsMaximized(); }));
  EXPECT_TRUE(second->GetWindow()->IsMaximized());
  EXPECT_FALSE(first->GetWindow()->IsMaximized());
  click("maximize");
  ASSERT_TRUE(base::test::RunUntil([&] { return !widget->IsMaximized(); }));
  click("minimize");
  ASSERT_TRUE(base::test::RunUntil([&] { return widget->IsMinimized(); }));
  EXPECT_FALSE(first->GetWindow()->IsMinimized());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Active(second)->GetVisibility() != content::Visibility::VISIBLE;
  }));
  ExpectVisible(Active(first), true);
  Activate(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return !widget->IsMinimized(); }));
  ExpectVisible(Active(second), true);
}

// The close control closes its window, after the page's beforeunload
// decision; cancelling keeps the window and the other workspaces.
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
  auto& window = Window(second);
  ASSERT_TRUE(App().preferences().SetWindowControlsVisible(true));
  auto* close = window.window_control_for_testing("close");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return close->IsDrawn() && !close->GetVisibleBounds().IsEmpty();
  }));
  views::test::RunScheduledLayout(close->GetWidget());
  auto point = close->GetVisibleBounds().CenterPoint();
  views::View::ConvertPointToScreen(close, &point);
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(point);
  mouse.ClickLeftButton();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  ASSERT_TRUE(first && second_alive);
  EXPECT_EQ("keep", content::EvalJs(contents, "window.unsavedControls"));
  EXPECT_TRUE(window.window_controls_for_testing()->GetVisible());
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload=null"));
  views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return !second_alive; }));
  ASSERT_TRUE(first);
  EXPECT_TRUE(FlyWindowController::FromBrowser(first.get()));
}

// Quitting closes every window after their beforeunload decisions; a
// cancellation keeps all of them and Fly keeps working.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       QuitHonoursBeforeUnloadCancellation) {
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
  chrome::CloseAllBrowsers();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog);
  ASSERT_TRUE(dialog->is_before_unload_dialog());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil([&] { return cancellation.cancelled; }));
  ASSERT_TRUE(first);
  ASSERT_TRUE(second_alive);
  ASSERT_TRUE(base::test::RunUntil([&] { return !App().closing(); }));
  ExpectVisible(contents, true);
  ASSERT_TRUE(content::ExecJs(contents, "window.onbeforeunload = null"));
  const int count = second->GetTabStripModel()->count();
  ASSERT_TRUE(Window(second).RunCommand("spawn-tab"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return second->GetTabStripModel()->count() == count + 1; }));
}

// Switching workspaces activates the other workspace's window. Both windows
// stay open and their pages stay visible.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SwitchingWorkspacesActivatesTheirWindows) {
  auto* first = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      first, embedded_test_server()->GetURL("/title1.html")));
  auto* first_contents = Active(first);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      second, embedded_test_server()->GetURL("/title2.html")));
  ASSERT_TRUE(IsActive(second));
  ASSERT_TRUE(Window(second).RunCommand("next-workspace"));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(first); }));
  EXPECT_TRUE(first->GetWindow()->IsActive());
  EXPECT_FALSE(second->GetWindow()->IsActive());
  ExpectVisible(first_contents, true);
  ExpectVisible(Active(second), true);
  ASSERT_TRUE(Window(first).RunCommand("previous-workspace"));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(second); }));
  // The catalogue's active workspace follows the window used last.
  const auto workspace = App().WorkspaceFor(second->GetProfile());
  ASSERT_TRUE(workspace);
  EXPECT_EQ(workspace->id, App().workspaces().Read()->active_id);
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
  auto* sidebar = Window().tab_sidebar_for_testing();
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
  // Another workspace's window lists only its own tabs; this list is kept.
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto* second_sidebar = Window(second).tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return second_sidebar->row_count_for_testing() == 1 &&
           second_sidebar->row_for_testing(Active(second)) != nullptr;
  }));
  EXPECT_EQ(nullptr, second_sidebar->row_for_testing(first));
  Activate(browser());
  EXPECT_EQ(original_row, sidebar->row_for_testing(first));
  EXPECT_EQ(2u, sidebar->row_count_for_testing());
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
  auto* sidebar = Window().tab_sidebar_for_testing();
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
  Activate(browser());
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
  auto* sidebar = Window().tab_sidebar_for_testing();
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
                       HiddenChromiumOmniboxKeepsOnlyNativeFallback) {
  auto* location = BrowserView::GetBrowserViewForBrowser(browser())
                       ->toolbar()->location_bar_view();
  ASSERT_TRUE(location->GetOmniboxPopupView());
  EXPECT_EQ(nullptr, location->GetOmniboxPopupUI());
  EXPECT_EQ(nullptr, location->GetOmniboxPopupAimPresenter());
  Activate(browser());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Window().address_picker().active() &&
           Window().address_picker().search_for_testing()->HasFocus();
  }));
  Window().address_picker().Cancel();
}

// Hold the response before headers and after commit independently. These tests
// assert the feedback during the otherwise invisible part of a real navigation,
// rather than merely setting a synthetic loading flag on a view.
class FlyNavigationFeedbackBrowserTest : public FlySidebarBrowserTest {
 public:
  void SetUpOnMainThread() override {
    FlySidebarBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(App().preferences().SetStatusBarVisible(true));
    host_resolver()->AddRule("*", "127.0.0.1");
    waiting_ = std::make_unique<net::test_server::ControllableHttpResponse>(
        &server_, "/waiting", true);
    next_ = std::make_unique<net::test_server::ControllableHttpResponse>(
        &server_, "/next");
    ASSERT_TRUE(server_.Start());
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("/title1.html")));
    Activate(browser());
  }

 protected:
  TabSidebar* Sidebar() { return Window().tab_sidebar_for_testing(); }
  StatusBar* Status() { return Window().status_bar_for_testing(); }
  void WaitForFeedback(content::WebContents* contents) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      return Sidebar()->loading_icon_for_testing(contents) != nullptr;
    }));
    if (contents == Active(browser()))
      EXPECT_EQ(u"Stop loading",
                Status()->reload_for_testing()->GetTooltipText());
  }
  void WaitForIdle(content::WebContents* contents) {
    ASSERT_TRUE(base::test::RunUntil([&] {
      return !contents->IsLoading() &&
             Sidebar()->row_for_testing(contents) &&
             Sidebar()->row_count_for_testing() ==
                 static_cast<size_t>(browser()->GetTabStripModel()->count()) &&
             !Sidebar()->loading_icon_for_testing(contents);
    }));
    if (contents == Active(browser())) {
      EXPECT_EQ(u"Reload page",
                Status()->reload_for_testing()->GetTooltipText());
      EXPECT_TRUE(Status()->location_for_testing()->GetText().empty());
    }
  }
  void Start(const GURL& url) {
    Active(browser())->GetController().LoadURL(url, content::Referrer(),
                                               ui::PAGE_TRANSITION_TYPED, "");
  }
  void SendPage(net::test_server::ControllableHttpResponse& response) {
    response.Send(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n"
        "<!doctype html><title>Destination</title><h1>Destination</h1>" +
        std::string(4096, ' '));
  }
  net::EmbeddedTestServer server_;
  std::unique_ptr<net::test_server::ControllableHttpResponse> waiting_;
  std::unique_ptr<net::test_server::ControllableHttpResponse> next_;
};

IN_PROC_BROWSER_TEST_F(FlyNavigationFeedbackBrowserTest,
                       SearchAnswersBeforeHeadersAndStopKeepsPreviousPage) {
  auto* contents = Active(browser());
  const auto previous = contents->GetLastCommittedURL();
  SearchService search(*g_browser_process->local_state());
  ASSERT_TRUE(search.SetEngine(server_.GetURL("/waiting?q=%s").spec()));
  auto& address = Window().address_picker();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] { return address.active(); }));
  address.search_for_testing()->SetText(u"slow search");
  address.ContentsChanged(address.search_for_testing(), u"slow search");
  ui::test::EventGenerator keys(
      Window().widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  waiting_->WaitForRequest();
  WaitForFeedback(contents);
  EXPECT_EQ(previous, contents->GetLastCommittedURL());
  EXPECT_EQ(u"Opening 127.0.0.1\u2026",
            Status()->location_for_testing()->GetText());
  auto* row = Sidebar()->row_for_testing(contents);
  auto* select = static_cast<views::Button*>(row->children()[0].get());
  EXPECT_NE(std::u16string::npos,
            select->GetViewAccessibility().GetCachedName().find(u", loading"));
  views::test::ButtonTestApi(Status()->reload_for_testing())
      .NotifyDefaultMouseClick();
  WaitForIdle(contents);
  EXPECT_EQ(previous, contents->GetLastCommittedURL());
  EXPECT_EQ(row, Sidebar()->row_for_testing(contents));
  waiting_->Done();
  // The same control now reloads the current page, rather than the cancelled
  // URL.
  content::TestNavigationObserver reloaded(contents);
  views::test::ButtonTestApi(Status()->reload_for_testing())
      .NotifyDefaultMouseClick();
  reloaded.Wait();
  WaitForIdle(contents);
  EXPECT_EQ(previous, contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(FlyNavigationFeedbackBrowserTest,
                       LinkFeedbackSurvivesCommitRailThemeAndScale) {
  auto* contents = Active(browser());
  const auto previous = contents->GetLastCommittedURL();
  const auto destination = server_.GetURL("/waiting");
  ASSERT_TRUE(content::ExecJs(contents, content::JsReplace(R"js(
    const link = document.createElement('a');
    link.href = $1; link.textContent = 'Open destination';
    document.body.append(link); link.click();
  )js",
                                                           destination)));
  waiting_->WaitForRequest();
  WaitForFeedback(contents);
  EXPECT_EQ(previous, contents->GetLastCommittedURL());
  EXPECT_EQ(u"Opening 127.0.0.1\u2026",
            Status()->location_for_testing()->GetText());
  auto* icon = Sidebar()->loading_icon_for_testing(contents);
  for (const char* scale : {"large", "normal"}) {
    ASSERT_TRUE(App().preferences().SetString("interface_scale", scale));
    ASSERT_TRUE(App().preferences().ToggleTheme());
    OpenByPointer();
    WaitForSidebarMotion();
    views::test::RunScheduledLayout(icon->GetWidget());
    EXPECT_EQ(gfx::Size(FlyScaled(26), FlyScaled(26)), icon->size());
    const auto expanded = icon->GetBoundsInScreen();
    MoveToPage();
    WaitForSidebarMotion();
    EXPECT_TRUE(Sidebar()->rail());
    EXPECT_EQ(expanded, icon->GetBoundsInScreen());
    EXPECT_EQ(icon, Sidebar()->loading_icon_for_testing(contents));
    ASSERT_TRUE(App().preferences().ToggleTheme());
  }
  SendPage(*waiting_);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetLastCommittedURL() == destination; }));
  WaitForFeedback(contents);
  EXPECT_EQ(u"Loading 127.0.0.1\u2026",
            Status()->location_for_testing()->GetText());
  // A hovered link still takes priority and clearing it restores load feedback.
  Window().UpdateTargetURL(contents, GURL("https://example.test/link"));
  EXPECT_NE(
      std::u16string::npos,
      Status()->location_for_testing()->GetText().find(u"example.test/link"));
  Window().UpdateTargetURL(contents, GURL());
  EXPECT_EQ(u"Loading 127.0.0.1\u2026",
            Status()->location_for_testing()->GetText());
  waiting_->Done();
  WaitForIdle(contents);
}

IN_PROC_BROWSER_TEST_F(
    FlyNavigationFeedbackBrowserTest,
    BackgroundTabKeepsItsOwnFeedbackAndCanCloseWhileLoading) {
  auto* loading = Active(browser());
  Start(server_.GetURL("/waiting"));
  waiting_->WaitForRequest();
  WaitForFeedback(loading);
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* strip = browser()->GetTabStripModel();
  strip->ActivateTabAt(1);
  auto* idle = Active(browser());
  WaitForIdle(idle);
  EXPECT_TRUE(Sidebar()->loading_icon_for_testing(loading));
  strip->ActivateTabAt(0);
  WaitForFeedback(loading);
  EXPECT_EQ(u"Opening 127.0.0.1\u2026",
            Status()->location_for_testing()->GetText());
  strip->CloseWebContentsAt(0, TabCloseTypes::CLOSE_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return strip->count() == 1; }));
  WaitForIdle(idle);
  EXPECT_EQ(1u, Sidebar()->row_count_for_testing());
  waiting_->Done();
}

IN_PROC_BROWSER_TEST_F(FlyNavigationFeedbackBrowserTest,
                       RedirectAndSupersededRequestDoNotLeaveStaleFeedback) {
  auto* contents = Active(browser());
  Start(server_.GetURL("first.test", "/waiting"));
  waiting_->WaitForRequest();
  WaitForFeedback(contents);
  EXPECT_EQ(u"Opening first.test\u2026",
            Status()->location_for_testing()->GetText());
  // Resume observing a navigation which started before this tab was selected.
  ASSERT_TRUE(AddTabAtIndex(1, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* strip = browser()->GetTabStripModel();
  strip->ActivateTabAt(1);
  WaitForIdle(Active(browser()));
  strip->ActivateTabAt(0);
  WaitForFeedback(contents);
  const auto redirected = server_.GetURL("second.test", "/next");
  waiting_->Send("HTTP/1.1 302 Found\r\nLocation: " + redirected.spec() +
                 "\r\nContent-Length: 0\r\n\r\n");
  waiting_->Done();
  next_->WaitForRequest();
  EXPECT_EQ(u"Opening second.test\u2026",
            Status()->location_for_testing()->GetText());
  WaitForFeedback(contents);
  // Replacing the in-flight navigation removes the old destination and spinner.
  const auto replacement = embedded_test_server()->GetURL("/title2.html");
  Start(replacement);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetLastCommittedURL() == replacement;
  }));
  WaitForIdle(contents);
  next_->Done();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(contents, "location.hash = 'section'"));
  WaitForIdle(contents);
}

IN_PROC_BROWSER_TEST_F(FlyNavigationFeedbackBrowserTest,
                       ErrorResponseAndNoContentBothEndFeedback) {
  auto* contents = Active(browser());
  Start(server_.GetURL("/waiting"));
  waiting_->WaitForRequest();
  WaitForFeedback(contents);
  // A server closing without headers produces a real Chromium error page.
  waiting_->Done();
  WaitForIdle(contents);
  const auto error_url = contents->GetLastCommittedURL();
  Start(server_.GetURL("/next"));
  next_->WaitForRequest();
  WaitForFeedback(contents);
  next_->Send("HTTP/1.1 204 No Content\r\n\r\n");
  next_->Done();
  WaitForIdle(contents);
  EXPECT_EQ(error_url, contents->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarTitleChangesTouchOnlyTheirRow) {
  for (int i = 1; i < 24; ++i)
    ASSERT_TRUE(AddTabAtIndex(i, GURL("about:blank"), ui::PAGE_TRANSITION_TYPED));
  auto* strip = browser()->GetTabStripModel();
  auto* list = Window().tab_sidebar_for_testing();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(24u, list->row_count_for_testing());
  auto* contents = Active(browser());
  auto* row = list->row_for_testing(contents);
  auto* select = static_cast<views::MdTextButton*>(row->children()[0]);
  select->RequestFocus();
  const auto before = list->row_updates_for_testing();
  ASSERT_TRUE(content::ExecJs(contents, "document.title = 'Updated title'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return select->GetTooltipText().find(u"Updated title") != std::u16string::npos;
  }));
  EXPECT_EQ(before + 1, list->row_updates_for_testing());
  EXPECT_EQ(row, list->row_for_testing(contents));
  EXPECT_EQ(select, select->GetFocusManager()->GetFocusedView());
  const auto selected_before = list->row_updates_for_testing();
  strip->ActivateTabAt(0);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(selected_before + 2, list->row_updates_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarUsesNativeFaviconAndKeepsBlankTabRow) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(),
      embedded_test_server()->GetURL("/favicon/page_with_favicon.html")));
  auto* contents = Active(browser());
  auto* sidebar = Window().tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* entry = contents->GetController().GetVisibleEntry();
    return entry && entry->GetFavicon().valid &&
           !entry->GetFavicon().image.IsEmpty() &&
           sidebar->row_for_testing(contents) &&
           !sidebar->favicon_for_testing(contents).isNull();
  }));
  auto* row = sidebar->row_for_testing(contents);
  auto* select = static_cast<views::MdTextButton*>(row->children()[0]);
  // The 16px favicon inside its 18px wrapper, with room for the split badge.
  EXPECT_EQ(gfx::Size(26, 26),
            select->GetImage(views::Button::STATE_NORMAL).size());
  const std::string id = TabIdentity::Get(contents);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), GURL("about:blank")));
  // The globe fallback replaces the site's favicon in the same row.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return sidebar->favicon_for_testing(contents).isNull(); }));
  EXPECT_EQ(row, sidebar->row_for_testing(contents));
  EXPECT_EQ(id, TabIdentity::Get(contents));
  EXPECT_EQ(GURL("about:blank"), contents->GetLastCommittedURL());
}

// The same row supplies the expanded panel and its collapsed icon column.
// Interface scale must resize its artwork as well as its text and surfaces.
IN_PROC_BROWSER_TEST_F(FlySidebarBrowserTest,
                       TabArtworkAndControlsFollowInterfaceScale) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/favicon/page_with_favicon.html")));
  auto& window = Window();
  auto* contents = Active(browser());
  auto* list = window.tab_sidebar_for_testing();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return list->row_for_testing(contents) &&
           !list->favicon_for_testing(contents).isNull();
  }));
  SetTabAudioMuted(contents, true, TabMutedReason::kAudioIndicator, {});
  auto* row = list->row_for_testing(contents);
  auto* select = static_cast<views::MdTextButton*>(row->children()[0]);
  auto* pin = row->children()[1].get();
  auto* close = row->children()[2].get();
  auto* audio = row->children()[3].get();
  for (const char* scale : {"normal", "large", "normal"}) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(App().preferences().SetString("interface_scale", scale));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return select->GetImage(views::Button::STATE_NORMAL).size() ==
                 gfx::Size(FlyScaled(26), FlyScaled(26)) &&
             audio->GetVisible();
    }));
    OpenByPointer();
    WaitForSidebarMotion();
    views::test::RunScheduledLayout(row->GetWidget());
    EXPECT_EQ(FlyScaled(40), row->height());
    EXPECT_EQ(FlyScaled(18), audio->width());
    EXPECT_EQ(FlyScaled(10), close->x() - audio->bounds().right());
    EXPECT_EQ(FlyScaled(8), row->width() - close->bounds().right());
    const auto expanded = row->GetBoundsInScreen();
    MoveToPage();
    WaitForSidebarMotion();
    EXPECT_EQ(expanded, row->GetBoundsInScreen());
    EXPECT_EQ(row, list->row_for_testing(contents));
    EXPECT_EQ(gfx::Size(FlyScaled(26), FlyScaled(26)),
              select->GetImage(views::Button::STATE_NORMAL).size());
    // A pinned row has no close slot; pin and audio keep the CSS flex gap
    // plus badge margin rather than crowding the title or each other.
    browser()->GetTabStripModel()->SetTabPinned(0, true);
    ASSERT_TRUE(base::test::RunUntil([&] { return pin->GetVisible(); }));
    views::test::RunScheduledLayout(row->GetWidget());
    EXPECT_EQ(FlyScaled(14), audio->x() - pin->bounds().right());
    EXPECT_FALSE(close->GetVisible());
    EXPECT_EQ(FlyScaled(8), row->width() - audio->bounds().right());
    browser()->GetTabStripModel()->SetTabPinned(0, false);
    ASSERT_TRUE(base::test::RunUntil([&] { return !pin->GetVisible(); }));
  }
}

// New tabs open Fly's new tab page: clock, date, lists and the memory use of
// Fly, refreshed while the page shows.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabShowsFlysPageWithLiveMemory) {
  auto* model = browser()->GetTabStripModel();
  const int count = model->count();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  ASSERT_EQ(count + 1, model->count());
  auto* contents = Active(browser());
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(GURL(kFlyNewTabURL), contents->GetLastCommittedURL());
  EXPECT_EQ(u"New tab", contents->GetTitle());
  EXPECT_TRUE(IsFlyNewTabPage(contents->GetLastCommittedURL()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.getElementById('memory').textContent")
        .ExtractString()
        .ends_with(" MB");
  }));
  EXPECT_FALSE(content::EvalJs(contents,
                               "document.getElementById('clock').textContent")
                   .ExtractString()
                   .empty());
  EXPECT_EQ("MEMORY",
            content::EvalJs(contents,
                            "document.querySelector('.annotation small')"
                            ".textContent"));
  // The figure is rewritten every second while the page shows.
  ASSERT_TRUE(content::ExecJs(
      contents, "document.getElementById('memory').textContent = 'stale'"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
                           "document.getElementById('memory').textContent")
               .ExtractString() != "stale";
  }));
  EXPECT_EQ(false, content::EvalJs(
                       contents,
                       "document.getElementById('saved-bookmarks').hidden"));
  // The sidebar row shows the title alone.
  auto* row =
      Window().tab_sidebar_for_testing()->row_for_testing(
          contents);
  ASSERT_TRUE(row);
}

// The HTML and its blocking stylesheet must already carry the correct state,
// independently of the asynchronous WebUI ready message and script execution.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabAddressAcceptsTypingBeforeLoadFinishes) {
  auto& window = Window();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* contents = Active(browser());
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-address"));
  ASSERT_TRUE(window.address_picker().active());
  auto* search = window.address_picker().search_for_testing();
  ASSERT_TRUE(search);
  search->SetText(u"typed immediately");
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(window.address_picker().active());
  search = window.address_picker().search_for_testing();
  ASSERT_TRUE(search);
  EXPECT_EQ(u"typed immediately", search->GetText());
  EXPECT_TRUE(search->HasFocus());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.address_picker().active();
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabInitialResponseHasThemeGeometryAndSafeSnapshot) {
  ASSERT_TRUE(App().preferences().ToggleTheme());
  ASSERT_TRUE(App().preferences().SetString("interface_scale", "large"));
  ASSERT_TRUE(App().preferences().SetSidebarAutoCollapse(true));
  ASSERT_TRUE(App().preferences().SetThemeColor(false, "bg_main", "#efe8d1"));
  const std::string title = "</script><script>window.injected = true</script>";
  ASSERT_TRUE(App().bookmarks().Add("https://snapshot.example/", title));
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  EXPECT_EQ(true, content::EvalJs(contents, R"JS(
    (async () => {
      const css = [...document.styleSheets].find(sheet =>
          sheet.href?.endsWith('/new-tab.css')).cssRules;
      const initialStyle = [...css].at(-1).style;
      const parsed = document;
      const data = JSON.parse(parsed.getElementById('initial-data').textContent);
      return parsed.body.classList.contains('light') && data.light &&
          data.scale === 1.25 && data.sidebarOverhang === 280 &&
          data.colours['--bg-main'] === '#efe8d1' &&
          data.bookmarks[0].title === '</script><script>window.injected = true</script>' &&
          parsed.querySelectorAll('script').length === 2 && !window.injected &&
          parsed.getElementById('memory-fly').getAttribute('src').includes('papel') &&
          initialStyle.getPropertyValue('--bg-main').trim() === '#efe8d1' &&
          initialStyle.getPropertyValue('--scale').trim() === '1.250000' &&
          initialStyle.getPropertyValue('--sidebar-overhang').trim() === '280px';
    })()
  )JS"));
  EXPECT_EQ("rgb(239, 232, 209)", content::EvalJs(contents,
      "getComputedStyle(document.body).backgroundColor"));
  EXPECT_EQ(title, content::EvalJs(contents,
      "document.querySelector('#bookmarks a').textContent"));
  const auto queries = App().new_tab_history().query_count_for_testing();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() != browser();
  }));
  auto* private_browser = LastActive();
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(content::WaitForLoadStop(Active(private_browser)));
  EXPECT_EQ(true, content::EvalJs(Active(private_browser), R"JS(
    (() => {
      const data = JSON.parse(document.getElementById('initial-data').textContent);
      return data.light && !data.showBookmarks && !data.showSites &&
          data.bookmarks.length === 0 && data.sites.length === 0 &&
          document.querySelectorAll('#bookmarks a, #sites a').length === 0;
    })()
  )JS"));
  EXPECT_EQ(queries, App().new_tab_history().query_count_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabsShareHistoryAndInvalidateDeletedEntries) {
  const auto visited = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), visited));
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<history::QueryResults> flushed;
  auto* history = HistoryServiceFactory::GetForProfile(
      browser()->GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
  history->QueryHistory({}, history::QueryOptions(), flushed.GetCallback(), &tracker);
  ASSERT_FALSE(flushed.Get().empty());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  ASSERT_TRUE(content::WaitForLoadStop(Active(browser())));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(Active(browser()),
        "document.querySelectorAll('#sites a').length").ExtractInt() == 1;
  }));
  const auto queries = App().new_tab_history().query_count_for_testing();
  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
    ASSERT_TRUE(content::WaitForLoadStop(Active(browser())));
    EXPECT_EQ(1, content::EvalJs(Active(browser()),
        "document.querySelectorAll('#sites a').length"));
    EXPECT_EQ(queries, App().new_tab_history().query_count_for_testing());
  }
  ASSERT_TRUE(App().preferences().ToggleTheme());
  ASSERT_TRUE(App().preferences().SetString("interface_scale", "large"));
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(queries, App().new_tab_history().query_count_for_testing());
  // All new tabs are hidden when the original HTTP tab is selected. Deletion
  // must invalidate their cached history without querying in the background.
  browser()->GetTabStripModel()->ActivateTabAt(0);
  base::RunLoop().RunUntilIdle();
  history->DeleteURLs({visited});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return App().new_tab_history().entries().empty();
  }));
  EXPECT_EQ(queries, App().new_tab_history().query_count_for_testing());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  ASSERT_TRUE(content::WaitForLoadStop(Active(browser())));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return App().new_tab_history().query_count_for_testing() > queries;
  }));
  EXPECT_EQ(0, content::EvalJs(Active(browser()),
      "document.querySelectorAll('#sites a').length"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabReservesScaledSidebarOverhang) {
  auto& window = Window();
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  for (const char* scale : {"normal", "large", "normal"}) {
    SCOPED_TRACE(scale);
    ASSERT_TRUE(App().preferences().SetString("interface_scale", scale));
    for (bool collapse : {true, false}) {
      SCOPED_TRACE(collapse);
      ASSERT_TRUE(App().preferences().SetSidebarAutoCollapse(collapse));
      const int overhang = collapse
          ? FlyScaled(kSidebarWidth) - FlyScaled(kSidebarRailWidth) : 0;
      ASSERT_TRUE(base::test::RunUntil([&] {
        return content::EvalJs(contents, content::JsReplace(
            "document.documentElement.style.getPropertyValue("
            "'--sidebar-overhang') === $1", std::to_string(overhang) + "px"))
            .ExtractBool();
      }));
      // Native viewport resizing and the WebUI preference update arrive
      // separately. Measure after both, before checking hover's invariant.
      window.widget()->LayoutRootViewIfNecessary();
      ASSERT_TRUE(base::test::RunUntil([&] {
        return content::EvalJs(contents, "innerWidth").ExtractInt() ==
               contents->GetRenderWidgetHostView()->GetViewBounds().width();
      }));
      ASSERT_TRUE(content::ExecJs(contents,
          "(async () => { await document.fonts.ready; "
          "await new Promise(requestAnimationFrame); "
          "await new Promise(requestAnimationFrame); })()"));
      const auto clock_left = content::EvalJs(contents,
          "document.getElementById('clock').getBoundingClientRect().left")
          .ExtractDouble();
      EXPECT_GE(clock_left, overhang);
      if (collapse) {
        ASSERT_TRUE(InvokeNativeCommand(browser(), "focus-sidebar"));
        ASSERT_TRUE(base::test::RunUntil([&] {
          return window.sidebar_expanded_for_testing();
        }));
        EXPECT_EQ(clock_left, content::EvalJs(contents,
            "document.getElementById('clock').getBoundingClientRect().left")
            .ExtractDouble());
        ASSERT_TRUE(InvokeNativeCommand(browser(), "focus-sidebar"));
      }
    }
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NewTabLoadsFreshListsAndFollowsLivePreferences) {
  const auto visited = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), visited));
  // Flush the native history write, without ever opening a Fly picker.
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<history::QueryResults> flushed;
  HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                       ServiceAccessType::EXPLICIT_ACCESS)
      ->QueryHistory({}, history::QueryOptions(), flushed.GetCallback(), &tracker);
  ASSERT_FALSE(flushed.Get().empty());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::WaitForLoadStop(contents));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelectorAll('#sites a').length").ExtractInt() == 1;
  }));
  EXPECT_EQ(url::Origin::Create(visited).GetURL().spec(),
            content::EvalJs(contents, "document.querySelector('#sites a').href"));
  // Bookmarks also exclude their origins from frequent sites immediately.
  ASSERT_TRUE(App().bookmarks().Add(visited.spec(), "Saved page"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelector('#bookmarks a')?.textContent === 'Saved page'"
        " && document.querySelectorAll('#sites a').length === 0").ExtractBool();
  }));
  ASSERT_TRUE(App().bookmarks().Remove(visited.spec()));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelectorAll('#sites a').length").ExtractInt() == 1;
  }));
  ASSERT_TRUE(App().preferences().SetFrequentSitesVisible(false));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.getElementById('frequent-sites').hidden").ExtractBool();
  }));
  ASSERT_TRUE(App().preferences().SetFrequentSitesVisible(true));
  ASSERT_TRUE(App().preferences().ToggleTheme());
  ASSERT_TRUE(App().preferences().SetString("interface_scale", "large"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.body.classList.contains('light') && "
        "document.documentElement.style.getPropertyValue('--scale') === '1.25' && "
        "!document.getElementById('frequent-sites').hidden && "
        "document.querySelectorAll('#sites a').length === 1").ExtractBool();
  }));
  // Native history deletion updates the visible new tab too.
  HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                       ServiceAccessType::EXPLICIT_ACCESS)
      ->DeleteURLs({visited});
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.querySelectorAll('#sites a').length").ExtractInt() == 0;
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BackgroundNewTabSamplesOnlyWhileVisible) {
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), GURL(kFlyNewTabURL), WindowOpenDisposition::NEW_BACKGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  auto* model = browser()->GetTabStripModel();
  auto* contents = model->GetWebContentsAt(model->count() - 1);
  ASSERT_NE(contents, Active(browser()));
  ASSERT_NE(content::Visibility::VISIBLE, contents->GetVisibility());
  // Give a hidden page more than a complete sample interval. Its initial
  // ready message must not start a timer, even without a visibilitychange.
  const auto wait_interval = [] {
    base::RunLoop loop;
    base::OneShotTimer timer;
    timer.Start(FROM_HERE, base::Milliseconds(1500), loop.QuitClosure());
    loop.Run();
  };
  wait_interval();
  EXPECT_EQ("—", content::EvalJs(contents,
      "document.getElementById('memory').textContent"));
  model->ActivateTabAt(model->GetIndexOfWebContents(contents));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.getElementById('memory').textContent")
        .ExtractString().ends_with(" MB");
  }));
  model->ActivateTabAt(0);
  ASSERT_NE(content::Visibility::VISIBLE, contents->GetVisibility());
  ASSERT_TRUE(content::ExecJs(contents,
      "document.getElementById('memory').textContent = 'hidden'"));
  wait_interval();
  EXPECT_EQ("hidden", content::EvalJs(contents,
      "document.getElementById('memory').textContent"));
  model->ActivateTabAt(model->GetIndexOfWebContents(contents));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.getElementById('memory').textContent")
        .ExtractString().ends_with(" MB");
  }));
  auto* desktop = Window().widget();
  desktop->Minimize();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetVisibility() != content::Visibility::VISIBLE;
  }));
  ASSERT_TRUE(content::ExecJs(contents,
      "document.getElementById('memory').textContent = 'minimized'"));
  wait_interval();
  EXPECT_EQ("minimized", content::EvalJs(contents,
      "document.getElementById('memory').textContent"));
  desktop->Restore();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents,
        "document.getElementById('memory').textContent")
        .ExtractString().ends_with(" MB");
  }));
}

// The download shelf appears when a download starts, shows the card, and
// toggle-downloads hides it again.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, DownloadShelfFollowsDownloads) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = Window();
  auto* shelf = window.download_shelf_for_testing();
  ASSERT_TRUE(shelf);
  EXPECT_FALSE(shelf->GetVisible());
  ASSERT_TRUE(content::ExecJs(Active(browser()), R"js(
    const link = document.createElement('a');
    link.href = URL.createObjectURL(new Blob(['fly download']));
    link.download = 'fly-shelf.txt';
    document.body.append(link);
    link.click();
  )js"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return shelf->GetVisible() && shelf->card_count_for_testing() == 1; }));
  auto* manager = browser()->GetProfile()->GetDownloadManager();
  ASSERT_TRUE(base::test::RunUntil([&] {
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    return items.size() == 1 &&
           items[0]->GetState() == download::DownloadItem::COMPLETE;
  }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-downloads"));
  EXPECT_FALSE(shelf->GetVisible());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-downloads"));
  EXPECT_TRUE(shelf->GetVisible());
}

// The status bar: messages in the link area, the zoom controls above
// the zoom pill and the mode badge entering passthrough.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       StatusBarMessagesZoomControlsAndModeBadge) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = Window();
  auto* status = window.status_bar_for_testing();
  window.ShowStatus(u"Saved");
  EXPECT_EQ(u"Saved", status->message_for_testing());
  EXPECT_NE(u"Saved", std::u16string(status->mode_label()->GetText()));
  views::test::ButtonTestApi(status->zoom_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_TRUE(status->zoom_controls_open_for_testing());
  views::test::ButtonTestApi(status->zoom_for_testing())
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !status->zoom_controls_open_for_testing(); }));
  EXPECT_EQ(u"NORMAL", std::u16string(status->mode_label()->GetText()));
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  const auto badge = status->mode_label()->GetBoundsInScreen().CenterPoint();
  mouse.MoveMouseTo(badge);
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::u16string(status->mode_label()->GetText()) == u"PASSTHROUGH";
  }));
  mouse.ClickLeftButton();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return std::u16string(status->mode_label()->GetText()) == u"NORMAL";
  }));
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
  EXPECT_EQ(other, LastActive());
  // Separate windows: the first workspace's page stays on screen.
  ExpectVisible(inspected, true);
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
  // The playing page is a background tab of its own window.
  ASSERT_TRUE(OpenNativeBlankTab(playing_browser));
  ExpectVisible(playing, false);
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
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
  EXPECT_EQ(other, LastActive());
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
  // The playing page is a background tab of its own window.
  ASSERT_TRUE(OpenNativeBlankTab(first));
  ExpectVisible(playing, false);
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
  EXPECT_EQ(other, LastActive());
  ExpectVisible(playing, false);
  ExpectVisible(Active(other), true);
  ASSERT_TRUE(content::ExecJs(playing,
                              "document.getElementById('fly-media').pause()"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       GlobalMuteUsesNativeTabsAcrossHiddenAndPrivateWorkspaces) {
  auto* first = browser();
  auto* first_tab = Active(first);
  ASSERT_TRUE(OpenNativeBlankTab(first));
  auto* second_tab = Active(first);
  auto* other = SecondWorkspace();
  ASSERT_TRUE(other);
  auto* hidden_tab = Active(other);
  ASSERT_TRUE(Window(other).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() != other;
  }));
  auto* private_browser = LastActive();
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
  EXPECT_EQ(private_browser, LastActive());
  EXPECT_EQ(second_tab, Active(first));
  EXPECT_EQ(hidden_tab, Active(other));
  // Every workspace has its own window, so each active tab stays visible.
  ExpectVisible(second_tab, true);
  ExpectVisible(hidden_tab, true);
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
  auto* sidebar = Window().tab_sidebar_for_testing();
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

// Closing a window releases its Fly interface; the other windows keep theirs.
// Closing a window's last tab closes that window only.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ClosingAWindowReleasesOnlyItsInterface) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(IsActive(second));
  auto second_lifetime = second->GetWeakPtr();
  second->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] { return !second_lifetime; }));
  EXPECT_EQ(1u, App().Windows().size());
  auto& window = Window(first);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.tab_sidebar_for_testing()->row_for_testing(Active(first));
  }));
  EXPECT_EQ(first->GetTabStripModel()->count(),
            static_cast<int>(
                window.tab_sidebar_for_testing()->row_count_for_testing()));
  // The last tab of a workspace window closes the window, not Fly.
  auto* third = SecondWorkspace();
  ASSERT_TRUE(third);
  ASSERT_EQ(1, third->GetTabStripModel()->count());
  auto third_lifetime = third->GetWeakPtr();
  ASSERT_TRUE(InvokeNativeCommand(third, "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !third_lifetime; }));
  EXPECT_TRUE(FlyWindowController::FromBrowser(first));
  EXPECT_FALSE(App().closing());
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
    Activate(target);
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
    const auto bounds = Window(target).widget()->GetWindowBoundsInScreen();
    EXPECT_EQ(bounds.x(), native_window->FindInt("left"));
    EXPECT_EQ(bounds.y(), native_window->FindInt("top"));
    EXPECT_EQ(bounds.width(), native_window->FindInt("width"));
    EXPECT_EQ(bounds.height(), native_window->FindInt("height"));
  }
}

// A browser's bounds are its own OS window, sidebar and footer included; the
// page is narrower. Other workspaces' windows keep theirs.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       BrowserBoundsAreTheWindowNotThePageViewport) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto* widget = Window(first).widget();
  EXPECT_EQ(widget->GetWindowBoundsInScreen(), first->GetWindow()->GetBounds());
  EXPECT_EQ(widget->GetRestoredBounds(),
            first->GetWindow()->GetRestoredBounds());
  const auto second_bounds = second->GetWindow()->GetBounds();
  const gfx::Rect requested(64, 70, 1000, 700);
  first->GetWindow()->SetBounds(requested);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return widget->GetWindowBoundsInScreen() == requested; }));
  EXPECT_EQ(requested, first->GetWindow()->GetBounds());
  EXPECT_EQ(second_bounds, second->GetWindow()->GetBounds());
  auto* view = Active(first)->GetRenderWidgetHostView();
  ASSERT_TRUE(view);
  EXPECT_LT(view->GetViewBounds().width(), requested.width());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspaceWindowChoicesRememberAndDropClosedModels) {
  auto* first = browser();
  auto& window = Window();
  base::test::TestFuture<BrowserWindowInterface*> future;
  BrowserWindowCreateParams params(*first->GetProfile(), true);
  params.should_trigger_session_restore = false;
  CreateBrowserWindow(std::move(params), future.GetCallback());
  auto* second = future.Get();
  ASSERT_TRUE(second);
  chrome::NewTab(second, NewTabTypes::kNewTabCommand);
  Activate(second);
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
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(second); }));
  ASSERT_TRUE(click(u"Window 1 ·", true));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(first); }));
  ASSERT_TRUE(click(other_name));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(other); }));
  ASSERT_TRUE(click(first_name));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(first); }));
  // A closed window leaves the other windows' lists.
  first->GetWindow()->Close();
  auto* remaining = Window(second).workspace_buttons_for_testing();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return remaining->children().size() == 2; }));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    ExtensionCreatedNormalWindowOpensWithFlyAndPicker) {
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
  auto& window = Window();
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(second); }));
  EXPECT_EQ(first->GetProfile(), second->GetProfile());
  EXPECT_NE(first->GetSessionID(), second->GetSessionID());
  // Another window of the same workspace, with its own Fly interface.
  EXPECT_NE(window.widget()->GetNativeWindow()->GetHost(),
            second->GetWindow()->GetNativeWindow()->GetHost());
  EXPECT_TRUE(FlyWindowController::FromBrowser(second));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 3;
  }));
  auto* choice = static_cast<views::Button*>(
      window.workspace_buttons_for_testing()->children()[1]);
  views::test::ButtonTestApi(choice).NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(first); }));
  second->GetWindow()->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.workspace_buttons_for_testing()->children().size() == 1;
  }));
  EXPECT_TRUE(IsActive(first));
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    ExtensionBackgroundWindowDoesNotReplaceVisibleWorkspace) {
  auto* first = browser();
  auto& window = Window();
  Activate(first);
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
  EXPECT_TRUE(IsActive(first));
  EXPECT_FALSE(second->GetWindow()->IsActive());
  // It opens as its own window behind the active one.
  ExpectVisible(Active(first), true);
  ExpectVisible(Active(second), true);
  auto* choice = static_cast<views::Button*>(
      window.workspace_buttons_for_testing()->children()[2]);
  views::test::ButtonTestApi(choice).NotifyDefaultMouseClick();
  // Whether the window manager then focuses it is its own policy.
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(second); }));
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
    Activate(second);
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
            if (FlyWindowController::ShouldDecorate(model) &&
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
    // Every restored window is its own top-level window with Fly's interface.
    std::set<aura::WindowTreeHost*> hosts;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          if (!FlyWindowController::ShouldDecorate(model) ||
              model->GetProfile()->IsOffTheRecord())
            return true;
          EXPECT_TRUE(FlyWindowController::FromBrowser(model));
          EXPECT_TRUE(
              hosts.insert(model->GetWindow()->GetNativeWindow()->GetHost())
                  .second);
          const auto directory =
              model->GetProfile()->GetPath().BaseName().AsUTF8Unsafe();
          if (profiles.insert(directory).second) {
            Activate(model);
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
  // The other workspace's page is a background tab of its window.
  ASSERT_TRUE(OpenNativeBlankTab(second));
  Activate(first);
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
  second->GetTabStripModel()->ActivateTabAt(
      second->GetTabStripModel()->GetIndexOfWebContents(other));
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(other); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(other));
  EXPECT_EQ(73, content::EvalJs(other, "window.otherWorkspaceDraft"));
}

IN_PROC_BROWSER_TEST_F(
    FlyNativeFreezeBrowserTest,
    AutomaticSuspendUsesQuietDeadlineAndHonorsDisabledChoice) {
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(0));
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
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(15));
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(0));
  ASSERT_TRUE(base::test::RunUntil([&] { return !frozen(); }));
  EXPECT_FALSE(NativeTabSuspensionRequested(background));
  EXPECT_EQ(99, content::EvalJs(background, "window.autoSuspendDraft"));
  // Disabling the automatic timeout preserves an explicit manual request.
  ASSERT_TRUE(RequestNativeTabSuspension(background));
  ASSERT_TRUE(base::test::RunUntil(frozen));
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(15));
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(0));
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
  auto& window = Window();
  ASSERT_TRUE(App().preferences().SetSuspendTimeoutMinutes(0));
  const auto keyboard_before = App().commands().Read();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  editor.SetBindingsForTesting("spawn-tab", false, u"Ctrl+Alt+Y");
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
  EXPECT_EQ(u"Off", timeout->GetText());
  ui::test::EventGenerator input(
      window.widget()->GetNativeWindow()->GetRootWindow());
  // The select changes value with the arrow keys, without opening its list.
  timeout->RequestFocus();
  ASSERT_TRUE(timeout->HasFocus());
  input.PressKey(ui::VKEY_DOWN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return App().preferences().ReadSuspendTimeoutMinutes() == 5; }));
  input.PressKey(ui::VKEY_DOWN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return App().preferences().ReadSuspendTimeoutMinutes() == 15; }));
  EXPECT_EQ(keyboard_before, App().commands().Read());
  auto* prefs = g_browser_process->local_state();
  const auto valid = prefs->GetDict(kPreferencesPref).Clone();
  auto future = valid.Clone();
  future.Set("schema", 99);
  prefs->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(timeout->GetEnabled());
  timeout->Choose(0);
  EXPECT_EQ(future, prefs->GetDict(kPreferencesPref));
  prefs->SetDict(kPreferencesPref, valid.Clone());
  EXPECT_TRUE(timeout->GetEnabled());
  editor.ShowKeyboard();
  EXPECT_EQ(u"Ctrl+Alt+Y", editor.bindings_for_testing("spawn-tab", false));
  EXPECT_EQ(keyboard_before, App().commands().Read());
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
  // The playing page is a background tab of its own window.
  ASSERT_TRUE(OpenNativeBlankTab(first));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
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
  EXPECT_EQ(second, LastActive());
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
  auto& window = Window();
  Activate(browser());
  contents->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetRenderWidgetHostView()->HasFocus() &&
           content::EvalJs(contents, "document.hasFocus()") == true;
  }));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  auto& window = Window();
  App().CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected != first && selected != second;
  }));
  auto* third = LastActive();
  WorkspaceService service(*g_browser_process->local_state());
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(3u, before->items.size());
  const auto third_item = before->items.back();
  ASSERT_EQ(third->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
            third_item.profile_directory);
  Activate(first);
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
        window.widget()->GetNativeWindow()->GetRootWindow());
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
  EXPECT_EQ(first, LastActive());
  EXPECT_EQ(before->items[0], service.Read()->items[0]);
  EXPECT_EQ(third_item, service.Read()->items[1]);
  EXPECT_EQ(before->items[1], service.Read()->items[2]);
  EXPECT_EQ(original_page, Active(first));
  EXPECT_EQ("order=first", content::EvalJs(original_page, "document.cookie"));
  ASSERT_TRUE(Window(first).RunCommand("next-workspace"));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(third); }));
  Activate(first);
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
  EXPECT_EQ(first, LastActive());
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
  auto& window = Window();
  Activate(first);
  WorkspaceService service(*g_browser_process->local_state());
  const auto before = service.Read();
  ASSERT_TRUE(before);
  ASSERT_EQ(2u, before->items.size());
  const auto item = before->items.back();
  const auto directory = second->GetProfile()->GetPath();
  const auto tab_id = TabIdentity::Get(Active(second));
  auto open = [&] {
    // Dialog closure and workspace refresh post widget/layout work. Drain it
    // before finding the current row and injecting a pointer event at it.
    Activate(first);
    base::RunLoop().RunUntilIdle();
    window.widget()->LayoutRootViewIfNecessary();
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
    // The sidebar is a separate control widget embedded in the frame.
    ASSERT_TRUE(row->GetWidget());
    row->GetWidget()->LayoutRootViewIfNecessary();
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
        window.widget()->GetNativeWindow()->GetRootWindow());
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
  EXPECT_EQ(first, LastActive());
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
  EXPECT_EQ(first, LastActive());
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
  dialog->Close();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.workspace_rename_dialog_for_testing();
  }));
  EXPECT_EQ(*valid.FindList("items"),
            *prefs->GetDict(kWorkspacesPref).FindList("items"));
  // Using the workspace's window makes it the active workspace.
  Activate(second);
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
  auto& window = Window(source);
  ASSERT_TRUE(window.ShowTabMovePicker(contents));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return window.tab_move_picker().active(); }));
  window.RunCommand("toggle-dashboard");
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !window.tab_move_picker().active() &&
           window.workspace_picker().active();
  }));
  window.workspace_picker().Cancel();
  ChooseMoveDestination(source, catalog->items.front().name, true);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !source_weak && !App().tab_move_pending() &&
           LastActive() == destination;
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
  Activate(source);
  const int destination_count = destination->GetTabStripModel()->count();
  ChooseMoveDestination(source, destination_name);
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  EXPECT_EQ(destination_count, destination->GetTabStripModel()->count());
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !App().tab_move_pending(); }));
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
      [&] { return !App().tab_move_pending(); }));
  ASSERT_TRUE(source_weak);
  EXPECT_EQ(source, LastActive());
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
  ASSERT_TRUE(Window(normal_source).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() != normal_source; }));
  auto* private_source = LastActive();
  ASSERT_TRUE(private_source && private_source->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      private_source, embedded_test_server()->GetURL("/title1.html")));
  auto* source = Active(private_source);
  auto source_weak = source->GetWeakPtr();
  const auto id = TabIdentity::Get(source);
  ASSERT_TRUE(content::ExecJs(source, "document.cookie='move=private-source'"));
  Activate(browser());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(browser()),
                              "document.cookie='move=normal-destination'"));
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() != browser(); }));
  auto* destination = LastActive();
  ASSERT_TRUE(destination && destination->GetProfile()->IsOffTheRecord());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      destination, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(content::ExecJs(Active(destination),
                              "document.cookie='move=private-destination'"));
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  Activate(private_source);
  ChooseMoveDestination(private_source,
                        "Private · " + catalog->items.front().name);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !source_weak && !App().tab_move_pending() &&
           LastActive() == destination;
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
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !source_weak && !App().tab_move_pending(); }));
  auto* target = LastActive();
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
  ASSERT_TRUE(Window(normal_source).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() != normal_source; }));
  auto* source = LastActive();
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
      [&] { return !source_weak && !App().tab_move_pending(); }));
  auto* destination = LastActive();
  ASSERT_TRUE(destination && destination->GetProfile()->IsOffTheRecord());
  EXPECT_EQ(path, destination->GetProfile()->GetOriginalProfile()->GetPath());
  auto* moved = Active(destination);
  ASSERT_TRUE(moved);
  ASSERT_TRUE(content::WaitForLoadStop(moved));
  EXPECT_EQ(id, TabIdentity::Get(moved));
  EXPECT_EQ("", content::EvalJs(moved, "document.cookie"));
  EXPECT_FALSE(TabIdentity::CanPersist(moved));
  EXPECT_EQ(before->items, service.Read()->items);
  // A private window never becomes the active workspace.
  EXPECT_NE(target->id, service.Read()->active_id);
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
  auto& window = Window();
  ASSERT_TRUE(Window(second).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() != second; }));
  auto* private_browser = LastActive();
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
  Activate(first);
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
      [&] { return !App().workspace_removal_pending(); }));
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
    return !App().workspace_removal_pending() &&
           service.Read()->items.size() == 1 && !second_weak && !private_weak;
  }));
  EXPECT_EQ(first, LastActive());
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
  auto& window = Window();
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
  EXPECT_FALSE(App().workspace_removal_pending());
  prefs->SetDict(kWorkspacesPref, valid.Clone());
  dialog->Close();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.workspace_remove_dialog_for_testing(); }));
  // The UI keeps the rule that the last workspace cannot be removed.
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
            window.widget()->GetNativeWindow()->GetRootWindow());
        escape.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
      }));
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(row->GetBoundsInScreen().CenterPoint());
  mouse.ClickRightButton();
  EXPECT_EQ(1u, service.Read()->items.size());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspaceProfileCompletionRejectsRetiredAndFutureCatalogs) {
  auto* first = browser();
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
  App().CreateWorkspace();
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
  EXPECT_EQ(first, LastActive());
  ASSERT_TRUE(service.Read());
  EXPECT_EQ(before->items, service.Read()->items);
  EXPECT_FALSE(service.Add(retired.name, retired.profile_directory));

  App().CreateWorkspace();
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
  EXPECT_EQ(first, LastActive());
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
  auto& window = Window();
  WorkspaceService service(*g_browser_process->local_state());
  const auto catalog = service.Read();
  ASSERT_TRUE(catalog);
  const auto item = std::ranges::find(
      catalog->items, second->GetProfile()->GetPath().BaseName().AsUTF8Unsafe(),
      &Workspace::profile_directory);
  ASSERT_NE(item, catalog->items.end());
  ASSERT_TRUE(service.Rename(item->id, "Trabajo"));
  Activate(first);
  original_page->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return original_page->GetRenderWidgetHostView()->HasFocus() &&
           content::EvalJs(original_page, "document.hasFocus()",
                           content::EXECUTE_SCRIPT_NO_USER_GESTURE) == true;
  }));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  auto& picker = window.workspace_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  ExpectVisible(original_page, true);
  EXPECT_TRUE(picker.search_for_testing()->HasFocus());
  auto* root = window.widget()->GetNativeWindow()->GetRootWindow();
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
           LastActive() == second;
  }));
  // The other workspace has its own window; this one keeps its page.
  EXPECT_EQ(original_page, Active(first));

  Activate(first);
  window.RunCommand("toggle-dashboard");
  ASSERT_TRUE(picker.active());
  picker.search_for_testing()->SetText(u"Trabajo");
  picker.ContentsChanged(picker.search_for_testing(), u"Trabajo");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  ui::KeyEvent enter(ui::EventType::kKeyPressed, ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(picker.HandleOverlayKey(enter));
  Activate(second);
  Activate(first);
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_EQ(first, LastActive());

  ASSERT_TRUE(Window(first).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() != first;
  }));
  auto* private_browser = LastActive();
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  Activate(second);
  auto& second_window = Window(second);
  auto& second_picker = second_window.workspace_picker();
  second_window.RunCommand("toggle-dashboard");
  ASSERT_TRUE(second_picker.active());
  second_picker.search_for_testing()->SetText(u"Private");
  second_picker.ContentsChanged(second_picker.search_for_testing(), u"Private");
  ASSERT_EQ(1u, second_picker.result_count_for_testing());
  // A sidebar refresh while the dashboard is open keeps its window rows.
  App().BroadcastWorkspaceRefresh();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(1u, second_picker.result_count_for_testing());
  ui::test::EventGenerator second_keys(
      second_window.widget()->GetNativeWindow()->GetRootWindow());
  second_keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !second_picker.search_for_testing() &&
           LastActive() == private_browser;
  }));
  EXPECT_EQ(catalog->items.size(), service.Read()->items.size());
}

// The dashboard creates a workspace in a window of its own, with one new tab
// and no placeholder tab in the window it was opened from.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       WorkspacePickerCreatesWorkspaceInItsOwnWindow) {
  auto& window = Window();
  const auto original_directory = browser()->GetProfile()->GetPath();
  const int original_count = browser()->GetTabStripModel()->count();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_W, ui::EF_CONTROL_DOWN | ui::EF_SHIFT_DOWN);
  auto& picker = window.workspace_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  picker.search_for_testing()->SetText(u"New workspace");
  picker.ContentsChanged(picker.search_for_testing(), u"New workspace");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  // Fly asks for the name first.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.workspace_name_dialog_for_testing() != nullptr; }));
  window.workspace_name_dialog_for_testing()->AcceptDialog();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* active = LastActive();
    return !picker.search_for_testing() && active && active != browser();
  }));
  auto* created = LastActive();
  EXPECT_NE(original_directory, created->GetProfile()->GetPath());
  EXPECT_NE(browser()->GetWindow()->GetNativeWindow()->GetHost(),
            created->GetWindow()->GetNativeWindow()->GetHost());
  ASSERT_EQ(1, created->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(created)));
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(created)->GetLastCommittedURL());
  EXPECT_EQ(created->GetProfile(), Active(created)->GetBrowserContext());
  EXPECT_EQ(original_count, browser()->GetTabStripModel()->count());
}

// Closing a workspace's last tab closes its window; opening the workspace
// again opens a new window with one new tab in the same profile.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ReopeningAClosedWorkspaceOpensANewWindow) {
  auto* first = browser();
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  // The profile can unload with its last window; keep only its path.
  const auto path = second->GetProfile()->GetPath();
  const auto workspace = App().WorkspaceFor(second->GetProfile());
  ASSERT_TRUE(workspace);
  while (second->GetTabStripModel()->count() > 1)
    second->GetTabStripModel()->CloseWebContentsAt(
        0, TabCloseTypes::CLOSE_USER_GESTURE);
  // A lone new tab page is not worth reopening; a visited page is.
  const auto visited = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, visited));
  auto second_lifetime = second->GetWeakPtr();
  ASSERT_TRUE(ExecuteNativeCommand(second, "remove-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !second_lifetime; }));
  ASSERT_TRUE(base::test::RunUntil([&] { return IsActive(first); }));
  EXPECT_FALSE(App().closing());
  // The sidebar's workspace list reopens it.
  ASSERT_TRUE(Window(first).RunCommand("next-workspace"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* active = LastActive();
    return active && active != first &&
           active->GetProfile()->GetPath() == path;
  }));
  auto* reopened = LastActive();
  ASSERT_EQ(1, reopened->GetTabStripModel()->count());
  ASSERT_TRUE(content::WaitForLoadStop(Active(reopened)));
  EXPECT_EQ(GURL(kFlyNewTabURL), Active(reopened)->GetLastCommittedURL());
  // A closed tab of the closed window can be reopened from the new one.
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return NativeCommandAvailable(reopened, "reopen-closed-tab"); }));
  ASSERT_TRUE(InvokeNativeCommand(reopened, "reopen-closed-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    bool found = false;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* model) {
          auto* tab = model->GetTabStripModel()->GetActiveWebContents();
          found |= model->GetProfile()->GetPath() == path && tab &&
                   tab->GetVisibleURL() == visited;
          return true;
        });
    return found;
  }));
}

// Settings tools open in another Fly window of the same workspace.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       StoreSettingsLinksAlwaysOpenAnotherWindow) {
  auto& window = Window();
  auto* original = browser();
  const auto page = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(original, page));
  for (const auto& [label, url] :
       {std::pair{u"Browse Chrome Web Store",
                  "https://chromewebstore.google.com/"},
        std::pair{u"Manage in chrome://extensions", "chrome://extensions/"}}) {
    ASSERT_TRUE(Window(original).RunCommand("toggle-help"));
    window.keyboard_editor().ShowExtensions();
    auto created = std::make_unique<BrowserCreatedWaiter>();
    ClickPreferencesButton(label);
    auto* tool = created->Wait();
    created.reset();
    ASSERT_TRUE(tool);
    EXPECT_EQ(original->GetProfile(), tool->GetProfile());
    EXPECT_EQ(BrowserWindowInterface::TYPE_NORMAL, tool->GetType());
    EXPECT_TRUE(FlyWindowController::FromBrowser(tool));
    EXPECT_NE(window.widget()->GetNativeWindow()->GetHost(),
              tool->GetWindow()->GetNativeWindow()->GetHost());
    EXPECT_EQ(GURL(url), Active(tool)->GetVisibleURL());
    EXPECT_EQ(1, original->GetTabStripModel()->count());
    EXPECT_EQ(page, Active(original)->GetLastCommittedURL());
    auto lifetime = tool->GetWeakPtr();
    tool->GetWindow()->Close();
    ASSERT_TRUE(base::test::RunUntil([&] { return !lifetime; }));
  }
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
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive()
        ->GetProfile()
        ->IsOffTheRecord();
  }));
  auto* private_browser = LastActive();
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
  auto& window = Window(second);
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
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           page->GetLastCommittedURL() == first_url;
  }));
  EXPECT_EQ(second, LastActive());
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
  auto& window = Window(second);
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
                       BookmarksExcludeFrequentSitesAndHonorLiveEdits) {
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  // Ensure this visit is eligible for frequent sites before opening bookmarks.
  base::CancelableTaskTracker tracker;
  base::test::TestFuture<history::QueryResults> flushed;
  HistoryServiceFactory::GetForProfile(browser()->GetProfile(),
                                       ServiceAccessType::EXPLICIT_ACCESS)
      ->QueryHistory({}, history::QueryOptions(), flushed.GetCallback(), &tracker);
  ASSERT_FALSE(flushed.Get().empty());
  auto& window = Window();
  ASSERT_TRUE(App().preferences().SetFrequentSitesVisible(true));
  auto& picker = window.bookmark_picker();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.search_for_testing()->HasFocus();
  }));
  EXPECT_EQ(0u, picker.result_count_for_testing());
  ASSERT_TRUE(App().preferences().SetFrequentSitesVisible(false));
  EXPECT_EQ(0u, picker.result_count_for_testing());
  ASSERT_TRUE(App().preferences().SetFrequentSitesVisible(true));
  EXPECT_EQ(0u, picker.result_count_for_testing());
  ASSERT_TRUE(App().bookmarks().Add(url.spec(), "Saved visit"));
  ASSERT_EQ(1u, picker.result_count_for_testing());
  picker.search_for_testing()->SetText(u"Frequent site");
  picker.ContentsChanged(picker.search_for_testing(), u"Frequent site");
  EXPECT_EQ(0u, picker.result_count_for_testing());
  picker.search_for_testing()->SetText(u"Saved visit");
  picker.ContentsChanged(picker.search_for_testing(), u"Saved visit");
  ASSERT_EQ(1u, picker.result_count_for_testing());
  const int count = browser()->tab_strip_model()->count();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return !picker.search_for_testing() &&
           browser()->tab_strip_model()->count() == count + 1;
  }));
  content::WaitForLoadStop(Active(browser()));
  EXPECT_EQ(url, Active(browser())->GetLastCommittedURL());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.result_count_for_testing() == 1;
  }));
  ASSERT_TRUE(picker.remove_for_testing(0));
  views::test::ButtonTestApi(picker.remove_for_testing(0))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return picker.active() && picker.result_count_for_testing() == 0;
  }));
  EXPECT_TRUE(App().bookmarks().Read()->empty());
  // Removing a bookmark must not hide the origin from frequent sites.
  FrequentSitesService sites(*g_browser_process->local_state());
  const auto frequent = sites.Read({{url.spec(), base::Time::Now()}});
  ASSERT_TRUE(frequent);
  ASSERT_EQ(1u, frequent->size());
  EXPECT_EQ(url::Origin::Create(url).Serialize(), frequent->front().url);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeSettingsMenuEditsStartupAndSidebarSettings) {
  auto& window = Window();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-menu-bar"));
  auto* menu = window.settings_menu().model_for_testing();
  ASSERT_TRUE(menu);
  EXPECT_GT(menu->GetItemCount(), 15u);
  EXPECT_FALSE(window.settings_menu().IsCommandIdChecked(1));
  window.settings_menu().ExecuteCommand(1, 0);
  EXPECT_EQ(true, App().preferences().ReadBool("disable_gpu"));
  window.settings_menu().ExecuteCommand(102, 0);
  EXPECT_EQ(2, App().preferences().ReadInt("process_limit"));
  EXPECT_TRUE(window.settings_menu().IsCommandIdChecked(102));
  window.settings_menu().ExecuteCommand(230, 0);
  EXPECT_EQ(30, App().preferences().ReadSuspendTimeoutMinutes());
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
  EXPECT_EQ(false, FlyApp::Get().preferences().ReadBool(
                       "web_notifications"));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, setting(browser()));
  EXPECT_EQ(CONTENT_SETTING_BLOCK, setting(second));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-notifications"));
  EXPECT_EQ(CONTENT_SETTING_ASK, setting(browser()));
  EXPECT_EQ(CONTENT_SETTING_ASK, setting(second));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeSpellingLanguagesReachEveryWorkspaceInOrder) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  const auto dictionaries = [](BrowserWindowInterface* window) {
    std::vector<std::string> result;
    for (const auto& value : window->GetProfile()->GetPrefs()->GetList(
             spellcheck::prefs::kSpellCheckDictionaries))
      result.push_back(value.GetString());
    return result;
  };
  const auto enabled = [](BrowserWindowInterface* window) {
    return window->GetProfile()->GetPrefs()->GetBoolean(
        spellcheck::prefs::kSpellCheckEnable);
  };
  const auto chosen = [&] {
    const auto spellcheck = App().preferences().ReadSpellcheck();
    return spellcheck ? spellcheck->languages : std::vector<std::string>();
  };
  using Codes = std::vector<std::string>;
  ASSERT_TRUE(App().preferences().SetSpellcheck(
      {.enabled = true, .languages = {"en-GB"}}));
  EXPECT_EQ(Codes{"en-GB"}, dictionaries(browser()));
  EXPECT_EQ(Codes{"en-GB"}, dictionaries(second));

  auto& window = Window();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  editor.ShowSearch();
  ASSERT_TRUE(editor.spelling_add_for_testing());
  editor.AddSpellingLanguageForTesting("es");
  EXPECT_EQ((Codes{"en-GB", "es"}), chosen());
  EXPECT_EQ((Codes{"en-GB", "es"}), dictionaries(browser()));
  EXPECT_EQ((Codes{"en-GB", "es"}), dictionaries(second));
  EXPECT_EQ((std::vector<std::u16string>{u"English (UK)", u"Español"}),
            editor.spelling_languages_for_testing());
  EXPECT_FALSE(editor.spelling_button_for_testing("up", "en-GB")->GetEnabled());
  // Up puts Spanish first, where Chromium checks it first.
  views::test::ButtonTestApi(editor.spelling_button_for_testing("up", "es"))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return dictionaries(second) == Codes{"es", "en-GB"}; }));
  EXPECT_EQ((Codes{"es", "en-GB"}), dictionaries(browser()));
  EXPECT_EQ((std::vector<std::u16string>{u"Español", u"English (UK)"}),
            editor.spelling_languages_for_testing());
  for (const char* code : {"fr", "de", "it"})
    editor.AddSpellingLanguageForTesting(code);
  EXPECT_EQ((Codes{"es", "en-GB", "fr", "de", "it"}), chosen());
  EXPECT_FALSE(editor.spelling_add_for_testing()->GetEnabled());
  editor.AddSpellingLanguageForTesting("pt-BR");
  EXPECT_EQ((Codes{"es", "en-GB", "fr", "de", "it"}), chosen());
  views::test::ButtonTestApi(editor.spelling_button_for_testing("remove", "fr"))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return dictionaries(browser()) == Codes{"es", "en-GB", "de", "it"};
  }));
  EXPECT_TRUE(editor.spelling_add_for_testing()->GetEnabled());
  auto* toggle = editor.appearance_for_testing("spellcheck_enabled");
  ASSERT_TRUE(toggle && toggle->GetChecked());
  views::test::ButtonTestApi(toggle).NotifyDefaultMouseClick();
  EXPECT_FALSE(enabled(browser()));
  EXPECT_FALSE(enabled(second));
  EXPECT_EQ(false, App().preferences().ReadSpellcheck()->enabled);

  // A change made in one workspace outside Preferences, as Chromium's context
  // menu makes it, becomes Fly's choice for the others.
  second->GetProfile()->GetPrefs()->SetBoolean(
      spellcheck::prefs::kSpellCheckEnable, true);
  ASSERT_TRUE(base::test::RunUntil([&] { return enabled(browser()); }));
  EXPECT_EQ(true, App().preferences().ReadSpellcheck()->enabled);
  EXPECT_TRUE(toggle->GetChecked());
  base::ListValue single;
  single.Append("en-US");
  second->GetProfile()->GetPrefs()->SetList(
      spellcheck::prefs::kSpellCheckDictionaries, std::move(single));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return dictionaries(browser()) == Codes{"en-US"}; }));
  EXPECT_EQ(Codes{"en-US"}, chosen());
  EXPECT_EQ(std::vector<std::u16string>{u"English (US)"},
            editor.spelling_languages_for_testing());
  // Languages Fly does not offer, and more than five, are written over.
  base::ListValue many;
  for (const char* code : {"en-US", "xx", "es-ES", "fr", "de", "it", "pt-BR"})
    many.Append(code);
  second->GetProfile()->GetPrefs()->SetList(
      spellcheck::prefs::kSpellCheckDictionaries, std::move(many));
  const Codes five{"en-US", "es", "fr", "de", "it"};
  ASSERT_TRUE(base::test::RunUntil([&] {
    return dictionaries(second) == five && dictionaries(browser()) == five;
  }));
  EXPECT_EQ(five, chosen());
  editor.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  EXPECT_EQ(nullptr, editor.spelling_add_for_testing());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeDownloadsAndDiagnosticsSurfacesOpenAndClose) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = Window();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-downloads"));
  auto& picker = window.downloads_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  EXPECT_EQ(1u, picker.result_count_for_testing());
  picker.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  // Diagnostics & Health Hub: system, processes, log and report.
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

// The dashboard: each workspace with its tabs (pinned marked) and
// rename/delete buttons; choosing a tab opens its workspace on that tab.
// Dragging a tab onto another tab of its workspace reorders them.
// Popups belong to Fly's window (WM_TRANSIENT_FOR), so tiling window managers
// float them.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, PopupsAreTransientForFly) {
  if (ui::OzonePlatform::GetPlatformNameForTest() != "x11")
    GTEST_SKIP() << "X11 only";
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  BrowserWindowInterface* popup = nullptr;
  ASSERT_TRUE(content::ExecJs(
      Active(browser()),
      "window.open('/title2.html', 'fly-popup', 'popup,width=320,height=240')"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    ForEachCurrentBrowserWindowInterfaceOrderedByActivation(
        [&](BrowserWindowInterface* candidate) {
          if (candidate->GetType() == BrowserWindowInterface::TYPE_POPUP)
            popup = candidate;
          return true;
        });
    return popup != nullptr;
  }));
  auto* widget = BrowserView::GetBrowserViewForBrowser(popup)->GetWidget();
  x11::Window owner = x11::Window::None;
  ASSERT_TRUE(x11::Connection::Get()->GetPropertyAs(
      static_cast<x11::Window>(
          widget->GetNativeWindow()->GetHost()->GetAcceleratedWidget()),
      x11::Atom::WM_TRANSIENT_FOR, &owner));
  EXPECT_EQ(static_cast<x11::Window>(Window().widget()
                                         ->GetNativeWindow()
                                         ->GetHost()
                                         ->GetAcceleratedWidget()),
            owner);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, DashboardDragReordersTabs) {
  auto& window = Window();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      browser(), embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  auto* model = browser()->GetTabStripModel();
  auto* first = model->GetWebContentsAt(0);
  auto* second = model->GetWebContentsAt(1);
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-dashboard"));
  auto& picker = window.workspace_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return picker.active(); }));
  // Rows: the workspace, then its two tabs.
  auto* from = picker.row_for_testing(2);
  auto* to = picker.row_for_testing(1);
  ASSERT_TRUE(from && to);
  views::test::RunScheduledLayout(from->GetWidget());
  ui::test::EventGenerator mouse(
      window.widget()->GetNativeWindow()->GetRootWindow());
  mouse.MoveMouseTo(from->GetBoundsInScreen().CenterPoint());
  mouse.PressLeftButton();
  mouse.MoveMouseBy(0, -10);
  mouse.MoveMouseTo(to->GetBoundsInScreen().CenterPoint());
  mouse.ReleaseLeftButton();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return model->GetWebContentsAt(0) == second &&
           model->GetWebContentsAt(1) == first;
  }));
  EXPECT_TRUE(picker.active());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       DashboardListsWorkspaceTabsAndOpensThem) {
  auto& window = Window();
  auto* original = browser();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      original, embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(ui_test_utils::NavigateToURLWithDisposition(
      original, embedded_test_server()->GetURL("/title2.html"),
      WindowOpenDisposition::NEW_FOREGROUND_TAB,
      ui_test_utils::BROWSER_TEST_WAIT_FOR_LOAD_STOP));
  original->GetTabStripModel()->SetTabPinned(1, true);
  App().CreateWorkspace("Second");
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
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
  // The other workspace is listed; this window's own one is marked active.
  EXPECT_EQ("Second", titles[3]);
  EXPECT_EQ(entries[1].action, pinned_action);
  EXPECT_EQ(2u, entries[0].row_actions.size());
  EXPECT_EQ(1, entries[2].indent);
  window.choose_workspace_for_testing(entries[2].action);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() == original; }));
  EXPECT_EQ(1, original->GetTabStripModel()->active_index());
}

// Site zoom changes by 0.5 levels per step and one level per hostname is shared
// by every normal workspace, whatever path changed it.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SiteZoomUsesHalfLevelStepsAndIsSharedByWorkspaces) {
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
  App().CreateWorkspace();
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected != original &&
           selected->GetTabStripModel()->count() == 1;
  }));
  auto* second = LastActive();
  // A workspace created later takes the shared levels.
  EXPECT_DOUBLE_EQ(1.0, level(second));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, page));
  ASSERT_TRUE(InvokeNativeCommand(second, "zoom-out"));
  EXPECT_DOUBLE_EQ(0.5, level(second));
  EXPECT_DOUBLE_EQ(0.5, level(original));
  ASSERT_TRUE(InvokeNativeCommand(second, "reset-zoom"));
  EXPECT_DOUBLE_EQ(0.0, level(original));
}

// The reader (readerDistiller.js) opens over the article with its toolbar,
// lights the reader button, and closes on the second toggle and on Escape.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ReaderViewOpensOverTheArticleAndCloses) {
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
  auto& window = Window();
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
  auto& extensions = App().extensions();
  auto loaded = [&](BrowserWindowInterface* browser) {
    return extensions::ExtensionRegistry::Get(browser->GetProfile())
               ->enabled_extensions()
               .GetByID(kId) != nullptr;
  };
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded(browser()); }));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return loaded(second); }));
  auto& window = Window(second);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.extension_actions_for_testing()->children().size() == 1u;
  }));
  // A tab or action change updates the button in place: a blocker updates
  // its count on every request.
  const auto* button = window.extension_actions_for_testing()->children()[0].get();
  auto* contents = Active(second);
  auto* blocker = extensions::ExtensionRegistry::Get(second->GetProfile())
                      ->enabled_extensions()
                      .GetByID(kId);
  auto* action = extensions::ExtensionActionManager::Get(second->GetProfile())
                     ->GetExtensionAction(*blocker);
  ASSERT_TRUE(action);
  action->SetBadgeText(extensions::ExtensionTabUtil::GetTabId(contents), "7");
  extensions::ExtensionActionDispatcher::Get(second->GetProfile())
      ->NotifyChange(action, contents, second->GetProfile());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.extension_badge_for_testing(0) == "7"; }));
  EXPECT_EQ(button, window.extension_actions_for_testing()->children()[0].get());
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
                       LocalExtensionsLandInTheExtensionsFolderAndReplace) {
  auto& extensions = FlyApp::Get().extensions();
  auto* profile = browser()->GetProfile();
  // Outlives `sources`, whose destructor deletes files.
  base::ScopedAllowBlockingForTesting allow_temp_dir;
  base::ScopedTempDir sources;
  auto write = [&](const base::FilePath& dir, std::string_view name,
                   std::string_view version) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::CreateDirectory(dir));
    ASSERT_TRUE(base::WriteFile(
        dir.AppendASCII("manifest.json"),
        base::StrCat({"{\"name\":\"", name, "\",\"version\":\"", version,
                      "\",\"manifest_version\":3}"})));
  };
  auto installed = [&](std::string_view name) -> const extensions::Extension* {
    for (const auto& extension :
         extensions::ExtensionRegistry::Get(profile)->enabled_extensions()) {
      if (extension->name() == name)
        return extension.get();
    }
    return nullptr;
  };
  auto local_folders = [&] {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::vector<std::string> names;
    base::FileEnumerator folders(FlyExtensions::Folder(), false,
                                 base::FileEnumerator::DIRECTORIES);
    for (auto path = folders.Next(); !path.empty(); path = folders.Next())
      names.push_back(path.BaseName().AsUTF8Unsafe());
    return names;
  };
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(sources.CreateUniqueTempDir());
  }
  // A folder goes to local-<24 hex>; loading it again replaces that copy.
  const auto folder = sources.GetPath().AppendASCII("my-extension");
  write(folder, "Fly Local Folder", "1");
  extensions.LoadUnpacked(profile, folder);
  ASSERT_TRUE(base::test::RunUntil([&] { return installed("Fly Local Folder"); }));
  const auto id = installed("Fly Local Folder")->id();
  auto names = local_folders();
  ASSERT_EQ(names.size(), 1u);
  EXPECT_EQ(names[0],
            "local-" + base::HexEncodeLower(
                           crypto::SHA256HashString(folder.AsUTF8Unsafe()))
                           .substr(0, 24));
  write(folder, "Fly Local Folder", "2");
  extensions.LoadUnpacked(profile, folder);
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto* extension = installed("Fly Local Folder");
    return extension && extension->version().GetString() == "2";
  }));
  EXPECT_EQ(installed("Fly Local Folder")->id(), id);
  EXPECT_EQ(local_folders(), names);
  // A copy Chromium rejects leaves the installed one in place.
  write(folder, "Fly Local Folder", "not-a-version");
  extensions.LoadUnpacked(profile, folder);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Window().status_text_for_testing().starts_with(
        u"The extension could not be loaded");
  }));
  EXPECT_EQ(local_folders(), names);
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string manifest;
    ASSERT_TRUE(base::ReadFileToString(FlyExtensions::Folder()
                                           .AppendASCII(names[0])
                                           .AppendASCII("manifest.json"),
                                       &manifest));
    EXPECT_NE(manifest.find("\"version\":\"2\""), std::string::npos)
        << manifest;
  }
  // So does a copy whose manifest key Chromium cannot read.
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::WriteFile(
        folder.AppendASCII("manifest.json"),
        "{\"name\":\"Fly Local Folder\",\"version\":\"3\","
        "\"manifest_version\":3,\"key\":\"not base64!\"}"));
  }
  Window().ShowStatus(u"");
  extensions.LoadUnpacked(profile, folder);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return Window().status_text_for_testing().starts_with(
        u"The extension could not be loaded");
  }));
  EXPECT_EQ(local_folders(), names);
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    std::string manifest;
    ASSERT_TRUE(base::ReadFileToString(FlyExtensions::Folder()
                                           .AppendASCII(names[0])
                                           .AppendASCII("manifest.json"),
                                       &manifest));
    EXPECT_NE(manifest.find("\"version\":\"2\""), std::string::npos)
        << manifest;
    EXPECT_EQ(manifest.find("not base64!"), std::string::npos) << manifest;
  }
  EXPECT_EQ(installed("Fly Local Folder")->version().GetString(), "2");
  // A ZIP gets its own folder from its path, and reinstalling reuses it.
  const auto zip_source = sources.GetPath().AppendASCII("zip-source");
  const auto zip = sources.GetPath().AppendASCII("package.zip");
  write(zip_source, "Fly Local Zip", "1");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(zip::Zip(zip_source, zip, false));
  }
  extensions.InstallPackage(profile, Active(browser()), zip);
  ASSERT_TRUE(base::test::RunUntil([&] { return installed("Fly Local Zip"); }));
  ASSERT_EQ(local_folders().size(), 2u);
  write(zip_source, "Fly Local Zip", "3");
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::DeleteFile(zip));
    ASSERT_TRUE(zip::Zip(zip_source, zip, false));
  }
  extensions.InstallPackage(profile, Active(browser()), zip);
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto* extension = installed("Fly Local Zip");
    return extension && extension->version().GetString() == "3";
  }));
  EXPECT_EQ(local_folders().size(), 2u);
  // An extension with a manifest key keeps one folder whatever path it is
  // installed from.
  auto write_keyed = [&](const base::FilePath& dir, std::string_view version) {
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(base::CreateDirectory(dir));
    ASSERT_TRUE(base::WriteFile(
        dir.AppendASCII("manifest.json"),
        base::StrCat({"{\"name\":\"Fly Keyed\",\"version\":\"", version,
                      "\",\"manifest_version\":3,\"key\":\"AQIDBA==\"}"})));
  };
  write_keyed(sources.GetPath().AppendASCII("keyed-a"), "1");
  extensions.LoadUnpacked(profile, sources.GetPath().AppendASCII("keyed-a"));
  ASSERT_TRUE(base::test::RunUntil([&] { return installed("Fly Keyed"); }));
  ASSERT_EQ(local_folders().size(), 3u);
  write_keyed(sources.GetPath().AppendASCII("keyed-b"), "2");
  extensions.LoadUnpacked(profile, sources.GetPath().AppendASCII("keyed-b"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    const auto* extension = installed("Fly Keyed");
    return extension && extension->version().GetString() == "2";
  }));
  EXPECT_EQ(local_folders().size(), 3u);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       UserContentReusesFilesAndRepairsDamagedGeneration) {
  auto& service = App().user_content();
  ASSERT_TRUE(base::test::RunUntil([&] { return !service.generating_for_testing(); }));
  base::FilePath user_data;
  ASSERT_TRUE(base::PathService::Get(chrome::DIR_USER_DATA, &user_data));
  const auto directory = user_data.AppendASCII("fly-user-content");
  const std::vector<std::string> names = {"background.js", "manifest.json",
      "offscreen.html", "offscreen.js", "content.js"};
  std::map<std::string, std::string> expected;
  const auto stamp = base::Time::Now() - base::Days(1);
  {
    base::ScopedAllowBlockingForTesting allow;
    for (const auto& name : names) {
      const auto file = directory.AppendASCII(name);
      ASSERT_TRUE(base::ReadFileToString(file, &expected[name]));
      ASSERT_TRUE(base::TouchFile(file, stamp, stamp));
    }
  }
  service.Reload();
  ASSERT_TRUE(base::test::RunUntil([&] { return !service.generating_for_testing(); }));
  {
    base::ScopedAllowBlockingForTesting allow;
    for (const auto& name : names) {
      base::File::Info info;
      ASSERT_TRUE(base::GetFileInfo(directory.AppendASCII(name), &info));
      EXPECT_EQ(stamp, info.last_modified) << name;
    }
    ASSERT_TRUE(base::WriteFile(directory.AppendASCII("content.js"), "corrupt"));
    ASSERT_TRUE(base::DeleteFile(directory.AppendASCII("offscreen.js")));
    ASSERT_TRUE(base::WriteFile(directory.AppendASCII("background.js"),
                               expected["background.js"] + "extra bytes"));
  }
  service.Reload();
  ASSERT_TRUE(base::test::RunUntil([&] { return !service.generating_for_testing(); }));
  {
    base::ScopedAllowBlockingForTesting allow;
    for (const auto& name : names) {
      std::string actual;
      ASSERT_TRUE(base::ReadFileToString(directory.AppendASCII(name), &actual));
      EXPECT_EQ(expected[name], actual) << name;
    }
  }
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
  App().user_content().Reload();
  // A generation that started before both files existed can finish first.
  ASSERT_TRUE(base::test::RunUntil([&] {
    return App().user_content().script_count() == 1u &&
           App().user_content().style_count() == 1u;
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
// hosts and kept off local network addresses before it is sent.
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
  App().user_content().Reload();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return App().user_content().script_count() == 2u; }));
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
            LastActive());
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
  auto& window = Window();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  editor.SetBindingsForTesting("spawn-tab", false, u"Ctrl+Y");
  editor.ShowSearch();
  auto* engine = editor.search_setting_for_testing("engine");
  ASSERT_TRUE(engine && engine->IsDrawn());
  engine->SetText(u"https://example.test/?q=%s");
  views::test::ButtonTestApi(editor.search_save_for_testing())
      .NotifyDefaultMouseClick();
  SearchService search(*g_browser_process->local_state());
  ASSERT_TRUE(search.Read());
  EXPECT_EQ("https://example.test/?q=%s", search.Read()->engine);
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
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
  ASSERT_TRUE(editor.alias_remove_for_testing("gh"));
  views::test::ButtonTestApi(editor.alias_remove_for_testing("gh"))
      .NotifyDefaultMouseClick();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return search.ResolveNavigation("gh c++") ==
           GURL("https://github.com/search?q=c%2B%2B");
  }));
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  const auto future =
      base::DictValue().Set("schema", 2).Set("values", base::DictValue());
  g_browser_process->local_state()->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(engine->GetEnabled());
  EXPECT_FALSE(editor.search_save_for_testing()->GetEnabled());
  EXPECT_FALSE(editor.alias_save_for_testing()->GetEnabled());
  views::test::ButtonTestApi(editor.search_save_for_testing())
      .NotifyDefaultMouseClick();
  EXPECT_EQ(future,
            g_browser_process->local_state()->GetDict(kPreferencesPref));
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  editor.Cancel(true);
  ASSERT_TRUE(
      base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  EXPECT_EQ(nullptr, editor.search_setting_for_testing("engine"));
  EXPECT_EQ(nullptr, editor.alias_save_for_testing());
  EXPECT_EQ(nullptr, editor.alias_remove_for_testing("gh"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeAddressAliasesNavigateWithinSelectedProfile) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  auto& window = Window(second);
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
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  EXPECT_EQ(second, LastActive());
}

IN_PROC_BROWSER_TEST_F(
    FlyWorkspaceBrowserTest,
    NativeAddressPrivateNavigationKeepsNormalStateUntouched) {
  const auto url = embedded_test_server()->GetURL("/title1.html");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), url));
  auto* normal = Active(browser());
  ASSERT_TRUE(
      content::ExecJs(normal, "document.cookie='normal=retained;path=/';"));
  const auto preferences =
      g_browser_process->local_state()->GetDict(kPreferencesPref).Clone();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return LastActive() != browser(); }));
  auto* private_browser = LastActive();
  ASSERT_TRUE(private_browser->GetProfile()->IsOffTheRecord());
  auto* page = Active(private_browser);
  ASSERT_TRUE(content::WaitForLoadStop(page));
  auto& window = Window(private_browser);
  auto& address = window.address_picker();
  if (!address.active())
    ASSERT_TRUE(InvokeNativeCommand(private_browser, "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] { return address.active(); }));
  const auto input = base::UTF8ToUTF16(url.spec());
  address.search_for_testing()->SetText(input);
  address.ContentsChanged(address.search_for_testing(), input);
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  auto& second_window = Window(second);
  auto& second_address = second_window.address_picker();
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return second_address.active() &&
           second_address.search_for_testing()->HasFocus();
  }));
  second_address.search_for_testing()->SetText(
      u"ws:Address workspace fixture");
  second_address.ContentsChanged(second_address.search_for_testing(),
                                 u"ws:Address workspace fixture");
  ASSERT_EQ(1u, second_address.result_count_for_testing());
  ui::test::EventGenerator second_keys(
      second_window.widget()->GetNativeWindow()->GetRootWindow());
  second_keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return LastActive() == browser() &&
           !second_address.search_for_testing();
  }));
  auto& window = Window();
  auto& address = window.address_picker();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  const auto target = embedded_test_server()->GetURL("/title2.html");
  ASSERT_TRUE(
      App().bookmarks().Add(target.spec(), "Address bookmark fixture"));
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
                       NativeAddressCancelDiscardsInput) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(
      content::ExecJs(page,
                      "document.body.innerHTML='<input "
                      "id=entry>';document.querySelector('#entry').focus();"));
  auto& window = Window();
  const auto original = page->GetLastCommittedURL();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  auto& address = window.address_picker();
  ASSERT_TRUE(address.active());
  address.search_for_testing()->SetText(u"never-persist-this-query");
  address.ContentsChanged(address.search_for_testing(),
                          u"never-persist-this-query");
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
}

// A surface belongs to its window and its tab. Another window or application
// taking the keyboard, or the window being resized, leaves it open with its
// text, section and focus; switching to another tab closes it.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SurfacesKeepTheirStateWhileTheirWindowIsInactive) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  ASSERT_TRUE(AddTabAtIndex(1, embedded_test_server()->GetURL("/title2.html"),
                            ui::PAGE_TRANSITION_TYPED));
  auto* model = browser()->GetTabStripModel();
  model->ActivateTabAt(0);
  auto* page = Active(browser());
  const auto original = page->GetLastCommittedURL();
  auto& window = Window();
  auto& address = window.address_picker();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(address.active());
  address.search_for_testing()->SetText(u"half-typed");
  address.ContentsChanged(address.search_for_testing(), u"half-typed");

  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_FALSE(browser()->GetWindow()->IsActive());
  EXPECT_TRUE(address.active());
  EXPECT_EQ(u"half-typed", address.search_for_testing()->GetText());

  // A tiling window manager resizes a window when another opens beside it.
  const gfx::Rect requested(64, 70, 900, 640);
  browser()->GetWindow()->SetBounds(requested);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.widget()->GetWindowBoundsInScreen() == requested;
  }));
  ASSERT_TRUE(address.active());
  EXPECT_EQ(window.widget()->GetContentsView()->GetBoundsInScreen(),
            address.search_for_testing()->GetWidget()->GetWindowBoundsInScreen());

  Activate(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return address.search_for_testing()->HasFocus(); }));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_END, ui::EF_NONE);
  keys.PressKey(ui::VKEY_X, ui::EF_NONE);
  EXPECT_EQ(u"half-typedx", address.search_for_testing()->GetText());
  EXPECT_EQ(original, page->GetLastCommittedURL());

  // Switching tabs still closes it.
  model->ActivateTabAt(1);
  ASSERT_TRUE(base::test::RunUntil([&] { return !address.active(); }));
  model->ActivateTabAt(0);

  // Preferences keeps its section and focus.
  ASSERT_TRUE(window.RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  editor.ShowPane(KeyboardEditor::kAppearancePane);
  editor.settings_search_for_testing()->RequestFocus();
  Activate(second);
  EXPECT_TRUE(editor.active());
  Activate(browser());
  ASSERT_TRUE(editor.active());
  EXPECT_EQ(KeyboardEditor::kAppearancePane,
            editor.selected_pane_for_testing());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return editor.settings_search_for_testing()->HasFocus(); }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeClockUsesLiveAppPreferencesWithoutTickWrites) {
  auto& window = Window();
  auto* clock = window.status_bar_for_testing()->clock_for_testing();
  ASSERT_TRUE(clock);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return clock->GetVisible() && !clock->GetText().empty();
  }));
  auto* state = g_browser_process->local_state();
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  views::test::ButtonTestApi(clock).NotifyDefaultMouseClick();
  ASSERT_TRUE(App().preferences().ReadClock());
  EXPECT_EQ("time", App().preferences().ReadClock()->format);
  ASSERT_TRUE(NativeCommandAvailable(browser(), "cycle-clock-format"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "cycle-clock-format"));
  EXPECT_EQ("seconds", App().preferences().ReadClock()->format);
  ASSERT_TRUE(NativeCommandAvailable(browser(), "toggle-clock"));
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-clock"));
  EXPECT_FALSE(clock->GetVisible());
  EXPECT_FALSE(App().preferences().ReadClock()->visible);
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-clock"));
  EXPECT_TRUE(clock->GetVisible());
  EXPECT_TRUE(window.clock_timer_running_for_testing());
  ASSERT_TRUE(App().preferences().SetStatusBarVisible(false));
  EXPECT_FALSE(window.clock_timer_running_for_testing());
  ASSERT_TRUE(App().preferences().SetStatusBarVisible(true));
  EXPECT_TRUE(window.clock_timer_running_for_testing());
  const auto future = base::DictValue().Set("schema", 2)
      .Set("values", base::DictValue().Set("show_clock", true));
  state->SetDict(kPreferencesPref, future.Clone());
  EXPECT_FALSE(clock->GetVisible());
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-clock"));
  EXPECT_FALSE(Window(browser()).RunCommand("toggle-clock"));
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
  auto& window = Window();
  Activate(browser());
  const auto keyboard = App().commands().Read();
  ASSERT_TRUE(keyboard);
  auto* pointer = window.preferences_button_for_testing();
  ASSERT_TRUE(pointer && pointer->GetEnabled());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.MoveMouseTo(pointer->GetBoundsInScreen().CenterPoint());
  keys.ClickLeftButton();
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  EXPECT_TRUE(editor.appearance_for_testing("theme")->IsDrawn());
  // Only the shown section exists.
  EXPECT_EQ(nullptr, editor.search_for_testing());
  auto* state = g_browser_process->local_state();
  EXPECT_EQ(nullptr, state->GetUserPrefValue(kPreferencesPref));
  editor.SetBindingsForTesting("spawn-tab", false, u"Ctrl+Y");
  for (const auto key : {"theme", "sync_website_theme", "show_clock",
                         "show_status_bar"}) {
    auto* checkbox = editor.appearance_for_testing(key);
    ASSERT_TRUE(checkbox && checkbox->GetEnabled());
    EXPECT_TRUE(checkbox->GetChecked());
    views::test::ButtonTestApi(checkbox).NotifyDefaultMouseClick();
    EXPECT_FALSE(checkbox->GetChecked());
  }
  EXPECT_EQ("light", App().preferences().ReadTheme()->mode);
  EXPECT_FALSE(App().preferences().ReadTheme()->sync_websites);
  EXPECT_FALSE(
      window.status_bar_for_testing()->clock_for_testing()->GetVisible());
  EXPECT_FALSE(window.status_bar_for_testing()->GetVisible());
  editor.clock_format_for_testing()->Choose(1);
  EXPECT_EQ("time", App().preferences().ReadClock()->format);
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  ASSERT_TRUE(App().preferences().ToggleTheme());
  EXPECT_TRUE(editor.appearance_for_testing("theme")->GetChecked());
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  ASSERT_TRUE(NativeCommandAvailable(browser(), "open-keyboard-settings"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  EXPECT_TRUE(editor.active());
  EXPECT_TRUE(editor.search_for_testing()->IsDrawn());
  EXPECT_TRUE(editor.search_for_testing()->HasFocus());
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "open-keyboard-settings"));
  EXPECT_TRUE(editor.active());
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  const auto appearance = state->GetDict(kPreferencesPref).Clone();
  // Unsaved shortcut changes take a second Escape to discard.
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_TRUE(editor.active());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  EXPECT_EQ(appearance, state->GetDict(kPreferencesPref));
  ASSERT_TRUE(App().commands().Read());
  EXPECT_EQ(keyboard->shortcuts, App().commands().Read()->shortcuts);
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
  auto& window = Window();
  auto* state = g_browser_process->local_state();
  const auto future = base::DictValue().Set("schema", 2)
      .Set("values", base::DictValue().Set("show_clock", true));
  state->SetDict(kPreferencesPref, future.Clone());
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  editor.ShowAppearance();
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
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
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
  auto& window = Window();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  // Preferences opens on General.
  editor.ShowKeyboard();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  for (auto key : {ui::VKEY_S, ui::VKEY_P, ui::VKEY_A, ui::VKEY_W, ui::VKEY_N})
    keys.PressKey(key, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return editor.search_for_testing()->GetText() == u"spawn";
  }));
  // Selecting the cell focuses it; Delete clears it.
  auto* record = editor.record_for_testing("spawn-tab", false);
  ASSERT_TRUE(record);
  keys.PressKey(ui::VKEY_DELETE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return editor.bindings_for_testing("spawn-tab", false).empty(); }));
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ASSERT_TRUE(editor.recording());
  const int tabs = browser()->GetTabStripModel()->count();
  keys.PressKey(ui::VKEY_Y, ui::EF_CONTROL_DOWN);
  EXPECT_FALSE(editor.recording());
  EXPECT_EQ(u"Ctrl+Y", editor.bindings_for_testing("spawn-tab", false));
  EXPECT_EQ(tabs, browser()->GetTabStripModel()->count());
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  // Saving keeps Preferences open; with nothing unsaved, Escape closes it.
  EXPECT_TRUE(editor.active());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  ASSERT_TRUE(App().commands().Read());
  EXPECT_EQ(std::vector<std::string>({"Ctrl+Y"}),
            App().commands().Read()->shortcuts.at("spawn-tab"));
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
  auto& window = Window();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  EXPECT_TRUE(editor.recording());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  EXPECT_FALSE(editor.recording());
  EXPECT_EQ(u"J\nQ Q", editor.bindings_for_testing("scroll-down", true));
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "Math.round(scrollY)").ExtractInt() == 80;
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecorderRetainsSemicolonBindings) {
  auto& window = Window();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_OEM_1, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.recording(); }));
  EXPECT_EQ(u"J\n;", editor.bindings_for_testing("scroll-down", true));
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
  const auto config = App().commands().Read();
  ASSERT_TRUE(config);
  EXPECT_EQ((std::vector<std::string>{"J", ";"}), config->normal.at("scroll-down"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardEditorPreservesConflictsAndConcurrentEdits) {
  auto& window = Window();
  const auto before = App().commands().Read();
  ASSERT_TRUE(before);
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("go-back", false);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_T, ui::EF_CONTROL_DOWN);
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  EXPECT_TRUE(editor.active());
  EXPECT_NE(std::u16string::npos, editor.error_for_testing().find(u"Conflicting"));
  EXPECT_EQ(before, App().commands().Read());
  auto external = before->ToValue();
  external.FindDict("shortcuts")->Set("spawn-tab", base::ListValue().Append("Ctrl+Y"));
  ASSERT_TRUE(App().commands().Save(external).config);
  const auto newer = App().commands().Read();
  views::test::ButtonTestApi(editor.save_for_testing()).NotifyDefaultMouseClick();
  EXPECT_TRUE(editor.active());
  EXPECT_NE(std::u16string::npos, editor.error_for_testing().find(u"changed"));
  EXPECT_EQ(newer, App().commands().Read());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_TRUE(editor.active());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
}

// A list opened in a short window builds the rows a taller window shows when
// the window grows.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ResizedListBuildsTheRowsItNowShows) {
  for (int i = 0; i < 30; ++i)
    ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                              ui::PAGE_TRANSITION_TYPED));
  auto& window = Window();
  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1000, 360));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.widget()->GetWindowBoundsInScreen().height() == 360;
  }));
  ASSERT_TRUE(window.RunCommand("toggle-dashboard"));
  auto& dashboard = window.workspace_picker();
  ASSERT_TRUE(dashboard.active());
  const size_t before = dashboard.built_rows_for_testing();
  ASSERT_LT(before, dashboard.result_count_for_testing());
  browser()->GetWindow()->SetBounds(gfx::Rect(40, 40, 1000, 1000));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.widget()->GetWindowBoundsInScreen().height() == 1000;
  }));
  ASSERT_TRUE(dashboard.active());
  EXPECT_GT(dashboard.built_rows_for_testing(), before);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeKeyboardRecordingStopsWhenItsWindowIsInactive) {
  auto& window = Window();
  const auto before = App().commands().Read();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  auto* record = editor.record_for_testing("scroll-down", true);
  ASSERT_TRUE(record);
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_FALSE(editor.recording());
  EXPECT_TRUE(editor.active());
  views::test::ButtonTestApi(record).NotifyDefaultMouseClick();
  keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.recording(); }));
  EXPECT_TRUE(editor.active());
  EXPECT_EQ(u"J", editor.bindings_for_testing("scroll-down", true));
  ASSERT_TRUE(Window(second).RunCommand("toggle-help"));
  EXPECT_EQ(u"J", Window(second).keyboard_editor().bindings_for_testing(
                      "scroll-down", true));
  EXPECT_EQ(before, App().commands().Read());
}

// Enter applies a text field in any section, also while other sections' and
// hidden rows' fields do not exist.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PreferencesTextFieldsApplyWithEnterInAnySection) {
  auto& window = Window();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.active());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  editor.ShowAppearance();
  auto* accent = editor.color_field_for_testing("accent");
  ASSERT_TRUE(accent);
  accent->RequestFocus();
  accent->SetText(u"#123456");
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(editor.active());
  const bool dark = App().preferences().ReadTheme()->mode == "dark";
  EXPECT_EQ("#123456",
            App().preferences().ReadThemeColor(dark, "accent").value_or(""));
  // Every preference change refreshes the controls that are built.
  ASSERT_TRUE(App().preferences().SetStatusBarVisible(false));
  ASSERT_TRUE(App().preferences().SetStatusBarVisible(true));
  EXPECT_TRUE(editor.active());
  // A search that hides the search URL row still saves an alias with Enter.
  auto* search = editor.settings_search_for_testing();
  search->SetText(u"alias");
  editor.ContentsChanged(search, u"alias");
  ASSERT_EQ(nullptr, editor.search_setting_for_testing("engine"));
  editor.search_setting_for_testing("alias")->SetText(u"fx");
  editor.search_setting_for_testing("name")->SetText(u"Example");
  auto* url = editor.search_setting_for_testing("url");
  url->SetText(u"https://example.test/?q=%s");
  url->RequestFocus();
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(editor.active());
  SearchService service(*g_browser_process->local_state());
  EXPECT_EQ(GURL("https://example.test/?q=query"),
            service.ResolveNavigation("fx query"));
  editor.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !editor.root_for_testing(); }));
}

// In the shortcut table, typing filters and Space records.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       KeyboardTableTypingFiltersAndSpaceRecords) {
  auto& window = Window();
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(editor.record_for_testing("spawn-tab", false));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_T, ui::EF_NONE);
  EXPECT_EQ(u"t", editor.search_for_testing()->GetText());
  EXPECT_TRUE(editor.search_for_testing()->HasFocus());
  ASSERT_TRUE(editor.record_for_testing("spawn-tab", false));
  EXPECT_FALSE(editor.recording());
  keys.PressKey(ui::VKEY_SPACE, ui::EF_NONE);
  keys.ReleaseKey(ui::VKEY_SPACE, ui::EF_NONE);
  EXPECT_TRUE(editor.recording());
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_FALSE(editor.recording());
  EXPECT_TRUE(editor.active());
}

// Preferences: General first, the settings search filters every section and
// jumps to the one with matches.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PreferencesOpenOnGeneralAndSearchEverySection) {
  auto& window = Window();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-help"));
  auto& editor = window.keyboard_editor();
  ASSERT_TRUE(base::test::RunUntil([&] { return editor.active(); }));
  EXPECT_EQ(KeyboardEditor::kGeneralPane, editor.selected_pane_for_testing());
  auto* search = editor.settings_search_for_testing();
  ASSERT_TRUE(search);
  search->SetText(u"cookies");
  editor.ContentsChanged(search, u"cookies");
  EXPECT_EQ(KeyboardEditor::kPrivacyPane, editor.selected_pane_for_testing());
  EXPECT_TRUE(editor.setting_visible_for_testing(u"Site cookies"));
  EXPECT_FALSE(editor.setting_visible_for_testing(u"Interface scale"));
  // Clearing the search shows the section the matches were in.
  search->SetText(u"");
  editor.ContentsChanged(search, u"");
  EXPECT_EQ(KeyboardEditor::kPrivacyPane, editor.selected_pane_for_testing());
  EXPECT_TRUE(editor.setting_visible_for_testing(u"Site cookies"));
  editor.ShowAppearance();
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

// The Command Centre: count, category chips, inspector card,
// recently used commands and the terminal fallback.
// A long list builds the rows its panel can show; keyboard selection builds
// the rest as it reaches them, so typing does not rebuild every result.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       CommandCentreBuildsRowsAsSelectionReachesThem) {
  auto& palette = Window().palette();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-palette"));
  ASSERT_TRUE(base::test::RunUntil([&] { return palette.active(); }));
  const size_t total = palette.result_count_for_testing();
  const size_t built = palette.built_rows_for_testing();
  ASSERT_GT(built, 0u);
  ASSERT_LT(built, total);
  // The selected row is scrolled into view, also right after it is built.
  const auto selected_row_visible = [&] {
    for (size_t i = 0; i < palette.built_rows_for_testing(); ++i) {
      auto* row = static_cast<FlyButton*>(palette.row_for_testing(i));
      if (row->selected())
        return !row->GetVisibleBounds().IsEmpty();
    }
    return false;
  };
  for (size_t i = 0;
       i < 2 * built && palette.built_rows_for_testing() == built; ++i) {
    ui::KeyEvent down(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE);
    ASSERT_TRUE(palette.HandleOverlayKey(down));
  }
  EXPECT_GT(palette.built_rows_for_testing(), built);
  EXPECT_FALSE(palette.selected_action_for_testing().empty());
  EXPECT_TRUE(selected_row_visible());
  palette.Cancel();
  ASSERT_TRUE(base::test::RunUntil([&] { return !palette.active(); }));
  // Up from the first row wraps to the last command.
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-palette"));
  ASSERT_TRUE(base::test::RunUntil([&] { return palette.active(); }));
  ui::KeyEvent up(ui::EventType::kKeyPressed, ui::VKEY_UP, ui::EF_NONE);
  ASSERT_TRUE(palette.HandleOverlayKey(up));
  EXPECT_EQ(total, palette.built_rows_for_testing());
  EXPECT_FALSE(palette.selected_action_for_testing().empty());
  EXPECT_TRUE(selected_row_visible());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       CommandCentreFiltersInspectsAndRemembersCommands) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto& window = Window();
  Activate(browser());
  auto& palette = window.palette();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-palette"));
  ASSERT_TRUE(base::test::RunUntil([&] { return palette.active(); }));
  EXPECT_TRUE(palette.count_for_testing().ends_with(u" commands"));
  EXPECT_EQ(u"New Blank Tab", palette.inspector_title_for_testing());
  // A row that opens under the resting pointer is not selected by it; once
  // the pointer moves, hovering selects.
  auto* row = palette.row_for_testing(3);
  ASSERT_TRUE(row);
  const auto row_centre = row->GetBoundsInScreen().CenterPoint();
  ui::MouseEvent resting(ui::EventType::kMouseEntered, gfx::Point(),
                         gfx::Point(), base::TimeTicks::Now(), ui::EF_NONE,
                         ui::EF_NONE);
  row->OnMouseEntered(resting);
  EXPECT_EQ(u"New Blank Tab", palette.inspector_title_for_testing());
  ui::test::EventGenerator pointer(
      window.widget()->GetNativeWindow()->GetRootWindow());
  pointer.MoveMouseTo(row_centre);
  EXPECT_NE(u"New Blank Tab", palette.inspector_title_for_testing());
  // Hover and keyboard navigation must leave exactly one selected row.
  const auto assert_single_selection = [&] {
    int count = 0;
    for (size_t i = 0; i < palette.result_count_for_testing(); ++i) {
      auto* button = static_cast<FlyButton*>(palette.row_for_testing(i));
      if (!button)
        continue;
      count += button->selected();
      EXPECT_FALSE(button->GetIsDefault());
      EXPECT_NE(ui::ButtonStyle::kProminent, button->GetStyle());
    }
    EXPECT_EQ(1, count);
  };
  assert_single_selection();
  for (int i = 0; i < 8; ++i) {
    ui::KeyEvent down(ui::EventType::kKeyPressed, ui::VKEY_DOWN, ui::EF_NONE);
    ASSERT_TRUE(palette.HandleOverlayKey(down));
    assert_single_selection();
  }
  palette.SetCategoryForTesting("Split View");
  EXPECT_EQ(u"6 commands", palette.count_for_testing());
  palette.SetCategoryForTesting("All");
  palette.search_for_testing()->SetText(u"zz-no-command");
  palette.ContentsChanged(palette.search_for_testing(), u"zz-no-command");
  EXPECT_EQ(u"1 match", palette.count_for_testing());
  EXPECT_EQ("terminal:zz-no-command", palette.selected_action_for_testing());
  EXPECT_EQ(u"Run in terminal", palette.inspector_title_for_testing());
  palette.search_for_testing()->SetText(u"reader");
  palette.ContentsChanged(palette.search_for_testing(), u"reader");
  ASSERT_EQ("toggle-reader-view", palette.selected_action_for_testing());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return !palette.active(); }));
  EXPECT_EQ(std::vector<std::string>{"toggle-reader-view"},
            App().preferences().ReadRecentCommands());
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
  auto& window = Window();
  Activate(browser());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_P, ui::EF_CONTROL_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] { return window.palette().active(); }));
  // The Command Centre lists New Blank Tab (not Duplicate Tab).
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
  EXPECT_EQ(browser(), LastActive());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativePaletteCustomBindingCancelAndWindowSwitch) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents,
                              "document.body.innerHTML = '<input id=editable>';"
                              "document.querySelector('#editable').focus()"));
  auto& window = Window();
  Activate(browser());
  auto config = DefaultKeyboardConfig().ToValue();
  config.FindDict("shortcuts")
      ->Set("toggle-palette", base::ListValue().Append("Alt+P"));
  ASSERT_TRUE(App().commands().Save(config).config);
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  EXPECT_NE(browser(), LastActive());
  EXPECT_TRUE(window.palette().active());
  Activate(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.palette().search_for_testing()->HasFocus(); }));
  keys.PressKey(ui::VKEY_P, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.palette().search_for_testing() == nullptr; }));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return contents->GetRenderWidgetHostView()->HasFocus();
  }));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativePointerFocusTypesAndReturnsFromWorkspaceOverlay) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = Window();
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<input id="native-edit" '
      + 'style="position:fixed;left:40px;top:40px;width:250px;height:24px">';
  )js"));
  Activate(browser());
  window.new_tab_button_for_testing()->RequestFocus();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.new_tab_button_for_testing()->HasFocus();
  }));
  ui::test::EventGenerator input(
      window.widget()->GetNativeWindow()->GetRootWindow());
  const auto bounds = contents->GetRenderWidgetHostView()->GetViewBounds();
  const auto click_point = bounds.origin() + gfx::Vector2d(70, 52);
  auto native_point = click_point;
  auto* root = window.widget()->GetNativeWindow()->GetRootWindow();
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
      << " desktop=" << window.widget()->GetWindowBoundsInScreen().ToString();
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
  auto& window = Window();
  Activate(browser());
  auto* status = window.status_bar_for_testing();
  ASSERT_TRUE(status->GetVisible());
  EXPECT_EQ(nullptr, g_browser_process->local_state()->GetUserPrefValue(kPreferencesPref));
  ASSERT_TRUE(content::ExecJs(contents, R"js(
    document.body.innerHTML = '<a href="https://example.invalid/fixture" '
      + 'style="position:fixed;left:20px;top:20px;width:180px;height:40px;display:block">Link</a>';
  )js"));
  ui::test::EventGenerator pointer(
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  EXPECT_EQ(false, App().preferences().ReadStatusBarVisible());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-status-bar"));
  EXPECT_TRUE(status->GetVisible());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  Window(browser()).UpdateTargetURL(contents, GURL("https://hidden.invalid/"));
  EXPECT_EQ(std::u16string::npos, status->location_for_testing()->GetText().find(u"hidden.invalid"));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  // Another window's page never reaches this window's status bar.
  auto* second_status = Window(second).status_bar_for_testing();
  const std::u16string before(second_status->location_for_testing()->GetText());
  Window(second).UpdateTargetURL(Active(browser()),
                                 GURL("https://other-profile.invalid/"));
  EXPECT_EQ(before, second_status->location_for_testing()->GetText());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       NativeStatusUsesEngineSecurityAndPrivateWorkspaceState) {
  net::EmbeddedTestServer https(net::EmbeddedTestServer::TYPE_HTTPS);
  https.ServeFilesFromSourceDirectory("chrome/test/data");
  ASSERT_TRUE(https.Start());
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser(), https.GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = Window();
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
  ASSERT_TRUE(Window(browser()).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  auto* private_status = Window(LastActive()).status_bar_for_testing();
  EXPECT_EQ(u"PRIVATE PROFILE",
            private_status->workspace_for_testing()->GetText());
  EXPECT_FALSE(
      private_status->security_for_testing()->GetTooltipText().starts_with(
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
  auto& window = Window();
  const auto mode = [&] {
    const auto desktop_mode =
        window.widget()->GetColorProviderKeyForTesting().color_mode;
    EXPECT_EQ(desktop_mode, window.sidebar_widget_for_testing()
                                ->GetColorProviderKeyForTesting()
                                .color_mode);
    return desktop_mode;
  };
  const auto dark = [](content::WebContents* contents) {
    return content::EvalJs(contents, "matchMedia('(prefers-color-scheme: dark)').matches").ExtractBool();
  };
  Activate(browser());
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
      window.widget()->GetNativeWindow()->GetRootWindow());
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
  ASSERT_TRUE(App().preferences().SetWebsiteThemeSync(false));
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-theme"));
  EXPECT_EQ(ui::ColorProviderKey::ColorMode::kDark, mode());
  ASSERT_TRUE(base::test::RunUntil([&] { return !dark(first) && !dark(second_contents); }));
  ASSERT_TRUE(App().preferences().SetWebsiteThemeSync(true));
  ASSERT_TRUE(base::test::RunUntil([&] { return dark(first) && dark(second_contents); }));
  ASSERT_TRUE(Window(second).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  auto* private_browser = LastActive();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(private_browser,
      embedded_test_server()->GetURL("/title1.html")));
  auto* private_contents = Active(private_browser);
  ASSERT_TRUE(base::test::RunUntil([&] { return dark(private_contents); }));
  ASSERT_TRUE(App().preferences().SetWebsiteThemeSync(false));
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
  ASSERT_TRUE(InvokeNativeCommand(browser(), "add-bookmark"));
  ASSERT_EQ(1u, App().bookmarks().Read()->size());
  const auto stored = g_browser_process->local_state()->GetDict(kBookmarksPref).Clone();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "add-bookmark"));
  EXPECT_EQ(stored, g_browser_process->local_state()->GetDict(kBookmarksPref));
  auto* first_model = BookmarkModelFactory::GetForBrowserContext(browser()->GetProfile());
  ASSERT_TRUE(first_model);
  ASSERT_TRUE(base::test::RunUntil([&] { return first_model->loaded(); }));
  EXPECT_FALSE(first_model->IsBookmarked(url));
  const auto profile_only = embedded_test_server()->GetURL("/profile-only");
  ASSERT_TRUE(first_model->AddURL(first_model->other_node(), 0, u"Profile only", profile_only));
  EXPECT_FALSE(App().bookmarks().Contains(profile_only.spec()));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, url));
  auto* destination = Active(second);
  ASSERT_TRUE(content::ExecJs(destination, "document.cookie='identity=second'"));
  auto* second_model = BookmarkModelFactory::GetForBrowserContext(second->GetProfile());
  ASSERT_TRUE(second_model);
  ASSERT_TRUE(base::test::RunUntil([&] { return second_model->loaded(); }));
  EXPECT_FALSE(second_model->IsBookmarked(profile_only));
  ASSERT_EQ(1u, App().bookmarks().Read()->size());
  const int count = second->GetTabStripModel()->count();
  auto& window = Window(second);
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  ASSERT_TRUE(InvokeNativeCommand(second, "toggle-bookmarks"));
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
  EXPECT_EQ(1u, App().bookmarks().Read()->size());
  const auto before_private = g_browser_process->local_state()->GetDict(kBookmarksPref).Clone();
  ASSERT_TRUE(Window(second).RunCommand("toggle-private-mode"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    auto* selected = LastActive();
    return selected && selected->GetProfile()->IsOffTheRecord();
  }));
  ASSERT_TRUE(ui_test_utils::NavigateToURL(LastActive(), url));
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
  auto& window = Window();
  ASSERT_TRUE(App().bookmarks().Add("https://one.test/", "alpha one"));
  ASSERT_TRUE(App().bookmarks().Add("https://two.test/", "alpha two"));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-bookmarks"));
  auto& picker = window.bookmark_picker();
  ASSERT_TRUE(picker.active());
  auto* search = picker.search_for_testing();
  search->SetText(u"alphaX");
  picker.ContentsChanged(search, std::u16string(search->GetText()));
  search->SetSelectedRange(gfx::Range(0));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_DELETE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] { return search->GetText() == u"lphaX"; }));
  EXPECT_EQ(2u, App().bookmarks().Read()->size());
  search->SetText(u"alpha");
  picker.ContentsChanged(search, std::u16string(search->GetText()));
  search->SetSelectedRange(gfx::Range(5));
  ASSERT_EQ(2u, picker.result_count_for_testing());
  keys.PressKey(ui::VKEY_DELETE, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return App().bookmarks().Read()->size() == 1 && picker.result_count_for_testing() == 1;
  }));
  EXPECT_EQ(u"alpha", search->GetText());
  auto* remove = picker.remove_for_testing(0);
  ASSERT_TRUE(remove && remove->GetEnabled());
  views::test::ButtonTestApi(remove).NotifyDefaultMouseClick();
  picker.Cancel(true);
  ASSERT_TRUE(base::test::RunUntil([&] { return !picker.search_for_testing(); }));
  EXPECT_EQ(1u, App().bookmarks().Read()->size());
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
  EXPECT_FALSE(NativeCommandAvailable(browser(), "toggle-bookmarks"));
  EXPECT_EQ(future, g_browser_process->local_state()->GetDict(kBookmarksPref));
}

class FlyTabMenuBrowserTest : public FlyWorkspaceBrowserTest {
 protected:
  void Menu(content::WebContents* contents,
            base::OnceCallback<void(ui::SimpleMenuModel*)> inspect) {
    auto& window = Window();
    auto* active = Active(LastActive());
    ASSERT_TRUE(content::WaitForLoadStop(active));
    base::RunLoop().RunUntilIdle();
    if (window.address_picker().active()) {
      ui::test::EventGenerator keys(
          window.widget()->GetNativeWindow()->GetRootWindow());
      keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
      ASSERT_TRUE(base::test::RunUntil([&] { return !window.address_picker().active(); }));
    }
    auto* sidebar = window.tab_sidebar_for_testing();
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
        Window().widget()->GetNativeWindow()->GetRootWindow());
    auto point = control->GetVisibleBounds().CenterPoint();
    views::View::ConvertPointToScreen(control, &point);
    auto* root_window = Window().widget()->GetNativeWindow()->GetRootWindow();
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
  auto* row = Window().tab_sidebar_for_testing()->row_for_testing(target);
  ASSERT_TRUE(base::test::RunUntil([&] { return !row->children()[2]->GetEnabled(); }));
}

IN_PROC_BROWSER_TEST_F(FlyTabMenuBrowserTest,
                       MenuTracksReordersAndRejectsClosedTargets) {
  // Profile creation posts non-nestable work. Prepare it before MenuRunner's
  // nested loop; the menu callback still switches the workspace synchronously.
  auto* other_workspace = SecondWorkspace();
  ASSERT_TRUE(other_workspace);
  Activate(browser());
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
    Activate(other_workspace);
  }));
  // The command belongs to this window's tab, whichever window is active.
  ASSERT_TRUE(base::test::RunUntil([&] { return !lifetime; }));
  EXPECT_EQ(1, model->count());
  EXPECT_EQ(1, other_workspace->GetTabStripModel()->count());
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
  EXPECT_EQ(GURL(kFlyNewTabURL),
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

// Hints read only the visible part of the page. A button deep inside a tall
// frame, scrolled into view, must still get a hint, whether the frame is in
// the page's process or in another site's process.
IN_PROC_BROWSER_TEST_F(FlyPageScrollBrowserTest,
                       HintsReachTargetsDeepInScrolledFrames) {
  for (const bool cross_site : {false, true}) {
    SCOPED_TRACE(cross_site ? "cross-site frame" : "same-site frame");
    ASSERT_TRUE(ui_test_utils::NavigateToURL(
        browser(), embedded_test_server()->GetURL("a.com", "/title1.html")));
    auto* contents = Active(browser());
    ASSERT_TRUE(content::ExecJs(
        contents,
        content::JsReplace(R"js(
      document.body.style.margin = '0';
      document.body.innerHTML = '<div style="height:4000px"></div>' +
          '<iframe style="display:block;border:0;width:300px;height:6000px">' +
          '</iframe><div style="height:4000px"></div>';
      window.addEventListener('message', e => document.title = e.data);
      new Promise(resolve => {
        const frame = document.querySelector('iframe');
        frame.onload = resolve;
        frame.src = $1;
      });
    )js",
                           embedded_test_server()->GetURL(
                               cross_site ? "b.com" : "a.com", "/title1.html"))));
    auto* frame = content::ChildFrameAt(contents->GetPrimaryMainFrame(), 0);
    ASSERT_TRUE(frame);
    EXPECT_EQ(cross_site, frame->IsCrossProcessSubframe());
    ASSERT_TRUE(content::ExecJs(frame, R"js(
      document.body.style.margin = '0';
      document.body.innerHTML =
          '<div style="height:5000px"></div><button id="deep">Deep</button>';
      document.querySelector('#deep').onclick =
          () => parent.postMessage('deep activated', '*');
    )js"));
    ASSERT_TRUE(content::ExecJs(contents, "window.scrollTo(0, 4000 + 5000 - 100)"));
    ASSERT_EQ(true, content::EvalJs(frame, R"js(
      new Promise(resolve => new IntersectionObserver(entries => {
        if (entries[0].isIntersecting) resolve(true);
      }).observe(document.querySelector('#deep')));
    )js"));
    auto& window = Window();
    Activate(browser());
    contents->Focus();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return contents->GetRenderWidgetHostView()->HasFocus();
    }));
    ui::test::EventGenerator keys(
        window.widget()->GetNativeWindow()->GetRootWindow());
    keys.PressKey(ui::VKEY_F, ui::EF_ALT_DOWN);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return window.hints().active() &&
             window.hints().target_count_for_testing() > 0;
    })) << "cancellation=" << window.hints().cancellation_for_testing();
    EXPECT_EQ(1u, window.hints().target_count_for_testing());
    EXPECT_TRUE(contents->GetAccessibilityMode().HasFilterFlags(
        ui::AXMode::kOnScreenOnly));
    content::TitleWatcher title(contents, u"deep activated");
    keys.PressKey(ui::VKEY_Q, ui::EF_NONE);
    EXPECT_EQ(u"deep activated", title.WaitAndGetTitle());
    EXPECT_FALSE(window.hints().active());
    EXPECT_FALSE(contents->GetAccessibilityMode().has_mode(
        ui::AXMode::kWebContents));
  }

  // A screen reader's full tree is not reduced to the visible part.
  auto* contents = Active(browser());
  auto screen_reader = content::BrowserAccessibilityState::GetInstance()
                           ->CreateScopedModeForWebContents(
                               contents, ui::kAXModeComplete);
  auto& window = Window();
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_F, ui::EF_ALT_DOWN);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.hints().active() &&
           window.hints().target_count_for_testing() > 0;
  })) << "cancellation=" << window.hints().cancellation_for_testing();
  EXPECT_FALSE(contents->GetAccessibilityMode().HasFilterFlags(
      ui::AXMode::kOnScreenOnly));
  keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
  EXPECT_FALSE(window.hints().active());
}

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
  auto& window = Window();
  Activate(browser());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  // A zoom step (factor 1.095) leaves fractional device pixels, so
  // offsets may round one pixel either way.
  auto wait_offset = [&](const char* axis, int offset) {
    return base::test::RunUntil([&] {
      return content::EvalJs(contents, content::JsReplace(R"js(
        (async () => {
          const box = document.querySelector('#box');
          for (let i = 0; i < 3; ++i) {
            if (Math.abs(Math.round(box[$1]) - $2) > 1) return false;
            await new Promise(requestAnimationFrame);
          }
          return Math.abs(Math.round(box[$1]) - $2) <= 1;
        })()
      )js", axis, offset)).ExtractBool();
    });
  };
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80)) << content::EvalJs(contents, "document.querySelector('#box').scrollTop");
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
  const int half_page =
      content::EvalJs(
          contents,
          "Math.floor(document.querySelector('#box').clientHeight / 2)")
          .ExtractInt();
  keys.PressKey(ui::VKEY_D, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80 + half_page))
      << content::EvalJs(contents, "JSON.stringify({top:document.querySelector('#box').scrollTop,left:document.querySelector('#box').scrollLeft,height:document.querySelector('#box').clientHeight})");
  keys.PressKey(ui::VKEY_U, ui::EF_NONE);
  ASSERT_TRUE(wait_offset("scrollTop", 80)) << content::EvalJs(contents, "document.querySelector('#box').scrollTop");
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
  auto& window = Window();
  Activate(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetFocusedFrame() == child; }));
  ASSERT_TRUE(SecondWorkspace());
  Activate(browser());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return contents->GetFocusedFrame() == child; }));
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_J, ui::EF_NONE);
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(child, "Math.round(scrollY)").ExtractInt() == 80;
  }));
  EXPECT_EQ(0, content::EvalJs(contents, "scrollY"));
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       ExtensionAndDownloadPickersDismissWithEscape) {
  auto& window = Window();
  Activate(browser());
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  for (auto [action, picker] :
       {std::pair{"open-extension-actions",
                  &window.extensions_picker_for_testing()},
        {"toggle-downloads", &window.downloads_picker_for_testing()}}) {
    ASSERT_TRUE(window.RunCommand(action));
    ASSERT_TRUE(picker->active());
    keys.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
    ASSERT_TRUE(base::test::RunUntil([&] { return !picker->active(); }));
  }
}

IN_PROC_BROWSER_TEST_F(FlyPageScrollBrowserTest,
                       RepeatedScrollAccumulatesWithoutRestartingOrBouncing) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  ASSERT_TRUE(content::ExecJs(contents, R"JS(
    document.body.innerHTML = '<div style="height:12000px">scroll</div>';
    window.scrollTo(0, 0);
    window.samples = [];
    window.sampling = true;
    requestAnimationFrame(function sample() {
      samples.push(scrollY);
      if (sampling) requestAnimationFrame(sample);
    });
  )JS"));
  for (int i = 0; i < 12; ++i)
    ASSERT_TRUE(ExecuteNativeCommand(browser(), "scroll-down"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "Math.round(scrollY)").ExtractInt() == 960;
  })) << content::EvalJs(contents, "JSON.stringify({y:scrollY,last:samples.slice(-5)})");
  EXPECT_EQ(true, content::EvalJs(
                      contents,
                      "samples.every((y,i) => i === 0 || y >= samples[i-1])"));
  ASSERT_TRUE(content::ExecJs(contents, "samples = [];"));
  for (int i = 0; i < 8; ++i)
    ASSERT_TRUE(ExecuteNativeCommand(browser(), "scroll-up"));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return content::EvalJs(contents, "Math.round(scrollY)").ExtractInt() == 320;
  })) << content::EvalJs(contents, "JSON.stringify({y:scrollY,last:samples.slice(-5)})");
  EXPECT_EQ(true, content::EvalJs(
                      contents,
                      "samples.every((y,i) => i === 0 || y <= samples[i-1])"));
  ASSERT_TRUE(content::ExecJs(contents, "sampling = false;"));
}

// Measure complete synchronous construction plus first layout, including a cold
// opening and repeated openings. These surfaces must not block input for
// seconds.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, SurfaceOpeningLatency) {
  auto& window = Window();
  for (const char* scale : {"normal", "large"}) {
    ASSERT_TRUE(App().preferences().SetString("interface_scale", scale));
    for (int pass = 0; pass < 3; ++pass) {
      for (const std::string action :
           {"toggle-help", "toggle-palette", "toggle-address",
            "toggle-dashboard", "toggle-bookmarks", "toggle-history",
            "toggle-downloads", "open-extension-actions", "open-diagnostics"}) {
        SCOPED_TRACE(action + " / " + scale);
        const auto start = base::TimeTicks::Now();
        ASSERT_TRUE(window.RunCommand(action));
        window.widget()->LayoutRootViewIfNecessary();
        const auto elapsed = base::TimeTicks::Now() - start;
        LOG(INFO) << "SURFACE_LATENCY " << action << " scale=" << scale
                  << " pass=" << pass << " ms=" << elapsed.InMillisecondsF();
        EXPECT_LT(elapsed, base::Milliseconds(200));
        if (action == "toggle-help") {
          const auto keyboard_start = base::TimeTicks::Now();
          window.keyboard_editor().ShowKeyboard();
          window.keyboard_editor()
              .search_for_testing()
              ->GetWidget()
              ->LayoutRootViewIfNecessary();
          EXPECT_LT(base::TimeTicks::Now() - keyboard_start,
                    base::Milliseconds(200));
        } else if (action == "toggle-palette") {
          auto& palette = window.palette();
          const auto filter_start = base::TimeTicks::Now();
          palette.search_for_testing()->SetText(u"tab");
          palette.ContentsChanged(palette.search_for_testing(), u"tab");
          palette.search_for_testing()
              ->GetWidget()
              ->LayoutRootViewIfNecessary();
          EXPECT_LT(base::TimeTicks::Now() - filter_start,
                    base::Milliseconds(200));
        }
        ASSERT_TRUE(window.RunCommand(action));
        base::RunLoop().RunUntilIdle();
      }
    }
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, PageFullscreenHidesFlyChrome) {
  auto& window = Window();
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  const bool footer_visible = window.status_bar_for_testing()->IsDrawn();
  ASSERT_TRUE(content::ExecJs(contents, "document.body.requestFullscreen()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return window.widget()->IsFullscreen(); }));
  window.widget()->LayoutRootViewIfNecessary();
  EXPECT_FALSE(window.sidebar_widget_for_testing()->IsVisible());
  EXPECT_FALSE(window.status_bar_for_testing()->IsDrawn());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window.widget()->GetContentsView()->GetBoundsInScreen() ==
           contents->GetRenderWidgetHostView()->GetViewBounds();
  }));
  ASSERT_TRUE(content::ExecJs(contents, "document.exitFullscreen()"));
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !window.widget()->IsFullscreen(); }));
  EXPECT_TRUE(window.sidebar_widget_for_testing()->IsVisible());
  EXPECT_EQ(footer_visible, window.status_bar_for_testing()->IsDrawn());
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SplitButtonTracksActiveTabState) {
  auto& window = Window();
  auto* split = window.split_button_for_testing();
  EXPECT_FALSE(split->selected());
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-split"));
  ASSERT_TRUE(base::test::RunUntil([&] { return split->selected(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "spawn-tab"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !split->selected(); }));
  browser()->GetTabStripModel()->ActivateTabAt(0);
  ASSERT_TRUE(base::test::RunUntil([&] { return split->selected(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-split"));
  ASSERT_TRUE(base::test::RunUntil([&] { return !split->selected(); }));
}

void CheckSidebarPointerState(BrowserWindowInterface* browser,
                              const GURL& url,
                              bool collapse,
                              bool leave_before_close) {
  auto& window = *FlyWindowController::FromBrowser(browser);
  ASSERT_TRUE(ui_test_utils::NavigateToURL(browser, url));
  ui::test::EventGenerator pointer(
      window.widget()->GetNativeWindow()->GetRootWindow());
  ASSERT_TRUE(FlyApp::Get().preferences().SetSidebarAutoCollapse(collapse));
  for (const std::u16string label : {u"Settings", u"Search or enter address"}) {
    SCOPED_TRACE(base::UTF16ToUTF8(label) +
                 " collapse=" + base::NumberToString(collapse) +
                 " leave=" + base::NumberToString(leave_before_close));
    FlyButton* target = nullptr;
    for (const auto& child :
         window.sidebar_root_for_testing()->panel()->children()) {
      auto* button = views::AsViewClass<FlyButton>(child.get());
      if (button && button->GetTooltipText() == label)
        target = button;
    }
    ASSERT_TRUE(target);
    const auto desktop =
        window.widget()->GetContentsView()->GetBoundsInScreen();
    const auto rail = desktop.origin() + gfx::Vector2d(12, 180);
    pointer.MoveMouseTo(rail);
    ASSERT_TRUE(base::test::RunUntil(
        [&] { return window.sidebar_expanded_for_testing(); }));
    ASSERT_TRUE(base::test::RunUntil([&] {
      return target->layer() && target->layer()->opacity() == 1.f &&
             window.sidebar_root_for_testing()
                 ->panel()
                 ->layer()
                 ->transform()
                 .IsIdentity();
    }));
    views::test::RunScheduledLayout(target->GetWidget());
    pointer.MoveMouseTo(target->GetBoundsInScreen().CenterPoint());
    pointer.ClickLeftButton();
    ASSERT_TRUE(base::test::RunUntil([&] {
      return label == u"Settings" ? window.keyboard_editor().active()
                                  : window.address_picker().active();
    }));
    if (leave_before_close)
      pointer.MoveMouseTo(desktop.CenterPoint());
    pointer.PressKey(ui::VKEY_ESCAPE, ui::EF_NONE);
    base::RunLoop().RunUntilIdle();
    ASSERT_FALSE(window.keyboard_editor().active());
    ASSERT_FALSE(window.address_picker().active());
    pointer.MoveMouseTo(desktop.CenterPoint());
    base::RunLoop().RunUntilIdle();
    pointer.MoveMouseTo(rail);
    ASSERT_TRUE(base::test::RunUntil([&] {
      return window.sidebar_expanded_for_testing() &&
             target->layer()->opacity() == 1.f &&
             window.sidebar_root_for_testing()
                 ->panel()
                 ->layer()
                 ->transform()
                 .IsIdentity();
    }));
    base::RunLoop().RunUntilIdle();

    EXPECT_FALSE(target->HasFocus());
    EXPECT_FALSE(target->selected());
    EXPECT_EQ(views::Button::STATE_NORMAL, target->GetVisualState());
    // A genuine subsequent enter/exit must still work normally.
    pointer.MoveMouseTo(target->GetBoundsInScreen().CenterPoint());
    EXPECT_EQ(views::Button::STATE_HOVERED, target->GetVisualState());
    pointer.MoveMouseTo(rail);
    EXPECT_EQ(views::Button::STATE_NORMAL, target->GetVisualState());
  }
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarActionsDoNotKeepPointerFocus) {
  CheckSidebarPointerState(
      browser(), embedded_test_server()->GetURL("/title1.html"), true, false);
}
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarHoverAfterMovingBehindOverlay) {
  CheckSidebarPointerState(
      browser(), embedded_test_server()->GetURL("/title1.html"), true, true);
}
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       PinnedSidebarActionsDoNotKeepHover) {
  CheckSidebarPointerState(
      browser(), embedded_test_server()->GetURL("/title1.html"), false, true);
}

IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       SidebarCollapseClearsTransientHover) {
  auto& window = Window();
  auto* panel = window.sidebar_root_for_testing()->panel();
  for (const auto& child : panel->children()) {
    auto* button = views::AsViewClass<FlyButton>(child.get());
    if (!button || (button->GetTooltipText() != u"Settings" &&
                    button->GetTooltipText() != u"Search or enter address" &&
                    button != window.split_button_for_testing()))
      continue;
    panel->SetRail(false, nullptr);
    button->SetState(views::Button::STATE_HOVERED);
    panel->SetRail(true, nullptr);
    EXPECT_EQ(views::Button::STATE_NORMAL, button->GetVisualState());
    panel->SetRail(false, nullptr);
    EXPECT_EQ(views::Button::STATE_NORMAL, button->GetVisualState());
  }
}

// A new window starts in Fly's background colour, not Chromium's grey, also
// after the background changes.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, WindowsStartInFlysBackground) {
  EXPECT_EQ(GetFlyColors().bg_main,
            Window().widget()->GetColorProvider()->GetColor(
                ui::kColorWindowBackground));
  const SkColor chosen = SkColorSetRGB(0x10, 0x20, 0x30);
  ASSERT_TRUE(App().preferences().SetThemeColor(GetFlyColors().dark,
                                                "bg_main", "#102030"));
  ASSERT_TRUE(
      base::test::RunUntil([&] { return GetFlyColors().bg_main == chosen; }));
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  EXPECT_EQ(chosen, Window(second).widget()->GetColorProvider()->GetColor(
                        ui::kColorWindowBackground));
}

// Clicking a tab's close button fades its row at once, even while the page
// holds the close with a leave dialog; cancelling restores the row.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       TabRowFadesWhileItsPageHoldsTheClose) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* page = Active(browser());
  ASSERT_TRUE(ExecuteNativeCommand(browser(), "spawn-tab"));
  browser()->GetTabStripModel()->ActivateTabAt(0);
  ASSERT_TRUE(
      content::ExecJs(page, "window.onbeforeunload = () => 'unsaved';"));
  content::PrepContentsForBeforeUnloadTest(page);
  auto* sidebar = Window().tab_sidebar_for_testing();
  views::View* row = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&] {
    row = sidebar->row_for_testing(page);
    return row && row->children()[2]->GetVisible();
  }));
  auto* close = static_cast<views::Button*>(row->children()[2].get());
  views::test::ButtonTestApi(close).NotifyDefaultMouseClick();
  auto* dialog = ui_test_utils::WaitForAppModalDialog();
  ASSERT_TRUE(dialog && dialog->is_before_unload_dialog());
  ASSERT_TRUE(base::test::RunUntil([&] {
    return row->layer() && row->layer()->opacity() < 1.f &&
           !close->GetVisible();
  }));
  dialog->view()->CancelAppModalDialog();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !row->layer() && close->GetVisible(); }));
  EXPECT_EQ(page, Active(browser()));
  EXPECT_EQ(2, browser()->GetTabStripModel()->count());
  ASSERT_TRUE(content::ExecJs(page, "window.onbeforeunload = null;"));
}

// The address bar suggests pages from open workspaces only and never loads
// the profile of a closed one. The History panel covers every workspace.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       AddressSuggestionsLeaveClosedWorkspacesUnloaded) {
  auto* second = SecondWorkspace();
  ASSERT_TRUE(second);
  const auto path = second->GetProfile()->GetPath();
  const auto visited =
      embedded_test_server()->GetURL("/title2.html?closed=workspace");
  ASSERT_TRUE(ui_test_utils::NavigateToURL(second, visited));
  history::BlockUntilHistoryProcessesPendingRequests(
      HistoryServiceFactory::GetForProfile(second->GetProfile(),
                                           ServiceAccessType::EXPLICIT_ACCESS));
  auto second_lifetime = second->GetWeakPtr();
  second->GetWindow()->Close();
  auto* manager = g_browser_process->profile_manager();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !second_lifetime && !manager->GetProfileByPath(path); }));
  Activate(browser());
  auto& window = Window();
  auto& address = window.address_picker();
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] { return address.active(); }));
  address.search_for_testing()->SetText(u"closed=workspace");
  address.ContentsChanged(address.search_for_testing(), u"closed=workspace");
  base::RunLoop().RunUntilIdle();
  // Only the typed search itself.
  EXPECT_EQ(1u, address.result_count_for_testing());
  EXPECT_FALSE(manager->GetProfileByPath(path));
  address.Cancel();
  ASSERT_TRUE(base::test::RunUntil([&] { return !address.showing(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-history"));
  auto& history = window.history_picker();
  ASSERT_TRUE(base::test::RunUntil([&] { return history.active(); }));
  history.search_for_testing()->SetText(u"closed=workspace");
  history.ContentsChanged(history.search_for_testing(), u"closed=workspace");
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return history.result_count_for_testing() >= 1u; }));
}

// The address bar marks its navigations as typed in the address bar, and,
// when the user allows it, preloads the highlighted result so it opens from
// memory.
IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest,
                       AddressBarPreloadsTheHighlightedResultWhenAllowed) {
  ASSERT_TRUE(ui_test_utils::NavigateToURL(
      browser(), embedded_test_server()->GetURL("/title1.html")));
  auto* contents = Active(browser());
  auto& window = Window();
  auto& address = window.address_picker();
  const auto target = embedded_test_server()->GetURL("/title2.html");
  const auto open = [&] {
    ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
    ASSERT_TRUE(base::test::RunUntil([&] { return address.active(); }));
    const auto input = base::UTF8ToUTF16(target.spec());
    address.search_for_testing()->SetText(input);
    address.ContentsChanged(address.search_for_testing(), input);
  };
  open();
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(
      content::test::PrerenderTestHelper::GetHostForUrl(*contents, target)
          .is_null());
  address.Cancel();
  ASSERT_TRUE(base::test::RunUntil([&] { return !address.showing(); }));
  ASSERT_TRUE(App().preferences().SetBool("preload_address_results", true));
  content::test::PrerenderHostObserver preloaded(*contents, target);
  open();
  content::test::PrerenderTestHelper::WaitForPrerenderLoadCompletion(*contents,
                                                                     target);
  ui::test::EventGenerator keys(
      window.widget()->GetNativeWindow()->GetRootWindow());
  keys.PressKey(ui::VKEY_RETURN, ui::EF_NONE);
  preloaded.WaitForActivation();
  EXPECT_EQ(target, contents->GetLastCommittedURL());
  const auto transition =
      contents->GetController().GetLastCommittedEntry()->GetTransitionType();
  EXPECT_TRUE(
      ui::PageTransitionCoreTypeIs(transition, ui::PAGE_TRANSITION_TYPED));
  EXPECT_TRUE(ui::PageTransitionGetQualifier(transition) &
              ui::PAGE_TRANSITION_FROM_ADDRESS_BAR);
  // A search is never preloaded, whatever its engine's address looks like.
  SearchService search(*g_browser_process->local_state());
  ASSERT_TRUE(search.SetEngine(
      embedded_test_server()->GetURL("/results/%s").spec()));
  const auto results = search.ResolveNavigation("unfinished query");
  ASSERT_TRUE(results);
  ASSERT_TRUE(base::test::RunUntil([&] { return !address.showing(); }));
  ASSERT_TRUE(InvokeNativeCommand(browser(), "toggle-address"));
  ASSERT_TRUE(base::test::RunUntil([&] { return address.active(); }));
  address.search_for_testing()->SetText(u"unfinished query");
  address.ContentsChanged(address.search_for_testing(), u"unfinished query");
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(
      content::test::PrerenderTestHelper::GetHostForUrl(*contents, *results)
          .is_null());
}

}  // namespace
}  // namespace fly
