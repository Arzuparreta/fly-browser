# Chromium control development

This tooling prepares the **unmodified upstream control** for M0. It does not
build native Fly yet. Electron remains the default app, installer and release
runtime until all cutover gates pass. Follow the
[implementation record](./implementation-status.md) for actual results.

## Source and prerequisites

Use a dedicated external checkout, Python 3.9+, Git and upstream-supported
build dependencies. The Linux upstream minimum is 8 GiB RAM and 100 GB free;
more than 16 GiB RAM is recommended. The development host passed the resource
checks on its external volume. Downloading source and dependencies is explicit
and may take substantial time and disk space.

The source and depot_tools revisions are exact official Git SHAs in
`chromium/upstream.lock.json`. The Chromium `DEPS` content digest is also checked.
`sync` writes actual dependency revisions to the external
`evidence/dependency-revisions.json`; hooks fetch tools/sysroots according to
that pinned dependency state. depot_tools automatic updates and metrics are
disabled. No Google credentials or remote build service are configured.

Official instructions:
[Linux](https://chromium.googlesource.com/chromium/src/+/main/docs/linux/build_instructions.md),
[macOS](https://chromium.googlesource.com/chromium/src/+/main/docs/mac_build_instructions.md),
[Windows](https://chromium.googlesource.com/chromium/src/+/main/docs/windows_build_instructions.md).
The scripts have only been exercised on Linux; other hosts remain unverified.
On Windows use the supported Visual Studio toolchain and a depot_tools-capable
shell; the runner bootstraps pinned Windows tools directly, without updating depot_tools.
Windows wrapper/toolchain validation is tracked by the manual CI workflow.

## Commands

Replace the root below with a dedicated directory outside the Fly repository.
The local execution record uses `/mnt/storage/fly-chromium`.

```sh
python3 scripts/chromium/engine.py doctor --root /path/to/fly-chromium
python3 scripts/chromium/engine.py bootstrap --root /path/to/fly-chromium
python3 scripts/chromium/engine.py sync --root /path/to/fly-chromium --jobs 8
```

Before hooks/build, install the pinned upstream host build dependencies. On a
supported Ubuntu host, inspect and run the upstream
`src/build/install-build-deps.sh`. On other distributions reconcile those
dependencies with the distro packages; the runner does not silently invoke a
privileged package manager. Then:

```sh
python3 scripts/chromium/engine.py hooks --root /path/to/fly-chromium
python3 scripts/chromium/engine.py configure --root /path/to/fly-chromium
python3 scripts/chromium/engine.py build --root /path/to/fly-chromium --jobs 8
python3 scripts/chromium/engine.py run --root /path/to/fly-chromium --url about:blank
```

The target is `chrome`, output `src/out/FlyControl`, using
`chromium/gn/control.gnargs`: an optimised component build with local execution.
It is a development control, not a distribution or performance comparison build.
Never compare it against an optimised packaged Fly and claim resource savings.

`run` creates and retains a new disposable profile below the external checkout.
It takes a URL, not arbitrary Chromium switches, and keeps the sandbox enabled.
Process exit success records only that the process exited; visual/security/
feature acceptance must be observed separately.

Each successful stage writes an atomic record under external `evidence/`.
Downstream stages require the preceding record for the current lock, and build
arguments and executable hashes are checked. Records capture Fly's working-tree
dirty state so a development run cannot be mistaken for a clean-checkout build.

## Resume and preserve local work

Re-run `bootstrap` after an interrupted initial source fetch and `sync` after a
dependency download failure. Existing remote/revision/configuration mismatches,
tracked changes and untracked source files are rejected. The runner never resets
or cleans them. Investigate and preserve such work before retrying.

One command holds `.fly-engine-operation/owner.json` under the external root.
If interrupted, inspect the recorded PID/host and its child processes before
removing that directory. Never remove an active lease or sync while building.
Build/install output and disposable profiles are outside the product repository.

## Contracts and tooling verification

```sh
node scripts/chromium/capture-contracts.js
node scripts/chromium/capture-contracts.js --check
python3 -B -m unittest discover -s tests/chromium -p 'test_*.py'
```

Contract capture reads repository loaders/catalogues only, never user data.
The generated [parity ledger](./parity-ledger.md) maps actions and preferences
to milestones/acceptance rows; every native result starts pending. Python tests
use local disposable repositories and do not fetch source or compile Chromium.
Ordinary PR CI remains the existing `npm test` workflow.

## Desktop fixture harness

The explicit fixture smoke requires a separately available Playwright module
supporting CDP `noDefaults`. Set `FLY_PLAYWRIGHT_MODULE` to its module path if it
is not in normal Node resolution. Use the compiled control executable:

```sh
node tests/chromium/control-smoke.js \
  --executable /path/to/fly-chromium/src/out/FlyControl/chrome \
  --work-dir /path/to/fly-chromium/fixture-evidence --backend x11
node tests/chromium/control-smoke.js \
  --executable /path/to/fly-chromium/src/out/FlyControl/chrome \
  --work-dir /path/to/fly-chromium/fixture-evidence --backend x11 --extension
```

These commands create a fresh disposable profile, loopback fixtures, a bounded
extension probe, screenshot, process log and JSON result per run. They observe
native foreground/background visibility, explicit pause and playing background
audio. The optional MV3 probe records native tab/window IDs, profile-local
extension storage and installation events, and remembers playback across native
visibility changes. The fixture also supplies synthetic cookie/localStorage/
IndexedDB/service-worker inputs for future workspace tests.

The harness disables Playwright's default focus emulation via `noDefaults` and
explicit CDP guards; otherwise the automation itself keeps background documents
visible. It asserts both test tabs share one native window and observes native
hidden state without replacing any page getter or media method. Security stays
enabled. Audio-only fixture checks are preparatory evidence: they do not cover
Fly workspace composition, native split, minimisation, video, Twitch/real Pie,
packaged applications or the complete A08/A09 acceptance rows.

The first successful harness checks used the system Chromium executable. That
validates the harness only; the same-pin upstream control and native Fly still
need separate runs. The implementation record distinguishes their evidence.

## Resolve a future engine update

```sh
python3 scripts/chromium/pin-upstream.py --output /tmp/fly-upstream-candidate.json
```

This explicitly queries official Stable/Linux release metadata and verifies
`chrome/VERSION` and `DEPS` against the exact source pin. It refuses to overwrite
an existing output. Review the candidate, upstream security/release state and
platform coverage before adopting it on an engine-update branch. It does not
rebase, update the current lock or change a checkout. The engine-update rehearsal
is still outstanding. Fly's product version remains generated from
`package.json` by the existing release tooling.


## Native overlay development

After the unmodified control build succeeds, use the same dedicated engine root:

```sh
python3 scripts/chromium/native.py apply --root /path/to/fly-chromium
python3 scripts/chromium/native.py configure --root /path/to/fly-chromium
python3 scripts/chromium/native.py test --root /path/to/fly-chromium
python3 scripts/chromium/native.py build --root /path/to/fly-chromium
```

The native output is `src/out/FlyNative`. During development, its native desktop
window requires `--fly-native`; Electron remains the production runtime. Native
source changes and input fingerprints are recorded in `evidence/native-overlay.json`.
`verify` rejects edits outside that manifest as well as changed managed files.
To install a changed overlay, first `revert` the previous managed copy, then
`apply` again. `recover` restores pristine upstream after an interrupted apply or
revert, provided every affected file still matches one of the transaction's
expected states. It refuses to overwrite subsequent edits. Never remove a live
operation lease or run dependency sync over an installed native overlay.

The manual `Native Chromium validation` workflow accepts `depth=configure` or
`depth=build` and selected platforms. It retains stage logs and provenance even
on failure. Tooling tests, source configuration, a linked browser and packaged
desktop acceptance are separate results. Hosted-runner resource limits may
prevent larger stages; those failures require evidence, not silent skips.


### Native service tests without browser integration

`native.py test-core --root /path/to/dedicated-engine` installs only the new
`//fly` sources and builds the real Chromium `fly_unittests` target using
`gn gen out/FlyUnit --root-target=//fly`. It does not patch the browser or require
a completed control browser. This is useful for native service tests on CI while
the full local control is still building. It still requires pinned source,
dependencies and completed hooks. Use a separate checkout when another engine
operation is active. Revert this core-only overlay before applying browser hooks.

The manual workflow's `units` depth performs this native compilation and test
execution on the selected platforms. `contracts` runs only the small Python
transaction/checkout tests and JavaScript syntax checks. Neither depth passes
desktop, migration or package acceptance by itself.
