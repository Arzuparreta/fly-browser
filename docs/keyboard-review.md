# Keyboard control coverage review

Reviewed the action catalogue, renderer keyboard listeners, sandboxed guest preload, custom hinter, native reader, navigation/sidebar controls, all overlays and the existing command palette. The controller is implemented by Mise itself; no Vimium extension is embedded.

## Control surfaces

| Surface | Keyboard route | Personalisation and safeguards |
| :--- | :--- | :--- |
| Address/search | Address action, native text editing, arrow/Enter suggestions, Escape | Assignable action; submitting or closing the address bar returns focus to the selected guest, while native field keys retain their meaning. |
| Tabs | New/close/duplicate/reopen, next/previous, numbered selection, reorder, pin, mute | All assignable; repeats suppressed for destructive actions; pin boundary maintained. |
| Workspaces | Dashboard, create, previous/next, destination chooser; native rename/delete buttons | Assignable actions and native controls; transfers reload in the destination container. |
| Page navigation | Back/forward, reload/force reload/stop, scroll directions/half pages/top/bottom | Browser and page bindings; editable/shadow-root checks; compiled sequence prefixes. |
| Page interaction | Hints, find, reader, Dark Reader, copy URL, zoom, print, site shields | Assignable actions; hint labels and native controls are contextual. PDF URL copying retains the original document URL. |
| Modes | Insert, Escape cancellation, passthrough/recovery | Assignable entry/toggle; at least one browser recovery chord required. Guest mode is retained per tab. |
| Split view | Side-by-side/stacked, orientation, focus, swap, close | Existing distinct palette actions are assignable; native tab selection remains available. |
| Bookmarks/quickmarks | Open/add/set/jump actions, arrows, Enter/Delete, search, native buttons | Configurable actions; Tab reaches controls rather than switching columns. |
| History | Open, native result buttons, search, purge and close | Configurable opener; results work with Tab and Enter/Space. |
| Notes/downloads | Open/toggle actions, editor and native action buttons | Customisable openers, normal text editing and focus containment. |
| Preferences | Direct keyboard-settings action, vertical section index (↑/↓/Home/End with roving Tab stop), / or find shortcut for settings search, native fields/buttons, Escape (clears search first) | One section shown at a time; search filters all sections in place. Search/filter/record/reassign/reset/import/export/save without text-file editing. Drafts survive closing settings. |
| System/performance | Full screen, window controls, themes, notifications, clock, diagnostics, hibernate/wake, user-content actions, updates | Distinct existing palette actions are exposed in the binding catalogue. |
| Embedded frames | Native browser chord interception and native frame navigation | Page-modal keys and hint discovery stay in the main document; security isolation is retained. |

Native UI semantics are intentionally stable: Tab, Escape, Enter, Space, arrow navigation and contextual Delete are not a second hidden action-binding system. Unmodified page keys belong to NORMAL mode; browser shortcuts require a modifier or function key. Browser shortcuts also apply while editing text, so users should choose clipboard/editor chords deliberately.

## Configuration and performance contract

`keybinds.js` defines actions and defaults. `keyboard.js` defines normalisation and validation. The renderer editor and generated sandboxed preload use that same grammar. Native guest interception uses the same compiled browser map and keeps passthrough state per webview. Every ordinary key is matched locally; configuration is sent at initialisation and after a save, rather than fetched per key press.

Version 2 stores separate browser/page tables. Eight alternatives per action and two strokes per page sequence are bounded. Duplicate/prefix conflicts and browser-shadowed page keys are rejected. Save commits to disk before activating the new map; errors retain the user's draft. Old flat profiles retain explicit assignments without silently restoring alternates. No dependency, polling loop or sequence timer is introduced.

The canonical action catalogue supplies the editor and any missing command-palette entries. Tooltips and welcome badges use active bindings. The generated reference and guest grammar are checked by the test suite to prevent drift.

## Acceptance

Automated checks cover configuration and guest/native dispatch, plus a live Linux/X11 Electron flow with a disposable profile. The user's next step is to choose preferred bindings in the editor. Exporting that profile gives a reviewable source for updating future new-profile defaults; existing user assignments remain independent.

See [the generated reference](./reference/keybinds.md) for all actions and defaults, and [the fork fixes ledger](./fork-fixes.md) for each inherited defect, its repair and validation limits.

To run the optional UI suite, make Playwright available to Node or set `MISE_PLAYWRIGHT_MODULE` to its installed module path, then run `npm run test:keyboard-ui`. It needs an Electron binary and a display. `MISE_TEST_OZONE_PLATFORM` selects the display backend; the default is `x11`. `MISE_KEYBOARD_SCREENSHOT` optionally saves the editor screenshot. The suite sets Electron's home and user-data paths to temporary directories and removes them after the run.
