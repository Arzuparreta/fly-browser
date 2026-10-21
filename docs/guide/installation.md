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

The user installation lives in `$XDG_DATA_HOME/fly-browser` (default
`~/.local/share/fly-browser`). Start `~/.local/bin/fly-browser`, or choose **Fly** in your launcher. Add
`~/.local/bin` to `PATH` if needed. Uninstall removes the Arch package and user installation, and keeps profiles by default.
`--purge` also deletes the Fly profile and launcher flags
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
archive with `sha256sum`, extract it, enter `fly-browser/` and run
`./install.sh`. Run `./install.sh --remove` from the extracted bundle to remove
the user installation. Linux must support Chromium's sandbox and runtime
libraries; no launcher disables the sandbox.

## Clean installation

Chromium Fly uses `~/.config/fly-browser` (or `$XDG_CONFIG_HOME/fly-browser`).
This release uses the canonical Fly paths and does not migrate the earlier
`fly-browser-native` profile. For a clean start, uninstall and reinstall Fly.
Use `fly-install uninstall --purge --yes` to delete the Fly profile as well.
If this directory contains an old Electron profile, back it up or purge it
before the first Chromium start. There is no automatic profile conversion.

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
