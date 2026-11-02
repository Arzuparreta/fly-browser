# Fly Browser

Fly is a Linux browser built on Chromium, with its own interface in Chromium's
Views toolkit: a compact vertical sidebar, keyboard navigation, split view and
workspaces with separate Chromium profiles.

[Documentation](docs/guide/getting-started.md) ·
[Releases](https://github.com/Arzuparreta/fly-browser/releases) ·
[Contributing](CONTRIBUTING.md)

![Fly Browser](docs/public/screenshots/new-tab.webp)

## Install

```sh
mkdir -p ~/.local/bin
curl -fsSL https://arzuparreta.github.io/fly-browser.github.io/packages/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install --package
```

With `curl` and `gpgv`/GnuPG installed, `fly-install` verifies Fly's release
signature and installs the official DEB, RPM or pacman package and its signed
repository. Update with your usual package manager. Use `install --native` for
a user installation without root, `status` to inspect it, or `uninstall` to
remove it while keeping your profile. See [installation](docs/guide/installation.md).

Releases are built for Linux x86_64 only.

## Features

- Keyboard commands, link hints, floating address bar and editable shortcuts.
- Isolated workspace profiles, each in its own window; split view and private browsing.
- Bookmarks, shared history, frequent sites and state-preserving tab suspension.
- Chrome Web Store extensions and bundled uBlock Origin Lite.
- Local user scripts and styles through a generated component extension.
- Local data, no Fly telemetry, update notifications and in-place updates.

## Development

`./launch.sh` (or `npm start`) starts Fly with a separate development profile on
any Linux x64 computer. Without a local engine build it downloads the published
release; `./launch.sh --pr 153` downloads the test build of pull request #153
([test builds](docs/releasing.md#test-builds-of-pull-requests)). Downloads are
checked against their SHA-256 and kept in `~/.cache/fly-browser-launch`.

To run your own changes, build the pinned engine and overlay using
[Chromium development](docs/chromium/build.md). The launcher then starts that
build:

```sh
FLY_ENGINE_ROOT=/path/to/fly-chromium npm start
```

`npm ci` installs the tooling; it does not download or build Chromium.
`npm test` checks the generated files, the version copies, the release tooling
and the launcher and installer scripts; engine tooling and browser tests are
separate explicit commands. See [releasing](docs/releasing.md) for the build
host and packaging.

## Project

Fly is the renamed, independent continuation of Mise. It does not track or
contribute to its former upstream. Original copyright notices are retained.
Licensed under [GPL-3.0-or-later](LICENSE).

[Security](SECURITY.md) · [Privacy](PRIVACY.md)
