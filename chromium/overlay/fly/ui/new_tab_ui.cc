// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/new_tab_ui.h"

#include <map>
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
#include "fly/ui/fly_app.h"
#include "fly/ui/fly_window_controller.h"
#include "fly/ui/fly_theme.h"
#include "fly/ui/sidebar_panel.h"
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
// Immutable, bounded product assets are read once per browser process, off the
// UI thread on first demand; concurrent tabs share the same pending load.
// No profile data enters this cache.
class NewTabResources {
 public:
  using Bytes = scoped_refptr<base::RefCountedMemory>;
  using Bundle = std::map<std::string, Bytes>;
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
          Bundle bundle;
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
              bundle.emplace(path, base::MakeRefCounted<base::RefCountedString>(
                                       std::move(bytes)));
            }
          }
          return bundle;
        }),
        base::BindOnce(&NewTabResources::Loaded, base::Unretained(this)));
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
  void Loaded(Bundle bundle) {
    bundle_ = std::move(bundle);
    loaded_ = true;
    loading_ = false;
    auto pending = std::move(pending_);
    for (auto& [path, done] : pending)
      Read(std::move(path), std::move(done));
  }
  bool loading_ = false;
  bool loaded_ = false;
  Bundle bundle_;
  std::vector<std::pair<std::string, content::WebUIDataSource::GotDataCallback>>
      pending_;
};

std::string Hex(SkColor color) {
  return base::StringPrintf("#%02x%02x%02x", SkColorGetR(color),
                            SkColorGetG(color), SkColorGetB(color));
}

// A current snapshot is embedded as inert JSON in the document. The first
// stylesheet includes the current theme/geometry, so painting never depends on
// a renderer -> browser -> renderer ready message. Dynamic bytes are never
// cached with the product assets, nor shared between regular/private pages.
base::DictValue PageData(bool private_page) {
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
    const auto config = window->commands().Read().value_or(DefaultKeyboardConfig());
    for (const auto [key, action] : {std::pair{"searchBinding", "toggle-address"},
                                    std::pair{"bookmarksBinding", "toggle-bookmarks"}}) {
      const auto bindings = config.shortcuts.find(action);
      if (bindings != config.shortcuts.end() && !bindings->second.empty())
        data.Set(key, bindings->second.front());
    }
    data.Set("sidebarOverhang",
             window->preferences().ReadSidebarAutoCollapse().value_or(false)
                 ? static_cast<int>((kSidebarWidth - kSidebarRailWidth) * scale)
                 : 0);
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
            const auto data = PageData(private_page);
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
                  ";--sidebar-overhang:" +
                  std::to_string(data.FindInt("sidebarOverhang").value_or(0)) +
                  "px;color-scheme:" +
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
    for (const char* pref : {kPreferencesPref, kKeyboardPref})
      registrar_.Add(pref, base::BindRepeating(&NewTabHandler::Refresh,
                                              base::Unretained(this)));
    web_ui()->RegisterMessageCallback("ready", base::BindRepeating(
        &NewTabHandler::OnReady, base::Unretained(this)));
    web_ui()->RegisterMessageCallback("openSearch", base::BindRepeating(
        &NewTabHandler::Open, base::Unretained(this), "toggle-address"));
    web_ui()->RegisterMessageCallback("openBookmarks", base::BindRepeating(
        &NewTabHandler::Open, base::Unretained(this), "toggle-bookmarks"));
  }
  void OnVisibilityChanged(content::Visibility) override { Refresh(); }
  void DidStartNavigation(content::NavigationHandle* navigation) override {
    if (navigation->IsInPrimaryMainFrame() &&
        !IsFlyNewTabPage(navigation->GetURL()))
      web_contents()->SetPageBaseBackgroundColor(std::nullopt);
  }

 private:
  bool OffTheRecord() { return Profile::FromWebUI(web_ui())->IsOffTheRecord(); }
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
    if (Visible())
      web_contents()->SetPageBaseBackgroundColor(GetFlyColors().bg_main);
    CallJavascriptFunction("updateFlyNewTab", base::Value(PageData(OffTheRecord())));
  }
  void Open(std::string action, const base::ListValue&) {
    // A background or detached WebUI must not operate another tab's window.
    auto* window = FlyWindowController::FromContents(web_contents());
    if (Visible() && window && window->ActiveContents() == web_contents())
      window->RunCommand(action);
  }
  PrefChangeRegistrar registrar_;
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
