# Native Chromium implementation record

The native port is **incomplete**. Electron remains the production runtime.
No native application, migration or package has passed acceptance yet.

## Current checkpoint

Merged main includes PR [114](https://github.com/Arzuparreta/fly-browser/pull/114)
as `485d4bc`. Its ordinary checks and manual matrix 39078704053 passed; all
three official native service summaries contain 31 SUCCESS results. Follow-up work is consolidated into
PR [115](https://github.com/Arzuparreta/fly-browser/pull/115); the stacked feature
PRs preserve their original review history. Its latest remote checks and explicit
CI must finish successfully before merging.
The consolidated head is `9aaf531`: its ordinary checks pass. Manual service run
39144885256 has passed Linux, macOS and Windows with exactly 46 SUCCESS results
each, verified in downloaded summaries. Manual object run 39144886975 is separate and still pending. The local
bookmark commit `0294989` follows that fixed integration head and is not part of
its CI evidence.
Official service CI at `e4150ad` passed all 37 cases on Linux, macOS and Windows,
with downloaded per-test summaries verified. The current development source in the
hints worktree has sixteen patches, 53 Linux partial service cases and 46 browser
declarations; the browser cases remain unexecuted. These counts describe different
source snapshots and validation scopes, not a completed native port.

The codec-matched unmodified control has built and passed actual video/MV3 and
PCM audio fixtures (`control-smoke-a47ZFT` and `control-smoke-X0emxw`). Both control
outputs remain independent. The full CI attempt at an earlier head timed out on
Linux/macOS and lost the Windows runner; those are unverified builds.

The native build and following unit/browser/media pipeline use the clean
frozen `b98cae4` checkout on `codex/chromium-native-runtime` in the sidebar
worktree. The external engine source has its nine-patch overlay, fingerprint
`75a051104f8d5ef10732edca9b7144b01a7e18a9b65294694661af17390f07a0`.
The build was interrupted by a signal after roughly 46,722 compilation steps,
resumed with the existing output and completed successfully. The frozen native
binary is SHA-256 `de7ebfdb766f3e32b9cf6a110f518fbefd050941f1b035abe2dc0cca52be5cd4`.
Its official 31-case service runner passes. Browser tests are still compiling;
full native acceptance remains pending. User units `fly-chromium-native-build.service`
and `fly-chromium-native-acceptance.service` own these operations; inspect their
live state before editing that checkout or the managed engine source. Follow-up
source work uses the hints worktree. The matching inputs are recorded in external
`evidence/native-preparation.json`; logs are `native-build-systemd.log` and
`native-acceptance-systemd.log` under `/mnt/storage/fly-chromium/`.

The pipeline waits for successful compilation, then runs native services, the
frozen browser suite and video/audio fixtures, recording every actual exit status.
That frozen runner predates strict browser-summary verification: independently
inspect its test summary against its 23 declarations, including PRE cases, before
accepting a successful process exit. This is a running operation, not a completed
native test result. Do not edit its inputs during either service.

Next: finish and inspect the frozen build/pipeline, repair actual runtime failures,
then rebuild and run the complete current source. Finish the remaining M2–M6 work
and all platform/package/migration acceptance before default cutover. The sections
below preserve earlier build attempts and decisions.

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
  targets from 5,063 files in 57.174 seconds. Both source-unmodified controls
  have since completed. Full native compilation is now active as described above.
  The external `.fly-engine-operation/owner.json` identifies the current engine
  operation. Earlier configure/build logs remain historical evidence.

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
| M0 | pending | Source/tools/dependencies/hooks, generation and contract capture pass; control builds/launch and codec-matched audio/video/MV3 fixtures pass; clean second checkout outstanding |
| M1 | pending | Native source implemented, full compilation and 23-case runtime suite in progress; macOS adapter outstanding |
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
| A08 | pending | Same-pin control fixture passes; native Fly and minimise/restore outstanding |
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

The initial control build and both audio/extension smoke modes now pass. Preserve
`out/FlyControl` and its `evidence/control-initial/` records before changing GN
arguments. Build the codec-enabled source-unmodified control from the media
branch and run the MP4 fixture, then apply the complete managed native overlay,
configure/build Fly and execute the scoped native browser tests. Keep the initial
control executable/output available as an independent checkpoint. Never mutate
an active engine build's source or output.

Open/validate the combined follow-up PR from
`codex/chromium-browser-test-runner`, wait for all latest-head checks and the
explicit native matrix, then squash-merge and pull main. The retained Electron
recorder/editable-page suite passed in a private Xvfb display with a disposable
profile and no page errors; its latest log is
`evidence/palette-electron-keyboard-xvfb.log`.

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

## Focused native browser-test executable

The dependent `codex/chromium-browser-test-runner` branch defines
`//fly:fly_browser_tests`. It compiles the same Fly browser acceptance sources
with Chromium's original `InProcessBrowserTest`, browser-test main, process
launcher, renderer and extension test support, plus the native browser/resource
runtime. It avoids building every unrelated upstream browser test merely to run
Fly's filter. `native.py test-browser` selects this target by default;
`--browser-target browser_tests` retains the original full-binary path. Both
choices preserve the same fixture assertions and sandbox policy, and provenance
records the actual target. The scoped native UI currently exists on Linux and
Windows; this does not claim macOS native browser coverage.

GN formatting and all declared upstream target labels have been checked against
the pin. Linking and execution remain pending the full control/native builds.

The complete native GN graph exposed an invalid direct dependency from the UI
to the visibility-restricted accessibility platform target. The hint integration
now uses the public `//ui/accessibility` aggregator; upstream visibility rules
remain intact. Isolated generation with the full patch stack succeeds (35,555
targets from 4,996 files). This graph validation does not modify the active
control checkout or count as a native executable build.

The retained Electron keyboard scenario subsequently **passed** in a private
Xvfb display: recorder, customised bindings, editable input and guest scrolling
(`errors: []`, 80 pixels). Log: `evidence/palette-electron-keyboard-xvfb.log`.
The display used an externally extracted Arch package whose detached signature
was verified against the host's Arch keyring; no system package or owner profile
was changed. The prior desktop attempts remain recorded. The second was stopped
after more than four minutes while a key dispatch was pending; its diagnostic
is `palette-electron-keyboard-retry-timeout.txt`. The passing isolated run is
baseline evidence only, not native acceptance.

The isolated `gn check` passes for both `//fly:*` and
`//chrome/browser/ui:ui`, including Fly's new native sources and the scoped test
target. External evidence: `native-shadow-gn-retry.log` and
`native-shadow-ui-headers.log`. The initial restricted-dependency failure is
preserved in `native-shadow-gn.log`.

## Shared native window geometry

PR [#114](https://github.com/Arzuparreta/fly-browser/pull/114) contains the
consolidated keyboard/UI follow-up. Its latest head `4bd4588` passed ordinary PR
checks; the explicit all-platform unit matrix is
[39078704053](https://github.com/Arzuparreta/fly-browser/actions/runs/39078704053)
and is still running. All eight modified upstream implementation files now pass
pinned-target syntax checks, including BrowserView/BrowserWidget, preferences
and native session hooks (`evidence/upstream-native-syntax.log`).

The dependent `codex/chromium-native-window-geometry` branch addresses another
M1 API seam: the embedded BrowserView's browser-window GetBounds/SetBounds must
represent Fly's shared OS desktop, including its sidebar/frame, rather than the
profile child viewport. The eighth patch adapts those two BrowserWindow methods;
native child view/layout geometry and native window handles remain real. This
also routes extension window bounds through Chromium's existing window controller
instead of fabricating extension responses. Restored bounds already use the
shared desktop through the embedded native widget.

A new browser assertion resizes the shared desktop through a background profile,
checks both native BrowserWindows before/after workspace switching, and verifies
the page viewport remains smaller. The real MV3 profile probe now asserts
chrome.windows.get bounds in both profiles. There are 16 browser tests in this
branch; execution remains pending, and no browser acceptance status changes from
these source assertions.

The geometry patch and updated 16-case browser-test source pass syntax checks
with the pinned flags (`evidence/window-geometry-syntax.log` and
`window-geometry-browser-syntax.log`). The embedding predicate is now a const
query so BrowserWindow's const bounds method can use it without a cast. All
eight ordered patches preflight cleanly over the original pinned files. Local
repository/tooling/docs checks pass; these are still source checks rather than
a linked native run.

## Completed initial control and video coverage

The systemd-managed initial control build completed successfully; the lease was
released. Its `evidence/build.json` records the pinned source, clean Fly commit
and executable SHA-256
`f2dddc7d5ac03794095e779d421ba64f4b5af73476855b1156dfa9c348aa7e4a`.
The actual executable matches that digest. Both pinned-engine fixture runs pass:
`fixture-evidence/control-smoke-nULvg6/result.json` (audio) and
`fixture-evidence/control-smoke-NMaq7o/result.json` (audio plus real MV3 probe).
They verify same-window native tab visibility, editable focus, preserved pause
and continuing background audio with sandboxing enabled. Raw logs are
`evidence/pinned-control-audio.log` and `pinned-control-extension.log`.
Build/configure/dependency records and original GN arguments are preserved in
`evidence/control-initial/`. This is actual pinned control evidence, not native
Fly acceptance or a clean second build.

The dependent `codex/chromium-native-media-fixtures` branch adds a generated
H264/AAC MP4 and `control-smoke.js --video`; `--native` runs the same single-profile
fixture through the explicit Fly development gate. The video check requires real
codec support and decoded video frames, then applies the same pause/background
checks. No media methods or visibility APIs are replaced. The initial pinned
control fails this test because its MP4 codec capability is empty (external
`fixture-evidence/control-smoke-synIpw/result.json`). The existing system Chromium
passes the new video-plus-MV3 harness in `control-smoke-zNXZ3E/result.json`; this
validates the harness only and cannot pass pinned/native video acceptance.

The common GN preset now requests `proprietary_codecs` and the upstream Chrome
FFmpeg codec selection, without enabling Google product branding or credentials.
It applies identically to future control and native builds. The matching rebuild
has not completed yet. Full overlay application and control launch now require a
build record with matching GN argument and executable hashes; changing args or
losing/replacing the executable cannot reuse an earlier success marker. Two new
transaction guards bring the Python suite to 29 passing tests. HLS/live Twitch, real Pie, DRM, hardware decode and platform
packages remain unverified.

## Multiple native window models in one workspace

The dependent `codex/chromium-native-workspace-windows` branch keeps multiple
normal BrowserWindowInterfaces created inside the same profile accessible in
Fly's single desktop. A workspace with multiple native windows exposes a separate
choice for each model, in creation order, using weak native identities. Opening
a workspace returns to its last selected live model instead of whichever raw
pointer happened to sort first. Closing its selected model prefers another live
model in the same workspace before switching profiles. Native extension window
and tab IDs remain untouched; no tab models or stores are combined.

Workspace-button rebuilding is coalesced into a non-nestable task. Profile
creation or close may complete inside a native button callback, so the callback's
own view must survive until dispatch finishes. Closed/expired models are removed
from the remembered choices. This changes selection memory only; automatic
multi-workspace session startup is still pending.

Two additional browser cases (18 total on this branch) cover pointer switching,
remembering a workspace's selected native window, closing/removing that model,
and a real MV3 service worker creating a normal window through chrome.windows.
The latter must share Fly's OS host and remain selectable from its workspace.
Both cases are syntax-checked but unexecuted; no M1 acceptance is claimed.
Pinned-header source checks pass for the controller, embedded native widget and
updated browser-test source. Logs: external `workspace-windows-syntax.log` and
`workspace-windows-browser-syntax.log`.

## Full manual CI preparation

The dependent `codex/chromium-native-full-ci` branch extends only the existing
explicit `workflow_dispatch` build depth. Ordinary PR CI remains unchanged.
The prior hosted runs reported initial free space of 85.7 GiB (Linux), 95.7 GiB
(macOS) and 146.9 GiB (Windows). The completed component/symbol-free local control
occupies about 10 GiB. Full CI therefore reserves 80 GiB at initial inspection,
60 GiB before the remaining dependency sync and 40 GiB for each compilation
phase, rather than demanding the initial source-plus-two-build budget again
after the source and control have already consumed part of it. Local native
commands retain the existing 100-GiB default and accept an explicit floor of at
least 40 GiB. Actual resource failures remain failures in the evidence.

After compiling the native browser, Linux and Windows build/run the scoped
browser suite. Linux uses an isolated Xvfb display installed only in that manual
job. Before native test/build compilation, CI removes only recognised
compilation-object/static archive files from the control's obj tree; its executable, shared libraries,
resources, arguments and provenance remain available. The removed-file count and
observed space reclaimed are recorded. macOS still has no native embedding, so
its CI record explicitly says native desktop is unsupported rather than claiming
browser-test coverage. The new full matrix has not completed yet.

## Background native windows

The dependent `codex/chromium-native-background-windows` branch makes native
ShowInactive respect an already selected browser model. An extension-created
normal window with focused:false remains a real separate native model but does
not replace the visible workspace or briefly show its page. The picker can
activate it later through the ordinary native selection path. No DOM visibility
state is replaced. A nineteenth browser case installs a real MV3 worker, creates
that background window, checks native hidden/active state and then selects it
from the picker. Source checks pass; linked runtime execution remains pending.

Full manual build matrix
[39080367029](https://github.com/Arzuparreta/fly-browser/actions/runs/39080367029)
is running at head `2755733` (the preceding 18-case source snapshot). Its tooling
contracts pass on all three platforms; complete builds/browser results are still
pending. The local codec-enabled control rebuild is managed by user unit
`fly-chromium-codec-control-build.service`, log
`/mnt/storage/fly-chromium/control-codec-systemd.log`. Its independent original
control output is `src/out/FlyControlInitial`; the copy/configuration preparation
is recorded in `evidence/control-codec-preparation.json`. The source remains
unmodified until the matching control and fixtures pass.

## Native session startup source

The dependent `codex/chromium-native-session-startup` branch routes normal Fly
startup through Chromium's existing per-profile SessionRestore and last-opened
profile list. The development switch selects restore-last-session without
overwriting the user's stored startup preference or mandatory policy. Existing
guest/private guards still apply. The same upstream startup decision controls
session cookie and storage retention. Explicit empty-workspace/new-window
requests continue to suppress restoration within the running process.

Patch nine changes the startup preference seam and honours the existing native
BrowserInitState restore flag for Fly. There is no second URL/session store.
The scoped browser suite now contains 21 cases, including a PRE/restart pair
that records two profiles/three native windows, stable workspace/tab identities,
pinned state and isolated session cookies, then verifies them after process
restart in one OS host. These runtime assertions are pending execution.
The startup and session-service translation units and browser test source pass
pinned-header syntax checks (`session-startup-syntax.log` and
`session-startup-browser-syntax.log`). All nine patches preflight over 20 original
upstream files without modifying the active control checkout. Repository tests
and the 29 tooling transaction tests pass.
The complete isolated GN graph also generates and passes header ownership
checks (`session-startup-gn.log`, 35,576 targets). This validation uses private
patched copies and leaves the in-progress control source untouched.

## Native media provenance

The dependent `codex/chromium-native-provenance` branch makes native build evidence
identify the executable path/hash, argument hash and whether a desktop adapter is
implemented on that platform. The media harness requires this completed build
record with `--native-record` when using `--native`, and checks its engine lock,
overlay fingerprint format, desktop support and executable identity before
creating a work directory, starting a server or loading Playwright. A browser
ignoring the development switch can no longer be labelled native by that switch
alone. macOS compilation cannot produce native desktop acceptance until its
adapter exists. The record digest and build provenance accompany fixture results.
Six boundary tests use inert file bytes and no browser launch; missing, control,
unsupported-platform, different-lock, malformed-overlay and replaced-binary
records fail before side effects. The tooling suite now has 35 passing tests.
These tests validate evidence integrity; no native runtime result is claimed.

## Private native workspace source

The dependent `codex/chromium-native-private-workspaces` branch connects the
private-mode command and palette entry to Chromium off-the-record profiles.
Each persistent workspace has its own native OTR context; entering starts an
empty private window in the shared desktop, preserving normal tabs in their
original profile. Native incognito policy controls availability. Private choices
and the return-window weak pointers live only in memory and do not enter the
workspace catalog or persistent Fly tab metadata. The sidebar identifies the
private workspace and the mode indicator includes PRIVATE.

Exiting closes that OTR profile's native windows using Chromium's beforeunload
coordinator, returning to its normal workspace after successful close. Cancelling
a dialog retains the private window. Closing one of several private windows
first selects another window of the same OTR profile. Native Chromium owns OTR
service/storage destruction; Fly does not copy cookies or original navigations.
This replaces Electron's shared private partition with workspace-isolated native
private profiles. Extensions retain Chromium's user-controlled incognito access.

There are now 23 browser cases. Two new cases cover per-workspace private
isolation, normal-cookie preservation, native host identity, unchanged catalog,
private metadata exclusion, closing/re-entering with empty cookies/local storage,
and beforeunload cancellation. Their linked execution remains pending. Controller,
native commands and browser-test translation units pass pinned source checks;
repository and 35 tooling tests pass. No A17 completion is claimed yet.

PR [114](https://github.com/Arzuparreta/fly-browser/pull/114) was squash-merged as
`485d4bc` after ordinary checks and all six jobs in manual run
[39078704053](https://github.com/Arzuparreta/fly-browser/actions/runs/39078704053)
succeeded at its latest head. The downloaded Linux/macOS/Windows test summaries
each contain 31 SUCCESS results (`evidence/ci-palette-final/`). The root checkout
is pulled main. Later source changes in the dependent branches still need their
own final delivery and runtime validation.

## Matching codec control and bounded hosted compilation

The codec-enabled source-unmodified control completed at
`2026-10-18T00:43:15Z`, clean Fly source `2c3aead`, executable SHA-256
`f66bf58d2cd2ea47159f4e68e0c8203a2dceb4d4366eb85007fb5b7f7b795d72` and
argument digest `09a546dca3089bf4d9cc93de2555ebe54ff6528a214d099c6e2a9ccbbd6cd71d`.
Its real H264/AAC video/MV3 fixture passes in
`fixture-evidence/control-smoke-a47ZFT/result.json`; PCM audio passes in
`control-smoke-X0emxw/result.json`. They verify decode, pause, native visibility
and continuing background audio with sandboxing enabled. This is pinned control
evidence, not Fly runtime acceptance. The original control remains independent.

Full manual matrix 39080367029 did not complete: Linux and macOS hit the hosted
5h50 limit during their initial control compilation (about 30,907/66,953 and
27,862/70,761 actions respectively). Windows lost communication with its runner;
its cause and compiler result are unknown. Downloaded Linux/macOS evidence is
retained in `evidence/ci-full-first/`. Neither cancelled job is a source failure
or a successful build, and Windows has no retained build log.

The dependent `codex/chromium-native-ci-objects` branch adds explicit manual
`objects` depth to validate actual compiler outputs and GN ownership within
hosted resources. It retains generated prerequisites, resolves source-to-object
edges from GN's Ninja files, prefers the scoped browser runner, fails on missing
shared/compiler edges and records macOS-only desktop exclusions. A distinct
compile-only transaction cannot authorise browser configuration/build; the
completed matching-control requirement still guards full native builds. Hosted
compilations use two workers, and active-stage metadata survives job timeout in
retained evidence. Four new boundary tests bring tooling coverage to 39 tests.

PR 114 is merged main `485d4bc`. Its eight dependent source commits were rebased
onto that main in `codex/chromium-native-runtime` at `b98cae4`; the overlay is
unchanged by the rebase. The clean sidebar worktree is frozen for user service
`fly-chromium-native-build.service`. It independently seeds `out/FlyNative` from
the matching control, then applies/configures/builds all nine patches and the
23-case browser source. Log: `native-build-systemd.log`; preparation identity:
`evidence/native-preparation.json`. Do not edit/switch that worktree during the
build. Subsequent tooling work uses the hints worktree. Native runtime and
remaining M0-M6 acceptance still require execution.
The object resolver finds 31 compiler edges with no exclusions in the actual
Linux native GN graph (`evidence/ci-objects-actual-plan.json`). This checks target
selection only; hosted object compilation has not yet run on this branch.

## Account and vault defaults source

The dependent `codex/chromium-native-privacy-defaults` branch adds patch ten at
the native profile preference registration seam, after keyed services register
their preferences. For the development-native gate it disables browser sign-in
(including the next-startup setting), password saving/autosign-in, browser passkey
storage and automatic passkey upgrades by default. It changes native default
values; user and enterprise preference precedence remains native. It does not
copy an account/password database or disable website WebAuthn authenticators.

A 24th browser case checks both newly created normal profiles and their OTR
profile. The profile utility and 24-case browser-test source pass pinned compiler syntax
checks. The full private GN graph generates and both Fly/profile utility header
ownership checks pass (`privacy-defaults-gn.log`). All ten patches preflight over
22 upstream files without modifying the active build source. Linked execution is
pending. This branch is outside the frozen running 23-case build.
Telemetry and the rest of the product privacy/service acceptance remain separate
unverified gates. The running local acceptance pipeline waits for the complete
build and will record native unit/browser/media exit statuses automatically; it
does not turn an attempted stage into a pass.
## Native blank tabs and restart test coverage

`about:blank` is an ordinary native tab. Each workspace owns a Chromium Profile;
its window models own their native TabStripModels and WebContents. The sidebar
projects every tab from the selected model, including blank pages, new-tab pages
and extension pages. URLs and titles are presentation data, never tab identity
or a reason to discard a tab. Zero native tabs produces the empty desktop UI.
There is no replacement blank WebContents, URL-only recreation or second tab
registry. Chromium retains opener, origin inheritance, extension IDs and security
semantics; Fly adds its independent UUID to the same native tab.

The browser suite now contains 27 declarations, including two PRE restart phases
and 25 main cases. New cases exercise a real page's `window.open('about:blank')`
with inherited origin/opener, navigation without replacing the tab/sidebar row,
and MV3 `chrome.tabs.create` with an explicit blank URL, native IDs, profile
ownership, openerTabId, sidebar inclusion and native close/reopen identity.
These source cases are not yet executed: the first full native link is pending.

Restart coverage distinguishes an ordinary launch without URLs from a launch
with an explicit command-line `about:blank`. Both must restore all saved native
tabs, including a bare blank tab; the latter must also retain the newly requested
blank as a separate native tab with its own identity. The general Chromium test
fixture injects a command-line blank by default, so the ordinary-launch fixture
explicitly disables that test-only argument. No product URL filtering is added.

The browser runner selects all Fly browser fixtures and checks a newly generated
upstream result summary against every main case declared in the source. Missing,
failed, skipped and zero-test runs fail evidence recording; successful PRE phases
are correctly folded into their main case by Chromium's launcher. An old summary
is archived before execution and cannot satisfy a new run. All 43 Python tooling
contracts pass locally; browser runtime acceptance remains pending.

Windows object run 39120988598 exposed CRLF conversion of the product patch
checkout before native application. A local reproduction confirms that an LF
patch applies to exact upstream LF bytes while its CRLF-converted counterpart
fails identically. Repository attributes now preserve all `chromium/` inputs
byte for byte; a real temporary Git checkout with `core.autocrlf=true` verifies
both patch/overlay bytes and successful patch preflight. No host-global Git
setting is needed for these product files. The earlier failed run remains failed;
the updated remote head must pass fresh CI.

## Native keyboard preferences editor

The dependent `codex/chromium-native-keyboard-editor` branch adds an actual
browser-owned Views editor, reached through the existing keyboard-help action
and palette. It exposes every action's global and NORMAL bindings, search,
numbered-tab modifiers, shortcut recording, per-action clear/default and restore
all defaults. Each binding occupies one editable line; punctuation such as `;`
remains an actual key. Recording intercepts native keys before commands, ignores
repeats and composition/AltGr, and shares the router's two-stroke timeout.
Escape cancels recording first and closes the editor on a subsequent press.

Edits stay in memory until the shared CommandService validates and saves the
complete transaction. Invalid/conflicting bindings remain editable without
partially saving; explicit disabled actions survive future default updates.
A generation guard rejects overwriting settings changed elsewhere. Unrecognised
or corrupt existing storage is preserved and cannot be overwritten by Apply.
Navigation, profile changes, page destruction, desktop blur/close and resize
cancel recording/drafts. Posted widget cleanup precedes restoring native focus.

The complete source now has 33 browser declarations, including two PRE phases
and 31 main cases. Five editor cases exercise real native key events, editable
page focus, disabled/default shortcuts, two-stroke page scrolling, semicolon
recording, conflicts, concurrent changes and cancellation on workspace switch.
These native GUI cases await a linked browser/test executable. Source compilation
checks and the independent GN/header checks cover the eleven-patch stack over
22 upstream files, without changing the frozen engine source currently building.
The retained Electron recorder/editable-page regression is run separately with
a disposable profile and authenticated private Xvfb display; it cannot validate
the new native UI. Native preferences beyond keyboard remain pending.

Keyboard editor validation: pinned Clang accepts the editor, controller, command
adapter and all 33 browser declarations; eleven-patch preflight and full GN graph
plus Fly/UI/profile header ownership checks pass. All 43 Python tooling contracts,
`npm test`, documentation build and whitespace check pass. The retained Electron
keyboard test passes with `errors: []` and native guest scroll of 80 pixels under
private Xvfb (`keyboard-editor-retained-electron-slow-start.log`). This run retained
all assertions and raised only the Electron launch timeout from 30 to 120 seconds
on the compiling host. Earlier attempts remain recorded: X11 launch timed out at
30 seconds; headless launched but could not open Preferences through the focused
shortcut. Neither attempted run is counted as a pass. The new native editor
still requires actual linked runtime execution and platform acceptance.

## Native clock and first app preferences group

PR [116](https://github.com/Arzuparreta/fly-browser/pull/116) contains native
keyboard preferences and account/vault defaults, based on PR 115's branch until
that predecessor passes and merges. Its `b2a30b4` object matrix is run 39124090577.
The next dependent branch is `codex/chromium-native-clock` in the hints worktree;
none of these source changes enter the frozen nine-patch local build implicitly.

The native sidebar now has a browser-owned live clock with the existing three
formats (`datetime`, `time`, `seconds`), local 24-hour time, click-to-cycle,
keyboard/palette `toggle-clock`, and immediate preference updates. The timer
stops when the clock or desktop is hidden and never saves on ticks. Visibility
and format live in `fly.preferences` in native Local State, schema 1 with a
`values` dictionary. Reads do not write defaults before migration; mutations
preserve other groups and unknown metadata. Malformed/future data is preserved
and disables editing. Managed native preference precedence also disables edits.
This is app presentation state shared across profiles, with no site/private tab
metadata. The other 26 baseline preferences still need native implementation.

Patch twelve registers the service and its actual GN dependencies, including
both scoped and upstream browser-test targets. All twelve patches preflight over
22 upstream files; complete GN generation (35,577 targets) and Fly/UI/profile
header checks pass in the independent shadow. Six new preferences tests execute
in the pinned Linux partial service runner: all **37 C++ service tests pass**.
The raw log is `evidence/clock-native-tests.log`. This runner links actual pinned
Chromium libraries and gtest; it is not full-browser or GUI acceptance.

The source now has 34 browser declarations (two PRE phases, 32 main cases).
The added native clock case checks sidebar rendering, no initial tick/default
write, click format changes, command visibility changes and preserving future
schema data. Pinned source checks pass; that GUI case remains unexecuted until
a current native browser/test binary links. No clock acceptance or broader M2
completion is claimed from the standalone service results.

## CI progress retention

The dependent `codex/chromium-ci-progress` branch addresses the missing Windows
full-build diagnosis when its runner connection was lost before artifact upload.
CI now preserves exact merged child stdout/stderr bytes and emits bounded live
progress every fifteen seconds, plus failures immediately. Actual child exit
codes and full log hashes still determine each recorded result; no time/resource
failure becomes a pass. Source lines are prefixed in the console, including the
bounded failure tail, so progress containing carriage returns or workflow-like
text remains ordinary log output. A lost runner can still lose its artifact, but
the server-side job log retains the latest emitted build progress.

Four real subprocess logging cases pass locally, including failure exit status,
non-UTF8 bytes, bounded noisy progress, carriage returns and a quiet successful
child. The complete Python tooling suite passes **47 tests**. This changes no
ordinary PR workflow or required check; existing manual engine jobs call the
updated logger. The new native clock branch is PR
[117](https://github.com/Arzuparreta/fly-browser/pull/117) at `e4150ad`; its manual
service and object runs are 39124850207 and 39124851963.

The earlier native service matrix 39120990306 at `39d68ec` has completed
successfully on Linux, macOS and Windows. It precedes the Windows line-ending
and browser-fixture repair, so it cannot substitute for the latest-head matrix
39123604736 at `5d76c7e` required before merging PR 115.

## Native sidebar favicons, audio and mute metadata

The dependent `codex/chromium-native-tab-indicators` branch projects the native
navigation entry's actual favicon into each existing sidebar row, with a native
vector fallback for blank/unknown icons. It caches bitmap identity and uses
16-DIP images. Pin/close controls are compact native icon buttons with tooltips
and accessible names; all hover/pressed/disabled icon states follow changes.
Rows and their weak WebContents identity survive favicon/title/pin updates.

The audio control uses native mute state and RecentlyAudibleHelper, including its
native two-second quiet-period notification. Clicking it resolves that row's
current contents after moves and can mute background tabs without selecting
them. Both this control and the shared mute command now call Chromium's
`SetTabAudioMuted` with its user-audio-indicator reason. The earlier direct
WebContents setter changed audio but omitted native extension mute metadata;
standard `chrome.tabs` queries now receive the user reason and clear any previous
extension ownership through the upstream utility. No media play/pause or web API
is replaced, and this remains tab-specific rather than changing site permissions.

Two source browser cases fetch/decode the real upstream favicon fixture, retain
the same row/UUID through navigation to bare `about:blank`, and query mute state
through a loaded MV3 extension after user/extension changes and a background-row
move/click. The scoped runner declares its actual favicon HTML/PNG dependencies.
There are now 36 browser declarations, two PRE phases and 34 main cases.
These GUI/API cases remain unexecuted pending a current linked test binary.

Pinned Clang accepts the sidebar, native commands, controller and all browser
cases. All thirteen patches preflight over 22 upstream files; native GN
generation and Fly/UI/profile header checks pass. The initial GN attempt exposed
an incorrect vector icon generator target; the corrected stack uses the existing
Views target and the explicit component icon dependency. All 47 Python tooling
contracts, `npm test`, docs build and whitespace pass locally. This does not
substitute for native GUI, audible playback or packaged acceptance.

## Native product tab menu and pinned-tab protection

The dependent `codex/chromium-native-tab-menu` branch adds the product's native
sidebar context menu: adjacent blank tab, reload, duplicate, pin/unpin, tab-specific
mute/unmute, close, close others/right and profile-native closed-tab recovery.
Opening it preserves the selected tab. It resolves the target's weak native
identity and current index again after menu dispatch; reorders do not retarget a
command, and closed targets or a changed workspace generation cancel it. Native
model/unload/session services perform the operations. Muting retains Chromium's
user-indicator metadata and does not change site sound permissions.

Pinned product tabs now reject the shared close action and disable the sidebar
close button/context item until unpinned, matching the retained Fly guard.
Extension APIs and whole-window shutdown keep their native close semantics.
Native close-other/right commands already exclude pinned tabs in the pinned
upstream implementation.

Three additional browser cases use native pointer menu opening and cover
background targeting, pinned protection, reorders, closed targets, workspace
switch cancellation, adjacent creation and stable identity through close/reopen.
There are 39 browser declarations (two PRE phases, 37 main cases). Source checks
pass with the pinned compiler, and the complete thirteen-patch graph plus Fly/UI
header ownership checks pass in an independent shadow. These new GUI cases
remain unexecuted until a current browser-test executable links; the running
complete native build stays frozen at its reviewed nine-patch snapshot.

Local repository checks pass: `npm test`, 47 Python tooling contracts,
documentation generation and whitespace. Retained Electron context-menu and
recorder/editable-page regressions both pass under private authenticated Xvfb
with disposable profiles (`tab-menu-retained-context.log` and
`tab-menu-retained-keyboard.log`; keyboard errors empty, guest scroll 80).
The keyboard run retains all assertions and uses the existing external wrapper
that only raises its app-launch timeout on the compiling host. These passes are
retained-runtime regression evidence, not execution of the new native cases.

## Native footer status and security presentation

The dependent `codex/chromium-native-status-bar` branch places the keyboard-mode
label in a native footer alongside current URL/link target, native page security
and workspace/private context. The read-only view observes the actual selected
tab model and WebContents. Chromium's browser delegate forwards native pointer
link targets; hidden tabs and other profile/window models cannot replace them.
A separate target observer clears links when their pane hides, navigates or dies,
including a visible unfocused split pane. URL formatting and the page's security
level come from Chromium's existing formatter and Chrome security-state utility,
not a scheme heuristic or renderer binding. Clearing the page also clears the
accessible names and tooltips that could otherwise retain old private metadata.

`show_status_bar` is now a third native presentation preference in the existing
schema-1 app store. The shared command/palette action toggles it immediately.
Reads do not save defaults; edits preserve clock and unknown data, respect native
policy precedence, and reject malformed/future implemented groups. Two new
service cases bring the pinned Linux partial runner to **39 passing C++ tests**
(`status-bar-native-tests-final.log`). This remains separate from the official native
GN launcher and cannot pass GUI acceptance.

The fourteenth patch integrates the new view and browser-target forwarding seam.
All fourteen patches preflight over 23 upstream files; independent full GN
generation and Fly/UI/browser-delegate header ownership checks pass. Pinned Clang
accepts the view, controller, command adapter, modified upstream delegate and all
41 browser declarations (two PRE phases, 39 main cases). Evidence:
`status-bar-gn-final.log`, `status-bar-syntax-final.log`,
`status-delegate-syntax.log` and `status-bar-browser-syntax-final.log`.
Two new native browser cases cover real pointer hover/exit, visibility preference,
hidden/other-profile rejection, HTTPS engine state, a native test malware verdict,
and private/blank state. They remain unexecuted pending a current linked test
binary; the full active build keeps its earlier frozen inputs unchanged.

The preference guard also detects a typed default/recommended fallback while an
explicit user value is present. A corrupt non-dictionary preference remains
untouched rather than being mistaken for a new store. The first synthetic case
exposed the typed user getter's fatal mismatch diagnostic; the corrected guard
uses read-only native provenance/effective-value APIs. The final native tests
exercise both fallback and preservation boundaries.
`npm test`, 47 Python tooling tests, documentation and whitespace pass locally.
The retained menu/keyboard disposable-profile regressions passed immediately
before this footer-only follow-up; native footer GUI acceptance remains pending.

## Native interface and website color scheme

The dependent `codex/chromium-native-theme` branch implements the native
light/dark action and the `theme`/`sync_website_theme` preference group. The shared
desktop uses Views color-provider overrides, inherited by its native child
surfaces. An explicit native background keeps unpainted sidebar/content areas
consistent with those colors. There is no CSS injection or web API replacement.

The existing Chrome browser-client preference seam maps the selected website
scheme to actual Blink preferences and native scrollbar appearance. Turning
website synchronization off selects light, matching the retained product's
`websiteTheme` function. The generator executes that exact repository function
to capture all four supported mode/sync combinations for the C++ contract test.
WebUI, native managed browser-color policy, enterprise isolation, forced colors
and Chromium's guest-owner preference routing retain their original paths.
Changes notify native web-theme observers, including hidden/private/extension
contents, without changing the process-wide OS color preference. Clock ticks do
not issue theme notifications or write preferences.

Three new native service cases cover the retained contract, restart, independent
UI/website choices, other-group metadata, malformed/future data and policy.
All **42 C++ service tests pass** in the pinned Linux partial runner
(`theme-native-tests.log`). Complete fifteen-patch preflight and independent GN
checks pass for Fly, UI, browser delegate and browser core (`theme-gn.log`).
Pinned Clang accepts the controller/dispatcher, modified browser-client
implementation and all 42 browser declarations (two PRE phases, 40 main cases):
`theme-ui-syntax-final.log`, `theme-client-syntax.log` and
`theme-browser-syntax.log`.

The new native browser case checks actual matchMedia output, live changes in
already loaded/hidden profiles, private contents, native UI color keys, preserved
editable focus and absence of preference writes from notification processing.
It remains unexecuted until a current native test executable links. The complete
running build still uses its frozen nine-patch inputs. Custom theme palettes,
the general preferences UI and the remaining product settings are still pending;
this does not pass A04/A15 or complete M2 from source checks alone.

## Native presentation controls

The native preferences surface now exposes the five implemented presentation
settings: dark/light interface, website color synchronization, clock visibility,
clock format and status-bar visibility. Native Views checkboxes and a format
button occupy an independently scrollable Appearance tab. Keyboard keeps its
own tab and native scroll viewport. They call the same app-wide PreferencesService
used by commands; changes apply
immediately. The sidebar has a Preferences button, disabled when no browser
context exists. Keyboard edits remain drafts until Apply in the Keyboard tab, and Close or
Escape discards only that keyboard draft. The panel states this distinction.

A pref observer refreshes presentation controls after outside changes without
rebuilding keyboard rows, dropping a draft or cancelling recording. Future,
malformed or managed appearance data leaves controls disabled; preserved data is
never replaced with defaults. Callbacks after cancellation cannot save a change.
Observer subscriptions and control pointers are cleared before widget teardown.
The native child surface shares the existing window and focus/IME lifecycle.

Two additional browser cases cover pointer entry, all five settings, keyboard
draft preservation, outside updates, close and restored page typing without
visibility events; and unsupported data, live recovery and cancelled callbacks.
These bring the current suite to 44 declarations including two PRE cases. They
remain unexecuted pending the complete current native browser. This implements
presentation controls, not the remaining preference groups or complete M2.

Downloaded official summaries from run 39130191670 at `6ff2d1e` contain exactly
39 SUCCESS results on each of Linux, macOS and Windows. Object compilation for
that snapshot and later theme-source CI are separate, still pending operations.

## Strict native dictionary storage

The app-wide workspace and keyboard services now share the preference service's
native dictionary provenance check. A wrong-typed user value can resolve through
Chromium's typed getter to an empty default or recommended dictionary; treating
that result as a new store would overwrite the original data. The common
`fly/prefs/dictionary_store.h` guard uses read-only native preference provenance,
preserves valid explicit empty dictionaries and avoids the typed user getter's
fatal type-mismatch diagnostic. No private PrefService API is exposed or patched.

Managed dictionaries remain readable with native precedence. Every workspace
mutation and keyboard save also checks native user modifiability, preventing a
hidden user write behind managed settings. The keyboard Apply button reflects
that restriction, and workspace creation checks editability before allocating a
profile directory. Malformed and future schemas remain fail-closed.

Four regression cases failed against the earlier implementation while all 42
existing cases passed. After the fix, all 46 cases pass in the partial local
pinned Linux runner (`store-regression-before-final.log` and
`store-native-tests-final.log`). The cases cover wrong string/bool/list roots,
recommended fallback, managed read-only values, preserved raw data and valid empty
user dictionaries. Official GN and three-platform execution remain a separate CI
gate for the consolidated head.

The integration branch retains the feature commits from PRs 116–123. Those PRs
are superseded by PR 115 so that ordinary checks and explicit service/object CI
can validate one complete source head. In-progress manual runs for their earlier
snapshots are cancelled when superseded; completed results remain historical
snapshot evidence, never results for the final combined source. This does not
change ordinary PR workflows or native cutover requirements.

## App-wide native Fly bookmarks

The native BookmarkService owns Fly's shared flat collection in schema-1 Local
State `fly.bookmarks`, independent of profile cookie/storage state. Explicit adds
prepend a URL/title/timestamp row; exact duplicate URLs preserve the original
entry and position. Exact removals preserve other row ordering, unknown row data
and import metadata. Reads do not create a store. Typed-root fallback, future or
malformed records and managed writes are rejected using the common provenance
guard. Native PrefService owns asynchronous atomic disk persistence; storage-fault
and interruption acceptance remain pending and are not established by these
in-memory service tests.

The collection has one authority. Chromium's profile BookmarkModel remains the
separate native scope used by extensions and internal Chromium bookmark pages;
there is no synchronization or API replacement between it and the explicitly
labelled Fly collection. Final adaptation of remaining stock browser menu/toolbar
bookmark surfaces is still an M2 task, so their profile scope must not be presented
as the shared Fly list during default cutover.

The shared list is available from the sidebar's Fly bookmarks button and the
existing bookmark action/palette bindings. A native child picker searches titles
and URLs, opens a new foreground tab through Chromium's real navigator in the
selected browser's Profile, and supports pointer removal, arrow/page navigation,
Enter, Escape and Delete. Delete retains normal text editing while a selection or
text ahead of the cursor exists. Existing keyboard binding choices apply. The
command palette and picker share native surface machinery with separate callback
instances; URL strings never become command names or page-accessible APIs.

Removal runs after the input callback returns and rechecks picker lifetime and
membership. Store changes refresh the open list and native enabled state without
clearing its query. Closing, navigation, profile switching and destroyed source
contents cancel pending actions. Cached URL membership keeps row availability
checks from repeatedly traversing the complete store. Pages retain real native
visibility and restore their saved input focus on dismissal.

Only explicit bookmark actions write this list. Merely visiting a page privately
adds no entry; an explicit save from private mode follows the browser's usual
user-requested persistent bookmark behavior. Unsafe executable/data URLs and blank
tabs cannot be added. HTTP(S) origin exclusions retain scheme, host and port for
later frequent-site integration; no new quickmark store is introduced.

Seven new service cases bring the partial pinned Linux suite to 53 passing cases.
Two browser cases bring the current declaration count to 46, including two PRE
cases: shared-list navigation verifies distinct destination cookies and separate
native profile bookmark models; deletion verifies native text editing, deferred
cancelled clicks and restored page typing/visibility. The browser cases remain
unexecuted pending the complete current native build. Patch 16 registers the store
and GN dependencies. The unmodified control and frozen nine-patch runtime build
are separate snapshots; neither proves this bookmark implementation accepted.

If bookmark data becomes invalid while its picker is open, the picker closes
and the disabled sidebar button explains that recovery is needed and saved data
is preserved. The browser case also covers this transition; it cannot appear to
be an ordinary empty collection.

## Complete runtime-file provenance

New control/native build records capture GN's complete `//chrome:chrome`
`runtime_deps` closure after successful compilation. Each file has a streaming
SHA-256, size and POSIX mode where supported; directory contents, relative
framework links and their resolved payloads are covered. Paths cannot escape the
output directory. A complete snapshot rejects concurrent changes, missing files,
extra directory entries, changed link destinations and altered code/resources.
The browser executable must itself be included. Manifest bytes are bound into the
build record with a separate digest.

Native media fixtures now verify that closure before loading automation or
creating fixture profiles. This closes the gap where an unchanged launcher could
hide replaced component libraries. The verifier is a developer-side Python tool;
it adds no Python dependency to the browser's runtime. Tests use data-only files,
never a simulated browser. All 60 tooling cases pass locally, including replaced
libraries, changed resources/modes, escaping paths, framework links, malformed
manifests and pre-launch rejection. Malformed manifest metadata and paths outside the evidence directory are rejected
before file hashing. The same C++ service source still has 53
passing partial Linux cases; this tooling change proves no additional UI cases.

The actual Linux GN probe returned 9,210 runtime dependency entries, 5,301 unique
paths. That was a read-only graph query during the frozen build, not an attested
finished runtime. Existing M0/control and frozen `b98cae4` records predate closure
capture and retain their limited original scope. Their metadata is not silently
upgraded. After the frozen pipeline completes, revert its managed overlay using
its own frozen inputs, rerun the unmodified control build with the current tools,
and capture its full closure before applying/building the current native source.
The existing control executable/args checks remain available for those historical
records; any present runtime manifest must validate, including malformed ones.

An independent `fly-chromium-native-summary-audit.service` now waits for the frozen
pipeline. It binds the browser-source hash, frozen commit/fingerprint and fresh
summary timestamp, then requires every declared main case to finish SUCCESS
(PRE outcomes are folded by Chromium's launcher). It records
`evidence/native-acceptance-audit.json`; an empty, stale or incomplete summary
cannot pass. This audit does not mutate the frozen checkout or engine source.

## First complete native Linux executable

The frozen `b98cae4` nine-patch browser build completed on the actual pinned
source, with the matching H264/AAC control preset. `evidence/native-build.json`
records the clean source, overlay fingerprint, arguments and executable hash.
The following official native service run reports all 31 cases SUCCESS. The
scoped browser runner is still compiling its additional test support; its case
results must pass the independent summary audit before acceptance.

The first actual native audio fixture passes under X11 with the sandbox enabled
and focus emulation explicitly disabled. Evidence is
`fixture-evidence/control-smoke-21MafT/result.json`. Its four checks cover focus in
an editable field without hiding the page, tabs in the same native window,
paused audio retaining its state across tab switches, and continuing background
audio. It uses generated loopback PCM media and one profile; real Twitch/Pie,
workspace/split lifecycle and packaged/platform acceptance remain unverified.

A separate clean-profile launch was captured from the actual Xvfb desktop in
`fixture-evidence/native-desktop-zva_t74q/desktop.png`; the visible Fly sidebar and
embedded native browser confirm the Views shell is running. It also keeps the
sandbox enabled. This is the older frozen snapshot: the blank-tab row fix,
accountless defaults, clock/footer, preferences and bookmarks in later commits
are not validated by that image. The screenshot is a launch observation, not a
completed product UX or M1 acceptance result.

## Native teardown failure and current repair

The first browser suite executed actual cases and repeatedly crashed during
teardown. Chromium's dangling-pointer detector identifies
`EmbeddedBrowserNativeWidget::~EmbeddedBrowserNativeWidget` releasing its raw
BrowserWindowInterface pointer after BrowserManagerService deleted that model.
The first six test bodies reached teardown without an assertion failure, but
those crashes are failed tests, not passing cases.

The owned acceptance pipeline was stopped, its lease owner verified dead, and
its original logs/records archived as `evidence/lifetime-before-*`. The audit
records all_passed=false. No dangling-pointer diagnostics were suppressed.
Minimal repair `5fd0d8a` on `codex/chromium-native-lifetime` in the sidebar worktree
uses the model's native WeakPtr and refuses stale activation/show requests. It
passes pinned Linux Clang and npm test. Its new source fingerprint is
`bd7881e588206f4ff1fafb7e89eb8b234c3087ecd50c9d1030fa657cd9b2cfc0`.

That checkout and the reapplied nine-patch engine source are frozen again during
`fly-chromium-native-build.service`, the restarted acceptance service and summary
audit. New logs use `lifetime-build-systemd.log`,
`lifetime-acceptance-systemd.log` and `lifetime-summary-audit.log`. The rebuild is
still running. The repair is also cherry-picked onto the current development
branch; it has not yet been added to PR 115, whose old head cannot be treated as
runtime-safe. Finish the actual repaired browser/media tests before claiming M1.

## Safe incremental Fly-source refresh

`native.py refresh-overlay --root /mnt/storage/fly-chromium` updates only managed
Fly-owned source files after verifying the current engine state and replaying the
entire patch stack in a private staging directory. Upstream target paths and
resulting patched bytes must be identical. Patch changes require the existing
revert/apply path. Unchanged upstream/Fly files retain their timestamps, avoiding
unnecessary invalidation of large public-header dependency trees.

The operation journals both reviewed versions of changed owned files. An
interrupted refresh can use `recover` to restore clean pinned upstream; unknown
source edits still cause recovery to refuse writes. Core/compile-only overlays
cannot be promoted through refresh. Five new disposable-tree regressions cover
unchanged timestamps, add/remove, upstream/owner-edit rejection, interruption
before/after a write and preservation of edits after interruption. All 65 tooling
cases pass locally. This is tested tooling behavior; the running frozen repair
uses its original tools and does not exercise the new command yet.
