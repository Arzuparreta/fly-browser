# Fly Browser

> A small, minimal alternative to the typical Linux browsers: full keyboard control of the browser, with a polished experience for mouse users.

[![Latest Release](https://img.shields.io/github/v/release/Arzuparreta/fly-browser?logo=github&label=release)](https://github.com/Arzuparreta/fly-browser/releases/latest)
[![Documentation](https://img.shields.io/badge/docs-GitHub_Pages-blue.svg)](https://arzuparreta.github.io/fly-browser/)
[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)

Fly wraps the Chromium engine in a lean Electron shell. Every browser action can be driven from the keyboard, and the interface is being refined so that pointer users get an equally polished experience. It stays small: low memory use, isolated workspaces and no background services.

📖 **Official Documentation**: [https://arzuparreta.github.io/fly-browser/](https://arzuparreta.github.io/fly-browser/)

📦 **Packages**: [Releases](https://github.com/Arzuparreta/fly-browser/releases) • [Running from source](#quick-start-running-from-source)

![Fly Browser Dual-Split View](docs/public/screenshots/split-view.webp)

***

## Key Highlights

* **True Tab Hibernation**: Detaches idle `<webview>` tags completely from the DOM across all workspaces, terminating idle Chromium renderer processes and reducing RAM usage by 60% or more. Live URLs, page titles, and scroll offsets are restored on demand.
* **Auto-Collapsing Vertical Strip**: The vertical sidebar collapses into a 36px icon strip displaying workspace badges and tab favicons, expanding smoothly as an overlay on hover or via shortcut without triggering webview layout shifts.
* **Multi-Account Container Partitions**: Each workspace runs in its own isolated Electron persistent partition (`persist:work`, `persist:personal`), preventing cookie cross-contamination across accounts.
* **Mouse-Free Navigation**: Complete keyboard control via Link Hints, floating address bar with search aliases, and customisable browser shortcuts and Vim-style page keys in **Preferences → Keyboard** (new-profile default: **Ctrl+,**).
* **Clipboard-Mediated Terminal Oversight**: High-risk script execution and raw web commands trigger a clipboard-mediated security handshake, sanitising and staging vetted commands to your clipboard for deliberate user review and execution in an external terminal.
* **Native Privacy & Trusted Sites**: Built-in request interception blocks trackers, telemetry, and advertisements without heavy third-party extensions, while allowing selective whitelisting for trusted banking and shopping services.
* **Local User Scripts & Styles**: Direct native injection of `.user.js` and `.user.css` files directly from `~/.config/fly-browser/` with inotify watching and live CSS hot-reloading, avoiding the heavy memory overhead of full WebExtensions.
* **Power Tools**: Integrated Markdown notes, bookmarks, quickmarks, actionable history overlay, and a full-page Command Centre.

***

## Security & Release Cadence

Security, isolation, and upstream dependency freshness are foundational principles for Fly:

* **Twice-Weekly Upstream Tracking**: Fly monitors upstream Electron and Chromium security releases twice weekly. Patches are evaluated, verified, and shipped promptly.
* **Engine Version Verification**: Active Fly, Electron, and Chromium runtime versions are visible in **Preferences → About** (`Ctrl + H`) and the Welcome view.
* **Strict Process Sandboxing**: Webviews run with `contextIsolation: true`, `nodeIntegration: false`, and Chromium sandboxing enabled, guarded by a minimal IPC whitelist.
* **Permissions Denied by Default**: Hardware access (camera, microphone, geolocation, and MIDI) is blocked by default.
* **Local Data Ownership & Zero Password Vault Targets**: Plaintext JSON configuration, notes, and history files are stored locally under user-only permissions (`0600`). Fly deliberately excludes built-in password managers, eliminating browser credential harvesting by infostealer malware. Session cookies are encrypted via host OS keyrings.
* **Responsible Disclosure**: See the [Security Policy](SECURITY.md) for vulnerability disclosure guidelines via GitHub Security Advisories.

***

## Downloads

No binary packages have been published from this repository yet. Run Fly from source (below). Pushing a version tag runs the release workflow, which publishes Linux, macOS and Windows packages with SHA-256 checksums on the [Releases page](https://github.com/Arzuparreta/fly-browser/releases).

For platform notes, see the [Installation Guide](https://arzuparreta.github.io/fly-browser/guide/installation).

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

# Install dependencies
npm install

# Start the browser
npm start
```

On Linux systems, you can also launch directly using the helper script:
```bash
./launch.sh
```

***

## Documentation

Comprehensive user guides and configuration references are hosted on the GitHub Pages site:

* [Getting Started & Philosophy](https://arzuparreta.github.io/fly-browser/guide/getting-started)
* [Mouse-Free Navigation & Link Hints](https://arzuparreta.github.io/fly-browser/guide/navigation)
* [Workspaces & Multi-Account Containers](https://arzuparreta.github.io/fly-browser/guide/workspaces-and-containers)
* [True Tab Hibernation Guide](https://arzuparreta.github.io/fly-browser/guide/tab-hibernation)
* [Local User Scripts & Styles Guide](https://arzuparreta.github.io/fly-browser/guide/user-scripts-and-styles)
* [Terminal Security & Oversight Scanner](https://arzuparreta.github.io/fly-browser/guide/terminal-oversight)
* [Keyboard Shortcuts Reference](https://arzuparreta.github.io/fly-browser/reference/keybinds)
* [Configuration Files Reference](https://arzuparreta.github.io/fly-browser/reference/configuration)

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

- [Keyboard coverage review](docs/keyboard-review.md)
- [Fork fixes ledger](docs/fork-fixes.md)

Every PR records the problems it solves, including defects inherited from the original Mise code.
