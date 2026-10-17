# Native Chromium implementation record

The native port is **incomplete**. Electron remains the production runtime.
No native application, migration or package has passed acceptance yet.

## Current checkpoint

PR [#112](https://github.com/Arzuparreta/fly-browser/pull/112) merged as
`1b82cc5` after its latest-head ordinary checks and explicit native matrix passed.
[Run 39071709021](https://github.com/Arzuparreta/fly-browser/actions/runs/39071709021)
built and executed 26 native service tests on **Linux, macOS and Windows**, and
passed all 27 Python tooling contracts on all three platforms. Downloaded
`native-core-tests.json` files each contain 26 SUCCESS results, under external
`evidence/ci-native-units-final/`. These are service tests, not browser acceptance.

The sidebar branch `codex/chromium-native-sidebar` is now based on that merged
main. Its nine browser tests are source-checked but unexecuted. The dependent
`codex/chromium-native-hints` branch is already committed/pushed at `003b2fd`;
it adds native accessibility hints, empty-window input/lifetime handling and two
more browser tests, with 28 service tests passing in the local bootstrap runner.
It must be rebased after the sidebar PR merges. Further native page-scroll work
is in `codex/chromium-native-page-scroll` in the managed hints worktree; inspect
its status before switching branches. No source overlay has been applied to the
active control checkout.

## Source and working state

- Planning baseline: `a477efd0cd47c24d8053ce2be013446054268fe9`.
- Starting merged main: `98fc5f9` (planning documents only since the baseline).
- M0 infrastructure branch: `codex/chromium-rework`, initial implementation
  commit `6de1c9d`; [PR #107](https://github.com/Arzuparreta/fly-browser/pull/107).
  This PR contains preparatory tooling/contracts, not the finished port. Use
  `git status --short --branch` and the PR's current remote head for live state.
- Exact upstream and depot_tools pins: `chromium/upstream.lock.json`, resolved
  from ChromiumDash Stable/Linux and official depot_tools Gitiles. The engine
  version in that file is independent of Fly's version in `package.json`.
- External engine root: `/mnt/storage/fly-chromium`; source: `src/`;
  tools: `depot_tools/`. No owner profile has been read or migrated.
- Host: CachyOS Linux, x86-64, 16 logical CPUs, 31 GiB RAM, 31 GiB swap.
  Initial free space: 522 GiB on the external checkout volume, 47 GiB on `/`.
- Source fetch, dependency sync and hooks passed. The actual 249 dependency
  records (168 Git repositories plus content-pinned packages/objects) are in
  external `evidence/dependency-revisions.json`, hashed by `evidence/sync.json`.
- M0 infrastructure merged as `d91ab27` through PR #107. Follow-up control-build
  refinements: `codex/chromium-control-build`, initial commit `89e2b0b`,
  [PR #108](https://github.com/Arzuparreta/fly-browser/pull/108). The PR links
  expose their current remote heads and merge state; after merging, resume from
  pulled `main` with the existing external engine checkout.
- The initial whole-tree `gn gen --check` run was deliberately interrupted after
  it spent minutes reading upstream headers on the HDD. The control now uses
  upstream's normal `gn gen out/FlyControl`; compiler/header checks for Fly
  integration belong to its changed targets. Normal generation passed: 35,976
  targets from 5,063 files in 57.174 seconds. The full `chrome` build is running;
  no completed browser executable exists yet.
  The external `.fly-engine-operation/owner.json` identifies the current process;
  `configure-native.log` and `build.log` hold generation/build output.

## Decisions and topology

The approved full `chrome`/Views architecture remains in force. M1 must prove a
single native window composed from profile-bound workspace controllers, with
native extension registration and two visible split panes in one workspace.
No mixed-profile tab strip, visibility override or cookie swapping is accepted.
Actual upstream ownership seams must be inspected before implementing adapters.
The [pinned-source audit](./upstream-ownership.md) records the real native seams.
The generated [parity ledger](./parity-ledger.md) and
`chromium/baseline-contracts.json` capture 82 actions, 46 explicit palette entries,
28 preferences, 16 persisted inputs and the full product-feature checklist.
Source/test anchors are discovery aids, not claims of native coverage.

Baseline reconciliation: bookmarks are an ordered flat list. Quickmarks and
their shortcuts have already been retired; `tests/bookmarks-ui.test.js`
explicitly requires that no quickmark API/UI exists and `quickmarks.json` stays
untouched. Native parity must retain that behaviour and preserve the unused
legacy file. The plan's older folder/quickmark wording is not current runtime
behaviour and must not become an unsupported import promise. Split layout is
also renderer-memory state, separate from persisted session tab metadata.

## Migration and recovery

No importer exists yet. Legacy loaders use `~/.config/fly-browser` for app JSON
and user content, separately from Electron's platform-dependent `userData` root
for `Partitions`, extension storage and host local storage. An importer must
accept both roots explicitly; assuming they coincide loses data on macOS and
Windows. `workspace_partitions` already persists partition identity across
renames. Preserve that association when allocating native profile IDs.

Do not copy SQLite/LevelDB directories as a claimed auth migration. All storage
and credentials remain unverified. Transaction staging, restrictive permissions,
commit markers, interruption/retry and rollback have not been implemented. No
live source or destination data has been modified.

## Milestones and acceptance

| Milestone | Status | Evidence / remaining gate |
| --- | --- | --- |
| M0 | pending | Source/tools/dependencies/hooks, generation and contract capture pass; control build/launch and clean second checkout outstanding |
| M1 | pending | Native topology, shell, split and extension probe outstanding |
| M2 | pending | Commands, preferences and product UI outstanding |
| M3 | pending | Native services, blocker and user content outstanding |
| M4 | pending | Import, recovery and rollback outstanding |
| M5 | pending | Linux/macOS/Windows packages and update rehearsals outstanding |
| M6 | pending | Acceptance, native default cutover and delivery outstanding |

| Native acceptance | Status | Remaining evidence |
| --- | --- | --- |
| A01 | pending | Native Fly launch/URL/second instance |
| A02 | pending | Native tabs and all input routes |
| A03 | pending | Native keyboard/modes/recorder/editables/IME |
| A04 | pending | Native sidebar/overlays/accessibility/DPI |
| A05 | pending | Single-window multi-profile isolation and restart |
| A06 | pending | Destination-profile moves and in-flight work |
| A07 | pending | Native split visibility, focused pane and restore |
| A08 | pending | Same-pin control and native Fly visibility, including minimise/restore |
| A09 | pending | Native media, video and real Twitch/Pie |
| A10 | pending | Freeze/resume eligibility versus discard |
| A11 | pending | Native extension lifecycle, IDs, services and isolation |
| A12 | pending | Real native-rule blocking, exceptions and startup order |
| A13 | pending | Native user scripts/styles, grants and hot reload |
| A14 | pending | Native search, bookmarks, quickmarks and history |
| A15 | pending | Native preferences/theme/zoom/scale/clock |
| A16 | pending | PDF/reader/downloads/OAuth |
| A17 | pending | Native permissions/private/WebUI boundaries |
| A18 | pending | Clipboard-mediated terminal and hostile inputs |
| A19 | pending | Transactional import, interruption/retry and rollback |
| A20 | pending | Native renderer/browser crashes and recovery |
| A21 | pending | All shipped platform packages/install/update |
| A22 | pending | Second clean build and actual engine update rehearsal |

Existing Electron tests and system-Chromium fixture checks are baseline/harness
evidence only. They cannot pass any native acceptance row.

## Baseline and preparatory test evidence

The committed `docs/chromium/evidence/baseline.json` contains synthetic fixture
traces, observed system-engine identity, baseline outcomes and raw log hashes.
The original logs and disposable profiles remain in the external checkout.

- `npm test`: passed, including the network-free generated contract check.
- `npm run docs:build`: passed. `git diff --check`: passed.
- `python3 -B -m unittest discover -s tests/chromium -p 'test_*.py'`:
  11 tests passed. Covers dirty/unexpected source/dependency revisions,
  preserving unrelated files/configuration, lock/provenance guards and argv safety.
- Existing Electron `tests/keyboard-ui.test.js`,
  `tests/workspace-partitions-ui.test.js`, `tests/split-view-ui.test.js`: passed
  with `FLY_TEST_OZONE_PLATFORM=x11` and the available Playwright module.
  Logs: external `baseline-keyboard-x11.log`, `baseline-workspaces-x11.log`,
  `baseline-split-x11.log`. Disposable fixtures only.
- `tests/bookmarks-ui.test.js`: also passed on X11, verifying the single list,
  retired shortcuts, unmodified unused legacy file and editable-page input.
  External log: `baseline-bookmarks-x11.log`.
- Those same three baseline tests failed under headless Ozone (visibility/focus
  timeouts). A later screenshot-capture repeat of the keyboard test timed out
  attaching to Electron while GN generation was running. A subsequent retry
  passed and saved external `evidence/electron-keyboard.png` (956 × 1052),
  visually inspected as the actual Electron Preferences/Keyboard baseline.
  Its narrow viewport shows a horizontal scrollbar; it is not a native Fly UI.
  Log: `baseline-keyboard-capture-retry.log`. The earlier failure remains in
  the evidence record and is not counted as a passing capture.
- `tests/chromium/control-smoke.js` on `/usr/bin/chromium`, X11, fresh profiles:
  passed both without and with the generated MV3 visibility/ID probe. Evidence:
  external `fixture-evidence/control-smoke-pt2Je3/result.json` and
  `fixture-evidence/control-smoke-qw1CZE/result.json`, with screenshots and logs.
  These used the system engine, **not** the locked upstream build. Results cover
  same-window tabs, native visible/hidden state, editable focus, preserved pause
  and playing background audio. They do not cover native Fly or real Pie/video.
- Earlier harness attempts failed because Playwright's CDP attach enabled focus
  emulation, keeping background tabs visible. The harness now uses `noDefaults`
  and disables focus emulation explicitly, asserts a shared native window, and
  polls hidden documents without relying on suspended animation frames. No web
  visibility getters or media methods are replaced. Sandbox remains enabled.

## Artefacts and performance

No native packages, package checksums or performance benchmarks exist yet.
External stage provenance records exist for bootstrap, sync and hooks.
Existing `docs/public/benchmarks/` data is historical Electron/control evidence,
not evidence for this implementation. Native/control/baseline comparisons and
the Twitch/Pie reproduction have not run. Explicit hosted macOS and Windows CI
has configured and compiled native service tests; interactive/package validation
on those platforms remains outstanding.

## Next action

Wait for the resumed unmodified `chrome` build managed by the user service
`fly-chromium-control-build.service`, inspect its exit and
`evidence/build.json`, then run the same-pin control fixture with and without the
extension probe. The active log is `/mnt/storage/fly-chromium/build-systemd.log`;
inspect `.fly-engine-operation/owner.json` and the live processes before running
another engine operation. Do not modify that source tree while it builds.

Open/validate the sidebar PR, wait for its latest-head checks and manual native
validation, then squash-merge and pull main. Rebase the dependent hints branch
from its old sidebar base `65ce772` onto that merged main. Preserve the separate
page-scroll work before switching its worktree. The retained Electron
recorder/editable-page suite passed with a disposable profile and no page errors;
its external log is `evidence/command-integration-electron-keyboard.log`.

After the control passes, run the same-pin control fixtures, apply the complete
managed overlay, configure/build the native browser, then run
`native.py test-browser`. This is the real runtime gate for the native topology;
no desktop acceptance is claimed yet. Hosted macOS and Windows runners are
available for explicit CI, while only local Linux supports interactive testing.

## Command evidence

- `git fetch origin` and
  `git merge-base --is-ancestor origin/main HEAD`: passed before branching.
- `git switch -c codex/chromium-rework`: passed.
- Official ChromiumDash release and Gitiles depot_tools/DEPS requests: passed.
- External depot_tools shallow clone and detached checkout at lock revision:
  passed.
- External Chromium shallow fetch at lock revision: passed.
- `python3 scripts/chromium/engine.py doctor --root /mnt/storage/fly-chromium`:
  passed.
- `python3 scripts/chromium/engine.py bootstrap --root /mnt/storage/fly-chromium`:
  passed. Explicit `ensure_bootstrap` is required: merely running gclient with
  automatic depot_tools updates disabled did not provision the Python wrapper
  used by `gn`. The runner now includes upstream's non-updating bootstrap.
- `python3 scripts/chromium/engine.py sync --root /mnt/storage/fly-chromium --jobs 8`:
  passed; log `sync.log`, dependency/provenance records under `evidence/`.
- `python3 scripts/chromium/engine.py hooks --root /mnt/storage/fly-chromium`:
  passed; log `hooks.log`.
- `python3 scripts/chromium/engine.py configure --root /mnt/storage/fly-chromium`:
  passed with normal upstream generation; log `configure-native.log` and
  provenance `evidence/configure.json`. It also verified all recorded Git
  dependency revisions and clean trees. The record honestly marks the Fly
  tooling working tree as dirty during this development invocation.
  Earlier wrapper-bootstrap failure and deliberately interrupted full-tree
  header scan are recorded in `configure.log` and the command history above.
- A successful configure is chained to
  `python3 scripts/chromium/engine.py build --root /mnt/storage/fly-chromium --jobs 8`.
  This full local compile is running. Inspect the live operation lease and
  `build.log` before launching another command. There is no build pass yet.
- `python3 scripts/chromium/pin-upstream.py --output /mnt/storage/fly-chromium/evidence/upstream-candidate.json`:
  passed; independent re-resolution verifies source `chrome/VERSION`. This is a
  pin-resolution check, not an engine-update rehearsal.


## Active native implementation and CI

The current continuation starts from merged `106a1c7` on
`codex/chromium-native-window`. The owner's intervening image cleanup was pulled
before this branch. The unmodified control build remains running under the
external operation lease; the source has not been patched during that build.
PR #108 merged as `670c6c8` with its latest remote checks successful.

Native sources now live under `chromium/overlay/fly/`. They include a Local State
workspace catalog and a Views desktop controller composing profile-bound
BrowserWidgets through non-desktop NativeWidgetAura/NativeViewHost. Integration
hooks are an ordered patch in `chromium/patches/series`. The native window is
explicitly gated by `--fly-native` on Linux/Windows; macOS embedding and native
production cutover are still outstanding. This code has **not** passed a linked
native build or desktop acceptance yet. It is not a finished M1.

The catalog uses UUIDs independent of display names, portable unique profile
directory mappings, retired-directory tombstones, exact reorder validation and
fail-closed schema validation. Unknown metadata survives edits. Its tests cover
identity through rename/restart, case distinctions, delete/recreate, invalid
paths/names, corrupt/future schemas and atomic reordering. They are native C++
tests, not JavaScript stand-ins. Private entries stay in the window controller
and never enter the catalog.

The four new C++ translation units pass `clang++ -fsyntax-only` with the pinned
source's actual UI compile flags, generated headers and toolchain. This is a
syntax/header check only; the unit tests have not been linked/executed. Raw flags,
checker and logs are under the external `evidence/` directory. The integration
patch passes `git apply --check` against the pinned pristine source.

`native.py` preflights the whole patch series in a temporary tree, fingerprints
all source inputs, owns an explicit source manifest, refuses unrelated/edited
changes, and can recover an interrupted apply/revert without `reset` or `clean`.
Its first application requires a successful unmodified control build. The
checkout/overlay tooling suite currently passes 22 tests on Linux, including
simulated interruption and preservation of subsequent owner edits.

The owner has only this Linux host and explicitly requested all possible CI
validation. `.github/workflows/chromium-native.yml` is manual-only. It runs
portable tooling contracts on Linux/macOS/Windows, and attempts pinned-source
bootstrap, dependencies, hooks and GN configuration on the selected platforms.
The explicit build depth additionally builds the control, native unit tests and
native browser. Source/configuration attempts use a declared 40 GiB minimum;
full builds keep the 100 GiB floor. Resource/toolchain failures are retained as
failed attempts with artifacts, not counted as native validation. No publication
or ordinary-PR checks are added. Actual CI results must be recorded after dispatch.


## Native commands and cross-platform follow-up

PR #109 merged as `7b7c60d`; PR #110 merged as `a108401`, after current-head PR
checks passed. PR #110 canonicalized temporary test roots on macOS and Windows:
the original fixtures bypassed the CLI's canonicalization and tripped containment
checks on `/var` aliases and Windows short paths. Production guards were retained.
Manual [contracts run 39033282537](https://github.com/Arzuparreta/fly-browser/actions/runs/39033282537)
passed all 22 tooling tests on Linux/macOS/Windows at PR #110's actual remote head.
The earlier [configuration run 39033161482](https://github.com/Arzuparreta/fly-browser/actions/runs/39033161482)
has a successful Linux source/dependencies/hooks/GN job; its original macOS and
Windows fixture failures are preserved, and its other engine jobs are separate.
Downloaded Linux provenance is under external `evidence/ci-linux-configure/`.

The current branch is `codex/chromium-commands`. Native keyboard metadata,
defaults and cross-language validation fixtures are generated from the actual
Fly keyboard catalogue by `scripts/chromium/generate-commands.js`. `npm test`
checks this generated file. New C++ code validates conflict-free assignments,
reserved keys, numbered-tab chords and the passthrough return shortcut, and
routes per-document NORMAL/INSERT/passthrough modes, prefixes, repeat, editable,
composition, hint and recorder contexts. This core is **not yet wired into the
native desktop event pipeline**; product keyboard acceptance remains pending.

Eighteen actual C++ tests (ten workspace tests and eight keyboard tests) have
been linked and executed successfully against the already compiled pinned
Chromium components. This bootstrap used the saved GN compile flags/toolchain
and upstream GTest sources, with a small external test main; it did not build a
browser or replace the standard GN test gate. Exact helper, runner, objects and
output remain under external `evidence/build-keyboard-native.py`,
`run-keyboard-native.cc` and `keyboard-native-tests.log`. The standard
`//fly:fly_unittests` build/execution is being added as manual CI `depth=units`.
It uses a core-only overlay and the real Chromium GN/test launcher, without
patching browser integration or asserting an unbuilt control-browser pass.

Git line-ending settings for dependency tools are process-local, so Windows
source bytes can match the upstream DEPS hash without changing global user Git
configuration. Workspace directory validation also rejects reserved Windows
device names. All desktop/private/isolation/extension/media/package acceptance
rows remain pending until their own real browser evidence exists.


## Native event integration and browser acceptance implementation

PR #111 merged as `f316184`. Its manual unit run
[39034216256](https://github.com/Arzuparreta/fly-browser/actions/runs/39034216256)
compiled and ran all 18 native tests successfully with the official Chromium GN
and test launcher on Linux and macOS. Tooling contracts passed on all three
platforms. Windows native tests stopped during GN because the hosted runner lacks
the SDK explicitly required by the pinned source. That failed attempt is retained
in external `evidence/ci-windows-units-first.log`; it is not a Windows pass.
Linux/macOS unit artifacts, including test summary JSON and provenance, are under
external `evidence/ci-linux-units/` and `evidence/ci-macos-units/`.

The active branch `codex/chromium-command-integration` adds a second ordered
integration patch. Browser page events route through a per-WebContents keyboard
state and native browser UI events through the desktop ancestor. Composition and
native IME paths remain intact. Fly shortcuts replace stock BrowserView
accelerators so disabled/custom assignments cannot silently revert to upstream
keys. Navigation, tab operations, pinning, tab-only mute, clipboard URL copy and
split actions use real upstream browser services. Unimplemented actions return
unavailable rather than pretending success. Floating/product UI parity and the
recorder still need their own implementation/desktop acceptance.

CommandService stores only changes from generated defaults in Local State,
retains explicit disables and numbered-tab choices, validates edits before
persistence, preserves unknown metadata and refuses to overwrite future/corrupt
stores. Chosen chords take precedence over newly introduced defaults. A preference
generation updates each document router without destroying an in-progress sequence
on every keypress. Twenty-six C++ service tests pass in the recorded local
bootstrap runner, including eight new command-store tests. This updated target
still needs the new standard cross-platform GN run.

Four real InProcessBrowserTests are now wired into upstream `browser_tests`:
one desktop host with distinct profiles/native window IDs, cookie/local-storage
isolation, workspace visibility and active-window routing, and native split
visibility/focus/orientation. They have **not run**. Use
`native.py test-browser --root /path/to/engine` after the full native configure.
The source/header checks for native UI/commands pass; full patched BrowserView
syntax checking encountered a not-yet-generated upstream theme resource header.
No browser-linked compilation or desktop acceptance is inferred from that check.

The Windows CI prerequisite step installs the official Microsoft-signed SDK
installer linked from Microsoft's downloads page, checks Authenticode and records
its SHA-256/install logs. This is restricted to disposable hosted runners; local
bootstrap never installs it. No upstream SDK requirement is relaxed.

The original local control build stopped abruptly after 33,695 completed tasks,
without a compiler diagnostic or a confirmed kernel OOM record. Its owner/build
processes were verified absent before removing only the stale operation lease.
Existing source and objects were preserved; resumed build output is
`/mnt/storage/fly-chromium/build-resume.log`. The interruption record is external
`evidence/interrupted-control-operation.json`. Inspect the current lease and live
processes before continuing; do not assume the old PID is still valid.


The follow-up manual unit matrix at PR #112 head `534862f`
([39046264148](https://github.com/Arzuparreta/fly-browser/actions/runs/39046264148))
compiled and executed all 26 tests successfully on Linux and macOS. Windows
successfully installed the required SDK and compiled the same test target, but
all child tests stopped in Chromium's test-suite priority assertion before test
bodies ran. Evidence is external `evidence/ci-windows-units-sdk.log`. The launcher
now explicitly creates only Windows test processes at normal priority; it does
not disable Chromium's assertion or change build/runner priority. This needs a
fresh matrix at the corrected head. The retained Electron recorder/editable-page
suite also passed with a disposable profile, no page errors, and its external
log is `evidence/command-integration-electron-keyboard.log`.
## Native tab sidebar implementation

The dependent sidebar branch adds a native, scrollable vertical tab list that
observes only the selected workspace's actual `TabStripModel`. Native tab creation,
selection, pinning and close/history/beforeunload use upstream services. Buttons
resolve weak WebContents identities against the current model when invoked, so
reordering cannot redirect an existing callback to a different tab. Deferred,
coalesced updates retain existing row/button objects through title and selection
changes; closing or switching workspaces removes only obsolete rows. Observation
is released when the model or view dies.

The third ordered patch suppresses the embedded browser's duplicate tab strip
through its layout query. It retains upstream navigation/security UI for now;
the floating address flow, pointer collapse/resizing, row context menus and drag
ordering still require implementation. Keyboard sidebar focus now targets the
active tab row. A fifth browser test exercises callbacks across a reorder, native
pinning and profile switches. These browser tests are source-checked but have not
run; do not mark A02/A04/A05 passed from their existence.

Stable product tab UUIDs now use Chromium's native session command extra-data
and closed-tab restore paths. They are separate from extension/native session
IDs. New/duplicated tabs get a new UUID; restored tabs may reclaim their saved
UUID after the original is gone. Malformed IDs and live collisions get a fresh
identity, and a live tab's identity cannot be reassigned. Private tabs never add
this field to persistent restore data. Two additional browser tests cover native
close/reopen, collision rejection, malformed input and private exclusion. All
nine browser tests still await their real linked execution. Automatic reopening
of all saved workspaces/session startup remains outstanding.

The detached build received another interrupt signal after 5,901 additional
completed tasks and no failed tasks. Its source/objects were retained and the
verified stale lease was cleared. The build now runs independently of the chat
under `systemd-run --user`, unit `fly-chromium-control-build.service`, with output
in `build-systemd.log`. The interruption record is external
`evidence/interrupted-control-operation-second.json`.

The sidebar's close/teardown test also covers switching back after its selected
profile-bound browser closes. The model observer base removes its registration
before the destruction callback; the sidebar clears its references there without
removing twice. The desktop controller invalidates pending callbacks and clears
its non-owning child-view references before destroying the widget tree.

A ninth browser test loads the same real Manifest V3 extension into both native
profiles through Chromium's extension loader. Its runtime installation event,
profile-local `chrome.storage`, native tab/window IDs and active-tab query are
asserted from a real content script/service worker. This is still unexecuted
browser coverage; extension compatibility, blocking and media acceptance remain
pending until real runs.


## Native accessibility hints and empty-window commands

The dependent `codex/chromium-native-hints` branch is in the managed
`chromium-native-hints` worktree. It derives the hint alphabet and width-boundary
fixtures from the retained `hinter.js`; generated-contract checking remains part
of `npm test`. Two native unit tests verify the cross-language codes, bounded
allocation, uniqueness and fixed-width prefixes. All 28 service tests pass in the
external bootstrap runner (`evidence/build-hints-native.py`,
`evidence/hints-native-tests.log`); an updated official GN run is still needed.

HintController requests native accessibility only for its active WebContents and
releases the scoped mode on completion/cancellation. Targets use native clipped
screen-DIP bounds, live tree/node IDs and frame ownership checks. The overlay is
a non-activating, pointer-transparent NativeWidgetAura child of the existing
desktop, not a second OS window or page DOM. Selection issues native focus/default
actions after validating the current node, owner and bounds again. AX notifications
are coalesced and examined after their update stack unwinds. Navigation, blur,
hiding, frame removal, window movement/deactivation and teardown cancel hints.
No page scripts or web API replacements implement this path. Target count is
bounded and the status label discloses truncation. Native keyboard integration,
IME/AltGr handling and mode labels are wired, but real desktop acceptance is pending.

The desktop has a distinct Fly keep-alive origin. Chromium intentionally ignores
browser-only keep-alives when its last browser closes, so reusing BROWSER would
prematurely start shutdown. A successful native browser close now clears its
controller/sidebar references through RegisterBrowserDidClose before destruction;
widget destruction later detaches the native host. The empty desktop routes
validated bindings without a page and reopens its active workspace with either
Ctrl+T or the sidebar's New tab button. Browser close still uses upstream unload
processing and supports cancellation.

Two more browser tests (11 total in this branch) inject native keyboard events to
activate a hint without hiding/blurring the document, and close/reopen the last
browser using keyboard and pointer flows. They are unexecuted until the full
native browser/test link completes. Do not mark M1/M2 or A02/A03/A04/A08 passed
from source checks or partial service tests.


## Native focused-page scrolling

The dependent `codex/chromium-native-page-scroll` branch extends the browser's
existing ordered edit/input channel with a bounded C++ scroll-command enum.
Chromium's public WebContents interface exposes document jumps but does not
expose Fly's 80-CSS-pixel steps and half-viewport commands. The sixth integration
patch maps these controls to private renderer input commands, which execute in
the currently focused local frame and find its native scroll container through
layout/shadow ancestry. Blink's own scrollBy/scrollTo implementations handle
smooth scrolling, snapping and zoom; no injected script or page API replacement
implements this path. The private names do not enter document.execCommand's
web-exposed dispatch table. Ordinary edit commands retain their original path.

The browser, Blink renderer and Fly dispatcher pass syntax checking with their
actual respective pinned GN compile flags; raw output is external
`evidence/scroll-syntax.log` and the three `scroll-*-syntax.log` files. The engine
source remains unmodified while the control builds. Two more browser tests
(13 total) cover focused-container steps, half-page/jump/horizontal movement at
native zoom, editable typing, hostile page method overrides and a focused
cross-origin/OOPIF scroll target. Native browser execution remains pending.

Workspace switching also stores/restores native WebContents focus instead of
unconditionally resetting it. The OOPIF case switches to a second profile and
back before issuing its native scroll key, to check that the focused frame is
retained. This remains an unexecuted browser assertion.

## Native command palette

The dependent `codex/chromium-native-palette` branch adds a native Views command
surface inside the existing desktop Aura host. It searches the generated 46
explicit palette entries and the remaining action catalogue, retaining explicit
aliases and all 82 action routes. The displayed global and NORMAL bindings come
from the current validated keyboard configuration; filtering uses Unicode case
folding and requires every query word to match. It adds no renderer bridge,
page scripts or privileged WebUI binding.

The search field supports native typing/IME, arrows, Enter, Escape, pointer
selection and dismissal outside the panel. The configured palette binding also
closes it. Unsupported native actions remain disabled during this development
stage. Selection posts execution until the native surface/row callbacks have
finished, checks the originating WebContents and current profile again, and
cancels on profile switch, navigation, tab hiding/destruction or desktop close.
Cancellation restores the saved native page focus. Trusted native command
invocation shares mode transitions with keyboard dispatch, so selecting INSERT
or passthrough does not silently do nothing or apply a transition twice.

The catalogue/filter tests and native-action routing case bring the Linux
partial service runner to 31 passing C++ tests. This uses the pinned compiler,
actual GN flags and already-linked upstream libraries; it is separate from the
official GN test launcher and cannot pass browser acceptance. Evidence:
`evidence/palette-native-tests-final.log`, `palette-syntax-final.log` and
`palette-browser-syntax.log`. The UI, controller and dispatcher pass pinned-header
syntax checks. Two new browser tests (15 total) cover native search/selection,
editable-page isolation, a customised palette binding, focus restoration and
cancellation on workspace switch. They have not run in a linked native browser.

The full palette inspector/preferences, recent commands, extension/terminal
entries and availability of all remaining actions are still pending M2/M3 work.
This is a development component behind `--fly-native`, not a production cutover.

Local repository checks (`npm test`, 27 Python tooling tests, documentation build
and whitespace) pass for this branch. A repeat of the retained Electron keyboard
scenario failed at `tests/keyboard-ui.test.js:183` because its test window closed;
`evidence/palette-electron-keyboard.log` preserves the failure. A fresh disposable
profile repeat is in progress in `palette-electron-keyboard-retry.log`. Neither
attempt is native UI acceptance; the earlier retained-runtime pass remains in
this record, and this branch changes no Electron keyboard implementation.
