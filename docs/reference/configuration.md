# Configuration and storage

Fly's launcher uses `$XDG_CONFIG_HOME/fly-browser`, defaulting to
`~/.config/fly-browser`. A direct engine invocation must include
`--fly-native` and the intended `--user-data-dir`.

## Preferences

Use **Preferences** for appearance, keyboard bindings, search and aliases,
performance, privacy and extensions. Fly stores product dictionaries in
Chromium's `Local State`; workspace profiles hold Chromium `Preferences`,
cookies, history and extension state. Files are managed by Chromium's
PrefService. Close Fly and back up the directory before inspecting or changing
anything manually.

The implementation contracts are in
`chromium/overlay/fly/preferences/preferences_service.{h,cc}` and the
workspace, keyboard, bookmark and session services. The default process limit
is four; it is a renderer process policy, not a guarantee that every site can
share one renderer. Interface scale and website zoom are separate preferences.

## Local user content

Put `.user.js` in `<user data>/scripts/` and `.user.css` in `<user data>/styles/`.
Fly generates `<user data>/fly-user-content/`, a component extension, from these
files. Do not edit the generated component. GM values live in extension
storage. See [user scripts and styles](/guide/user-scripts-and-styles).

## Backup

A full backup requires closing Fly and copying the entire user data directory.

Removing the installation keeps data. Deleting the profile is a separate
destructive action: `fly-install uninstall --purge`.
