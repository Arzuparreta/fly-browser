# Ad blocker performance follow-up

The CNN request storm no longer reproduces after removing Ghostery and using the included uBlock Origin. This fixes a severe default-path defect; it is **not a general reduction in startup time or one-tab memory**. The full uBlock has its own cost, and Fly now waits for its filters before the first web navigation.

Application source: `25b81677940ad1b73ec59149ba31886ad74e3218`. The [original Chromium comparison](./performance-vs-chromium) and its raw samples remain unchanged as a historical baseline. Current engine: Electron 44.7.2 / Chromium 152.0.7977.149. Subsequent changes in this task add documentation and optional probes, not application code.

## Three repeated CNN runs

[Raw samples](/benchmarks/2026-10-15-ublock-followup.json), three fresh profiles, each cold-started and then restarted. The same runner, physical X11/i3 display, default GPU and machine as the original audit are used. This workload has **one local tab plus CNN in the foreground**, rather than ten real sites. The idle interval is 10 seconds, rather than the original 30. These runs are not directly comparable to the original ten-tab totals.

| Metric | Median [min–max] |
| --- | ---: |
| Cold launch to protected local page, ms | 1558 [1491–1563] |
| Warm launch to protected local page, ms | 1472 [1469–1508] |
| Cold sampled peak PSS, MiB | 620 [619–620] |
| Warm sampled peak PSS, MiB | 593 [590–595] |
| Cold one-tab PSS after 15 s, MiB | 469 [459–508] |
| Warm one-tab PSS after 15 s, MiB | 519 [470–523] |
| Cold one-tab CPU over the next 10 s, % of one core | 4.70 [1.21–5.15] |
| Warm one-tab CPU over the next 10 s, % of one core | 1.36 [1.15–1.50] |
| CNN requests started, including cross-site frames | 117 [117–118] |
| CNN requests blocked | 6 [6–6] |
| Local tab + visible CNN, PSS MiB | 913 [900–923] |
| Visible CNN CPU over 10 s, % of one core | 92.16 [89.70–96.96] |
| PSS 10 s after closing CNN, MiB | 382 [381–383] |

The old blocker produced about 3,700 retries per second against a video configuration URL. The new samples contain only 117–118 requests across the complete CNN observation, including its cross-site frames. Network and cosmetic blocking are also checked independently with deterministic local rules.

Startup is slower than the old visible-first-page metric. Previously the extension could still be initialising after the page appeared; now first navigation waits for filtering, preventing permanently aborted media. Even against the old extension-enabled configuration, this is a real user-visible delay, not a claimed startup improvement. The short-lived cold profile is restarted before all of uBlock's later background cache work necessarily finishes, so this is not a mature profile used for hours. Memory sampling does not force garbage collection and excludes GPU device memory. The native compiler/cache allocation is gone, but the extension remains substantial.

## Foreground and background control

The high foreground CPU above warranted a separate check. One fresh-profile control per configuration loads CNN after 15 seconds on a local fixture, settles CNN for 15 seconds, measures 10 seconds, switches to the local tab, settles for 3 seconds and measures another 10 seconds. This is exploratory, not a repeated browser ranking. [Control samples](/benchmarks/2026-10-15-cnn-visibility.json).

| Configuration | Visible CPU, % of one core | Background CPU, % of one core | Root requests | Brightline config requests |
| --- | ---: | ---: | ---: | ---: |
| chromium | 99.03 | 2.62 | 129 | 2 |
| fly | 86.47 | 2.44 | 114 | 0 |
| fly-noblock | 87.23 | 2.56 | 138 | 2 |

Both browsers loaded several muted, automatically playing high-resolution videos. The substantial foreground CPU also occurs in Chromium and in Fly without blocking; it does not establish a new Fly-specific regression. The background controls return to a few percent of a core, unlike the original sustained retry storm. Root request counts in this control exclude cross-site frames; the repeated-run table above includes them.

Fly's hidden webview still reports `document.hidden === false`, while Chromium's background tab reports `true`. This Electron/webview behaviour remains a limitation; these CPU results do not establish complete background-tab parity for other sites. An initial control launcher looked for `about:blank` rather than Fly's local new-tab URL and failed before collecting any Fly data. Its completed Chromium sample is retained; the corrected launcher identifies the newly created target by id for the two Fly samples.

## Why Lite is not installed

[uBlock Origin Lite](https://github.com/uBlockOrigin/uBOL-home) is the maintained Manifest V3 option for current Chromium. The [full uBlock](https://github.com/gorhill/uBlock) also remains maintained, and the bundled copy matches its latest stable release checked for this task. Electron supports only a [subset of extension APIs](https://www.electronjs.org/docs/latest/api/extensions/).

A standalone Electron probe, with no Fly request hooks or compatibility adapter, loads an MV3 extension containing an enabled static `declarativeNetRequest` block rule. The matching script still reaches the local server and executes: `serverHits=1`, `scriptExecuted=true`, `enforced=false`. [Probe result](/benchmarks/2026-10-15-declarative-blocking.json). Lite depends on this enforcement, so substituting it now would lose network blocking. Future MV2 support is not guaranteed; repeat the capability probe when updating Electron before switching.

## Changes and validation

Ghostery, its compiler, per-tab shields and Canvas/WebGL/audio/hardware spoofing are removed. uBlock is enabled in new profiles, waits for final filter readiness, receives network hooks before loading, and keeps the latest address entered during startup. Closed or hibernated views cannot start a delayed navigation. Extension backgrounds use Chromium's renderer sandbox. Extension event callbacks now deduplicate and detach their actual wrappers; production debug logging is disabled.

`npm test`, whitespace checks and the documentation build pass. Disposable-profile Electron coverage checks network/cosmetic filtering, popup and persistent disable/remove decisions, media on the first enabled launch and on restore, saved filters on first navigation, unchanged Canvas pixels, the extension renderer's sandbox flag and seccomp, keyboard recording and editable pages, and sidebar controls. A Linux directory build and packaged smoke test pass. Other operating systems, low-memory hardware, battery use and a repeat of the entire ten-site/Speedometer/video matrix remain unverified.

Saved extension settings are preserved: users whose global switch is off must enable it to retain ad blocking. The in-memory private session cannot load extensions and now has no ad blocker. Trusted-site entries remain permission exceptions; uBlock's own allowlist controls ad blocking.

## Reproduce

```sh
FLY_VS_CONFIGS=fly FLY_VS_SCENARIOS=browse \
  FLY_VS_SITES=https://edition.cnn.com/ FLY_VS_ROUNDS=3 \
  FLY_VS_IDLE_MS=10000 FLY_VS_OUTPUT=/tmp/fly-ublock-followup.json \
  node scripts/benchmark-vs-chromium.js
node scripts/benchmark-cnn-visibility.js
FLY_PLAYWRIGHT_MODULE=/path/to/playwright node scripts/probe-declarative-blocking.js
FLY_PLAYWRIGHT_MODULE=/path/to/playwright npm run test:blocking-ui
```

Run desktop tests and benchmarks sequentially. The probes use disposable profiles, not the owner's data. Playwright is an optional external test dependency for the capability and UI tests; the benchmark runner uses its built-in DevTools client.
