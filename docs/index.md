---
layout: home

hero:
  name: Fly Browser
  text: A fast Chromium you drive from the keyboard
  tagline: Vertical tabs, Vim-style page keys, link hints, workspaces in separate profiles and a status bar that reads like i3bar. For Linux.
  image:
    src: /logo.png
    alt: Fly Browser logo
  actions:
    - theme: brand
      text: Get started
      link: /guide/getting-started
    - theme: alt
      text: Install
      link: /guide/installation
    - theme: alt
      text: Keyboard
      link: /guide/navigation
    - theme: alt
      text: GitHub
      link: https://github.com/Arzuparreta/fly-browser

features:
  - title: Keyboard first
    details: Single keys scroll, follow links, switch tabs and open every panel. Every binding can be changed, and the status bar always shows the mode you are in.
  - title: Link hints
    details: Press f and type one or two letters to click anything on screen. F opens links in background tabs; g i jumps to the first text field.
  - title: Workspaces
    details: Each workspace is a separate Chromium profile in its own window, with its own cookies, logins and extensions. Ctrl+1 to Ctrl+9 switch between them.
  - title: Vertical tabs
    details: A 36 px strip of icons that opens over the page, or a pinned list. Alt+1 to Alt+9 select tabs; g a returns to the previous one.
  - title: Split view
    details: Two tabs side by side or stacked in one window, with keys to change focus and swap them.
  - title: Tab suspension
    details: Idle background pages are paused in memory and resume where they were, without reloading.
  - title: Address bar
    details: Addresses, search aliases, DuckDuckGo bangs, a calculator, your open tabs, bookmarks and history in one floating bar.
  - title: uBlock Origin Lite
    details: Bundled and on by default in every workspace. Chrome Web Store extensions install as in Chromium.
  - title: Local by default
    details: No Fly telemetry and no account. Settings, bookmarks and history stay under ~/.config/fly-browser.
---

![Fly Browser new tab](/screenshots/new-tab.webp)

## What Fly is

Fly is the full Chromium browser with its own interface, written in Chromium's native toolkit: a sidebar of vertical tabs, a status bar and floating panels in place of the tab strip and toolbar. Pages render and behave exactly as in Chromium, and Chrome Web Store extensions work.

Its design follows three ideas:

- **The keyboard reaches everything.** Browser shortcuts work everywhere; single page keys work whenever you are not typing. The mouse works everywhere too.
- **The interface stays out of the way.** The page gets the whole window except a thin strip of tab icons and one status line.
- **It runs well on modest hardware.** The interface animates only on the compositor, background tabs are suspended, and Chromium's field-trial experiments are off.

## The keys to learn first

| Key | What it does |
| :--- | :--- |
| `j` `k` `d` `u` | Scroll |
| `f` | Link hints: type the label of what you want to click |
| `o` / `Ctrl + L` | Address bar |
| `t` / `x` / `X` | New tab / close tab / reopen the closed tab |
| `J` `K` | Next / previous tab |
| `H` `L` | Back / forward |
| `/` then `n` `N` | Find in the page, next and previous match |
| `Ctrl + 1` … `9` | Go to a workspace |
| `Ctrl + P` | Command Centre: find any command and its key |
| `?` | Every shortcut, and where to change it |

The complete list is in [Keyboard navigation](/guide/navigation).

## Install

```bash
mkdir -p ~/.local/bin
curl -fsSL https://arzuparreta.github.io/fly-browser.github.io/packages/fly-install -o ~/.local/bin/fly-install
chmod +x ~/.local/bin/fly-install
~/.local/bin/fly-install install --package
```

`fly-install` verifies Fly's release signature and installs the official DEB, RPM or pacman package with its signed repository, so your package manager keeps Fly up to date. `install --native` installs for your user without root. See [Installation](/guide/installation). Releases are built for Linux x86_64.

Building from source needs the pinned Chromium checkout and Fly's overlay, not only `npm install`: follow [Chromium development](/chromium/build).

## Source and issues

* [Source repository](https://github.com/Arzuparreta/fly-browser)
* [Issues](https://github.com/Arzuparreta/fly-browser/issues)
* Fly is the independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser); it does not track that repository.
