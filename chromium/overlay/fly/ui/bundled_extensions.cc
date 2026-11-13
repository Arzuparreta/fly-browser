// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/bundled_extensions.h"

#include <algorithm>
#include <optional>
#include <utility>

#include "base/command_line.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/json/json_reader.h"
#include "base/no_destructor.h"
#include "base/path_service.h"
#include "base/task/thread_pool.h"
#include "base/trace_event/trace_event.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/chrome_browser_main_extra_parts.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_paths.h"
#include "extensions/browser/api/declarative_net_request/file_backed_ruleset_source.h"
#include "extensions/browser/extension_system.h"
#include "extensions/common/extension_l10n_util.h"
#include "fly/preferences/preferences_service.h"

namespace fly {
namespace {
// Directory below the user data directory that holds the indexed
// declarativeNetRequest rulesets of the bundled extensions.
constexpr char kRulesetDirectory[] = "Bundled Extension Rules";

bool NativeFly() {
  return base::CommandLine::ForCurrentProcess()->HasSwitch("fly-native");
}

std::optional<std::vector<BundledExtension>>& Catalog() {
  static base::NoDestructor<std::optional<std::vector<BundledExtension>>>
      catalog;
  return *catalog;
}

// Reads <assets>/fly_extensions/<id>/manifest.json for every bundled copy.
std::vector<BundledExtension> ReadFromDisk() {
  std::vector<BundledExtension> result;
  base::FilePath assets;
  if (!base::PathService::Get(base::DIR_ASSETS, &assets))
    return result;
  base::FileEnumerator dirs(assets.AppendASCII("fly_extensions"), false,
                            base::FileEnumerator::DIRECTORIES);
  for (auto path = dirs.Next(); !path.empty(); path = dirs.Next()) {
    std::string text;
    if (!base::ReadFileToStringWithMaxSize(path.AppendASCII("manifest.json"),
                                           &text, 1024 * 1024))
      continue;
    auto manifest = base::JSONReader::ReadDict(text, base::JSON_PARSE_RFC);
    if (!manifest || !manifest->FindString("key"))
      continue;
    // Component loading skips Chromium's localisation of __MSG_ names.
    std::string error;
    extension_l10n_util::LocalizeExtension(
        path, &*manifest,
        extension_l10n_util::GzippedMessagesPermission::kDisallow, &error);
    result.emplace_back(path.BaseName().AsUTF8Unsafe(), path,
                        std::move(*manifest));
  }
  std::ranges::sort(result, {}, &BundledExtension::id);
  return result;
}

void Store(std::vector<BundledExtension> read) {
  if (Catalog())
    return;
  base::FilePath user_data;
  if (base::PathService::Get(chrome::DIR_USER_DATA, &user_data)) {
    // Chromium writes indexed rulesets next to the extension by default,
    // which fails in a read-only installation and leaves every static
    // ruleset off.
    for (const auto& bundle : read)
      extensions::declarative_net_request::FileBackedRulesetSource::
          SetIndexedRulesetDirectory(bundle.id,
                                     user_data.AppendASCII(kRulesetDirectory)
                                         .AppendASCII(bundle.id));
  }
  Catalog() = std::move(read);
}

class BundledExtensionsMainParts : public ChromeBrowserMainExtraParts {
 public:
  void PreProfileInit() override {
    if (!NativeFly())
      return;
    // A manifest and its localisation, a few kilobytes: read here, before
    // the message loop forbids blocking, so that a profile never waits for
    // a worker to get its blocker.
    TRACE_EVENT("browser", "Fly.ReadBundledExtensions");
    Store(ReadFromDisk());
  }
  void PostProfileInit(Profile* profile, bool is_initial_profile) override {
    if (NativeFly())
      LoadWantedBundledExtensions(profile);
  }
};
}  // namespace

BundledExtension::BundledExtension() = default;
BundledExtension::BundledExtension(std::string id,
                                   base::FilePath path,
                                   base::DictValue manifest)
    : id(std::move(id)), path(std::move(path)), manifest(std::move(manifest)) {}
BundledExtension::BundledExtension(BundledExtension&&) = default;
BundledExtension& BundledExtension::operator=(BundledExtension&&) = default;
BundledExtension::~BundledExtension() = default;
BundledExtension BundledExtension::Clone() const {
  return BundledExtension(id, path, manifest.Clone());
}

const std::vector<BundledExtension>* BundledExtensionCatalog() {
  return Catalog() ? &*Catalog() : nullptr;
}

void ReadBundledExtensionCatalogAsync(base::OnceClosure done) {
  if (Catalog()) {
    std::move(done).Run();
    return;
  }
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadFromDisk),
      base::BindOnce(
          [](base::OnceClosure done, std::vector<BundledExtension> read) {
            Store(std::move(read));
            std::move(done).Run();
          },
          std::move(done)));
}

std::string BundledExtensionsWorkspaceKey(Profile* profile) {
  return profile->GetPath().BaseName().AsUTF8Unsafe();
}

bool BundledExtensionWanted(const PreferencesService& preferences,
                            Profile* profile,
                            const std::string& id) {
  if (!preferences.ReadBool("extensions_enabled").value_or(true))
    return false;
  const auto state =
      preferences.ReadNativeExtensions().value_or(base::DictValue());
  if (const auto* removed = state.FindList("removed");
      removed && std::ranges::find(*removed, base::Value(id)) != removed->end())
    return false;
  const auto* off = state.FindDict("workspace_off");
  const auto* list =
      off ? off->FindList(BundledExtensionsWorkspaceKey(profile)) : nullptr;
  return !list || std::ranges::find(*list, base::Value(id)) == list->end();
}

void LoadWantedBundledExtensions(Profile* profile) {
  const auto* catalog = BundledExtensionCatalog();
  if (!catalog || !profile || profile->IsOffTheRecord() ||
      !extensions::ExtensionSystem::Get(profile))
    return;
  auto* loader = extensions::ComponentLoader::Get(profile);
  if (!loader)
    return;
  const PreferencesService preferences(*g_browser_process->local_state());
  for (const auto& bundle : *catalog) {
    if (!loader->Exists(bundle.id) &&
        BundledExtensionWanted(preferences, profile, bundle.id))
      loader->Add(bundle.manifest.Clone(), bundle.path);
  }
}

std::unique_ptr<ChromeBrowserMainExtraParts>
CreateBundledExtensionsMainParts() {
  return std::make_unique<BundledExtensionsMainParts>();
}

}  // namespace fly
