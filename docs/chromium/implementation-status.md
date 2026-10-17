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
- `gn gen out/FlyControl --check` is running. No compiled browser exists yet.
  The external `.fly-engine-operation/owner.json` identifies the current process;
  `configure.log` and `build.log` hold generation/build output.

## Decisions and topology

The approved full `chrome`/Views architecture remains in force. M1 must prove a
single native window composed from profile-bound workspace controllers, with
native extension registration and two visible split panes in one workspace.
No mixed-profile tab strip, visibility override or cookie swapping is accepted.
Actual upstream ownership seams must be inspected before implementing adapters.
The [pinned-source audit](./upstream-ownership.md) records the real native seams.
The generated [parity ledger](./parity-ledger.md) and
`chromium/baseline-contracts.json` capture 82 actions, 46 explicit palette entries,
28 preferences, 15 persisted inputs and the full product-feature checklist.
Source/test anchors are discovery aids, not claims of native coverage.

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
| M0 | pending | Source/tools/dependencies/hooks and contract capture pass; control generation/build/launch and clean second checkout outstanding |
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
- Those same three baseline tests failed under headless Ozone (visibility/focus
  timeouts). A later screenshot-capture repeat of the keyboard test timed out
  attaching to Electron while GN generation was running; no screenshot from
  that attempt exists. Do not treat it as a passing capture.
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

Finish the pinned source/dependency checkout, run upstream hooks, generate and
build the unmodified `chrome` control, then inspect native browser/profile/Views
and split-tab ownership at that exact revision. Record every exercised command
and blocker below as work proceeds.

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
  in progress after the bootstrap correction; log `configure.log`.
- A successful configure is chained to
  `python3 scripts/chromium/engine.py build --root /mnt/storage/fly-chromium --jobs 8`.
  Inspect the live operation lease and `build.log` before launching another command.
- `python3 scripts/chromium/pin-upstream.py --output /mnt/storage/fly-chromium/evidence/upstream-candidate.json`:
  passed; independent re-resolution verifies source `chrome/VERSION`. This is a
  pin-resolution check, not an engine-update rehearsal.
