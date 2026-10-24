# Keyboard controls

Open **Preferences → Keyboard**, or press **Ctrl+,** with a new profile. The editor is a table with one row per action and three columns: the action, its **shortcut** (works everywhere, also while typing) and its **NORMAL mode** key (works only when you are not typing). Filter the table by action, category or key, or turn on **Changed only** to list the actions that differ from the defaults. **Tab numbers** sets the one modifier used with 1–9 to select tabs (Alt by default, or Off).

Select a cell with the arrow keys or the pointer. **Enter**, **Space**, a double click or **Record key** records a new binding for that cell; a NORMAL mode cell accepts one key or two keys in a row. **Delete** or **Clear** empties the cell and **Default** restores it. Changed cells are drawn in the accent colour. Typing a letter while a cell has focus filters the table.

**Save shortcuts** (Ctrl+S) validates the whole draft and applies it to every open tab immediately; conflicts are reported and nothing is saved until they are resolved. **Discard changes** returns to the saved profile. **Restore, import, export…** restores all defaults, only the shortcuts or only the NORMAL keys, imports a profile for review or exports the current draft's changes from the defaults. Closing Preferences with unsaved changes asks once; close it again to discard them.

## Modes and text input

Fly has a small custom Vim-style controller, not the Vimium extension. Browser shortcuts work in text fields as well as elsewhere. Page keys only run in NORMAL mode, outside editable fields, explicit INSERT mode and link hints. Open shadow roots and composed input events are checked before consuming page keys. Custom element names do not imply editing; actual inputs, contenteditable regions and textbox/searchbox/combobox roles protect text entry. IME composition, dead keys and AltGr are left alone.

- **NORMAL:** page keys are available. Defaults include hjkl scrolling, d/u for half-page scrolling, gg for the top, and Shift+G for the bottom.
- **INSERT:** focus a text field or use the configured insert action. Escape leaves insert mode.
- **PASSTHROUGH:** all page keys and browser shortcuts go to the page except the configured passthrough toggle. Keep at least one browser binding for this toggle so you can return. The default is Shift+Escape.
- **Hints:** the custom hint overlay consumes its label alphabet, Backspace and Escape. Browser shortcuts retain precedence. Disabled and inert controls are excluded. Hint letters are temporary labels, not action bindings.

Escape and Tab are reserved for cancellation and focus navigation. Native control keys such as arrows, Enter, Space and Delete remain contextual UI behaviour. Configured page keys also work from non-editable browser navigation controls and the empty workspace, outside browser panels. They use the same saved assignments as the page; there is no hidden set of sidebar shortcuts. Tab stays within visible overlays; arrow keys navigate tab lists, dashboard selections and bookmark columns.

Alt+S (sidebar focus) opens and focuses the tab list; pressing it again from any sidebar control returns focus to the active page and collapses an unpinned sidebar, including retained pointer expansion. In an empty workspace it releases sidebar focus. Pinning remains unchanged: a pinned sidebar stays open. Custom bindings and the NORMAL-mode S action use the same behaviour. In the tab list, the configured scroll-down/up actions (j/k by default) move the cursor without activating or waking tabs. Enter/Space activate and retain sidebar focus; scroll-right (l) or Right activates and focuses the page. The close and yank actions target the highlighted tab. Fixed arrow/Delete keys retain their native UI roles. The tab selection modifier (Alt by default) with 1 through 8 activates the numbered tab, or the last tab when fewer exist, and with 9 always selects the last tab. Holding it on its own (the left key, for Alt) paints these numbers over the tab icons, whether a page or the interface has the keyboard; releasing it, pressing any other key or leaving the window hides them. Pages in passthrough mode show none.

Opening address/search, find, dashboard, preferences, palette, bookmarks, history or diagnostics closes the previous browser panel; the exception is find inside Preferences, which focuses the settings search. Escape closes the current panel and returns focus to the page; pressing its opening shortcut again also closes it. Ctrl+L can be cancelled even after focus moves to another browser control. In the dashboard, Escape first cancels workspace creation or renaming, then closes the dashboard. The binding recorder and native dialogs keep their own cancellation. Delayed data loads cannot reopen closed panels or take focus from a newer panel. Sharing accepts Escape from its controls and embedded page, except in page passthrough mode.

## Preferences

The section list on the left shows where you are. When a section has focus, ↑/↓ move between sections (wrapping at the ends), Home/End jump to the first or last, and Right, Enter or Tab enter the section. Ctrl+PgDn and Ctrl+PgUp (or Ctrl+Tab and Ctrl+Shift+Tab) change section from anywhere in Preferences. Tab and Shift+Tab move through the controls of the panel. Switches toggle with Space; lists open with Enter or Space, and ↑/↓ change their value directly. **/** or Ctrl+F focuses **Search settings**, which lists the matching settings of every section with the number of matches beside each section. Enter moves to the first result. Escape cancels recording, then clears a non-empty search, then closes Preferences.

Browser chords also work when an iframe has focus. Vim-style page keys and hint discovery are confined to the main document and its open shadow roots; embedded frames retain their native keyboard navigation. Closed shadow roots cannot be inspected. Operating-system shortcuts may be intercepted before Fly receives them.

## Default bindings

The table lists the bindings of a new profile.

<!-- default bindings: generated by scripts/chromium/generate-commands.js -->

| Action | Browser shortcuts | Page keys |
| :--- | :--- | :--- |
| Enter insert mode | Unassigned | `i` |
| Toggle passthrough mode | `Shift+Escape` | Unassigned |
| Close Current Tab | `Ctrl+W` | `x` |
| Create workspace | Unassigned | Unassigned |
| Cycle Clock Format (Date & Time / Time / Seconds) | Unassigned | Unassigned |
| Focus Active Page | `Alt+W` | Unassigned |
| Focus Navigation Buttons | `Alt+N` | Unassigned |
| Go back | `Alt+ArrowLeft` | `Shift+H` |
| Go forward | `Alt+ArrowRight` | `Shift+L` |
| New Blank Tab | `Ctrl+T` | `t` |
| Next workspace | `Alt+PageDown` | Unassigned |
| Open Bookmarks | `Ctrl+Shift+B` | Unassigned |
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
| Resume Tabs in Workspace | Unassigned | Unassigned |
| Suspend Inactive Tabs | Unassigned | Unassigned |
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
| Reset Tab Zoom Level | `Ctrl+0` | Unassigned |
| Toggle Auto-Collapse Sidebar (Pin / Unpin) | Unassigned | Unassigned |
| Open User Scripts Directory | Unassigned | Unassigned |
| Open User Styles Directory | Unassigned | Unassigned |
| Reload User Scripts and Styles | Unassigned | Unassigned |
| Add Current Page to Bookmarks | `Ctrl+D` | Unassigned |
| Copy page URL | Unassigned | `y` |
| Find In Page | `Ctrl+F` / `F3` | `/` |
| Open Extension Actions (Popups, Commands, Options) | Unassigned | Unassigned |
| Play/Pause Media Playback Globally | `Ctrl+Shift+0` / `F10` | Unassigned |
| Print active page | Unassigned | Unassigned |
| Reload without cache | `Ctrl+Shift+R` | `Shift+R` |
| Stop loading | Unassigned | Unassigned |
| Toggle Audio Mute Globally (All Tabs) | Unassigned | `Shift+M` |
| Toggle Audio Mute on Active Tab | `Ctrl+Shift+M` / `F8` | `m` |
| Toggle Developer Tools | `Ctrl+Shift+I` / `F12` | Unassigned |
| Toggle Link Hints Overlay | `Alt+F` | `f` |
| Toggle Reader View on Active Tab | `F9` | `e` |
| Zoom in | `Ctrl+Plus` / `Ctrl+=` | Unassigned |
| Zoom out | `Ctrl+-` | Unassigned |
| Select tab by number (9: last tab) | `Alt+1` … `Alt+9` | Unassigned |

<!-- end of default bindings -->

## Saved bindings

Fly saves only the bindings that differ from the defaults, in the `fly.keyboard`
entry of the profile's `Local State` file. Actions you have not changed follow
the defaults of the Fly version you run, so later default improvements still
apply. An empty list disables an action. Your bindings take precedence: a
default that would collide with one of them gives up only the colliding chord.
If one of your bindings uses a default tab-number chord, tab numbers are off
until you choose a modifier.

**Export to file…** writes the same changes, including unsaved ones, as JSON:

```json
{
  "version": 2,
  "changesOnly": true,
  "shortcuts": {"spawn-tab": ["Ctrl+Shift+Y"], "reload-active-tab": []},
  "normal": {"scroll-down": ["Z", "ArrowDown"]},
  "tabs": "Ctrl"
}
```

**Import from file…** loads such a file into the editor for review; nothing
changes until you save. `tabs` is the modifier for selecting tabs by number; an
empty string turns it off. Values use Ctrl, Alt, Shift and Meta; modifiers are
normalised, while visible punctuation and the keyboard layout are preserved.
Use Plus for the plus key. Sequences have two strokes, separated by a space,
and a 700 ms interval.

Duplicate assignments, ambiguous sequence prefixes, unknown action IDs and
unreachable page bindings are rejected, and nothing is saved until they are
resolved.

## Development

Every action, with its label, category and default bindings, is defined in
`chromium/overlay/fly/commands/actions.json`; the Command Centre entries are in
`commands.json` beside it. After changing either, run:

```sh
node scripts/chromium/generate-commands.js
npm test
```

The script writes `generated_catalog.h`, which the native keyboard and Command
Centre code read, and the table above. `npm test` fails when either is stale.
