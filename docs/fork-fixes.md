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

## Automatic personal work log · 13 October 2026

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| WF-01 | Completed work could stop after a local commit or reach personal directly without a PR entry. | Repository instructions now require an automatic commit, fork push, PR against personal and merge for every completed task, followed by returning the checkout to personal. Validation failures remain explicitly recorded. | Reviewed the instructions against the owner's standing authorisation and exercised this workflow with its own documentation PR. |

The extensions integration at `1e71460` was already present on `fork/personal` before this workflow update. This PR records that integration retrospectively through EXT-01 and EXT-02 above; its code was already merged and cannot produce a new diff against personal.

## Browser panel states and cancellation · 13 October 2026

Implementation and validation: [PR #4](https://github.com/Arzuparreta/Mise-browser/pull/4), targeting the fork's `personal` branch.

This pass fixes inherited UI defects and gaps left in the fork's keyboard work. No default binding changes or new dependencies are introduced.

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-01 | Opening Ctrl+L from the sidebar could send focus back to the tab list; Escape only worked when the address input received it. Pending history suggestions could appear after closing or changing the query. | Sidebar focus accepts the address controls. Escape cancels an open address bar from browser chrome; deferred focus and suggestions check the current state and query. Workspace address commands also return page focus. | Live Electron opens Ctrl+L from the tab list, cancels from the input and a different control, and checks navigation/page hints. |
| UX-02 | Panels used incomplete, duplicated conflict lists, including obsolete help flags. Preferences could hide the dashboard while leaving its state active and its guests hidden. Closing one panel during another opening could focus the guest in the middle of the transition. | Shared opening/closing callbacks keep the state and visibility together for address, find, dashboard, preferences, palette, bookmarks, history, notes, diagnostics and sharing. Guest focus is suppressed during transitions; deferred tab focus respects all active panels and native dialogs. | Live Electron checks 56 ordered transitions among the eight primary panels, repeated-shortcut closure and page focus, plus diagnostics/sharing checks. Unit tests check transition suppression. |
| UX-03 | History had no Escape exit; notes only closed from the textarea and preview could leave focus on the underlying page. Workspace creation could be opened inside a hidden dashboard, and Escape from its buttons closed the parent too. | Unhandled Escape closes the focused panel. Notes focus visible controls. Workspace creation opens its dashboard; child cancellation leaves the parent open and dashboard closing clears the child. | Live Electron checks history, notes preview/editing and cancellation from the workspace Save button. Recorder cancellation remains covered by the existing Electron suite. |
| UX-04 | Notes reads could reopen a closed panel, replace newer edits or save an empty field before the old content loaded. Palette preparation could reopen after cancellation or clear text typed while loading. Preferences and diagnostics could refocus after closing. | Notes reads belong to one opening and do not overwrite edits; closing saves only loaded/edited notes and cancels pending autosave. Palette requests validate the current opening and retain current input. Preferences/diagnostics focus before loading, without delayed refocus. | Deterministic delayed-read tests cover notes cancellation, close/reopen, typing during loading and avoiding destructive saves, plus palette cancellation, reopening and preserving input. Live Electron verifies notes persistence and fast panel transitions. |
| UX-05 | Sharing did not focus its controls or restore page focus, and Escape inside its guest never reached the host listener. Reopening accumulated host listeners. | One close lifecycle destroys the transient guest, removes its listener and restores page focus. The guest reports Escape to its owning share overlay; passthrough keeps its normal semantics. | Live Electron closes from host controls and an editable share guest, checks destruction and page focus. |
| UX-06 | The navigation UI test clicked coordinates underneath the expanded sidebar, preventing editable-page verification. | The local fixture places its input beyond the sidebar and clicks that field. | Full live navigation suite exercises real text input without bypassing pointer hit testing. |

### Verification and limits

Run `npm test`, `npm run test:keyboard-ui` with Playwright available, `npm run docs:build`, modified JavaScript syntax checks and `git diff --check`. The UI tests use local fixtures and disposable homes/profiles on Electron under Linux/X11. Generated grammar and binding reference checks remain enabled. This pass does not establish exhaustive compatibility with arbitrary websites, operating-system interception, cross-origin frame editing, closed shadow roots or native menu focus. Downloads remains a non-modal shelf; page INSERT, hints and PASSTHROUGH retain their own keyboard states. Sharing Escape in PASSTHROUGH belongs to the page until its recovery chord is used.

Validation results for this pass: `npm test`, the complete `npm run test:keyboard-ui` suite, `npm run docs:build`, modified JavaScript syntax checks and `git diff --check` passed. The initial navigation UI baseline failed because its pointer target was covered by the expanded sidebar (UX-06); the corrected fixture passes. No validation failures remain at submission.

## Sidebar stacking above browser content · 13 October 2026

Implementation and validation: [PR #5](https://github.com/Arzuparreta/Mise-browser/pull/5), targeting the fork's `personal` branch.

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-07 | The auto-collapsing sidebar expanded to 260 px, but the empty-workspace panel (`z-index: 100`) and other content layers could paint above the sidebar (`z-index: 90`). Only its 36 px strip remained visible and its expanded controls could not receive clicks. This was inherited CSS behaviour. | The positioned right-content panel now establishes its own stacking context at `z-index: 0`. Content and overlay layers stay inside that context, below the sidebar, while retaining their ordering within the panel. | The live Electron regression fails before the fix on the expanded sidebar hit target. After the fix it checks hover expansion over a live guest and an empty workspace, clicks the workspace label, checks keyboard expansion and pinned layout, and verifies an expanded-sidebar screenshot. |

Validation uses Linux/X11 Electron with a disposable home/profile. Run `npm test`, the complete `npm run test:keyboard-ui`, `npm run docs:build` and `git diff --check`; default bindings and generated grammar/reference remain unchanged and drift-checked. An initial full UI rerun also timed out in the pre-existing rename cancellation check; the isolated rerun passed, and that check now waits for the rename input to receive focus before sending Escape. Native compositor behaviour on Wayland and other operating systems is not verified by this pass.

Final validation: `npm test`, the complete live Electron UI suite, documentation build and whitespace checks passed. Screenshot inspection confirms the full sidebar paints above the empty-workspace content.

## Sidebar retention at window edges · 13 October 2026

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-08 | The inherited sidebar used CSS `:hover` for expansion. Electron losing pointer hover outside the window immediately collapsed it, and alternating between the outer left pixel and the window caused repeated expansion and contraction. | Retain pointer presence until the mouse enters another part of the window. All sidebar layout and control selectors use the same retained class; keyboard expansion, focus and pinning remain independent. Guest preload reports trusted document entry once per entry, so returning directly into an embedded page clears retention too. No timers, polling, movement listeners or dependencies are added. | The live Electron regression fails before the fix at the left-edge exit. It checks repeated crossings between x = -1 and x = 0, top/bottom exits, direct guest re-entry, empty-workspace exit/re-entry, keyboard expansion and pinned layout. |

Validation uses Electron on Linux/X11 with a disposable home/profile. Guest entry is injected through Electron's `sendInputEvent`, since host CDP mouse movement does not route into the separate guest surface; a host-only test initially timed out there. Run `npm test`, the complete `npm run test:keyboard-ui`, `npm run docs:build`, modified JavaScript syntax checks and `git diff --check`. Native compositor edge handling on Wayland and other operating systems remains unverified.

Final validation: all listed checks passed, including the Electron recorder and editable-page checks. No validation failures remain at submission.

## Unified sidebar pin and expansion · 13 October 2026

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-09 | Inherited shortcut and button paths used different states: Ctrl+Shift+Z toggled temporary `expanded`, while the button toggled persistent pinning. The shortcut never updated the pin highlight. Unpinning retained button focus and could retain temporary expansion, so leaving the sidebar did not collapse it; generic hover/focus styling also resembled a selected pin. | Both action ids and the legacy expansion API use the same pin toggle. One mode setter synchronises layout, highlight, `aria-pressed` and Preferences, clears temporary expansion and releases sidebar focus on unpinning. Pin highlight reflects pin state; keyboard focus has a separate outline. Remove the duplicate Zen palette entry and unused hiding implementation, preserving existing shortcut ids. Save only the changed setting through the backend merge, avoiding stale full-config writes. | Electron regressions alternate shortcuts and clicks, check highlight removal under the pointer, persistence, contraction without content clicks, temporary keyboard expansion and compatibility aliases, with a live page and an empty workspace. Palette tests verify one entry with compatible ids. |

Validation uses Linux/X11 Electron with disposable homes/profiles. Required checks: `npm test`, Electron recorder and editable-page tests, sidebar/overlay regressions, documentation build, modified JavaScript syntax and `git diff --check`. Default chords and guest grammar remain unchanged; the generated keyboard reference reflects pin/unpin semantics. Wayland and other operating systems remain unverified.

During implementation, an initial UI launch exposed an alias initialisation error, corrected by using a hoisted function export. The first pin regression attempts required adjustment for guest pointer injection and CSS transition completion. A concurrent UI rerun also timed out at the existing share-overlay cancellation check; the isolated run passed. The catalogue coverage assertion was updated to recognise a single palette entry exposing compatible ids. Final validation results are recorded below.

Final validation: `npm test`, the complete `npm run test:keyboard-ui` suite, documentation build, modified JavaScript syntax and `git diff --check` passed. No validation failures remain at submission.

## Sidebar history button focus · 13 October 2026

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-10 | Inherited back/forward click handlers left focus on the sidebar button. Its focus highlight resembled a persistent selection, and `:focus-within` prevented the unpinned sidebar from collapsing when the pointer returned to content. | Pointer activation releases button focus and returns focus to the active page. Keyboard activation retains button focus and sidebar expansion. Empty workspaces release button focus even without a page to focus; pinning continues to keep the sidebar open. | The live Electron regression fails before the fix because a pointer back action leaves the button focused. It covers back/forward history destinations, contraction after host content entry, Enter/Space focus retention, pinned layout and empty workspaces. The existing overlay regression covers guest pointer entry. |

Validation uses Linux/X11 Electron with disposable homes/profiles. Required checks include `npm test`, the full Electron keyboard/recorder/editable-page and overlay suites, documentation build, modified JavaScript syntax and `git diff --check`. Default bindings and generated grammar/reference are unchanged and drift-checked. Wayland and other operating systems remain unverified.

Initial UI runs timed out in guest pointer injection immediately after history navigation and failed existing hint checks, including during concurrent execution. The history regression now exits through real host content and runs after the existing navigation scenarios; the overlay suite independently checks trusted guest entry. The final sequential full suite passed.

Final validation: `npm test`, the complete `npm run test:keyboard-ui` suite, documentation build, modified JavaScript syntax and `git diff --check` passed. No validation failures remain at submission.

## Sidebar shortcut focus toggle · 13 October 2026

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| UX-11 | The inherited sidebar focus action (Alt+S in new profiles) cycled between the tab list and navigation controls. Focus remained inside the sidebar, so a second press never collapsed it. | Keep the action id and bindings, but toggle focus between the sidebar and the active page. Running it from any sidebar control clears temporary and retained pointer expansion, releases focus even in empty workspaces, and returns focus to the active guest when available. Persistent pinning remains unchanged. Ignore pointer transitions between sidebar children so contraction cannot immediately restore retained hover. Update the catalogue, palette description and generated keyboard reference. | The live Electron regression fails before the fix on the second Alt+S press. It checks repeated presses from the tab list, navigation and bottom controls, retained pointer expansion, pinned and empty workspaces. The full keyboard suite covers recording and editable-page input; the sidebar regression also checks returning to a focused text field without changing its text. |

Validation uses Linux/X11 Electron with disposable homes/profiles. Default chords and guest grammar remain unchanged and are drift-checked. Pinning deliberately keeps the sidebar visible; subsequent pointer entry can expand it again. Wayland and other operating systems remain unverified.

An initial post-fix hover regression exposed internal mouseover events restoring expansion during contraction. Restricting retained pointer updates to sidebar boundary transitions fixes it; the isolated overlay suite then passed.

Final validation: `npm test`, the complete `npm run test:keyboard-ui` suite, documentation build, modified JavaScript syntax and `git diff --check` passed. No validation failures remain at submission.

## Numbered tab shortcut dispatch · 14 October 2026

| ID | Original behaviour | Fix | Validation |
| :--- | :--- | :--- | :--- |
| KB-24 | Alt+1 through Alt+9 selected a tab, then unexpectedly opened the Command Centre with the Extensions category. The renderer dispatcher default branch lacked a break before the extension action case, so fallback and unknown actions also opened the palette, including in empty workspaces. | Terminate the default branch after numbered tab selection or fallback dispatch. Preserve explicit extension palette opening, existing bindings, last-tab selection and out-of-range clamping. | The dispatcher regression fails before the fix because tab actions open Extensions. It covers all nine actions with zero, three and ten tabs, fallback and unknown actions, and explicit extension opening. Disposable-profile Electron tests exercise Alt+1 through Alt+9, an empty workspace and explicit extension opening alongside the recorder and editable-page checks. |

Default bindings, guest grammar and the generated keyboard reference are unchanged and drift-checked. Linux/X11 is verified; Wayland and other operating systems remain unverified. The pre-fix regression failure is intentional evidence of the defect.

Initial live runs timed out in the workspace-transfer check and in the new tab-selection fixture. Tab creation now waits for each asynchronous render. An extra extension-category chip assertion was removed because a disabled-extension profile does not provide that chip; explicit palette opening remains checked in Electron and its category argument is checked by the dispatcher regression.

The sequential UI run passed the keyboard/recorder/editable-page and numbered shortcut checks, then timed out clicking the existing navigation suite back button because sidebar elements intercepted the pointer. Navigation and overlay checks are rerun separately to distinguish fixture behaviour from the dispatcher change.

Final validation: `npm test`, documentation build, modified JavaScript syntax and `git diff --check` passed. The keyboard UI suite passed with recorder, editable-page and numbered shortcut checks. Navigation and overlay UI suites passed on the isolated rerun; the earlier sequential navigation pointer timeout remains recorded above.
