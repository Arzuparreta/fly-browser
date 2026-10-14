# Fork performance audit

Audit date: 14 October 2026. The fork before optimisation is personal at `d4014bdb9e689d6667316fd00ee28c34de2529aa`, based on upstream 0.15.0 at `fa77f782efe2c1fe1e0655eb87382cca509b6212`. That was the remote main when the audit began. During final verification, upstream advanced to `2f8ca5f30e014cad1e2a3ff57d45be69ec777dba`, adding direct tab jumping, number badges and a sidebar cursor. The final comparison additionally measures that newer, pinned main revision. The integration-base results are retained separately so the fork's own changes remain traceable. Main later advanced to `3be7f9694bf6a77e916623f667e25f6199e741d0` with a documentation-only chronicle; it is integrated before submission. Its application-source hash is unchanged from 2f8ca5f, so the final measured baseline is also the current main application.

The target is virtually no additional cost during normal browsing, with optional work paid for only when requested. This means no added idle polling or processes, no extension runtime while disabled, preserved tab hibernation and no repeatable material regression in the measured interactions. It does not mean literally zero additional code, DOM nodes or bytes, or a guarantee for untested hardware and websites.

## Final controlled comparison with identical cached filters

Three alternating fresh-profile pairs use the final source hashes stated below, the same 6.6 MiB filter cache and software rendering on the physical display. Values are medians with per-run ranges; interaction medians are calculated within each run first. The initial Preferences open remains in the raw arrays. The idle window is 5 seconds.

| Metric | Main 3be7f96 | Optimised personal |
| --- | ---: | ---: |
| Local page ready, ms | 656.8 [638.9–698.0] | 580.6 [568.5–581.8] |
| One-tab PSS, MiB | 355.4 [350.1–361.8] | 324.6 [324.2–328.2] |
| Six-tab PSS, MiB | 519.6 [511.0–523.4] | 453.4 [452.5–462.8] |
| Hibernated PSS, MiB | 386.5 [367.3–390.1] | 341.9 [340.9–345.9] |
| Idle CPU, % of one core | 0.992 [0.793–1.191] | 0.794 [0.397–0.794] |
| Preferences open/layout, ms | 3.50 [3.50–3.50] | 2.00 [1.90–2.00] |
| Preferences at 6× host CPU throttle, ms | 17.85 [17.55–18.15] | 9.70 [9.55–9.90] |
| Tab creation/layout, ms | 4.30 [4.20–4.30] | 4.20 [4.20–4.30] |
| Tab switching/layout, ms | 0.60 [0.60–0.60] | 0.50 [0.50–0.60] |
| Tab switching at 6× host CPU throttle, ms | 3.15 [3.10–3.15] | 2.80 [2.60–2.80] |
| Host JS heap, MiB | 4.61 [4.09–4.64] | 5.14 [4.98–5.17] |

[Final cached-filter samples](/benchmarks/2026-10-14-final-cached.json). Process counts remain 7 / 12 / 7 for one tab / six tabs / hibernation. The host retains a small static catalogue/handler footprint visible in heap, DOM and listener counters; the complete process tree is the memory comparison. Sub-millisecond creation differences are retained explicitly rather than rounded to equality. These numbers do not support a claim of literally zero extra bytes or identical timings.

## Final comparison with default GPU and an empty filter cache

Three alternating fresh-profile pairs use real filter downloads, GPU compositing confirmed enabled, an active NVIDIA device (vendor 0x10de, device 0x2504, driver 615.78.08) and a 30-second idle window. Both revisions use the same physical monitor and recorded viewport.

| Metric | Main 3be7f96 | Optimised personal |
| --- | ---: | ---: |
| Local page ready, ms | 405.8 [402.2–422.8] | 400.0 [397.2–419.6] |
| One-tab PSS, MiB | 268.5 [265.5–270.5] | 275.5 [272.7–275.8] |
| Six-tab PSS, MiB | 390.6 [375.2–432.3] | 384.2 [381.1–396.9] |
| Hibernated PSS, MiB | 305.1 [299.5–350.0] | 305.2 [302.5–308.8] |
| Idle CPU, % of one core | 0.665 [0.632–0.766] | 0.433 [0.433–0.499] |
| Preferences open/layout, ms | 2.80 [2.75–2.90] | 1.55 [1.50–1.55] |
| Preferences at 6× host CPU throttle, ms | 13.70 [13.55–15.00] | 6.90 [6.70–7.00] |
| Tab creation/layout, ms | 4.10 [4.00–4.20] | 4.20 [4.10–4.20] |
| Tab switching/layout, ms | 0.45 [0.40–0.50] | 0.40 [0.30–0.50] |
| Tab switching at 6× host CPU throttle, ms | 2.05 [1.75–3.10] | 1.60 [1.60–1.70] |
| Host JS heap, MiB | 3.19 [3.19–4.04] | 3.41 [3.41–3.41] |

[Final GPU-default samples](/benchmarks/2026-10-14-final-gpu.json). The normal launch has no added idle process or polling; disabled extension compatibility code stays unloaded, and hibernation leaves one guest in every run. Real filter fetches fall from 45 to 15 per cold startup. For 150 ordinary body mutations, hints-state IPC falls from 150 messages to zero; actual root-layer open/close now produces the two required transitions instead of being missed. PSS is not GPU device-memory usage, and point samples depend on garbage collection and allocator retention; the separately sampled startup peak is recorded next.

The practical result is lower idle CPU and cheaper Preferences/switching, with no added steady browser process or idle polling. The cached series uses about 31 MiB less PSS with one tab and 66 MiB less with six. The cold/default-GPU series is not zero additional resident memory at every instant: its one-tab idle median is **7.0 MiB higher** (275.5 versus 268.5 MiB). Six-tab PSS is lower and post-hibernation PSS is equivalent within overlapping ranges. The separately sampled cold-start peak is about **150 MiB lower**. The temporary compiler's whole-process memory is included in those figures, and its worker has exited before the idle sample. No forced garbage collection is used to hide allocator/GC retention.

Tab creation is effectively comparable in this fixture: cached medians are 4.2 versus 4.3 ms, and GPU medians 4.2 versus 4.1 ms, with overlapping ranges. The latter 0.1 ms cost is reported rather than claimed as an improvement. This is a practical browsing/low-resource improvement, not a proof of zero allocation or a guarantee for small-RAM machines. The remaining cold-idle difference is explicit so adoption does not depend on such a claim.

## Final cold-start and peak-memory check

The final application source at 3fe47bb has SHA-256 `818f89c00b149a7ceffd4a1266d0af1533e1959ac038458bc1ee08eb3f0e712e`; the pinned main tree has SHA-256 `6e1ea2386b18b9c7c29fd4ffc77f2f3c9d1ee43f92718ae6963bf2c2f02daff8`. The report's final measurements use those same application sources and Electron 44.7.0. Earlier experiments below have different hashes and are retained as historical evidence.

Five alternating cold-start pairs use clean profiles, default GPU settings and real filter downloads on the physical X11 display. This probe additionally records when the guest session receives its real blocker and samples process-tree PSS every 100 ms from the Electron connection until that point.

| Metric, median [range] | Main 3be7f96 | Optimised personal |
| --- | ---: | ---: |
| Local page ready, ms | 416.2 [411.0–431.9] | 388.6 [376.2–404.9] |
| Page ready and guest filter attached, ms | 1707.7 [1263.2–1758.0] | 1100.2 [1071.0–1109.1] |
| Sampled startup peak PSS, MiB | 580.3 [505.2–626.3] | 430.5 [427.1–452.6] |
| Filter-list fetches | 45 in every run | 15 in every run |

[All five cold-start pairs](/benchmarks/2026-10-14-startup.json). Visible readiness is at parity or earlier within overlapping ranges; filter attachment is earlier in every pair and the sampled peak is lower. Sampling is a lower bound on the true allocation peak, excludes GPU device memory and does not simulate a low-RAM/OOM condition. “Filter attached” means the guest session is registered with the real blocker, not proof that its initial requests were filtered; upstream also initialises filtering asynchronously.

The previous integrated tree exposed a cold-start regression in two of three GPU runs: local readiness reached 1119 and 1187 ms. A [separate instrumented diagnosis](/benchmarks/2026-10-14-sync-startup-diagnostic.json) measured a 584 ms synchronous keyboard-table wait while Ghostery compiled downloaded lists on the main thread. This was corrected rather than accepted as parity: compilation now uses one temporary worker, with the exact same preset/resources recipe and existing fetch retries. It exits before the main process deserialises and attaches the shared engine. The transferred binary is reused for the cache file, avoiding a second 6.6 MiB serialisation/allocation. A valid cache bypasses the worker. Initial custom bindings remain synchronously available, so the repair does not trade first-use keyboard correctness for a faster first paint.

The [pre-worker cached](/benchmarks/2026-10-14-integrated-before-worker-cached.json) and [pre-worker GPU](/benchmarks/2026-10-14-integrated-before-worker-gpu.json) results are preserved to show the regression. They are not substituted for the final runtime measurements.

## Historical pre-integration comparison with remote main 2f8ca5f

The optimised tree before merging the newer tab-navigation changes is compared directly with that remote main, using three fresh profiles per revision and the same workload. The identical-cache/software-rendering series gives the following medians and per-run ranges:

| Metric | Current main 2f8ca5f | Optimised personal |
| --- | ---: | ---: |
| Local page ready, ms | 643.8 [631.6–643.9] | 626.0 [596.1–647.4] |
| One-tab PSS, MiB | 340.6 [339.3–341.2] | 313.5 [312.9–316.3] |
| Six-tab PSS, MiB | 501.9 [500.9–513.0] | 448.1 [438.3–449.8] |
| PSS after inactive-tab hibernation, MiB | 371.3 [362.3–378.2] | 336.3 [328.6–338.0] |
| Idle CPU over 5 seconds, % of one core | 0.596 [0.595–0.992] | 0.595 [0.595–0.794] |
| Preferences open/layout, ms | 4.10 [3.95–4.10] | 1.90 [1.90–1.90] |
| Preferences at 6× CPU throttle, ms | 20.60 [20.45–21.10] | 9.95 [9.15–10.20] |
| Tab creation/layout, ms | 4.3 [4.2–4.4] | 4.2 [4.2–4.3] |
| Tab switching/layout, ms | 0.60 [0.60–0.60] | 0.50 [0.50–0.50] |
| Tab switching at 6× CPU throttle, ms | 3.00 [2.95–3.00] | 2.60 [2.55–2.75] |

[Current-main cached samples](/benchmarks/2026-10-14-current-cached.json). Median PSS is about 27 MiB lower in this scenario; creation is equivalent, switching is cheaper and Preferences takes less than half the synchronous work. Startup ranges overlap, so the smaller ready-time median is not a universal speedup claim. Process counts and hibernation behaviour remain equivalent. These historical samples precede the final integration; the original common-base measurements remain below for attribution.

## Integration-base comparison with identical cached filters

Three fresh-profile samples per revision, Electron 44.7.0, software rendering and a 5-second idle window. Values are medians; brackets show the minimum and maximum per-run value. Interaction rows use each run's median, then the median across runs. This historical series includes the clock and status-badge optimisations, before the subsequent upstream integration and focus/dispatch repairs.

| Metric | Upstream fa77f782 | personal before audit | Optimised personal |
| --- | ---: | ---: | ---: |
| Local page ready, ms | 663.3 [647.7–696.5] | 694.3 [660.5–710.0] | 604.8 [583.2–627.9] |
| One-tab PSS, MiB | 338.4 [329.1–346.0] | 349.7 [346.7–351.9] | 310.2 [308.4–312.0] |
| Six-tab PSS, MiB | 501.8 [488.9–504.5] | 480.3 [477.9–484.1] | 441.5 [433.5–446.7] |
| PSS after inactive-tab hibernation, MiB | 359.2 [359.1–381.3] | 349.3 [333.9–350.3] | 327.1 [326.1–336.9] |
| Idle CPU, % of one core | 0.99 [0.60–1.19] | 0.79 [0.40–0.99] | 0.79 [0.20–0.79] |
| Preferences open/layout, ms | 4.1 [4.1–4.1] | 2.1 [2.0–2.1] | 2.0 [2.0–2.1] |
| Preferences at 6× CPU throttle, ms | 20.8 [20.4–21.2] | 9.95 [9.85–10.05] | 9.90 [9.80–10.05] |
| Tab creation/layout, ms | 4.1 [4.0–4.2] | 4.4 [4.3–4.4] | 4.1 [4.0–4.2] |
| Tab switching/layout, ms | 0.60 [0.60–0.60] | 0.60 [0.60–0.60] | 0.50 [0.50–0.50] |
| Tab switching at 6× CPU throttle, ms | 3.05 [3.05–3.10] | 3.50 [3.40–3.60] | 2.75 [2.70–2.90] |
| Host JS heap, MiB | 4.59 [4.06–4.60] | 5.53 [4.95–5.56] | 5.13 [4.79–5.13] |
| Initial host DOM counter | 2,190 | 7,129–7,133 | 2,279–2,280 |
| Initial host listener counter | 215 | 1,045 | 259 |
| Process counts: one tab / six tabs / hibernated | 7 / 12 / 7 | 7 / 12 / 7 | 7 / 12 / 7 |

[Raw cached-filter samples, source hashes and environment](/benchmarks/2026-10-14-cached.json). The shared cache SHA-256 is `e98d2e8f5abe49a0b80e10c05afad66bc0de312158ecdff1564d190df4e7bc26`.

The pre-audit fork had a measurable startup/static-memory cost despite its faster Preferences. The optimised pre-integration tree removes that regression in this experiment: one-tab PSS is about 28 MiB lower than upstream, local readiness about 58 ms earlier, Preferences takes about half the synchronous work, creation is equivalent and switching is cheaper in this fixture. Hibernation still removes five inactive views. The small static catalogue/handler cost remains visible in host heap (about 0.54 MiB above upstream) and roughly 90 additional initial DOM nodes; the full process-tree footprint is lower. This meets the practical parity target in the measured workloads without claiming literal zero additional bytes. Short idle CPU samples are noisy; the longer GPU-default series below is the stronger idle check.

## Integration-base comparison with default GPU and an empty filter cache

Three fresh profiles per revision, GPU compositing confirmed enabled by Electron, real filter downloads and a 30-second idle window. The source hashes match the historical integration-base cached series above. Medians and per-run ranges use the same definitions.

| Metric | Upstream fa77f782 | Optimised personal |
| --- | ---: | ---: |
| Local page ready, ms | 422.5 [401.1–434.4] | 432.2 [410.0–994.2] |
| One-tab PSS, MiB | 254.8 [253.7–257.7] | 254.9 [253.5–256.6] |
| Idle CPU, % of one core | 0.698 [0.599–0.732] | 0.499 [0.432–0.499] |
| Preferences open/layout, ms | 3.25 [3.15–3.30] | 1.50 [1.50–1.50] |
| Preferences at 6× CPU throttle, ms | 16.75 [15.85–17.10] | 6.85 [6.70–6.95] |
| Tab creation/layout, ms | 4.2 [4.1–4.3] | 4.1 [4.1–4.2] |
| Tab switching/layout, ms | 0.40 [0.40–0.50] | 0.30 [0.30–0.35] |
| Tab switching at 6× CPU throttle, ms | 2.00 [1.90–2.15] | 1.60 [1.60–1.70] |
| Six-tab PSS, MiB | 377.2 [367.1–411.1] | 374.3 [362.7–376.0] |
| PSS after inactive-tab hibernation, MiB | 292.1 [291.7–339.4] | 286.6 [284.6–287.9] |
| Filter fetches per clean startup | 45 | 15 |
| IPC messages for 150 body mutations | 150 | 0 |
| IPC messages for hint-layer open/close at the root | 0 | 2 |

[Raw GPU-default samples, source hashes and environment](/benchmarks/2026-10-14-gpu.json).

Default-GPU memory is effectively equivalent: the 0.13 MiB median difference is within the overlapping ranges. Idle CPU is lower in all three samples in this historical series, and the interaction costs improve. Cold page-ready time is not a demonstrated improvement: its median is 9.7 ms later and there is one 994.2 ms outlier, retained in the data. These small, network-dependent cold samples do not establish a repeatable startup regression; the identical-cache experiment is the stronger controlled startup comparison. They also cannot prove universal zero overhead. No extra application process or disabled-extension runtime appeared, and all hibernation assertions passed.

## Findings and corrections

| Area | Before this audit | Correction and remaining cost |
| --- | --- | --- |
| Packaged runtime, fork defect | The explicit package file list omitted the local extensions.js module required by main.js, even with extensions disabled. | Include the required module and verify the actual Linux package archive and a disposable-profile packaged launch. The source runtime and its measured defaults are unchanged. |
| Keyboard editor, fork regression | Constructed all action rows and both binding menus at startup, including controls hidden inside closed menus. The host retained about 7,129 DOM nodes and 1,045 listeners, compared with upstream's 2,190 and 215. | Build rows when Keyboard is shown, and binding controls when their menu expands. Remove controls on collapse and rows when leaving Keyboard or closing Preferences. Keep the small draft/catalogue in memory so unsaved edits and settings search still work. Initial host DOM returns to about 2,280 nodes and 259 listeners. |
| Filter engine, inherited defect | Several sessions could enter the asynchronous cold-cache build before an instance existed. Three builds fetched the same lists, retained different engines through their session listeners and wrote the same cache. The initial shared exception rules were also reapplied per session. | All sessions await one pending initialisation, which installs shared exceptions once and then attaches protection separately to each session. A failed build can be retried; a corrupt cache is rebuilt. No protection or exception rule is removed. The clean-profile fixture makes 15 filter fetches instead of 45. |
| Status badges, inherited inefficiency | Rebuilt container and connection badge children on every tab refresh, including unchanged values. A small switch-time cost remained in the fork. | Retain unchanged badge nodes using two small keys; still update on URL, workspace and private-profile transitions. No new timer or observer. |
| Clock ticks, inherited inefficiency | Replaced date and minute text nodes every second even when the text had not changed, including hidden clocks. | Only change labels and date visibility when the displayed value changes. Disabled clocks skip DOM updates and refresh immediately on re-enabling. Seconds mode and live tooltips are preserved. |
| Hints state, inherited defect | An observer on `body` sent IPC on every direct child mutation even when hints were absent. The actual hinter inserts its layer under `document.documentElement`, so opening and closing hints was not reliably reported. | Observe only the root's direct children and send only actual state changes. In the fixture, 150 body mutations produce zero messages instead of 150, while adding/removing the real layer location produces exactly two state messages. No subtree observer or polling is added. |

Ledger: [PERF-01 through PERF-07](./fork-fixes#perf-01), [local dispatch and palette focus](./fork-fixes#perf-08), [upstream integration](./fork-fixes#sync-03), [input recovery](./fork-fixes#kb-27), [hint resize handling](./fork-fixes#kb-28) and [sharing cancellation](./fork-fixes#kb-29).

## Cost map of the fork

| Fork work | Cost model and low-resource assessment |
| --- | --- |
| Shared keyboard grammar and custom bindings | Maps are compiled when loading or changing bindings; dispatch uses map lookup. A new guest document obtains its binding configuration once by synchronous IPC. There is no per-keystroke disk read or configuration IPC. The editor's data is retained, its hidden controls are not. Defaults and generated guest grammar remain drift-checked. |
| Sectioned Preferences and search | Only selected sections participate in layout. Matching and highlighting run in response to search input; there is no periodic index rebuild. Extensions are scanned and process metrics read only when their section is requested. Search still sees keyboard actions before the editor has been built. |
| Optional extensions | Disabled by default. Runtime entry points return early; the compatibility package is required only when an extension loads. No extension controller, extension worker, scan, grace timer or popup measurement timer is created by the disabled path. The separate temporary cold-filter compiler is independent of extensions and exits before filtering attaches. The benchmark checks that the compatibility package stays unloaded. Enabling third-party extensions has an explicit, workload-dependent cost and is outside the parity claim. |
| Focus, overlays and sidebar | Event-driven handlers. Focus recovery and pointer actions can schedule a single deferred operation; these are not idle loops. The action history for reopening tabs is bounded at 20 entries. More HTML, CSS and handler code has a small static cost, reported rather than described as zero bytes. |
| Tabs, workspaces and hibernation | The existing inactive-tab policy and process limit are retained. The benchmark creates six local tabs, switches them, then hibernates inactive views and asserts that one view remains. Pinned, audible and split-view tabs retain their existing exemptions. |
| Themes, transparency, Dark Reader and diagnostics | The measurements use opaque clean defaults; the owner's personal preferences are never loaded. Transparency and injected page styling can change rendering cost when enabled. Diagnostics remains disabled by default. Existing clock and memory-management timer schedules come from upstream; the clock now avoids redundant DOM writes. |
| Audit tooling | Playwright, process sampling, IPC counters and CPU throttling belong only to the optional test launcher. Normal application launches load none of this instrumentation. No runtime dependency or telemetry is added. |

## Method

The runner is `scripts/benchmark-performance.js`. Each sample creates a new temporary home and Electron user-data directory and removes them after exit. It writes only a one-tab local fixture session and the already-seen application version; every performance, theme, opacity and extension preference uses that revision's defaults. It does not read or reuse the owner's profile. Pages are served from a deterministic local HTTP server. Chromium's non-local page traffic is sent to a closed local proxy; Node's filter-list fetches remain real in the empty-cache scenario.

Source directories use the same installed dependencies and Electron binary. No browser versions or engine switches differ between compared revisions within a scenario. Final tests and measurements run sequentially on the physical Linux/X11 display in i3 workspace 4. Benchmark windows float at 1600 × 1000 outer pixels, yielding a recorded 1596 × 996 viewport on the 1920 × 1080 monitor. This prevents tiling with the owner's applications from changing the workload. GPU feature status and the final default-GPU series' NVIDIA device/driver metadata are recorded in the raw data. No virtual-display measurement is used in the final comparison. Launches alternate source order on each round to reduce order/cache bias. These are fresh profiles with a warm operating-system file cache, not physical cold-boot tests.

The host is an AMD Ryzen 7 5800X with 32 GiB RAM. This is not low-end hardware. The software-rendering scenario and Chromium's 6× host-renderer CPU throttle are proxies for limited resources; the throttle does not constrain the main process, guests, GPU, RAM capacity or memory bandwidth. A separate scenario retains the default GPU setting. The cold filter compiler is a temporary thread, not an additional browser process; its whole-process memory is included in startup PSS sampling. Actual small-RAM/ARM/fanless hardware, memory pressure, battery consumption, Wayland and other operating systems remain unverified.

Process memory is Linux PSS summed over the application process tree, including zygotes and utility processes. Shared pages are apportioned rather than counted repeatedly as RSS would do. These are point-in-time footprints, not allocation peaks or memory-pressure/swap tests; PSS does not include all GPU device memory. Host JS heap and DOM/listener counts come from CDP; detached documents and nodes remain in those counters until garbage collection. Process CPU comes from `/proc` ticks during the idle window, expressed as a percentage of one CPU core; 10 ms tick quantisation and other desktop activity limit interpretation. Startup ends when the local guest fixture and host tab list are ready, rather than when all remote work has completed. PSS/idle sampling follows filter-cache creation and a settling interval.

Preferences timings cover synchronous toggle plus forced layout, with the initial open included. Tab creation covers host creation plus layout, not full guest navigation. Switching covers synchronous host work plus layout, not presentation latency. The same definitions are used for every source. Raw sample arrays, source hashes, engine version and process breakdowns accompany the report; medians and ranges should be read together. An isolated timing difference below the clock/layout noise is not evidence of a regression or an improvement.

## Reproduction

Use separate exported source trees for upstream and the pre-audit fork, each pointing at the same installed `node_modules`. Make Playwright available through `MISE_PLAYWRIGHT_MODULE`; it is an optional external test tool, not a new production dependency. An X11 display and Linux `/proc` are required. The launcher uses the existing desktop fixtures' sandbox switch; sandbox hardening is covered separately by the security tests.

```sh
git fetch origin main
mkdir -p /tmp/mise-upstream /tmp/mise-before
git archive 2f8ca5f | tar -x -C /tmp/mise-upstream
git archive d4014bdb | tar -x -C /tmp/mise-before
ln -s "$PWD/node_modules" /tmp/mise-upstream/node_modules
ln -s "$PWD/node_modules" /tmp/mise-before/node_modules
MISE_PLAYWRIGHT_MODULE=/path/to/playwright MISE_BENCH_ROUNDS=3 \
  MISE_BENCH_GPU=1 MISE_BENCH_IDLE_MS=15000 MISE_BENCH_OUTPUT=/tmp/mise-cold.json \
  node scripts/benchmark-performance.js /tmp/mise-upstream .
```

For the controlled cached-filter comparison, generate one filter cache with the same installed library, then provide it to every otherwise fresh profile. This removes remote download timing and corpus variation without removing the real blocking engine:

```sh
node -e "require('@ghostery/adblocker-electron').ElectronBlocker.fromPrebuiltAdsAndTracking().then(b=>require('fs').writeFileSync('/tmp/mise-filters.bin',b.serialize()))"
MISE_PLAYWRIGHT_MODULE=/path/to/playwright MISE_BENCH_ROUNDS=3 \
  MISE_BENCH_CACHE=/tmp/mise-filters.bin MISE_BENCH_OUTPUT=/tmp/mise-cached.json \
  node scripts/benchmark-performance.js /tmp/mise-upstream /tmp/mise-before .
```

Omitting `MISE_BENCH_GPU=1` selects the common software-rendering scenario. `MISE_BENCH_IDLE_MS` changes the idle window (default 5,000 ms). Do not run multiple desktop suites or benchmarks on the same display at the same time.

## Validation and excluded runs

The benchmark asserts the same single initial tab/view after settling, unloaded extension compatibility code, no renderer exceptions and one remaining view after hibernation. Unit coverage exercises shared filter initialisation with failures and corrupt caches, clock changes and unchanged labels, hints transitions, status badges, keyboard defaults and generated-file drift. Disposable-profile Electron coverage exercises the recorder, editable pages, hints, overlays, sidebar controls, Preferences search, unsaved editor state across disposal/rebuild and immediate typing after closing a tab.

Early exploratory probes used a loose bootstrap file and could open it and release notes as additional tabs; readiness timed out or sampled a different workload. Those measurements are excluded from the published datasets. An interrupted virtual-display attempt and desktop runs affected by competing windows or accidental interaction are also excluded from the final acceptance result. The corrected launcher is shared by the desktop suites and checked for application version and a single startup tab. Earlier desktop runs failed or stalled at hint activation, input-event acknowledgement during self-closing guests, palette-to-Preferences cancellation and one dashboard-rename focus check. The corresponding code/readiness repairs and their limits are recorded in the ledger. The final complete sequential desktop command passed on the physical display; earlier failures are not counted as passes or proof that every platform's focus races are eliminated.

The comparison covers source launches with a shared engine/dependency installation. A Linux directory build and disposable packaged launch additionally check that required local modules are present, including the previously omitted extensions.js and both cold-filter compiler modules. The packaged test waits for a real cold cache to be created. Packaging verification is a functional smoke check, not another performance dataset.

To repeat the packaged functional check after a Linux directory build:

```sh
MISE_PLAYWRIGHT_MODULE=/path/to/playwright \
  node tests/packaged-ui.test.js /path/to/linux-unpacked/mise-browser
```
