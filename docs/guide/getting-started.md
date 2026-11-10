# Getting started

Fly is the full Chromium browser with its own interface: vertical tabs in a sidebar, one status bar and floating panels, all of it reachable from the keyboard. It is the renamed, independent continuation of [Mise by Rakosn1cek](https://github.com/Rakosn1cek/Mise-browser).

## Design principles

- **The keyboard reaches everything.** Browser shortcuts work everywhere; single page keys, as in Vim, work whenever you are not typing. The mouse works everywhere too.
- **The interface stays out of the way.** The page gets the window, except a strip of tab icons and one status line.
- **Workspaces are isolated.** Each one is a separate Chromium profile in its own window: cookies, logins and extensions never cross.
- **Background tabs do not cost.** Idle pages are paused in memory and resume where they were.
- **Local by default.** No Fly telemetry and no account. See the [privacy policy](/guide/privacy-policy) for what Chromium's own services contact.

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

- Press **Ctrl + L** (or **o**) to open the address bar and go to any site.
- Press **Ctrl + T** (or **t**) to open a new tab with the address bar ready for typing.
- Press **f** and type the letters that appear on a link to follow it without the mouse.
- Press **j** and **k** to scroll, **J** and **K** to change tab, **H** and **L** to go back and forward.
- Look at the left end of the status bar: it shows `NORMAL` while single keys act on the browser and `INSERT` while they go to a text field. **Escape** returns to `NORMAL`.
- Press **Ctrl + P** to open the Command Centre, which finds any command and shows its key.
- Press **Ctrl + H** to open Preferences, and again to close it. Use the section index on the left, or ↑/↓ once it has focus, to move between sections. Press **/** or **Ctrl + F** to search every setting.
- Press **Ctrl + ,** (or **?** in NORMAL mode) to open Preferences directly on its **Keyboard** section, where you see every shortcut and can change it.

These are new-profile defaults. See the [keyboard reference](/reference/keybinds).
