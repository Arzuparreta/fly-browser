// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_FLY_EXTENSIONS_H_
#define FLY_UI_FLY_EXTENSIONS_H_

#include <set>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/values.h"
#include "base/memory/raw_ref.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_multi_source_observation.h"
#include "chrome/browser/extensions/extension_action_dispatcher.h"
#include "chrome/browser/profiles/profile_observer.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_registry_observer.h"
#include "fly/commands/palette_catalog.h"
#include "ui/gfx/image/image.h"

class BrowserWindowInterface;
class Profile;
namespace content {
class WebContents;
}
namespace views {
class View;
}

namespace fly {
class PreferencesService;
struct FlyExtensionInfo {
  std::string id;
  std::u16string name;
  std::string version;
  bool enabled = false;
  bool bundled = false;
  bool removable = false;
  bool has_options = false;
};

// Extensions in native Fly. Each workspace is a Chromium profile, so installed
// extensions are naturally per workspace. Fly adds:
// - Bundled extensions (uBlock Origin Lite) loaded as Fly components from the
//   installation's fly_extensions directory into every normal workspace,
//   unless removed (all workspaces) or switched off in that workspace.
// - The global "Enable Chrome extensions" switch: off unloads the bundled
//   extensions and suspends the user's own, restoring exactly those later.
// - Action buttons and popups in the footer (the Chromium toolbar is hidden).
// State lives in the fly.preferences "native_extensions" entry.
class FlyExtensions : public extensions::ExtensionRegistryObserver,
                      public extensions::ExtensionActionDispatcher::Observer,
                      public ProfileObserver {
 public:
  FlyExtensions(PreferencesService& preferences,
                base::RepeatingClosure changed,
                base::RepeatingCallback<void(std::u16string)> announce = {});
  ~FlyExtensions() override;
  FlyExtensions(const FlyExtensions&) = delete;
  FlyExtensions& operator=(const FlyExtensions&) = delete;

  // Normal (not off-the-record) workspace profiles. Idempotent.
  void AddProfile(Profile* profile);

  bool Enabled() const;
  bool CanEdit() const;
  bool SetEnabled(bool enabled);
  std::vector<FlyExtensionInfo> List(Profile* profile) const;
  bool SetExtensionEnabled(Profile* profile, const std::string& id, bool on);
  bool Remove(Profile* profile, const std::string& id);
  void OpenOptions(BrowserWindowInterface* browser, const std::string& id);

  // Extensions with a toolbar action, enabled in `profile`, by name.
  std::vector<const extensions::Extension*> ActionExtensions(
      Profile* profile) const;
  gfx::Image ActionIcon(content::WebContents* contents,
                        const extensions::Extension& extension) const;
  std::u16string ActionTitle(content::WebContents* contents,
                             const extensions::Extension& extension) const;
  std::string ActionBadge(content::WebContents* contents,
                          const extensions::Extension& extension) const;
  std::vector<PaletteEntry> ActionEntries(Profile* profile) const;
  // Runs the action like a toolbar click; popups anchor to `anchor`.
  void RunAction(BrowserWindowInterface* browser,
                 const std::string& id,
                 views::View* anchor);

  bool IsBundled(const std::string& id) const;
  // Reloads every unpacked extension in every workspace; false when none.
  bool ReloadUnpacked();

  // ExtensionRegistryObserver:
  void OnExtensionLoaded(content::BrowserContext* context,
                         const extensions::Extension* extension) override;
  void OnExtensionInstalled(content::BrowserContext* context,
                            const extensions::Extension* extension,
                            bool is_update) override;
  void OnExtensionUnloaded(content::BrowserContext* context,
                           const extensions::Extension* extension,
                           extensions::UnloadedExtensionReason reason) override;
  // ExtensionActionDispatcher::Observer:
  void OnExtensionActionUpdated(
      extensions::ExtensionAction* action,
      content::WebContents* contents,
      content::BrowserContext* context) override;
  void OnShuttingDown() override;
  // ProfileObserver:
  void OnProfileWillBeDestroyed(Profile* profile) override;

 private:
  struct Bundle {
    std::string id;
    base::FilePath path;
    base::DictValue manifest;
  };
  void BundlesRead(std::vector<Bundle> bundles);
  void ApplyProfile(Profile* profile);
  bool RestoreUserDisable(Profile* profile, const std::string& id);
  bool BundleWanted(Profile* profile, const std::string& id) const;
  base::DictValue ReadState() const;
  bool WriteState(base::DictValue state);
  bool ListContains(const base::DictValue& state,
                    std::string_view section,
                    Profile* profile,
                    const std::string& id) const;
  static std::string Directory(Profile* profile);

  const raw_ref<PreferencesService> preferences_;
  base::RepeatingClosure changed_;
  base::RepeatingCallback<void(std::u16string)> announce_;
  std::vector<Bundle> bundles_;
  bool bundles_ready_ = false;
  std::set<raw_ptr<Profile>> profiles_;
  base::ScopedMultiSourceObservation<extensions::ExtensionRegistry,
                                     extensions::ExtensionRegistryObserver>
      registries_{this};
  base::ScopedMultiSourceObservation<Profile, ProfileObserver> profile_obs_{
      this};
  base::ScopedMultiSourceObservation<extensions::ExtensionActionDispatcher,
                                     extensions::ExtensionActionDispatcher::Observer>
      dispatchers_{this};
  base::WeakPtrFactory<FlyExtensions> weak_factory_{this};
};
}  // namespace fly

#endif  // FLY_UI_FLY_EXTENSIONS_H_
