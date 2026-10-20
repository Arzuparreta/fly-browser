// Copyright Fly Browser contributors. All rights reserved.
// Use of this source code is governed by the GPL-3.0-or-later license in the
// Fly repository.
#ifndef FLY_IMPORT_LEGACY_IMPORT_H_
#define FLY_IMPORT_LEGACY_IMPORT_H_

#include <string>

#include "base/files/file_path.h"

class PrefService;

namespace fly {
// Import from Electron Fly (~/.config/fly-browser), run once at start before
// any profile loads, into a fresh native user data directory.
//
// The legacy directory is only read. Each Electron workspace becomes a native
// workspace profile whose site data (cookies, local/session storage,
// IndexedDB, service workers) is copied from its partition; the first one uses
// "Default". App settings, search, keyboard, bookmarks, hidden frequent sites,
// user scripts/styles and their GM values are written now. Tabs, history and
// site zoom need the loaded profiles: they are stored in
// <user data>/fly-legacy-import.json and applied by the window controller.
// A summary for the user is stored there too.
inline constexpr char kLegacyImportFile[] = "fly-legacy-import.json";

enum class LegacyImportResult {
  kNotApplicable,  // No legacy data, not a fresh native directory, or done.
  kSourceRunning,  // Electron Fly holds its profile; retry next start.
  kImported,
  kFailed,
};

// $XDG_CONFIG_HOME/fly-browser (or the --fly-import-from directory).
base::FilePath LegacyProfileDirectory();
LegacyImportResult RunLegacyImport(PrefService& local_state,
                                   const base::FilePath& user_data_dir,
                                   const base::FilePath& legacy_dir);
}  // namespace fly

#endif  // FLY_IMPORT_LEGACY_IMPORT_H_
