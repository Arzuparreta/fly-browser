// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/native_commands.h"
#include "fly/bookmarks/bookmark_service.h"

#include <algorithm>
#include <memory>
#include <optional>
#include "base/callback_list.h"
#include "base/functional/bind.h"
#include "components/performance_manager/public/freezing/freezing.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/devtools_agent_host.h"
#include "content/public/browser/devtools_agent_host_observer.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/media_session.h"
#include "content/public/browser/render_frame_host.h"

#include <utility>
#include "base/strings/utf_string_conversions.h"

#include "chrome/app/chrome_command_ids.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/split_tab_metrics.h"
#include "chrome/browser/ui/tabs/tab_muted_utils.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/tabs/public/split_tab_data.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "fly/commands/keyboard_router.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/hint_controller.h"
#include "ui/base/clipboard/clipboard_buffer.h"
#include "ui/base/clipboard/scoped_clipboard_writer.h"
#include "ui/events/keycodes/dom/dom_code.h"
#include "ui/events/keycodes/dom/dom_key.h"
#include "ui/events/keycodes/dom/keycode_converter.h"
#include "url/url_constants.h"

namespace fly {
namespace {
class TabSuspension : public content::WebContentsUserData<TabSuspension>,
                      public content::WebContentsObserver,
                      public content::DevToolsAgentHostObserver {
 public:
  static bool Request(content::WebContents* contents);
  static void Resume(content::WebContents* contents);
  static bool Requested(content::WebContents* contents);
  static bool MaybeAuto(content::WebContents* contents,
                        base::TimeDelta timeout,
                        base::TimeTicks now);
  ~TabSuspension() override;

  void OnVisibilityChanged(content::Visibility visibility) override;
  void DidStartLoading() override;
  void DidStopLoading() override;
  void PrimaryPageChanged(content::Page&) override;
  void MediaStartedPlaying(const MediaPlayerInfo&,
                           const content::MediaPlayerId&) override;
  void MediaStoppedPlaying(const MediaPlayerInfo&,
                           const content::MediaPlayerId&,
                           WebContentsObserver::MediaStoppedReason) override;
  void OnAudioStateChanged(bool audible) override;
  void AboutToBeDiscarded(content::WebContents*) override;
  void WasDiscarded() override;
  void WebContentsDestroyed() override;
  void DevToolsAgentHostAttached(content::DevToolsAgentHost* host) override;
  void DevToolsAgentHostDetached(content::DevToolsAgentHost* host) override;

 private:
  friend class content::WebContentsUserData<TabSuspension>;
  explicit TabSuspension(content::WebContents* contents);
  static bool Eligible(content::WebContents* contents);
  void Reset();
  std::optional<base::TimeTicks> quiet_since_;
  bool automatic_ = false;
  base::CallbackListSubscription pinned_subscription_;
  std::unique_ptr<performance_manager::freezing::FreezingVote> vote_;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

WEB_CONTENTS_USER_DATA_KEY_IMPL(TabSuspension);

TabSuspension::TabSuspension(content::WebContents* contents)
    : content::WebContentsUserData<TabSuspension>(*contents),
      content::WebContentsObserver(contents) {
  content::DevToolsAgentHost::AddObserver(this);
  if (auto* tab = tabs::TabInterface::MaybeGetFromContents(contents)) {
    pinned_subscription_ = tab->RegisterPinnedStateChanged(base::BindRepeating(
        [](TabSuspension* state, tabs::TabInterface*, bool) { state->Reset(); },
        base::Unretained(this)));
  }
}
TabSuspension::~TabSuspension() {
  vote_.reset();
  content::DevToolsAgentHost::RemoveObserver(this);
}
bool TabSuspension::Eligible(content::WebContents* contents) {
  if (!contents || contents->IsBeingDestroyed() ||
      contents->GetVisibility() != content::Visibility::HIDDEN ||
      contents->IsLoading() || contents->IsCurrentlyAudible() ||
      contents->IsBeingCaptured() ||
      contents->GetCurrentlyPlayingVideoCount() ||
      !contents->GetPrimaryMainFrame() ||
      !contents->GetPrimaryMainFrame()->IsRenderFrameLive() ||
      content::DevToolsAgentHost::IsDebuggerAttached(contents))
    return false;
  auto* tab = tabs::TabInterface::MaybeGetFromContents(contents);
  if (!tab || tab->IsPinned())
    return false;
  auto* downloads = contents->GetBrowserContext()->GetDownloadManager();
  // Conservatively protect the whole profile while a download is active.
  if (downloads && downloads->InProgressCount())
    return false;
  if (auto* session = content::MediaSession::GetIfExists(contents)) {
    const auto info = session->GetMediaSessionInfoSync();
    if (info && info->is_controllable &&
        info->playback_state ==
            media_session::mojom::MediaPlaybackState::kPlaying)
      return false;
  }
  return performance_manager::PerformanceManager::IsAvailable() &&
         performance_manager::PerformanceManager::
             GetPrimaryPageNodeForWebContents(contents);
}
bool TabSuspension::Request(content::WebContents* contents) {
  if (!Eligible(contents))
    return false;
  CreateForWebContents(contents);
  auto* state = FromWebContents(contents);
  state->automatic_ = false;
  state->quiet_since_.reset();
  if (!state->vote_)
    state->vote_ =
        std::make_unique<performance_manager::freezing::FreezingVote>(contents);
  return true;
}
void TabSuspension::Resume(content::WebContents* contents) {
  if (!contents)
    return;
  if (auto* state = FromWebContents(contents))
    state->Reset();
}
bool TabSuspension::Requested(content::WebContents* contents) {
  if (!contents)
    return false;
  auto* state = FromWebContents(contents);
  return state && state->vote_;
}
bool TabSuspension::MaybeAuto(content::WebContents* contents,
                              base::TimeDelta timeout,
                              base::TimeTicks now) {
  if (!contents)
    return false;
  if (!Eligible(contents)) {
    Resume(contents);
    return false;
  }
  CreateForWebContents(contents);
  auto* state = FromWebContents(contents);
  if (!timeout.is_positive()) {
    if (state->automatic_)
      state->vote_.reset();
    state->automatic_ = false;
    state->quiet_since_.reset();
    return false;
  }
  if (state->vote_)
    return state->automatic_;
  if (!state->quiet_since_ || now < *state->quiet_since_) {
    state->quiet_since_ = now;
    return false;
  }
  if (now - *state->quiet_since_ < timeout)
    return false;
  state->automatic_ = true;
  state->vote_ =
      std::make_unique<performance_manager::freezing::FreezingVote>(contents);
  return true;
}
void TabSuspension::Reset() {
  vote_.reset();
  automatic_ = false;
  quiet_since_.reset();
}
void TabSuspension::OnVisibilityChanged(content::Visibility) {
  Reset();
}
void TabSuspension::DidStartLoading() {
  Reset();
}
void TabSuspension::DidStopLoading() {
  Reset();
}
void TabSuspension::PrimaryPageChanged(content::Page&) {
  Reset();
}
void TabSuspension::MediaStartedPlaying(const MediaPlayerInfo&,
                                        const content::MediaPlayerId&) {
  Reset();
}
void TabSuspension::MediaStoppedPlaying(const MediaPlayerInfo&,
                                        const content::MediaPlayerId&,
                                        MediaStoppedReason) {
  Reset();
}
void TabSuspension::OnAudioStateChanged(bool) {
  Reset();
}
void TabSuspension::AboutToBeDiscarded(content::WebContents*) {
  Reset();
}
void TabSuspension::WasDiscarded() {
  Reset();
}
void TabSuspension::WebContentsDestroyed() {
  Reset();
}
void TabSuspension::DevToolsAgentHostAttached(
    content::DevToolsAgentHost* host) {
  if (host->GetWebContents() == web_contents())
    Reset();
}
void TabSuspension::DevToolsAgentHostDetached(
    content::DevToolsAgentHost* host) {
  if (host->GetWebContents() == web_contents())
    quiet_since_.reset();
}

constexpr std::pair<std::string_view, int> kNativeMappings[] = {
    {"spawn-tab", IDC_NEW_TAB},
    {"remove-tab", IDC_CLOSE_TAB},
    {"next-tab", IDC_SELECT_NEXT_TAB},
    {"previous-tab", IDC_SELECT_PREVIOUS_TAB},
    {"duplicate-tab", IDC_DUPLICATE_TAB},
    {"reopen-closed-tab", IDC_RESTORE_TAB},
    {"move-tab-left", IDC_MOVE_TAB_PREVIOUS},
    {"move-tab-right", IDC_MOVE_TAB_NEXT},
    {"go-back", IDC_BACK},
    {"go-forward", IDC_FORWARD},
    {"reload-active-tab", IDC_RELOAD},
    {"force-reload-tab", IDC_RELOAD_BYPASSING_CACHE},
    {"stop-loading", IDC_STOP},
    {"zoom-in", IDC_ZOOM_PLUS},
    {"zoom-out", IDC_ZOOM_MINUS},
    {"reset-zoom", IDC_ZOOM_NORMAL},
    {"toggle-find", IDC_FIND},
    {"toggle-devtools", IDC_DEV_TOOLS},
    {"toggle-fullscreen", IDC_FULLSCREEN},
    {"print-page", IDC_PRINT},
    {"toggle-pin-tab", IDC_WINDOW_PIN_TAB},
    {"focus-webview", IDC_FOCUS_WEB_CONTENTS_PANE},
    {"focus-nav-buttons", IDC_FOCUS_TOOLBAR},
    {"toggle-address", IDC_FOCUS_LOCATION},
    {"toggle-menu-bar", IDC_SHOW_APP_MENU}};
class PageKeyboardState
    : public content::WebContentsObserver,
      public content::WebContentsUserData<PageKeyboardState> {
 public:
  ~PageKeyboardState() override = default;
  KeyboardRouter& router() {
    auto& service = FlyWindowController::Get().commands();
    if (generation_ != service.generation()) {
      router_.Configure(service.Read().value_or(DefaultKeyboardConfig()));
      generation_ = service.generation();
    }
    return router_;
  }
  void OnVisibilityChanged(content::Visibility visibility) override {
    router_.ClearPending();
  }
  void OnWebContentsLostFocus(content::RenderWidgetHost* host) override {
    router_.ClearPending();
  }
  void DidFinishNavigation(content::NavigationHandle* navigation) override {
    if (navigation->HasCommitted() && navigation->IsInPrimaryMainFrame() &&
        !navigation->IsSameDocument()) {
      router_.ClearPending();
    }
  }

 private:
  friend class content::WebContentsUserData<PageKeyboardState>;
  explicit PageKeyboardState(content::WebContents* contents)
      : content::WebContentsObserver(contents),
        content::WebContentsUserData<PageKeyboardState>(*contents),
        router_(DefaultKeyboardConfig()) {}
  KeyboardRouter router_;
  uint64_t generation_ = 0;
  WEB_CONTENTS_USER_DATA_KEY_DECL();
};
WEB_CONTENTS_USER_DATA_KEY_IMPL(PageKeyboardState);

std::string EventStroke(const input::NativeWebKeyboardEvent& event) {
  using Event = blink::WebInputEvent;
  const auto modifiers = event.GetModifiers();
  const ui::DomKey dom_key(event.dom_key);
  if (dom_key.IsDeadKey() || dom_key.IsComposeKey() ||
      (modifiers & (Event::kIsComposing | Event::kAltGrKey))) {
    return {};
  }
  auto key = ui::KeycodeConverter::DomKeyToKeyString(dom_key);
  if (key == "Dead" || key == "Process" || key == "Unidentified") {
    return {};
  }
  if (key == " ")
    key = "Space";
  if (key == "+")
    key = "Plus";
  // Match the existing recorder's modified digit grammar on all layouts.
  const auto code = ui::KeycodeConverter::DomCodeToCodeString(
      static_cast<ui::DomCode>(event.dom_code));
  if ((modifiers & Event::kShiftKey) &&
      (modifiers & (Event::kControlKey | Event::kAltKey | Event::kMetaKey)) &&
      code.size() == 6 && code.starts_with("Digit")) {
    key = code.substr(5);
  }
  std::string stroke;
  if (modifiers & Event::kControlKey)
    stroke += "Ctrl+";
  if (modifiers & Event::kAltKey)
    stroke += "Alt+";
  if (modifiers & Event::kShiftKey)
    stroke += "Shift+";
  if (modifiers & Event::kMetaKey)
    stroke += "Meta+";
  return NormalizeStroke(stroke + key);
}

bool SplitCommand(BrowserWindowInterface* browser, std::string_view action) {
  auto* model = browser->GetTabStripModel();
  if (model->empty())
    return false;
  const auto existing = model->GetSplitForTab(model->active_index());
  if (action == "close-split-view") {
    if (existing)
      model->RemoveSplit(*existing);
    return true;
  }
  if (action == "switch-split-focus" || action == "swap-split-panes" ||
      action == "cycle-split-orientation") {
    if (!existing) {
      return action == "cycle-split-orientation"
                 ? SplitCommand(browser, "toggle-split")
                 : true;
    }
    auto* data = model->GetSplitData(*existing);
    if (action == "swap-split-panes") {
      model->ReverseTabsInSplit(*existing);
    } else if (action == "switch-split-focus") {
      for (auto* tab : data->ListTabs()) {
        if (tab != model->GetActiveTab()) {
          model->ActivateTab(tab);
          tab->GetContents()->Focus();
          break;
        }
      }
    } else {
      using Layout = split_tabs::SplitTabLayout;
      if (data->visual_data()->split_layout() == Layout::kSideBySide) {
        model->UpdateSplitLayout(*existing, Layout::kStacked);
      } else {
        model->RemoveSplit(*existing);
      }
    }
    return true;
  }
  if (action != "toggle-split" && action != "toggle-split-horizontal") {
    return false;
  }
  if (existing) {
    if (action == "toggle-split-horizontal" &&
        model->GetSplitData(*existing)->visual_data()->split_layout() !=
            split_tabs::SplitTabLayout::kStacked) {
      model->UpdateSplitLayout(*existing, split_tabs::SplitTabLayout::kStacked);
    } else {
      model->RemoveSplit(*existing);
    }
    return true;
  }
  if (model->count() < 2) {
    if (!OpenNativeBlankTab(browser))
      return false;
    model->AddToNewSplit({0, 1},
                         split_tabs::SplitTabVisualData(
                             action == "toggle-split-horizontal"
                                 ? split_tabs::SplitTabLayout::kStacked
                                 : split_tabs::SplitTabLayout::kSideBySide),
                         split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
    return true;
  }
  const auto first = model->active_index();
  const auto second = first + 1 < model->count() ? first + 1 : first - 1;
  model->AddToNewSplit({first, second},
                       split_tabs::SplitTabVisualData(
                           action == "toggle-split-horizontal"
                               ? split_tabs::SplitTabLayout::kStacked
                               : split_tabs::SplitTabLayout::kSideBySide),
                       split_tabs::SplitTabCreatedSource::kKeyboardShortcut);
  return true;
}
}  // namespace

bool OpenNativeBlankTab(BrowserWindowInterface* browser, int index) {
  if (!browser || !chrome::IsCommandEnabled(browser, IDC_NEW_TAB))
    return false;
  NavigateParams params(browser, GURL(url::kAboutBlankURL),
                        ui::PAGE_TRANSITION_TYPED);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  params.window_action = NavigateParams::WindowAction::kShowWindow;
  if (index >= 0) {
    params.tabstrip_index = index;
    params.tabstrip_add_types |= AddTabTypes::ADD_FORCE_INDEX;
  }
  Navigate(&params);
  return params.navigated_or_inserted_contents != nullptr;
}

bool RequestNativeTabSuspension(content::WebContents* contents) {
  return TabSuspension::Request(contents);
}
void ResumeNativeTab(content::WebContents* contents) {
  TabSuspension::Resume(contents);
}
bool NativeTabSuspensionRequested(content::WebContents* contents) {
  return TabSuspension::Requested(contents);
}

bool MaybeAutoSuspendNativeTab(content::WebContents* contents,
                               base::TimeDelta timeout,
                               base::TimeTicks now) {
  return TabSuspension::MaybeAuto(contents, timeout, now);
}

std::string NativeEventStroke(const input::NativeWebKeyboardEvent& event) {
  return EventStroke(event);
}

content::KeyboardEventProcessingResult RouteNativePageKey(
    BrowserWindowInterface* browser,
    const input::NativeWebKeyboardEvent& event,
    bool browser_ui_editable) {
  using Result = content::KeyboardEventProcessingResult;
  if (event.GetType() != blink::WebInputEvent::Type::kRawKeyDown) {
    return Result::NOT_HANDLED;
  }
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents)
    return Result::NOT_HANDLED;
  auto& window = FlyWindowController::Get();
  if (window.hints().HandleKey(contents, event))
    return Result::HANDLED;
  PageKeyboardState::CreateForWebContents(contents);
  auto& router = PageKeyboardState::FromWebContents(contents)->router();
  const auto key = router.Route(
      {.stroke = EventStroke(event),
       .time = base::TimeTicks::Now(),
       .repeat =
           bool(event.GetModifiers() & blink::WebInputEvent::kIsAutoRepeat),
       .composing =
           event.skip_if_unhandled ||
           bool(event.GetModifiers() & (blink::WebInputEvent::kIsComposing |
                                        blink::WebInputEvent::kAltGrKey)),
       .editable =
           browser_ui_editable || contents->IsFocusedElementEditable()});
  window.UpdateModeLabel();
  if (key.kind == KeyResultKind::kPass)
    return Result::NOT_HANDLED;
  if (key.kind == KeyResultKind::kExitInsert) {
    contents->ClearFocusedElement();
    contents->Focus();
  } else if (key.kind == KeyResultKind::kAction) {
    // Native commands can close the BrowserView; never read it afterwards.
    FlyWindowController::Get().RunCommand(browser, key.action);
  }
  return Result::HANDLED;
}

std::u16string NativePageModeLabel(BrowserWindowInterface* browser) {
  if (browser && browser->IsDeleteScheduled())
    return u"NORMAL";
  auto* contents =
      browser ? browser->GetTabStripModel()->GetActiveWebContents() : nullptr;
  auto* state =
      contents ? PageKeyboardState::FromWebContents(contents) : nullptr;
  if (state && state->router().mode() == KeyboardMode::kPassthrough)
    return u"PASSTHROUGH";
  if ((state && state->router().mode() == KeyboardMode::kInsert) ||
      (contents && contents->IsFocusedElementEditable() &&
       contents->GetRenderWidgetHostView() &&
       contents->GetRenderWidgetHostView()->HasFocus()))
    return u"INSERT";
  return u"NORMAL";
}

bool InvokeNativeCommand(BrowserWindowInterface* browser, std::string action) {
  if (!browser || !IsKnownAction(action))
    return false;
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  if (!contents)
    return false;
  PageKeyboardState::CreateForWebContents(contents);
  PageKeyboardState::FromWebContents(contents)->router().Invoke(action);
  auto& window = FlyWindowController::Get();
  const bool handled = window.RunCommand(browser, action);
  window.UpdateModeLabel();
  return handled;
}

bool NativeCommandAvailable(BrowserWindowInterface* browser,
                            std::string_view action) {
  if (!browser)
    return false;
  if (action == "move-tab-to-workspace") {
    const auto* window = FlyWindowController::GetIfCreated();
    return window &&
           window->CanMoveTab(
               browser, browser->GetTabStripModel()->GetActiveWebContents());
  }
  if (action == "toggle-dashboard") {
    const auto* window = FlyWindowController::GetIfCreated();
    return window && window->IsSelected(browser) &&
           window->CanShowWorkspacePicker();
  }
  if (action == "toggle-global-audio-mute" || action == "toggle-global-media" ||
      action == "hibernate-tabs" || action == "wake-all-tabs") {
    const auto* window = FlyWindowController::GetIfCreated();
    return window && window->IsSelected(browser) &&
           !browser->GetTabStripModel()->empty();
  }
  if (action == "toggle-bookmarks" || action == "add-bookmark") {
    auto* window = FlyWindowController::GetIfCreated();
    auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
    return window && (action == "toggle-bookmarks"
        ? window->bookmarks().Read().has_value()
        : contents && window->bookmarks().CanAdd(contents->GetLastCommittedURL().spec()));
  }
  if (action == "toggle-theme") {
    auto* window = FlyWindowController::GetIfCreated();
    return window && window->preferences().CanEditTheme();
  }
  if (action == "toggle-zen-mode" || action == "toggle-sidebar-pin") {
    auto* window = FlyWindowController::GetIfCreated();
    return window && window->preferences().CanEditSidebarAutoCollapse();
  }
  if (action == "toggle-window-controls") {
    auto* window = FlyWindowController::GetIfCreated();
    return window && window->preferences().CanEditWindowControls();
  }
  if (action == "toggle-status-bar") {
    auto* window = FlyWindowController::GetIfCreated();
    return window && window->preferences().CanEditStatusBar();
  }
  if (action == "toggle-clock" || action == "cycle-clock-format") {
    auto* window = FlyWindowController::GetIfCreated();
    return window && window->preferences().CanEditClock();
  }
  if (action == "toggle-help" || action == "open-keyboard-settings")
    return FlyWindowController::GetIfCreated() != nullptr;
  if (action == "toggle-private-mode") {
    const auto* window = FlyWindowController::GetIfCreated();
    return window && window->CanTogglePrivate(browser);
  }
  for (const auto& [name, command] : kNativeMappings) {
    if (action == name) {
      auto* tabs = browser->GetTabStripModel();
      if (action == "remove-tab" &&
          tabs->active_index() != TabStripModel::kNoTab &&
          tabs->IsTabPinned(tabs->active_index()))
        return false;
      return chrome::IsCommandEnabled(browser, command);
    }
  }
  constexpr std::string_view actions[] = {"enter-insert",
                                          "toggle-passthrough",
                                          "toggle-tab-audio-mute",
                                          "yank-url",
                                          "scroll-down",
                                          "scroll-up",
                                          "scroll-left",
                                          "scroll-right",
                                          "half-page-down",
                                          "half-page-up",
                                          "page-top",
                                          "page-bottom",
                                          "toggle-split",
                                          "toggle-split-horizontal",
                                          "close-split-view",
                                          "switch-split-focus",
                                          "swap-split-panes",
                                          "cycle-split-orientation"};
  return std::ranges::find(actions, action) != std::end(actions);
}

bool ExecuteNativeCommand(BrowserWindowInterface* browser,
                          std::string_view action) {
  if (action == "spawn-tab")
    return OpenNativeBlankTab(browser);
  for (const auto& [name, command] : kNativeMappings) {
    if (action == name) {
      if (action == "remove-tab") {
        auto* tabs = browser->GetTabStripModel();
        if (tabs->active_index() != TabStripModel::kNoTab &&
            tabs->IsTabPinned(tabs->active_index()))
          return true;
      }
      // A disabled native command is a handled key, just like upstream.
      if (chrome::IsCommandEnabled(browser, command)) {
        chrome::ExecuteCommand(browser, command);
      }
      return true;
    }
  }
  if (action.size() == 5 && action.starts_with("tab-") &&
      action.back() >= '1' && action.back() <= '9') {
    const int command = action.back() == '9'
                            ? IDC_SELECT_LAST_TAB
                            : IDC_SELECT_TAB_0 + action.back() - '1';
    if (chrome::IsCommandEnabled(browser, command))
      chrome::ExecuteCommand(browser, command);
    return true;
  }
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  using Scroll = content::WebContents::PageScrollCommand;
  constexpr std::pair<std::string_view, Scroll> scrolling[] = {
      {"scroll-down", Scroll::kStepDown},
      {"scroll-up", Scroll::kStepUp},
      {"scroll-left", Scroll::kStepLeft},
      {"scroll-right", Scroll::kStepRight},
      {"half-page-down", Scroll::kHalfPageDown},
      {"half-page-up", Scroll::kHalfPageUp},
      {"page-top", Scroll::kTop},
      {"page-bottom", Scroll::kBottom}};
  for (const auto& [name, command] : scrolling) {
    if (action == name) {
      if (contents)
        contents->ScrollFocusedPage(command);
      return true;
    }
  }
  if (action == "toggle-tab-audio-mute") {
    if (contents)
      SetTabAudioMuted(contents, !contents->IsAudioMuted(),
                       TabMutedReason::kAudioIndicator, std::string());
    return true;
  }
  if (action == "yank-url") {
    if (contents) {
      ui::ScopedClipboardWriter writer(ui::ClipboardBuffer::kCopyPaste);
      writer.WriteText(base::UTF8ToUTF16(contents->GetVisibleURL().spec()));
    }
    return true;
  }
  if (action == "enter-insert" || action == "toggle-passthrough")
    return true;
  return SplitCommand(browser, action);
}
}  // namespace fly
