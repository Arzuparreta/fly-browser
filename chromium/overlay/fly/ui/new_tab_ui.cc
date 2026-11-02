// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/new_tab_ui.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/containers/fixed_flat_set.h"
#include "base/containers/span.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/process/process_metrics.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/webui_config.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "fly/bookmarks/bookmark_service.h"
#include "fly/commands/keyboard_config.h"
#include "fly/preferences/new_tab_tips.h"
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/fly_theme.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"
#include "ui/aura/window.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/gfx/animation/animation.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/view.h"
#include "ui/views/widget/native_widget_aura.h"
#include "ui/views/widget/widget.h"

namespace fly {
namespace {
// The files the page may load, relative to <out>/fly_resources/new_tab.
constexpr auto kFiles = base::MakeFixedFlatSet<std::string_view>({
    "new-tab.html",
    "new-tab.css",
    "new-tab.js",
    "fonts/fonts.css",
    "fonts/doto-0.woff2",
    "fonts/plex-mono-0.woff2",
    "fonts/plex-mono-1.woff2",
    "fonts/plex-mono-2.woff2",
    "fonts/plex-sans-0.woff2",
    "fonts/plex-sans-1.woff2",
    "fonts/plex-sans-2.woff2",
    "identity/memory-chasis@2x.png",
    "identity/memory-papel@2x.png",
    "identity/paper-grain.png",
    "identity/pixel-32.png",
});

std::string PagePath(const std::string& request) {
  std::string path = request.substr(0, request.find_first_of("?#"));
  return path.empty() ? "new-tab.html" : path;
}
// Immutable, bounded product assets are read once per browser process, off the
// UI thread on first demand; concurrent tabs share the same pending load.
// No profile data enters this cache.
class NewTabResources {
 public:
  using Bytes = scoped_refptr<base::RefCountedMemory>;
  using Bundle = std::map<std::string, Bytes>;
  struct LoadedBundle {
    Bundle files;
    SkBitmap dark;
    SkBitmap light;
  };
  static NewTabResources& Get() {
    static base::NoDestructor<NewTabResources> instance;
    return *instance;
  }
  void Warm() {
    if (loading_ || loaded_)
      return;
    loading_ = true;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
        base::BindOnce([] {
          TRACE_EVENT("browser", "Fly.NewTabResources.ReadBundle");
          LoadedBundle bundle;
          base::FilePath assets;
          if (!base::PathService::Get(base::DIR_ASSETS, &assets))
            return bundle;
          for (const auto path : kFiles) {
            std::string bytes;
            if (base::ReadFileToStringWithMaxSize(
                    assets.AppendASCII("fly_resources")
                        .AppendASCII("new_tab")
                        .AppendASCII(path),
                    &bytes, 4 * 1024 * 1024)) {
              if (path == "identity/memory-chasis@2x.png")
                bundle.dark = gfx::PNGCodec::Decode(base::as_byte_span(bytes));
              else if (path == "identity/memory-papel@2x.png")
                bundle.light = gfx::PNGCodec::Decode(base::as_byte_span(bytes));
              bundle.files.emplace(path, base::MakeRefCounted<base::RefCountedString>(
                                       std::move(bytes)));
            }
          }
          return bundle;
        }),
        base::BindOnce(&NewTabResources::Loaded, base::Unretained(this)));
  }
  const gfx::ImageSkia& Memory(bool dark) const {
    return dark ? dark_ : light_;
  }
  void Read(std::string path, content::WebUIDataSource::GotDataCallback done) {
    if (!kFiles.contains(path)) {
      std::move(done).Run(nullptr);
    } else if (loaded_) {
      const auto it = bundle_.find(path);
      std::move(done).Run(it == bundle_.end() ? nullptr : it->second);
    } else {
      pending_.emplace_back(std::move(path), std::move(done));
      Warm();
    }
  }

 private:
  void Loaded(LoadedBundle bundle) {
    // Decode once off the UI thread, then share immutable pixels across tabs.
    bundle.dark.setImmutable();
    bundle.light.setImmutable();
    dark_ = gfx::ImageSkia::CreateFromBitmap(bundle.dark, 2.0f);
    light_ = gfx::ImageSkia::CreateFromBitmap(bundle.light, 2.0f);
    bundle_ = std::move(bundle.files);
    loaded_ = true;
    loading_ = false;
    auto pending = std::move(pending_);
    for (auto& [path, done] : pending)
      Read(std::move(path), std::move(done));
  }
  bool loading_ = false;
  bool loaded_ = false;
  Bundle bundle_;
  gfx::ImageSkia dark_;
  gfx::ImageSkia light_;
  std::vector<std::pair<std::string, content::WebUIDataSource::GotDataCallback>>
      pending_;
};

std::string Hex(SkColor color) {
  return base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

// The key a tip teaches: the NORMAL key when there is one, as it is the
// shortest, otherwise the first shortcut. No key, no tip.
std::optional<base::DictValue> TipKey(const KeyboardConfig& config,
                                      std::string_view action) {
  for (const auto [table, normal] :
       {std::pair{&config.normal, true}, std::pair{&config.shortcuts, false}}) {
    const auto bindings = table->find(std::string(action));
    if (bindings != table->end() && !bindings->second.empty())
      return base::DictValue()
          .Set("key", bindings->second.front())
          .Set("normal", normal);
  }
  return std::nullopt;
}
KeyboardConfig CurrentKeyboard(FlyApp& app) {
  return app.commands().Read().value_or(DefaultKeyboardConfig());
}
bool TipsEnabled(FlyApp& app) {
  return app.preferences().ReadBool("new_tab_tips").value_or(true);
}
// The tip a new page starts with; moves the rotation on.
std::string NextTip(FlyApp& app, const KeyboardConfig& config) {
  if (!TipsEnabled(app))
    return std::string();
  const auto tip = app.new_tab_tips().Next([&](std::string_view action) {
    return TipKey(config, action).has_value();
  });
  return tip ? std::string(tip->action) : std::string();
}

// A current snapshot is embedded as inert JSON in the document. The first
// stylesheet includes the current theme/geometry, so painting never depends on
// a renderer -> browser -> renderer ready message. Dynamic bytes are never
// cached with the product assets, nor shared between regular/private pages.
// `tip` is the action of the keyboard tip to show, if any.
base::DictValue PageData(bool private_page, std::string_view tip = {}) {
  const auto& colors = GetFlyColors();
  base::DictValue colours;
  colours.Set("--bg-main", Hex(colors.bg_main));
  colours.Set("--text", Hex(colors.text));
  colours.Set("--accent", Hex(colors.accent));
  colours.Set("--panel", Hex(colors.panel));
  colours.Set("--cell", Hex(colors.cell));
  colours.Set("--border", Hex(colors.border));
  colours.Set("--dim", Hex(colors.dim));
  colours.Set("--link", Hex(colors.link));
  const double scale = PreferencesService(*g_browser_process->local_state())
                                   .ReadString("interface_scale")
                                   .value_or("normal") == "large"
                           ? 1.25
                           : 1.0;
  base::DictValue data;
  if (auto* window = FlyApp::GetIfCreated()) {
    const auto config = CurrentKeyboard(*window);
    for (const auto [key, action] : {std::pair{"searchBinding", "toggle-address"},
                                    std::pair{"bookmarksBinding", "toggle-bookmarks"}}) {
      const auto bindings = config.shortcuts.find(action);
      if (bindings != config.shortcuts.end() && !bindings->second.empty())
        data.Set(key, bindings->second.front());
    }
    const auto found = NewTabTips::Find(tip);
    if (auto key = found ? TipKey(config, tip) : std::nullopt) {
      key->Set("action", tip);
      key->Set("text", found->text);
      data.Set("tip", std::move(*key));
    }
  }
  data.Set("light", !colors.dark);
  data.Set("scale", scale);
  data.Set("colours", std::move(colours));
  return data;
}
void Serve(bool private_page,
           const std::string& request,
           content::WebUIDataSource::GotDataCallback done) {
  const auto path = PagePath(request);
  NewTabResources::Get().Read(
      path,
      base::BindOnce(
          [](bool private_page, std::string path,
             content::WebUIDataSource::GotDataCallback done,
             scoped_refptr<base::RefCountedMemory> bytes) {
            if (!bytes || (path != "new-tab.html" && path != "new-tab.css")) {
              std::move(done).Run(std::move(bytes));
              return;
            }
            std::string tip;
            if (auto* app = FlyApp::GetIfCreated();
                app && path == "new-tab.html")
              tip = NextTip(*app, CurrentKeyboard(*app));
            const auto data = PageData(private_page, tip);
            std::string text(base::as_string_view(base::span(*bytes)));
            if (path == "new-tab.html") {
              auto json = base::WriteJson(data).value_or("{}");
              // Titles/URLs are untrusted. Never let JSON close its inert
              // script.
              base::ReplaceSubstringsAfterOffset(&json, 0, "<", "\\u003c");
              base::ReplaceSubstringsAfterOffset(
                  &text, 0, "<!--INITIAL_DATA-->",
                  "<script id=\"initial-data\" type=\"application/json\">" +
                      json + "</script>");
              base::ReplaceSubstringsAfterOffset(
                  &text, 0, "<!--BODY_CLASS-->",
                  data.FindBool("light").value_or(false) ? "light" : "");
              if (data.FindBool("light").value_or(false))
                base::ReplaceSubstringsAfterOffset(
                    &text, 0, "memory-chasis@2x.png", "memory-papel@2x.png");
            } else {
              text += "\n:root{";
              for (const auto [key, value] : *data.FindDict("colours"))
                text += key + ":" + value.GetString() + ";";
              text +=
                  "--scale:" +
                  std::to_string(data.FindDouble("scale").value_or(1)) +
                  ";color-scheme:" +
                  (data.FindBool("light").value_or(false) ? "light" : "dark") +
                  ";}";
            }
            std::move(done).Run(
                base::MakeRefCounted<base::RefCountedString>(std::move(text)));
          },
          private_page, path, std::move(done)));
}


class NewTabHandler : public content::WebUIMessageHandler,
                      public content::WebContentsObserver {
 public:
  NewTabHandler() = default;
  NewTabHandler(const NewTabHandler&) = delete;
  NewTabHandler& operator=(const NewTabHandler&) = delete;
  ~NewTabHandler() override = default;

  void RegisterMessages() override {
    Observe(web_ui()->GetWebContents());
    registrar_.Init(g_browser_process->local_state());
    for (const char* pref :
         {kPreferencesPref, kKeyboardPref, kNewTabTipsPref})
      registrar_.Add(pref, base::BindRepeating(&NewTabHandler::Refresh,
                                              base::Unretained(this)));
    web_ui()->RegisterMessageCallback("ready", base::BindRepeating(
        &NewTabHandler::OnReady, base::Unretained(this)));
    web_ui()->RegisterMessageCallback("openSearch", base::BindRepeating(
        &NewTabHandler::Open, base::Unretained(this), "toggle-address"));
    web_ui()->RegisterMessageCallback("openBookmarks", base::BindRepeating(
        &NewTabHandler::Open, base::Unretained(this), "toggle-bookmarks"));
    web_ui()->RegisterMessageCallback("dismissTip", base::BindRepeating(
        &NewTabHandler::DismissTip, base::Unretained(this)));
    web_ui()->RegisterMessageCallback("artwork", base::BindRepeating(
        &NewTabHandler::Artwork, base::Unretained(this)));
  }
  void OnVisibilityChanged(content::Visibility) override { Refresh(); }

 private:
  bool OffTheRecord() { return Profile::FromWebUI(web_ui())->IsOffTheRecord(); }
  void OnReady(const base::ListValue& args) {
    // The page reports the tip its document was served with.
    if (!args.empty() && args[0].is_string() &&
        NewTabTips::Find(args[0].GetString()))
      tip_ = args[0].GetString();
    AllowJavascript();
    Refresh();
  }
  void Artwork(const base::ListValue& args) {
    if (args.size() != 5 || !args[4].is_bool())
      return;
    for (size_t i = 0; i < 4; ++i) {
      if (!args[i].is_double() && !args[i].is_int())
        return;
      if (!std::isfinite(args[i].GetDouble()) ||
          std::abs(args[i].GetDouble()) > 2)
        return;
    }
    NewTabArtwork::CreateForWebContents(web_contents());
    NewTabArtwork::FromWebContents(web_contents())->Update(
        gfx::RectF(args[0].GetDouble(), args[1].GetDouble(),
                   args[2].GetDouble(), args[3].GetDouble()),
        args[4].GetBool());
    if (IsJavascriptAllowed())
      CallJavascriptFunction("useNativeFlyArtwork");
  }
  // Keeps this page's tip until it is learned, dismissed or loses its key;
  // then the next pending one takes its place. A tip dismissed here leaves
  // the page without one.
  std::string CurrentTip() {
    auto* app = FlyApp::GetIfCreated();
    if (!app || dismissed_ || !TipsEnabled(*app))
      return std::string();
    const auto config = CurrentKeyboard(*app);
    if (tip_.empty() || !app->new_tab_tips().Pending(tip_) ||
        !TipKey(config, tip_))
      tip_ = NextTip(*app, config);
    return tip_;
  }
  void DismissTip(const base::ListValue&) {
    auto* app = FlyApp::GetIfCreated();
    if (!app || tip_.empty())
      return;
    dismissed_ = true;
    // Other new tabs showing the same tip move on to the next one.
    app->new_tab_tips().Dismiss(tip_);
    Refresh();
  }
  bool Visible() const {
    return web_contents() &&
           web_contents()->GetVisibility() == content::Visibility::VISIBLE;
  }
  void Refresh() {
    if (!IsJavascriptAllowed())
      return;
    if (Visible())
      web_contents()->SetPageBaseBackgroundColor(GetFlyColors().bg_main);
    CallJavascriptFunction("updateFlyNewTab",
                           base::Value(PageData(OffTheRecord(), CurrentTip())));
  }
  void Open(std::string action, const base::ListValue&) {
    // A background or detached WebUI must not operate another tab's window.
    auto* window = FlyWindowController::FromContents(web_contents());
    if (Visible() && window && window->ActiveContents() == web_contents())
      window->RunCommand(action);
  }
  PrefChangeRegistrar registrar_;
  std::string tip_;
  bool dismissed_ = false;
};

class NewTabUI : public content::WebUIController {
 public:
  explicit NewTabUI(content::WebUI* web_ui) : content::WebUIController(web_ui) {
    web_ui->GetWebContents()->SetPageBaseBackgroundColor(
        GetFlyColors().bg_main);
    auto* source = content::WebUIDataSource::CreateAndAdd(
        Profile::FromWebUI(web_ui), kFlyNewTabHost);
    source->SetRequestFilter(
        base::BindRepeating([](const std::string&) { return true; }),
        base::BindRepeating(&Serve,
                            Profile::FromWebUI(web_ui)->IsOffTheRecord()));
    using Directive = network::mojom::CSPDirectiveName;
    source->OverrideContentSecurityPolicy(Directive::ScriptSrc,
                                          "script-src 'self';");
    source->OverrideContentSecurityPolicy(Directive::StyleSrc,
                                          "style-src 'self';");
    source->OverrideContentSecurityPolicy(Directive::ImgSrc,
                                          "img-src 'self' data:;");
    source->OverrideContentSecurityPolicy(Directive::FontSrc,
                                          "font-src 'self';");
    web_ui->AddMessageHandler(std::make_unique<NewTabHandler>());
  }
  NewTabUI(const NewTabUI&) = delete;
  NewTabUI& operator=(const NewTabUI&) = delete;
  ~NewTabUI() override = default;

  WEB_UI_CONTROLLER_TYPE_DECL();
};
WEB_UI_CONTROLLER_TYPE_IMPL(NewTabUI)

class NewTabConfig : public content::DefaultWebUIConfig<NewTabUI> {
 public:
  NewTabConfig()
      : DefaultWebUIConfig(content::kChromeUIScheme, kFlyNewTabHost) {}
};
}  // namespace

class NewTabArtworkView : public views::View {
  METADATA_HEADER(NewTabArtworkView, views::View)
 public:
  NewTabArtworkView() { SetCanProcessEventsWithinSubtree(false); }
  void SetPose(int pose) {
    if (pose_ == pose && dark_ == GetFlyColors().dark)
      return;
    pose_ = pose;
    dark_ = GetFlyColors().dark;
    SchedulePaint();
  }
  void OnPaint(gfx::Canvas* canvas) override {
    const auto& strip = NewTabResources::Get().Memory(dark_);
    if (!strip.isNull())
      canvas->DrawImageInt(strip, pose_ * 280, 0, 280, 280, 0, 0, width(),
                           height(), true);
  }

 private:
  int pose_ = 0;
  bool dark_ = GetFlyColors().dark;
};
BEGIN_METADATA(NewTabArtworkView)
END_METADATA

WEB_CONTENTS_USER_DATA_KEY_IMPL(NewTabArtwork);
NewTabArtwork::NewTabArtwork(content::WebContents* contents)
    : content::WebContentsObserver(contents),
      content::WebContentsUserData<NewTabArtwork>(*contents) {}
NewTabArtwork::~NewTabArtwork() {
  DestroyOverlay();
}

void NewTabArtwork::Update(const gfx::RectF& bounds, bool reduced_motion) {
  if (!web_contents() ||
      !IsFlyNewTabPage(web_contents()->GetLastCommittedURL()))
    return;
  active_ = true;
  relative_bounds_ = bounds;
  reduced_motion_ = reduced_motion;
  Sync();
}
bool NewTabArtwork::visible_for_testing() const {
  return overlay_ && overlay_->IsVisible();
}
gfx::Rect NewTabArtwork::bounds_for_testing() const {
  return overlay_ ? overlay_->GetWindowBoundsInScreen() : gfx::Rect();
}
void NewTabArtwork::Sync() {
  auto* parent = web_contents() ? web_contents()->GetNativeView() : nullptr;
  if (parent != parent_) {
    DestroyOverlay();
    parent_ = parent;
    if (parent_)
      parent_->AddObserver(this);
  }
  const bool visible =
      active_ && !relative_bounds_.IsEmpty() && parent && parent->IsVisible() &&
      web_contents()->GetVisibility() == content::Visibility::VISIBLE;
  if (!visible) {
    timer_.Stop();
    SetPose(0);
    // Keep observing the container: it can become visible after the tab's
    // visibility notification (for example when restoring a minimized window).
    artwork_ = nullptr;
    overlay_.reset();
    return;
  }
  if (!overlay_) {
    overlay_ = std::make_unique<views::Widget>();
    views::Widget::InitParams params(
        views::Widget::InitParams::CLIENT_OWNS_WIDGET,
        views::Widget::InitParams::TYPE_CONTROL);
    params.parent = parent;
    params.child = true;
    params.activatable = views::Widget::InitParams::Activatable::kNo;
    params.accept_events = false;
    params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
    params.native_widget = new views::NativeWidgetAura(overlay_.get());
    params.name = "FlyNewTabArtwork";
    overlay_->Init(std::move(params));
    artwork_ = overlay_->SetContentsView(std::make_unique<NewTabArtworkView>());
  }
  const auto size = parent_->bounds().size();
  overlay_->SetBounds(gfx::ToRoundedRect(gfx::RectF(
      relative_bounds_.x() * size.width(), relative_bounds_.y() * size.height(),
      relative_bounds_.width() * size.width(),
      relative_bounds_.height() * size.height())));
  artwork_->SetPose(pose_);
  overlay_->ShowInactive();
  parent_->StackChildAtTop(overlay_->GetNativeWindow());
  if (!loading_ || reduced_motion_ || gfx::Animation::PrefersReducedMotion()) {
    timer_.Stop();
    SetPose(0);
  } else if (!timer_.IsRunning()) {
    started_ = base::TimeTicks::Now() + base::Milliseconds(160);
    timer_.Start(FROM_HERE, base::Milliseconds(160), this,
                 &NewTabArtwork::Tick);
  }
}
void NewTabArtwork::SetPose(int pose) {
  pose_ = pose;
  if (artwork_)
    artwork_->SetPose(pose);
}
void NewTabArtwork::Tick() {
  ++ticks_;
  if (!active_ || !loading_ || !overlay_ || reduced_motion_ ||
      gfx::Animation::PrefersReducedMotion()) {
    timer_.Stop();
    SetPose(0);
    return;
  }
  // Use elapsed time, not chained durations: a busy machine skips a missed
  // boundary instead of accumulating drift or replaying a burst of callbacks.
  constexpr std::array poses = {1, 2, 1, 2, 0};
  const int64_t position =
      (base::TimeTicks::Now() - started_).InMicroseconds() % 2400000;
  const int phase = std::min<int64_t>(position / 150000, 4);
  SetPose(poses[phase]);
  const int64_t next = phase < 4 ? (phase + 1) * 150000 : 2400000;
  timer_.Start(FROM_HERE, base::Microseconds(next - position), this,
               &NewTabArtwork::Tick);
}
void NewTabArtwork::Clear() {
  active_ = false;
  loading_ = committed_ = false;
  navigation_ = 0;
  timer_.Stop();
  SetPose(0);
  DestroyOverlay();
}
void NewTabArtwork::DestroyOverlay() {
  if (parent_)
    parent_->RemoveObserver(this);
  parent_ = nullptr;
  artwork_ = nullptr;
  overlay_.reset();
}
void NewTabArtwork::DidStartNavigation(content::NavigationHandle* navigation) {
  if (!active_ || !navigation->IsInPrimaryMainFrame() ||
      navigation->IsSameDocument())
    return;
  if (IsFlyNewTabPage(navigation->GetURL())) {
    Clear();
    return;
  }
  web_contents()->SetPageBaseBackgroundColor(std::nullopt);
  navigation_ = navigation->GetNavigationId();
  loading_ = true;
  Sync();
}
void NewTabArtwork::DidFinishNavigation(content::NavigationHandle* navigation) {
  if (!navigation_ || navigation->GetNavigationId() != navigation_)
    return;
  if (navigation->HasCommitted()) {
    committed_ = true;
    // The old WebUI is gone, but its surface can still be on screen.
    Sync();
  } else {
    navigation_ = 0;
    loading_ = false;
    if (committed_)
      Clear();
    else
      Sync();
  }
}
void NewTabArtwork::DidStopLoading() {
  navigation_ = 0;
  loading_ = false;
  if (committed_)
    Clear();
  else
    Sync();
}
void NewTabArtwork::DidFirstVisuallyNonEmptyPaint() {
  if (committed_)
    Clear();
}
void NewTabArtwork::OnFirstContentfulPaintInPrimaryMainFrame(base::TimeTicks) {
  if (committed_)
    Clear();
}
void NewTabArtwork::OnVisibilityChanged(content::Visibility) {
  Sync();
}
void NewTabArtwork::PrimaryMainFrameRenderProcessGone(base::TerminationStatus) {
  Clear();
}
void NewTabArtwork::WebContentsDestroyed() {
  Clear();
}
void NewTabArtwork::OnWindowBoundsChanged(aura::Window* window,
                                          const gfx::Rect&,
                                          const gfx::Rect&,
                                          ui::PropertyChangeReason) {
  if (window == parent_)
    Sync();
}
void NewTabArtwork::OnWindowVisibilityChanged(aura::Window* window, bool) {
  if (parent_ && window->Contains(parent_))
    Sync();
}
void NewTabArtwork::OnWindowAdded(aura::Window* window) {
  if (overlay_ && window != overlay_->GetNativeWindow())
    parent_->StackChildAtTop(overlay_->GetNativeWindow());
}
void NewTabArtwork::OnWindowDestroying(aura::Window*) {
  Clear();
}

NewTabHistory::NewTabHistory(ProfileManager& manager,
                             WorkspaceService& workspaces,
                             PrefService& prefs)
    : workspaces_(workspaces), history_(manager, workspaces) {
  history_.SetChangedCallback(
      base::BindRepeating(&NewTabHistory::Invalidate, base::Unretained(this)));
  registrar_.Init(&prefs);
  registrar_.Add(kWorkspacesPref,
                 base::BindRepeating(&NewTabHistory::OnWorkspacesChanged,
                                     base::Unretained(this)));
  OnWorkspacesChanged();
}
NewTabHistory::~NewTabHistory() = default;
base::CallbackListSubscription NewTabHistory::Subscribe(
    base::RepeatingClosure changed) {
  auto subscription = changed_.Add(std::move(changed));
  EnsureQuery();
  return subscription;
}
void NewTabHistory::OnWorkspacesChanged() {
  std::optional<std::vector<std::pair<std::string, std::string>>> catalog;
  if (const auto saved = workspaces_->Read()) {
    catalog.emplace();
    for (const auto& workspace : saved->items)
      catalog->emplace_back(workspace.id, workspace.profile_directory);
  }
  // Renaming/selecting a workspace does not change authorized history sources.
  if (catalog == catalog_)
    return;
  catalog_ = std::move(catalog);
  Invalidate();
}
void NewTabHistory::Invalidate() {
  valid_ = false;
  entries_.clear();
  history_.Cancel();
  querying_ = false;
  // Drop the cache immediately, but publish one replacement to visible pages
  // when the query finishes instead of flashing an empty list in between.
  EnsureQuery();
}
void NewTabHistory::EnsureQuery() {
  if (valid_ || querying_ || changed_.empty())
    return;
  querying_ = true;
  ++query_count_;
  history_.Query(
      base::BindOnce(&NewTabHistory::OnQuery, weak_factory_.GetWeakPtr()));
}
void NewTabHistory::OnQuery(std::optional<std::vector<HistoryEntry>> rows) {
  querying_ = false;
  valid_ = rows.has_value();
  entries_ = rows.value_or(std::vector<HistoryEntry>());
  changed_.Notify();
}

bool IsFlyNewTabPage(const GURL& url) {
  return url.is_empty() || url.IsAboutBlank() ||
         (url.SchemeIs(content::kChromeUIScheme) &&
          (url.host() == kFlyNewTabHost || url.host() == "newtab"));
}

bool RewriteNewTabURL(GURL* url, content::BrowserContext*) {
  if (!url->SchemeIs(content::kChromeUIScheme) || url->host() != "newtab")
    return false;
  *url = GURL(kFlyNewTabURL);
  return true;
}

void RegisterFlyNewTabPage() {
  static bool registered = false;
  if (registered)
    return;
  registered = true;
  content::WebUIConfigMap::GetInstance().AddWebUIConfig(
      std::make_unique<NewTabConfig>());
}
}  // namespace fly
