# Native Chromium implementation record

The native port is **incomplete**. Electron remains the production runtime.
No native application, migration or package has passed acceptance yet.

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
  refinements are on `codex/chromium-control-build` (PR/commit recorded below
  when created).
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
the Twitch/Pie reproduction have not run. macOS and Windows execution hosts
have not been established.

## Next action

Wait for the existing unmodified `chrome` build, inspect its process exit and
`evidence/build.json`, then run the same-pin control fixture with and without the
probe. Source/dependencies/hooks/generation are complete; do not repeat source
sync under the active build. Native browser/profile/Views ownership findings are
recorded, but the M1 composition adapter and its native browser tests still need
implementation. No external platform or build failure is being claimed: the
local compilation is simply unfinished. macOS/Windows hosts remain unestablished.

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
