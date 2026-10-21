# Configuration and storage

Native Fly's launcher uses `$XDG_CONFIG_HOME/fly-browser-native`, defaulting to
`~/.config/fly-browser-native`. A direct engine invocation must include
`--fly-native` and the intended `--user-data-dir`. Never point Chromium at the
Electron profile (`~/.config/fly-browser`).

## Preferences

Use **Preferences** for appearance, keyboard bindings, search and aliases,
performance, privacy and extensions. Fly stores product dictionaries in
Chromium's `Local State`; workspace profiles hold Chromium `Preferences`,
cookies, history and extension state. Files are managed by Chromium's
PrefService. Close Fly and back up the directory before inspecting or changing
anything manually. Editing the old Electron `config.json` after import does
not change native settings.

The implementation contracts are in
`chromium/overlay/fly/preferences/preferences_service.{h,cc}` and the native
workspace, keyboard, bookmark and session services. The default process limit
is four; it is a renderer process policy, not a guarantee that every site can
share one renderer. Interface scale and website zoom are separate preferences.

## Local user content

Put `.user.js` in `<user data>/scripts/` and `.user.css` in `<user data>/styles/`.
Fly generates `<user data>/fly-user-content/`, a component extension, from these
files. Do not edit the generated component. Legacy `script-storage/` values
seed extension storage on import; new GM values live in extension storage.
See [user scripts and styles](/guide/user-scripts-and-styles).

## Import and backup

The importer reads legacy JSON files once, maps them to native preferences,
and records its summary and pending profile data in `fly-legacy-import.json`.
An interrupted import uses `fly-legacy-import.partial` for rollback and retry.
Keep these files with the rest of the profile when backing up. A full backup
requires closing Fly and copying the entire native user data directory.

Removing the native installation keeps data. Deleting the native profile is a
separate destructive action; the original Electron profile stays independent.
The [archived Electron reference](https://github.com/Arzuparreta/fly-browser/tree/main/legacy/electron/docs)
describes the old JSON schema for comparison only.
