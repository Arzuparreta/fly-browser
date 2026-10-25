# Overview & Philosophy

Fly Browser is a small, minimal alternative to the typical Linux browsers. It keeps full control of the browser on the keyboard while offering a polished experience for mouse users.

Fly is the renamed, independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser). It aims to stay a lean, intentional tool built for privacy, focus and low resource use rather than an all-consuming platform of its own.

Typical desktop browsers consume gigabytes of memory across dozens of background processes, producing heat and throttling on fanless hardware. Fly provides an ultra-lean browsing environment by stripping away unnecessary browser overhead while delivering modern privacy isolation, multi-account containers, and complete mouse-free operation.

---

## Design Principles

- **Keyboard First**: Every single feature, tab switch, link click, and setting can be reached without touching a mouse.
- **Resource Respect**: Eligible background pages pause without unloading, saving CPU and battery while preserving page state.
- **Strict Privacy Isolation**: Workspaces operate in isolated Chromium profiles. Work and personal profiles never leak cookies or storage.
- **Crash & Session Resilience**: Atomic disk writes, rolling backup journals, and clean-exit sentinels protect active workspaces against power cuts or sudden terminations.
- **Air-Gapped Security**: Untrusted scripts and shell commands are never blindly executed; they pass through clipboard verification.
- **Distro & Shell Agnostic**: Fully integrated with Arch Linux and native terminal emulators while functioning seamlessly across all environments.

---

## Architecture at a Glance

Fly uses the full Chromium browser layer with native Views UI:

1. **Native shell** (`chromium/overlay/fly/ui/`): sidebar, split panes, pickers and Preferences.
2. **Workspace profiles**: separate Chromium profiles for logins, storage and extensions.
3. **Product services**: commands, keyboard bindings, bookmarks, preferences and recovery.
4. **Local data**: Chromium-managed state under `~/.config/fly-browser/`.

---

## First Steps

Once launched, Fly opens a window for your first workspace with a new tab:

![Fly Browser New Tab](/screenshots/new-tab.webp)

- Press **Ctrl + L** to open the floating address bar and navigate to any site.
- Press **Ctrl + T** to open a new tab.
- Press **Alt + F** (or **f** in NORMAL mode) to activate the Link Hints overlay and navigate links using your keyboard.
- Press **Ctrl + P** to bring up the Command Palette for quick access to all browser functions.
- Press **Ctrl + ,** to open Preferences. Use the section index on the left, or ↑/↓ once it has focus, to move between sections. Press **/** or **Ctrl + F** to search every setting.

These are new-profile defaults. Open **Preferences → Keyboard** (default **Ctrl+,**) to choose your browser and page bindings. See the [keyboard reference](/reference/keybinds).
