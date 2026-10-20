// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#include "fly/ui/fly_extensions.h"

#include <algorithm>
#include <utility>

#include "base/process/process_metrics.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "chrome/browser/extensions/extension_install_prompt.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_process_host.h"
#include "extensions/browser/crx_installer.h"
#include "extensions/browser/extension_file_task_runner.h"
#include "extensions/browser/process_manager.h"
#include "extensions/browser/unpacked_installer.h"
#include "extensions/browser/zipfile_installer.h"
#include <set>
#include "crypto/sha2.h"
#include "base/functional/callback_helpers.h"
#include "base/base64.h"
#include "components/crx_file/id_util.h"
#include "base/files/file_enumerator.h"
#include "base/files/file_util.h"
#include "base/json/json_reader.h"
#include "base/path_service.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/extensions/component_loader.h"
#include "chrome/browser/extensions/extension_action_runner.h"
#include "chrome/browser/extensions/extension_tab_util.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/extensions/extension_popup_types.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/extensions/extension_popup.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/extensions/extension_view_host_factory.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/extension_action.h"
#include "extensions/browser/extension_action_icon_factory.h"
#include "extensions/browser/extension_action_manager.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registrar.h"
#include "base/task/single_thread_task_runner.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/management_policy.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_l10n_util.h"
#include "extensions/common/manifest_handlers/options_page_info.h"
#include "fly/preferences/preferences_service.h"
#include "ui/views/bubble/bubble_anchor.h"
#include "ui/views/bubble/bubble_border.h"

namespace fly {
namespace {
// Reads <assets>/fly_extensions/<id>/manifest.json for every bundled copy.
std::vector<std::pair<std::string, std::pair<base::FilePath, base::DictValue>>>
ReadBundles() {
  std::vector<std::pair<std::string, std::pair<base::FilePath, base::DictValue>>>
      result;
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
    result.emplace_back(path.BaseName().AsUTF8Unsafe(),
                        std::make_pair(path, std::move(*manifest)));
  }
  return result;
}
base::ListValue* EnsureList(base::DictValue& state,
                            std::string_view section,
                            const std::string& directory) {
  auto* dict = state.EnsureDict(section);
  if (!dict->FindList(directory))
    dict->Set(directory, base::ListValue());
  return dict->FindList(directory);
}
}  // namespace

FlyExtensions::FlyExtensions(
    PreferencesService& preferences,
    base::RepeatingClosure changed,
    base::RepeatingCallback<void(std::u16string)> announce)
    : preferences_(preferences),
      changed_(std::move(changed)),
      announce_(std::move(announce)) {
  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&ReadBundles),
      base::BindOnce(
          [](base::WeakPtr<FlyExtensions> self,
             std::vector<std::pair<
                 std::string, std::pair<base::FilePath, base::DictValue>>>
                 read) {
            if (!self)
              return;
            std::vector<Bundle> bundles;
            for (auto& [id, data] : read)
              bundles.push_back(
                  {id, std::move(data.first), std::move(data.second)});
            self->BundlesRead(std::move(bundles));
          },
          weak_factory_.GetWeakPtr()));
}
FlyExtensions::~FlyExtensions() = default;

std::string FlyExtensions::Directory(Profile* profile) {
  return profile->GetPath().BaseName().AsUTF8Unsafe();
}
base::DictValue FlyExtensions::ReadState() const {
  return preferences_->ReadNativeExtensions().value_or(base::DictValue());
}
bool FlyExtensions::WriteState(base::DictValue state) {
  return preferences_->SetNativeExtensions(std::move(state));
}
bool FlyExtensions::ListContains(const base::DictValue& state,
                                 std::string_view section,
                                 Profile* profile,
                                 const std::string& id) const {
  const auto* dict = state.FindDict(section);
  const auto* list = dict ? dict->FindList(Directory(profile)) : nullptr;
  return list && std::ranges::find(*list, base::Value(id)) != list->end();
}
bool FlyExtensions::Enabled() const {
  return preferences_->ReadBool("extensions_enabled").value_or(true);
}
bool FlyExtensions::CanEdit() const {
  return preferences_->CanEdit();
}
bool FlyExtensions::IsBundled(const std::string& id) const {
  return std::ranges::any_of(bundles_,
                             [&](const Bundle& b) { return b.id == id; });
}
bool FlyExtensions::BundleWanted(Profile* profile,
                                 const std::string& id) const {
  if (!Enabled())
    return false;
  const auto state = ReadState();
  if (const auto* removed = state.FindList("removed");
      removed && std::ranges::find(*removed, base::Value(id)) != removed->end())
    return false;
  return !ListContains(state, "workspace_off", profile, id);
}

void FlyExtensions::BundlesRead(std::vector<Bundle> bundles) {
  bundles_ = std::move(bundles);
  bundles_ready_ = true;
  for (Profile* profile : profiles_)
    ApplyProfile(profile);
}
void FlyExtensions::AddProfile(Profile* profile) {
  if (!profile || profile->IsOffTheRecord() || profiles_.contains(profile))
    return;
  profiles_.insert(profile);
  profile_obs_.AddObservation(profile);
  registries_.AddObservation(extensions::ExtensionRegistry::Get(profile));
  if (auto* dispatcher = extensions::ExtensionActionDispatcher::Get(profile))
    dispatchers_.AddObservation(dispatcher);
  ApplyProfile(profile);
}
void FlyExtensions::OnProfileWillBeDestroyed(Profile* profile) {
  if (auto* registry = extensions::ExtensionRegistry::Get(profile);
      registries_.IsObservingSource(registry))
    registries_.RemoveObservation(registry);
  if (auto* dispatcher = extensions::ExtensionActionDispatcher::Get(profile);
      dispatcher && dispatchers_.IsObservingSource(dispatcher))
    dispatchers_.RemoveObservation(dispatcher);
  profile_obs_.RemoveObservation(profile);
  profiles_.erase(profile);
}
void FlyExtensions::OnShuttingDown() {
  dispatchers_.RemoveAllObservations();
}
void FlyExtensions::ApplyProfile(Profile* profile) {
  if (!bundles_ready_ || !extensions::ExtensionSystem::Get(profile))
    return;
  auto* loader = extensions::ComponentLoader::Get(profile);
  if (!loader)
    return;
  for (const auto& bundle : bundles_) {
    const bool wanted = BundleWanted(profile, bundle.id);
    const bool loaded = loader->Exists(bundle.id);
    if (wanted && !loaded)
      loader->Add(bundle.manifest.Clone(), bundle.path);
    else if (!wanted && loaded)
      loader->Remove(bundle.id);
  }
  // The global switch suspends the user's own extensions and restores
  // exactly those it suspended.
  auto state = ReadState();
  const auto directory = Directory(profile);
  auto* registrar = extensions::ExtensionRegistrar::Get(profile);
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  bool changed = false;
  if (!Enabled()) {
    auto* suspended = EnsureList(state, "suspended", directory);
    std::vector<scoped_refptr<const extensions::Extension>> enabled;
    for (const auto& extension : registry->enabled_extensions())
      enabled.push_back(extension);
    for (const auto& extension : enabled) {
      if (extension->location() == extensions::mojom::ManifestLocation::kComponent ||
          extension->location() ==
              extensions::mojom::ManifestLocation::kExternalComponent ||
          !extensions::ExtensionSystem::Get(profile)
               ->management_policy()
               ->UserMayModifySettings(extension.get(), nullptr))
        continue;
      registrar->DisableExtension(
          extension->id(), {extensions::disable_reason::DISABLE_USER_ACTION});
      suspended->Append(extension->id());
      changed = true;
    }
  } else if (auto* section = state.FindDict("suspended");
             section && section->FindList(directory)) {
    for (const auto& id : *section->FindList(directory))
      if (id.is_string() && registry->disabled_extensions().GetByID(
                                id.GetString()))
        RestoreUserDisable(profile, id.GetString());
    section->Remove(directory);
    changed = true;
  }
  if (changed)
    WriteState(std::move(state));
}
bool FlyExtensions::SetEnabled(bool enabled) {
  if (!CanEdit() || !preferences_->SetBool("extensions_enabled", enabled))
    return false;
  for (Profile* profile : profiles_)
    ApplyProfile(profile);
  changed_.Run();
  return true;
}
std::vector<FlyExtensionInfo> FlyExtensions::List(Profile* profile) const {
  std::vector<FlyExtensionInfo> list;
  if (!profile)
    return list;
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  auto* policy = extensions::ExtensionSystem::Get(profile)->management_policy();
  const auto add = [&](const extensions::Extension& extension, bool enabled) {
    const bool bundled = IsBundled(extension.id());
    if (!extension.is_extension() ||
        (!bundled &&
         (extension.location() ==
              extensions::mojom::ManifestLocation::kComponent ||
          extension.location() ==
              extensions::mojom::ManifestLocation::kExternalComponent)))
      return;
    list.push_back({extension.id(), base::UTF8ToUTF16(extension.name()),
                    extension.VersionString(), enabled, bundled,
                    bundled || policy->UserMayModifySettings(&extension, nullptr),
                    extensions::OptionsPageInfo::HasOptionsPage(&extension)});
  };
  for (const auto& extension : registry->enabled_extensions())
    add(*extension, true);
  for (const auto& extension : registry->disabled_extensions())
    add(*extension, false);
  // Bundled extensions switched off here are unloaded, not disabled.
  for (const auto& bundle : bundles_) {
    if (registry->GetInstalledExtension(bundle.id))
      continue;
    const auto state = ReadState();
    const auto* removed = state.FindList("removed");
    if (removed &&
        std::ranges::find(*removed, base::Value(bundle.id)) != removed->end())
      continue;
    const auto* name = bundle.manifest.FindString("short_name");
    const auto* version = bundle.manifest.FindString("version");
    list.push_back({bundle.id, base::UTF8ToUTF16(name ? *name : bundle.id),
                    version ? *version : "", false, true, true, false});
  }
  std::ranges::sort(list, {}, &FlyExtensionInfo::name);
  return list;
}
bool FlyExtensions::SetExtensionEnabled(Profile* profile,
                                        const std::string& id,
                                        bool on) {
  if (!profile || !CanEdit())
    return false;
  if (IsBundled(id)) {
    auto state = ReadState();
    auto* list = EnsureList(state, "workspace_off", Directory(profile));
    list->EraseValue(base::Value(id));
    if (!on)
      list->Append(id);
    WriteState(std::move(state));
    ApplyProfile(profile);
    changed_.Run();
    return true;
  }
  auto* registry = extensions::ExtensionRegistry::Get(profile);
  const auto* extension = registry->GetInstalledExtension(id);
  if (!extension || !extensions::ExtensionSystem::Get(profile)
                         ->management_policy()
                         ->UserMayModifySettings(extension, nullptr))
    return false;
  auto* registrar = extensions::ExtensionRegistrar::Get(profile);
  if (on) {
    if (!RestoreUserDisable(profile, id))
      return false;
  } else {
    registrar->DisableExtension(
        id, {extensions::disable_reason::DISABLE_USER_ACTION});
  }
  changed_.Run();
  return true;
}
bool FlyExtensions::RestoreUserDisable(Profile* profile,
                                       const std::string& id) {
  // Only lift Fly's/the user's own disable. Other reasons (for example an
  // update asking for more permissions) need Chromium's own prompt, which
  // chrome://extensions shows; EnableExtension would clear them silently.
  auto* prefs = extensions::ExtensionPrefs::Get(profile);
  prefs->RemoveDisableReason(id,
                             extensions::disable_reason::DISABLE_USER_ACTION);
  if (prefs->GetDisableReasons(id).empty()) {
    extensions::ExtensionRegistrar::Get(profile)->EnableExtension(id);
    return true;
  }
  if (announce_)
    announce_.Run(u"This extension needs your approval in Manage extensions "
                  u"before it can run again.");
  return false;
}
bool FlyExtensions::Remove(Profile* profile, const std::string& id) {
  if (!profile || !CanEdit())
    return false;
  if (IsBundled(id)) {
    // Removal of a bundled extension applies everywhere and survives updates.
    auto state = ReadState();
    auto* removed = state.FindList("removed");
    if (!removed) {
      state.Set("removed", base::ListValue());
      removed = state.FindList("removed");
    }
    if (std::ranges::find(*removed, base::Value(id)) == removed->end())
      removed->Append(id);
    WriteState(std::move(state));
    for (Profile* each : profiles_)
      ApplyProfile(each);
    changed_.Run();
    return true;
  }
  std::u16string error;
  const bool removed = extensions::ExtensionRegistrar::Get(profile)
                           ->UninstallExtension(
                               id, extensions::UNINSTALL_REASON_USER_INITIATED,
                               &error);
  changed_.Run();
  return removed;
}
void FlyExtensions::OpenOptions(BrowserWindowInterface* browser,
                                const std::string& id) {
  if (!browser)
    return;
  const auto* extension = extensions::ExtensionRegistry::Get(
                              browser->GetProfile())
                              ->enabled_extensions()
                              .GetByID(id);
  if (!extension || !extensions::OptionsPageInfo::HasOptionsPage(extension))
    return;
  NavigateParams params(browser,
                        extensions::OptionsPageInfo::GetOptionsPage(extension),
                        ui::PAGE_TRANSITION_LINK);
  params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  Navigate(&params);
}

std::vector<const extensions::Extension*> FlyExtensions::ActionExtensions(
    Profile* profile) const {
  std::vector<const extensions::Extension*> result;
  if (!profile)
    return result;
  auto* manager = extensions::ExtensionActionManager::Get(profile);
  for (const auto& extension :
       extensions::ExtensionRegistry::Get(profile)->enabled_extensions()) {
    if (manager->GetExtensionAction(*extension) &&
        (IsBundled(extension->id()) ||
         (extension->location() !=
              extensions::mojom::ManifestLocation::kComponent &&
          extension->location() !=
              extensions::mojom::ManifestLocation::kExternalComponent)))
      result.push_back(extension.get());
  }
  std::ranges::sort(result, {}, &extensions::Extension::name);
  return result;
}
gfx::Image FlyExtensions::ActionIcon(
    content::WebContents* contents,
    const extensions::Extension& extension) const {
  auto* action = extensions::ExtensionActionManager::Get(
                     contents ? contents->GetBrowserContext() : nullptr)
                     ->GetExtensionAction(extension);
  if (!action)
    return gfx::Image();
  const int tab = contents ? extensions::ExtensionTabUtil::GetTabId(contents)
                           : extensions::ExtensionAction::kDefaultTabId;
  auto icon = action->GetExplicitlySetIcon(tab);
  if (icon.IsEmpty())
    icon = action->GetDefaultIconImage();
  return icon;
}
std::u16string FlyExtensions::ActionTitle(
    content::WebContents* contents,
    const extensions::Extension& extension) const {
  auto* action =
      contents ? extensions::ExtensionActionManager::Get(
                     contents->GetBrowserContext())
                     ->GetExtensionAction(extension)
               : nullptr;
  const auto title =
      action ? action->GetTitle(extensions::ExtensionTabUtil::GetTabId(contents))
             : std::string();
  return base::UTF8ToUTF16(title.empty() ? extension.name() : title);
}
std::string FlyExtensions::ActionBadge(
    content::WebContents* contents,
    const extensions::Extension& extension) const {
  auto* action =
      contents ? extensions::ExtensionActionManager::Get(
                     contents->GetBrowserContext())
                     ->GetExtensionAction(extension)
               : nullptr;
  return action ? action->GetDisplayBadgeText(
                      extensions::ExtensionTabUtil::GetTabId(contents))
                : std::string();
}
std::vector<PaletteEntry> FlyExtensions::ActionEntries(Profile* profile) const {
  std::vector<PaletteEntry> entries;
  for (const auto* extension : ActionExtensions(profile)) {
    PaletteEntry entry;
    entry.id = entry.action = "extension:" + extension->id();
    entry.title = extension->name();
    entry.category = "Extension action";
    entries.push_back(std::move(entry));
  }
  for (const auto& [action, title, category] :
       {std::tuple{"extensions-settings", "Extension settings",
                   "Preferences"},
        std::tuple{"extensions-page", "Manage extensions",
                   "chrome://extensions"},
        std::tuple{"extensions-store", "Chrome Web Store",
                   "chromewebstore.google.com"}}) {
    PaletteEntry entry;
    entry.id = entry.action = action;
    entry.title = title;
    entry.category = category;
    entries.push_back(std::move(entry));
  }
  return entries;
}
void FlyExtensions::RunAction(BrowserWindowInterface* browser,
                              const std::string& id,
                              views::View* anchor) {
  if (!browser || !anchor)
    return;
  auto* contents = browser->GetTabStripModel()->GetActiveWebContents();
  const auto* extension = extensions::ExtensionRegistry::Get(
                              browser->GetProfile())
                              ->enabled_extensions()
                              .GetByID(id);
  if (!contents || !extension)
    return;
  auto* runner = extensions::ExtensionActionRunner::GetForWebContents(contents);
  if (!runner)
    return;
  if (runner->RunAction(extension, /*grant_tab_permissions=*/true) !=
      extensions::ExtensionAction::ShowAction::kShowPopup)
    return;
  auto* action = extensions::ExtensionActionManager::Get(browser->GetProfile())
                     ->GetExtensionAction(*extension);
  const GURL popup =
      action->GetPopupUrl(extensions::ExtensionTabUtil::GetTabId(contents));
  if (!popup.is_valid())
    return;
  ExtensionPopup::ShowPopup(
      browser,
      extensions::ExtensionViewHostFactory::CreatePopupHost(*extension, popup,
                                                            browser),
      views::BubbleAnchor(anchor), views::BubbleBorder::BOTTOM_RIGHT,
      PopupShowAction::kShow, {});
}

void FlyExtensions::OnExtensionLoaded(content::BrowserContext*,
                                      const extensions::Extension*) {
  changed_.Run();
}
void FlyExtensions::OnExtensionInstalled(
    content::BrowserContext* context,
    const extensions::Extension* extension,
    bool is_update) {
  // With extensions switched off, a new installation is suspended at once.
  if (!Enabled()) {
    for (Profile* profile : profiles_)
      if (profile == Profile::FromBrowserContext(context))
        base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](base::WeakPtr<FlyExtensions> self, Profile* profile) {
                  if (self && self->profiles_.contains(profile))
                    self->ApplyProfile(profile);
                },
                weak_factory_.GetWeakPtr(), profile));
  }
  if (is_update || !announce_ || !extension->is_extension() ||
      extension->location() == extensions::mojom::ManifestLocation::kComponent)
    return;
  announce_.Run(u"Installed " + base::UTF8ToUTF16(extension->name()) +
                u". Its button is in the footer; manage it in Preferences.");
}
void FlyExtensions::OnExtensionUnloaded(content::BrowserContext*,
                                        const extensions::Extension*,
                                        extensions::UnloadedExtensionReason) {
  changed_.Run();
}
void FlyExtensions::OnExtensionActionUpdated(extensions::ExtensionAction*,
                                             content::WebContents*,
                                             content::BrowserContext*) {
  changed_.Run();
}

bool FlyExtensions::ReloadUnpacked() {
  bool any = false;
  for (Profile* profile : profiles_) {
    std::vector<std::string> ids;
    for (const auto& extension :
         extensions::ExtensionRegistry::Get(profile)->enabled_extensions())
      if (extensions::Manifest::IsUnpackedLocation(extension->location()))
        ids.push_back(extension->id());
    for (const auto& id : ids) {
      extensions::ExtensionRegistrar::Get(profile)->ReloadExtension(id);
      any = true;
    }
  }
  return any;
}

// static
base::FilePath FlyExtensions::Folder() {
  base::FilePath dir;
  if (!base::PathService::Get(chrome::DIR_USER_DATA, &dir))
    return base::FilePath();
  return dir.AppendASCII("extensions");
}

namespace {
// Unpacked extensions need developer mode on the profile, as when loaded
// from chrome://extensions; otherwise Chromium disables them at start.
void LoadUnpackedNow(base::WeakPtr<Profile> profile,
                     base::RepeatingCallback<void(std::u16string)> announce,
                     const base::FilePath& path) {
  if (!profile || path.empty())
    return;
  profile->GetPrefs()->SetBoolean(prefs::kExtensionsUIDeveloperMode, true);
  auto installer = extensions::UnpackedInstaller::Create(profile.get());
  installer->set_be_noisy_on_failure(true);
  installer->set_completion_callback(base::BindOnce(
      [](base::RepeatingCallback<void(std::u16string)> announce,
         const extensions::Extension* extension, const base::FilePath&,
         const std::u16string& error) {
        if (!announce)
          return;
        announce.Run(extension ? u"Loaded " +
                                     base::UTF8ToUTF16(extension->name()) +
                                     u" in this workspace."
                               : u"The extension could not be loaded: " +
                                     error);
      },
      announce));
  installer->Load(path);
}
}  // namespace

namespace {
// A local install in progress: a unique staging folder inside the
// extensions folder (Electron's .install-*), with the copy in `unpacked`.
struct Staged {
  base::FilePath staging;
  base::FilePath unpacked;
  std::u16string error;
};
Staged Stage() {
  Staged staged;
  const auto folder = FlyExtensions::Folder();
  if (!base::CreateDirectory(folder) ||
      !base::CreateTemporaryDirInDir(folder, ".install-", &staged.staging)) {
    staged.error = u"The extensions folder is not writable.";
    return staged;
  }
  staged.unpacked = staged.staging.AppendASCII("unpacked");
  return staged;
}
// The extension id Chromium gives an unpacked extension in `folder` (its
// manifest key when present, otherwise the folder path), and the key id.
std::optional<std::string> KeyId(const base::FilePath& root) {
  std::string json;
  if (!base::ReadFileToString(root.AppendASCII("manifest.json"), &json))
    return std::nullopt;
  const auto manifest = base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  const auto* key = manifest ? manifest->FindString("key") : nullptr;
  std::string bytes;
  if (!key || !base::Base64Decode(*key, &bytes))
    return std::nullopt;
  return crx_file::id_util::GenerateId(bytes);
}
std::string IdIn(const base::FilePath& folder, const base::FilePath& root) {
  return KeyId(root).value_or(crx_file::id_util::GenerateIdForPath(folder));
}
// Electron's extensionStore.js install step: the destination is the folder
// of an installed extension with the same manifest key, otherwise
// local-<first 24 hex of sha256(absolute source path)>, so reinstalling the
// same source replaces it and keeps its id. A different id is refused; the
// old folder is set aside and restored if the swap fails.
std::pair<base::FilePath, std::u16string> Finish(Staged staged,
                                                 base::FilePath source) {
  base::ScopedClosureRunner cleanup(base::BindOnce(
      [](base::FilePath staging) { base::DeletePathRecursively(staging); },
      staged.staging));
  if (!base::PathExists(staged.unpacked.AppendASCII("manifest.json")))
    return {{}, u"That is not an extension (no manifest.json)."};
  const auto folder = FlyExtensions::Folder();
  base::FilePath destination;
  if (const auto key_id = KeyId(staged.unpacked)) {
    base::FileEnumerator installed(folder, false,
                                   base::FileEnumerator::DIRECTORIES);
    for (auto path = installed.Next(); !path.empty();
         path = installed.Next()) {
      if (!path.BaseName().value().starts_with(".") &&
          KeyId(path) == key_id) {
        destination = path;
        break;
      }
    }
  }
  if (destination.empty()) {
    const auto absolute =
        base::MakeAbsoluteFilePathNoResolveSymbolicLinks(source);
    const auto digest = crypto::SHA256HashString(
        (absolute ? *absolute : source).value());
    destination = folder.AppendASCII(
        "local-" + base::HexEncodeLower(digest).substr(0, 24));
  }
  const bool previous = base::PathExists(destination);
  if (previous) {
    if (!base::PathExists(destination.AppendASCII("manifest.json")))
      return {{}, u"An invalid extension already occupies this location. "
                  u"Remove it first."};
    if (IdIn(destination, destination) !=
        IdIn(destination, staged.unpacked))
      return {{}, u"The new package has a different extension identity."};
  }
  const auto backup = staged.staging.AppendASCII("previous");
  if (previous && !base::Move(destination, backup))
    return {{}, u"The installed copy could not be replaced."};
  if (!base::Move(staged.unpacked, destination)) {
    if (previous)
      base::Move(backup, destination);
    return {{}, u"The extension could not be moved into the extensions "
                u"folder."};
  }
  return {destination, u""};
}
void FinishAndLoad(base::WeakPtr<Profile> profile,
                   base::RepeatingCallback<void(std::u16string)> announce,
                   Staged staged,
                   base::FilePath source) {
  extensions::GetExtensionFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce(&Finish, std::move(staged), std::move(source)),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile,
             base::RepeatingCallback<void(std::u16string)> announce,
             std::pair<base::FilePath, std::u16string> result) {
            if (result.first.empty()) {
              if (announce)
                announce.Run(result.second);
              return;
            }
            LoadUnpackedNow(profile, announce, result.first);
          },
          profile, announce));
}
}  // namespace

void FlyExtensions::InstallPackage(Profile* profile,
                                   content::WebContents* contents,
                                   const base::FilePath& file) {
  if (!profile || profile->IsOffTheRecord())
    return;
  const auto extension = base::ToLowerASCII(file.Extension());
  if (extension == ".crx") {
    auto installer = extensions::CrxInstaller::Create(
        profile,
        contents ? std::make_unique<ExtensionInstallPrompt>(contents, nullptr)
                 : nullptr);
    installer->set_off_store_install_allow_reason(
        extensions::CrxInstaller::OffStoreInstallAllowedFromSettingsPage);
    installer->InstallCrx(file);
    return;
  }
  if (extension != ".zip") {
    if (announce_)
      announce_.Run(u"Choose a .zip or .crx extension package.");
    return;
  }
  extensions::GetExtensionFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE, base::BindOnce([] {
        auto staged = Stage();
        // LoadFromZipFileInDir unpacks into an existing folder only.
        if (staged.error.empty() && !base::CreateDirectory(staged.unpacked))
          staged.error = u"The extensions folder is not writable.";
        return staged;
      }),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile,
             base::RepeatingCallback<void(std::u16string)> announce,
             base::FilePath file, Staged staged) {
            if (!staged.error.empty()) {
              extensions::GetExtensionFileTaskRunner()->PostTask(
                  FROM_HERE, base::GetDeletePathRecursivelyCallback(
                                 staged.staging));
              if (announce)
                announce.Run(staged.error);
              return;
            }
            const auto unpacked = staged.unpacked;
            auto installer = extensions::ZipFileInstaller::Create(
                extensions::GetExtensionFileTaskRunner(),
                base::BindOnce(
                    [](base::WeakPtr<Profile> profile,
                       base::RepeatingCallback<void(std::u16string)> announce,
                       Staged staged, base::FilePath file,
                       const base::FilePath&, const base::FilePath&,
                       const std::string& error) {
                      if (!error.empty()) {
                        extensions::GetExtensionFileTaskRunner()->PostTask(
                            FROM_HERE, base::GetDeletePathRecursivelyCallback(
                                           staged.staging));
                        if (announce)
                          announce.Run(u"The package could not be unpacked: " +
                                       base::UTF8ToUTF16(error));
                        return;
                      }
                      FinishAndLoad(profile, announce, std::move(staged),
                                    std::move(file));
                    },
                    profile, announce, std::move(staged), file));
            installer->LoadFromZipFileInDir(file, unpacked);
          },
          profile->GetWeakPtr(), announce_, file));
}

void FlyExtensions::LoadUnpacked(Profile* profile,
                                 const base::FilePath& folder) {
  if (!profile || profile->IsOffTheRecord() || folder.empty())
    return;
  extensions::GetExtensionFileTaskRunner()->PostTaskAndReplyWithResult(
      FROM_HERE,
      base::BindOnce(
          [](base::FilePath source) {
            if (!base::PathExists(source.AppendASCII("manifest.json"))) {
              Staged staged;
              staged.error =
                  u"That folder is not an extension (no manifest.json).";
              return staged;
            }
            auto staged = Stage();
            if (staged.error.empty() &&
                !base::CopyDirectory(source, staged.unpacked, true)) {
              base::DeletePathRecursively(staged.staging);
              staged.error =
                  u"The folder could not be copied into the extensions "
                  u"folder.";
            }
            return staged;
          },
          folder),
      base::BindOnce(
          [](base::WeakPtr<Profile> profile,
             base::RepeatingCallback<void(std::u16string)> announce,
             base::FilePath source, Staged staged) {
            if (!staged.error.empty()) {
              if (announce)
                announce.Run(staged.error);
              return;
            }
            FinishAndLoad(profile, announce, std::move(staged),
                          std::move(source));
          },
          profile->GetWeakPtr(), announce_, folder));
}

std::u16string FlyExtensions::Usage(Profile* profile) const {
  if (!profile)
    return u"";
  auto* processes = extensions::ProcessManager::Get(profile);
  std::u16string text;
  for (const auto& extension :
       extensions::ExtensionRegistry::Get(profile)->enabled_extensions()) {
    if (extension->location() == extensions::mojom::ManifestLocation::kComponent &&
        !IsBundled(extension->id()))
      continue;
    std::set<content::ChildProcessId> ids;
    for (auto* frame : processes->GetRenderFrameHostsForExtension(extension->id()))
      ids.insert(frame->GetProcess()->GetID());
    for (const auto& worker :
         processes->GetServiceWorkersForExtension(extension->id()))
      ids.insert(worker.render_process_id);
    uint64_t bytes = 0;
    for (const auto& id : ids) {
      auto* host = content::RenderProcessHost::FromID(id);
      if (!host || !host->GetProcess().IsValid())
        continue;
      const auto metrics = base::ProcessMetrics::CreateProcessMetrics(
          host->GetProcess().Handle());
      if (const auto info = metrics->GetMemoryInfo(); info.has_value())
        bytes += info->resident_set_bytes;
    }
    text += base::UTF8ToUTF16(extension->name()) + u": " +
            (ids.empty() ? std::u16string(u"idle")
                         : base::NumberToString16(bytes / (1024 * 1024)) +
                               u" MB in " + base::NumberToString16(ids.size()) +
                               (ids.size() == 1 ? u" process" : u" processes")) +
            u"\n";
  }
  return text.empty() ? u"No extensions are running in this workspace." : text;
}
}  // namespace fly
