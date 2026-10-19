# Native Chromium implementation record

The native port is **incomplete**. Electron remains the production runtime.
Full product, migration and packaged application acceptance remain outstanding.

## Active execution state

PRs [127](https://github.com/Arzuparreta/fly-browser/pull/127) (window controls,
sidebar expansion and indicators) and
[128](https://github.com/Arzuparreta/fly-browser/pull/128) (address/search,
shared history, frequent sites) are merged.

The shell-look block gives native Fly the Electron interface instead of
Chromium's: the embedded profile windows no longer show Chromium's toolbar
(`BrowserView::IsToolbarVisible`, patch 0023), so the sign-in button and
omnibox are gone and Fly's floating address surface is the only address UI.
`fly/ui/fly_theme.{h,cc}` carries the product palette (dark and light defaults
from `main.js`), the bundled IBM Plex Sans/Mono and Doto fonts (TTF copies of
`assets/fonts`, registered with the browser's fontconfig at start) and the
shared `FlyButton`/`FlyLabel`/background/border helpers. The sidebar,
compact rail, footer, pickers (Command Centre, address, history, Bookmarks
Manager, Workspace Dashboard) and Preferences use them. The footer follows the
Electron layout (mode badge, hovered link, zoom, notifications, connection,
workspace and clock badges); the clock wakes at the next visible change rather
than every second. Preferences builds the first 25 keyboard actions at open
(the whole catalogue took about a second) with a "Show all" control.

Local validation uses the engine checkout `/mnt/storage/fly-chromium`, output
`src/out/FlyNative`, `native.py refresh-overlay --include-upstream` followed by
an incremental `autoninja` of `chrome fly_unittests fly_browser_tests`. Browser
cases run serially (`--test-launcher-jobs=1`) on an owned Xvfb/Openbox display:
parallel jobs share one display and steal window activation, which produces
focus failures unrelated to the code under test.

## Historical checkpoint notes

The sections below preserve earlier execution checkpoints. Their words
“current”, “next” and “pending” refer to the checkpoint being recorded. Resume
from **Active execution state** above and the reviewed native parity inventory.


### Execution state recorded before PR 128

Primary main is `9089776`, after squash-merging PR
[126](https://github.com/Arzuparreta/fly-browser/pull/126). Its ordinary checks and
both latest-head manual matrices pass. All six downloaded artifacts independently
verify clean head `bbb8a09` and the pinned engine lock: 59 service cases on Linux,
macOS and Windows, and 50 Linux / 50 Windows / 39 macOS compiler outputs.
The external audit is `evidence/ci-bbb8a09-audit.json`. macOS object validation
excludes the unimplemented desktop adapter; no cross-platform linked browser or
package is implied.

PR [127](https://github.com/Arzuparreta/fly-browser/pull/127) contains native window
controls and sidebar expansion/indicators at `2b547de`, with one `impact:none`
label and passing ordinary checks. Latest-head manual matrices 39287341694
(services) and 39287345899 (objects) are running. Wait for all jobs, audit all six
artifacts, fetch/check the main ancestor again and then squash-merge. Its C++ and
patch stack are byte-identical to the accepted `9606b31` Linux checkpoint below.

The complete `bbb8a09` Linux run passes 59 services, 71 browser declarations
(69 main plus two PRE stages), H.264/AAC with MV3 and PCM audio. Independent audit
passes with fingerprint
`e77bf94a7625b99aa5fbe6f7ce4a4c67b3bab0bbe03fc98f5f0db93b3278b0b1`
and summary SHA256
`7376b157daea697650127e67ddf8094e0819ac3f5b160eca3fa054959b221b81`.
Evidence is archived externally in
`evidence/runtime-bbb8a09-before-window-controls`.

Development continues on `codex/chromium-native-history`, based on the unpublished
address/search block in `codex/chromium-native-address-search`. The preceding
window-control checkpoint `5d55d9a` passes the complete Linux run: 62 services,
74 browser declarations, both media fixtures and independent audit, archived in
`evidence/runtime-5d55d9a-before-sidebar-collapse`.

The follow-up implements automatic sidebar expansion without page reflow, a
compact native favicon/pin/audio rail, persistent pinning, keyboard focus and
Escape, live Appearance settings, native theme updates and suspension of the
hidden clock timer. Native profile windows and the sidebar use Chromium's
clip-window NativeViewHost backend consistently, preserving native transient
stacking while allowing the details panel to cover the page. Layout explicitly
positions the panel's clip before showing it. Real page clicks cross the desktop
and profile focus-manager boundary; address/toolbar focus remains deliberate.
Pointer-opened Preferences explicitly returns to the page when dismissed.

The complete `9606b31` Linux run passes all 65 services, all 78 browser
stages (76 main cases plus two PRE stages), H.264/AAC video with MV3 and PCM
audio. Independent audit passes with fingerprint
`81af250535f4ba7e8a1d41df3829d5a7d6d65f1b1da734bd8fd751c0d31fdb7d`
and browser summary SHA256
`225ce73b50e2b12606fc56544ff9edd699f1d9665adcb3d562ed6bd6c3870b12`.
The accepted checkpoint is archived in external
`evidence/runtime-9606b31-sidebar-accepted`. It covers native session restoration,
beforeunload, isolation, physical overlay hit testing/pinning, compact indicators,
editable page clicks, deliberate toolbar focus and actual document focus during
mere sidebar hover. npm test, documentation build, whitespace checks and all
88 Python tooling contracts pass. Hosted latest-head validation for this separate
follow-up remains outstanding.
The retained Electron keyboard/editor/editable regression passes on owned
Xvfb/Openbox in `evidence/native-sidebar-retained-keyboard-x11.log`; a headless
attempt did not receive the opening shortcut and remains separate failed evidence.

The address/search follow-up adds a floating native address surface using the
shared modal picker, current-profile navigation, twelve built-in and editable
custom aliases, DuckDuckGo bangs, workspace lookup, shared bookmark suggestions
and a bounded calculator without page execution. A native Search preference tab
edits the engine and aliases while preserving keyboard drafts, managed choices
and future/malformed data. Queries are not written to preferences. The later blocks below add history
suggestions and frequent sites; transactional legacy import remains outstanding.

Checkpoint `438daa4` passed 74 service cases and nine directed browser cases
with no retries (`evidence/native-address-438daa4-directed-audit.json`).
`a6d787c` also passed 74 services and eleven directed cases. These are historical
directed checks; the complete accepted address checkpoint follows below.

The first complete address run at `ac6866a` passed 74 services, 82/83 browser
stages and both media fixtures. It is failed evidence, archived in
`evidence/runtime-ac6866a-address-workspace-fixture-failure`: the new combined
workspace/bookmark fixture opened its selector before the newly created
workspace's initial navigation committed. `ab4b496` explicitly navigates that
fixture before opening the selector. Its complete Linux run passes all 74
services, all 83 browser stages (81 main plus two PRE stages), both media
fixtures and independent audit. The accepted archive is
`evidence/runtime-ab4b496-address-search-accepted`, with fingerprint
`dd2def3acba81d524bb23006289cef322e9ffb99e33001848c82e27bccb68c88` and summary
SHA256 `225ce545986d1b7cdfe691b68b1365c7944074351c50423499015eb10f678478`.
A subsequent platform fix sends Windows drive/UNC paths and POSIX home paths to
Chromium's file fixup; platform-specific expectations await the next matrix.

The history follow-up implements an app-wide, in-memory projection of native
Chromium history. It loads only existing, authorized normal workspace profiles,
keeps them alive during asynchronous queries, caps the combined projection at
500 visits and never queries OTR profiles. It owns no parallel persistent history
file. Native extensions still see only their profile's database. The native
history selector searches these rows and navigates in the selected destination
profile; address suggestions reuse the projection. Execution requeries native
history so deleted visits or retired workspaces cannot execute cached rows.

Clearing history uses an explicit native confirmation with Cancel as default,
checks deletion policy in every workspace before deleting any database, and
uses native expiration callbacks. Cookies, site storage and bookmarks remain
untouched. Read-query cancellation is separate from an accepted clear operation.
The five new browser cases (plus a PRE stage) cover aggregation, private
exclusion, native profile scope, deletion policy/cookies, future authorization, selector/confirmation and
live native deletions. Native history notifications refresh open projections;
profile teardown notifications are deferred until their dispatch has finished.
A PRE/restart case covers querying a dormant profile without opening a browser.
Checkpoint `7dd9c75` passes all 74 services and seventeen directed browser
stages (sixteen main plus one PRE stage), without retries. These include the new
history cases, private/address navigation, alias settings, palette, sidebar and
unloaded-profile restoration. Source compilation, GN checks and verification of
all 249 pinned dependencies pass. Its logs and summaries are prefixed
`evidence/native-history-7dd9c75`. This is directed validation; complete
history/browser/media acceptance, crash recovery and other-platform runtime are
still pending.

The next local block derives five frequent sites from that bounded history,
retaining the legacy seven-day half-life, future-time cap, exact-origin bookmark
exclusion and explicit hiding. Hidden origins use a validated `hidden_sites`
list in the shared bookmarks dictionary, preserving its items/unknown metadata
and policy. This is not another visit log. A native Appearance checkbox controls
`show_frequent_sites`; bookmark search offers the derived rows, opens them with
the selected profile and hides them explicitly. Selection requeries native
history and settings before navigation. Disabled display avoids loading history
for bookmark search. New core and desktop cases await execution; do not claim
complete acceptance from compilation checks.

The first full sidebar run at `fdf867e` passed services and media but failed
23 browser stages, primarily at Chromium's layer-order check, alongside focus
and test-default issues. It is preserved in
`evidence/runtime-fdf867e-sidebar-layer-and-focus-failure`. Subsequent directed
attempts and their causes have separate commit-prefixed logs/audits. Do not use
historical full-build metadata after directed rebuilds. Check all three native
services before changing the frozen worktree; configure/build through native.py
before full acceptance. Tests use official signed Openbox extracted only for
owned displays; no package was installed into the owner desktop.

The [native parity inventory](./native-parity.md) records all 82 actions and
28 preferences. Fourteen actions still have no native route; routed actions also
retain explicit product/platform/recovery gaps. This is incomplete M0–M6 work.

## Merged checkpoint before PR 126

Merged main is `00d6a2e` through PR
[125](https://github.com/Arzuparreta/fly-browser/pull/125), following PR
[124](https://github.com/Arzuparreta/fly-browser/pull/124). Both were squash-merged after
all ordinary checks and both latest-head manual matrices passed. The branch
was deleted and the primary checkout pulled to main. Exactly one `impact:none`
label covered the development-only integration. Previous integration PR
[115](https://github.com/Arzuparreta/fly-browser/pull/115) merged as `af2ad35`;
its results remain separate historical evidence. Superseded PRs 116–123 remain
closed with their review history preserved.

At PR 124 head `ae5e12e`, manual matrices 39197203490 (objects) and
39197205668 (services) pass on Linux, macOS and Windows. All tooling jobs pass
85 Python contracts. All six downloaded artifact sets independently verify the
exact clean commit and current engine lock, all 57 official service cases per
platform, and exactly 43 Linux/43 Windows/34 macOS applicable nonempty compiler
outputs. The audit is
`/mnt/storage/fly-chromium/evidence/ci-ae5e12e-audit.json`. macOS excludes the
unimplemented desktop adapter: these results do not establish a linked
Windows/macOS browser or an installable package. The superseded `27f6838`
matrices were cancelled; they are not evidence for the accepted head.

The complete sandboxed Linux run at the same head passes all 57 service cases,
all 64 browser declarations (62 main cases and two PRE stages), H.264/AAC video
with MV3 and PCM audio. The independent audit checks the exact source-declared
case set, fresh summary hash, clean source, binary/runtime closure and fingerprint
`4f2772f2a4c8386386024137e0112382a8da14caa679436ae5e42a3aa4da4fa4`.
`evidence/native-acceptance-audit.json` reports no failures and all stages passed.
The summary SHA256 is
`c6daa79bb05a72cf55b55ee4e42a02610e0dbcbed1cec86900e1446070702999`.

This validates product blank navigation; native global mute/media controls and
frozen-session resume; selected-profile docked DevTools; searchable workspace,
private and empty-desktop selection; ordering and rename; async profile/browser
completion guards; stable identity through both native discard implementations;
retained-document freezing/resume; automatic quiet deadlines and the
0/5/15/30/60-minute Performance preference; pointer/Space input and preserved
keyboard drafts. It does not complete the broader acceptance rows below.

Three separate disposable i3/Xvfb physical-input runs pass at the earlier,
source-equivalent focus checkpoint `27f6838`: `native-workspace-physical-yewmVJ`,
`native-workspace-physical-d1UhUj` and `native-workspace-physical-ILAxZZ` under
`fixture-evidence/`. A real click/type reaches the renderer field, the workspace
overlay blurs it without hiding it, and Escape restores continued editing.
Earlier failed captures sampled zero outer-window dimensions before native
compositor/layout readiness. No speculative activation patch was accepted.
Retained Electron recorder/editable regression also passes with disposable data.

Before changing frozen source or engine files, inspect user units
`fly-chromium-native-build.service`, `fly-chromium-native-acceptance.service` and
`fly-chromium-native-summary-audit.service`. All three completed successfully;
the sidebar worktree is detached at `0c8506e` after directed removal/move
validation. The full `ae5e12e` runtime evidence is archived in
`evidence/runtime-ae5e12e-before-workspace-retirement`. Archive the current
directed evidence before moving that worktree. Current logs are under `/mnt/storage/fly-chromium/` as
`current-build-systemd.log`, `current-acceptance-systemd.log` and
`current-summary-audit.log`; JSON/per-stage logs are under `evidence/`.

Runtime archives under that evidence directory retain actual earlier commits,
including `runtime-059735e-before-blank-navigation`,
`runtime-27f6838-before-auto-suspension-control`,
`runtime-84aee74-before-space-release` (the deliberately preserved missing-Space-
release fixture failure), and `runtime-c94412e-before-pr124-tooling-head`.
Do not infer later runtime acceptance from an earlier archive.

Merged PR 125 adds maintained live Twitch/Pie desktop evidence. The official signed
Chrome Pie package is external and never bundled. No owner profile is used.
All four maintained channel runs pass with identical harness/input hashes:

| Browser | Extension | Result file under `fixture-evidence/` |
| --- | --- | --- |
| Native Fly | official Pie | `live-twitch-PmJ3mw/result.json` |
| Native Fly | none | `live-twitch-j9WNez/result.json` |
| Unmodified Chromium control | official Pie | `live-twitch-DPGJdx/result.json` |
| Unmodified Chromium control | none | `live-twitch-yKIPwA/result.json` |

The audit `evidence/live-twitch-four-way-audit.json` checks every result,
checkpoint media presence/decoder status, declared scenario set, exact build and
harness hashes and all screenshot hashes. Fly passes actual player pause,
selector/preferences opening/closing, real tab hide/return, creation/switching
of another workspace and return, plus continuing live playback while hidden.
The control passes the same player/tab/background subset. These are disposable
Linux X11 build-tree runs against the public LofiGirl channel, not platform or
package acceptance. The signed Pie CRX hash is
`7ba1ab6266b643c8200945d4d6933f3637500c79e0903beb41c10fa3a4255e50`;
its external package record is
`fixture-evidence/pie-chrome-package-Z2LeJ0/package.json`.

Pie modifies main-world focus and visibility, so the fixture records exposed
values alongside read-only isolated-world native observations. No Fly or test
code rewrites page APIs. The initial prototype's main-world focus assertion
failed because of those Pie overrides; the revised native/exposed observations
retain the assertion against actual native focus. Other failed attempts preserve
an absent-worker timing check, duplicated hidden player controls, an advertisement
background-pause policy, and asynchronous onboarding stealing the selected tab.
The maintained fixture verifies worker readiness, selects the visible player,
waits for the real stream, closes only known disposable Pie welcome tabs and
bounds frame waits. The interrupted initial unbounded-frame attempt is recorded
in `evidence/live-initial-harness-interruption.json` and never counted as a pass.

The public Monstercat sample was offline; the Music directory identified an
active sample. Homepage inspection observes actual decoded video, but its live
badge is outside the channel player's LIVE label. The homepage pause sequence
has not been accepted: a channel result does not establish that separate flow.
See [live-media.md](./live-media.md) for reproducible commands and limitations.
All 88 Python tooling/fixture guards, npm tests, documentation build and whitespace
checks pass. The latest-head hosted contracts matrix 39247855539 also passes
all 88 cases on Linux, Windows and macOS; ordinary PR checks passed before merge.

Active branch: `codex/chromium-native-workspace-retirement`, based on merged
main. Commits `67f5506` and `9ab2944` implement removal through the actual native
context menu, confirmation and coordinated normal/private browser close. A
process-local retirement lease prevents new windows during close; persistent
retired metadata prevents extension or stale startup entries reopening removed
profiles. Cancellation retains the catalog and files; confirmed removal retains
profile files for recovery while a recreated workspace gets a fresh directory.
Native private download termination is disclosed in the confirmation.

Directed Linux validation passes all 14 workspace service cases and both new
removal browser cases. The initial browser attempt exposed repeated top-level
`const` declarations in the extension probe; the corrected IIFE probe passes.
The failed attempt is preserved in
`evidence/retirement-targeted-67f5506-before-worker-scope-fix/`; accepted directed
results are `evidence/retirement-targeted-browser-fixed.json` and
`evidence/retirement-targeted-stage.json`, tied to `9ab2944`. This is directed
coverage, not a new full-suite or cross-platform acceptance claim.

The same functional block adds the native Move to workspace action and tab
context menu, searchable destination picker and scoped native-close transaction.
A move opens a fresh destination-profile WebContents only after native close
consent, transfers Fly's logical UUID plus pin/mute metadata, and navigates using
the destination's own cookies. Native extension tab/window identities remain
Chromium-owned. Private moves stay in the destination's off-the-record profile.
Cancellation or a lost destination retains the source. Unloaded profiles load
asynchronously before requesting source close. Origin-bound blob/extension URLs,
credentials in URLs and pending navigations are currently ineligible.

All five new move browser cases and both removal cases now pass together at
`0c8506e`, independently checked against the exact seven-case result set in
`evidence/native-tab-move-directed-audit.json`. All 59 native service tests pass
at the earlier core-equivalent `8ef4793`; the complete latest-head run follows.
The new cases cover real menu/picker input, destination storage, identity/restore,
beforeunload cancellation, destination loss, private isolation and both
normal/private unloaded targets. The retained Electron recorder/editable test
passes with disposable data (`native-workspace-move-retained-keyboard.log`).

Earlier move failures are retained: `8ef4793` rejected fresh destinations because
Chromium creates an initial NavigationEntry; `aa61f6d` diagnostics isolated that
condition. The fix requires native initial-blank state, no pending navigation and
an initial entry. `156a2e0` then exposed a normal-workspace fallback persisting
during private source closure, plus an ambiguous generated-name fixture query.
`0c8506e` defers fallback until move completion and gives that fixture a unique
name. Real pointer menu selection also waits for the visible title control.
The original failed summaries/logs remain under their exact commit prefixes.
The new cross-profile move is not yet crash-atomic across independent native
session files; M4 transaction/recovery acceptance remains outstanding.

The first complete `e1aa6dd` runtime attempt passes 59 service cases, 70 of
71 browser stages and both multimedia fixtures. One repeat removal-menu action
missed a rebuilt row before layout; the run and failing independent audit are
preserved under `evidence/runtime-e1aa6dd-removal-pointer-failure`. The fixture now
waits for the row's drawn visible hit area and confirms actual menu dispatch
before awaiting the dialog. The full suite must be repeated at that correction;
the failed run is not accepted as a pass.

PR [126](https://github.com/Arzuparreta/fly-browser/pull/126) is the active draft,
with exactly one `impact:none` label. The first-head ordinary checks pass;
manual object/service matrices 39255701582/39255705748 are superseded by the
corrected head and must never be used as its acceptance evidence.

Next: run the full Linux native suite/media audit and latest-head hosted
object/service matrices; integrate this
functional block only after those checks pass. Continue native Fly address/window
UI, remaining commands/preferences, services, transactional migration, all-platform
packages, update rehearsals and full acceptance through M6. Electron remains
production. No owner profile has been read or migrated, no native package has
passed acceptance and no release has been published.

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
The reviewed [native action/preference inventory](./native-parity.md) records
explicit partial/pending status for every action and preference.
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
| M1 | pending | Linux full browser and 64-case suite pass; remaining topology/desktop acceptance and macOS adapter outstanding |
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
| A09 | pending | Native deterministic audio/video/MV3 pass; maintained real Twitch/Pie comparison and complete platform coverage outstanding |
| A10 | pending | Linux native freeze/automatic deadline/resume and both discard paths pass; complete lifecycle/platform acceptance outstanding |
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

## Canonical preference action entry points

The native keyboard settings action now opens/focuses its existing Keyboard tab
without closing the editor or discarding a draft when invoked again. Switching
from Appearance uses the same native surface and stops any ongoing recording.
`cycle-clock-format` also uses the shared validated preference service and native
policy availability, matching the sidebar's cycle behavior. Browser coverage
checks the repeated entry/draft/focus transition and canonical clock action;
those cases remain pending execution in the current full browser. Pinned Clang
accepts the changed UI and test source. The retained Electron recorder/editable
baseline is validated separately with a disposable Xvfb profile.

## Native blank-tab navigation follow-up

The `codex/chromium-native-blank-navigation` branch continues from the PR 115
runtime/CI checkpoint without changing its frozen acceptance source. Product new
tab actions now navigate to actual `about:blank` through Chromium NavigateParams
in the owning browser/profile. Keyboard commands, the sidebar button, newly
created persistent/private workspaces, split creation and adjacent-tab menus use
the same function. Command enablement and native insertion/pin rules still apply.
The new browser case covers committed blank URLs, insertion position and native
BrowserContext identity; the physical adjacent-tab case also asserts the URL.
Pinned Linux Clang accepts all four changed translation units and the browser
fixture. This follow-up still requires a full runtime run and latest-head CI;
it does not implement the remaining custom Fly new-tab page or floating address UI.


### Native global tab audio follow-up

`toggle-global-audio-mute` now snapshots the Fly desktop's live native tab
identities across hidden persistent windows and off-the-record contexts. As in
the retained product, any unmuted tab makes the operation mute all existing tabs;
when all are muted, it unmutes them. Native `SetTabAudioMuted` records the user
reason and notifies the existing tab/extension infrastructure. It neither writes
a global preference nor changes profile, selection or page visibility. Closed
windows are excluded and weak identities tolerate observer-driven destruction.
A browser case covers mixed state, hidden/private tabs, native reasons, stable
extension IDs, unchanged selection/visibility and newly created tabs. Runtime
execution and latest-head hosted validation remain pending.


### Native global media follow-up

`toggle-global-media` now controls existing, controllable native Media Sessions
across the Fly desktop. Each session's playback state is snapshotted before
sending UI-originated suspend/resume requests. Chromium keeps its normal audio
focus policy, renderer/player fallback and page-provided Media Session handlers.
No product script enumerates or replaces media elements, and tabs without a
session do not acquire one. This intentionally follows native controllable media
semantics rather than reviving never-started elements on every page.
Two real-audio browser fixtures exercise ordinary page action handlers and the
native player fallback, then check pause/resume from another workspace while
retaining native selection/visibility. Resume assertions run without a synthetic
user gesture. Their execution is pending; this does not establish real Twitch/Pie
acceptance.


### Native DevTools follow-up

`toggle-devtools` now dispatches Chromium's own command for the selected native
tab. The existing command controller retains availability/policy checks and the
native frontend owns docking, target registration and lifecycle. A browser case
opens and closes the real docked frontend in two isolated workspaces, verifies
profile ownership and stable inspected tab identity, and checks that both the
inspected page and docked frontend remain visible. Runtime execution is pending.


The first `bf464eb` runtime pass succeeds for global muting and site-defined
Media Session handlers. The no-handler fallback fixture references an `audio`
lexical binding created in a previous `ExecJs` invocation; that harness scopes
its script, so later evaluations raise ReferenceError. Querying the actual audio
node by ID in each evaluation fixes the fixture without changing pause/resume
assertions or injecting a synthetic user gesture. Preserve the failed summary
and rerun the complete updated browser/media pipeline before accepting it.


### Native workspace chooser follow-up

The Workspaces button and `toggle-dashboard` open a native searchable chooser
for persistent workspaces and live private windows. Selection uses stable
workspace IDs or ephemeral weak native window identities. Catalog edits refresh
the projection; malformed/future stores cannot be overwritten. Opening another
surface, changing workspace, closing the source or starting quit cancels queued
choices. The empty desktop uses an explicitly source-free picker, without a fake
page; choosing New workspace creates one real profile-bound blank tab.
Browser cases cover the actual shortcut and search keys, hidden-page visibility,
stale queued choices, private selection without catalog writes and creation from
the empty desktop. Execution remains pending. Rename/delete/reorder controls,
tab transfer and the remaining workspace management UX are still outstanding.


### Native workspace ordering follow-up

Persistent workspace headings now offer native pointer/keyboard context-menu
commands to move up or down. Boundaries and non-editable catalogs disable the
corresponding actions. The menu closes before a non-nestable task re-resolves the
stable ID and validates the current catalog and policy; row rebuilding cannot
invalidate its callback. The existing atomic catalog service owns ordering,
while active profile, open tabs and private windows keep their native identities.
A three-profile browser case checks actual pointer menus, boundaries, keyboard
workspace cycling after reordering, preserved cookies/metadata, and rejection of
a queued move after a future schema arrives inside the menu loop. Runtime
execution is pending; rename/delete controls and tab transfers remain unfinished.


### Native overlay focus regression probe

An actual workspace-picker compositor capture at
`fixture-evidence/native-workspace-picker-RIXiNy` shows a focused native search
field while the page still reports `document.hasFocus() === true`, with CDP focus
emulation explicitly disabled again after initial frame capture. A native browser
regression now checks this boundary without CDP and logs the Aura active/focused
window names on failure. This probe is not an accepted fix; native activation
ownership must be diagnosed and the full suite rerun before accepting a repair.


The unchanged activation implementation passes the complete `ce7f221` native
pipeline: 53 unit cases, 55 browser declarations (53 main cases and two PRE
stages), video/MV3 and audio/PCM; the independent summary audit also passes.
Evidence is `evidence/native-acceptance-audit.json`, with the same commit and
fingerprint as `native-build.json`. This validates blank navigation, global
mute/media, native DevTools, workspace selection/ordering and the native focus
regression described above. It does not establish the remaining product or
cross-platform package acceptance.

The proposed non-activatable-browser change was not accepted: the native
regression passes without it, while the external capture mixes direct CDP input
focus with native desktop input. A pure XTest capture without CDP input reports
an initially unfocused page, so it cannot prove a focus transition. The original
captures remain evidence of this harness discrepancy, not an established Aura
root cause or a product repair. The browser regression now explicitly waits for
both native render-widget focus and `document.hasFocus()` before opening the
chooser, preventing an initially unfocused page from making its blur assertion
pass vacuously. The strengthened case needs its own runtime execution.


### Native workspace rename follow-up

Persistent workspace context menus now offer Rename workspace. A native,
window-modal Views dialog edits the display name through the existing validated
catalog transaction. Duplicate, empty, oversized/invalid names and a stale or
policy/future catalog leave the dialog open with an error instead of overwriting
data. The callback re-resolves the stable workspace ID; it holds neither the
originating row nor a browser/profile pointer. Name changes keep profile storage,
tabs, extension identity and the active workspace. The selected workspace's
status heading updates immediately.

The widget and delegate are both client-owned and destroyed in that order.
Workspace selection, selected-browser close and application quit cancel the
editor. Escape cancels, Return saves, and the modal surface receives keys before
Fly page shortcuts. A browser regression uses the actual context menu and dialog
accelerators, checking Unicode names, duplicate rejection, cancellation, stale
future-schema rejection, unchanged profile/tab identity and retained page cookies.
Pinned-source compilation and the full runtime pipeline must pass before this
follow-up is accepted. Delete and cross-profile tab transfer remain outstanding.


### Native discard identity follow-up

Pinned Chromium can either retain a WebContents during memory discard or replace
it with a renderer-free WebContents. The latter invokes AboutToBeDiscarded before
tab insertion. Fly now observes that native seam and transfers only its small
product identity to the unpublished replacement in the same BrowserContext.
The old instance relinquishes ownership of the live-ID reservation; its later
destruction cannot allow a second restored tab to claim the replacement's ID.
Already published identities and cross-context transfers remain forbidden.

Two browser fixtures explicitly exercise both upstream feature configurations
with real native lifecycle discard, verifying hidden pinned tabs, unchanged
selection/profile, retained identity and collision protection, then native reload,
document.wasDiscarded and profile-local cookies. These cases need pinned compile
and full runtime execution. This is identity preservation for Chromium memory
reclamation, not the unfinished retained-document freeze/resume feature or its
hibernate/wake commands.


### Native retained-document suspension follow-up

`hibernate-tabs` now requests native freezing votes for eligible hidden pages
across the Fly desktop, including private contexts. `wake-all-tabs` withdraws
Fly's requests only in the selected workspace/profile, without loading unopened
or discarded tabs. The command adapter uses public PerformanceManager freezing
APIs: Chromium controls connected browsing instances, visibility, locks, capture,
WebRTC, loading and its normal recently-visible/audible grace periods. A request
is distinct from the policy actually freezing a page. Fly additionally excludes
pinned tabs, active video/media, debugger targets, unloaded documents and every
tab in a profile with an active download.

A scoped per-WebContents observer releases its vote on visibility, loading,
primary-page replacement, media playback, pinning, debugger attachment, discard
or destruction. Visibility restores either split pane through the same native
path; no page script changes visibility, focus or media. The existing command
adapter owns this small lifetime object, retaining the browser UI's current GN
dependencies rather than adding a dependency from core session metadata to UI.
Automatic suspension timeout preferences remain unfinished.

The browser regression shortens only Chromium's test protection grace periods,
then checks actual graph lifecycle transitions, retained unsaved JavaScript data,
workspace-scoped resume, debugger/pin/visibility protection and unchanged Fly
identity. Real audio coverage also rejects suspension of the already-playing
hidden media fixture. A second real-audio case freezes a paused native session,
then resumes it from another workspace without a synthetic user gesture. Global
media resume first withdraws Fly's freeze vote so native/site playback handlers
can run. Visible split panes reject suspension requests. Compiler checks and the full native pipeline must pass;
this is not yet accepted A10 evidence. Ordinary Chromium memory discard remains
a separate lifecycle operation with separate replacement/retention coverage.


The preceding rename checkpoint `41b86d1` passes the complete Linux native
pipeline: 53 unit cases, 56 browser declarations including two PRE stages,
video/MV3 and audio/PCM. The independent summary audit reports no failures and
matching source/binary provenance. Its evidence is preserved before starting the
combined discard-identity and retained-freezing run; neither new lifecycle case
inherits this earlier result.


### Asynchronous workspace creation validation

Profile-load and browser-factory completion now both re-resolve the requested
stable workspace ID against the current catalog and exact ProfileManager child
path. Retirement, schema recovery, profile reassignment or quit while the load
is pending cannot open an unmapped recovery browser, create a fresh blank tab or
switch the selected workspace. A browser created just before the second check
fails is closed through the native window path before adding a tab. No original
profile directory is deleted.

A browser case starts real asynchronous profile creation, retires metadata or
installs a future catalog before the queued completion, waits on a second native
profile-ready callback and verifies no browser was opened, no selection changed
and the retired/future data stayed intact. Runtime execution remains pending.


### Native page pointer/keyboard focus coverage

The external XTest capture still fails to focus/type into its page field when
using guessed compositor coordinates, including with an isolated i3 window
manager. Its key reaches Chromium's address bar instead. The regression suite
now adds a stricter browser-owned input case: start focused on a native sidebar
control, click the renderer input using RenderWidgetHostView screen geometry,
type through native events, open the workspace overlay, verify renderer blur
without hidden visibility, Escape and continue editing the same field. It uses
no WebContents::Focus call or CDP input/emulation to establish page focus, and
logs renderer/desktop geometry on failure. Execution is pending. This does not
resolve the external capture discrepancy or establish packaged desktop input
acceptance by itself.


### Automatic native suspension timeout and Performance preferences

The native preference service validates the existing 0/5/15/30/60-minute contract,
with 15 as the missing default and 0 as an explicit disabled choice. Reads/ticks
never create a preference write; changes preserve unrelated and unknown metadata.
Malformed/future stores and policy-managed values cannot be overwritten.
A native Performance tab edits this setting immediately with pointer or keyboard,
without applying/discarding an existing keyboard-binding draft or leaving its
recorder active when switching sections.

A thirty-second desktop scheduler snapshots live native tab identities and
requests freezing only after a continuously quiet eligible hidden interval.
Visibility, navigation/loading, pinning, playback, debugger transitions and
manual resume reset that interval. Disabled or unreadable settings immediately
withdraw automatic requests; an explicit manual suspension remains a separate
choice until a resume/protection event. This deliberately gives the disabled
setting an automatic-only scope; the legacy periodic scan resumes every frozen
page when its timeout is zero, including manual requests. App close withdraws all Fly requests before native beforeunload handling;
close cancellation restarts the configured scheduler. Native Chromium still owns
actual freezing, connected-page eligibility and its protection grace periods.

Four native service cases cover defaults, disabled/restart persistence, allowed
values, metadata preservation, malformed/future data and managed precedence.
Two browser cases drive controlled monotonic scheduler ticks through actual
native graph freeze/unfreeze and retained document assertions, and use the
Performance control with real pointer/Space input while checking keyboard drafts
and future-store refusal. Pinned compile, Electron recorder/editable regression
and the full native run remain required; no new automatic-suspension acceptance
is claimed before those results.


The first `84aee74` automatic-suspension run passes all 57 service cases. Its
Performance UI case clicks successfully but then sends a Space key press without
its release. Chromium's native button controller activates Space on release on
this host, so the expected second cycle times out. Add the native release event
and assert button focus, preserving the pointer and keyboard cycle assertions;
rerun the complete pipeline before accepting the UI. Preserve this failed
summary rather than treating the syntax/service passes as runtime acceptance.


### Explicit reviewed upstream patch refresh

The next browser-lifetime hooks require changing upstream C++/GN patches. The
existing Fly-only incremental refresh deliberately rejects those changes. An
explicit `refresh-overlay --include-upstream` mode now stages the entire reviewed
stack against the unchanged pinned revision, validates existing managed hashes
and new targets against pinned originals, and journals before writing. Added,
changed and retired upstream targets participate in the same recovery protocol;
unchanged source/header bytes retain their timestamps. Default refresh behavior
and core/compile-only promotion guards remain unchanged.

Seven disposable-Git regressions exercise opt-in changes, unchanged timestamps,
added/retired targets, late-hunk preflight failure, interruptions before/after
new/changed/restored writes, owner-edit races and recovery refusal, mode guards
and CLI scoping. This is tooling work on the independent
`codex/chromium-native-reviewed-patch-refresh` branch. Full Python contracts,
npm/documentation/whitespace checks and any actual-engine exercise must be
recorded before accepting it. Do not treat it as completed workspace deletion
or retirement enforcement.


The reviewed upstream refresh tooling passes all 85 Python contracts, npm tests,
documentation build and whitespace checks. A real pinned-engine probe at tool
commit `27641f2` privately replays the nineteen patches plus a temporary comment
patch to `chrome/browser/profiles/profiles_state.cc`, applies that one changed
source and restores the original stack. All source hashes return to the original
overlay fingerprint and all 73 other managed files keep their timestamps. No
public header or browser behavior changes. Evidence is
`evidence/reviewed-patch-refresh-engine-proof.json`; the temporary probe patch
was external and is not part of the product stack. This validates actual-engine
incremental patch refresh, not workspace deletion itself.

### Workspace retirement and cross-profile movement block

PR [125](https://github.com/Arzuparreta/fly-browser/pull/125) is merged as
`00d6a2e`; ordinary checks and explicit tooling run 39247855539 pass on all three
platforms (88 cases each). Its branch is deleted and the primary checkout is on
pulled main. The current local branch is
`codex/chromium-native-workspace-retirement`, based on that merged checkpoint.

The next functional block closes the workspace lifecycle and destination-profile
move gaps. Pending retirement now has a process-local creation lease, with
persistent retired-profile checks in native window eligibility and startup
selection/restoration. No new browser for that original/OTR profile may race a
pending close; cancelling or crashing before commit releases the lease without
retiring metadata. Startup skips retired last-opened entries and a stale last-used
choice falls back to a live catalog identity. An explicitly empty retired catalog
gets a fresh recovery directory, never the old Default profile's cookies.

A native removal confirmation retains the existing last-workspace restriction,
validates current stable ID/path/managed/schema state and coordinates all native
windows for the original profile, including OTR windows. Native warnings and
beforeunload run before closing; metadata retires only after native close
completion. Private downloads are explicitly covered by the removal confirmation
because closing the complete OTR context ends them. Profile picker metadata is
removed while native ProfileManager retains lifetime ownership. Original profile
files remain available for recovery; a recreated display name receives a new
storage identity. This does not implement secure erasure or migrate old data.

Two service regressions cover retirement queries, managed/future metadata and
scoped exact-path leases. Two browser regressions use actual context menus,
confirmation/beforeunload cancellation, private windows, a real MV3 background
worker attempting windows/tabs creation, stale startup entries, fresh recreated
cookies and future/last-workspace refusal. Pinned syntax checks pass for the core,
UI coordinator and changed upstream profile sources; linked targeted execution
is still required. Cross-profile movement is the next implementation in this
block. This source checkpoint is not yet accepted A05/A06 completion.
