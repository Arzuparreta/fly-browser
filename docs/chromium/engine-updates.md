# Chromium engine updates

Fly is Chromium with Fly's overlay and integration patches applied. Every
security fix Google ships lands in a Chromium Stable release, and Fly only has
it once its pin, `chromium/upstream.lock.json`, moves to that release. Chromium
publishes a major version every four weeks and security updates within it about
once or twice a week. Fly follows them automatically:

1. Every six hours, `.github/workflows/engine-update.yml` asks
   [chromiumdash](https://chromiumdash.appspot.com/releases?platform=Linux) for
   the latest Stable release for Linux.
2. When it is newer than the pin, it commits the new pin to the
   `engine/chromium-stable` branch and opens a pull request labelled
   `impact:patch`. If that pull request is already open (for example, with patch
   fixes on it), the newer pin is committed on top.
3. It validates the branch in the **candidate engine root**: it checks that every
   patch applies, moves the root to the new pin, builds the unmodified Chromium
   control and Fly, and runs Fly's unit and browser tests. The result is the
   `engine-validation` status on the commit.
4. It merges the pull request once every check passes, then cuts a release with
   `scripts/release.js prepare`.
5. `release.yml` makes the validated candidate the active engine root by
   swapping the two roots' symlinks ([Engine roots](#engine-roots)), builds the
   packages and publishes them. A separate job then brings `out/FlyNative` to
   the release commit, so `./launch.sh` runs the new engine. The candidate
   already built and tested it, so only the release's own changes compile.

A patch-context conflict first receives a conservative automatic repair
([Automatic patch repair](#automatic-patch-repair)). Code/API conflicts and
native validation failures can then receive a bounded agent proposal and an
independent review. A repair that cannot satisfy all gates stops with the
pull request or warning issue open and its reason recorded. Nothing reaches
users until the whole chain has passed.

## Pon Chromium al día: complete maintenance task

The owner can invoke this task simply by saying **“Pon Chromium al día”**.
“Actualiza Chromium” and “encárgate del seguimiento de Chromium” mean the same
complete task. A fresh agent must execute the following procedure, not return
a suggested prompt, stop after a status report, or ask the owner to identify
which patches need work. A question asking only for status remains read-only.
Explicit limits in the owner's request take precedence.

1. Read this document and `../releasing.md` from latest `origin/main`, and obey
   `AGENTS.md`, including task worktrees and engine-root coordination. Perform
   the complete status checklist below: Stable, Beta, workflow enabled state,
   runner, update PRs, issues, checks and any unfinished release. Use live
   evidence; issue numbers and versions from old conversations are not a work
   list. Resume deliberately paused tracking when the owner requests it; do
   not silently override a pause that is still in force.
2. Take responsibility for all discovered Chromium maintenance work. Repair
   Stable patch conflicts, build/test failures and update-branch conflicts;
   also prepare the failing Beta patches using the Beta repair procedure below.
   Preserve Fly behavior and current Stable compatibility. Never publish Beta
   as the shipping engine to clear a warning. If a Beta change cannot coexist
   with Stable, keep the preparation isolated and record the concrete blocker
   and remaining work rather than claiming completion.
3. Run the required local checks and native validation. Commit, push, open
   appropriately labelled PRs and supervise every check on their latest heads.
   Fix failures, integrate current main without losing changes, and merge only
   after the repository gates pass. Coordinate with the automatic update
   workflow rather than racing it or using an engine root it owns. Re-run the
   workflow after repairs and verify the actual patch-check results and issue
   state, even when Actions is green.
4. Complete publication when the Stable engine changed or the task merged
   corrections that need shipping. Follow `../releasing.md`: inspect
   `node scripts/release.js plan`, use `prepare` when a release is needed, and
   supervise the bump PR, tagging, release and package jobs through completion.
   If the automatic workflow already started that release, finish supervising
   it instead of creating another one. Release numbers come from the tooling.
   Preparation that only supports a future Beta does not itself require an
   empty release. The invocation authorizes pushes, gated PR merges and release
   publication; do not stop to request that authorization again.
5. Verify the published release and its downloadable assets/checksums when a
   release was produced. Ensure the active native build and ordinary
   `./launch.sh` use the updated Stable engine and task changes, preserving the
   owner's session; report any required restart. Do not equate merged source,
   a published package, a local build and the running browser's state.
6. Clean up only this task's branches/worktrees. Report the resulting Stable
   and Beta state, merged PRs, release and validation evidence, and any genuine
   blocker. If no maintenance or release was needed, say so with the checked
   evidence. Opening a PR, starting a build or enabling tracking is not task
   completion.

## Checking whether there is work to do

There are two independent tracks: **Stable freshness** (the engine Fly ships)
and **Beta patch compatibility** (preparation for the next major release).
Being on the latest Stable does not mean there are no pending patch repairs.
When asked about security updates, pending patches, or resuming tracking,
check all of the following against current GitHub state:

```sh
python3 scripts/chromium/engine_update.py status
gh api repos/Arzuparreta/fly-browser/actions/workflows/engine-update.yml --jq .state
gh run list --workflow engine-update.yml --limit 5
gh pr list --head engine/chromium-stable
gh issue list --state open --search 'in:title "Fly patches do not apply to Chromium Beta"'
```

Read the latest run's jobs/logs, any update PR's checks and failure comments,
and the Beta issue's body. The workflow can finish successfully **while Beta
patches fail**: that failure is deliberately recorded in the issue rather than
failing the whole workflow. No Stable update skips the candidate/release steps
in the update job. Inspect the separate repair job before claiming a native
build ran: a context-only or blocked repair does not establish that.
Report these results separately, with links and any unverified build or
installed-browser state. Read current merged main rather than treating an
open PR or an old conversation as delivered work.

The owner can see pending Beta work in the repository's **Issues**, Stable
update failures in **Actions** and the update **Pull request**, and published
updates in **Releases**. To receive GitHub notifications, use **Watch > Custom**
for issues, pull requests and releases, or subscribe to the Beta issue itself.
Notification delivery depends on the owner's GitHub settings; the workflow
opening an issue does not guarantee an email or a chat message.

The automatic workflow detects conflicts, repairs unambiguous context drift,
and attempts bounded code/API repairs with coding agents. Failed proposals, exhausted
quota or validation failures remain visible; they are never silently accepted.
An agent answering a status question must include pending Beta work even when
Stable is current.

To resume tracking after a deliberate pause, check the workflow state and
runner availability, then enable it and dispatch an immediate check:

```sh
gh workflow enable engine-update.yml
gh workflow run engine-update.yml --ref main
```

Verify the resulting run; enabling the schedule alone is not proof that an
update, build or patch repair has completed.

## The patch series

The fewer upstream lines Fly changes, the fewer conflicts a new Chromium
brings. Fly's own code lives under `//fly` in `chromium/overlay`, which upstream
never touches. The patches in `chromium/patches` only connect it to Chromium.

New Fly sources and dependencies are listed in `chromium/overlay/fly/integration.gni`,
not added to upstream `BUILD.gn` files. Fly's browser UI is its own target,
`//fly:browser_ui`, with the sources in `fly_browser_ui_sources`; like the
targets Chromium split out of `//chrome/browser:browser`, it takes
`//chrome/browser:browser_public_dependencies`, which includes every header the
browser generates. Each upstream target that uses Fly code
(`//chrome/browser:browser`, `//chrome/test:browser_tests`,
`//chrome/browser/prefs:impl`) adds those lists with a single hook from
`0000-native-build-integration.patch`. When Chromium changes one of those
files, at most that one hunk needs updating. Previously about twenty patches
each added their files to the same list in `chrome/browser/ui/BUILD.gn`, so one
upstream change there broke all of them at once.

When you add a file under `chromium/overlay/fly/ui/`, add it to
`fly_browser_ui_sources` in `integration.gni`. Do not write a patch for it.

## Engine roots

The engine host has two Chromium checkouts, reached by two fixed paths:

| Role | Path | Environment variable | Used by |
| --- | --- | --- | --- |
| Active | `/mnt/storage/fly-chromium` | `FLY_ENGINE_ROOT` | Development builds, `./launch.sh`, releases |
| Candidate | `/mnt/storage/fly-chromium-next` | `FLY_ENGINE_CANDIDATE_ROOT` | Validating engine updates |
| Repair | `/mnt/storage/fly-chromium-repair` | `FLY_ENGINE_REPAIR_ROOT` | Isolated Beta compilation/testing; never swapped into production |

An update is built and tested in the candidate first. If a patch or the build
fails there, the active root keeps working on the current pin, and the agents
and the owner using it are not blocked.

### Slots

The two paths are symlinks to two slots, `/mnt/storage/fly-chromium-a` and
`/mnt/storage/fly-chromium-b`. Which slot is active changes; the paths do not,
so commands, `./launch.sh` and the runner service always use the paths above.

A release whose pin the candidate validated makes it the active root by
swapping the two symlinks (`engine_update.py follow`). Nothing is copied: the
candidate's control and `out/FlyNative`, which validate built and tested, become
the active builds as they are, and the previous active slot becomes the next
candidate, still at the previous pin. The next update moves it from there.

Every tool resolves a root to its slot before building, so the build state Siso
keeps in each output directory, which records absolute paths, stays valid in
its slot across swaps. Each root's `test-tools` is copied to the new active
slot if it lacks one. The swap holds both roots' leases; if it stops between its
two symlink changes, the next `follow` completes it from
`evidence/engine-swap-pending.json`.

A running Fly keeps using the slot it was started from. Restart it after a
release to run the new engine.

`engine_update.py slots --root /mnt/storage/fly-chromium --candidate
/mnt/storage/fly-chromium-next` sets slots up once on a host: it renames the two
roots to `-a` and `-b`, leaves symlinks at their paths, and rewrites the
absolute paths their evidence records and Siso states hold (the previous state
stays beside it as `.siso_fs_state.before-slots`), so nothing is rebuilt.
Check afterwards with `autoninja -C out/FlyNative -n chrome` in each slot.

### Plain roots

On a host whose roots are plain directories, a release adopts the validated
sources instead (`engine_update.py adopt`): `rsync` copies the files whose
contents differ (never build outputs) and dates each written file now, so the
build compiles exactly what changed, and nothing is downloaded. The active
root then still compiles that change itself, which the candidate already did.

Both variables are set by the runner service,
`~/.config/systemd/user/fly-chromium-runner.service` (see
[releasing](../releasing.md#build-host)).

## Commands

`scripts/chromium/engine_update.py`:

| Command | What it does |
| --- | --- |
| `status` | Prints the pinned and the latest Stable versions |
| `pin` | Writes the latest Stable pin into `chromium/upstream.lock.json` when it is newer |
| `patch-check [--channel Stable\|Beta]` | Replays the patch series on the upstream files of the pin, or of the latest release of a channel, fetched from Gitiles. Needs no checkout; downloads a few MB |
| `patch-repair [--channel Beta] [--reference REV]` | Reanchors the complete patch series on the target using a known-good Chromium revision (the pin by default); preserves every added/removed line and proves the reference output unchanged, or writes nothing |
| `patch-verify --base REF` | Proves that a Beta preparation only changes existing patch context and leaves the complete patched Stable files byte-identical to REF |
| `seed --from A --root B` | Creates root B by copying root A locally (sources and dependency state, no build outputs) |
| `move --root R` | Moves root R from the pin it was synced to onto this checkout's pin: reverts the overlay, checks out the new revisions, `gclient sync`, hooks. Resumable after an interruption |
| `upgrade --root R [--through apply\|build]` | `move`, then rebuilds the control, applies the overlay and, with `build`, builds `out/FlyNative`. Does nothing when R is already current |
| `validate --root R --test-tools DIR` | `upgrade`, then Fly's unit and browser tests on an owned Xvfb display |
| `adopt --candidate C --root R` | Gives R the sources of candidate C, when C passed `validate` for this pin |
| `slots --root R --candidate C` | Turns two plain roots into slots behind their paths (once per host) |
| `follow --root R [--candidate C]` | What a release runs: with slots, swaps them when C passed `validate`; with plain roots, `adopt`; otherwise `upgrade --through apply` |

A long command holds the root's engine lease for its whole sequence. The
`engine.py` and `native.py` commands it runs inherit the lease; any other
command on that root is refused until it finishes.

When main has moved the pin and your engine root has not followed it yet,
`native.py` says so. Merge `origin/main` into your branch and run:

```sh
python3 scripts/chromium/engine_update.py upgrade --root /mnt/storage/fly-chromium
```

## When an update fails

The pull request comment names the stage that failed.

- **A patch does not apply.** Chromium changed the lines a patch relies on.
  Check out `engine/chromium-stable`. Reproduce the failure with
  `python3 scripts/chromium/engine_update.py patch-check`, then rebase the
  patch: apply it to the new upstream file, resolve the rejected hunk while
  keeping its intent, and regenerate it with the same `--- a/` and `+++ b/`
  headers and 3 lines of context. Commit, push, and run
  `gh workflow run engine-update.yml`.
- **The build or a test fails.** Fly's code uses a Chromium API that changed.
  Fix it on the branch the same way. To iterate faster, run
  `engine_update.py validate` against the candidate root locally: the build
  is incremental.
- **main does not merge into the branch.** Resolve the conflict on the branch.

The workflow revalidates a branch when main changes under it, and it never
merges a head that has not passed.

## Early warning: Chromium Beta

The Beta channel carries the next major version. The `beta` job replays Fly's
patches on it every six hours. While any patch does not apply, it keeps the
**Fly patches do not apply to Chromium Beta** issue open with the list. Rebase
those patches before that version reaches Stable, and the Stable update goes
through without stopping. Compiling against Beta is not checked: only patch
application in that baseline job. A semantic repair additionally builds and
tests the frozen Beta in the separate repair root before its PR can merge.

### Automatic patch repair

After the warning and Stable jobs finish, a separate job on the trusted engine
host attempts a repair in a dedicated worktree from current main. Pending
releases get the host first: Beta preparation defers while any release is
running or queued. The same scheduled workflow, host login, PR checks and
guarded merge process handle both channels. A failed Beta repair does not
block Stable publication.

The mechanical repair only replaces unchanged context around Fly's edits. It
preserves all added and removed lines, accepts only exact anchors that occur
once in each version, and strictly replays the entire series on both versions.
For every repaired patch, the output on the known-good reference must remain
byte-for-byte identical. Downloads, ambiguous matches, changed/deleted code and
unsupported patch formats fail closed. A late failure writes no patches,
including earlier repairs in that attempt. No code-generating model or API key
is used by this layer.

A successful context-only Beta repair opens `engine/chromium-beta` with `impact:none`. Before
merging, it proves the complete Stable output is identical to current main,
replays Stable and Beta, and waits for the latest head's `verify`, `impact_label`
and `engine-validation` checks plus any other registered checks. If main moves,
the combined result is checked again. This changes patch metadata, so it needs
neither a native rebuild nor an empty release. It does not establish Beta build
or runtime compatibility: the normal Stable update still compiles the control
and Fly and runs Fly's native tests before shipping that engine.

The warning closes only after strict Beta replay succeeds on merged main.
Failures leave it open; a distinct blocker is commented with its workflow log.
If the mechanical layer cannot repair a function move, API change or ambiguous
match, the agent layer below attempts the code adaptation. It never drops a
patch to obtain a green result.

Stable patch replay uses the same repair engine when needed, with main's
previous pin as the known-good reference. It commits and pushes a successful
repair on the existing Stable update PR, then builds/tests that new head in
the candidate root. Any failed repair leaves that PR open. Publication retains
all its existing native validation and release gates.

Source preparation snapshots the previous dependency inventory before moving.
When Chromium replaces or retires a managed dependency Git repository,
source preparation first verifies its recorded origin, revision and clean tree.
It moves that checkout to `evidence/retired-dependencies/` with a recovery journal
before checking out the new upstream tree. Checkout failure restores the
verified dependency; edited/unrecognised paths stop the operation. Neither
`git reset --hard` nor `git clean` is used to clear a collision.
The same checks cover nested managed repositories and Git links no longer
managed by DEPS; their complete old checkouts remain recoverable. The known
removed generated `reproxy.cfg` is likewise preserved outside the source tree.
Arbitrary untracked files still stop validation. Git hooks/LFS filters are
disabled only in the isolated runtime, leaving the host's configuration intact.

### Shared agent policy for code/API repairs

The update, validation and release pipeline consists of repository scripts and
GitHub Actions; it does not depend on the agent supervising it. Any configured
T3 Code agent can run the documented maintenance task. Stable and Beta use the
same repair controller, `scripts/chromium/agent_repair.py`, and the same policy
for both author proposals and independent reviews:

1. **Claude Opus 5.5, high**, using the existing Claude Code session configured
   on the host/T3.
2. If Claude reports a quota or availability failure, **Codex Sol 6.1, high**,
   using T3's existing Codex provider connection and the existing CLIProxyAPI
   route for the owner's two Codex accounts. The proxy owns account routing;
   Fly never copies tokens or selects a single account behind it.

The controller preserves connection/authentication configuration while excluding
user/project tools, hooks, plugins and commands from repair executions. Ignoring
user tools must not silently discard the proxy endpoint and fall back to a
personal default Codex login. Claude currently uses its existing Claude Code
login; it does not require moving that login into the Codex proxy.
No global T3, proxy, model-default or authentication configuration is changed.

Only explicit client quota/availability failures trigger the alternative
provider. A blocked proposal, rejected review, malformed result, isolation
failure, compilation error or failing test is never an excuse to seek a more
favourable verdict from another model. A partially written proposal is restored
to its exact starting state before another provider is tried. Evidence records
which provider, exact model and effort actually completed each role. If both
providers are unavailable, the validation defers and the issue/PR stays open.

Each proposal has a disposable Git worktree and independent object store.
Agent tools see public upstream sources read-only, scoped Fly source writes and
a scratch directory, with no host HOME/authentication, network, display, build
outputs or real profiles. Claude receives only the controller's isolated MCP
shell tool, with built-in tools disabled. Codex also enforces a custom filesystem
profile and a credential-free tool environment. Claude checks its installed
client's tool inventory with a harmless preflight before receiving repair
instructions. Every role starts with a fresh scratch HOME, and Python user-site
loading is disabled. Live denial probes must pass before either provider runs;
there is no unsandboxed fallback. Clients retain
their normal host authentication outside those tool namespaces.

The controller enforces the diff independently of the model's Git index. It
allows existing integration patches and C++/headers, new compatibility tests,
and quoted list entries/comments in `integration.gni`. It refuses deletions,
new patch files, scripts, workflows, resources, command catalogs, version data,
the shipping pin, symlinks and excessive file/diff sizes. Baseline test sources
remain read-only throughout the overlay. Only `compat_*_test.cc` files proven
introduced after the trusted base may be refined by later repair attempts.
New upstream build hooks and executable GN statements remain forbidden.

Pure compatibility tests use `fly_compatibility_test_sources` and
`fly_compatibility_test_deps` and run in `fly_unittests`; guard platform-specific
headers and test bodies in C++. Browser regressions use
`fly_browser_tests_sources` and match `Fly*BrowserTest.*`, so the native browser
target really executes them. A fresh read-only execution independently reviews
the complete committed/uncommitted repair and upstream callers. It must approve
with no findings. Model assertions cannot replace replay, tests or that review.

Every semantic Beta head needs strict full-series replay on current Stable and
the frozen Beta, local npm checks, independent approval, and control/Fly builds
plus native unit/browser tests for both pins. Builds use separate filesystem,
PID and network namespaces with an empty HOME. Trusted dependency preparation
happens first; untrusted native tests have no external network or credentials.
Stable uses the candidate slot; Beta uses the dedicated repair root, which is
never a production/adoption candidate. A root already leased by another build
or still running a browser defers without spending a code proposal or
interrupting that session. A semantic repair updates the active Stable native
build only after merge and merged-main replay; restart Fly to load rebuilt code.

All latest-head remote checks must pass. Main moving invalidates the old
combined validation. Beta advancing or an active Stable slot lagging behind
main also prevents merge. A native failure can receive one further bounded
proposal; a rejected or incomplete result stays open. No Beta engine is shipped
and future-Beta preparation alone does not cut a release.

Cost, isolation and recovery controls:

- At most two logical proposals per Chromium input/target in 24 hours. Each
  author/review phase has one shared time budget across its provider fallback;
  native phases and the whole repair job also have bounded durations.
- State lives in `~/.cache/fly-engine-repair` (override
  `FLY_ENGINE_REPAIR_STATE`). `retry_repair=true` permits a fresh bounded pair
  for that workflow run; it does not weaken scope/review/test requirements.
- Set `FLY_ENGINE_AGENT_REPAIR=off` to stop model usage. The old
  `FLY_ENGINE_CODEX_REPAIR` setting/entry point remains a compatibility alias.
  Detection and exact context-only repair continue while models are disabled.
- The host needs its configured Claude/Codex clients, existing provider logins,
  the Codex proxy, Linux namespaces and bubblewrap. Optional T3 provider binary
  and home paths are read from the existing T3 settings. No credentials belong
  in the repository or repair prompts.
- Engine roots remain selectable through `FLY_ENGINE_ROOT`,
  `FLY_ENGINE_CANDIDATE_ROOT` and `FLY_ENGINE_REPAIR_ROOT`; they must be separate,
  non-nested and external to the Fly repository. An unowned existing repair
  directory is never adopted/reset implicitly.
- Owned Xvfb uses software rendering with GLX disabled and must answer X11 setup
  with live display processes before tests start. Native diagnostics retain the
  first assertion/startup failure alongside the final summary. This does not
  establish physical GPU acceptance.
- Official metadata throttling/outages have bounded retries and then defer.
  Final Beta freshness uses only the official release revision.
- Stable/Beta jobs preserve full diagnostics and role/provider metadata as
  Actions artifacts for seven days, including failed attempts. Python output
  is unbuffered so the actual error remains at the end of failure comments.

A model-independent pipeline cannot guarantee that every future Chromium
redesign has a compatible automatic repair. A concrete blocker remains visible
when the bounded proposal/review/native gates cannot be satisfied. Ordinary PR
CI and branch rules remain unchanged.

### Starting a Beta patch repair

Use the open Beta issue as the current work list, not a hard-coded version or
issue number in this document. In a dedicated task worktree from latest
`origin/main`, reproduce it with:

```sh
python3 scripts/chromium/engine_update.py patch-check --channel Beta
```

Adapt each failing patch to the upstream changes while preserving its Fly
behavior. Keep the patch series applicable to the **current Stable pin** as
well: run `patch-check` without `--channel` and with `--channel Beta`. Do not
move the shipping pin to Beta or modify the owner's active engine root just
to investigate an early warning. If the two versions require incompatible
changes, record that limitation in the issue and keep the preparation isolated
rather than breaking Stable to make the warning disappear.

Follow the repository's normal commit, PR, local validation and remote-check
requirements. Patch changes that affect runtime still require the native
build and acceptance checks described in `AGENTS.md`; successful patch replay
alone does not prove compilation or runtime compatibility. After merge, rerun
`engine-update.yml` so its Beta job refreshes or closes the issue based on the
latest upstream release. An automatic detection system being ready does not
mean every future compatibility repair is already done.

## Cost of an update

What the engine host compiles for one Chromium update, from the
`evidence/engine-update-logs` of each root and the Actions job times. Before
slots (the update released as v0.42.4, 9–10 November 2026, a security update
within 157):

| Step | Root | Siso steps | Time |
| --- | --- | --- | --- |
| Validate: control | candidate | 406 | 13 min |
| Validate: `out/FlyNative` | candidate | 487 | 12 min |
| Release: `out/FlyRelease` | active | n/a | 3 h 01 min |
| Development build: control | active | 56 138 | 2 h 35 min |
| Development build: `out/FlyNative` | active | 56 218 | 2 h 36 min |

The update released as v0.42.1 (4 November) had the same shape: 56 611 and
56 691 steps in the active root.

The development build compiled again what validate had just compiled, and
about 140 times as much: adopting wrote 152 061 files
(`evidence/engine-adopt.json`) because rsync compared times rather than
contents, and every written file was dated now. Each adoption left the active
root's times different from the candidate's, so the next one rewrote them all
again. Slots remove the copy and that development build; plain roots now copy
only files whose contents differ.

The release build itself stays: `out/FlyRelease` uses other build arguments
(PGO, ThinLTO, the Google API key) and cannot be taken from validate. The
control also stays in validate: `native.py` requires a control built for the
pin before it applies the overlay or builds Fly.

Setting up slots on the engine host (12 November 2026) took 17 seconds and
rewrote 22 evidence records and the Siso state of five output directories.
`autoninja -n chrome` reported 0 steps for the control and `out/FlyNative` of
both roots before and after.

The first release with slots, v0.43.1 (12 November 2026, Chromium
157.0.8103.78, run 43209127877), measured against v0.42.4:

| Release run | v0.42.4 (adopt) | v0.43.1 (slots) |
| --- | --- | --- |
| Build job | 3 h 29 min | 2 h 54 min |
| of which `follow` (move the active root to the validated pin) | 26 min 52 s | 5 s |
| of which compiling and packaging `out/FlyRelease` | 3 h 01 min | 2 h 54 min |
| Development build job | 5 h 21 min | 6 min 35 s |

The development build job of v0.43.1 compiled 7 steps in 2 min 15 s for
`out/FlyNative`; the rest of the job is checking the dependency records,
`gn gen` and applying the overlay. The release build started from an empty
`out/FlyRelease`, since the slot that became active had never built one; a
release that moves Chromium recompiles nearly everything in either case
(57 875 steps, against 56 882 for v0.42.4 in an existing `out/FlyRelease`). The release and its checksums were public
2 h 55 min after the build job started, and the official packages 5 min later.

The automatic **Cut the release** step of `engine-update.yml` was not part of
this release, which was cut by hand with `scripts/release.js`. Its fix is
covered by the tests and is first exercised by the next Stable update.

Two more causes of repeated work were removed with slots:

- The **Cut the release** step failed both times it ran, on 9 and 12
  November: `engine-update-ci.sh` set its Git credential helper twice in one
  checkout, and the second `git config credential.helper ''` exits 5 when two
  values exist. Both updates had to be released by hand.
- `scripts/release.js prepare` refuses to release while an `engine-update.yml`
  run is taking an open update pull request to its merge
  (`--during-engine-update` overrides it): that update releases everything on
  main itself when it merges, so a release cut meanwhile built the optimised
  package one more time and shipped the old engine. An update that failed, or
  that no run is working on, does not hold a release back.

Deferred investigation of earlier failure detection, phase-level proof reuse
and repeated acceptance work is recorded in the
[engine pipeline performance study](./engine-pipeline-study.md). It is a
follow-up for after the current maintenance/release work, not a change to the
validation or publication gates described here.

## Data use

Moving the candidate downloads only what changed in Chromium since the previous
pin: Git objects, toolchain packages and hook downloads. Before `gclient sync`,
`move` fetches each dependency's new revision alone (`git fetch --depth=1`), one
repository at a time and waiting when googlesource answers HTTP 429. A
dependency with its own `DEPS` (ANGLE, Dawn) is checked out at its new revision
first, so the dependencies it pins are fetched the same way. Left to
itself, `gclient sync` fetches every branch's full history into an existing
dependency that lacks its new revision, which costs gigabytes per version on a
shallow root. A security update within
a major version is a small download; a new major version is larger, because four
weeks of Chromium changes and usually new toolchains come with it. Adopting a
candidate into the active root and building packages downloads nothing more than
the release's PGO profile. A new engine root is created with `seed`, which copies
an existing root instead of downloading Chromium again.

With slots, each slot moves every other update and covers the two versions
since it was last active, so it downloads the changes of both. For security
updates within a major version that is still a small download.

## Limits

- Validation runs Fly's own unit and browser tests (`fly_unittests`,
  `Fly*BrowserTest.*`), not Chromium's full test suites. Google tests the engine
  itself. On the owned Xvfb display, browser tests use software compositing
  (`native.py test-browser --disable-gpu`): GPU-backed snapshots can return
  transparent pixels there. This does not establish physical GPU acceptance.
- Releasing is automatic. Everything merged to main since the last release ships
  with the engine update. To hold the version-bump pull request for a manual
  merge, set the repository variable `FLY_ENGINE_RELEASE` to `hold`.
- The engine host must be on to run the workflow. A powered-off host delays the
  update; nothing is lost.
- Scheduled workflows on GitHub stop after 60 days without repository activity.
