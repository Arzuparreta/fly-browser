# Fork fixes ledger

Record every solved problem here, including defects inherited from upstream. Each PR must list its applicable entries in the **Fork fixes** section of the PR template. Add the PR link when one exists; do not mark an unmerged PR as repository truth.

## Keyboard control and personalised bindings · 13 October 2026

Implementation and validation: [PR #1](https://github.com/Arzuparreta/Mise-browser/pull/1), targeting the fork's `personal` branch. It replaces the separate chord and hard-coded page-key paths with a shared grammar and versioned configuration. The page controller is custom Mise code, not Vimium.

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| KB-01 | Help documented `d` as half-page scrolling; the guest used it to toggle Dark Reader. | `d` scrolls half a page by default. Dark Reader has its own configurable action. | Guest-handler regression test executes the real handler and checks the scroll amount. |
| KB-02 | Vim-style page actions, mode entry and `gg` were hard-coded separately from configurable chords. | Browser and page bindings share one grammar and action catalogue, including configurable two-key sequences and insert/passthrough entry. | Grammar and guest-handler tests cover custom keys, sequences, interruption and expiry. |
| KB-03 | F9 existed in native interception, the guest handler and reader view. Removing its configured binding left hard-coded behaviour behind. | Reader shortcuts come from the registry; Escape remains native cancellation. Reader event listeners are removed when the overlay closes. | Unbinding F9 is tested against the actual guest handler; reader source and syntax checks. |
| KB-04 | Duplicate bindings silently overwrote one another; malformed combinations and unknown action IDs could be saved. | Validation rejects duplicates, ambiguous prefixes, unsupported values, unknown actions and page bindings blocked by browser chords. | Positive and negative configuration tests. |
| KB-05 | Saving changed the active bindings before writing succeeded and used a direct write. | Atomic file replacement completes before active maps change. Failed saves preserve both disk contents and runtime bindings. | Simulated rename failure, file comparison, active-map comparison and temporary-file cleanup. |
| KB-06 | Legacy string overrides retained unwanted default alternates; empty values could leave secondary shortcuts active. | Explicit strings, arrays and empty assignments are respected. New defaults cannot take over legacy chords. Invalid startup files remain untouched and show a settings warning. | Legacy-profile migration, disabled binding, round-trip and defensive-copy tests. |
| KB-07 | Preferences only displayed bindings and required file editing. | Searchable editor with key recording, explicit conflict reassignment, alternate bindings, removal, reset, discard, import/export and live save. | Live Electron editor checks; screenshot reviewed for contrast, layout and focus visibility. |
| KB-08 | The main process consumed browser chords even when the guest was in passthrough mode. | Passthrough sends all keys to the page except its configured recovery chord. Its state survives tab switches and same-document navigation. | Native-routing and guest-mode restoration tests; live Electron passthrough check. |
| KB-09 | The mode bar reset to NORMAL on tab switches even when the guest remained in INSERT or PASSTHROUGH. | Guest mode indicators are stored per webview and restored on selection. | Controller tests switch between guests in different modes. |
| KB-10 | Settings, help, tooltips, welcome cards and palette tips advertised hard-coded or obsolete bindings. | Runtime hints use the saved bindings; misleading fallback tips are removed. The reference is generated from the catalogue. | Generated-reference drift check, UI inspection and documentation build. |
| KB-11 | There were no assignable actions for tab cycling/selection/reordering/reopening, workspace switching, force reload, stop, zoom and several existing palette actions. | The catalogue exposes these actions and every distinct existing palette action. Duplicate aliases are removed. New-profile defaults use conventional close/find/bookmark/tab-cycle chords. | Default conflict validation, action coverage review and Electron smoke checks. |
| KB-12 | Bookmarks and sidebar navigation intercepted Tab, skipping controls. History results and several chrome controls were click-only. | Native Tab navigation, overlay focus containment, keyboard history buttons, and keyboard activation for workspace, clock, welcome and status controls. Contextual arrows/Enter/Delete remain available. | Live Electron focus check and source review. |
| KB-13 | Sidebar letters formed another hidden hard-coded set of browser actions. | Browser actions come from personalised shortcuts; sidebar navigation uses native arrows, Enter and Delete. | Source review and syntax checks. |
| KB-14 | Hint input consumed composition, dead keys, repeated keys and unmatched modifier chords; disabled controls could get labels. | These inputs are left alone, and disabled/inert controls are excluded. | Source review, shared composition/AltGr tests and syntax checks. |
| KB-15 | Scrolling inspected only the top-level active element, missing ancestor scrollers around shadow-root focus. | Scrolling walks from deep focus through scrollable ancestors and uses the selected container's height for half-page movement. | Guest-handler scroll tests and source review. |
| KB-16 | Keybinding mutation IPC lacked sender checks, and an unused action-bubbling channel accepted arbitrary actions. | Binding mutation and action dispatch require the browser's main frame. Actions must exist in the catalogue. The unused channel is removed; guests receive only the public binding table. | Boundary review and live Electron saving with context isolation and sandbox retained. |
| KB-17 | Saving disabled the focused button, allowing focus to drop outside settings and leaving Escape unreliable. Palette actions also moved focus back to the page after opening preferences. | Save/discard keep focus on a stable settings control. Escape closes preferences, and opening an overlay retains its focus. | Live Electron save, Escape and direct-keyboard-settings checks. |
| KB-18 | Moving a tab across workspaces required dragging. The two drag paths disagreed, and the workspace-header path reused a live webview from the source container. Several tab metadata arrays were omitted. | Keyboard destination chooser and both drag paths share a transfer function. The source webview is destroyed, destination views use their own partition, and tab metadata, pin order, history indices and split indices move together. | Transfer-state regression tests and live Electron workspace transfer check. |
| KB-19 | A delayed tab-focus callback dereferenced a mutable array index after a tab was moved or closed, causing errors or focusing another page. | Focus callbacks retain the intended view and verify its connection, workspace and current selection before focusing. | Live Electron transfer flow caught the regression; rerun verifies no renderer errors. |
| KB-20 | Palette actions and shortcuts used different dispatch paths; the menu action opened settings from a key but toggled visibility from the palette. New actions were absent from fixed category filters. | The palette and shortcuts share canonical dispatch. Opening native settings and toggling menu visibility are distinct actions. Filters and browser/page-key badges follow the catalogue. | Catalogue coverage checks and live Electron palette/settings flow. |

New capabilities include a bounded, in-memory closed-tab URL buffer, direct keyboard-settings access and a workspace destination chooser. These are distinguished from the inherited defects above so PR reviewers can assess both.

### Verification and limits

- `npm test`: existing security, audio, pinning and configuration tests, plus grammar, migration, atomic writes, actual guest modal handling, native chord routing, mode restoration, workspace transfers and generated-file drift checks.
- `npm run test:keyboard-ui`: optional live Electron suite using Playwright and a disposable home/profile. Checks recording safety, conflicts, saves, page sequences, discard, focus, invalid imports, text entry, customised page scrolling, passthrough, iframe browser chords and workspace-container transfer.
- Modified JavaScript syntax and `git diff --check` must pass. Documentation builds with VitePress.
- Main-document page controls and open shadow roots are supported. Cross-origin frames and closed shadow roots retain their native controls; hint discovery does not cross those boundaries. Native browser chords reach focused frames. Window-manager interception and arbitrary live-site behaviour require user acceptance.
- Closed-tab recovery restores URLs and workspace choice, not full navigation history. Its buffer is volatile and clears on private-mode changes.
- Native print dialogs, all operating-system key combinations, every remote site, Windows and macOS have not been exhaustively exercised by the automated checks.

## Fork development workflow · 13 October 2026

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| KB-21 | Upstream AI-development rules prevented completing commits and pushes in the fork, contrary to the fork owner's working process. | Removed both AI Policy documents and their README, contribution, PR-template and documentation-navigation references. Fork instructions now follow the owner's authorised workflow. Browser security and privacy requirements remain in place. | Reference search, documentation build and explicit owner authorisation to commit, push, create a PR and merge. |
| KB-22 | The fork had release and documentation workflows but no pull-request verification for browser changes. | Added a least-privilege verification workflow for pull requests and main/personal pushes, covering regression tests, generated-file drift, syntax and documentation builds. | Local checks plus the GitHub Actions verification run before merge. |

## Address-bar search focus · 13 October 2026

Implementation and validation: [PR #2](https://github.com/Arzuparreta/Mise-browser/pull/2), targeting the fork's `personal` branch.

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| KB-23 | Submitting a search with Ctrl+L changed the guest URL and hid the address input without returning host focus to the webview. The host active element could remain BODY, so page-only f never reached the guest even with the hint engine ready. Cancelling or toggling the bar closed had the same missing handoff. | Return focus to the selected webview after submitting navigation or closing the address bar. Keep bare page bindings scoped to the guest and preserve text-field behaviour. No load-completion autofocus, timer or new key interception is added. | A disposable Electron search fixture fails before the fix with host BODY and no hints, then passes three consecutive searches with host WEBVIEW and visible hints. Additional checks cover Escape/Ctrl+L close, ordinary f typing, slow navigation while Preferences is open, and a background result after switching tabs. Unit tests cover the search route and waking a guest. |

The test substitutes a local search endpoint for DuckDuckGo to exercise the browser's exact Ctrl+L search path reproducibly. It does not claim exhaustive verification of DuckDuckGo's live page variants.

## Opt-in extensions integration · 13 October 2026

Merged `fork/feat/opt-in-extensions` into `personal`, preserving the personalised keyboard catalogue and address-bar focus fixes.

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| EXT-01 | The browser could not load user-installed Chrome extensions. | Optional unpacked extensions are scoped to opted-in workspaces, loaded lazily, excluded from private mode and unloaded after a grace period. Preferences, status actions, popups and palette commands expose their controls. | Extension tests cover manifest validation, IDs, background types, path boundaries, workspace gating, private mode and lazy loading. |
| EXT-02 | The extensions branch predates personalised keyboard controls; merging its documentation and test runner would discard current behaviour or leave incompatible test expectations. | Retain both test suites, both renderer dispatch paths and both stylesheets. Register extension actions in the canonical catalogue without a default binding, regenerate the keyboard reference and correct the extensions guide's Dark Reader shortcut. Update isolated test fixtures for the extension notification import and unassigned-binding representation. | `npm test`, generated-file checks, JavaScript syntax, documentation build and whitespace checks; disposable Electron recorder and editable-page checks pass. |

Compatibility limits remain documented in [Extensions](guide/extensions.md#compatibility): declarative network rules, blocking webRequest, install events, side panels, native messaging and sync are limited or unsupported. Arbitrary third-party extensions have not been exhaustively verified by this merge.

The broader Electron navigation-focus suite passes three Ctrl+L search/hint cycles but times out twice at the slow-navigation, Preferences-close hint check (`tests/navigation-focus-ui.test.js:92`). That scenario remains an unresolved verification limit of this integration; the complete UI suite is not reported as passing.
