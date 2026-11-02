# Installation

Fly provides its own signed Linux x86_64 packages. Packages are built from one
native Chromium payload and published directly by Fly, without a third-party
store or AUR. Other architectures and musl-based systems are not supported.

## Recommended: your system package manager

Install `curl` and `gpgv` (or GnuPG) using your distribution's package manager,
then download the official installer, inspect it if desired, and run:

```sh
curl -fsSL https://arzuparreta.github.io/fly-browser.github.io/packages/fly-install -o fly-install
bash fly-install install --package
```

The installer verifies the release signature using its embedded public key and
checks the package hash before requesting administrator privileges. It selects
DEB for APT systems, RPM for DNF/Zypper, or a binary pacman package for Arch.
It uses your package manager to install dependencies and the browser.

DEB and RPM install Fly's repository configuration automatically. On Arch the
installer imports the pinned key, locally trusts it and registers Fly's signed
repository in pacman. Afterwards update normally:

| Family | Update |
| --- | --- |
| Debian, Ubuntu and derivatives | `sudo apt update && sudo apt upgrade` |
| Fedora | `sudo dnf upgrade` |
| openSUSE Tumbleweed | `sudo zypper dup` |
| Arch and derivatives | `sudo pacman -Syu` |

The package is `fly-browser-bin`, the command is `fly-browser`, and the menu
entry is **Fly**. System installation lives in `/opt/fly-browser`. The browser
does not replace files owned by your package manager. You can also download
the native package and its signature from [Releases](https://github.com/Arzuparreta/fly-browser/releases).

## Install for your user

For a conventional Linux system without administrator access, use the same
installer with `--native`. It verifies and installs the tarball instead:

```sh
bash fly-install install --native
bash fly-install update
bash fly-install status
```

The installation lives in `$XDG_DATA_HOME/fly-browser` (default
`~/.local/share/fly-browser`), with a launcher in `~/.local/bin/fly-browser`.
Preferences can install signed updates for this installation. Updates preserve
a previous installation for recovery and retain your profile.

This route still needs Chromium's system runtime libraries, `gpgv`, `flock`
(util-linux) and a working Chromium sandbox. AppImage would not remove all of
these constraints. Fly never starts with `--no-sandbox`.

Immutable systems such as Silverblue/Bazzite and NixOS need separate acceptance;
a user tarball is not a promise of compatibility there. A self-hosted Flatpak
channel is a possible later addition, not an available installation today.

## Trust and repositories

The public key and fingerprint are published on the
[official packages page](https://arzuparreta.github.io/fly-browser.github.io/packages/).
APT and pacman metadata/package files are hosted on the project's
`linux-repository` GitHub prerelease; RPM is a conventional repository on GitHub Pages, built from verified packages
on Releases without storing binaries in Git. This infrastructure prerelease is not a browser
version and is never offered as an update.

For an already installed older user copy, remove it before switching to a
system package, so a stale user launcher cannot hide the system installation:

```sh
bash fly-install uninstall native --yes
bash fly-install install --package
```

The profile is preserved. Package-manager installations and direct installations
have different update owners; `fly-install update` delegates to your package
manager when it detects an installed system package.

## Remove Fly

```sh
bash fly-install uninstall --yes
# Also permanently delete your profile, history, passwords and settings:
bash fly-install uninstall --purge --yes
```

You can instead remove `fly-browser-bin` using your package manager. Removal
keeps `~/.config/fly-browser` (or `$XDG_CONFIG_HOME/fly-browser`) unless you
explicitly request `--purge` from the installer.

## Running from source

Follow [Chromium development](/chromium/build), then run
`FLY_ENGINE_ROOT=/path/to/fly-chromium npm start`. Building Chromium is explicit;
launching never builds an engine. `./launch.sh --release` verifies the stable
release signature before running a downloaded build with a development profile.
Pull-request test builds are explicitly selected development artifacts, checked
by hash, and are not the signed stable distribution channel.
