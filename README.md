# Fly Browser

> A small, minimal alternative to the typical Linux browsers: full keyboard control of the browser, with a polished experience for mouse users.

[![Latest Release](https://img.shields.io/github/v/release/Arzuparreta/fly-browser?logo=github&label=release)](https://github.com/Arzuparreta/fly-browser/releases/latest)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Fly wraps the Chromium engine in a lean Electron shell. Every browser action can be driven from the keyboard, and the interface is being refined so that pointer users get an equally polished experience. It stays small: low memory use, isolated workspaces and no background services.

📖 **Documentation**: [`docs/`](docs/guide/getting-started.md)

📦 **Packages**: [Releases](https://github.com/Arzuparreta/fly-browser/releases) • [Running from source](#quick-start-running-from-source)

![Fly Browser Dual-Split View](docs/public/screenshots/split-view.webp)

***

## Key Highlights

* **Tab Suspension**: Pauses eligible background pages in memory to save CPU and battery. Selecting a tab resumes the same document without reloading or discarding its page state.
* **Auto-Collapsing Vertical Strip**: The vertical sidebar collapses into a 36px icon strip displaying workspace badges and tab favicons, expanding smoothly as an overlay on hover or via shortcut without triggering webview layout shifts.
* **Multi-Account Container Partitions**: Each workspace runs in its own isolated Electron persistent partition (`persist:work`, `persist:personal`), preventing cookie cross-contamination across accounts.
* **Mouse-Free Navigation**: Complete keyboard control via Link Hints, floating address bar with search aliases, and customisable browser shortcuts and Vim-style page keys in **Preferences → Keyboard** (new-profile default: **Ctrl+,**).
* **Clipboard-Mediated Terminal Oversight**: High-risk script execution and raw web commands trigger a clipboard-mediated security handshake, sanitising and staging vetted commands to your clipboard for deliberate user review and execution in an external terminal.
* **uBlock Origin**: Included ad and tracker blocking, with filter lists and site exceptions managed in uBlock. Workspaces keep separate cookies and extension settings.
* **Local User Scripts & Styles**: Direct native injection of `.user.js` and `.user.css` files directly from `~/.config/fly-browser/` with inotify watching and live CSS hot-reloading, avoiding the heavy memory overhead of full WebExtensions.
* **Power Tools**: Integrated bookmarks, quickmarks, actionable history overlay, and a full-page Command Centre.

***

## Security & Release Cadence

Security, isolation, and upstream dependency freshness are foundational principles for Fly:

* **Versions that decide themselves**: Every merged pull request declares its impact, and the next version number is derived from those declarations. See [Versioning and releases](docs/releasing.md).
* **Engine Version Verification**: Active Fly, Electron, and Chromium runtime versions are visible in **Preferences → About** (`Ctrl + H`) and the Welcome view.
* **Strict Process Sandboxing**: Webviews run with `contextIsolation: true`, `nodeIntegration: false`, and Chromium sandboxing enabled, guarded by a minimal IPC whitelist.
* **Permissions Denied by Default**: Hardware access (camera, microphone, geolocation, and MIDI) is blocked by default.
* **Local Data Ownership & Zero Password Vault Targets**: Plaintext JSON configuration and history files are stored locally under user-only permissions (`0600`). Fly deliberately excludes built-in password managers, eliminating browser credential harvesting by infostealer malware. Session cookies are encrypted via host OS keyrings.
* **Responsible Disclosure**: See the [Security Policy](SECURITY.md) for vulnerability disclosure guidelines via GitHub Security Advisories.

***

## Install

On Linux, paste this into a terminal:

```bash
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

On Arch-based systems (CachyOS, EndeavourOS, Manjaro) it builds and installs the `fly-browser-bin` package, which asks for your password; anywhere else it installs the AppImage. Either way Fly appears in your applications menu, Rofi and other launchers. Later, `fly-install update` updates it, `fly-install status` lists every copy on the machine and `fly-install uninstall` removes it (see the [installation guide](docs/guide/installation.md#installer-script-any-distribution)).

`fly-browser-bin` is going to the AUR, where it will update with the rest of the system, as soon as AUR registration reopens.

Every release also publishes Linux (AppImage, tar.gz), macOS and Windows packages with SHA-256 checksums on the [Releases page](https://github.com/Arzuparreta/fly-browser/releases).

For platform notes, see the [Installation Guide](docs/guide/installation.md).

***

## Quick Start (Running from Source)

For developers and contributors running Fly directly from the repository:

### Prerequisites
* Linux (recommended), macOS, or Windows
* `Node.js` (v20+ recommended) and `npm`
* A terminal emulator (e.g. Kitty, Alacritty, Foot, st, xterm)

### Clone & Launch

```bash
# Clone the repository
git clone git@github.com:Arzuparreta/fly-browser.git
cd fly-browser

# Start the browser
./launch.sh
```

`launch.sh` installs the dependencies and the Electron runtime on the first run, and again whenever a pull changes them; later starts are immediate. The launcher stays attached to the terminal so diagnostics and the exit status remain visible. Alternatively, run `npm install` followed by `npm start`.

***

## Documentation

User guides and configuration references live in [`docs/`](docs/):

* [Getting Started & Philosophy](docs/guide/getting-started.md)
* [Mouse-Free Navigation & Link Hints](docs/guide/navigation.md)
* [Workspaces & Multi-Account Containers](docs/guide/workspaces-and-containers.md)
* [Tab Suspension Guide](docs/guide/tab-hibernation.md)
* [Local User Scripts & Styles Guide](docs/guide/user-scripts-and-styles.md)
* [Terminal Security & Oversight Scanner](docs/guide/terminal-oversight.md)
* [Keyboard Shortcuts Reference](docs/reference/keybinds.md)
* [Configuration Files Reference](docs/reference/configuration.md)

***

## Standalone Oversight Security Tool (Optional)

Fly never executes shell commands directly or automatically in the background. When terminal operations are triggered, commands are copied to the system clipboard and an empty, unmanaged terminal emulator is spawned.

Active command and script scanning is handled exclusively by [Oversight](https://github.com/Rakosn1cek/oversight), an independent external Rust-based terminal security intelligence scanner and interactive pager:

* **With Oversight Installed**: High-risk scripts and commands are audited for malicious patterns, Shannon entropy obfuscation, and CVE vulnerabilities prior to execution.
* **Without Oversight (Default)**: Oversight is completely optional and unbundled. Without it, commands copied to your clipboard are pasted and run entirely at your own risk without automated checks.

***

## Project Policies

* [Privacy Policy](PRIVACY.md) (Zero telemetry, container isolation, and local data ownership)
* [Security Policy](SECURITY.md) (Process sandboxing, threat model, and responsible disclosure)

***

## Source & Issues

* [Source repository](https://github.com/Arzuparreta/fly-browser)
* [Issues](https://github.com/Arzuparreta/fly-browser/issues)

***

## Origin

Fly is the renamed, independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser), developed separately since upstream commit `3be7f96` (0.15.0). Existing Mise profiles in `~/.config/mise-browser` move to `~/.config/fly-browser` on first launch. It does not track or depend on that repository. Original copyright notices and the GPL-3.0-or-later licence are retained. The optional [Oversight](https://github.com/Rakosn1cek/oversight) scanner and the community scripts collection remain separate external projects.

***

## Licence

This project is licensed under the terms of the GNU General Public Licence v3.0 (or any later version). See the [LICENSE](LICENSE) file for details.

## Engineering records

- [Versioning and releases](docs/releasing.md)
- [Contributing](CONTRIBUTING.md)
- [Keyboard coverage review](docs/keyboard-review.md)
- [Fork fixes ledger](docs/fork-fixes.md) (closed; records the work done before releases began)
