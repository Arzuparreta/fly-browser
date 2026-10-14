const fs = require('node:fs');
const path = require('node:path');
const keybinds = require('../keybinds');
const keyboard = require('../keyboard');
const config = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA).config;
const format = (values, scope) => values.length ? values.map(value => '`' + keyboard.display(value, scope).replace(/\|/g, '&#124;') + '`').join(' / ') : 'Unassigned';
let document = `# Keyboard controls

Open **Preferences → Keyboard**, or press **Ctrl+,** with a new profile. The editor has its own Preferences section with a pinned search bar and save bar. Search by action, category or binding. Each action shows two expandable menus side by side, **Browser shortcuts** and **Page keys** (active only when not typing), with both sets of bindings visible in their summaries. Expand either menu to edit it. Search and the customised filter check both groups. Each group supports up to eight bindings per action.

Select a binding to change it, **Add binding** to record another, or × to remove it. The recorder reports conflicts and offers an explicit reassignment with the affected action names. Select **Record a two-key sequence** for page sequences. Tab moves around the recorder; Escape cancels it.

**Save bindings** validates and applies changes to every open tab immediately. **Discard changes** restores the saved profile. **Reset** restores one action; **Restore browser shortcuts** and **Restore page keys** restore their respective groups. Import stages a profile for review; export writes the current draft's changes from the defaults. No restart or configuration-file editing is needed.

## Modes and text input

Fly has a small custom Vim-style controller, not the Vimium extension. Browser shortcuts work in text fields as well as elsewhere. Page keys only run in NORMAL mode, outside editable fields, explicit INSERT mode and link hints. Open shadow roots and composed input events are checked before consuming page keys. Custom element names do not imply editing; actual inputs, contenteditable regions and textbox/searchbox/combobox roles protect text entry. IME composition, dead keys and AltGr are left alone.

- **NORMAL:** page keys are available. Defaults include hjkl scrolling, d/u for half-page scrolling, gg for the top, and Shift+G for the bottom.
- **INSERT:** focus a text field or use the configured insert action. Escape leaves insert mode.
- **PASSTHROUGH:** all page keys and browser shortcuts go to the page except the configured passthrough toggle. Keep at least one browser binding for this toggle so you can return. The default is Shift+Escape.
- **Hints:** the custom hint overlay consumes its label alphabet, Backspace and Escape. Browser shortcuts retain precedence. Disabled and inert controls are excluded. Hint letters are temporary labels, not action bindings.

Escape and Tab are reserved for cancellation and focus navigation. Native control keys such as arrows, Enter, Space and Delete remain contextual UI behaviour. Configured page keys also work from non-editable browser navigation controls and the empty workspace, outside browser panels. They use the same saved assignments as the page; there is no hidden set of sidebar shortcuts. Tab stays within visible overlays; arrow keys navigate tab lists, dashboard selections and bookmark columns.

Alt+S (sidebar focus) opens and focuses the tab list; pressing it again from any sidebar control returns focus to the active page and collapses an unpinned sidebar, including retained pointer expansion. In an empty workspace it releases sidebar focus. Pinning remains unchanged: a pinned sidebar stays open. Custom bindings and the NORMAL-mode S action use the same behaviour. In the tab list, the configured scroll-down/up actions (j/k by default) move the cursor without activating or waking tabs. Enter/Space activate and retain sidebar focus; scroll-right (l) or Right activates and focuses the page. The close and yank actions target the highlighted tab. Fixed arrow/Delete keys retain their native UI roles. Ctrl+1 through Ctrl+8 activate the numbered tab; Ctrl+9 selects the ninth tab, falling back to the last when fewer than nine exist. Bare 1–9 do the same from the sidebar unless assigned as page keys. Existing Alt+1 through Alt+8 shortcuts remain available, and Alt+9 always selects the last tab.\n\nOpening address/search, find, dashboard, preferences, palette, bookmarks, history, notes or diagnostics closes the previous browser panel; the exception is find inside Preferences, which focuses the settings search. Escape closes the current panel and returns focus to the page; pressing its opening shortcut again also closes it. Ctrl+L can be cancelled even after focus moves to another browser control. In the dashboard, Escape first cancels workspace creation or renaming, then closes the dashboard. The binding recorder and native dialogs keep their own cancellation. Notes keep edits on closing; delayed data loads cannot reopen closed panels or take focus from a newer panel. Sharing accepts Escape from its controls and embedded page, except in page passthrough mode.

## Preferences

The Preferences index on the left lists its sections. When an index entry has focus, ↑/↓ move between sections (wrapping at the ends), Home/End jump to the first or last, and Tab enters the selected section. Inside Preferences, / or the configured find shortcut (Ctrl+F by default) focuses **Search settings** instead of the page find bar. Search filters every section in place, matching labels, descriptions, option names and common synonyms; matching keyboard actions are counted, and **Show in Keyboard** opens the editor with the same filter. Enter moves to the first result. Escape clears a non-empty search, then closes Preferences.

Browser chords also work when an iframe has focus. Vim-style page keys and hint discovery are confined to the main document and its open shadow roots; embedded frames retain their native keyboard navigation. Closed shadow roots cannot be inspected. Operating-system shortcuts may be intercepted before Electron receives them.

## Default bindings

Existing legacy profiles retain their explicit assignments, including empty and alternate bindings. Values that an earlier release wrote into the file as its own defaults are not assignments and are replaced by the current defaults. Newly introduced defaults never take over a legacy shortcut the user changed. The table below describes new-profile defaults and is generated from the action catalogue.

| Action | Browser shortcuts | Page keys |
| :--- | :--- | :--- |
`;
for (const [id, info] of Object.entries(keybinds.ACTION_METADATA).sort((a,b) => a[1].category.localeCompare(b[1].category) || a[1].label.localeCompare(b[1].label))) {
    document += `| ${info.label} | ${format(config.shortcuts[id])} | ${format(config.normal[id], 'normal')} |\n`;
}
document += `
## Profile format

Bindings remain local in \`~/.config/fly-browser/keybinds.json\`. Version 2 separates browser and page bindings and stores only changes from the defaults:

\`\`\`json
{
  "version": 2,
  "changesOnly": true,
  "shortcuts": {"spawn-tab": ["Ctrl+Shift+Y"], "reload-active-tab": []},
  "normal": {"scroll-down": ["Z", "ArrowDown"]}
}
\`\`\`

Omitted actions inherit the current defaults, so later default improvements still apply. An empty array explicitly disables an action in that section. Listed actions take precedence: a default that would collide with one of them loses only the colliding chord instead of rejecting the profile. Files without \`changesOnly\` come from releases that also stored unchanged defaults; values matching what those releases wrote by default are treated as unchanged. Legacy flat action tables are accepted and migrated on the next successful save. Values use Ctrl, Alt, Shift and Meta; modifiers are normalised, while visible punctuation and the user's keyboard layout are preserved. Use Plus for the plus key. Sequences have two strokes, separated by a space, and a 700 ms interval.

Duplicate assignments, ambiguous sequence prefixes, unknown action IDs and unreachable page bindings are rejected. Saving replaces the file atomically before changing the active map; a failed write leaves the previous file and bindings intact. An invalid file at startup is left untouched, logged and reported in settings, with defaults used for that run.

Closed-tab recovery keeps at most twenty URLs in memory, restores the original workspace when it still exists, and clears its buffer when private mode changes. It does not restore a closed tab's navigation history or persist recovery data.

## Development

The action catalogue and defaults live in \`keybinds.js\`. \`keyboard.js\` is the dependency-free grammar used by Node and the renderer. Electron's sandbox cannot require local files, so \`scripts/sync-keyboard.js\` embeds an identical generated copy in the guest preload. Keep the sandbox and isolation settings enabled.

After changing the grammar or defaults:

\`\`\`sh
node scripts/sync-keyboard.js
node scripts/generate-keybind-docs.js
npm test
\`\`\`

The test suite checks that generated code and this reference have not drifted. Browser and guest maps are compiled only when configuration changes. Ordinary key presses do not write files or fetch bindings over IPC. Two-key prefixes use a precompiled set and a timestamp, with no background timer.
`;
const file = path.join(__dirname, '..', 'docs/reference/keybinds.md');
if (process.argv.includes('--check')) {
    if (fs.readFileSync(file, 'utf8') !== document) throw new Error('Run node scripts/generate-keybind-docs.js after changing bindings');
} else fs.writeFileSync(file, document);
