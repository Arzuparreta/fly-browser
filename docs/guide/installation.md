# Installation and migration

Fly now runs on native Chromium. Release packages currently support Linux
x86_64. Older macOS, Windows and AppImage packages are Electron legacy, not
native Fly. Screenshots retained in these guides may show the legacy interface.

## Installer script (any distribution)

```sh
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

The installer downloads `Fly-<version>-native-linux-x64.tar.gz`, verifies it
against the release's `SHA256SUMS.txt`, and runs its `install.sh`. If the latest
release has only Electron assets, installation stops without substituting Electron.

```sh
fly-install status
fly-install update
fly-install uninstall --yes
# Also delete profiles, history, passwords and settings (even if Fly is already removed):
fly-install uninstall --purge --yes
```

The user installation lives in `$XDG_DATA_HOME/fly-browser-native` (default
`~/.local/share/fly-browser-native`). Start `~/.local/bin/fly-browser` or
`~/.local/bin/fly-browser-native`, or choose **Fly** in your launcher. Add
`~/.local/bin` to `PATH` if needed. Uninstall removes the Arch package and user installation, and keeps profiles by default.
`--purge` also deletes the native and Electron Fly profiles and launcher flags
under `$XDG_CONFIG_HOME` (default `~/.config`), even if no installation remains.
Legacy AppImage binaries are managed separately by the legacy installer.

On Arch-based systems with `pacman` and `makepkg`, installation uses the
release's `PKGBUILD` and installs `fly-browser-bin`. You can request it with
`fly-install install --package`; `--native` selects the user tarball when no
system package is installed. A system package is updated through `makepkg`,
not replaced with a second user installation. Remove it with
`sudo pacman -R fly-browser-bin`.

## Manual tarball

Download the native Linux tarball and `SHA256SUMS.txt` from
[Releases](https://github.com/Arzuparreta/fly-browser/releases), verify the
archive with `sha256sum`, extract it, enter `fly-browser-native/` and run
`./install.sh`. Run `./install.sh --remove` from the extracted bundle to remove
the user installation. Linux must support Chromium's sandbox and runtime
libraries; no launcher disables the sandbox.

## Moving from Electron

Close Electron Fly before starting native Fly. Native data goes into
`$XDG_CONFIG_HOME/fly-browser-native` (default
`~/.config/fly-browser-native`), separate from Electron's `~/.config/fly-browser`.
The importer reads legacy settings there and partition storage from Electron's
XDG data location. `--fly-import-from=/path/to/legacy-profile` selects an
explicit source. Never pass a legacy directory as `--user-data-dir`.

On first import, workspaces become Chromium profiles. Supported settings,
shortcuts, bookmarks, tabs, history, site zoom, scripts/styles and workspace
site storage are imported. The legacy source is only read. A running Electron
instance defers import until it closes; an interrupted import is rolled back
and retried. The import summary reports extensions that need reinstalling.

Hand-installed Electron extensions must be reinstalled from the Chrome Web
Store. uBlock Origin Lite replaces full uBlock Origin Lite; filter configuration is
not interchangeable. Saved removal/disable choices are preserved. Some sites
may require signing in again. See [extensions](/guide/extensions) and
[user scripts](/guide/user-scripts-and-styles) for compatibility limits.

To return to Electron for comparison, use
`npm run start:legacy` in the source checkout or an older installed binary.
Its original profile remains available. Native changes are not exported back.

## Running from source

Follow [Chromium development](/chromium/build) to prepare and build the pinned
engine. Then run `FLY_ENGINE_ROOT=/path/to/fly-chromium npm start` from the repo.
Set `FLY_ENGINE_OUTPUT=out/FlyNative` to run the development build. Building
Chromium is explicit and requires substantial disk, memory and time; launching
does not install Electron or fetch an engine automatically.

## Updates

`fly-install update` installs the latest stable native package. Preferences can
also check GitHub for updates and replace a writable native installation.
System packages should be updated through the package manager/installer.
Release candidates are not offered by the stable update check.
