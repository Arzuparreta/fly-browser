# Chrome Extensions (Opt-in)

Mise is built around **zero extensions**: ad blocking, tab suspension, styling and page scripting are handled natively or through [user scripts and styles](/guide/user-scripts-and-styles). That remains the recommended setup.

For the few extensions that have no userscript equivalent, Mise can load **unpacked Chrome extensions** under strict rules designed to keep its idle footprint at zero:

* **Off by default.** While disabled, no extension code, library or process is loaded.
* **Per workspace.** Each extension only runs in the workspaces you tick. Every other workspace stays exactly as lean as before.
* **Only while needed.** Extensions load when an opted-in workspace gets its first live tab and unload once every tab in it has been closed or hibernated, after a grace period (60 seconds by default).
* **Local only.** There is no extension store and no automatic updates. Only code you place in your config directory can run.
* **Never in private browsing.** Electron refuses to load extensions into in-memory sessions.

---

## Installing an Extension

1. Download the extension's **Chrome build** as a `.zip` (most open source extensions publish one on their GitHub releases page) and unpack it.
2. Move the unpacked folder into `~/.config/mise-browser/extensions/`, so that `manifest.json` sits directly inside it:
   ```text
   ~/.config/mise-browser/extensions/
   └── darkreader/
       ├── manifest.json
       └── ...
   ```
3. Open **Preferences** (`Ctrl + H`) and scroll to **Extensions**, or run *Extension Settings* from the Command Palette.
4. Turn on **Enable Chrome Extensions**, then tick the workspaces the extension may run in.

Tabs that were already open in that workspace need a reload before content scripts appear in them. New tabs get the extension immediately.

::: warning RENAMING THE FOLDER
An unpacked extension's identity is derived from its folder path. Renaming or moving the folder gives it a new identity and its saved settings stay behind with the old one. Keep the folder name stable when updating.
:::

### Updating

Replace the folder contents with the new version and run **Reload Extensions** (Preferences or Command Palette). Open tabs keep the old content scripts until reloaded.

---

## Using Extensions

| What | How |
| :--- | :--- |
| **Popup** | Click the extension icon on the right of the status bar, or run `Extension Name: Open Popup` from the Command Palette. Close it with `Esc` or by clicking outside. |
| **Extension shortcuts** | Commands an extension declares (for example Dark Reader's *Toggle current site*) appear in the Command Palette under **Extensions** and run against the active tab. |
| **Options page** | `Extension Name: Options` in the Command Palette, or the Options button in Preferences. It opens as a normal tab in the current workspace. |
| **Context menu** | Items added by extensions appear in the right-click menu of pages in that workspace (see the `onInstalled` note under Compatibility). |
| **Keyboard** | Bind `open-extension-actions` in `keybinds.json` to jump straight to the Extensions category of the Command Palette. It has no default key so pages keep every shortcut. |

Popups render inside the Mise window rather than as separate windows, so they behave the same on X11, Wayland, Hyprland and sway.

---

## What It Costs

Preferences shows live numbers for your machine under **Live Resource Usage**. As a rule of thumb, the cost depends on the extension type, which Preferences labels for each extension:

| Label | What runs | Cost |
| :--- | :--- | :--- |
| **No background** | Content scripts only | No process of its own. Work happens inside the pages it matches. |
| **Service worker** | MV3 background worker | A process only while handling events. Chromium stops it after about 30 seconds idle. |
| **Event page** | MV2 lazy background page | Loaded on demand. |
| **Persistent** | MV2 background page | A process that stays alive for as long as the extension is loaded. |

Content scripts scale with the number of **live** tabs that match, which tab hibernation already keeps low. Background processes scale with the number of **workspaces** that have the extension loaded at the same time.

---

## Workspaces, Storage and Privacy

* Each workspace is its own Chromium session, so each keeps **separate extension settings and storage**, exactly like cookies. Configure an extension once per workspace.
* An extension that matches `<all_urls>` can read every page in the workspaces where it runs, including trusted sites such as banks. Enable broad extensions only in the workspaces that need them.
* Extension pages use the same permission rules as websites: camera, microphone and location stay blocked.

---

## Compatibility

Extension APIs come from Electron plus the [electron-chrome-extensions](https://github.com/samuelmaddock/electron-browser-shell/tree/master/packages/electron-chrome-extensions) library (GPL-3.0). Most extensions that modify pages work. Known limits:

* **`declarativeNetRequest` rules are not enforced.** Electron exposes the API but its rules never block anything, so rule based blockers such as uBlock Origin Lite load without effect. Mise's built-in blocker covers this.
* **Blocking `webRequest` is overridden** by Mise's own network filters, which take precedence in Electron.
* **`runtime.onInstalled` does not fire** for unpacked extensions in Electron. Anything an extension only sets up there, most often its context menu items, will be missing. Items created when the extension starts do appear.
* **No side panels, native messaging hosts or Chrome sync.**
* **Firefox builds**: there is no Gecko engine, so `.xpi` files are not supported as such. Cross-browser extensions often work when their Firefox build is unpacked, but the Chrome build is the safer choice.
* **Hibernation** closes a tab's renderer, so an extension sees the tab close and a new one open when it wakes. Extensions that keep per-tab state start fresh for that tab.
* Mise's native Dark Reader (`Alt + D` by default; configurable in Keyboard preferences) and a dark mode extension on the same tab invert colours twice. Use one or the other.
* In NORMAL mode Mise keeps keystrokes for its own shortcuts, so extension keyboard handlers inside pages only receive keys in INSERT or PASSTHROUGH mode.

---

## Disabling

Turn off **Enable Chrome Extensions**. Every extension unloads immediately; pages that already ran content scripts keep them until reloaded. Removing a folder from `~/.config/mise-browser/extensions/` uninstalls it.
