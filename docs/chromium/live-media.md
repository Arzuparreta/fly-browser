# Explicit live Twitch/Pie desktop regression

This is an additional media test against a public service. It does not replace
the network-free media fixtures or establish that a package works. Ordinary pull
requests still run only the existing lightweight
checks. Live failures and unavailable/offline samples stay recorded as failures
or coverage gaps, never as passes.

`tests/chromium/live-twitch.js` launches a sandboxed native Fly or unmodified
Chromium control from the same engine lock, verifies the binary and complete
runtime manifest, and creates a fresh disposable profile. It observes actual
video frames, uses physical X11 input to pause/resume the player and verifies
pause retention across real tab hiding/return. Fly additionally exercises the
native workspace selector, preferences and creation/switching of a second
profile-isolated workspace. Playing live media must continue in a hidden tab.
A preroll ad is not the live stream: the test waits for LIVE and unmutes through
the actual player before checking background playback.

Pie itself changes main-world focus/visibility on Twitch. Each checkpoint
therefore records both site-exposed values and native DOM values read in an
isolated world. Creating that world does not override APIs, install event
handlers or change media/focus/visibility. The test disables CDP focus emulation;
its physical input and native activation drive the transitions. An empty video
list or decoder error cannot count as preserved pause.

The official Pie CRX is signature-checked against its fixed Chrome Web Store
identity. The original CRX hash, actual manifest version, harness/input hashes,
exact build record, arguments, timestamps, native/exposed checkpoints and root
screenshots/hashes are retained. Unpacking adds only the public manifest key to
retain the official extension identity. This tests the real extension code in
disposable profiles; it does not validate native store installation or updates.
Pie onboarding tabs are closed without configuring accounts, rewards or sync.
The package is never committed or bundled into Fly.

## Run

Build both browsers using [the pinned build workflow](./build.md). Control build
records created before runtime-closure recording need a separately identified,
verified runtime supplement; do not silently relabel an old build as newly built.
Provide Node/Playwright, Python, Xvfb, xauth, i3/i3-msg, libX11/libXtst and
ImageMagick's `import`. `desktop-fixture.py` owns a unique private display and
window manager, imposes an outer timeout and cleans up its own process groups.
It never connects input automation to the owner's desktop or profile.

Download the current Chrome package explicitly, preserving its original bytes:

```sh
FLY_PIE_FIXTURE="$FLY_ENGINE_ROOT/fixture-evidence/pie.crx"
export FLY_PIE_FIXTURE
node - <<'JS'
const fs = require('node:fs');
const crypto = require('node:crypto');
const {downloadStorePackage, parseCrx} = require('./extensionPackages');
const lock = require('./chromium/upstream.lock.json');
(async () => {
    const id = 'jpkfgepcmmchgfbjblnodjhldacghenp';
    const bytes = await downloadStorePackage(id, lock.engine_version);
    parseCrx(bytes, id);
    fs.writeFileSync(process.env.FLY_PIE_FIXTURE, bytes, {flag: 'wx', mode: 0o600});
    console.log(crypto.createHash('sha256').update(bytes).digest('hex'));
})().catch(error => {console.error(error); process.exitCode = 1;});
JS
```

Choose an actually live public sample, without account/query data. The following
example is a sample channel, not a guarantee that it remains live. Repeat for
native/control and with/without `--pie-crx`:

```sh
python3 tests/chromium/desktop-fixture.py \
  --work-dir "$FLY_ENGINE_ROOT/fixture-evidence" --timeout 360 -- \
  node tests/chromium/live-twitch.js --native \
  --executable "$FLY_ENGINE_ROOT/src/out/FlyNative/chrome" \
  --build-record "$FLY_ENGINE_ROOT/evidence/native-build.json" \
  --work-dir "$FLY_ENGINE_ROOT/fixture-evidence" \
  --url https://www.twitch.tv/lofigirl --pie-crx "$FLY_PIE_FIXTURE"
```

For the control, omit `--native` and select its executable and successful control
build record. Use `--xvfb /absolute/path/to/Xvfb` when the executable is not on
PATH. Set `FLY_PLAYWRIGHT_MODULE` only if Playwright is installed outside the
normal Node module search path. Each invocation reports its `desktop-*/` log
folder; its fixture log reports the separate `live-twitch-*/result.json` folder.
Inspect both, including nonzero exits and outer timeouts. No credentials,
channel history, owner extension storage or original profile is an input.

The public homepage is an accepted URL, but it must expose an actual playable
live player for the same assertions. A rotating/offline featured card, consent
screen, service error or absent LIVE indicator is a gap to investigate, not a
reason to remove the live-video assertions. A channel result does not establish
the homepage flow without its own recorded run.

The three network-free guards in `test_live_fixture.py` cover URL boundaries,
vacuous/decoder-error pause evidence and refusal to send input to an owner
display. They do not claim to execute X11 or live playback on other platforms.

Official references: [Pie installation](https://help.pie.org/en/articles/8781761)
and [its Chrome Web Store entry](https://chromewebstore.google.com/detail/pie-adblock-a-powerful-fr/jpkfgepcmmchgfbjblnodjhldacghenp).
