# Command Centre & Palette Reference

The Command Centre gives you searchable access to Fly's actions and their keyboard shortcuts from a compact panel in the middle of the window.

---

## Accessing the Command Centre

![Fly Browser Command Centre Overlay](/screenshots/command-centre.webp)

- Press **Ctrl + P** from anywhere in Fly.
- Type to search titles, descriptions, categories, keywords and bindings.
- Press **Tab** and **Shift + Tab**, or click the chips (**All**, **Tabs**, **Split View**, **Performance**, **Tools**, **Scripts**, **Extensions**, **Preferences**), to filter by category.
- Use **Up** and **Down** (or move the pointer) to select a command. **Page Up** and **Page Down** move eight rows.
- Press **Enter** or click a row to run it.
- Press **Escape** or click outside the panel to close it and return focus to your page.

---

## Layout

Every Fly menu (the Command Centre, the address bar, history, bookmarks, downloads, extensions, workspaces, Preferences and Diagnostics) shares one look: a header with the menu's own icon and name, a search field, the list, and a footer that shows what the keyboard does in that menu. The icon and the shape of the panel tell the menus apart.

1. **List**: commands grouped by category, with **Recently used** first when nothing is typed. Each row shows the shortcut on the right; NORMAL mode keys are the quieter chips.
2. **Details**: under the list, the selected command's name and what it does.
3. **Clipboard-mediated terminal fallback**: if a search matches no command, the only row is **Run in terminal**. Enter copies the text and passes it to your external terminal using the oversight protocol.

---

## Common commands

The shortcuts below are new-profile defaults. The palette shows your saved assignments, including empty assignments. Every action is available in the [generated keyboard catalogue](/reference/keybinds), including tab/workspace navigation, transfers, zoom, full screen and mode controls.

### Tabs & Navigation

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **New Blank Tab** | `Ctrl+T` | `spawn-tab` | Creates an isolated page in the current workspace. |
| **Toggle Floating Address Bar** | `Ctrl+L` | `toggle-address` | Modal address bar with query completion and bang search engine aliases. |
| **Toggle Workspace Dashboard** | `Ctrl+Shift+W` | `toggle-dashboard` | Full-page hierarchical tree of all workspaces, tabs, and windows. |
| **Toggle Actionable History** | `Ctrl+Shift+H` | `toggle-history` | Local history ledger with domain-level filtering and record purging. |
| **Toggle Link Hints Overlay** | `Alt+F` | `trigger-hints` | Letter tags overlaid on clickable webpage links for keyboard browsing. |
| **Find In Page** | `Ctrl+F` / `F3` | `toggle-find` | In-page text search overlay across active page content. |
| **Reload Active Tab** | `Ctrl+R` / `F5` | `reload-active-tab` | Refreshes current page document while preserving session state. |
| **Close Current Tab** | `Ctrl+W` | `remove-tab` | Disposes of active page and reclaims allocated V8 heap memory. |
| **Pin / Unpin Active Tab** | `Ctrl+Shift+X` / `F4` | `toggle-pin-tab` | Pins essential tab to the top of the workspace or unpins it. |
| **Mute / Unmute Active Tab** | — | — | Toggles Chromium audio output for the active tab without pausing playback. |
| **Reset Site Zoom Level** | — | — | Restores 100% zoom for the hostname across open tabs and clears its saved preference. Private tabs reset only their separate in-memory preference. |
| **Focus Sidebar Tab List** | `Alt+S` | `focus-sidebar` | Moves keyboard focus directly into the vertical tab list. |
| **Focus Navigation Buttons** | `Alt+N` | `focus-nav-buttons` | Cycles keyboard focus across toolbar navigation controls. |
| **Focus Active Webview** | `Alt+W` | `focus-webview` | Returns keyboard and typing focus directly to the guest webpage. |
| **Toggle Sidebar (Expand / Collapse)** | `Ctrl+Shift+Z` | `toggle-zen-mode` | Alternates sidebar between 220px expanded width and 48px icon strip. |
| **Toggle Status & Mode Bar** | `Ctrl+/` | `toggle-status-bar` | 22px footer status bar displaying modal state, target URL, and TLS encryption. |
| **Toggle Auto-Collapse Sidebar** | — | — | Pins sidebar open or enables auto-shrink to 36px strip with hover expansion. |
| **Toggle Zen Mode (Hide Sidebar)** | — | — | Completely hides peripheral sidebar chrome for edge-to-edge reading. |

### Split View

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **Toggle Dual-Split View (Side-by-Side)** | `Ctrl+Shift+S` | `toggle-split` | Tiles two pages side by side in equal columns for multitasking. |
| **Toggle Dual-Split View (Stacked)** | — | — | Tiles two pages stacked vertically in top and bottom rows. |
| **Cycle Split View Orientation** | — | — | Cycles layout geometry between vertical side-by-side and horizontal stacked. |
| **Switch Split Pane Focus** | `Ctrl+O` | `switch-split-focus` | Moves active input and scroll focus to the opposite split pane. |
| **Swap Split Panes** | `Ctrl+\` | `swap-split-panes` | Reverses positions of primary and secondary split panes. |
| **Close Split View** | — | — | Exits dual-split mode and restores primary page to full viewport. |

### Performance & Memory

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **Suspend Inactive Tabs** | — | — | Pauses eligible background pages in memory without reloading. |
| **Resume Tabs in Workspace** | — | — | Resumes existing pages in the active workspace without reloading. |

### Tools

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **Bookmarks Manager** | `Ctrl+Shift+B` | `toggle-bookmarks` | Manager for saved bookmarks. |
| **Bookmark Current Page** | `Ctrl+D` | `add-bookmark` | Saves current webpage title and URL into bookmarks collection. |
| **Toggle Downloads Shelf** | `Ctrl+Shift+D` | `toggle-downloads` | Bottom drawer showing transfer speed, progress bars, and file links. |
| **Toggle Active Webview DevTools** | `Ctrl+Shift+I` / `F12` | `toggle-devtools` | Chromium developer tools for inspecting DOM, network, and console. |
| **Toggle Global Media Playback** | `Ctrl+Shift+0` / `F10` | `toggle-global-media` | Global play and pause toggle across all active media streams. |
| **Toggle Tab Audio Mute** | `Ctrl+Shift+M` / `F8` | `toggle-tab-audio-mute` | Mute or unmute audio output on active tab without pausing playback. |
| **Toggle Global Audio Mute** | Unassigned | `toggle-global-audio-mute` | Mute or unmute audio output across all tabs and workspaces simultaneously. |
| **Toggle Reader View on Active Tab** | `F9` | `toggle-reader-view` | Distils webpage into a clean, distraction-free reading layout. |

### User Scripts & Styles

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **Open User Scripts Directory** | — | — | Opens `~/.config/fly-browser/scripts` in default system file manager. |
| **Open User Styles Directory** | — | — | Opens `~/.config/fly-browser/styles` in default system file manager. |
| **Reload User Scripts and Styles** | — | — | Hot-reloads custom scripts and stylesheets across active pages. |

### Preferences & System

| Command Title | Shortcut | Action ID | Description |
| :--- | :--- | :--- | :--- |
| **Open Preferences** | `Ctrl+H` | `toggle-help` | Settings panel with a section list (General, Appearance, Keyboard, Privacy, Extensions, Performance, About) and a search across every section. |
| **Toggle Private Browsing Mode** | `Ctrl+Shift+P` | `toggle-private-mode` | Switches session to in-memory non-persistent partition. |
| **Test Clean Google Sign-In Flow** | Unassigned | `test-google-signin` | Launches Google Sign-In in a clean, volatile scratch partition to audit authentication health. |
| **Toggle Colour Theme (Dark / Light)** | — | — | Swaps UI styling and website theme preference between dark and light. |
| **Open Native Settings Menu** | `F1` | `toggle-menu-bar` | Opens the native browser settings menu. |
