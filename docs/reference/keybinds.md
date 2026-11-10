# Keyboard controls

Press **Ctrl+,**, or **?** (Shift+?) in NORMAL mode, to open Preferences directly on its **Keyboard** section. **Ctrl+H** opens Preferences; choose **Keyboard** in the section list from there. Both are new-profile defaults. The editor is a table with one row per action and three columns: the action, its **shortcut** (works everywhere, also while typing) and its **NORMAL mode** key (works only when you are not typing). Filter the table by action, category or key, or turn on **Changed only** to list the actions that differ from the defaults. **Tab numbers** sets the one modifier used with 1–9 to select tabs (Alt by default, or Off).

Select a cell with the arrow keys or the pointer. **Enter**, **Space**, a double click or **Record key** records a new binding for that cell; a NORMAL mode cell accepts one key or two keys in a row. **Delete** or **Clear** empties the cell and **Default** restores it. Changed cells are drawn in the accent colour. Typing a letter while a cell has focus filters the table.

**Save shortcuts** (Ctrl+S) validates the whole draft and applies it to every open tab immediately; conflicts are reported and nothing is saved until they are resolved. **Discard changes** returns to the saved profile. **Restore, import, export…** restores all defaults, only the shortcuts or only the NORMAL keys, imports a profile for review or exports the current draft's changes from the defaults. Closing Preferences with unsaved changes asks once; close it again to discard them.

## Modes and text input

Fly uses a native Vim-style controller with continuous scrolling inspired by Vimium. Browser shortcuts work in text fields as well as elsewhere. Page keys only run in NORMAL mode, outside editable fields, explicit INSERT mode and link hints. Open shadow roots and composed input events are checked before consuming page keys. Custom element names do not imply editing; actual inputs, contenteditable regions and textbox/searchbox/combobox roles protect text entry. IME composition, dead keys and AltGr are left alone.

- **NORMAL:** page keys are available. Tap hjkl for a 60 CSS pixel step or hold for continuous scrolling. d/u jump half the current scroll container per press; gg and Shift+G move to its vertical ends.
- **INSERT:** focus a text field or use the configured insert action. Escape leaves insert mode.
- **PASSTHROUGH:** all page keys and browser shortcuts go to the page except the configured passthrough toggle. Keep at least one browser binding for this toggle so you can return. The default is Shift+Escape.
- **Hints:** the custom hint overlay consumes its label alphabet, Backspace and Escape. Browser shortcuts retain precedence. Disabled and inert controls are excluded. Hint letters are temporary labels, not action bindings.

Escape and Tab are reserved for cancellation and focus navigation. Native control keys such as arrows, Enter, Space and Delete remain contextual UI behaviour. Configured page keys also work from non-editable browser navigation controls, outside browser panels. They use the same saved assignments as the page; there is no hidden set of sidebar shortcuts. Tab stays within visible overlays; arrow keys navigate tab lists, dashboard selections and bookmark columns.

Alt+S (sidebar focus) opens and focuses the tab list; pressing it again from any sidebar control returns focus to the active page and collapses an unpinned sidebar, including retained pointer expansion. Pinning remains unchanged: a pinned sidebar stays open. Custom bindings and the NORMAL-mode S action use the same behaviour. In the tab list the keyboard cursor is drawn as four corner brackets. The keys bound to scroll down and up (j/k by default), and Down/Up, move it without activating or waking tabs; the key bound to the bottom of the page (G), and Home/End, move it to the ends. Enter, Space, the key bound to scroll right (l) or Right select the tab under the cursor and give its page the keyboard. The key bound to closing a tab (x) closes the tab under the cursor, which stays where it was; pinned tabs are left alone. The tab selection modifier (Alt by default) with 1 through 8 activates the numbered tab, or the last tab when fewer exist, and with 9 always selects the last tab. Holding it on its own (the left key, for Alt) paints these numbers over the tab icons, whether a page or the interface has the keyboard; releasing it, pressing any other key or leaving the window hides them. Pages in passthrough mode show none.

Opening address/search, find, dashboard, preferences, palette, bookmarks, history or diagnostics closes the previous browser panel; the exception is find inside Preferences, which focuses the settings search. Escape closes the current panel and returns focus to the page; pressing its opening shortcut again also closes it. Ctrl+L can be cancelled even after focus moves to another browser control. In the dashboard, Escape first cancels workspace creation or renaming, then closes the dashboard. The binding recorder and native dialogs keep their own cancellation. Delayed data loads cannot reopen closed panels or take focus from a newer panel. Sharing accepts Escape from its controls and embedded page, except in page passthrough mode.

## Preferences

The section list on the left shows where you are. When a section has focus, ↑/↓ move between sections (wrapping at the ends), Home/End jump to the first or last, and Right, Enter or Tab enter the section. Ctrl+PgDn and Ctrl+PgUp (or Ctrl+Tab and Ctrl+Shift+Tab) change section from anywhere in Preferences. Tab and Shift+Tab move through the controls of the panel. Switches toggle with Space; lists open with Enter or Space, and ↑/↓ change their value directly. **/** or Ctrl+F focuses **Search settings**, which lists the matching settings of every section with the number of matches beside each section. Enter moves to the first result. Escape cancels recording, then clears a non-empty search, then closes Preferences.

Browser chords also work when an iframe has focus. Vim-style page keys and hint discovery are confined to the main document and its open shadow roots; embedded frames retain their native keyboard navigation. Closed shadow roots cannot be inspected. Operating-system shortcuts may be intercepted before Fly receives them.

## Default bindings

The table lists the bindings of a new profile.

<!-- default bindings: generated by scripts/chromium/generate-commands.js -->

| Action | Browser shortcuts | Page keys |
| :--- | :--- | :--- |
| Change the clock format | Unassigned | Unassigned |
| Check for updates | Unassigned | Unassigned |
| Diagnostics | Unassigned | Unassigned |
| Fly menu | `F1` | Unassigned |
| Fly menu (same as above) | Unassigned | Unassigned |
| Keyboard settings | `Ctrl+,` | `Shift+?` |
| Open Google sign-in in a private window | Unassigned | Unassigned |
| Pin or unpin the sidebar | `Ctrl+Shift+Z` | Unassigned |
| Pin or unpin the sidebar (same as above) | Unassigned | Unassigned |
| Preferences | `Ctrl+H` | Unassigned |
| Show or hide the clock | Unassigned | Unassigned |
| Show or hide the status bar | `Ctrl+/` | Unassigned |
| Show or hide the window buttons | Unassigned | Unassigned |
| Switch between dark and light | Unassigned | Unassigned |
| Turn diagnostics recording on or off | Unassigned | Unassigned |
| Turn web notifications on or off | Unassigned | Unassigned |
| What's New | Unassigned | Unassigned |
| Insert mode | Unassigned | `i` |
| Passthrough mode | `Shift+Escape` | Unassigned |
| Address bar | `Ctrl+L` | `o` |
| Back | `Alt+ArrowLeft` | `Shift+H` |
| Copy page address | Unassigned | `y` |
| Find in page | `Ctrl+F` / `F3` | `/` |
| Find next | Unassigned | `n` |
| Find previous | Unassigned | `Shift+N` |
| Focus the first text field | Unassigned | `g i` |
| Forward | `Alt+ArrowRight` | `Shift+L` |
| Full screen | `F11` | Unassigned |
| Link hints | `Alt+F` | `f` |
| Link hints: open in a background tab | Unassigned | `Shift+F` |
| Print | Unassigned | Unassigned |
| Reader view | `F9` | `e` |
| Reload | `Ctrl+R` / `F5` | `r` |
| Reload without cache | `Ctrl+Shift+R` | `Shift+R` |
| Reset zoom | `Ctrl+0` | `z z` |
| Stop loading | Unassigned | Unassigned |
| Top of the site | Unassigned | `g Shift+U` |
| Up one level of the address | Unassigned | `g u` |
| Zoom in | `Ctrl+Plus` / `Ctrl+=` | `z i` |
| Zoom out | `Ctrl+-` | `z o` |
| Bottom of the page | Unassigned | `Shift+G` |
| Half a page down | Unassigned | `d` |
| Half a page up | Unassigned | `u` |
| Scroll down | Unassigned | `j` |
| Scroll left | Unassigned | `h` |
| Scroll right | Unassigned | `l` |
| Scroll up | Unassigned | `k` |
| Top of the page | Unassigned | `g g` |
| Split view: change orientation | Unassigned | Unassigned |
| Split view: close | Unassigned | Unassigned |
| Split view: focus the other pane | `Ctrl+O` | Unassigned |
| Split view: side by side | `Ctrl+Shift+S` | Unassigned |
| Split view: stacked | Unassigned | Unassigned |
| Split view: swap panes | `Ctrl+\` | Unassigned |
| Close tab | `Ctrl+W` | `x` |
| Duplicate tab | `Ctrl+Shift+K` | Unassigned |
| First tab | Unassigned | `g 0` |
| Focus the page | `Alt+W` | Unassigned |
| Focus the sidebar buttons | `Alt+N` | Unassigned |
| Focus the tab list | `Alt+S` | `s` |
| Last tab | Unassigned | `g $` |
| Move tab down | `Ctrl+Shift+PageDown` | `> >` |
| Move tab to another workspace | Unassigned | `Shift+W` |
| Move tab up | `Ctrl+Shift+PageUp` | `< <` |
| Mute or unmute tab | `Ctrl+Shift+M` / `F8` | `m` |
| New tab | `Ctrl+T` | `t` |
| Next tab | `Ctrl+Tab` / `Ctrl+PageDown` | `Shift+J` / `g t` |
| Pin or unpin tab | `Ctrl+Shift+X` / `F4` | `p` |
| Previous tab | `Ctrl+Shift+Tab` / `Ctrl+PageUp` | `Shift+K` / `g Shift+T` |
| Previously used tab | `Alt+0` | `g a` |
| Reopen closed tab | `Ctrl+Shift+T` | `Shift+X` |
| Bookmark this page | `Ctrl+D` | Unassigned |
| Bookmarks | `Ctrl+Shift+B` | `b` |
| Command Centre | `Ctrl+P` | Unassigned |
| Developer tools | `Ctrl+Shift+I` / `F12` | Unassigned |
| Downloads | `Ctrl+Shift+D` | Unassigned |
| Extension actions | Unassigned | Unassigned |
| History | `Ctrl+Shift+H` | Unassigned |
| Mute or unmute every tab | Unassigned | `Shift+M` |
| Open the user scripts folder | Unassigned | Unassigned |
| Open the user styles folder | Unassigned | Unassigned |
| Play or pause media in every tab | `Ctrl+Shift+0` / `F10` | Unassigned |
| Reload user scripts and styles | Unassigned | Unassigned |
| Resume suspended tabs | Unassigned | Unassigned |
| Suspend background tabs | Unassigned | Unassigned |
| New workspace | Unassigned | Unassigned |
| Next workspace | `Alt+PageDown` | Unassigned |
| Previous workspace | `Alt+PageUp` | Unassigned |
| Private window | `Ctrl+Shift+P` | Unassigned |
| Workspace 1 | `Ctrl+1` | Unassigned |
| Workspace 2 | `Ctrl+2` | Unassigned |
| Workspace 3 | `Ctrl+3` | Unassigned |
| Workspace 4 | `Ctrl+4` | Unassigned |
| Workspace 5 | `Ctrl+5` | Unassigned |
| Workspace 6 | `Ctrl+6` | Unassigned |
| Workspace 7 | `Ctrl+7` | Unassigned |
| Workspace 8 | `Ctrl+8` | Unassigned |
| Workspace 9 | `Ctrl+9` | Unassigned |
| Workspaces and tabs | `Ctrl+Shift+W` | `w` |
| Select tab by number (9: last tab) | `Alt+1` … `Alt+9` | Unassigned |

<!-- end of default bindings -->

## Saved bindings

Fly saves only the bindings that differ from the defaults, in the `fly.keyboard`
entry of the profile's `Local State` file. Actions you have not changed follow
the defaults of the Fly version you run, so later default improvements still
apply. An empty list disables an action. Your bindings take precedence: a
default that would collide with one of them gives up only the colliding chord.
If one of your bindings uses a default tab-number chord, tab numbers are off
until you choose a modifier. A tab modifier you choose keeps its nine chords
too: with **Tab numbers** set to Ctrl, Ctrl+1 to Ctrl+9 select tabs and the
default workspace shortcuts on those chords are off until you bind them to
other keys.

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
