---
layout: home

hero:
  name: Fly Browser
  text: Small, Minimal Linux Browser
  tagline: Full keyboard control of the browser with a polished experience for mouse users. State-preserving tab suspension, side-by-side split view and isolated multi-account containers.
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
  - title: Tab Suspension
    details: Eligible background pages pause in memory and resume without reloading, preserving their document and JavaScript state.
  - title: Dual-Split View
    details: Browse two pages side-by-side with customisable partition ratios. Swap panes, open links directly into split view, or toggle seamlessly via keyboard shortcuts.
  - title: Multi-Account Containers
    details: Every workspace runs inside an isolated Chromium profile. Run multiple corporate and personal logins simultaneously with zero cookie cross-contamination.
  - title: Mouse-Free Navigation
    details: Full Vim-inspired link hints (f), floating omnibar, and browser and page-key customisation in Preferences.
  - title: uBlock Origin Lite
    details: Included ad and tracker blocking for persistent workspaces, with filter lists and site exceptions managed in uBlock Origin Lite.
  - title: Clipboard-Mediated Oversight
    details: Web commands and script executions stage safely onto your system clipboard. External terminal scanning tools like Oversight can inspect actions before execution.
  - title: User Scripts & Styles Engine
    details: Local .user.js and .user.css files loaded through a generated component extension. Automatic file watching and live hot-reloading keep workflows fluid.
  - title: History & Bookmarks
    details: Actionable history overlay (Ctrl + Shift + H), structured bookmarks built right into the interface.
  - title: Distro-Agnostic & Zero Telemetry
    details: Native Linux x64 tarballs and an Arch package. Zero telemetry, zero usage tracking, and anonymous GitHub release notifications for effortless updates.
---

![Fly Browser New Tab](/screenshots/new-tab.webp)

## Why Fly Browser?

Most modern web browsers consume gigabytes of memory and leak telemetry continuously. **Fly** takes a smaller path: an intentional, distraction-free browser that respects your hardware and privacy, where every action works from the keyboard and pointer use remains comfortable and polished.

::: tip ARCHITECTURAL PILLARS
* **Memory Conservation:** Eligible idle tabs pause in memory and resume their existing document.
* **Complete Isolation:** Separate workspace containers keep cookies, caches, and logins entirely segregated.
* **Crash Resilience:** Atomic file writes, rolling backup journals, and on-demand session restoration prevent data loss and crash loops.
* **Transparent Storage:** Configuration, history, and keybinds live in human-readable files under `~/.config/fly-browser-native/`.
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
| `Ctrl + H` | Preferences | Adjust hardware flags, suspension timeouts, and site permissions |

---

## Quick Start on Linux

Paste this into a terminal:

```bash
mkdir -p ~/.local/bin
curl -fsSL https://raw.githubusercontent.com/Arzuparreta/fly-browser/main/scripts/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install
```

On Arch-based systems it installs `fly-browser-bin`; elsewhere it installs the
native Chromium Linux x64 tarball. Both add Fly to the applications menu.
`fly-install update` keeps it current. Close Electron before the first native
start; its profile is retained and supported data is imported separately.
See [Installation](/guide/installation) for migration limits and uninstall options.

Building from source requires the pinned external Chromium checkout and native
overlay, not just `npm install`. Follow [Chromium development](/chromium/build),
then run `FLY_ENGINE_ROOT=/path/to/fly-chromium npm start`.

## Built on Chromium

Fly uses Chromium's full browser layer, native Views controls, separate workspace
profiles and narrowly scoped internal WebUI. uBlock Origin Lite provides native
rule-based blocking. User content is loaded through a generated component
extension. [Electron legacy](https://github.com/Arzuparreta/fly-browser/tree/main/legacy/electron)
remains available for comparison, but is no longer the default release.

## Source & Issues

* [Source repository](https://github.com/Arzuparreta/fly-browser)
* [Issues](https://github.com/Arzuparreta/fly-browser/issues)
* Independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser); it does not track that repository.
