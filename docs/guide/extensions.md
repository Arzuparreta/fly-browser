# Chrome Extensions

Fly enables the included uBlock Origin **by default** in persistent workspaces. Tab suspension and [user scripts and styles](/guide/user-scripts-and-styles) remain available without enabling extension support. **uBlock Origin 1.75.0** is included as a normal preinstalled Chromium extension. Dark Reader is not included and its former native CSS toggle has been removed; install its Chrome build yourself if wanted.

Fly loads **unpacked Chrome extensions** with these rules:

* **uBlock enabled by default.** A saved global disable, individual disable, removal or empty workspace selection is preserved on upgrade. While disabled, no extension code, library or process is loaded and there is no ad blocker.
* **Per workspace.** Included extensions are selected in workspaces without a saved selection. You can change each workspace’s selection, including selecting none. A renamed workspace keeps its selection. Manually installed extensions run only where selected.
* **Only while needed.** Extensions load when an opted-in workspace gets its first live tab and unload once every tab in it has been closed or hibernated, after a grace period (60 seconds by default).
* **Local only.** There is no extension store. Included extensions are copied into your profile; Fly releases can update managed copies. Manually installed folders are never overwritten.
* **Never in private browsing.** Electron refuses to load extensions into in-memory sessions. Private browsing therefore has no ad blocker.

---

## Included uBlock Origin

Open **Preferences → Extensions** to manage **Enable Chrome extensions**. New profiles enable it; existing profiles keep their saved setting. uBlock Origin is already listed, with popup, options, commands, workspace selection and resource usage. Reload existing tabs to apply its content scripts; newly opened tabs receive them immediately.

The **Enabled** checkbox switches an extension off everywhere while retaining its files, storage and workspace selections. **Remove** unloads it and deletes its profile folder. Both choices persist across restarts and Fly updates. Deleting an included folder manually also records its removal on the next scan or enabled launch. An explicit empty workspace selection stays empty after updates. Turning the global extension switch back on never reverses an individual disable or removal.

To reinstall a removed extension deliberately, place its unpacked Chrome build in the extensions directory again, enable it and choose its workspaces. Fly does not silently restore removed bundles. Reinstalling at the same path retains the extension’s identity and workspace storage.

## Why the full uBlock Origin, not Lite?

[uBlock Origin Lite](https://github.com/uBlockOrigin/uBOL-home) is the Manifest V3 blocker for current Chrome/Chromium. The [full uBlock Origin](https://github.com/gorhill/uBlock) remains maintained for browsers that support Manifest V2, including Fly's current Electron runtime. The bundled copy matches the latest stable upstream release checked during this change.

Electron supports only a [subset of extension APIs](https://www.electronjs.org/docs/latest/api/extensions/). On Fly's installed runtime, a standalone Manifest V3 extension with an enabled static `declarativeNetRequest` blocking rule still lets its matching script reach a local server and execute. Loading an extension or finding its API object does not prove enforcement. Lite relies on this mechanism, so Fly retains the working full uBlock until real declarative blocking is available and verified. There is no guarantee of future Manifest V2 support; switching safely requires that runtime capability, not just replacing the extension files.

Managed bundled updates arrive through Fly releases; filter lists update through uBlock. Manually installed copies remain the user's responsibility. Recheck upstream releases and network enforcement when updating Electron.

## Installing an Extension

1. Download the extension's **Chrome build** as a `.zip` (most open source extensions publish one on their GitHub releases page) and unpack it.
2. Move the unpacked folder into `~/.config/fly-browser/extensions/`, so that `manifest.json` sits directly inside it:
   ```text
   ~/.config/fly-browser/extensions/
   └── darkreader/
       ├── manifest.json
       └── ...
   ```
3. Open **Preferences** (`Ctrl + H`) and select **Extensions** in the section index, or run *Extension Settings* from the Command Palette. The installed list and usage figures are scanned only while this section is shown.
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

Popups render inside the Fly window rather than as separate windows, so they behave the same on X11, Wayland, Hyprland and sway.

---

## What It Costs

Preferences shows live numbers for your machine under **Live resource usage**. As a rule of thumb, the cost depends on the extension type, which Preferences labels for each extension:

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

* **`declarativeNetRequest` rules are not enforced.** Electron exposes the API but its rules never block anything, so rule based blockers such as uBlock Origin Lite load without effect. Use the included full uBlock Origin for blocking; uBlock Origin Lite cannot replace it here.
* **Network filtering belongs to extensions.** Fly leaves native request hooks detached while a `webRequest` extension is loaded. uBlock Origin is the only included blocker; the former Ghostery engine and per-tab shields have been removed. Configure site exceptions in uBlock itself. Session isolation and permission controls remain active. PDF URLs and MIME-only PDF responses still route to the built-in viewer, and Google sign-in navigation rewriting remains available.
* **First navigation waits for uBlock's filters.** A loaded background page is not enough: Fly waits for uBlock's final startup marker so restored media requests are not cancelled during initialisation.
* **`runtime.onInstalled` does not fire** for unpacked extensions in Electron. Anything an extension only sets up there, most often its context menu items, will be missing. Items created when the extension starts do appear.
* **No side panels, native messaging hosts or Chrome sync.**
* **Firefox builds**: there is no Gecko engine, so `.xpi` files are not supported as such. Cross-browser extensions often work when their Firefox build is unpacked, but the Chrome build is the safer choice.
* **Hibernation** closes a tab's renderer, so an extension sees the tab close and a new one open when it wakes. Extensions that keep per-tab state start fresh for that tab.
* In NORMAL mode Fly keeps keystrokes for its own shortcuts, so extension keyboard handlers inside pages only receive keys in INSERT or PASSTHROUGH mode.

---

## Disabling

Turn off **Enable Chrome Extensions**. Every extension unloads immediately; pages that already ran content scripts keep them until reloaded. Use an extension’s **Enabled** checkbox or **Remove** button for an individual change. Removing its folder from `~/.config/fly-browser/extensions/` also uninstalls it. Included extensions stay removed after updates.
