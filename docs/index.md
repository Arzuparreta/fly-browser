---
layout: home

hero:
  name: Fly Browser
  text: Small, Minimal Linux Browser
  tagline: Full keyboard control of the browser with a polished experience for mouse users. True tab hibernation, side-by-side split view and isolated multi-account containers.
  image:
    src: /logo.png
    alt: Fly Browser Logo
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: Download Release
      link: /guide/installation
    - theme: alt
      text: Keyboard Shortcuts
      link: /reference/keybinds
    - theme: alt
      text: View on GitHub
      link: https://github.com/Arzuparreta/fly-browser

features:
  - title: True Tab Hibernation
    details: Inactive background tabs detach entirely from memory across all workspaces. Live scroll positions and navigation history restore smoothly upon selection.
  - title: Dual-Split View
    details: Browse two pages side-by-side with customisable partition ratios. Swap panes, open links directly into split view, or toggle seamlessly via keyboard shortcuts.
  - title: Multi-Account Containers
    details: Every workspace runs inside an isolated persistent Chromium partition. Run multiple corporate and personal logins simultaneously with zero cookie cross-contamination.
  - title: Mouse-Free Navigation
    details: Full Vim-inspired link hints (f), customisable quickmark actions, floating omnibar, and browser and page-key customisation in Preferences.
  - title: Native Privacy Shields
    details: Built-in Ghostery request filtering stops trackers, cryptominers, and analytics at the network layer. Trusted domains bypass fingerprint farbling on banking portals.
  - title: Clipboard-Mediated Oversight
    details: Web commands and script executions stage safely onto your system clipboard. External terminal scanning tools like Oversight can inspect actions before execution.
  - title: User Scripts & Styles Engine
    details: Native injection of local .user.js and .user.css files without extension overhead. Automatic file watching and live hot-reloading keep workflows fluid.
  - title: History & Bookmarks
    details: Actionable history overlay (Ctrl + Shift + H), structured bookmarks and single-key quickmarks built right into the interface.
  - title: Distro-Agnostic & Zero Telemetry
    details: An Arch package and a distro-agnostic AppImage for everything else. Zero telemetry, zero usage tracking, and anonymous GitHub release notifications for effortless updates.
---

![Fly Browser Dual-Split View](/screenshots/split-view.webp)

## Why Fly Browser?

Most modern web browsers consume gigabytes of memory and leak telemetry continuously. **Fly** takes a smaller path: an intentional, distraction-free browser that respects your hardware and privacy, where every action works from the keyboard and pointer use remains comfortable and polished.

::: tip ARCHITECTURAL PILLARS
* **Memory Conservation:** Idle tabs detach from DOM memory. Twenty tabs take the RAM footprint of two.
* **Complete Isolation:** Separate workspace containers keep cookies, caches, and logins entirely segregated.
* **Crash Resilience:** Atomic file writes, rolling backup journals, and safe startup hibernation prevent data loss and crash loops.
* **Transparent Storage:** Configuration, history, and keybinds live in human-readable files under `~/.config/fly-browser/`.
* **Zero Cloud Dependence:** No remote telemetry, no forced account sync, and zero phone-home profiling.
:::

---

## Fast-Track Navigation

Keep your hands on the home row. Every major workflow is reachable via concise, mnemonic keybindings:

| Shortcut | Command | Action |
| :--- | :--- | :--- |
| `f` | Link Hints | Highlight all clickable page links with two-letter tags |
| `Alt + S` | Dual Split | Toggle side-by-side split screen view |
| `Alt + \` | Swap Panes | Swap active page and split page positions |
| `Ctrl + 1` to `9` | Workspace Jump | Switch between isolated multi-account containers |
| `Ctrl + L` / `o` | Omnibar | Open floating address bar with search engine aliases |
| `Ctrl + Shift + H` | History Overlay | Search, reopen, or purge visited history records |
| `Ctrl + Shift + P` | Private Window | Open zero-trace volatile browsing container |
| `Ctrl + H` | Preferences | Adjust hardware flags, sleep timeouts, and site shields |

---

## Quick Start on Linux

Paste this into a terminal:

```bash
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

On Arch-based systems (CachyOS, EndeavourOS, Manjaro) it installs the `fly-browser-bin` package; anywhere else, the AppImage. Either way Fly lands in your applications menu, Rofi and other launchers. `fly-install update` keeps it current and `fly-install uninstall` removes it. See [Installation](/guide/installation) for every option.

Or download the self-contained AppImage from GitHub Releases yourself and run it:

```bash
# Make executable and launch
chmod +x Fly-*-x86_64.AppImage
./Fly-*-x86_64.AppImage
```

You can also compile directly from source:

```bash
# Clone and run from source
git clone https://github.com/Arzuparreta/fly-browser.git
cd fly-browser
npm install
npm start
```

---

## Built with Modern Open-Source Foundations

Fly combines the battle-tested rendering speed of Chromium and Electron with custom modular subsystems written in vanilla JavaScript:

* **Ghostery Network Engine:** Local request filtering without third-party extension overhead.
* **safeStorage Encryption:** Linux `libsecret`, macOS Keychain, and Windows DPAPI hardware-backed encryption for persistent cookies.
* **inotify Live Reloading:** Instant injection and live reloading of local `.user.js` and `.user.css` user customisations.
* **Atomic Session Recovery:** Atomic POSIX file swaps and rolling backup journals protect session state against sudden kernel panics or power outages.
* **Debounced Persistence:** Asynchronous disk flushing to prevent main-process stutter on single-page web applications.

---

## Source & Issues

* [Source repository](https://github.com/Arzuparreta/fly-browser)
* [Issues](https://github.com/Arzuparreta/fly-browser/issues)
* Independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser); it does not track that repository.
