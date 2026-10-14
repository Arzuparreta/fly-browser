# Mise work log (Arzuparreta/Mise-browser pull requests #1–#21)

Before the browser was renamed Fly, its work log lived in pull requests on Arzuparreta/Mise-browser. That repository was deleted on 14 October 2026. This archive keeps each pull request's title, state, branches, merge commit, description and comments. Links to that repository point to this file or to the same commits in fly-browser.

- [#1](#pr-1) feat: personalised keyboard controls and complete action coverage (merged)
- [#2](#pr-2) fix: return page focus after Ctrl+L searches (merged)
- [#3](#pr-3) docs: automatic personal work log and extensions integration record (merged)
- [#4](#pr-4) fix: consistent panel states, Escape cancellation and focus recovery (merged)
- [#5](#pr-5) fix: keep the expanded sidebar above webview content (merged)
- [#6](#pr-6) fix: retain sidebar expansion outside the window (merged)
- [#7](#pr-7) fix: unify sidebar expansion, pin highlight and auto-collapse (merged)
- [#8](#pr-8) fix: release sidebar history button focus after pointer navigation (merged)
- [#9](#pr-9) fix: let Alt+S close the unpinned sidebar (merged)
- [#10](#pr-10) fix: prevent Alt+number from opening the Command Centre (merged)
- [#11](#pr-11) feat: add settings access and centre sidebar footer controls (merged)
- [#12](#pr-12) fix: release pointer focus across the sidebar (merged)
- [#13](#pr-13) fix: show both keyboard binding groups and reliable page keys (merged)
- [#14](#pr-14) feat: split Preferences into indexed sections (merged)
- [#15](#pr-15) feat: search Preferences and keep typing after closing a tab (merged)
- [#16](#pr-16) chore: sync upstream v0.15.0 and retain personal customisations (merged)
- [#17](#pr-17) fix: embed update notice beside sidebar date and time (merged)
- [#18](#pr-18) perf: audit personal against upstream and remove cold-start overhead (merged)
- [#19](#pr-19) fix(keybinds): treat seeded upstream defaults in legacy profiles as unassigned (merged)
- [#20](#pr-20) fix(keybinds): store only changed bindings so later defaults still apply (merged)
- [#21](#pr-21) chore: continue Mise as an independent repository (merged)

---

<a id="pr-1"></a>
## #1 feat: personalised keyboard controls and complete action coverage

- State: merged (2026-10-13); `codex/personalised-keyboard-controls` → `personal`; merge commit `079b538`

Mise's browser chords, hard-coded Vim-style page keys and command-palette actions previously disagreed. This change supplies one validated action catalogue and shared grammar for 96 actions, with live personalised bindings and a keyboard-operable editor.

The editor supports search, scope filtering, recording, alternate bindings, two-key page sequences, explicit conflict reassignment, reset, discard and import/export. Saves are atomic and apply to open tabs. Existing profiles retain explicit assignments. Missing tab/workspace actions include cycling, selection, reordering, reopening and a container-safe workspace destination chooser.

Upstream AI-development policies and their references are removed at the fork owner's explicit request. Fork instructions follow the owner's authorised workflow; security and privacy controls remain. PRs now have a fixes section and a verification workflow.

## Fork fixes

All inherited defects and new workflow repairs are listed below. The [persistent fixes ledger](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md) and [coverage review](https://github.com/Arzuparreta/fly-browser/blob/main/docs/keyboard-review.md) describe the scope and limitations.

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

| ID | Original problem | Result | Validation |
| :--- | :--- | :--- | :--- |
| KB-21 | Upstream AI-development rules prevented completing commits and pushes in the fork, contrary to the fork owner's working process. | Removed both AI Policy documents and their README, contribution, PR-template and documentation-navigation references. Fork instructions now follow the owner's authorised workflow. Browser security and privacy requirements remain in place. | Reference search, documentation build and explicit owner authorisation to commit, push, create a PR and merge. |
| KB-22 | The fork had release and documentation workflows but no pull-request verification for browser changes. | Added a least-privilege verification workflow for pull requests and main/personal pushes, covering regression tests, generated-file drift, syntax and documentation builds. | Local checks plus the GitHub Actions verification run before merge. |


## Verification

- `npm test` passes, including migration, invalid-profile recovery, failed-save rollback, IME/AltGr handling, actual guest modal dispatch, native/frame chord routing, mode restoration, workspace metadata/container isolation, catalogue coverage and IPC sender boundaries.
- Live Linux/X11 Electron suite passes with a disposable home/profile: recording safety, conflicts, saves, page sequences, discard, focus, invalid imports, ordinary text input, custom page scrolling, passthrough, native iframe chords, palette/settings execution and cross-workspace transfer. No renderer errors.
- Modified JavaScript syntax, generated grammar/reference drift checks and `git diff --check` pass.
- `npm run docs:build` passes. The editor screenshot was inspected.
- GitHub Actions verification must pass before merging this PR.

## Limits

Page-modal keys and hint discovery cover the main document and open shadow roots. Embedded frames retain native page controls; browser chords reach focused frames. Closed-tab recovery restores URLs/workspace choice, not full navigation history. OS shortcut interception, native print dialogs, all remote sites, Windows and macOS are not exhaustively tested.

The fork owner explicitly authorised commit, push, PR creation and merge. Target: `personal`, the tracked working branch for this checkout.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T17:33:53.980758Z">2026-10-13T17:33:53.980758Z</relative-time> | `3a14bc7` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-2"></a>
## #2 fix: return page focus after Ctrl+L searches

- State: merged (2026-10-13); `codex/search-navigation-focus` → `personal`; merge commit `93ceed6`

Searching through Mise's Ctrl+L address bar hid the input and changed the URL without returning host focus to the selected webview. The host could remain on BODY, so the guest-only f binding did nothing even when the hint engine was ready. Clicking the page restored it.

Return focus to the selected webview after submitting navigation, cancelling with Escape, or toggling the address bar closed. Bare page bindings remain scoped to the guest and ordinary text entry is preserved. The handoff happens on the explicit user action, with no load-completion autofocus, timer or additional key interception.

## Fork fixes

**KB-23 — Address-bar search focus:** before the fix, the same Ctrl+L → search → Enter → f flow reproduces host BODY/no hints; after the fix, three consecutive searches produce host WEBVIEW/visible hints without a mouse click. The regression and its limits are recorded in [the fork fixes ledger](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md), and the coverage review is updated.

## Verification

- `npm test` passes, including a new navigation-focus test covering URL submission, hiding the input before handing off focus, toggling the bar closed, and waking a guest.
- The full live Electron keyboard suite passes on Linux/X11 using disposable profiles.
- The new live search-flow regression fails on the original implementation and passes after the fix. It also checks Escape/Ctrl+L close, normal f typing inside a field, slow loading while Preferences is open, and a background result after switching tabs. No renderer errors.
- Modified JavaScript syntax, `git diff --check`, and `npm run docs:build` pass.

The live regression uses a local search endpoint in place of DuckDuckGo to reproduce the browser's exact Ctrl+L route deterministically. It does not claim exhaustive testing of DuckDuckGo's live page variants.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T18:06:09.325767Z">2026-10-13T18:06:09.325767Z</relative-time> | `ab34206` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-3"></a>
## #3 docs: automatic personal work log and extensions integration record

- State: merged (2026-10-13); `codex/personal-pr-log` → `personal`; merge commit `c260085`

## Description
Record the owner's standing workflow: every completed task for personal is committed, pushed to a fork branch, opened as a PR and merged automatically. Return the checkout to personal afterwards.

This entry also records the opt-in extensions integration retrospectively: commit 1e71460 was already on fork/personal before this PR, so its implementation is not part of this diff.

## Fork fixes
- [WF-01](docs/fork-fixes.md#automatic-personal-work-log--13-october-2026): completed work previously lacked a guaranteed PR entry; AGENTS.md now requires the full automatic workflow.
- Retrospective [EXT-01 and EXT-02](docs/fork-fixes.md#opt-in-extensions-integration--13-october-2026): opt-in workspace extensions and integration with personalised keyboard controls. Preserved both suites, renderer handlers and styles; regenerated keyboard documentation and updated test fixtures.

## Type of Change
- [x] Documentation update or correction

## Verification & Testing
- npm test, git diff --check and npm run docs:build pass.
- The preceding integration passed JavaScript syntax and disposable Electron recorder/editable-page checks.
- Remaining integration limit: navigation-focus-ui passed three search/hint cycles but twice timed out at line 92 after slow navigation and Preferences close. Full UI suite is not reported as passing. Third-party extension compatibility limits remain documented.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T18:32:14.784027Z">2026-10-13T18:32:14.784027Z</relative-time> | `5adea6f` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-4"></a>
## #4 fix: consistent panel states, Escape cancellation and focus recovery

- State: merged (2026-10-13); `codex/overlay-state-ux` → `personal`; merge commit `c2ef292`

## Description
Ctrl+L opened the address bar but sidebar focus could pull keyboard input away, making Escape ineffective. Browser panels also disagreed about which other panels to close and when to return focus to the page.

Centralise panel transitions and cancel unhandled Escape consistently. Opening another panel runs the previous panel's actual close lifecycle, keeps state and visibility together, and suppresses intermediate guest focus. Preserve notes and palette input during asynchronous loads; cancel child workspace editing before its dashboard. Sharing now closes from its embedded page and restores focus.

## Fork fixes
All fixes are recorded in the [browser panel states and cancellation ledger](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#browser-panel-states-and-cancellation--13-october-2026). These address inherited defects and gaps left in the fork's earlier keyboard work, with no new default bindings or dependencies.

- UX-01: Ctrl+L from the sidebar now retains address focus, Escape works from other chrome controls, stale suggestions stay hidden, and workspace address commands return page focus.
- UX-02: address, find, dashboard, preferences, palette, bookmarks, history, notes, diagnostics and sharing use consistent opening/closing state. Delayed tab callbacks respect open panels and dialogs.
- UX-03: Escape exits history and notes preview; workspace creation opens its parent dashboard, and cancelling creation/renaming retains parent focus.
- UX-04: cancelled loads cannot reopen notes/palette or take focus from a newer panel. Notes cannot overwrite existing data with an unread empty field; in-flight loads preserve typed input.
- UX-05: sharing has one close lifecycle, removes its host listener, destroys the transient guest and accepts guest Escape outside passthrough.
- UX-06: correct the existing navigation UI fixture whose pointer target was covered by the expanded sidebar.

## Related Issues
User-reported Ctrl+L/Escape failure and requested review of keyboard states and UX.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] Documentation update or correction

## Verification & Testing
- `npm test`: passed, including generated keyboard grammar/reference checks and deterministic notes/palette race tests.
- `MISE_PLAYWRIGHT_MODULE=/tmp/mise-merge-validation/node_modules/playwright npm run test:keyboard-ui`: passed on local Electron/X11 with disposable homes/profiles. Covers recorder conflicts/save/discard, editable-page input, custom page keys, passthrough, iframe chords, navigation focus, all 56 ordered transitions among eight primary panels, repeated-shortcut/Escape closure, child workspace cancellation, notes persistence and share guest destruction. No renderer errors.
- `npm run docs:build`: passed.
- Modified JavaScript syntax checks and `git diff --check`: passed.
- Initial navigation UI baseline failed due to sidebar pointer interception; the corrected fixture and full suite pass. No remaining validation failures.

- [x] Verified on local physical environment (automated live Electron on Linux/X11)
- [x] JavaScript syntax validated across modified files
- [x] Documentation compiled without errors
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project Security Policy

Limits: local fixtures do not prove compatibility with every website, window manager, closed shadow root or cross-origin frame editor. Downloads remains a non-modal shelf. Page INSERT, hints and PASSTHROUGH retain their own cancellation semantics; sharing Escape in PASSTHROUGH goes to the page until its recovery chord is used.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T18:49:14.938941Z">2026-10-13T18:49:14.938941Z</relative-time> | `1ea5a48` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-5"></a>
## #5 fix: keep the expanded sidebar above webview content

- State: merged (2026-10-13); `codex/sidebar-webview-stacking` → `personal`; merge commit `24a9e00`

## Description
The auto-collapsing sidebar expanded, but browser content could paint above it, leaving only the 36 px strip visible. This is reproducible in an empty workspace because its welcome panel has a higher z-index than the sidebar.

Establish a stacking context on the positioned right-content panel so its webviews, welcome panel and overlays retain their internal order below the expanding sidebar. Sidebar width, animation and keyboard bindings remain unchanged.

## Fork fixes
- [UX-07: Sidebar stacking above browser content](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#sidebar-stacking-above-browser-content--13-october-2026): inherited CSS allowed right-content children to outrank the sidebar; the right-content stacking context now contains those layers. Live Electron reproduces the failure before the fix and validates hover/keyboard expansion, pointer access and pinned layout afterwards.

## Related Issues
User-reported sidebar expansion behind the webview panel.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] Documentation update or correction

## Verification & Testing
- `npm test`: passed, including generated keyboard grammar/reference drift checks.
- Full `npm run test:keyboard-ui` with Playwright and disposable home/profile on local Linux/X11 Electron: passed. Includes recorder behaviour, editable pages, panel transitions and the new sidebar regression.
- Visual inspection of the expanded sidebar screenshot: passed; full sidebar is visible above empty-workspace content.
- `npm run docs:build`, modified test syntax check and `git diff --check`: passed.
- The new regression failed before the CSS fix. An initial full UI rerun timed out in the existing rename cancellation test; its isolated rerun passed, and the test now waits for the rename input to receive focus before Escape. The final full suite passed.

- [x] Verified on local physical environment (automated live Electron on Linux/X11)
- [x] JavaScript syntax validated across modified files
- [x] Documentation compiled without errors
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project Security Policy

Limit: Wayland and other operating-system compositor behaviour has not been verified. No binding defaults or security boundaries change.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T18:54:25.684303Z">2026-10-13T18:54:25.684303Z</relative-time> | `5ff7229` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-6"></a>
## #6 fix: retain sidebar expansion outside the window

- State: merged (2026-10-13); `codex/sidebar-window-edge` → `personal`; merge commit `a4197fb`

## Description
The auto-collapsing sidebar stays expanded when the pointer leaves the window. Crossing the outer left pixel no longer alternates between expansion and contraction. Entering browser content collapses it normally, including when returning directly into a guest page.

Retained pointer presence replaces native sidebar hover throughout its CSS. A host boundary listener and a trusted guest document-entry message handle the separate Electron surfaces. No timers, polling, mouse-movement listeners or dependencies are added. Keyboard expansion, focus and pinning keep their independent behaviour.

## Fork fixes
- [UX-08: Sidebar retention at window edges](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#sidebar-retention-at-window-edges--13-october-2026): fixes inherited CSS hover loss on window exit and left-edge jitter. The regression fails before the fix and passes afterwards.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] Documentation update or correction

## Verification & Testing
- [x] npm test, including generated keyboard grammar and reference drift checks
- [x] Complete live Electron UI suite with a disposable home/profile on Linux/X11: recorder, editable-page input, overlays, repeated left-edge crossings, top/bottom exits, guest and empty-workspace re-entry, keyboard expansion and pinning
- [x] JavaScript syntax checks for all modified JavaScript files
- [x] npm run docs:build
- [x] git diff --check
- [x] UK English and existing security boundaries preserved

The original implementation fails the new left-edge assertion. An initial host-only re-entry test timed out because host CDP movement does not route into the guest; the test now injects guest leave/entry via Electron sendInputEvent. All final checks pass. Native compositor edge handling on Wayland and other operating systems remains unverified.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T19:06:07.208967Z">2026-10-13T19:06:07.208967Z</relative-time> | `f950b3e` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-7"></a>
## #7 fix: unify sidebar expansion, pin highlight and auto-collapse

- State: merged (2026-10-13); `codex/unify-sidebar-pin` → `personal`; merge commit `4911c78`

## Description
Ctrl+Shift+Z and the sidebar pin button now toggle the same persistent pin state. The button reflects shortcut changes, loses its selected appearance on unpinning, and the sidebar contracts when the pointer enters content without requiring a content click.

The shared mode setter clears temporary expansion and sidebar focus on unpinning and synchronises Preferences and accessible pressed state. Both existing action ids remain compatible, with one palette entry. The unused Zen hiding implementation is removed. Pin persistence updates only the changed setting through the backend merge.

## Fork fixes
- [UX-09: Unified sidebar pin and expansion](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#unified-sidebar-pin-and-expansion--13-october-2026): inherited shortcut/button state divergence, stale highlight, focus preventing contraction, and duplicate palette behaviour. Electron regressions cover mixed shortcut/click use, persistence, highlight removal under the pointer, contraction without clicks, keyboard focus expansion, compatibility aliases, live pages and empty workspaces.

## Type of Change
- [x] Bug fix
- [x] Documentation update or correction

## Verification & Testing
- [x] `npm test`, including generated guest grammar and keyboard reference checks
- [x] Complete `npm run test:keyboard-ui` on Electron/Linux/X11 with disposable homes/profiles, including recorder and editable-page behaviour
- [x] Modified JavaScript syntax checks
- [x] `npm run docs:build`
- [x] `git diff --check`
- [x] UK English and existing security boundaries preserved

Initial validation exposed an alias initialisation error, corrected before the final run. The regression fixture needed guest pointer injection and transition timing adjustments; catalogue coverage was updated for compatibility aliases. A concurrent UI attempt timed out in existing share-overlay cancellation, while the isolated and complete sequential suite passed. No validation failures remain. Wayland and other operating systems are unverified.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T19:21:54.586798Z">2026-10-13T19:21:54.586798Z</relative-time> | `5259f53` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-8"></a>
## #8 fix: release sidebar history button focus after pointer navigation

- State: merged (2026-10-13); `codex/sidebar-history-focus` → `personal`; merge commit `7f704c1`

## Description

Clicking Back or Forward retained focus on the sidebar button, leaving a persistent highlight and preventing the unpinned sidebar from collapsing when the pointer returned to content. Pointer clicks now release that focus and return it to the active page. Enter/Space activation keeps the button focused for keyboard navigation; pinning retains its normal layout.

## Fork fixes

- [UX-10: Sidebar history button focus](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#sidebar-history-button-focus--13-october-2026): fixes inherited click-handler behaviour. Electron regression fails before the fix on retained button focus, then covers both history destinations, pointer exit, keyboard activation, pinning and empty workspaces.

## Type of Change

- [x] Bug fix
- [x] Documentation update

## Verification & Testing

- [x] `npm test`, including generated guest grammar and keyboard reference drift checks
- [x] Complete `npm run test:keyboard-ui` with Playwright: disposable Linux/X11 Electron profiles, recorder, editable pages, navigation, sidebar and overlays
- [x] Modified JavaScript syntax and `git diff --check`
- [x] `npm run docs:build`
- [x] UK English and project security boundaries preserved

Initial UI attempts failed existing hint checks and timed out injecting guest entry after history navigation, including one concurrent run. The new history test exits through host content and runs after the existing navigation scenarios; guest entry remains covered by the overlay suite. The final sequential full suite passed. Wayland and other operating systems remain unverified.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T19:38:09.110965Z">2026-10-13T19:38:09.110965Z</relative-time> | `eeb46ad` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-9"></a>
## #9 fix: let Alt+S close the unpinned sidebar

- State: merged (2026-10-13); `codex/sidebar-shortcut-toggle` → `personal`; merge commit `0350cb5`

## Description
Alt+S opened the sidebar but subsequent presses cycled focus between its controls, keeping it expanded. The existing action now toggles focus between the sidebar and the active page: a second press collapses an unpinned sidebar, including retained pointer expansion. Pinned sidebars stay open and empty workspaces release sidebar focus.

Keep existing action ids, default chords and personalised bindings. Update the action catalogue, command palette and generated keyboard reference.

## Fork fixes
- [UX-11: Sidebar shortcut focus toggle](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#sidebar-shortcut-focus-toggle--13-october-2026): inherited focus cycling prevented shortcut cancellation. Toggle page/sidebar focus and ignore internal pointer transitions that could immediately undo contraction. The Electron regression fails before the fix and passes afterwards, covering tab, navigation and bottom controls, hover, pinning, empty workspaces and return to an editable field without losing text.

## Related Issues
User-reported Alt+S sidebar cancellation.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] Documentation update or correction

## Verification & Testing
- [x] Verified on local physical environment (Linux/X11 Electron, disposable homes/profiles)
- [x] JavaScript syntax validated across modified files
- [x] Documentation compiled without errors (`npm run docs:build`)
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project Security Policy
- `npm test`: passed, including default binding conflicts and generated grammar/reference drift checks.
- Full `npm run test:keyboard-ui`: passed, including the recorder, editable-page, navigation, sidebar and overlay regressions.
- `git diff --check`: passed.

The pre-fix test timed out because the second press retained sidebar focus. An initial post-fix hover test exposed internal mouseover restoring expansion; the boundary-transition fix resolves it. No validation failures remain. Pinning deliberately keeps the sidebar open, and subsequent pointer entry can expand an unpinned sidebar again. Wayland and other operating systems remain unverified.

### Comment by chatgpt-codex-connector[bot] (2026-10-13)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-13T19:47:29.004419Z">2026-10-13T19:47:29.004419Z</relative-time> | `ab9f898` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-10"></a>
## #10 fix: prevent Alt+number from opening the Command Centre

- State: merged (2026-10-14); `codex/alt-number-palette-fallthrough` → `personal`; merge commit `937af7f`

## Description
Alt+1 through Alt+9 selected a tab and then unexpectedly opened the Command Centre in the Extensions category. Add the missing break to the renderer dispatcher's default branch so numbered tab, fallback and unknown actions stop after their own handling.

Add dispatcher regressions for all nine tab actions with zero, three and ten tabs, preserving last-tab selection and clamping. Add real Electron shortcut checks with populated and empty workspaces and confirm explicit extension palette opening still works.

## Fork fixes
- [KB-24: Numbered tab shortcut dispatch](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#numbered-tab-shortcut-dispatch--14-october-2026): accidental fallthrough into extension actions made tab selection and unrelated fallback actions open the palette. Terminate that branch; regressions fail before the fix and pass afterwards. Default bindings and generated grammar/reference remain unchanged and drift-checked.

## Related Issues
User-reported Alt+number opening the Command Centre.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)

## Verification & Testing
- [x] Verified on local physical environment (Linux/X11 Electron with disposable homes/profiles)
- [x] JavaScript syntax validated across modified files
- [x] Documentation compiled without errors (`npm run docs:build`)
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project Security Policy
- `npm test`: passed, including the dispatcher regression and generated-file drift checks.
- `tests/keyboard-ui.test.js`: passed, including the recorder, editable-page and numbered tab shortcut checks. The sequential UI suite then timed out at the existing navigation back-button pointer check; navigation and overlay UI suites both passed on the isolated rerun. The initial sequential timeout remains recorded in the ledger.
- `git diff --check`: passed.

The pre-fix regression failure is intentional evidence of the defect. Initial UI runs timed out in workspace transfer and the new asynchronous tab fixture; creation now waits for each render. An extension-category chip assertion was removed because disabled-extension profiles do not expose it; explicit opening is still checked and the category argument is covered by the dispatcher regression. Wayland and other operating systems remain unverified.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T00:02:38.464477Z">2026-10-14T00:02:38.464477Z</relative-time> | `7562b56` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-11"></a>
## #11 feat: add settings access and centre sidebar footer controls

- State: merged (2026-10-14); `codex/sidebar-bottom-settings` → `personal`; merge commit `9bf9bb1`

## Description
Add a Settings gear to the sidebar footer and centre six equally spaced controls. Keep the pin at the same coordinates in collapsed, expanded and pinned states. Display the update indicator above the row so it cannot displace the controls.

## Fork fixes
- [UX-12: Sidebar footer settings and alignment](docs/fork-fixes.md#sidebar-footer-settings-and-alignment-14-october-2026): inherited left alignment and moving pin are fixed; settings access is new. Footer space is reserved and horizontal overflow suppressed. Arrow navigation includes settings and its tooltip follows configured bindings.

## Type of Change
- [x] Bug fix
- [x] New feature
- [x] Documentation update

## Verification & Testing
- [x] Linux/X11 Electron with disposable profiles: six centres and equal spacing, stable pin across three states, settings activation with arrow keys and Enter, update visibility without displacement.
- [x] Live keyboard editor suite including recorder and editable-page input.
- [x] npm test, including generated grammar/reference drift checks.
- [x] JavaScript syntax, npm run docs:build and git diff --check.
- [x] UK English and existing security boundaries preserved.

The initial footer test exposed a 15px shift caused by horizontal scrolling in pinned mode. Suppressing horizontal overflow fixed it; the rerun passed. Wayland and other operating systems remain unverified. Keyboard defaults are unchanged.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T00:09:13.925968Z">2026-10-14T00:09:13.925968Z</relative-time> | `345731b` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-12"></a>
## #12 fix: release pointer focus across the sidebar

- State: merged (2026-10-14); `codex/sidebar-pointer-focus` → `personal`; merge commit `8e8bf9d`

## Description

Clicking the sidebar clock or another action could leave keyboard focus on the control, keeping the hover-expanded sidebar open until another click. Centralise pointer focus handling for all sidebar controls, including dynamically rendered tab badges, and remove the back/forward-only workaround. Keyboard focus, editable controls, overlays and persistent pinning retain their intended behaviour.

## Fork fixes

- [UX-13: Shared sidebar pointer focus](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#shared-sidebar-pointer-focus--14-october-2026): inherited native pointer focus plus `:focus-within` caused sticky expansion. Delegated capture prevents action controls acquiring pointer focus; a deferred task returns page focus after the action only when an overlay or editable sidebar control does not own focus. Capture covers handlers that stop click propagation. Keyboard highlights use `:focus-visible`.
- Extends UX-10's history-only fix across the sidebar. No new default shortcuts or guest grammar changes.

## Type of Change

- [x] Bug fix (non-breaking change resolving an issue)
- [x] Documentation update or correction

## Verification & Testing

- `npm test` passed, including keyboard defaults, guest grammar and reference drift checks.
- Linux/X11 Electron with disposable homes/profiles: keyboard recorder and editable-page suite, navigation history focus suite, overlay transitions and guest pointer-entry suite, and the new sidebar pointer suite passed sequentially.
- New regression exercises nested clock targets, workspace/dashboard, address/settings/update surfaces, split badge positions, reader/dark/theme/notifications, tab selection and shield/sleep/pin/media badges, keyboard activation, prior keyboard focus, pinning, native sidebar editing and empty workspaces. It is included in `test:keyboard-ui`.
- Documentation build, modified JavaScript syntax and `git diff --check` passed.

Initial validation exposed premature page-focus restoration during captured event dispatch; deferring until after the action fixed history and dashboard focus. Fixture corrections for hidden keyboard targets, startup tab count, decorative split badge hit testing and update notification payload are recorded in the ledger. The initial full UI command stopped at the new fixture; its corrected standalone run passed. One early run overlapped another Electron instance; final verification was sequential.

Limits: Wayland and other operating systems remain unverified. Window controls are included in the real pointer/focus audit with click activation suppressed, so minimise/maximise/close actions themselves are not exercised.

- [x] Verified on local physical environment (Linux / X11 through Electron automation)
- [x] JavaScript syntax validated across modified files
- [x] Documentation compiled without errors
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project Security Policy

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T00:21:11.213543Z">2026-10-14T00:21:11.213543Z</relative-time> | `94524de` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-13"></a>
## #13 fix: show both keyboard binding groups and reliable page keys

- State: merged (2026-10-14); `codex/keyboard-settings-and-page-keys` → `personal`; merge commit `a55b95b`

## Description
Preferences now shows both binding groups for every action in two expandable menus. Their summaries expose both Ctrl+Shift+W and w without switching sections. The redundant Keyboard controls button at the top of Preferences is removed.

Configured page keys also work when non-editable browser controls retain focus. The guest no longer mistakes ordinary custom elements with names containing TEXT or INPUT for text fields, so x is not silently blocked there. Real text fields, INSERT, hints, browser panels, passthrough, native control keys and pinned-tab protection retain their own behaviour.

## Fork fixes
- [UX-14: visible binding groups](docs/fork-fixes.md#visible-binding-groups-and-reliable-page-keys-14-october-2026): resolves the fork editor hiding one group, searching only that group and its redundant header shortcut. Both groups remain editable and independently restorable.
- [UX-15: reliable page keys](docs/fork-fixes.md#visible-binding-groups-and-reliable-page-keys-14-october-2026): resolves host focus dropping page bindings and inherited custom-element editing false positives. Electron checks cover consecutive x closure, input typing, sidebar x/w, passthrough and INSERT cancellation.

## Type of Change
- [x] Bug fix
- [x] Documentation update or correction

## Verification & Testing
- [x] npm test, including guest grammar and generated reference drift checks
- [x] All four disposable-profile Electron suites in npm run test:keyboard-ui, sequentially on Linux/X11
- [x] Final standalone keyboard UI run after the native control guard; covers recording, conflicts, sequences, discard, save/import, live guest keys and INSERT/Escape
- [x] Screenshot inspected for layout and contrast
- [x] Modified JavaScript syntax checks and git diff --check
- [x] npm run docs:build
- [x] UK English and existing sandbox/isolation boundaries preserved

Initial checks exposed a stale generated documentation source, guest focus fixture assumptions and sidebar-covered pointer coordinates. These are corrected. An early navigation run timed out after overlapping Electron tests; its final sequential run passed.

Limits: page keys still do not run inside embedded frames or inspect closed shadow roots; Ctrl+W works with iframe focus. Custom editors need real editable semantics. Pinned tabs require unpinning before closure. Wayland and other operating systems were not tested. The user's specific website was not supplied, so the checks reproduce the identified focus and classification cases locally.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T00:34:43.605859Z">2026-10-14T00:34:43.605859Z</relative-time> | `769fd2e` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-14"></a>
## #14 feat: split Preferences into indexed sections

- State: merged (2026-10-14); `codex/settings-sections` → `personal`; merge commit `dbf3bd8`

## Description
Preferences is reorganised into clearly named sections behind a small index on the left. One section is shown at a time, ordered by estimated frequency of use: **General, Appearance, Keyboard, Privacy & security, Extensions, Performance, Diagnostics, About**. Long views (Keyboard bindings, Extensions) are their own screens. The keyboard editor keeps its search bar and save bar pinned and shows both binding groups side by side, so each action is one row tall until expanded.

Styling is compact: grouped cards with hairline separators, shared button styles, visible focus rings, thin scrollbars and an icon-only index below 760px. All existing control IDs and handlers are retained.

Keyboard: the index is a vertical tab list with one Tab stop (↑/↓ with wrap-around, Home/End). Opening focuses the selected entry; Tab goes straight into the section. The last section is remembered for the session. Ctrl+, opens Keyboard, *Extension Settings* opens Extensions and the sidebar update badge opens About.

Performance (same machine and method, 15 open/close cycles): median synchronous open plus layout **9.4 ms → 1.5 ms**; median to first frame 20.2 ms → 16.1 ms (frame bound). The extensions folder scan and process metrics IPC now run only when that section is shown.

## Fork fixes
See [docs/fork-fixes.md · Sectioned Preferences with a section index](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#sectioned-preferences-with-a-section-index-·-14-october-2026).

- **UX-16** (inherited layout): one long page with mixed groups and the binding editor buried in Help & Shortcuts Reference → indexed sections, regrouped by subject, dedicated Keyboard/Extensions screens. *New capability.*
- **UX-17** (inherited overhead): every opening scanned extensions and laid out the full page → lazy extension scan and single-section layout.
- **UX-18** (inherited defect): the overlay Tab trap counted `tabindex="-1"` controls, so Shift+Tab could stop on the overlay root → only native Tab stops count.
- **KB-25** (inherited test defect, failing on `personal` before this PR): the live keyboard suite raced asynchronous host focus on webview clicks, and `async` predicates passed to `waitForFunction` never waited → focus waits and Node-side polling. Product behaviour unchanged.

Remaining limits: no settings search; the last section is not persisted across restarts; Wayland, Windows and macOS are unverified.

## Related Issues
None.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] New feature (non-breaking enhancement adding functionality)
- [x] Performance optimisation or memory reduction
- [x] Documentation update or correction
- [ ] Security remediation or hardening

## Verification & Testing
- `npm test`: passed (defaults, generated guest grammar and keyboard reference drift checks included).
- `npm run test:keyboard-ui` with a disposable profile on Linux/X11: all five suites passed, including the new `tests/settings-nav-ui.test.js` and the recorder and editable-page scenarios. `keyboard-ui` passed three consecutive runs after the race fix.
- `tests/sidebar-footer-ui.test.js`: passed.
- Screenshots reviewed in dark, light and 700px layouts; documentation screenshots `preferences-1` to `preferences-5` regenerated.
- First run of the new navigation fixture tried to click sidebar buttons hidden in the collapsed strip; fixed in the fixture.

- [x] Verified on local physical environment (Linux / X11)
- [x] JavaScript syntax validated across modified files (`node -c <file>`)
- [x] Documentation compiled without errors (`npm run docs:build`)
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project [Security Policy](SECURITY.md)

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T01:02:46.818339Z">2026-10-14T01:02:46.818339Z</relative-time> | `087d9c4` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-15"></a>
## #15 feat: search Preferences and keep typing after closing a tab

- State: merged (2026-10-14); `codex/settings-search` → `personal`; merge commit `b6b7653`

## Description
**Settings search.** PR [#14](#pr-14) split Preferences into sections without a settings search, so finding a setting meant guessing its section. That should have been part of the rework, not listed as a limit. Preferences never had one before; the only search filtered keyboard actions, and it is still in the Keyboard section.

- **Search settings** sits above the section index, or in a top row in compact windows. Inside Preferences, `/` or the find shortcut (Ctrl+F by default) focuses it instead of opening the page find bar.
- Typing filters every section in place. Matching rows remain live controls, empty groups and sections hide, index entries show match counts or dim, and matches are highlighted with the CSS Custom Highlight API without changing the DOM.
- It matches labels, descriptions, option names (e.g. "kagi"), group and section titles, and per-setting synonyms (e.g. "incognito", "ram", "whitelist"). All words must match.
- Keyboard actions are counted with the editor's own matcher. **Show in Keyboard** opens the editor with the same filter.
- Enter moves to the first result. Escape clears a non-empty search before it closes Preferences. Choosing a section leaves search mode, and closing clears the search.
- Median keystroke cost including layout is 0.8 ms (worst 4.5 ms).

**Typing lost after closing a tab (inherited bug, and a correction to KB-25).** Investigating the intermittent keyboard suite showed a real defect. After a tab closed, the next page was focused from a 50 ms timer. Clicking into that page while the timer was pending left Chromium routing keys to the host, so the first character typed was lost every time, and sometimes every later key too. The KB-25 fixture fix in PR [#14](#pr-14) did not address this; its three passing runs were chance.

## Fork fixes
See [docs/fork-fixes.md](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md): **UX-19** (Preferences search) and **KB-26** (typing after closing a tab).

- **UX-19**: new capability filling a gap introduced by UX-16. Find inside Preferences now searches the settings.
- **KB-26**: inherited defect. Original: typing straight after closing a tab lost characters, and stray keys could run as page bindings from the host. Fix: the deferred focus yields to a page that already took focus, focused webviews release focus before removal, and the host resyncs focus when a key arrives at its body while a page is focused. A stray printable character is delivered to the page field with the webview's `insertText`; no new IPC.
- Remaining limits: the Chromium condition behind an occasional divergence before a tab closes is not identified. In that state Enter, Backspace and similar keys can still be lost once. Wayland and other operating systems are unverified.

## Related Issues
Follow-up to [#14](#pr-14).

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [x] New feature (non-breaking enhancement adding functionality)
- [ ] Performance optimisation or memory reduction
- [x] Documentation update or correction
- [ ] Security remediation or hardening

## Verification & Testing
- `npm test` passed (defaults, guest grammar and generated keyboard reference drift-checked).
- `npm run test:keyboard-ui` (six suites, disposable profiles, Linux/X11) passed three times in a row. It includes the extended `settings-nav-ui` test and the new `tab-close-typing-ui` regression, which fails in round 1 on `personal` and passes 5/5 here.
- Stress fixture: both typed keys reached the field in 0/20 rounds on `personal` and 30/30 here. The live keyboard suite passed 10 consecutive runs.
- `tests/sidebar-footer-ui.test.js`, documentation build, `node --check` and `git diff --check` passed.
- Test changes: the overlay suite now expects find inside Preferences to focus the settings search, and the navigation suite waits for page focus to return before pressing a page key.
- Earlier failures during investigation included an orphaned Electron window from an interrupted stress run that competed for focus on the same display; those runs are excluded from the figures.

- [x] Verified on local physical environment (Linux / X11)
- [x] JavaScript syntax validated across modified files (`node -c <file>`)
- [x] Documentation compiled without errors (`npm run docs:build`)
- [x] Follows UK English spelling in code comments and documentation
- [x] Follows the project [Security Policy](SECURITY.md)

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T02:14:38.267452Z">2026-10-14T02:14:38.267452Z</relative-time> | `f39641f` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-16"></a>
## #16 chore: sync upstream v0.15.0 and retain personal customisations

- State: merged (2026-10-14); `codex/sync-upstream-v0.15.0` → `personal`; merge commit `e9be9ae`

## Description
Synchronise `personal` with Rakosn1cek/Mise-browser `main` at `fa77f782` (v0.15.0), retaining upstream ancestry and all fork customisations. Upstream adds expanded/fullscreen notes, word counts, reader and selection clippings, external editor integration, configurable search aliases and updated release information. Its atomic config fix is already present in the fork.

Custom search aliases and external notes commands are integrated into General using our existing Preferences groups, cards, shared controls, search and compact layout. Preserve the keyboard editor, personalised defaults, extensions, overlay cancellation and delayed-load safeguards. Register export actions in the canonical keyboard catalogue and regenerate the reference. Combine the upstream and fork test commands.

## Fork fixes
- [SYNC-01 and SYNC-02: upstream integration and test compatibility](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#upstream-0150-integration--14-october-2026): resolves the divergent history and merge conflicts while retaining fork behaviour. New options are searchable, save immediately and fit the compact layout. Adapt upstream's blanket Alt restrictions to the fork's conflict validation; update the VM fixtures and guard a browser-only listener when modules are imported in Node.
- Preserve earlier ledger fixes, including atomic configuration writes, keyboard migration and editable-page protection, Preferences search, sidebar focus and tab-close typing.

## Type of Change
- [x] New feature (upstream synchronisation)
- [x] Bug fix (merge and test compatibility)
- [x] Documentation update or correction

## Verification & Testing
- [x] `npm test`, including upstream notes/aliases suites and fork grammar/reference drift checks.
- [x] JavaScript syntax checks for changed files.
- [x] `npm run docs:build`.
- [x] `git diff --check` and staged diff check. Removed trailing whitespace inherited in the new aliases test.
- [x] Linux/X11 Electron with disposable profiles: keyboard recorder and editable-page checks, overlay suite, sidebar pointer suite, Preferences suite, tab-close typing (5/5 rounds).
- [x] New Preferences controls: search, saved external command, saved alias and reviewed 700px screenshot. Corrected the compact Add alias position so hover expansion cannot cover it.

The complete UI command stopped twice at navigation-focus-ui.test.js:94: hints did not appear after closing Preferences during a slow result. An isolated archive of unchanged `personal` fails at the identical assertion, confirming a pre-existing limitation; earlier address-search focus scenarios pass on both revisions. This PR does not claim a fully passing UI command. Initial unit failures were caused by the upstream Alt assumption, a browser-only top-level listener and missing VM fixture dependencies; the final unit run passes.

Real external application launches, Wayland and other platforms are unverified. Node emits a module-type warning when importing renderer modules in unit tests. No new dependency is introduced.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T09:38:11.621465Z">2026-10-14T09:38:11.621465Z</relative-time> | `8849d9c` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-17"></a>
## #17 fix: embed update notice beside sidebar date and time

- State: merged (2026-10-14); `codex/inline-sidebar-update` → `personal`; merge commit `d4014bd`

## Description
When a release was available, the sidebar update action overlapped the date/time badge. Put both in one flex row: the clock yields width to the update button on its right, with matching vertical centres. Without an update, the clock fills the row again. Keep the footer and pin coordinates fixed, hide the notice in the collapsed sidebar and retain opening About.

## Fork fixes
- [UX-20: Inline sidebar update notice](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#inline-sidebar-update-notice--14-october-2026) fixes the fork's absolute-positioned update button overlap. Time stays on one line; the date can truncate only when space is tight. The update remains available when the clock is disabled.

## Type of Change
- [x] Bug fix
- [x] Documentation update

## Verification & Testing
- [x] `npm test`, including keyboard grammar and generated reference drift checks.
- [x] `npm run docs:build`, test JavaScript syntax, `git diff --check` and staged diff check.
- [x] Disposable-profile Linux/X11 Electron footer regression: no overlap, shared vertical centre, clock width recovery, all three formats, clock off/on, collapsed/expanded/pinned sidebar, fixed footer geometry and keyboard opening About.
- [x] Electron keyboard suite: recorder and editable-page behaviour.
- [x] Screenshot reviewed.

Initial desktop attempts stalled in setup and were interrupted. The first geometry assertions sampled separate frames during sidebar expansion and compared a rounded transition width exactly. The fixture now waits for pinned expansion, samples rectangles together and tolerates subpixel rounding for width recovery. Final sequential desktop checks pass. Wayland and other operating systems remain unverified. Bindings and guest grammar are unchanged.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

<!-- codex-pull-request-review-summary -->

## Codex Review Summary

This comment shows the latest Codex review activity on this pull request.

| Review | Status | Commit | Review trigger |
| --- | --- | --- | --- |
| 📝 **Code Review** | ✅ **Completed** <relative-time datetime="2026-10-14T09:48:34.999850Z">2026-10-14T09:48:34.999850Z</relative-time> | `24eb4a9` | PR opened |



<details> <summary>ℹ️ About Codex in GitHub</summary>
<br/>

[Your team has set up Codex to review pull requests in this repo](https://chatgpt.com/codex/cloud/settings/general). Reviews are triggered when you
- Open a pull request for review
- Mark a draft as ready
- Comment "@codex review" or "@codex security review".

Codex reacts with 👀 while any review is running, comments if it has suggestions, and reacts with 👍 once all reviews finish with no findings.

</details>

---

<a id="pr-18"></a>
## #18 perf: audit personal against upstream and remove cold-start overhead

- State: merged (2026-10-14); `codex/performance-parity-audit` → `personal`; merge commit `0afd248`

## Description

Audit `personal` against current upstream main, remove avoidable startup/interaction costs and integrate upstream through 3be7f96 with conflicts resolved. This is the fork's work-log PR; the subsequent upstream proposal preserves the original AI/contribution policies and excludes the fork's personal workflow instructions.

Start with the [performance report](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/performance-audit.md) and [adoption/review map](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/adoption-review.md). Both link the application paths, security boundaries, migration behaviour, optional feature costs and reproducible evidence. Generated benchmark JSON is collapsed by default during review.

## Fork fixes

Full original behaviour, repair and validation: [fix ledger](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md).

- [PERF-01–07](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#perf-01): lazy/disposable keyboard editor rows and controls; one shared pending filter build with per-session protection; correct hints observer/transitions; reliable disposable app fixtures; unchanged clock/status DOM retained; missing local extension runtime included in packages.
- [PERF-08](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#perf-08): local action dispatch avoids host/main/host IPC; palette commands hand focus directly to their destination. Guest self-closing actions finish their input event before teardown.
- [PERF-09](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#perf-09): cold list compilation leaves the main thread, exits before attachment and reuses transferred cache bytes. The exact Ghostery preset/retry recipe is retained; valid caches start no compiler.
- [SYNC-03](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#sync-03): merge upstream tab jumps/cursor with configurable bindings; preserve the active page when closing a highlighted sleeper; splice aliased session metadata only once.
- [KB-27](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#kb-27), [KB-28](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#kb-28), [KB-29](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#kb-29): recover misrouted guest keys, retain correctly positioned hints on redundant resize, and release sharing-guest focus before cancellation.
- [VAL-05](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#val-05): document/first-frame readiness in desktop fixtures; immediate post-close typing remains an immediate test.
- [SYNC-04](https://github.com/Arzuparreta/fly-browser/blob/6bee6f3cd7c1cb2e06107663bd64aeb8a7dc026c/docs/fork-fixes.md#sync-04): preserve the latest upstream chronicle and navigation; remove its trailing whitespace required by verification. Application sources are unchanged by that docs-only upstream commit.

## Performance and limits

Physical Linux/X11, i3 workspace 4, NVIDIA GPU, fixed-size windows, opaque defaults, disabled extensions and new temporary homes/profiles. Same Electron 44.7.0 and dependencies for both revisions. Three alternating cached pairs, three default-GPU/empty-cache pairs, five cold-start/peak pairs; raw arrays, ranges and application hashes are committed.

| Median | Main | Optimised personal |
| --- | ---: | ---: |
| Cached one-tab PSS | 355.4 MiB | 324.6 MiB |
| Cached six-tab PSS | 519.6 MiB | 453.4 MiB |
| Sampled cold-start peak PSS | 580.3 MiB | 430.5 MiB |
| Cold page ready / guest filter attached | 416 / 1708 ms | 389 / 1100 ms |
| GPU idle CPU, one core | 0.665% | 0.433% |
| GPU Preferences at 6× host throttle | 13.7 ms | 6.9 ms |

No extra steady browser process or idle polling; compatibility code stays unloaded while extensions are off; hibernation leaves one guest. Cold filter fetches fall 45 → 15 and ordinary body-mutation hint IPC 150 → 0.

This is **not a literal zero-byte claim**: cold one-tab idle PSS is 7.0 MiB higher at the measured point; hibernated PSS is equivalent within overlapping ranges. Creation/layout medians are effectively comparable, with a 0.1 ms GPU difference retained in the report. Low-RAM/ARM hardware, memory pressure, battery, Wayland and other platforms remain unverified. Enabled third-party extensions/visual effects are outside the default-parity comparison.

## Verification & Testing

- [x] `npm test`, actual filter-worker binary/preset equivalence, failure/exit/retry and generated-file checks.
- [x] Syntax checks across 60 changed JavaScript files and `git diff --check`.
- [x] `npm run docs:build`.
- [x] Complete seven-suite physical Electron keyboard/UI command, including recorder and editable pages; recorder/editable checks rerun after introducing the cold compiler.
- [x] Physical sidebar-footer test and actual Linux ASAR launch; final packaged binary builds a cold filter cache with both compiler modules present.
- [x] Security/session boundaries retained and regression-tested.

Earlier desktop runs failed/stalled at navigation readiness, self-closing guest input acknowledgement, palette cancellation and one rename-focus check. Their context, repairs and remaining limits are recorded; they are not counted as passing acceptance runs. Interrupted/competing-window/virtual-display runs are excluded from final performance evidence.

The initial CI run rejected one extra blank line at EOF in the new focus-recovery test. It was removed; validation now checks the complete target-branch diff, not only uncommitted edits ([VAL-06](https://github.com/Arzuparreta/fly-browser/blob/main/docs/fork-fixes.md#val-06)).

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

You have reached your Codex usage limits for code reviews. You can see your limits in the [Codex usage dashboard](https://chatgpt.com/codex/cloud/settings/usage).
To continue using code reviews, you can upgrade your account or add credits to your account and enable them for code reviews in your [settings](https://chatgpt.com/codex/cloud/settings/code-review).

---

<a id="pr-19"></a>
## #19 fix(keybinds): treat seeded upstream defaults in legacy profiles as unassigned

- State: merged (2026-10-14); `codex/legacy-keybind-defaults` → `personal`; merge commit `53b1af0`

## Description

Untouched profiles from upstream Mise kept the obsolete upstream keymap after upgrading to the fork's configurable bindings. Every upstream release before configuration version 2 wrote its whole default table to `keybinds.json` on first launch. The legacy migration treated each seeded entry as a deliberate user assignment. Ctrl+Tab therefore focused the sidebar navigation (expanding the sidebar with index badges until a click elsewhere) instead of cycling tabs. Ctrl+W focused the page instead of closing it, and new defaults were stripped wherever they collided.

This was found on a real machine running the latest `personal`: its `keybinds.json` is byte-identical to upstream's defaults from 30e8fbe–4057471, and nothing was customised.

Legacy migration now ignores entries whose normalised chord set equals a default shipped by any seeding upstream version for that action. Changed and disabled values remain user choices and keep precedence over new defaults. `scripts/legacy-keybind-defaults.js <upstream ref>` regenerates the table from git history (19 seeding versions, a71878f–2f8ca5f). Version 2 files are unchanged.

## Fork fixes

- [KB-30](https://github.com/Arzuparreta/fly-browser/blob/657ac71d8e89d569b8a791e0298f453c5675bd95/docs/fork-fixes.md#kb-30) (inherited upstream defect exposed by the fork's KB-06 migration): seeded upstream defaults in legacy profiles are no longer treated as user assignments. The KB-06 test fixture encoded the defect and now uses genuine customisations.

## Related Issues

The same change is applied to the open upstream proposal, Rakosn1cek/Mise-browser#22, whose `keybinds.js` is identical.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [ ] New feature (non-breaking enhancement adding functionality)
- [ ] Performance optimisation or memory reduction
- [x] Documentation update or correction
- [ ] Security remediation or hardening

## Verification & Testing
- [x] All 19 historical upstream seeds load without warnings, with Ctrl+Tab resolving to next-tab and Ctrl+W to remove-tab. Before the fix, 18 loaded the old keymap and 0f8fb7f was rejected entirely. The real seeded profile loads as exactly the current defaults.
- [x] `npm test`: new unit cases cover an untouched 2f8ca5f profile, older and reordered seeds, a retired action, a disabled seed, genuine customisations and conflict precedence. They fail on the previous migration.
- [x] New `tests/legacy-keybinds-ui.test.js` (added to `test:keyboard-ui`): Electron with a disposable seeded profile presses Ctrl+Tab. Before the fix, the sidebar keeps focus and the selection does not change; after the fix, the next tab is selected and the sidebar stays collapsed.
- [x] Recorder/editable-page (`keyboard-ui`), navigation, overlay, sidebar pointer/cursor and settings suites pass in Electron with disposable profiles on Linux/X11.
- [ ] `tab-close-typing-ui` is intermittent on this ThinkPad, independently of this change. In alternating runs, unchanged `personal` failed 4 of 4 and this branch 1 of 4, always with `keyboard.press: Application exited`. The earlier full `test:keyboard-ui` run also stopped at a pointer-interception timeout that passed on isolated reruns. These are not counted as passes.
- [x] JavaScript syntax checked; `git diff --check` clean; `npm run docs:build` passes; generated keyboard reference updated.
- [x] UK English spelling followed in comments and documentation.

### Remaining limits
- A user who deliberately kept a value identical to an old upstream default is indistinguishable from an untouched seed and receives the current default.
- Version 2 files saved by the editor store the full table, so a future change to Mise's own defaults would not reach profiles that have saved once. Storing only differences needs load-time conflict resolution for version 2 and is out of scope here.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

You have reached your Codex usage limits for code reviews. You can see your limits in the [Codex usage dashboard](https://chatgpt.com/codex/cloud/settings/usage).
To continue using code reviews, you can upgrade your account or add credits to your account and enable them for code reviews in your [settings](https://chatgpt.com/codex/cloud/settings/code-review).

---

<a id="pr-20"></a>
## #20 fix(keybinds): store only changed bindings so later defaults still apply

- State: merged (2026-10-14); `codex/sparse-keybind-saves` → `personal`; merge commit `29111d8`

## Description

Follow-up to [#19](#pr-19), resolving the limit it recorded. The keyboard editor saved and exported every action, including unchanged defaults. After one save, later changes to Mise's defaults never reached that profile, recreating the problem [#19](#pr-19) fixed for upstream seeds. A new default that collided with a user's chord also caused the whole profile to be rejected with a warning.

- **Format:** saves and exports write `{"version": 2, "changesOnly": true, ...}` containing only the actions that differ from the defaults.
- **Loading:** listed choices take precedence. A default loses only a chord that equals a user chord, shares a page-key sequence prefix or contains a browser-chord stroke.
- **Earlier complete tables** (files without the marker) are interpreted like seeded legacy files. Values that a full-table release wrote as defaults, or that came from upstream seeds migrated by the old KB-06 path, are treated as unchanged. Editor saves are explicit and never filtered.
- **Import** resolves through a host-only `resolve-keybinds` IPC with the startup rules, without changing the active profile. It previously used a separate renderer merge.
- **Generated history:** the tables move to `stored-keybind-defaults.json`, included in packages. `scripts/stored-keybind-defaults.js upstream/main personal origin/codex/upstream-adoption` regenerates them: 20 seeding versions (including the fork's 3bbe900) and 9 full-table version 2 versions.

## Fork fixes

- [KB-31](https://github.com/Arzuparreta/fly-browser/blob/cb7cf4961df04bbffae682589ea0f65ad777044b/docs/fork-fixes.md#kb-31) (fork defect in the version 2 format and the limit recorded in KB-30): bindings are saved as changes so later defaults still apply.

## Related Issues

Follows [#19](#pr-19). The same change is applied to Rakosn1cek/Mise-browser#22.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [ ] New feature (non-breaking enhancement adding functionality)
- [ ] Performance optimisation or memory reduction
- [x] Documentation update or correction
- [ ] Security remediation or hardening

## Verification & Testing
- [x] `npm test`: new cases cover default and changed saves on disk, a changed default reaching both a changes-only and a 079b538 complete-table profile, choices and migrated seeds in a complete table, an unfiltered changes-only value, shortcut and page-prefix collisions, import resolution and the save/reload round trip. They fail on the previous format.
- [x] `tests/legacy-keybinds-ui.test.js` now also imports a complete 079b538 table with one change in the real editor, saves it and checks that the file stores only that change. It fails before the fix. After the fix it passed 6 consecutive runs, once the Preferences opening stopped depending on a chord pressed straight after Ctrl+Tab moved focus (that chord race failed 2 of 6 earlier runs).
- [x] Electron suites with disposable profiles on Linux/X11: keyboard (recorder/editable page), navigation, sidebar pointer/cursor/footer, settings, tab-close typing and legacy bindings all pass. `overlay-state-ui` failed once with a DuckDuckGo network abort (`-3`) and passed on 2 reruns.
- [x] Guest grammar regenerated (`sync-keyboard`), keyboard reference regenerated, syntax checked, `git diff --check` clean, `npm run docs:build` passes.
- [x] UK English spelling followed in comments and documentation.

### Remaining limits
- Complete tables saved by earlier version 2 builds keep defaults that the old migration reduced because of a collision (for example `next-tab` saved as Ctrl+PageDown only). These do not match a recorded default and remain choices until reset in the editor.
- Earlier builds that open a changes-only file still merge it with their own defaults and reject it if one collides.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

You have reached your Codex usage limits for code reviews. You can see your limits in the [Codex usage dashboard](https://chatgpt.com/codex/cloud/settings/usage).
To continue using code reviews, you can upgrade your account or add credits to your account and enable them for code reviews in your [settings](https://chatgpt.com/codex/cloud/settings/code-review).

---

<a id="pr-21"></a>
## #21 chore: continue Mise as an independent repository

- State: merged (2026-10-14); `codex/standalone-fork` → `personal`; merge commit `bdea294`

## Description

Upstream declined Rakosn1cek/Mise-browser#22, so Mise continues here as an independent repository from upstream 3be7f96 (0.15.0). The application and documentation no longer depend on the original repository:

- **Update check:** queries this repository's releases. Previously it would have offered another project's builds; with no release published here it reports no update and no error.
- **Links:** diagnostics "report issue", What's New, documentation site URLs, badges, security and contribution links, and issue templates point here.
- **Package identifier:** `io.github.arzuparreta.mise-browser`, so packaged installs do not collide with upstream installs. The name Mise and the `~/.config/mise-browser` profile are unchanged.
- **Community and credit:** the original author's Reddit and Mastodon channels and the Discussions links (disabled here) are removed. The README and documentation footer credit the origin; copyright notices and GPL-3.0-or-later are retained.
- **Workflow:** `main` becomes the development branch; AGENTS.md and verification target it.
- **Unchanged on purpose:** historical records (blog, ledger, audit) keep their upstream references. Oversight and the community scripts collection stay linked as separate, optional external projects.

## Fork fixes

- [STANDALONE-01](https://github.com/Arzuparreta/fly-browser/blob/fee4e2413c1675b12e2cb725fcb66b8faf7aab80/docs/fork-fixes.md#standalone-01): continue as an independent repository without runtime or documentation dependencies on the original one.

## Related Issues

Follows the closed upstream proposal Rakosn1cek/Mise-browser#22.

## Type of Change
- [x] Bug fix (non-breaking change resolving an issue)
- [ ] New feature (non-breaking enhancement adding functionality)
- [ ] Performance optimisation or memory reduction
- [x] Documentation update or correction
- [ ] Security remediation or hardening

## Verification & Testing
- [x] Repository-wide search: no references to the original repository remain outside historical records and the two external projects.
- [x] Live request to this repository's latest-release endpoint returns 404, which the update check handles as "no update" without an error.
- [x] `npm test`, `git diff --check` and `npm run docs:build` pass. The Electron sidebar-footer suite, including the update indicator, passes with a disposable profile.
- [x] UK English spelling followed.

### Remaining limits
- GitHub still lists this repository as a fork until the owner asks GitHub Support to detach it; there is no API for this.
- Private vulnerability reporting must be enabled in repository settings for the security advisory links to accept reports.
- No binary release has been published from this repository yet.

### Comment by chatgpt-codex-connector[bot] (2026-10-14)

You have reached your Codex usage limits for code reviews. You can see your limits in the [Codex usage dashboard](https://chatgpt.com/codex/cloud/settings/usage).
To continue using code reviews, you can upgrade your account or add credits to your account and enable them for code reviews in your [settings](https://chatgpt.com/codex/cloud/settings/code-review).
