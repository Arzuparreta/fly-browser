# Keyboard controls

Open **Preferences → Keyboard controls**, or press **Ctrl+,** with a new profile. Search by action, category or binding. Choose **Browser shortcuts** or **Page keys (NORMAL mode)**. Each action supports up to eight bindings.

Select a binding to change it, **Add binding** to record another, or × to remove it. The recorder reports conflicts and offers an explicit reassignment with the affected action names. Select **Record a two-key sequence** for page sequences. Tab moves around the recorder; Escape cancels it.

**Save bindings** validates and applies changes to every open tab immediately. **Discard changes** restores the saved profile. **Reset** restores one action; **Restore this section** restores the selected section. Import stages a profile for review; export includes the current draft. No restart or configuration-file editing is needed.

## Modes and text input

Mise has a small custom Vim-style controller, not the Vimium extension. Browser shortcuts work in text fields as well as elsewhere. Page keys only run in NORMAL mode, outside editable fields, explicit INSERT mode and link hints. Open shadow roots and composed input events are checked before consuming page keys. IME composition, dead keys and AltGr are left alone.

- **NORMAL:** page keys are available. Defaults include hjkl scrolling, d/u for half-page scrolling, gg for the top, and Shift+G for the bottom.
- **INSERT:** focus a text field or use the configured insert action. Escape leaves insert mode.
- **PASSTHROUGH:** all page keys and browser shortcuts go to the page except the configured passthrough toggle. Keep at least one browser binding for this toggle so you can return. The default is Shift+Escape.
- **Hints:** the custom hint overlay consumes its label alphabet, Backspace and Escape. Browser shortcuts retain precedence. Disabled and inert controls are excluded. Hint letters are temporary labels, not action bindings.

Escape and Tab are reserved for cancellation and focus navigation. Native control keys such as arrows, Enter, Space and Delete remain contextual UI behaviour. Sidebar letters no longer form a hidden second set of hard-coded browser actions. Tab stays within visible overlays; arrow keys navigate tab lists, dashboard selections and bookmark columns.

Alt+S (sidebar focus) opens and focuses the tab list; pressing it again from any sidebar control returns focus to the active page and collapses an unpinned sidebar, including retained pointer expansion. In an empty workspace it releases sidebar focus. Pinning remains unchanged: a pinned sidebar stays open. Custom bindings and the NORMAL-mode S action use the same behaviour.

Opening address/search, find, dashboard, preferences, palette, bookmarks, history, notes or diagnostics closes the previous browser panel. Escape closes the current panel and returns focus to the page; pressing its opening shortcut again also closes it. Ctrl+L can be cancelled even after focus moves to another browser control. In the dashboard, Escape first cancels workspace creation or renaming, then closes the dashboard. The binding recorder and native dialogs keep their own cancellation. Notes keep edits on closing; delayed data loads cannot reopen closed panels or take focus from a newer panel. Sharing accepts Escape from its controls and embedded page, except in page passthrough mode.

Browser chords also work when an iframe has focus. Vim-style page keys and hint discovery are confined to the main document and its open shadow roots; embedded frames retain their native keyboard navigation. Closed shadow roots cannot be inspected. Operating-system shortcuts may be intercepted before Electron receives them.

## Default bindings

Existing legacy profiles retain their explicit assignments, including empty and alternate bindings. Newly introduced defaults never take over a legacy shortcut. The table below describes new-profile defaults and is generated from the action catalogue.

| Action | Browser shortcuts | Page keys |
| :--- | :--- | :--- |
| Enter insert mode | Unassigned | `i` |
| Toggle passthrough mode | `Shift+Escape` | Unassigned |
| Close Current Tab | `Ctrl+W` | `x` |
| Create workspace | Unassigned | Unassigned |
| Cycle Clock Format (Date & Time / Time / Seconds) | Unassigned | Unassigned |
| Focus Active Webview | `Alt+W` | Unassigned |
| Focus Navigation Buttons | `Alt+N` | Unassigned |
| Go back | `Alt+ArrowLeft` | `Shift+H` |
| Go forward | `Alt+ArrowRight` | `Shift+L` |
| New Blank Tab | `Ctrl+T` | `t` |
| Next workspace | `Alt+PageDown` | Unassigned |
| Open Bookmarks and Quickmarks | `Ctrl+Shift+B` | Unassigned |
| Open Quick Notes Overlay | `Ctrl+N` | Unassigned |
| Pin / Unpin Active Tab | `Ctrl+Shift+X` / `F4` | `p` |
| Previous workspace | `Alt+PageUp` | Unassigned |
| Reload Active Tab | `Ctrl+R` / `F5` | `r` |
| Swap Split Panes (Left / Right) | `Ctrl+\` | Unassigned |
| Switch Split Pane Focus (Pane 1 / Pane 2) | `Ctrl+O` | Unassigned |
| Toggle Actionable History | `Ctrl+Shift+H` | Unassigned |
| Toggle Command Palette | `Ctrl+P` | Unassigned |
| Toggle Downloads Shelf | `Ctrl+Shift+D` | Unassigned |
| Toggle Dual-Split View (Side-by-Side) | `Ctrl+Shift+S` | Unassigned |
| Toggle Floating Address Bar | `Ctrl+L` | `o` |
| Toggle Live Clock & Date Display | Unassigned | Unassigned |
| Toggle Sidebar Expansion (Pin / Unpin) | `Ctrl+Shift+Z` | Unassigned |
| Toggle Sidebar Focus | `Alt+S` | `s` |
| Toggle Status & Mode Bar | `Ctrl+/` | Unassigned |
| Toggle Workspace Dashboard | `Ctrl+Shift+W` | `w` |
| Go to page bottom | Unassigned | `Shift+G` |
| Go to page top | Unassigned | `g g` |
| Scroll down | Unassigned | `j` |
| Scroll half a page down | Unassigned | `d` |
| Scroll half a page up | Unassigned | `u` |
| Scroll left | Unassigned | `h` |
| Scroll right | Unassigned | `l` |
| Scroll up | Unassigned | `k` |
| Hibernate Inactive Tabs | Unassigned | Unassigned |
| Wake All Tabs in Workspace | Unassigned | Unassigned |
| Check for Updates | Unassigned | Unassigned |
| Open Diagnostics & Health Hub | Unassigned | Unassigned |
| Test Clean Google Sign-In Flow | Unassigned | Unassigned |
| Toggle Colour Theme (Dark / Light) | Unassigned | Unassigned |
| Toggle Diagnostics Recording (In-Memory) | Unassigned | Unassigned |
| Toggle Window Controls (Minimise, Maximise, Close) | Unassigned | Unassigned |
| Close Split View | Unassigned | Unassigned |
| Cycle Split View Orientation | Unassigned | Unassigned |
| Toggle Dual-Split View (Stacked) | Unassigned | Unassigned |
| Customise keyboard controls | `Ctrl+,` | Unassigned |
| Open native settings menu | `F1` | Unassigned |
| Show What's New | Unassigned | Unassigned |
| Toggle full screen | `F11` | Unassigned |
| Toggle native menu bar visibility | Unassigned | Unassigned |
| Toggle Preferences / Help | `Ctrl+H` | Unassigned |
| Toggle Private Browsing Mode | `Ctrl+Shift+P` | Unassigned |
| Toggle web notifications | Unassigned | Unassigned |
| Duplicate active tab | `Ctrl+Shift+K` | Unassigned |
| Move active tab to another workspace | Unassigned | Unassigned |
| Move tab left | `Ctrl+Shift+PageUp` | Unassigned |
| Move tab right | `Ctrl+Shift+PageDown` | Unassigned |
| Next tab | `Ctrl+Tab` / `Ctrl+PageDown` | Unassigned |
| Previous tab | `Ctrl+Shift+Tab` / `Ctrl+PageUp` | Unassigned |
| Reopen last closed tab | `Ctrl+Shift+T` | Unassigned |
| Select last tab | `Alt+9` | Unassigned |
| Select tab 1 | `Alt+1` | Unassigned |
| Select tab 2 | `Alt+2` | Unassigned |
| Select tab 3 | `Alt+3` | Unassigned |
| Select tab 4 | `Alt+4` | Unassigned |
| Select tab 5 | `Alt+5` | Unassigned |
| Select tab 6 | `Alt+6` | Unassigned |
| Select tab 7 | `Alt+7` | Unassigned |
| Select tab 8 | `Alt+8` | Unassigned |
| Reset Tab Zoom Level | `Ctrl+0` | Unassigned |
| Toggle Auto-Collapse Sidebar (Pin / Unpin) | Unassigned | Unassigned |
| Open User Script Storage Directory | Unassigned | Unassigned |
| Open User Scripts Directory | Unassigned | Unassigned |
| Open User Styles Directory | Unassigned | Unassigned |
| Reload User Scripts and Styles | Unassigned | Unassigned |
| Add Current Page to Bookmarks | `Ctrl+D` | Unassigned |
| Copy page URL | Unassigned | `y` |
| Find In Page | `Ctrl+F` / `F3` | `/` |
| Jump to Quickmark | `Ctrl+J` | Unassigned |
| Open Extension Actions (Popups, Commands, Options) | Unassigned | Unassigned |
| Play/Pause Media Playback Globally | `Ctrl+Shift+0` / `F10` | Unassigned |
| Print active page | Unassigned | Unassigned |
| Reload without cache | `Ctrl+Shift+R` | `Shift+R` |
| Set Quickmark | `Ctrl+Shift+Q` | Unassigned |
| Stop loading | Unassigned | Unassigned |
| Toggle Active Webview DevTools | `Ctrl+Shift+I` / `F12` | Unassigned |
| Toggle Audio Mute Globally (All Tabs) | Unassigned | `Shift+M` |
| Toggle Audio Mute on Active Tab | `Ctrl+Shift+M` / `F8` | `m` |
| Toggle Dark Reader on Active Tab | `Alt+D` | Unassigned |
| Toggle Link Hints Overlay | `Alt+F` | `f` |
| Toggle Reader View on Active Tab | `F9` | `e` |
| Toggle shields for the active site | Unassigned | Unassigned |
| Toggle website light or dark theme | Unassigned | Unassigned |
| Zoom in | `Ctrl+Plus` / `Ctrl+=` | Unassigned |
| Zoom out | `Ctrl+-` | Unassigned |

## Profile format

Bindings remain local in `~/.config/mise-browser/keybinds.json`. Version 2 separates browser and page bindings:

```json
{
  "version": 2,
  "shortcuts": {"spawn-tab": ["Ctrl+T"], "reload-active-tab": []},
  "normal": {"page-top": ["G G"], "scroll-down": ["J"]}
}
```

Omitted actions inherit defaults. An empty array explicitly disables an action in that section. Legacy flat action tables are accepted and migrated on the next successful save. Values use Ctrl, Alt, Shift and Meta; modifiers are normalised, while visible punctuation and the user's keyboard layout are preserved. Use Plus for the plus key. Sequences have two strokes, separated by a space, and a 700 ms interval.

Duplicate assignments, ambiguous sequence prefixes, unknown action IDs and unreachable page bindings are rejected. Saving replaces the file atomically before changing the active map; a failed write leaves the previous file and bindings intact. An invalid file at startup is left untouched, logged and reported in settings, with defaults used for that run.

Closed-tab recovery keeps at most twenty URLs in memory, restores the original workspace when it still exists, and clears its buffer when private mode changes. It does not restore a closed tab's navigation history or persist recovery data.

## Development

The action catalogue and defaults live in `keybinds.js`. `keyboard.js` is the dependency-free grammar used by Node and the renderer. Electron's sandbox cannot require local files, so `scripts/sync-keyboard.js` embeds an identical generated copy in the guest preload. Keep the sandbox and isolation settings enabled.

After changing the grammar or defaults:

```sh
node scripts/sync-keyboard.js
node scripts/generate-keybind-docs.js
npm test
```

The test suite checks that generated code and this reference have not drifted. Browser and guest maps are compiled only when configuration changes. Ordinary key presses do not write files or fetch bindings over IPC. Two-key prefixes use a precompiled set and a timestamp, with no background timer.
