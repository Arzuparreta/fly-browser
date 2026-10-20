// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_IMPORT_LEGACY_IMPORT_H_
#define FLY_IMPORT_LEGACY_IMPORT_H_

#include <string>

#include "base/files/file_path.h"
#include "fly/commands/keyboard_config.h"

class PrefService;

namespace fly {
// Import from Electron Fly (~/.config/fly-browser), run once at start before
// any profile loads, into a fresh native user data directory. When Electron
// Fly is open at that first start, it runs on the first start after it closes.
//
// The legacy directory is only read. Each Electron workspace becomes a native
// workspace profile whose site data (cookies, local/session storage,
// IndexedDB, service workers) is copied from its partition; in a fresh
// directory the first one uses "Default". App settings, search, keyboard, bookmarks, hidden frequent sites,
// user scripts/styles and their GM values are written now. Tabs, history and
// site zoom need the loaded profiles: they are stored in
// <user data>/fly-legacy-import.json and applied by the window controller.
// A summary for the user is stored there too.
inline constexpr char kLegacyImportFile[] = "fly-legacy-import.json";
// Present while the import waits for Electron Fly to close: it runs on the
// first start after that, adding the Electron workspaces next to the native
// ones and keeping the settings native Fly already stores.
inline constexpr char kLegacyImportWaitingFile[] = "fly-legacy-import.waiting";

enum class LegacyImportResult {
  kNotApplicable,  // No legacy data, not a fresh native directory, or done.
  kSourceRunning,  // Electron Fly holds its profile; runs on a later start.
  kImported,
  kFailed,
};

// Electron Fly keeps its own files (session, settings, scripts) in
// ~/.config/fly-browser and Electron's (Partitions, SingletonLock) in
// $XDG_CONFIG_HOME/fly-browser; both are the --fly-import-from directory when
// it is given.
base::FilePath LegacyProfileDirectory();
base::FilePath LegacyDataDirectory();
// Electron's keybinds.json as Electron Fly loads it (keybinds.js
// initialiseCandidate): values earlier versions wrote as defaults are not
// user choices, and defaults give up the combinations the user's choices use.
KeyboardValidation ResolveLegacyKeyboard(const base::DictValue& raw);
// `legacy_data_dir` empty means `legacy_dir`.
LegacyImportResult RunLegacyImport(PrefService& local_state,
                                   const base::FilePath& user_data_dir,
                                   const base::FilePath& legacy_dir,
                                   base::FilePath legacy_data_dir = {});
}  // namespace fly

#endif  // FLY_IMPORT_LEGACY_IMPORT_H_
