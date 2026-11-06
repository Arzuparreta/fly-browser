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
5. `release.yml` gives the owner's engine root the exact sources validated in the
   candidate, copied locally, builds the packages and publishes them. A separate
   job then rebuilds `out/FlyNative`, so `./launch.sh` runs the new engine.

A failure stops the process with the pull request still open and the log
commented on it. Nothing reaches users until the whole chain has passed.

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
   completion. No recurring agent wakeups or chat monitoring are included.

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
failing the whole workflow. No Stable update means the candidate build and
release steps are skipped; that run is not evidence of a fresh native build.
Report these results separately, with links and any unverified build or
installed-browser state. Read current merged main rather than treating an
open PR or an old conversation as delivered work.

The owner can see pending Beta work in the repository's **Issues**, Stable
update failures in **Actions** and the update **Pull request**, and published
updates in **Releases**. To receive GitHub notifications, use **Watch > Custom**
for issues, pull requests and releases, or subscribe to the Beta issue itself.
Notification delivery depends on the owner's GitHub settings; the workflow
opening an issue does not guarantee an email or a chat message.

The automatic workflow detects conflicts, records them, and processes compatible
Stable updates. It does **not** repair conflicting patches or start an agent.
The owner checks GitHub or asks an agent periodically; no scheduled agent
wakeups or chat monitoring are part of this setup. An agent answering a status
question must include pending Beta work even when Stable is current.

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

The engine host has two Chromium checkouts:

| Root | Environment variable | Used by |
| --- | --- | --- |
| Active, `/mnt/storage/fly-chromium` | `FLY_ENGINE_ROOT` | Development builds, `./launch.sh`, releases |
| Candidate, `/mnt/storage/fly-chromium-next` | `FLY_ENGINE_CANDIDATE_ROOT` | Validating engine updates |

An update is built and tested in the candidate first. If a patch or the build
fails there, the active root keeps working on the current pin, and the agents
and the owner using it are not blocked. A release then adopts the validated
sources: `rsync` copies only the files that differ (never build outputs) and
dates each written file now, so ninja rebuilds exactly what changed. The release
downloads nothing.

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
| `seed --from A --root B` | Creates root B by copying root A locally (sources and dependency state, no build outputs) |
| `move --root R` | Moves root R from the pin it was synced to onto this checkout's pin: reverts the overlay, checks out the new revisions, `gclient sync`, hooks. Resumable after an interruption |
| `upgrade --root R [--through apply\|build]` | `move`, then rebuilds the control, applies the overlay and, with `build`, builds `out/FlyNative`. Does nothing when R is already current |
| `validate --root R --test-tools DIR` | `upgrade`, then Fly's unit and browser tests on an owned Xvfb display |
| `adopt --candidate C --root R` | Gives R the sources of candidate C, when C passed `validate` for this pin |
| `follow --root R [--candidate C]` | What a release runs: `adopt` when possible, otherwise `upgrade --through apply` |

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
application.

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

## Limits

- Validation runs Fly's own unit and browser tests (`fly_unittests`,
  `Fly*BrowserTest.*`), not Chromium's full test suites. Google tests the engine
  itself.
- Releasing is automatic. Everything merged to main since the last release ships
  with the engine update. To hold the version-bump pull request for a manual
  merge, set the repository variable `FLY_ENGINE_RELEASE` to `hold`.
- The engine host must be on to run the workflow. A powered-off host delays the
  update; nothing is lost.
- Scheduled workflows on GitHub stop after 60 days without repository activity.
