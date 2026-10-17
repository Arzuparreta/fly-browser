# Fly on Chromium: implementation plan

Status: implementation started at M0; native runtime and cutover remain
outstanding. The [execution record](./chromium/implementation-status.md) tracks
actual evidence. This plan does not announce that Fly has migrated.

Planning baseline: `a477efd0cd47c24d8053ce2be013446054268fe9` in
`Arzuparreta/fly-browser`. Re-audit changes made after that commit before starting.
The [agent handoff](./chromium-rework-handoff.md) is the executable brief.

## 1. Objective and authorisation

Build the Fly experience on the **complete Chromium browser**, including its
native browser, profile, extension and tab infrastructure. Use the `chrome` layer,
not merely Blink, `content_shell`, CEF, Qt WebEngine or an Electron wrapper.

The owner has explicitly said that rewriting existing code is acceptable. Choose
the architecture for the resulting browser, not for proximity to today's files.
Keep Fly's keyboard and mouse workflows, isolated workspaces and visual identity.
Do not substitute a stock Chromium window with Fly branding for the finished app.

The implementation agent owns the work end to end: reproducible build, native
integration, feature parity, data migration, packaged application, validation,
documentation, and the repository's PR/check/merge procedure. A feasibility
report, vertical slice, green JavaScript tests, or a browser that only launches
does not complete this assignment. Work through the phases below, recording
evidence and actual external blockers as necessary.

This planning task does not launch the implementation or authorise changing the
owner's live profile. Use disposable profiles and copies for implementation tests.
Do not publish a release, create new credentials or pay for build infrastructure
without the authorisation those actions require. Normal implementation decisions,
local builds, commits and the PR procedure are covered by the implementation brief.

## 2. Product invariants

- Fly remains an independent project. Never fetch, merge or open PRs against the
  former Mise browser repository. Chromium upstream is a separate dependency.
- Linux desktop is the primary interactive development target. Completion covers
  the platforms currently shipped by Fly: Linux, macOS and Windows. A successful
  Linux port alone is an explicitly partial result, not cross-platform completion.
- Browser chrome, navigation and permissions belong to trusted browser code.
  Websites get native Chromium web APIs and the native security boundary.
- Keep keyboard, pointer, focus, accessibility and IME behaviour equally usable.
- A workspace's storage identity survives a rename. Different workspaces keep
  separate cookies, site storage, service workers and extension settings.
- Hidden, focused, playing, muted, frozen, discarded and restored are different
  states. Switching tabs must not issue a media play/pause operation.
- Keep native Chromium behaviour wherever Fly has no intentional product override.
  Record and test each intentional override; do not keep inherited workarounds
  merely because they exist in the Electron implementation.
- Preserve explicit user choices, including disabled features, removed bundled
  extensions, empty extension selections, keybind overrides and privacy settings.
- Do not trade away sandboxing or site isolation to achieve a memory target.
- No cloud account, telemetry, browser sync or password vault becomes enabled as
  a side effect of adopting Chromium. Existing Fly policy remains the baseline.

## 3. Architecture decision

### Native browser core and UI

Use Chromium's C++ browser layer and Views framework for the persistent window,
vertical tab/sidebar UI, workspace heading, content layout, status surfaces and
keyboard command routing. Render websites through native Chromium content views.

Use trusted WebUI for appropriate internal documents such as Preferences, the
command centre and the new-tab page. Existing HTML/CSS and algorithms can be
ported when useful, but no permanent Electron host renderer is required. Use
explicit, typed Mojo interfaces for internal pages. Native code owns authoritative
state; WebUI receives projections and sends validated commands.

Prefer upstream implementations of profiles, extension services, navigation,
downloads, permissions, crash recovery, printing/PDF, zoom and lifecycle. Add
Fly-specific adapters and product policy where necessary. Remove the Electron
extension compatibility layer instead of implementing a second `chrome.*` API.

Follow the selected revision's browser design conventions: prefer
`BrowserWindowInterface`, `TabInterface`, window/tab features and profile-keyed
services over introducing new dependencies on the legacy `Browser` god object.
Use modular GN targets and explicit lifetime/dependency ownership. Inspect native
vertical-tab facilities as well as native split tabs before replacing them.

Suggested ownership boundaries, to implement against the selected upstream tree:

| Component | Responsibility |
| --- | --- |
| `FlyWindowController` | Native window, active workspace, focus, overlays and content layout |
| `FlyWorkspaceService` | Stable workspace IDs, profile mappings, ordering and lifecycle |
| `FlyWorkspaceBrowser` | Profile-bound native browser/tab model and extension window integration |
| `FlyCommandService` | Action metadata, bindings, modes, command dispatch and recorder validation |
| `FlySessionService` | Fly layout/session metadata and integration with native restore |
| `FlyMigrationService` | Transactional legacy import, backups, progress and recovery |
| `FlyUserContentService` | Local scripts/styles, grants, storage, watchers and injection |
| `FlyPreferencesService` | Fly settings, legacy mapping and native policy integration |
| `FlyUpdateService` | Verified product updates and install-channel behaviour |

These are proposed Fly components, not existing Chromium API names. Confirm the
actual upstream types and paths at the pinned revision, and record replacements
in the implementation log. Do not invent an API and claim that it compiled.

### Workspaces and profiles: resolve this first

Use a real persistent Chromium `Profile` per persistent workspace as the default
design. A storage partition alone does not reproduce profile-scoped extension
services and settings. Keep app-wide bookmarks, history/search and Fly preferences
behind an explicit app-level service; do not leak workspace cookies through it.

Chromium's native browser/tab structures are profile-bound. Therefore **one native
Fly window containing several isolated workspace contexts is an integration task,
not a free property of profiles**. Implement a window controller that composes
profile-bound browser controllers and their native content views. Adapt the native
window/controller boundary where required, while preserving accurate native
browser registration, activation, extension window IDs and profile ownership.

The first native milestone must demonstrate two independently logged-in workspaces
behind the intended single-window UX. Do not put mixed-profile contents into a
tab model that assumes a single profile. Do not simulate isolation by swapping
cookies, rewriting origins, or globally rebinding extension APIs. Do not silently
turn workspaces into unrelated desktop windows to avoid this requirement.

Explore existing native split-tab and tab-layout facilities at the selected
revision before implementing parallel state. Split view must expose both visible
pages as visible, while retaining one focused/active tab for commands and standard
extension queries. Current Fly splits pages within the current workspace; preserve
that scope. Cross-workspace moves create a destination-profile navigation, not a
live `WebContents` transfer across profile boundaries.

For private browsing, use native off-the-record profile mechanisms. Preserve the
documented private behaviour without accidentally persisting private tabs, history,
extension state or migration records. Any changed private-extension capability
must be documented and covered by tests.

Keep persistent Fly tab/workspace identifiers separate from native extension
tab/window identifiers. Native IDs follow Chromium's lifetime and restoration
semantics; do not fabricate permanent `chrome.tabs` IDs across application restarts.
Resolve app-wide bookmark/history UX against profile-local native services
explicitly: global Fly search may aggregate authorised profile history, while an
extension sees only the history and bookmark scope granted to its own profile.
Avoid two independent authoritative stores that silently diverge after edits.

### Keep web behaviour native

Use the platform's real show/hide and lifecycle paths. No script replaces
`document.hidden`, `visibilityState`, `hasFocus`, media methods, networking APIs
or browser identity to repair the integration. No generic suppression of valid
visibility events. Any surviving site-specific exception needs a reproducer and
its own removal condition.

Overlays and the sidebar change focus and geometry; they do not automatically
make a page hidden. Two visible split panes remain visible. A background tab is
hidden but may play audio. Freezing retains the document; discarding does not.
Keep Fly's user-facing suspension promise and distinguish it from native memory
discarding. Do not promise unsaved document retention across a discard.

### Security boundaries

Keep Chromium sandboxing and site isolation enabled in packaged builds and in
the integration tests that claim to verify security. No production
`--no-sandbox`, disabled web security or single-process shortcuts.

Internal WebUI capabilities must be origin-bound and narrowly typed. Imported
titles, URLs, scripts, styles, diagnostics and history are untrusted data. Render
them as data; isolate any untrusted HTML. Websites and extension pages cannot
invoke Fly's file, update, desktop-integration or terminal capabilities.

Preserve deny-by-default device permission behaviour and explicitly map existing
trusted-site choices. Do not turn a legacy trust entry into blanket device,
filesystem or browser privilege. Keep the terminal workflow clipboard-mediated:
no page-originated or automatic command execution.

## 4. Repository and build arrangement

Keep this GitHub repository as the product source of truth. Store Fly's native
sources, resources, build configuration and an ordered Chromium patch series
here. Fetch the official Chromium source and dependencies into an external or
ignored checkout. Do not commit Chromium's full source tree or compiled output.

Proposed tracked layout; these paths do not exist yet:

```text
chromium/upstream.lock.json       exact source/tool pins and provenance
chromium/patches/series           ordered, documented integration patches
chromium/patches/                 reviewable changes to upstream files
chromium/overlay/                 new Fly sources/resources copied into the tree
chromium/gn/                      development and distribution build configuration
scripts/chromium/                 setup, sync, apply, build, run, test and packaging
tests/chromium/                   fixtures, native/UI test wiring and benchmarks
docs/chromium/                    build, architecture, upgrade and recovery guides
```

Use Chromium's supported depot_tools, GN and build toolchain at the pinned
revision. Prefer a current supported stable revision, with a reproducible source
SHA rather than a floating branch. Generate the upstream revision/version record
from the actual checkout; do not hand-maintain version literals. Record the
depot_tools revision, dependency state, relevant build arguments and artefact
provenance. Do not depend on private Google build services, credentials or keys.

Build the complete browser target. First build unmodified upstream as a control,
then the Fly overlay/patch stack. Verify a second clean checkout reproduces the
configured build inputs and applies the same patches. Do not claim byte-for-byte
reproducibility unless it has actually been measured.

Scripts must detect missing tools, disk/RAM constraints and dirty/unexpected
engine checkouts; never reset unrelated changes. Keep dependency installation
and large source sync explicit, resumable and separate from `npm test`. Record
actual resource constraints and continue independent implementation work when a
build is running. Do not invent a promised one-day schedule or a team-size gate.

`package.json` remains the authority for **Fly's product version**. Generate native
Fly branding/about/package metadata from it through `scripts/version-sync.js` or
its explicitly integrated successor. Keep Chromium's own engine version accurate
and separate: never replace its version with Fly's product version, including in
web-visible identity or client hints. Use the existing release planner and impact
labels, not a second independent numbering system.

Keep ordinary PR CI light as required by `AGENTS.md`. `npm test` remains a small,
network-free contract/unit/generated-file suite. Native builds and desktop suites
are explicit local or permitted release/dispatch checks. Adding required checks,
branch rules or workflows triggered by ordinary PRs needs owner approval; this
plan does not grant an exception. Adapt existing release packaging only when the
native packages are proven, preserving dry-run behaviour and tag validation.

## 5. Baseline inventory and destination

Treat source and observed behaviour as authoritative. Some guides and historical
audit pages describe older behaviour; reconcile discrepancies rather than porting
their claims. Inventory every action in `keybinds.js` and the command palette,
every preference and persisted field, and every current user-visible feature.
Assign each to an implementation milestone and a validation case.

| Area | Existing source anchors | Required destination/behaviour |
| --- | --- | --- |
| Tabs, pinning, ordering, reopen and numbers | `modules/webview.js`, `modules/tabActions.js`, `modules/tabRestoration.js`, `tabNumbers.js`, `tabContextMenu.js` | Native tab lifecycle plus stable Fly IDs; retain metadata and close/reopen behaviour |
| Workspaces and moves | `workspacePartitions.js`, `modules/tabTransfer.js`, `modules/state.js`, `modules/overlays/dashboard.js` | Stable workspace-to-profile map; rename retains identity; destination-context moves |
| Split layout | `modules/splitView.js` | Both native content panes shown, correct focus, orientation, selection and exit |
| Keyboard and hints | `keyboard.js`, `keybinds.js`, `stored-keybind-defaults.json`, `webview-preload.js`, `hinter.js`, `modules/keyboardSettings.js` | Preserve modes, actions, defaults, overrides, recorder, hints and editable/IME handling |
| Navigation/search | `modules/navigation.js`, `modules/overlays/searchEngine.js`, `modules/calculator.js` | Native navigation, aliases, suggestions, calculator and correct focus recovery |
| Sidebar and window UX | `renderer.js`, `index.html`, `style.css`, `modules/sidebarPointer.js`, `modules/windowControls.js`, `modules/focus.js` | Preserve expanded/collapsed UI, pointer entry, resizing, accessibility and overlays |
| New tab/bookmarks/history | `assets/new-tab.*`, `bookmarkStore.js`, `frequentSites.js`, `modules/overlays/bookmarks.js`, `modules/overlays/history.js` | Preserve formats, ordering, quickmarks, folder moves, frequent-site hiding and purge semantics |
| Settings/theme/zoom | `main.js` defaults, `modules/settings*.js`, `modules/theme.js`, `siteZoom.js`, `modules/clock.js` | Map every setting; distinguish UI scale from host-based site zoom and website theme |
| Media/lifecycle | `modules/webview.js`, `tabSuspension.js`, `modules/tabSuspension.js` | Native media behaviour; mute controls; eligibility protection; freeze without reload |
| Extensions and filtering | `extensions.js`, `extensionPackages.js`, `extensionStore.js`, `modules/extensions.js`, bundled extension assets | Native extension services, profile-local settings, real lifecycle events and native rule enforcement |
| Local scripts/styles | `userContent.js`, `modules/userScriptShim.js` | Preserve metadata, grants, matching, storage, request restrictions and live CSS updates |
| Reader/PDF/downloads | `modules/readerDistiller.js`, `modules/readability/`, `assets/pdfjs/`, download handlers in `main.js`, `modules/downloads.js` | Preserve reader workflow and PDF/download behaviour; use native implementations where equivalent |
| Privacy/auth/terminal | `security.js`, `closedOutput.js`, `main.js`, `SECURITY.md`, `PRIVACY.md` | Preserve product policy through native permissions; retire Electron-specific identity/network workarounds |
| Diagnostics | `diagnostics.js`, `modules/overlays/diagnostics.js` | Accurate Fly/engine/build identity and opt-in bounded diagnostics without browsing-data leakage |
| Installation/updates | `launch.sh`, `scripts/fly-install`, `desktopIntegration.js`, `appImageUpdate.js`, `scripts/aur.js`, release workflows | Native launch, URL/single-instance routing, default-browser integration, package/update channel parity |

Turn this inventory into a tracked parity checklist before replacing runtime
code. Mark each item implemented, validated, deliberately changed or blocked.
No feature is considered removed merely because its current implementation is
awkward. Any material product departure needs a recorded decision and honest
release/migration notes.

## 6. Data migration and rollback

### Storage contract

Keep legacy `~/.config/fly-browser/` data intact. Place the new native user-data
root in a distinct subdirectory, with a versioned import manifest and a stable
workspace-ID-to-profile-directory map. Do not point a new Chromium build directly
at Electron's live user-data directory. Names are presentation; profile IDs must
not be derived afresh from names.

Inspect and cover at least these sources: `config.json`, `session.json`, recovery
backups/sentinel, `bookmarks.json`, `history.json`, `keybinds.json` as resolved by
the keyboard loader, `site-zoom.json`, `hidden-sites.json`, local `scripts/`,
`styles/`, `script-storage/`, extension folders/source records and each partition's
site and extension data. Inspect actual loaders for additional files and platform
paths; do not assume this list is exhaustive or hard-code one machine's paths.

### Import transaction

1. Confirm the source application is closed and its data is in a consistent
   snapshot. Never read changing SQLite/LevelDB state as a completed import.
2. Back up or snapshot source inputs without logging credentials or cookie values.
3. Validate schemas, path boundaries and ownership. Build the destination in a
   staging directory with restrictive permissions and atomic writes.
4. Import app settings, stable workspace mappings, tab URLs/order/pins/titles,
   keyboard overrides, bookmarks, history, zoom and user content. Preserve unknown
   legacy fields in the import record rather than deleting them.
5. Migrate site/extension state only through a tested format-aware path. Verify
   cookies, storage, service workers and extension IDs/settings separately.
6. Verify counts, associations and functional behaviour; commit the migration
   marker atomically. Re-running an import must not duplicate or overwrite newer
   native user data.
7. Test interruption before and after commit, retry, rollback to the Electron app,
   and a subsequent native relaunch. Never write the native schema back into the
   legacy files as a rollback shortcut.

Cookies and credentials are a risk to test, not a promised directory copy.
Electron and native Chromium may differ in database schema, profile metadata,
encryption keys/keyring identity and supported formats. Direct directory copying
is allowed only for individually verified compatible data on a closed copy.
Otherwise implement a bounded importer or classify the item as unimportable.
Retain the original data and give a precise migration report if re-login is
required. Never print cookie/token/password material in evidence.

Preserve password-vault exclusion. Importing session cookies is distinct from
enabling a password manager. Verify private browsing never enters the importer.

### Extensions and default blocker

Current Fly bundles an extension using the legacy manifest format. Current
Chromium has removed support for that format; inspect the selected revision and
actual extension manifests before deciding what is compatible. Do not pin an
insecure engine, promise a removed feature flag, or quietly reintroduce a legacy
extension runtime to hide this difference.

The default plan is a maintained Manifest V3 blocker using Chromium's native
declarative request rules (evaluate uBlock Origin Lite as the default candidate).
Test real blocked requests, startup/restore ordering and site exceptions. Do not
claim equivalence to full uBlock Origin: map only verified compatible settings,
retain legacy lists/rules/settings for recovery, and report unsupported features.
Preserve explicit global/per-workspace disable, remove and empty-selection choices.
Never silently enable a replacement for a user who disabled the old blocker.

For every installed extension, preserve source and identity where supported,
validate packages, and produce a compatibility result. Do not install extensions
into a different workspace to make a test pass. Native messaging, side panels,
store installation/update and extension permissions must be explicitly exercised
or reported as limited; upstream support is not proof of Fly integration.

If the resulting upgrade needs user action, apply the repository's major-impact
policy and explain it. Completion cannot claim a clean automatic upgrade while
silently requiring users to reconstruct their extension configuration.

## 7. Execution milestones

These milestones are a continuous implementation sequence, not separate proposals
that require repeated approval. Commit coherent changes on implementation
branches. When intermediate work is merged, keep the existing default app usable
until the native cutover passes the completion criteria.

### M0 — Reproducible upstream and contract capture

- Read `AGENTS.md`, this plan and the handoff; record current Git state and source
  revision, then inspect intervening changes from the planning baseline.
- Resolve and pin supported upstream source/tooling; implement setup/sync/apply/
  build/run commands with a documented local checkout and dependency strategy.
- Build and launch unmodified Chromium using a disposable user-data root.
- Capture existing Fly workflows, representative screenshots, schemas and tests;
  turn section 5 into a complete parity ledger with action/preference coverage.
- Store the actual source SHAs, build arguments, commands and artefact locations.

Exit: another agent can reproduce the upstream control and locate the contracts.

### M1 — Native Fly window and workspace foundation

- Integrate Fly source/resources into the native build, brand the development
  binary, and show a native Fly shell with one functioning page.
- Implement stable tab/workspace IDs and real per-workspace profiles, native
  browser/window ownership and accurate active-tab/extension routing.
- Demonstrate two isolated workspaces in the intended single-window UX, native
  split view, focused-pane routing, profile-aware popups and clean teardown.
- Add a local extension probe for tab/window IDs and events across switches.
- Verify document/native visibility and focus with no page-API shims.

Exit: prove the highest-risk topology before building the rest of the product.
If upstream facilities differ from this plan, implement and document the necessary
native adaptation; do not substitute a less capable embedding framework.

### M2 — Complete browsing interaction

- Port all commands, modes, link hints, editable handling and binding editor.
- Implement sidebar, workspace dashboard, tab ordering/pinning/numbers/context
  menus, address/search flows, overlays, accessibility and mouse behaviour.
- Port preferences, new tab, bookmarks/quickmarks, history, calculator, clock,
  theming and host-based zoom. Preserve settings semantics and atomic writes.
- Validate hot changes and persistence; synchronize action defaults/reference
  generation with the new native command implementation.

Exit: every action and preference has an explicit implementation/parity status.

### M3 — Native services, extensions and lifecycle

- Integrate native extensions, installation/update controls, permissions and
  request-rule enforcement; validate profile-local extension state.
- Deliver the chosen default blocker and legacy-extension compatibility reports.
- Port local scripts/styles and their grant/storage/network boundaries.
- Deliver media controls, suspension eligibility, reader mode, PDF, downloads,
  OAuth/popups, private behaviour, permissions and clipboard-only terminal flows.
- Remove compatibility code that assumes Electron guest behaviour. Keep native
  extension installation events, tab IDs and web APIs accurate.

Exit: normal browsing and native service behaviour pass the acceptance matrix.

### M4 — Transactional migration and recovery

- Implement the importer and rollback contract from section 6 on synthetic and
  copied legacy profiles, including collisions, corruption and partial imports.
- Exercise session restoration, native crashes, renderer crashes and interrupted
  writes. A crash in one page must not destroy other workspace data.
- Finish migration UI/reporting, extension differences and precise upgrade notes.

Exit: no silent data loss, no cross-workspace leakage, deterministic retry/rollback.

### M5 — Delivery and engine update rehearsal

- Replace launcher/packaging/update integration after native smoke tests pass.
- Produce installable Linux, macOS and Windows development packages with checksums,
  resource/license manifests and provenance. Test the packages, not only build-tree
  binaries. Use external runners where already available; report unavailable
  platform execution honestly rather than marking it passed.
- Preserve URL handlers, single-instance routing, desktop icons/default-browser
  behaviour, installer/AUR channels, update verification and failure recovery.
- Exercise a product update and an engine rebase/update on a disposable branch.
  Reapply the patch stack and rerun the compatibility subset after the engine bump.
- Test that a clean checkout using documented commands builds the same product
  inputs without untracked source edits or local machine paths.

Exit: packages and future engine updates have concrete repeatable evidence.

### M6 — Validate, cut over and finish

- Finish every acceptance row, eliminate unexplained regressions, update current
  architecture/user/developer documentation and capture actual native screenshots.
- Make the native app the default launcher/package path. Remove Electron runtime
  dependencies and dead adapters only after feature/migration evidence exists;
  retain historical fixtures only where clearly labeled and useful.
- Keep the old installer/update handoff usable for existing users. Rehearse how
  a current installed Fly receives and starts the native replacement.
- Run the required repository checks and native/package checks. Open appropriately
  labeled PRs, wait for every latest-head check, fix failures and squash-merge per
  `AGENTS.md`. Finish on updated local `main`.
- Deliver the build/run/install commands, artefact/checksum paths, migration and
  rollback instructions, tested platforms, benchmark results and material limits.

Exit: satisfy section 10; publishing a release is separate from completing a
reviewed, packaged implementation and is not required by this brief.

## 8. Acceptance and regression matrix

Port behavioural assertions rather than mechanically translating Electron DOM
selectors or source-text regex tests. Reuse pure logic fixtures when meaningful.
Use native unit/browser/interactive tests for native ownership, focus and security;
CDP/desktop automation complements them. A test helper must not force all pages
visible, disable security or otherwise remove the behaviour being tested.

For keyboard UX changes, run the disposable-profile Electron recorder/editable
checks on the retained baseline as `AGENTS.md` requires, then run equivalent native
Fly tests. Do not remove a behavioural assertion merely because its old harness
depends on Electron. Run Linux desktop suites on X11 and Wayland where available;
record an unavailable backend as a coverage gap.

| ID | Scenario | Required observable result / evidence |
| --- | --- | --- |
| A01 | Clean launch, URL launch, second instance | One correctly branded usable Fly window; URL reaches the intended workspace; no orphan process |
| A02 | Tabs via keyboard, sidebar, palette and close/reopen | Correct destination, order, pins, titles and focus; no stale IDs or metadata drift |
| A03 | NORMAL/INSERT/PASSTHROUGH, recorder and editable pages | Default/overridden bindings, multi-strokes, cancellation, IME/AltGr, nested editables and extension keys behave correctly |
| A04 | Expanded/collapsed sidebar, overlays, small viewport, scale | Pointer entry/click-through, focus containment, accessibility, geometry and high-DPI behaviour match intended Fly UX |
| A05 | Same origin in two workspaces, rename/delete/recreate/restart | Distinct cookies/storage/service workers and extension state; rename retains identity; new workspace cannot inherit deleted data |
| A06 | Move tab across profiles | Destination navigation uses destination identity; source in-flight work cannot leak; no unsafe live profile reassignment |
| A07 | Both split orientations, switch pane, close/exit/restore | Both shown pages visible, one focused/active tab, correct selected-tab and restored-layout behaviour |
| A08 | Native visibility without extensions | Foreground/background/minimize/restore follow native state; focus changes do not fabricate visibility; no shimmed getters |
| A09 | Media paused or playing, with and without Pie | Pause survives tab/workspace switching and return by every input path; playing background audio continues; mute controls remain independent |
| A10 | Freeze/resume versus discard | Eligible page resumes same document without reload; media/download/socket/worker/DevTools protections and disabled timeout are covered |
| A11 | Extension lifecycle, storage, tabs/windows and permissions | Native installation event, stable IDs, accurate active tab, profile isolation, enable/disable/remove and explicit update behaviour |
| A12 | Blocking after fresh launch and restoration | Actual fixture requests blocked by native rules; exceptions honored; no first-load race or false positive based only on extension load |
| A13 | Script/style matching, grants, storage and hot reload | Correct scope/timing, retained local storage, restricted cross-origin requests, no privilege escalation from pages |
| A14 | Search, aliases, bookmarks, quickmarks and history | Results, ordering, moves, frequent-site hiding, purge and persistence retain their contracts |
| A15 | Settings, website theme, site zoom, UI scale and clock | Live changes persist; host zoom works across workspaces; UI/page scale stay separate; no unnecessary idle polling |
| A16 | PDF by URL and MIME, reader, downloads and OAuth popups | Content usable; safe save/open/reveal/cancel; popup lands in correct context; no inherited identity-rewrite requirement |
| A17 | Device permissions, private mode and internal WebUI | Correct denials and exceptions; private state not persisted; remote page cannot call privileged Fly APIs |
| A18 | Terminal, external URLs and hostile filenames/URLs | Clipboard-mediated execution; no auto-shell evaluation; explicit safe scheme/path handling |
| A19 | Migration, interrupted import, malformed inputs and rollback | Source untouched; repeatable counts/mappings; no duplicate import; real storage/auth outcomes reported per platform |
| A20 | Native page/browser crash and session recovery | Other contexts survive where applicable; restore metadata remains consistent; no truncated/reset preferences |
| A21 | Packaged install, launch, default browser and update | Resources complete, verified update/install channels, failure recovery and correct product/engine identity |
| A22 | Clean rebuild and upstream update rehearsal | Exact source/patch/tool provenance, no hidden edits, repeatable application of integration changes |

Keep automated deterministic fixtures for A08/A09 using local media and an
extension that remembers playback on visibility changes. Also reproduce the
reported Twitch homepage/live-player flow with the Chrome build of Pie in a
disposable profile, recording the exact package hash and actual outcome. Check
with/without extensions and against unmodified Chromium from the same source pin.
Do not publish the owner's cookies, channel history or personal extension data.

For live services that are offline or inaccessible, keep the deterministic case
and report the live-service gap. Do not turn an unexecuted live test into a pass.

## 9. Performance and maintenance evidence

Compare the native Fly build with both the current Electron baseline and the
unmodified Chromium control. Compare equivalent optimised builds, GPU settings,
security, display backend, page loads, extension selections and suspension policy.
Report engine differences when exact matching is impossible.

Measure total process-tree PSS/private/shared memory appropriately, steady idle
CPU and wakeups, cold/warm startup, tab/workspace switch latency and video behaviour.
Include empty/new-tab, a representative multi-site set, paused/playing media,
extensions on/off, multiple profiles and a many-tab session with explicit
loaded/frozen/discarded counts. Use repeated runs and retain raw samples.

No universal memory reduction is promised by this plan. Explain material
regressions and fix unintended work; do not improve scores by silently discarding
documents, turning off blocking/security or changing the workload. Set any future
numeric budget from recorded baseline data, not invented expectations.

Keep a small documented patch stack with rationale and corresponding tests.
Practice updating its source pin and dependency/tool state. Build compatibility,
runtime behaviour, extension behaviour and package/update continuity are separate
checks. Retain upstream notices and third-party resource/license manifests.

## 10. Definition of done

All of the following must be true before reporting the rework complete:

- A complete native Chromium-based Fly is the default application, with no
  Electron runtime or Electron extension shim on its production browsing path.
- Every baseline command, preference, data type and feature has a resolved parity
  status, with product departures and required user actions explicitly disclosed.
- Single-window isolated workspaces, native split visibility, keyboard/editable
  behaviour and native extension integration are demonstrated in the real app.
- Legacy migration and rollback preserve source data, and supported storage/auth
  migration outcomes are verified rather than assumed.
- Acceptance rows have concrete evidence. Missing platform/device/service tests
  are clearly outstanding; they do not count as a completed cross-platform port.
- Installable packages, update/installer handoff and an engine-update rehearsal
  have passed, with provenance and actual artefact/checksum locations recorded.
- `npm test`, `git diff --check` and `npm run docs:build` pass, together with the
  native/browser/UI/package checks required by the implementation changes.
- Latest remote-head GitHub checks pass; PRs are squash-merged, branches deleted,
  and local `main` is pulled as required by `AGENTS.md`.
- The final report distinguishes verified results, intentional limitations and
  outstanding blockers. A release is not fabricated or published implicitly.

## 11. Sources and confirmed facts

Consult current documentation at the selected revision; links to `main` are
research entry points, not immutable build pins.

- [Chromium Linux checkout/build instructions](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md): official source, dependency and build workflow.
- [Chromium browser design principles](https://chromium.googlesource.com/chromium/src/+/main/docs/chrome_browser_design_principles.md): native browser/window/tab ownership concepts.
- [Views UI framework](https://www.chromium.org/chromium-os/developer-library/guides/views/intro/): native browser UI foundation.
- [WebUI architecture](https://chromium.googlesource.com/chromium/src/+/main/docs/webui/webui_explainer.md) and [untrusted WebUI](https://chromium.googlesource.com/chromium/src/+/main/docs/webui/chrome_untrusted.md): internal page and capability boundaries.
- [Profile architecture](https://www.chromium.org/developers/design-documents/profile-architecture/): profile-scoped services; exact APIs need source inspection.
- [Native tab model](https://chromium.googlesource.com/chromium/src/+/main/chrome/browser/ui/tabs/tab_strip_model.cc): inspect current native split/tab facilities before adding parallel models.
- [Manifest V2 removal](https://developer.chrome.com/docs/extensions/develop/migrate/mv2-deprecation-timeline): legacy extension compatibility is a product migration issue.
- [uBlock Origin Lite](https://github.com/uBlockOrigin/uBOL-home): candidate maintained native-rule blocker, not a full-uBlock-equivalence claim.
- [Chromium security updates](https://chromium.googlesource.com/chromium/src/+/main/docs/security/updates.md): keeping the distributed engine current is ongoing work.
- [Chromium API keys](https://www.chromium.org/developers/how-tos/api-keys/): do not assume Google Chrome services or private credentials are available.

The Twitch investigation in the planning conversation found a native-versus-
exposed visibility discrepancy in Electron's webview layer and captured a Pie
visibility-handler call to `video.play()`. A matching Chromium/Pie run retained
the user's pause. This is motivation and a reproducible acceptance scenario,
not evidence that a future Fly build already passes it. The temporary probes from
that conversation are not maintained tests; create repository-owned fixtures.
