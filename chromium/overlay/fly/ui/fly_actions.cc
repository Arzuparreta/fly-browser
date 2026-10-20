// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_actions.h"

#include <algorithm>
#include <utility>

#include "base/files/file_util.h"
#include "base/no_destructor.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/system/sys_info.h"
#include "base/task/thread_pool.h"
#include "base/values.h"
#include "base/version.h"
#include "base/version_info/version_info.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/content_settings/host_content_settings_map_factory.h"
#include "fly/ui/fly_theme.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "chrome/browser/platform_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/common/chrome_isolated_world_ids.h"
#include "chrome/common/chrome_paths.h"
#include "components/content_settings/core/browser/host_content_settings_map.h"
#include "components/content_settings/core/common/content_settings.h"
#include "components/content_settings/core/common/content_settings_types.h"
#include "components/download/public/common/download_item.h"
#include "components/prefs/pref_service.h"
#include "components/spellcheck/browser/pref_names.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "fly/fly_version.h"
#include "fly/preferences/preferences_service.h"
#include "net/base/filename_util.h"
#include "net/base/net_errors.h"
#include "net/base/url_util.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/resource_request.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"
#include "services/network/public/cpp/simple_url_loader.h"
#include "ui/base/mojom/menu_source_type.mojom.h"
#include "ui/views/controls/menu/menu_runner.h"
#include "ui/views/widget/widget.h"

namespace fly {
namespace {
enum Command : int {
  kDisableGpu = 1,
  kBackgroundThrottling,
  kAutoCollapse,
  kWindowControls,
  kScriptsDir,
  kStylesDir,
  kScriptStorageDir,
  kReloadUserContent,
  kPreferences,
  kRestart,
  kDownloads,
  kTaskManager,
  kWhatsNew,
  kUpdates,
  kDiagnostics,
  kProcessBase = 100,
  kSuspensionBase = 200,
};
constexpr int kSuspensionChoices[] = {5, 15, 30, 60, 0};
constexpr char kReleasesPage[] =
    "https://github.com/Arzuparreta/fly-browser/releases";
constexpr char kLatestRelease[] =
    "https://api.github.com/repos/Arzuparreta/fly-browser/releases/latest";
constexpr char kDownloadPrefix[] = "download:";

std::string DownloadAction(Profile* profile, download::DownloadItem* item) {
  return kDownloadPrefix + profile->GetPath().BaseName().AsUTF8Unsafe() + ":" +
         item->GetGuid();
}
download::DownloadItem* FindDownload(const std::vector<Profile*>& profiles,
                                     std::string_view action,
                                     Profile** owner = nullptr) {
  if (!action.starts_with(kDownloadPrefix))
    return nullptr;
  const auto rest = action.substr(std::string_view(kDownloadPrefix).size());
  const auto split = rest.rfind(':');
  if (split == std::string_view::npos)
    return nullptr;
  const auto directory = rest.substr(0, split);
  const auto guid = rest.substr(split + 1);
  for (auto* profile : profiles) {
    if (profile->GetPath().BaseName().AsUTF8Unsafe() != directory)
      continue;
    if (auto* manager = profile->GetDownloadManager()) {
      if (auto* item = manager->GetDownloadByGuid(std::string(guid))) {
        if (owner)
          *owner = profile;
        return item;
      }
    }
  }
  return nullptr;
}
std::string Redact(std::string text) {
  base::FilePath home;
  if (base::PathService::Get(base::DIR_HOME, &home) &&
      home.value().size() > 1) {
    base::ReplaceSubstringsAfterOffset(&text, 0, home.value(), "~");
  }
  return text;
}
}  // namespace

SettingsMenu::SettingsMenu(PreferencesService& preferences,
                           base::RepeatingCallback<void(std::string)> run_action)
    : preferences_(preferences), run_action_(std::move(run_action)) {}
SettingsMenu::~SettingsMenu() = default;
void SettingsMenu::Build() {
  model_ = std::make_unique<ui::SimpleMenuModel>(this);
  process_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  suspension_menu_ = std::make_unique<ui::SimpleMenuModel>(this);
  model_->AddCheckItem(kDisableGpu,
                       u"Disable GPU Hardware Acceleration (after restart)");
  model_->AddCheckItem(kBackgroundThrottling,
                       u"Enable Background Throttling (after restart)");
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  for (int limit = 1; limit <= 5; ++limit)
    process_menu_->AddRadioItem(
        kProcessBase + limit,
        u"Limit to " + base::NumberToString16(limit) +
            (limit == 1 ? u" process" : u" processes"),
        1);
  model_->AddSubMenu(0, u"Renderer Process Limit (after restart)",
                     process_menu_.get());
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  for (int minutes : kSuspensionChoices)
    suspension_menu_->AddRadioItem(
        kSuspensionBase + minutes,
        minutes == 0    ? u"Never (Disabled)"
        : minutes == 60 ? u"1 hour"
        : minutes == 15 ? u"15 minutes (Default)"
                        : base::NumberToString16(minutes) + u" minutes",
        2);
  model_->AddSubMenu(0, u"Tab Suspension", suspension_menu_.get());
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddCheckItem(kAutoCollapse, u"Auto-Collapse Sidebar (36px Strip)");
  model_->AddCheckItem(kWindowControls, u"Window Controls (Sidebar)");
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddItem(kScriptsDir, u"Open User Scripts Directory");
  model_->AddItem(kStylesDir, u"Open User Styles Directory");
  model_->AddItem(kScriptStorageDir, u"Open User Script Storage Directory");
  model_->AddItem(kReloadUserContent, u"Reload User Scripts and Styles");
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddItem(kDownloads, u"Downloads");
  model_->AddItem(kTaskManager, u"Task Manager");
  model_->AddItem(kDiagnostics, u"Diagnostics");
  model_->AddItem(kWhatsNew, u"What's New");
  model_->AddItem(kUpdates, u"Check for Updates");
  model_->AddSeparator(ui::NORMAL_SEPARATOR);
  model_->AddItem(kPreferences, u"Open Full Preferences Overlay");
  model_->AddItem(kRestart, u"Restart Browser Now");
}
void SettingsMenu::Show(views::Widget* parent, const gfx::Point& point) {
  Cancel();
  Build();
  runner_ = std::make_unique<views::MenuRunner>(
      model_.get(), views::MenuRunner::CONTEXT_MENU);
  runner_->RunMenuAt(parent, nullptr, gfx::Rect(point, gfx::Size()),
                     views::MenuAnchorPosition::kTopLeft,
                     ui::mojom::MenuSourceType::kKeyboard);
}
void SettingsMenu::Cancel() {
  if (runner_ && runner_->IsRunning())
    runner_->Cancel();
}
ui::SimpleMenuModel* SettingsMenu::model_for_testing() {
  return runner_ && runner_->IsRunning() ? model_.get() : nullptr;
}
bool SettingsMenu::IsCommandIdChecked(int command) const {
  if (command > kSuspensionBase - 1 && command <= kSuspensionBase + 60)
    return preferences_->ReadSuspendTimeoutMinutes() ==
           command - kSuspensionBase;
  if (command > kProcessBase && command < kSuspensionBase)
    return preferences_->ReadInt("process_limit") == command - kProcessBase;
  switch (command) {
    case kDisableGpu:
      return preferences_->ReadBool("disable_gpu").value_or(false);
    case kBackgroundThrottling:
      return preferences_->ReadBool("background_throttling").value_or(true);
    case kAutoCollapse:
      return preferences_->ReadSidebarAutoCollapse().value_or(true);
    case kWindowControls:
      return preferences_->ReadWindowControlsVisible().value_or(false);
  }
  return false;
}
bool SettingsMenu::IsCommandIdEnabled(int command) const {
  switch (command) {
    case kDisableGpu:
    case kBackgroundThrottling:
      return preferences_->CanEdit();
    case kAutoCollapse:
      return preferences_->CanEditSidebarAutoCollapse();
    case kWindowControls:
      return preferences_->CanEditWindowControls();
  }
  if (command > kProcessBase && command < kSuspensionBase)
    return preferences_->CanEdit();
  if (command >= kSuspensionBase)
    return preferences_->CanEditSuspendTimeout();
  return true;
}
void SettingsMenu::ExecuteCommand(int command, int) {
  if (command >= kSuspensionBase) {
    preferences_->SetSuspendTimeoutMinutes(command - kSuspensionBase);
    return;
  }
  if (command > kProcessBase) {
    preferences_->SetInt("process_limit", command - kProcessBase);
    return;
  }
  // Actions may open other menus or close the window: run them after the
  // menu's nested loop has unwound.
  std::string action;
  switch (command) {
    case kDisableGpu:
      preferences_->SetBool("disable_gpu", !IsCommandIdChecked(command));
      return;
    case kBackgroundThrottling:
      preferences_->SetBool("background_throttling",
                            !IsCommandIdChecked(command));
      return;
    case kAutoCollapse:
      preferences_->SetSidebarAutoCollapse(!IsCommandIdChecked(command));
      return;
    case kWindowControls:
      preferences_->SetWindowControlsVisible(!IsCommandIdChecked(command));
      return;
    case kScriptsDir:
      action = "open-scripts-dir";
      break;
    case kStylesDir:
      action = "open-styles-dir";
      break;
    case kScriptStorageDir:
      action = "open-script-storage-dir";
      break;
    case kReloadUserContent:
      action = "reload-user-content";
      break;
    case kDownloads:
      action = "toggle-downloads";
      break;
    case kTaskManager:
      action = "open-task-manager";
      break;
    case kDiagnostics:
      action = "open-diagnostics";
      break;
    case kWhatsNew:
      action = "show-whats-new";
      break;
    case kUpdates:
      action = "check-for-updates";
      break;
    case kPreferences:
      action = "toggle-help";
      break;
    case kRestart:
      chrome::AttemptRestart();
      return;
  }
  if (!action.empty())
    run_action_.Run(std::move(action));
}

namespace {
// The reader runs in Chrome's internal isolated world: it shares the page's
// DOM and storage (its per-site preferences) but not the page's scripts.
constexpr int32_t kReaderWorld = ISOLATED_WORLD_ID_CHROME_INTERNAL;
void RunReaderScript(base::WeakPtr<content::WebContents> contents,
                     std::u16string script,
                     base::OnceCallback<void(base::Value)> done) {
  if (!contents || script.empty()) {
    std::move(done).Run(base::Value());
    return;
  }
  contents->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      script,
      base::BindOnce([](base::OnceCallback<void(base::Value)> done,
                        base::Value result) {
        std::move(done).Run(std::move(result));
      }, std::move(done)),
      kReaderWorld);
}
void WithReaderScript(base::OnceCallback<void(std::u16string)> done) {
  static base::NoDestructor<std::u16string> cached;
  if (!cached->empty()) {
    std::move(done).Run(*cached);
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_BLOCKING},
      base::BindOnce([]() {
        base::FilePath assets;
        std::string text;
        if (base::PathService::Get(base::DIR_ASSETS, &assets))
          base::ReadFileToStringWithMaxSize(
              assets.AppendASCII("fly_resources")
                  .AppendASCII("reader")
                  .AppendASCII("readerDistiller.js"),
              &text, 4 * 1024 * 1024);
        return base::UTF8ToUTF16(text);
      }),
      base::BindOnce(
          [](base::OnceCallback<void(std::u16string)> done,
             std::u16string script) {
            if (!script.empty())
              *cached = script;
            std::move(done).Run(std::move(script));
          },
          std::move(done)));
}
}  // namespace

void ToggleReaderView(content::WebContents* contents,
                      base::OnceCallback<void(ReaderResult)> done) {
  if (!contents || (!contents->GetLastCommittedURL().SchemeIsHTTPOrHTTPS() &&
                    !contents->GetLastCommittedURL().SchemeIsFile())) {
    std::move(done).Run({false, u"", u"This page has no article to read."});
    return;
  }
  WithReaderScript(base::BindOnce(
      [](base::WeakPtr<content::WebContents> contents,
         base::OnceCallback<void(ReaderResult)> done, std::u16string script) {
        if (script.empty()) {
          std::move(done).Run({false, u"", u"The reader is not installed."});
          return;
        }
        RunReaderScript(
            contents, std::move(script),
            base::BindOnce(
                [](base::OnceCallback<void(ReaderResult)> done,
                   base::Value value) {
                  ReaderResult result;
                  if (const auto* dict = value.GetIfDict()) {
                    result.active = dict->FindBool("active").value_or(false);
                    if (const auto* title = dict->FindString("title"))
                      result.title = base::UTF8ToUTF16(*title);
                    if (const auto* error = dict->FindString("error"))
                      result.error = base::UTF8ToUTF16(*error);
                  } else {
                    result.error = u"Distillation failed";
                  }
                  std::move(done).Run(std::move(result));
                },
                std::move(done)));
      },
      contents->GetWeakPtr(), std::move(done)));
}

void QueryReaderView(content::WebContents* contents,
                     base::OnceCallback<void(bool)> done) {
  if (!contents) {
    std::move(done).Run(false);
    return;
  }
  RunReaderScript(
      contents->GetWeakPtr(),
      u"!!document.getElementById('fly-reader-overlay')",
      base::BindOnce(
          [](base::OnceCallback<void(bool)> done, base::Value value) {
            std::move(done).Run(value.is_bool() && value.GetBool());
          },
          std::move(done)));
}

base::FilePath UserContentDirectory(std::string_view name) {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir))
    return base::FilePath();
  return dir.AppendASCII(name);
}
void OpenUserContentDirectory(Profile* profile, std::string_view name) {
  const auto dir = UserContentDirectory(name);
  if (dir.empty() || !profile)
    return;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](base::FilePath dir) { return base::CreateDirectory(dir); }, dir),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile, base::FilePath dir, bool ok) {
            if (ok && profile)
              platform_util::OpenItem(profile.get(), dir,
                                      platform_util::OPEN_FOLDER, {});
          },
          profile->GetWeakPtr(), dir));
}

void ApplyProfilePreferences(Profile* profile,
                             const PreferencesService& preferences) {
  if (!profile)
    return;
  // Only settings chosen in Fly are written: otherwise Chromium's own
  // per-profile choices (chrome://settings) stay as they are.
  if (const auto notifications = preferences.ReadBool("web_notifications");
      notifications && preferences.HasStoredValue("web_notifications")) {
    if (auto* map = HostContentSettingsMapFactory::GetForProfile(profile)) {
      map->SetDefaultContentSetting(
          ContentSettingsType::NOTIFICATIONS,
          *notifications ? CONTENT_SETTING_ASK : CONTENT_SETTING_BLOCK);
    }
  }
  if (profile->IsOffTheRecord())
    return;
  if (const auto language = preferences.ReadString("spellchecker_language");
      language && preferences.HasStoredValue("spellchecker_language")) {
    auto* prefs = profile->GetPrefs();
    const bool enabled = !language->empty() && *language != "disabled" &&
                         *language != "none";
    prefs->SetBoolean(spellcheck::prefs::kSpellCheckEnable, enabled);
    if (enabled) {
      base::ListValue dictionaries;
      dictionaries.Append(*language);
      prefs->SetList(spellcheck::prefs::kSpellCheckDictionaries,
                     std::move(dictionaries));
    }
  }
}

void CheckForUpdates(base::OnceCallback<void(UpdateCheckResult)> done) {
  auto request = std::make_unique<network::ResourceRequest>();
  request->url = GURL(kLatestRelease);
  request->credentials_mode = network::mojom::CredentialsMode::kOmit;
  request->headers.SetHeader("Accept", "application/vnd.github+json");
  const net::NetworkTrafficAnnotationTag annotation =
      net::DefineNetworkTrafficAnnotation("fly_update_check", R"(
        semantics {
          sender: "Fly update check"
          description: "Reads the latest published Fly release version."
          trigger: "Startup and the Check for Updates action."
          data: "None."
          destination: OTHER
        }
        policy {
          cookies_allowed: NO
          setting: "Not configurable."
          policy_exception_justification: "Product update notice."
        })");
  auto loader =
      network::SimpleURLLoader::Create(std::move(request), annotation);
  auto* raw = loader.get();
  raw->DownloadToString(
      g_browser_process->shared_url_loader_factory().get(),
      base::BindOnce(
          [](std::unique_ptr<network::SimpleURLLoader>,
             base::OnceCallback<void(UpdateCheckResult)> done,
             std::optional<std::string> body) {
            UpdateCheckResult result;
            result.page = GURL(kReleasesPage);
            const auto json =
                body ? base::JSONReader::ReadDict(*body, base::JSON_PARSE_RFC)
                     : std::nullopt;
            const auto* tag = json ? json->FindString("tag_name") : nullptr;
            if (tag) {
              std::string latest = *tag;
              if (latest.starts_with("v"))
                latest = latest.substr(1);
              const base::Version remote(latest);
              const base::Version local(kFlyVersion);
              if (remote.IsValid() && local.IsValid()) {
                result.ok = true;
                result.latest = latest;
                result.newer = remote > local;
                if (const auto* page = json->FindString("html_url");
                    page && GURL(*page).SchemeIs(url::kHttpsScheme))
                  result.page = GURL(*page);
              }
            }
            std::move(done).Run(std::move(result));
          },
          std::move(loader), std::move(done)),
      256 * 1024);
}

GURL WhatsNewURL(std::string_view from) {
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets))
    return GURL(kReleasesPage);
  GURL url = net::FilePathToFileURL(
      assets.AppendASCII("fly_resources").AppendASCII("whats-new.html"));
  if (!from.empty() && base::Version(from).IsValid())
    url = net::AppendQueryParameter(url, "from", std::string(from));
  return url;
}

void ResolveWhatsNewURL(std::string from, base::OnceCallback<void(GURL)> done) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(
          [](std::string from) {
            const GURL url = WhatsNewURL(from);
            base::FilePath path;
            if (url.SchemeIsFile() && net::FileURLToFilePath(url, &path) &&
                base::PathExists(path))
              return url;
            return GURL(kReleasesPage);
          },
          std::move(from)),
      std::move(done));
}

std::vector<PaletteEntry> DownloadEntries(
    const std::vector<Profile*>& profiles) {
  std::vector<std::pair<base::Time, PaletteEntry>> rows;
  for (auto* profile : profiles) {
    auto* manager = profile->GetDownloadManager();
    if (!manager)
      continue;
    content::DownloadManager::DownloadVector items;
    manager->GetAllDownloads(&items);
    for (download::DownloadItem* item : items) {
      if (item->IsTemporary() || item->IsTransient())
        continue;
      std::string state;
      switch (item->GetState()) {
        case download::DownloadItem::IN_PROGRESS:
          state = item->IsPaused() ? "Paused"
                  : item->PercentComplete() >= 0
                      ? base::NumberToString(item->PercentComplete()) + "%"
                      : "Downloading";
          break;
        case download::DownloadItem::COMPLETE:
          state = item->GetFileExternallyRemoved() ? "Removed from disk"
                                                   : "Complete";
          break;
        case download::DownloadItem::CANCELLED:
          state = "Cancelled";
          break;
        case download::DownloadItem::INTERRUPTED:
          state = "Failed";
          break;
        default:
          state = "Unknown";
      }
      PaletteEntry entry;
      entry.action = DownloadAction(profile, item);
      entry.id = entry.action;
      entry.title = item->GetFileNameToReportUser().AsUTF8Unsafe();
      entry.category = state + " · " + std::string(item->GetURL().host());
      rows.emplace_back(item->GetStartTime(), std::move(entry));
    }
  }
  std::ranges::sort(rows, std::greater<>(), &decltype(rows)::value_type::first);
  std::vector<PaletteEntry> entries;
  PaletteEntry page;
  page.action = page.id = "downloads-page";
  page.title = "Open full downloads page";
  page.category = "chrome://downloads";
  entries.push_back(std::move(page));
  for (auto& [time, entry] : rows)
    entries.push_back(std::move(entry));
  return entries;
}
bool OpenDownloadEntry(const std::vector<Profile*>& profiles,
                       BrowserWindowInterface* browser,
                       std::string_view action) {
  if (action == "downloads-page") {
    if (!browser)
      return false;
    NavigateParams params(browser, GURL("chrome://downloads/"),
                          ui::PAGE_TRANSITION_AUTO_TOPLEVEL);
    params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
    Navigate(&params);
    return true;
  }
  Profile* owner = nullptr;
  auto* item = FindDownload(profiles, action, &owner);
  if (!item)
    return false;
  if (item->GetState() == download::DownloadItem::COMPLETE &&
      !item->GetFileExternallyRemoved()) {
    item->OpenDownload();
  } else if (item->GetState() == download::DownloadItem::IN_PROGRESS) {
    if (item->IsPaused())
      item->Resume(true);
    else
      item->Pause();
  } else if (item->CanResume()) {
    item->Resume(true);
  } else if (owner && !item->GetTargetFilePath().empty()) {
    platform_util::ShowItemInFolder(owner, item->GetTargetFilePath());
  }
  return true;
}
bool RemoveDownloadEntry(const std::vector<Profile*>& profiles,
                         std::string_view action) {
  auto* item = FindDownload(profiles, action);
  if (!item)
    return false;
  if (item->GetState() == download::DownloadItem::IN_PROGRESS)
    item->Cancel(true);
  else
    item->Remove();
  return true;
}

std::u16string DiagnosticsReport(size_t workspace_count,
                                 const std::vector<std::u16string>& events) {
  std::string report = "Fly " + std::string(kFlyVersion) + "\n";
  report += "Engine: Chromium " +
            std::string(version_info::GetVersionNumber()) + "\n";
  report += "Platform: " + base::SysInfo::OperatingSystemName() + " " +
            base::SysInfo::OperatingSystemVersion() + " (" +
            base::SysInfo::OperatingSystemArchitecture() + ")\n";
  report += "Workspaces: " + base::NumberToString(workspace_count) + "\n";
  report += "Processors: " +
            base::NumberToString(base::SysInfo::NumberOfProcessors()) + "\n";
  report += "Memory: " +
            base::NumberToString(
                base::SysInfo::AmountOfTotalPhysicalMemory().InMiB()) +
            " MB\n";
  if (events.empty()) {
    report +=
        "\nEvent recording is off or empty. Enable it with \"Toggle "
        "Diagnostics Recording\"; events stay in memory only.\n";
  } else {
    report += "\nRecent events:\n";
    for (const auto& event : events)
      report += "  " + base::UTF16ToUTF8(event) + "\n";
  }
  return base::UTF8ToUTF16(Redact(std::move(report)));
}
}  // namespace fly
