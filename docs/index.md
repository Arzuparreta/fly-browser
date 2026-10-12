---
layout: home

hero:
  name: Mise Browser
  text: Keyboard-First, Isolated Web Browser
  tagline: Engineered for fanless Linux and focused workflows. True tab hibernation, side-by-side dual split view, isolated multi-account containers, and zero mouse dependency.
  image:
    src: /logo.png
    alt: Mise Browser Logo
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
      link: https://github.com/Rakosn1cek/Mise-browser

features:
  - title: True Tab Hibernation
    details: Inactive background tabs detach entirely from memory across all workspaces. Live scroll positions and navigation history restore smoothly upon selection.
  - title: Dual-Split View
    details: Browse two pages side-by-side with customisable partition ratios. Swap panes, open links directly into split view, or toggle seamlessly via keyboard shortcuts.
  - title: Multi-Account Containers
    details: Every workspace runs inside an isolated persistent Chromium partition. Run multiple corporate and personal logins simultaneously with zero cookie cross-contamination.
  - title: Mouse-Free Navigation
    details: Full Vim-inspired link hints (f), quickmark speed-dial (go), floating omnibar, and granular shortcut remapping in keybinds.json.
  - title: Native Privacy Shields
    details: Built-in Ghostery request filtering stops trackers, cryptominers, and analytics at the network layer. Trusted domains bypass fingerprint farbling on banking portals.
  - title: Clipboard-Mediated Oversight
    details: Web commands and script executions stage safely onto your system clipboard. External terminal scanning tools like Oversight can inspect actions before execution.
  - title: User Scripts & Styles Engine
    details: Native injection of local .user.js and .user.css files without extension overhead. Automatic file watching and live hot-reloading keep workflows fluid.
  - title: Markdown Scratchpad & Tools
    details: Instant Markdown quick notes (Alt + N), actionable history overlay (Ctrl + Shift + H), and structured bookmarks built right into the interface.
  - title: Distro-Agnostic & Zero Telemetry
    details: Distro-agnostic AppImage, Deb, RPM, and Arch packaging. Zero telemetry, zero usage tracking, and anonymous GitHub release notifications for effortless updates.
---

![Mise Browser Dual-Split View](/screenshots/split-view.webp)

## Why Mise Browser?

Most modern web browsers consume gigabytes of memory, leak telemetry continuously, and expect a pointing device for every interaction. Named after the Czech word for *"mission"* (pronounced *mee-seh*), **Mise** takes a fundamentally different path: it treats web navigation as an intentional, keyboard-centric, distraction-free environment designed to respect both your hardware and your privacy.

::: tip ARCHITECTURAL PILLARS
* **Memory Conservation:** Idle tabs detach from DOM memory. Twenty tabs take the RAM footprint of two.
* **Complete Isolation:** Separate workspace containers keep cookies, caches, and logins entirely segregated.
* **Crash Resilience:** Atomic file writes, rolling backup journals, and safe startup hibernation prevent data loss and crash loops.
* **Transparent Storage:** Configuration, notes, history, and keybinds live in human-readable files under `~/.config/mise-browser/`.
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
| `Alt + N` | Scratchpad | Open Markdown quick-notes scratchpad |
| `Ctrl + Shift + P` | Private Window | Open zero-trace volatile browsing container |
| `F1` / `Ctrl + H` | Preferences | Adjust hardware flags, sleep timeouts, and site shields |

---

## Quick Start on Linux

Download the self-contained Linux AppImage from GitHub Releases and run directly on any distribution:

```bash
# Make executable and launch
chmod +x Mise-*-x86_64.AppImage
./Mise-*-x86_64.AppImage
```

You can also compile directly from source:

```bash
# Clone and run from source
git clone https://github.com/Rakosn1cek/Mise-browser.git
cd Mise-browser
npm install
npm start
```

---

## Built with Modern Open-Source Foundations

Mise combines the battle-tested rendering speed of Chromium and Electron with custom modular subsystems written in vanilla JavaScript:

* **Ghostery Network Engine:** Local request filtering without third-party extension overhead.
* **safeStorage Encryption:** Linux `libsecret`, macOS Keychain, and Windows DPAPI hardware-backed encryption for persistent cookies.
* **inotify Live Reloading:** Instant injection and live reloading of local `.user.js` and `.user.css` user customisations.
* **Atomic Session Recovery:** Atomic POSIX file swaps and rolling backup journals protect session state against sudden kernel panics or power outages.
* **Debounced Persistence:** Asynchronous disk flushing to prevent main-process stutter on single-page web applications.

---

## Community & Fediverse

* [Reddit (r/mise_browser)](https://www.reddit.com/r/mise_browser/)
* <a rel="me" href="https://mastodon.social/@misebrowser">Mastodon (@misebrowser@mastodon.social)</a>
* [GitHub Discussions & Source](https://github.com/Rakosn1cek/Mise-browser)
