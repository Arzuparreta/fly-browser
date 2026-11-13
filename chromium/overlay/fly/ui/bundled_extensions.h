// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_UI_BUNDLED_EXTENSIONS_H_
#define FLY_UI_BUNDLED_EXTENSIONS_H_

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_path.h"
#include "base/functional/callback_forward.h"
#include "base/values.h"

class ChromeBrowserMainExtraParts;
class Profile;

namespace fly {
class PreferencesService;

// An extension shipped with Fly in <assets>/fly_extensions/<id>
// (chromium/bundled-extensions.json), loaded as a Fly-managed component.
struct BundledExtension {
  BundledExtension();
  BundledExtension(std::string id, base::FilePath path,
                   base::DictValue manifest);
  BundledExtension(BundledExtension&&);
  BundledExtension& operator=(BundledExtension&&);
  ~BundledExtension();
  BundledExtension Clone() const;

  std::string id;
  base::FilePath path;
  base::DictValue manifest;
};

// The bundled extensions, read once per browser process; null until read.
// Reading them also moves their indexed declarativeNetRequest rulesets out of
// the installation, which is read-only once packaged, into
// <user data>/Bundled Extension Rules/<id>.
const std::vector<BundledExtension>* BundledExtensionCatalog();
// Reads the catalog on a worker and then runs `done`; runs it at once when
// the catalog is already read.
void ReadBundledExtensionCatalogAsync(base::OnceClosure done);

// Whether Fly's settings want the bundled extension `id` in `profile`: Chrome
// extensions are on, it was not removed, and it is not off in that workspace.
bool BundledExtensionWanted(const PreferencesService& preferences,
                            Profile* profile,
                            const std::string& id);
// Fly's settings key for `profile`: its profile directory name.
std::string BundledExtensionsWorkspaceKey(Profile* profile);

// Loads the wanted bundled extensions that `profile` has not loaded yet.
// Does nothing before the catalog is read.
void LoadWantedBundledExtensions(Profile* profile);

// Reads the catalog before any profile loads, while startup may still read
// files on the UI thread, and loads the bundled extensions into every
// browsing profile as soon as it is initialised, before its tabs are
// restored. Native Fly only.
std::unique_ptr<ChromeBrowserMainExtraParts>
CreateBundledExtensionsMainParts();

}  // namespace fly

#endif  // FLY_UI_BUNDLED_EXTENSIONS_H_
