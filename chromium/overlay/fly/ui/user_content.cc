// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/user_content.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "base/files/file_enumerator.h"
#include "base/files/file_path_watcher.h"
#include "base/files/file_util.h"
#include "base/files/important_file_writer.h"
#include "base/hash/sha1.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/task/single_thread_task_runner.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/user_script_manager.h"
#include "fly/fly_version.h"

namespace fly {
namespace {
constexpr size_t kMaxFiles = 512;
constexpr int64_t kMaxFileBytes = 5 * 1024 * 1024;

base::FilePath UserData() {
  base::FilePath dir;
  base::PathService::Get(chrome::DIR_USER_DATA, &dir);
  return dir;
}
// Sorted {file, text} entries for one folder.
base::ListValue ReadFolder(const base::FilePath& dir,
                           const base::FilePath::StringType& suffix) {
  std::vector<base::FilePath> files;
  base::FileEnumerator entries(dir, false, base::FileEnumerator::FILES);
  for (auto path = entries.Next(); !path.empty() && files.size() < kMaxFiles;
       path = entries.Next()) {
    if (base::EndsWith(path.value(), suffix) && !base::IsLink(path))
      files.push_back(path);
  }
  std::ranges::sort(files);
  base::ListValue list;
  for (const auto& path : files) {
    std::string text;
    if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxFileBytes) ||
        !base::IsStringUTF8(text))
      continue;
    list.Append(base::DictValue()
                    .Set("file", path.BaseName().AsUTF8Unsafe())
                    .Set("text", std::move(text)));
  }
  return list;
}
}  // namespace

// Watches the scripts and styles folders on a blocking sequence and reports
// (debounced) changes back to the UI thread.
class UserContentService::Watcher {
 public:
  explicit Watcher(base::RepeatingClosure changed)
      : changed_(std::move(changed)) {
    for (const char* name : {"scripts", "styles"}) {
      const auto dir = UserData().AppendASCII(name);
      base::CreateDirectory(dir);
      auto watcher = std::make_unique<base::FilePathWatcher>();
      if (watcher->Watch(dir, base::FilePathWatcher::Type::kNonRecursive,
                         base::BindRepeating(&Watcher::OnChange,
                                             base::Unretained(this))))
        watchers_.push_back(std::move(watcher));
    }
  }

 private:
  void OnChange(const base::FilePath&, bool error) {
    // Editors write several times per save; coalesce.
    timer_.Start(FROM_HERE, base::Milliseconds(400), changed_);
  }
  base::RepeatingClosure changed_;
  std::vector<std::unique_ptr<base::FilePathWatcher>> watchers_;
  base::OneShotTimer timer_;
};

UserContentService::UserContentService(
    base::RepeatingCallback<void(std::u16string)> announce)
    : announce_(std::move(announce)) {
  auto ui = base::SingleThreadTaskRunner::GetCurrentDefault();
  watcher_ = base::SequenceBound<Watcher>(
      base::ThreadPool::CreateSequencedTaskRunner(
          {base::MayBlock(), base::TaskPriority::BEST_EFFORT}),
      base::BindRepeating(
          [](scoped_refptr<base::SingleThreadTaskRunner> ui,
             base::WeakPtr<UserContentService> self) {
            ui->PostTask(FROM_HERE,
                         base::BindOnce(&UserContentService::Regenerate, self,
                                        Announce::kIfChanged));
          },
          ui, weak_factory_.GetWeakPtr()));
  Regenerate(Announce::kNever);
}
UserContentService::~UserContentService() = default;

// static
UserContentService::Generated UserContentService::Generate(
    std::string email_handler) {
  TRACE_EVENT("browser", "Fly.UserContent.Generate");
  Generated result;
  const auto user_data = UserData();
  base::FilePath assets;
  if (user_data.empty() || !base::PathService::Get(base::DIR_ASSETS, &assets))
    return result;
  const auto source = assets.AppendASCII("fly_resources")
                          .AppendASCII("user_content");
  const auto target = user_data.AppendASCII("fly-user-content");
  std::string manifest_text;
  std::string worker;
  std::string offscreen_page;
  std::string offscreen_script;
  if (!base::ReadFileToStringWithMaxSize(source.AppendASCII("manifest.json"),
                                         &manifest_text, 64 * 1024) ||
      !base::ReadFileToStringWithMaxSize(source.AppendASCII("background.js"),
                                         &worker, 1024 * 1024) ||
      !base::ReadFileToStringWithMaxSize(source.AppendASCII("offscreen.html"),
                                         &offscreen_page, 64 * 1024) ||
      !base::ReadFileToStringWithMaxSize(source.AppendASCII("offscreen.js"),
                                         &offscreen_script, 64 * 1024))
    return result;
  auto manifest = base::JSONReader::ReadDict(manifest_text,
                                             base::JSON_PARSE_RFC);
  if (!manifest)
    return result;
  auto scripts = ReadFolder(user_data.AppendASCII("scripts"),
                            FILE_PATH_LITERAL(".user.js"));
  auto styles = ReadFolder(user_data.AppendASCII("styles"),
                           FILE_PATH_LITERAL(".user.css"));
  result.scripts = scripts.size();
  result.styles = styles.size();
  base::DictValue content;
  content.Set("flyVersion", kFlyVersion);
  content.Set("emailHandler", std::move(email_handler));
  content.Set("scripts", std::move(scripts));
  content.Set("styles", std::move(styles));
  std::string json;
  base::JSONWriter::Write(content, &json);
  const auto digest = base::SHA1HashString(json + worker + manifest_text +
                                           offscreen_page + offscreen_script);
  result.version = base::HexEncode(digest);
  // A new extension version makes Chromium replace the registered service
  // worker; with an unchanged version it would keep running the old files.
  manifest->Set("version",
                base::StringPrintf("1.%d.%d.%d",
                                   (uint8_t(digest[0]) << 8 | uint8_t(digest[1])) % 65535,
                                   (uint8_t(digest[2]) << 8 | uint8_t(digest[3])) % 65535,
                                   (uint8_t(digest[4]) << 8 | uint8_t(digest[5])) % 65535));
  std::string manifest_out;
  base::JSONWriter::WriteWithOptions(*manifest,
                                     base::JSONWriter::OPTIONS_PRETTY_PRINT,
                                     &manifest_out);
  manifest_text = manifest_out;
  const std::string data = "self.FLY_USER_CONTENT = " + json +
                           ";\nself.FLY_USER_CONTENT.version = \"" +
                           result.version + "\";\n";
  if (!base::CreateDirectory(target))
    return result;
  // Validate every byte, including missing/truncated/corrupt files. A digest
  // marker alone would accept a damaged generation. Keep document-start
  // registration on the existing path; only avoid redundant atomic writes.
  const std::pair<const char*, const std::string*> files[] = {
      {"background.js", &worker},
      {"manifest.json", &manifest_text},
      {"offscreen.html", &offscreen_page},
      {"offscreen.js", &offscreen_script},
      {"content.js", &data}};
  for (const auto& [name, expected] : files) {
    const auto file = target.AppendASCII(name);
    std::string existing;
    if (!base::IsLink(file) &&
        base::ReadFileToStringWithMaxSize(file, &existing, expected->size()) &&
        existing == *expected)
      continue;
    if (!base::ImportantFileWriter::WriteFileAtomically(file, *expected))
      return result;
    ++result.files_written;
  }
  TRACE_EVENT_INSTANT("browser", "Fly.UserContent.FilesWritten", "count",
                      result.files_written);
  result.ok = true;
  result.directory = target;
  result.manifest = std::move(*manifest);
  return result;
}
void UserContentService::Regenerate(Announce announce) {
  if (generating_) {
    pending_ = true;
    pending_announce_ = std::max(pending_announce_, announce);
    return;
  }
  generating_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&UserContentService::Generate, email_handler_),
      base::BindOnce(&UserContentService::OnGenerated,
                     weak_factory_.GetWeakPtr(), announce));
}
void UserContentService::SetEmailHandler(std::string handler) {
  if (handler == email_handler_)
    return;
  email_handler_ = std::move(handler);
  Regenerate(Announce::kNever);
}
void UserContentService::Reload() {
  Regenerate(Announce::kAlways);
}
void UserContentService::OnGenerated(Announce announce, Generated generated) {
  generating_ = false;
  if (pending_) {
    pending_ = false;
    const auto next = std::max(announce, pending_announce_);
    pending_announce_ = Announce::kNever;
    Regenerate(next);
    return;
  }
  if (!generated.ok)
    return;
  const bool first = !current_;
  const bool changed = first || current_->version != generated.version ||
                       generated.files_written != 0;
  scripts_ = generated.scripts;
  styles_ = generated.styles;
  current_ = std::move(generated);
  if (changed) {
    for (Profile* profile : profiles_) {
      auto* loader = extensions::ComponentLoader::Get(profile);
      if (loader && loader->Exists(kExtensionId))
        loader->Reload(kExtensionId);
      else
        Load(profile);
    }
  }
  // Folder watchers also fire when nothing changed (for example at start).
  const bool show = announce == Announce::kAlways ||
                    (announce == Announce::kIfChanged && changed && !first);
  if (show && announce_)
    announce_.Run(u"User content reloaded: " +
                  base::NumberToString16(scripts_) + u" scripts, " +
                  base::NumberToString16(styles_) + u" styles.");
}
void UserContentService::AddProfile(Profile* profile) {
  if (!profile || profiles_.contains(profile))
    return;
  profiles_.insert(profile);
  profile_obs_.AddObservation(profile);
  Load(profile);
}
void UserContentService::Load(Profile* profile) {
  if (!current_ || !extensions::ExtensionSystem::Get(profile))
    return;
  auto* loader = extensions::ComponentLoader::Get(profile);
  if (!loader || loader->Exists(kExtensionId))
    return;
  loader->Add(current_->manifest.Clone(), current_->directory);
  // The userScripts API also needs the per-extension "Allow user scripts"
  // choice; Fly's own component has it on.
  if (auto* manager =
          extensions::ExtensionSystem::Get(profile)->user_script_manager())
    manager->SetUserScriptPrefEnabled(kExtensionId, true);
}
void UserContentService::OnProfileWillBeDestroyed(Profile* profile) {
  profile_obs_.RemoveObservation(profile);
  profiles_.erase(profile);
}
}  // namespace fly
