// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/new_tab_ui.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "base/base_paths.h"
#include "base/containers/fixed_flat_set.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/memory/ref_counted_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/path_service.h"
#include "base/process/process.h"
#include "base/process/process_metrics.h"
#include "base/strings/stringprintf.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "components/prefs/pref_change_registrar.h"
#include "content/public/browser/browser_child_process_host_iterator.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_controller.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/browser/web_ui_message_handler.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/webui_config.h"
#include "content/public/browser/webui_config_map.h"
#include "content/public/common/url_constants.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/sidebar_panel.h"
#include "fly/bookmarks/bookmark_service.h"
#include "services/network/public/mojom/content_security_policy.mojom.h"

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
void Serve(const std::string& request,
           content::WebUIDataSource::GotDataCallback done) {
  const std::string path = PagePath(request);
  if (!kFiles.contains(path)) {
    std::move(done).Run(nullptr);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce(
          [](std::string path) -> scoped_refptr<base::RefCountedMemory> {
            base::FilePath assets;
            std::string bytes;
            if (!base::PathService::Get(base::DIR_ASSETS, &assets) ||
                !base::ReadFileToStringWithMaxSize(
                    assets.AppendASCII("fly_resources")
                        .AppendASCII("new_tab")
                        .AppendASCII(path),
                    &bytes, 4 * 1024 * 1024))
              return nullptr;
            return base::MakeRefCounted<base::RefCountedString>(
                std::move(bytes));
          },
          path),
      std::move(done));
}

std::string Hex(SkColor color) {
  return base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

// Fly's processes: the browser, renderers and child processes (GPU,
// utilities), as the diagnostics hub counts them.
std::vector<base::ProcessId> FlyProcesses() {
  std::vector<base::ProcessId> pids = {base::Process::Current().Pid()};
  for (auto it = content::RenderProcessHost::AllHostsIterator(); !it.IsAtEnd();
       it.Advance()) {
    if (it.GetCurrentValue()->GetProcess().IsValid())
      pids.push_back(it.GetCurrentValue()->GetProcess().Pid());
  }
  for (content::BrowserChildProcessHostIterator it; !it.Done(); ++it) {
    if (it.GetProcess().IsValid())
      pids.push_back(it.GetProcess().Pid());
  }
  return pids;
}
int ResidentMegabytes(std::vector<base::ProcessId> pids) {
  uint64_t bytes = 0;
  for (const auto pid : pids) {
    const auto metrics = base::ProcessMetrics::CreateProcessMetrics(pid);
    if (const auto info = metrics->GetMemoryInfo(); info.has_value())
      bytes += info->resident_set_bytes;
  }
  return static_cast<int>(bytes / (1024 * 1024));
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
    for (const char* pref : {kPreferencesPref, kBookmarksPref, kWorkspacesPref}) {
      registrar_.Add(pref, base::BindRepeating(&NewTabHandler::Refresh,
                                             base::Unretained(this)));
    }
    web_ui()->RegisterMessageCallback(
        "ready", base::BindRepeating(&NewTabHandler::OnReady,
                                     base::Unretained(this)));
    web_ui()->RegisterMessageCallback(
        "hideSite", base::BindRepeating(&NewTabHandler::OnHideSite,
                                        base::Unretained(this)));
  }
  void OnJavascriptDisallowed() override {
    memory_timer_.Stop();
    weak_factory_.InvalidateWeakPtrs();
    sampling_ = false;
    if (history_)
      history_->Cancel();
  }
  void OnVisibilityChanged(content::Visibility) override { Refresh(); }

 private:
  bool OffTheRecord() {
    return Profile::FromWebUI(web_ui())->IsOffTheRecord();
  }
  void OnReady(const base::ListValue&) {
    AllowJavascript();
    Refresh();
  }
  bool Visible() const {
    return web_contents() &&
           web_contents()->GetVisibility() == content::Visibility::VISIBLE;
  }
  void Refresh() {
    if (!IsJavascriptAllowed())
      return;
    if (!Visible()) {
      memory_timer_.Stop();
      if (history_)
        history_->Cancel();
      return;
    }
    SendPage();
    if (!memory_timer_.IsRunning())
      StartMemory();
    // The page needs its own query: a picker may never have been opened,
    // and its cancellable query must not cancel another new tab or picker.
    if (OffTheRecord() ||
        PreferencesService(*g_browser_process->local_state())
                .ReadFrequentSitesVisible() != true) {
      if (history_)
        history_->Cancel();
      history_entries_.clear();
      return;
    }
    if (!history_) {
      workspaces_ = std::make_unique<WorkspaceService>(
          *g_browser_process->local_state());
      history_ = std::make_unique<HistoryService>(
          *g_browser_process->profile_manager(), *workspaces_);
      history_->SetChangedCallback(base::BindRepeating(
          &NewTabHandler::Refresh, base::Unretained(this)));
    }
    history_->Query(base::BindOnce(
        [](base::WeakPtr<NewTabHandler> handler,
           std::optional<std::vector<HistoryEntry>> rows) {
          if (!handler || !handler->IsJavascriptAllowed() || !handler->Visible())
            return;
          handler->history_entries_ = rows.value_or(std::vector<HistoryEntry>());
          handler->SendPage();
        }, weak_factory_.GetWeakPtr()));
  }
  void OnHideSite(const base::ListValue& args) {
    auto* window = FlyWindowController::GetIfCreated();
    if (window && !OffTheRecord() && !args.empty() && args[0].is_string())
      window->HideFrequentSite(args[0].GetString());
    SendPage();
  }
  void SendPage() {
    if (!IsJavascriptAllowed())
      return;
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
    // Read the committed preference, independent of observer callback ordering.
    const double scale = PreferencesService(*g_browser_process->local_state())
                                     .ReadString("interface_scale")
                                     .value_or("normal") == "large"
                             ? 1.25
                             : 1.0;
    base::DictValue data;
    if (auto* window = FlyWindowController::GetIfCreated()) {
      data = window->NewTabLists(OffTheRecord(), history_entries_);
      data.Set(
          "sidebarOverhang",
          window->preferences().ReadSidebarAutoCollapse().value_or(false)
              ? static_cast<int>((kSidebarWidth - kSidebarRailWidth) * scale)
              : 0);
    }
    data.Set("light", !colors.dark);
    data.Set("scale", scale);
    data.Set("colours", std::move(colours));
    CallJavascriptFunction("updateFlyNewTab", base::Value(std::move(data)));
  }
  // The memory figure follows Fly live, once a second while the page shows.
  void StartMemory() {
    SampleMemory();
    memory_timer_.Start(FROM_HERE, base::Seconds(1),
                        base::BindRepeating(&NewTabHandler::SampleMemory,
                                            base::Unretained(this)));
  }
  void SampleMemory() {
    if (sampling_ || !IsJavascriptAllowed() || !Visible())
      return;
    sampling_ = true;
    base::ThreadPool::PostTaskAndReplyWithResult(
        FROM_HERE,
        {base::MayBlock(), base::TaskPriority::USER_VISIBLE,
         base::TaskShutdownBehavior::CONTINUE_ON_SHUTDOWN},
        base::BindOnce(&ResidentMegabytes, FlyProcesses()),
        base::BindOnce(&NewTabHandler::OnMemory,
                       weak_factory_.GetWeakPtr()));
  }
  void OnMemory(int megabytes) {
    sampling_ = false;
    if (IsJavascriptAllowed() && Visible())
      CallJavascriptFunction("updateFlyNewTabMemory", base::Value(megabytes));
  }

  base::RepeatingTimer memory_timer_;
  bool sampling_ = false;
  PrefChangeRegistrar registrar_;
  std::unique_ptr<WorkspaceService> workspaces_;
  std::unique_ptr<HistoryService> history_;
  std::vector<HistoryEntry> history_entries_;
  base::WeakPtrFactory<NewTabHandler> weak_factory_{this};
};

class NewTabUI : public content::WebUIController {
 public:
  explicit NewTabUI(content::WebUI* web_ui) : content::WebUIController(web_ui) {
    auto* source = content::WebUIDataSource::CreateAndAdd(
        Profile::FromWebUI(web_ui), kFlyNewTabHost);
    source->SetRequestFilter(
        base::BindRepeating([](const std::string&) { return true; }),
        base::BindRepeating(&Serve));
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
