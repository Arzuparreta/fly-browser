# Chromium control development

Native Chromium is Fly's default runtime. Electron remains available for
comparison under [legacy instructions](https://github.com/Arzuparreta/fly-browser/tree/main/legacy/electron).
The commands below first prepare an **unmodified upstream control**; the next
section builds Fly's native overlay. Installing npm dependencies alone does not
build the browser. See the [implementation record](./implementation-status.md)
for acceptance evidence and limits.

## Build and launch native Fly

After completing the control setup below:

```sh
python3 scripts/chromium/native.py apply --root /path/to/fly-chromium
python3 scripts/chromium/native.py configure --root /path/to/fly-chromium
python3 scripts/chromium/native.py build --root /path/to/fly-chromium
python3 scripts/chromium/native.py test --root /path/to/fly-chromium
python3 scripts/chromium/native.py test-browser --root /path/to/fly-chromium
FLY_ENGINE_ROOT=/path/to/fly-chromium FLY_ENGINE_OUTPUT=out/FlyNative npm start
```

Browser tests require a graphical session or owned virtual display. Use only
disposable profiles. For a static release build and tarball:

```sh
python3 scripts/chromium/release.py --root /path/to/fly-chromium
FLY_ENGINE_ROOT=/path/to/fly-chromium FLY_ENGINE_OUTPUT=out/FlyRelease npm start
```

Run `./launch.sh` (or `npm start`) to open the existing local build. The launcher
looks for `fly-chromium` beside this repository, beside its parent directory,
then in `~/.cache/fly-chromium`. It prefers `out/FlyNative` over `out/FlyRelease`.
Set `FLY_ENGINE_ROOT` and `FLY_ENGINE_OUTPUT` to select another checkout or output.
The script launches the compiled browser; it does not rebuild source changes.
Without a local build it downloads and starts the published release instead.
`./launch.sh --pr N` starts the test build of pull request N and `--release`
the published release, with or without a local build; see
[test builds](../releasing.md#test-builds-of-pull-requests).

Local launches use a persistent `fly-browser-dev` profile under
`${XDG_CONFIG_HOME:-$HOME/.config}` and an empty import source. They do not share
the installed browser's profile or automatically import personal data, so both
can run simultaneously. Arguments are preserved, including explicit
`--user-data-dir` and `--fly-import-from` overrides. The installed launcher keeps
its existing profile. Downloaded builds use the same development profile. There
is no automatic fallback to Electron.
Release hosting prerequisites are in [releasing](../releasing.md).

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
window requires `--fly-native`, supplied by the default launcher. Electron is legacy. Native
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


### Browser topology acceptance

After building the complete native browser, run:

```sh
python3 scripts/chromium/native.py test-browser --root /path/to/fly-chromium
```

This builds `fly_browser_tests` with Chromium's original in-process browser
fixture and out-of-process test launcher. It runs `FlyWorkspaceBrowserTest.*`
and `FlyPageScrollBrowserTest.*`, with disposable profiles owned by the upstream
test harness. Its summary is `evidence/native-browser-tests.json`. These tests exercise
real windows, profiles, content visibility and split models; service-unit results
cannot substitute for them. The tests keep sandboxing enabled. The scoped binary
compiles Fly's cases and real browser/test support without the unrelated upstream test catalogue. Native
browser embedding is currently implemented only on Linux and Windows.

To exercise the same cases inside upstream's complete binary, add
`--browser-target browser_tests`. Both choices use the same test sources and
write the target into `evidence/native-test-browser.json`. This option does not
run or pass additional upstream cases.

Windows hosted-runner validation provisions the SDK required by the pinned
Chromium source using the [official Microsoft installer](https://learn.microsoft.com/en-us/windows/apps/windows-sdk/downloads).
The CI-only PowerShell script validates the Microsoft signature and retains the
installer digest and installation logs. It never runs as part of local bootstrap.

### Synthetic video checks

The shared control/native GN preset enables upstream H264/AAC decoding through
`proprietary_codecs` and the Chrome FFmpeg codec selection. This does not enable
Google product branding. Control and Fly must use identical GN options before
comparing media behaviour. Preserve an older output before reconfiguring; a
successful build record with different arguments cannot authorise native apply.

The media fixture is a generated local MP4 with a test pattern and quiet tone.
After building the matching control, use:

```sh
node tests/chromium/control-smoke.js --executable /path/to/fly-chromium/src/out/FlyControl/chrome --work-dir /path/to/fly-chromium/fixture-evidence --backend x11 --video --extension
```

After building native Fly, select its executable and add
`--native --native-record /path/to/fly-chromium/evidence/native-build.json`.
The native build records the executable path/hash, overlay fingerprint and desktop
support. The fixture checks those fields and the pinned engine lock before
creating a profile or launching anything. A control/test record, changed binary
or platform without the desktop adapter cannot authorise native media evidence.
Omit
`--video` to retain the PCM audio scenario; omit `--extension` for the baseline
without the visibility-sensitive MV3 probe. Every run creates a new profile and
records the executable/media hashes, codec capability, actual frame decode,
visibility and pause/background checks. These fixtures do not establish live
Twitch, real Pie, HLS, DRM or package acceptance.

The manual workflow's `build` depth also runs the scoped browser suite on Linux
and Windows after compilation. Linux installs Xvfb for an isolated test display.
macOS currently validates compilation/services only, because its native desktop
adapter is unfinished. CI uses explicit phase-specific free-space floors (80 GiB
initially, 60 GiB before dependency sync, 40 GiB for compilation) and reclaims only
control compilation objects before native compilation. Logs retain actual resource
failures and test results; a successful run is not complete platform acceptance.
Local native builds retain a 100-GiB default floor; `--minimum-free-gib` makes an
explicit alternative of at least 40 GiB possible.

### Hosted source compilation

The manual workflow also accepts depth `objects`. It bootstraps the exact source
and toolchain, applies a distinct compile-only overlay transaction and builds
every modified upstream and Fly translation unit, including the scoped browser
test source, with GN's original compiler rules, flags, response files and per-edge
variables. A separate Ninja manifest retains generated header/module prerequisites
and their original generators/tools. It excludes runtime-only data generation,
such as snapshots whose generators would otherwise link most of Chromium.
Original GN files and the derived recipes are hashed in the evidence. Two
compiler workers bound hosted memory use. This validates compiler outputs and
header dependencies; linking, runtime resources and browser acceptance belong
to the separate full build.
On macOS the unfinished desktop/browser-test sources are explicitly excluded;
missing edges for shared/upstream sources fail validation.

Local equivalent (use a dedicated engine checkout):

```sh
python3 scripts/chromium/native.py test-objects --root /path/to/fly-chromium --jobs 2 --minimum-free-gib 40
```

The transaction is tagged `objects` and cannot authorise native configure/build
or media acceptance. Revert it before building the unmodified control and applying
the full native overlay. Full build depth still requires the matching control.


For an already applied full overlay at the same pinned engine revision,
`native.py refresh-overlay --root /path/to/engine --include-upstream` also permits
reviewed upstream patch changes. It replays the complete new stack privately,
checks all existing managed bytes and original pinned contents, and journals
both reviewed versions before updating only changed files. Removed patch targets
return to their original pinned bytes; unchanged headers keep their timestamps.
The ordinary `refresh-overlay` command continues to reject any upstream change.
This opt-in does not change the source pin or promote a core/compile-only tree.
Interrupted transactions use `native.py recover --root /path/to/engine`; unknown
owner edits remain protected and require preservation before recovery can proceed.

The optional [live Twitch/Pie desktop regression](./live-media.md) records real
public-player behavior separately from deterministic local media fixtures.

### Release build and Linux package

Development outputs (`FlyNative`) are component builds with DCHECKs on. Packages
come from a separate static build without DCHECKs:

```sh
cd /path/to/engine/src
mkdir -p out/FlyRelease && cp /path/to/fly/chromium/gn/release.gnargs out/FlyRelease/args.gn
gn gen out/FlyRelease
autoninja -C out/FlyRelease chrome chrome_sandbox
python3 /path/to/fly/scripts/chromium/package-linux.py --root /path/to/engine
```

The package is `dist/Fly-<version>-native-linux-x64.tar.gz` (with `.sha256`). It
contains `install.sh`, which installs "Fly" for the current user in
`$XDG_DATA_HOME/fly-browser` with a menu entry and
`~/.local/bin/fly-browser` plus the `fly-browser` alias, next to an Electron installation
(`install.sh --remove` uninstalls and keeps data). Native data lives in
`$XDG_CONFIG_HOME/fly-browser`, outside Electron Fly's `~/.config/fly-browser`.
On its first start native Fly imports the Electron profile; if Electron Fly is
open then, the import runs on the first start after it closes. A full static build does not fit the
hosted CI runners, so the release workflow uses a provisioned self-hosted engine runner.
