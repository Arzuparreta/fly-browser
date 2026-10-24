# Installation

Fly is built on Chromium. Release packages support Linux x86_64.

## Installer script (any distribution)

```sh
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

The installer downloads `Fly-<version>-native-linux-x64.tar.gz`, verifies it
against the release's `SHA256SUMS.txt`, and runs its `install.sh`. If the latest
release has no such package, installation stops and changes nothing.

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

On Arch-based systems with `pacman` and `makepkg`, installation uses the
release's `PKGBUILD` and installs `fly-browser-bin`. You can request it with
`fly-install install --package`; `--native` selects the user tarball when no
system package is installed. A system package is updated through `makepkg`,
not replaced with a second user installation. Remove it with
`sudo pacman -R fly-browser-bin`.

## Manual tarball

Download the Linux tarball and `SHA256SUMS.txt` from
[Releases](https://github.com/Arzuparreta/fly-browser/releases), verify the
archive with `sha256sum`, extract it, enter `fly-browser/` and run
`./install.sh`. Run `./install.sh --remove` from the extracted bundle to remove
the user installation. Linux must support Chromium's sandbox and runtime
libraries; no launcher disables the sandbox.

## Profile

Fly keeps its data in `~/.config/fly-browser` (or `$XDG_CONFIG_HOME/fly-browser`).
For a clean start, uninstall and reinstall Fly; use
`fly-install uninstall --purge --yes` to delete the profile as well.

## Running from source

Follow [Chromium development](/chromium/build) to prepare and build the pinned
engine. Then run `FLY_ENGINE_ROOT=/path/to/fly-chromium npm start` from the repo.
Set `FLY_ENGINE_OUTPUT=out/FlyNative` to run the development build. Building
Chromium is explicit and requires substantial disk, memory and time; launching
never builds an engine.

## Updates

`fly-install update` installs the latest stable package. Preferences can
also check GitHub for updates and replace a writable installation.
System packages should be updated through the package manager/installer.
Release candidates are not offered by the stable update check.
