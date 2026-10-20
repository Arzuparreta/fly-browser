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
#include "base/task/single_thread_task_runner.h"
#include "base/hash/sha1.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/task/thread_pool.h"
#include "base/timer/timer.h"
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
                                        /*announce=*/true));
          },
          ui, weak_factory_.GetWeakPtr()));
  Regenerate(false);
}
UserContentService::~UserContentService() = default;

// static
UserContentService::Generated UserContentService::Generate() {
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
  if (!base::ReadFileToStringWithMaxSize(source.AppendASCII("manifest.json"),
                                         &manifest_text, 64 * 1024) ||
      !base::ReadFileToStringWithMaxSize(source.AppendASCII("background.js"),
                                         &worker, 1024 * 1024))
    return result;
  auto manifest = base::JSONReader::ReadDict(manifest_text,
                                             base::JSON_PARSE_RFC);
  if (!manifest)
    return result;
  auto scripts = ReadFolder(user_data.AppendASCII("scripts"),
                            FILE_PATH_LITERAL(".user.js"));
  auto styles = ReadFolder(user_data.AppendASCII("styles"),
                           FILE_PATH_LITERAL(".user.css"));
  base::DictValue legacy;
  base::FileEnumerator stored(user_data.AppendASCII("script-storage"), false,
                              base::FileEnumerator::FILES,
                              FILE_PATH_LITERAL("*.json"));
  for (auto path = stored.Next(); !path.empty(); path = stored.Next()) {
    std::string text;
    if (!base::ReadFileToStringWithMaxSize(path, &text, kMaxFileBytes))
      continue;
    if (auto values = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC))
      legacy.Set(path.BaseName().RemoveExtension().AsUTF8Unsafe(),
                 std::move(*values));
  }
  result.scripts = scripts.size();
  result.styles = styles.size();
  base::DictValue content;
  content.Set("flyVersion", kFlyVersion);
  content.Set("scripts", std::move(scripts));
  content.Set("styles", std::move(styles));
  content.Set("legacyStorage", std::move(legacy));
  std::string json;
  base::JSONWriter::Write(content, &json);
  result.version = base::HexEncode(base::SHA1HashString(json + worker));
  const std::string data = "self.FLY_USER_CONTENT = " + json +
                           ";\nself.FLY_USER_CONTENT.version = \"" +
                           result.version + "\";\n";
  if (!base::CreateDirectory(target) ||
      !base::ImportantFileWriter::WriteFileAtomically(
          target.AppendASCII("background.js"), worker) ||
      !base::ImportantFileWriter::WriteFileAtomically(
          target.AppendASCII("manifest.json"), manifest_text) ||
      !base::ImportantFileWriter::WriteFileAtomically(
          target.AppendASCII("content.js"), data))
    return result;
  result.ok = true;
  result.directory = target;
  result.manifest = std::move(*manifest);
  return result;
}
void UserContentService::Regenerate(bool announce) {
  if (generating_) {
    pending_ = true;
    return;
  }
  generating_ = true;
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&UserContentService::Generate),
      base::BindOnce(&UserContentService::OnGenerated,
                     weak_factory_.GetWeakPtr(), announce));
}
void UserContentService::Reload() {
  Regenerate(true);
}
void UserContentService::OnGenerated(bool announce, Generated generated) {
  generating_ = false;
  if (pending_) {
    pending_ = false;
    Regenerate(announce);
    return;
  }
  if (!generated.ok)
    return;
  const bool changed = !current_ || current_->version != generated.version;
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
  if (announce && announce_)
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
