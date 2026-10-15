# Fly vs Chromium resource use

Measured on 15 October 2026 against `main` at `c199317` (the latest release). Fly runs on Electron 44.7.2, which embeds Chromium 152.0.7977.149. It is compared with the distribution's Chromium 153.0.8010.52 (Arch Linux package) and, as a reference, Google's Chrome for Testing 153.0.8010.12.

The script is [`scripts/benchmark-vs-chromium.js`](https://github.com/Arzuparreta/fly-browser/blob/main/scripts/benchmark-vs-chromium.js). Raw samples: [run 1](/benchmarks/2026-10-15-vs-chromium-run1.json) (5 rounds), [run 2](/benchmarks/2026-10-15-vs-chromium-run2.json) (3 rounds, adds Chrome for Testing) and [run 3](/benchmarks/2026-10-15-vs-chromium-no-cnn.json) (3 rounds, browsing without CNN). Values are medians with [min–max] across rounds. Configurations within a round run back to back, in alternating order.

## Summary

- **With Fly's default settings there is a serious problem on CNN.** Ghostery blocks a request that CNN's video player retries immediately, about 3,700 times a second, for as long as the tab is open. With CNN in the ten-site set, the open tabs keep **1.1 CPU cores busy at idle** (111.5 % of one core, against 2.5 % for Chromium) and use **2.87 GiB instead of 1.5 GiB**. It reproduces in every round, also without DevTools attached. See [Findings](#findings).
- **Without that page, default Fly is lighter than Chromium and than Fly without its blocker** (run 3, nine sites): 12 % less memory than Chromium with nine sites open (1182 vs 1346 MiB), 33 % less CPU while browsing (15.6 vs 23.2 s) and lower idle CPU. Built-in blocking pays for itself.
- **With one tab, Fly uses less memory than Chromium** (262 vs 285 MiB PSS after a warm start, 8 processes vs 12) and similar idle CPU.
- **Fly shows its first page later**: 613 ms after a warm start against 237 ms for Chromium. About 230 ms of that is loading the built-in blocker's filters. Without the blocker Fly takes 380 ms.
- **Extensions on (bundled uBlock Origin) cost about 170 MiB and a few percent of CPU for roughly a minute after each start.** While browsing the ten sites it was the cheapest configuration measured (1462 MiB, 24.8 s of CPU).
- **Chromium with uBlock Origin Lite and Fly with uBlock Origin end up level with ten tabs open** (1480 vs 1462 MiB, 26.8 vs 24.8 s CPU). With one tab, uBlock Origin adds about 170 MiB to Fly and uBO Lite about 35 MiB to Chromium. Chromium 153 can no longer run the full Manifest V2 uBlock Origin.
- **JavaScript speed is the same engine's**: Fly 32.6 in Speedometer 3.1, Chrome for Testing 31.3. Arch's Chromium scores 22.4 because the package is built without profile-guided optimisation, not because of anything Fly does.
- **Video playback costs the same** (26.6 % vs 27.5 % of one core, no dropped frames), and Fly's profile on disk is a third of Chromium's after browsing.

## Setup

| | |
| --- | --- |
| Machine | AMD Ryzen 7 5800X (16 threads), 31 GiB RAM, NVIDIA RTX 3060 (driver 615.78.08), Linux 7.2.9-cachyos |
| Display | X11 `:0`, i3, physical monitor; GPU compositing and rasterisation enabled in every browser |
| Window | Requested at 1600×1040; i3 tiled both to the screen. Page viewport: Chromium 1916×965, Fly 1880×1030 |
| Network | Real sites over the same home connection; the local pages come from a server on 127.0.0.1 |
| Other load | The owner's desktop session (including another Electron app) stayed open. Each measurement alternates configurations to spread that noise evenly |

Each launch starts a fresh profile in its own systemd user scope (`systemd-run --user --scope`). CPU is the scope's `cpu.stat` (exact, including processes that already exited). Memory is the PSS sum over every process in the scope, which counts shared pages once; the cgroup's anonymous memory is reported next to it. Both browsers are started as normal processes with `--remote-debugging-port` and driven over the DevTools protocol. Fly's profile lives under a temporary home through the same wrapper `benchmark-performance.js` uses.

### Configurations

| Name | What it is |
| --- | --- |
| `chromium` | Arch Chromium, clean profile. Only `--no-first-run`, `--no-default-browser-check` and `--password-store=basic` are added |
| `chromium-ubol` | The same with uBlock Origin Lite 2026.1012.1545 (Manifest V3) loaded. Chromium 153 refuses Manifest V2, so the full uBlock Origin that Fly bundles cannot be loaded there (checked) |
| `chromium-limit4` | Diagnostic: Chromium with `--renderer-process-limit=4`, the limit Fly sets by default (run 1 only) |
| `cft`, `cft-ubol` | Chrome for Testing 153, Google's official PGO build, without and with uBO Lite (run 2 only) |
| `fly` | Fly's defaults: built-in Ghostery blocker, extensions off |
| `fly-ext` | `extensions_enabled: true`: the bundled uBlock Origin 1.75.0 loads, and Ghostery steps aside in that session |
| `fly-noblock` | Diagnostic, not a user setting: Fly with the built-in blocker disabled, to separate the shell's cost from the blocker's |

### Scenarios

1. **Cold start**: empty profile, one local page. Time from launch until the page has loaded, CPU spent, sampled peak PSS, PSS after 15 s, then idle CPU over 30 s.
2. **Warm start**: the same profile launched again, measured the same way.
3. **Browsing**: in that warm session, ten sites are opened one per tab, the way a user would (a blank tab, then the address): Wikipedia, The Verge, CNN, Reddit, YouTube, GitHub, BBC News, Marca, El País, Stack Overflow. Each tab waits for its load event (at most 30 s) plus 3 s. Every request is counted, including cross-site frames and requests a blocker cancels. Then 20 s to settle, memory, 30 s of idle CPU, Fly's "hibernate inactive tabs", and finally closing all tabs but one.
4. **Video**: a local 1080p60 VP9 file, opened 15 s after start-up, measured over 20 s of playback.
5. **Speedometer 3.1**: the full benchmark from browserbench.org, ten iterations.

## Results

Run 2 unless noted. `chromium-ubol` and `chromium-limit4` come from run 1, whose shared configurations agree with run 2 within a few percent (full tables below).

### Start-up and one idle tab

| Metric | chromium | fly | fly-ext | fly-noblock | chromium-ubol (run 1) | cft |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Cold: launch to page loaded, ms | 247 | 419 | 537 | 398 | 641 | 286 |
| Cold: sampled peak PSS, MiB | 338 | 299 | 622 | 300 | 374 | 407 |
| Cold: PSS after 15 s, MiB | 335 | 297 | 481 | 298 | 370 | 407 |
| Cold: idle CPU, % of one core | 0.45 | 0.29 | 2.08 | 0.29 | 0.48 | 0.53 |
| Warm: launch to page loaded, ms | 237 | 613 | 708 | 380 | 631 | 299 |
| Warm: CPU until loaded, s | 0.53 | 1.20 | 1.36 | 0.63 | 1.00 | 0.64 |
| Warm: sampled peak PSS, MiB | 287 | 387 | 459 | 266 | 324 | 390 |
| Warm: PSS after 15 s, MiB | 285 | 262 | 429 | 257 | 321 | 389 |
| Warm: anonymous memory, MiB | 163 | 250 | 404 | 140 | 198 | 246 |
| Processes | 12 | 8 | 9 | 8 | 13 | 15 |
| Warm: idle CPU, % of one core | 0.11 | 0.14 | 3.01 | 0.13 | 0.17 | 0.33 |

- Fly's one-tab footprint is below Chromium's. The difference is in the process tree: Fly runs no separate storage service or crash handler, and its interface renderer (about 31 MiB) takes the place of Chromium's browser-UI renderer.
- The built-in blocker adds about 230 ms and 0.57 s of CPU to a warm start (613 vs 380 ms) and a transient peak of about 120 MiB while the filters load. PSS settles at the same level 15 s later, but the cgroup still holds about 110 MiB more anonymous memory than without the blocker. That gap is not visible in any process's PSS, so its owner is not established here.
- With extensions on, uBlock Origin's background page costs about 170 MiB and keeps 2–4 % of a core busy between 15 and 45 s after every start (2.9 s of CPU in the first 15 s of a cold start). The idle CPU is attributed to the extension renderer.
- uBO Lite in Chromium delays the first page by about 400 ms on every start.

### Browsing ten real sites

| Metric | chromium | fly | fly-ext | fly-noblock | chromium-ubol (run 1) | cft |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU for the whole browse, s | 31.9 | **75.2** | 24.8 | 25.2 | 26.8 | 29.3 |
| Median page load, ms | 1112 | 791 | 885 | 915 | 993 | 1094 |
| Requests started | 1424 | **220,717** | 1257 | 1495 | 1255 | 1498 |
| Requests blocked | 0 | **219,507** | 64 | 0 | 72 | 0 |
| Data received, MiB | 44.0 | 43.5 | 40.9 | 46.6 | 38.7 | 45.5 |
| Peak PSS while browsing, MiB | 1886 | **2887** | 1948 | 1843 | 1840 | 2266 |
| PSS with all tabs open, MiB | 1557 | **2865** | 1462 | 1506 | 1480 | 1808 |
| Processes | 33 | 23 | 24 | 28 | 29 | 37 |
| Idle CPU with all tabs open, % of one core | 2.52 | **111.51** | 1.50 | 1.89 | 2.21 | 3.51 |
| PSS after Fly hibernates inactive tabs, MiB | – | 422 | 482 | 441 | – | – |
| PSS 10 s after closing all but one tab, MiB | 526 | 380 | 436 | 381 | 392 | 1368 |
| Profile on disk afterwards, MiB | 184 | 62 | 128 | 61 | 226 | 94 |

Bold values are caused by the CNN loop described below. Run 3 repeats the browse without CNN (nine sites, three rounds):

| Metric (nine sites, no CNN) | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| CPU for the whole browse, s | 23.2 | 15.6 | 18.5 |
| Median page load, ms | 1014 | 794 | 849 |
| Requests started / blocked | 1295 / 0 | 1171 / 53 | 1343 / 0 |
| Data received, MiB | 25.6 | 23.5 | 27.6 |
| Peak PSS while browsing, MiB | 1677 | 1577 | 1740 |
| PSS with all tabs open, MiB | 1346 | 1182 | 1299 |
| Processes | 29 | 20 | 23 |
| Idle CPU with all tabs open, % of one core | 2.04 | 1.28 | 1.99 |
| PSS 10 s after closing all but one tab, MiB | 521 | 382 | 360 |
| Profile on disk afterwards, MiB | 165 | 44 | 42 |

- Without CNN, default Fly blocks a similar number of requests to uBlock Origin and is the cheapest configuration measured.
- Page loads are faster in every Fly configuration, including without a blocker. The viewports and enabled features differ, so this is not attributed to the engine.
- Fly needs fewer processes for the same tabs (23–28 against 33). `--renderer-process-limit=4` changes nothing in Chromium (32 processes, same memory) because site isolation overrides it; Fly's lower count is not explained by that switch.
- After closing nine tabs Fly returns to 380 MiB and Chromium to 526 MiB. Chrome for Testing kept 25 processes and 1.37 GiB after the tabs were closed through DevTools; that was not investigated further.
- Hibernation brings ten open tabs down to about 420–480 MiB in Fly. Chromium has no equivalent on demand.

### Video and JavaScript speed

| Metric | chromium | fly | fly-ext | fly-noblock | chromium-ubol (run 1) | cft | cft-ubol |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1080p60 VP9, CPU % of one core | 27.5 | 26.6 | 27.1 | 26.7 | 27.6 | 27.4 | 27.5 |
| Frames dropped in 20 s | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| PSS while playing, MiB | 409 | 388 | 452 | 388 | 421 | 463 | 482 |
| Speedometer 3.1 | 22.4 | 32.6 | 30.6 | 32.6 | 22.4 | 31.3 | 31.5 |

- The Speedometer gap against Arch's Chromium is a build difference. The Arch package compiles with the system clang and sets `chrome_pgo_phase=0` ([PKGBUILD for 153.0.8010.52-1](https://gitlab.archlinux.org/archlinux/packaging/packages/chromium/-/blob/153.0.8010.52-1/PKGBUILD)), while Electron and Chrome for Testing are official builds with profile-guided optimisation. Against Chrome for Testing, Fly is level (32.6 vs 31.3). Other Chromium packages may be built differently.
- uBlock Origin lowers Fly's Speedometer score by about 6 % (30.6 vs 32.6); uBO Lite has no measurable effect in Chromium.
- Video CPU is the same in every browser, which suggests they all decode on the CPU here; hardware decoding was not checked separately.

### Install size

| | Size |
| --- | ---: |
| Fly release `tar.gz`, unpacked | 310.7 MiB (Electron binary 218 MiB, `app.asar` 13.2 MiB) |
| Fly AppImage download | 120.4 MiB |
| Arch `chromium` package, installed | 422.2 MiB (uses system ICU, ffmpeg and other libraries) |
| Chrome for Testing 153, unpacked | 390 MiB |

## Findings

1. **CNN's video player loops against Ghostery (default Fly).** The player (`registry.api.cnn.io/bundles/fave/boltPlayer-…`) requests `https://cdn-media.brightline.tv/config/v3/1018.json` with XHR. Fly's built-in filter set blocks it, and the player retries immediately: 55,783 requests in 15 s with CNN in the foreground, about 190 % of one core, and about 110 % of one core with the tab in the background. It happened in all 8 rounds of default Fly that included CNN, and again in a separate run without DevTools attached to the network. uBlock Origin and uBO Lite do not block that URL, so they show no loop. Possible fixes: an exception for that config file, a redirect to an empty JSON resource so the player stops retrying, or a guard against the same blocked request repeating many times a second. Not fixed in this pull request.
2. **The first start after enabling extensions can break media in tabs restored at launch.** On that first start, while uBlock Origin is still setting up, a `<video>` in a restored tab fails (`MEDIA_ERR_SRC_NOT_SUPPORTED`, the request ends in `net::ERR_ABORTED` and never reaches the server). Reloading fixes it, and the second and later starts are unaffected. In Chromium, uBlock Origin reloads such tabs once it is ready; in Fly that reload apparently does not happen. The video scenario therefore opens the video after start-up.
3. **The built-in blocker delays the first page of every start by about 230 ms.** Deserialising the 6.6 MiB filter cache sits on the path to the first page (warm start 613 ms, 380 ms without blocker, Chromium 237 ms).
4. **Extension background pages run without `--enable-sandbox`.** Fly's uBlock Origin renderer is the only Fly renderer started without that switch. This was only observed in the command line and not investigated further.

## What these numbers do not show

- One machine, one display setup and ten real sites on one morning. The sites change between visits; the rounds alternate configurations to spread that out, but they are not a fixed corpus.
- PSS is not GPU memory. The startup and browsing peaks are sampled every 250–500 ms and are lower bounds.
- Requests are counted through DevTools; an attached session adds a small cost to every browser equally. The CNN loop was confirmed without it.
- Fly's tabs are `<webview>`s inside one window and Chromium's are real tabs, so tab opening and closing go through different code paths. Closing tabs in Chromium uses DevTools `Target.closeTarget`.
- Chromium's first-run UI is skipped with `--no-first-run`. Fly's first-run behaviour is left as it is.
- Run 1 recorded Fly's extension targets incorrectly in the `extensions` field (the background page is listed as a plain page); the process breakdown shows the extension renderer, and run 2 records it correctly. No measurement depends on that field.

## Reproducing

```sh
# uBlock Origin Lite for the -ubol configurations, unpacked
gh release download --repo uBlockOrigin/uBOL-home -p '*.chromium.zip' && unzip uBOLite_*.chromium.zip -d ubol
# a test video
ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=60 -t 30 -c:v libvpx-vp9 -deadline realtime -cpu-used 8 -b:v 6M -row-mt 1 video.webm

FLY_VS_UBOL=$PWD/ubol FLY_VS_VIDEO=$PWD/video.webm FLY_VS_ROUNDS=5 FLY_VS_OUTPUT=run1.json \
  node scripts/benchmark-vs-chromium.js
FLY_VS_CFT=/path/to/chrome-linux64/chrome FLY_VS_UBOL=$PWD/ubol FLY_VS_VIDEO=$PWD/video.webm \
  FLY_VS_CONFIGS=chromium,cft,cft-ubol,fly,fly-ext,fly-noblock FLY_VS_ROUNDS=3 FLY_VS_OUTPUT=run2.json \
  node scripts/benchmark-vs-chromium.js
node scripts/benchmark-vs-chromium.js --report run1.json
```

Runs take about four hours (run 1) and two and a half hours (run 2). Windows open on the current display while they run.

## Full tables

::: details Run 1: five rounds, Arch Chromium configurations

Samples: 78, failed: 0

#### Cold start, empty profile, one local tab

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Window to local page loaded, ms | 245 [239–301] | 641 [632–647] | 248 [242–252] | 418 [405–444] | 533 [526–569] | 382 [370–397] |
| CPU used until loaded, s | 0.62 [0.61–0.71] | 1.05 [1.04–1.05] | 0.63 [0.62–0.64] | 0.71 [0.67–0.72] | 1.01 [0.98–1.05] | 0.65 [0.64–0.67] |
| CPU in the 15 s after loading, s | 0.40 [0.32–0.40] | 0.53 [0.52–0.53] | 0.39 [0.38–0.40] | 0.28 [0.27–0.30] | 2.86 [2.79–2.97] | 0.29 [0.27–0.31] |
| Sampled peak PSS during start-up, MiB | 336 [333–338] | 374 [372–375] | 338 [336–338] | 301 [299–302] | 628 [613–667] | 299 [298–304] |
| PSS 15 s after loading, MiB | 334 [332–336] | 370 [368–372] | 336 [334–336] | 300 [297–301] | 480 [461–532] | 297 [296–303] |
| Processes | 13 [13–13] | 14 [14–14] | 13 [13–13] | 8 [8–8] | 9 [9–9] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.40 [0.32–0.45] | 0.48 [0.39–0.52] | 0.44 [0.40–0.45] | 0.24 [0.19–0.29] | 1.97 [1.95–2.11] | 0.27 [0.23–0.30] |
| Profile on disk after first run, MiB | 4.4 [4.4–4.4] | 50.5 [50.5–50.5] | 4.4 [4.4–4.4] | 12.6 [12.2–12.6] | 55.0 [54.5–55.0] | 6.1 [5.7–6.1] |

#### Warm start, same profile relaunched, one local tab

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Window to local page loaded, ms | 239 [228–277] | 631 [622–642] | 237 [232–258] | 621 [611–633] | 705 [694–720] | 384 [380–388] |
| CPU used until loaded, s | 0.54 [0.52–0.59] | 1.00 [0.99–1.02] | 0.54 [0.52–0.57] | 1.20 [1.19–1.23] | 1.38 [1.36–1.40] | 0.64 [0.63–0.65] |
| Sampled peak PSS during start-up, MiB | 286 [284–289] | 324 [318–357] | 286 [285–288] | 386 [260–389] | 457 [442–492] | 265 [263–267] |
| PSS 15 s after loading, MiB | 284 [282–287] | 321 [315–354] | 284 [283–285] | 259 [258–262] | 456 [434–491] | 260 [256–263] |
| Anonymous memory (cgroup), MiB | 164 [161–166] | 198 [188–229] | 162 [161–163] | 248 [231–253] | 432 [412–451] | 144 [139–147] |
| Processes | 12 [12–12] | 13 [13–13] | 12 [12–12] | 8 [8–8] | 9 [9–9] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.11 [0.11–0.12] | 0.17 [0.16–0.31] | 0.11 [0.11–0.12] | 0.15 [0.09–0.21] | 3.82 [3.27–4.46] | 0.15 [0.12–0.16] |

#### Browsing 10 real sites, one tab each

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU time for the whole browse, s | 29.4 [29.1–31.9] | 26.8 [26.6–28.2] | 29.7 [29.2–31.9] | 75.0 [74.8–75.3] | 23.6 [23.2–24.9] | 23.8 [23.6–26.1] |
| Median page load (navigate to load event), ms | 1143 [1098–1205] | 993 [956–1009] | 1143 [1115–1158] | 787 [756–850] | 800 [768–867] | 891 [880–955] |
| Pages that reached load within 30 s | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] |
| Requests started | 1415 [1405–1427] | 1255 [1237–1264] | 1419 [1416–1424] | 223195 [219424–224743] | 1242 [1234–1259] | 1506 [1497–1518] |
| Requests blocked by the client | 0 [0–0] | 72 [59–76] | 0 [0–0] | 221976 [218213–223523] | 59 [56–70] | 0 [0–0] |
| Other failed requests | 26 [20–34] | 7 [6–7] | 33 [26–34] | 25 [19–28] | 6 [6–7] | 26 [25–32] |
| Data received, MiB | 41.6 [41.3–44.4] | 38.7 [38.7–41.3] | 42.2 [41.5–44.3] | 41.6 [41.0–43.6] | 38.9 [38.5–40.9] | 43.9 [43.8–46.1] |
| Sampled peak PSS while browsing, MiB | 1863 [1813–1870] | 1840 [1820–1862] | 1856 [1788–1871] | 2847 [2832–2874] | 1829 [1799–1986] | 1842 [1804–1889] |
| PSS with all tabs open (20 s settle), MiB | 1550 [1537–1560] | 1480 [1474–1491] | 1544 [1524–1555] | 2835 [2826–2886] | 1454 [1445–1458] | 1494 [1494–1511] |
| Anonymous memory (cgroup), MiB | 1302 [1286–1311] | 1225 [1224–1244] | 1291 [1275–1299] | 2614 [2598–2663] | 1226 [1211–1233] | 1264 [1261–1277] |
| Processes | 33 [32–33] | 29 [28–29] | 32 [31–32] | 23 [22–23] | 24 [24–24] | 28 [27–28] |
| Idle CPU with all tabs open, % of one core | 2.35 [2.19–3.07] | 2.21 [1.94–2.56] | 2.35 [2.15–2.47] | 111.90 [111.77–112.94] | 1.70 [1.43–2.05] | 1.92 [1.80–2.09] |
| PSS after Fly hibernates inactive tabs, MiB | – | – | – | 418 [400–423] | 488 [482–492] | 441 [415–446] |
| PSS 10 s after closing all but one tab, MiB | 520 [503–528] | 392 [377–400] | 513 [495–515] | 382 [357–386] | 442 [439–447] | 382 [375–400] |
| Processes after closing | 14 [13–14] | 13 [12–13] | 13 [12–13] | 10 [9–10] | 10 [10–10] | 10 [9–10] |
| Profile on disk after browsing, MiB | 199.0 [180.7–200.2] | 226.0 [222.5–241.9] | 197.9 [181.6–200.7] | 61.3 [59.0–63.1] | 126.1 [122.8–131.7] | 58.8 [57.9–60.8] |

#### PSS by process type with all tabs open, MiB

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broker | 17 [17–17] | 17 [17–17] | 17 [17–17] | 15 [15–15] | 15 [15–15] | 15 [15–15] |
| crashpad | 2 [2–2] | 2 [2–2] | 2 [2–2] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| gpu-process | 151 [150–153] | 154 [154–157] | 151 [150–153] | 150 [148–153] | 150 [150–153] | 148 [148–148] |
| renderer | 1288 [1271–1292] | 1202 [1186–1205] | 1279 [1262–1291] | 2583 [2571–2631] | 1137 [1131–1140] | 1245 [1241–1262] |
| renderer:browser-ui | 31 [27–31] | 31 [28–33] | 27 [26–32] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| renderer:extension | 0 [0–0] | 15 [15–17] | 0 [0–0] | 0 [0–0] | 63 [62–65] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 0 [0–0] | 0 [0–0] | 33 [32–39] | 34 [32–36] | 31 [29–32] |
| utility:NetworkService | 45 [44–48] | 43 [40–44] | 46 [43–48] | 37 [34–38] | 37 [33–38] | 37 [37–41] |
| utility:StorageService | 7 [7–7] | 7 [7–7] | 7 [7–8] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| zygote | 12 [11–12] | 12 [12–13] | 11 [11–13] | 18 [18–18] | 17 [17–17] | 17 [17–17] |

#### Process count by type with all tabs open

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broker | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| crashpad | 2 [2–2] | 2 [2–2] | 2 [2–2] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| gpu-process | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| renderer | 23 [22–23] | 18 [17–18] | 22 [21–22] | 16 [15–16] | 16 [16–16] | 21 [20–21] |
| renderer:browser-ui | 1 [1–1] | 1 [1–1] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| renderer:extension | 0 [0–0] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 1 [1–1] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 0 [0–0] | 0 [0–0] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| utility:NetworkService | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| utility:StorageService | 1 [1–1] | 1 [1–1] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| zygote | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] |

#### Local 1080p60 VP9 video, 20 s of playback

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU, % of one core | 27.5 [27.5–27.6] | 27.6 [27.4–27.9] | 27.5 [27.3–27.6] | 26.5 [26.4–26.7] | 27.0 [26.9–27.3] | 26.5 [26.3–26.9] |
| Frames presented | 1202 [1201–1204] | 1201 [1200–1204] | 1201 [1201–1204] | 1201 [1200–1203] | 1201 [1200–1201] | 1201 [1201–1201] |
| Frames dropped | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| PSS while playing, MiB | 409 [406–411] | 421 [419–425] | 408 [405–413] | 385 [381–389] | 450 [445–451] | 385 [383–390] |

#### Speedometer 3.1 (higher is better)

| Metric | chromium | chromium-ubol | chromium-limit4 | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Score | 22.40 [22.30–22.40] | 22.40 [22.20–22.50] | 22.40 [22.40–22.50] | 32.70 [32.50–32.80] | 30.70 [30.50–30.90] | 32.60 [32.40–32.90] |

:::

::: details Run 2: three rounds, with Chrome for Testing

Samples: 54, failed: 0

#### Cold start, empty profile, one local tab

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Window to local page loaded, ms | 247 [239–266] | 286 [277–287] | 649 [640–699] | 419 [417–433] | 537 [523–550] | 398 [372–402] |
| CPU used until loaded, s | 0.62 [0.61–0.68] | 0.61 [0.60–0.66] | 1.04 [1.02–1.14] | 0.70 [0.70–0.71] | 1.02 [1.00–1.02] | 0.68 [0.65–0.68] |
| CPU in the 15 s after loading, s | 0.39 [0.33–0.39] | 0.35 [0.32–0.37] | 0.54 [0.43–0.55] | 0.28 [0.28–0.30] | 2.86 [2.84–2.87] | 0.28 [0.26–0.29] |
| Sampled peak PSS during start-up, MiB | 338 [338–339] | 407 [407–408] | 448 [445–450] | 299 [297–302] | 622 [617–625] | 300 [298–302] |
| PSS 15 s after loading, MiB | 335 [317–336] | 407 [407–407] | 448 [445–450] | 297 [295–301] | 481 [472–481] | 298 [297–300] |
| Processes | 13 [13–13] | 16 [16–16] | 17 [17–17] | 8 [8–8] | 9 [9–9] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.45 [0.43–0.45] | 0.53 [0.53–0.57] | 0.56 [0.55–0.58] | 0.29 [0.25–0.31] | 2.08 [2.00–2.33] | 0.29 [0.27–0.30] |
| Profile on disk after first run, MiB | 4.4 [4.4–4.4] | 4.5 [4.5–4.5] | 50.7 [50.7–50.7] | 12.6 [12.6–12.6] | 55.1 [54.9–55.1] | 6.1 [5.7–6.1] |

#### Warm start, same profile relaunched, one local tab

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Window to local page loaded, ms | 237 [229–241] | 299 [253–315] | 664 [663–672] | 613 [607–634] | 708 [699–718] | 380 [375–381] |
| CPU used until loaded, s | 0.53 [0.53–0.54] | 0.64 [0.57–0.68] | 1.06 [1.06–1.06] | 1.20 [1.20–1.24] | 1.36 [1.36–1.38] | 0.63 [0.61–0.64] |
| Sampled peak PSS during start-up, MiB | 287 [287–287] | 390 [387–393] | 430 [428–431] | 387 [266–387] | 459 [434–494] | 266 [259–268] |
| PSS 15 s after loading, MiB | 285 [285–285] | 389 [385–392] | 429 [427–430] | 262 [258–264] | 429 [424–493] | 257 [256–263] |
| Anonymous memory (cgroup), MiB | 163 [162–164] | 246 [244–247] | 288 [283–289] | 250 [248–252] | 404 [404–470] | 140 [139–143] |
| Processes | 12 [12–12] | 15 [15–15] | 16 [16–16] | 8 [8–8] | 9 [9–9] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.11 [0.11–0.11] | 0.33 [0.33–0.34] | 0.39 [0.37–0.39] | 0.14 [0.10–0.19] | 3.01 [0.54–4.18] | 0.13 [0.13–0.14] |

#### Browsing 10 real sites, one tab each

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU time for the whole browse, s | 31.9 [31.5–32.8] | 29.3 [29.2–29.3] | 26.6 [26.6–26.7] | 75.2 [73.9–75.7] | 24.8 [24.3–25.1] | 25.2 [25.2–26.2] |
| Median page load (navigate to load event), ms | 1112 [1075–1131] | 1094 [1072–1109] | 836 [828–959] | 791 [786–794] | 885 [831–937] | 915 [903–916] |
| Pages that reached load within 30 s | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] | 10 [10–10] |
| Requests started | 1424 [1421–1430] | 1498 [1478–1514] | 1265 [1262–1291] | 220717 [220291–222434] | 1257 [1236–1269] | 1495 [1483–1521] |
| Requests blocked by the client | 0 [0–0] | 0 [0–0] | 60 [59–77] | 219507 [219073–221195] | 64 [56–70] | 0 [0–0] |
| Other failed requests | 29 [28–31] | 20 [19–30] | 7 [6–9] | 25 [20–25] | 7 [6–18] | 26 [25–31] |
| Data received, MiB | 44.0 [43.7–44.1] | 45.5 [44.6–45.8] | 42.0 [39.8–42.3] | 43.5 [41.8–43.6] | 40.9 [38.7–41.3] | 46.6 [46.2–46.7] |
| Sampled peak PSS while browsing, MiB | 1886 [1861–1912] | 2266 [2084–2330] | 2138 [2088–2225] | 2887 [2875–2906] | 1948 [1819–1962] | 1843 [1830–1863] |
| PSS with all tabs open (20 s settle), MiB | 1557 [1539–1581] | 1808 [1805–1811] | 1759 [1736–1763] | 2865 [2845–2880] | 1462 [1432–1464] | 1506 [1486–1528] |
| Anonymous memory (cgroup), MiB | 1308 [1290–1323] | 1511 [1506–1518] | 1464 [1442–1473] | 2639 [2620–2657] | 1232 [1204–1237] | 1269 [1248–1285] |
| Processes | 33 [32–33] | 37 [37–38] | 34 [33–34] | 23 [22–23] | 24 [23–24] | 28 [27–28] |
| Idle CPU with all tabs open, % of one core | 2.52 [2.36–3.56] | 3.51 [2.67–4.36] | 2.28 [2.25–2.84] | 111.51 [111.42–112.21] | 1.50 [1.38–2.46] | 1.89 [1.81–1.94] |
| PSS after Fly hibernates inactive tabs, MiB | – | – | – | 422 [396–423] | 482 [461–492] | 441 [416–447] |
| PSS 10 s after closing all but one tab, MiB | 526 [499–531] | 1368 [1359–1372] | 1333 [1309–1345] | 380 [364–387] | 436 [416–444] | 381 [370–391] |
| Processes after closing | 14 [13–14] | 25 [25–26] | 26 [25–26] | 10 [9–10] | 10 [9–10] | 10 [9–10] |
| Profile on disk after browsing, MiB | 183.5 [181.9–183.6] | 94.2 [93.3–95.6] | 113.0 [111.4–132.1] | 61.9 [61.2–63.1] | 128.2 [125.6–133.5] | 61.1 [59.4–62.1] |

#### PSS by process type with all tabs open, MiB

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broker | 17 [17–17] | 27 [27–27] | 27 [27–27] | 15 [15–15] | 15 [15–15] | 15 [15–15] |
| crashpad | 2 [2–2] | 2 [2–2] | 2 [2–2] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| gpu-process | 151 [151–153] | 155 [154–157] | 158 [157–159] | 153 [152–153] | 152 [151–154] | 149 [148–151] |
| renderer | 1293 [1272–1316] | 1413 [1410–1415] | 1346 [1326–1348] | 2609 [2588–2623] | 1144 [1113–1147] | 1258 [1238–1272] |
| renderer:browser-ui | 31 [30–31] | 42 [41–44] | 44 [43–45] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| renderer:extension | 0 [0–0] | 0 [0–0] | 17 [16–18] | 0 [0–0] | 62 [61–62] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 0 [0–0] | 0 [0–0] | 34 [33–38] | 35 [34–36] | 31 [30–32] |
| utility:CdmServiceBroker | 0 [0–0] | 84 [77–87] | 85 [85–85] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| utility:NetworkService | 43 [43–46] | 55 [55–56] | 48 [48–52] | 37 [34–37] | 36 [34–37] | 37 [37–41] |
| utility:StorageService | 7 [7–7] | 8 [8–8] | 8 [8–8] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| zygote | 12 [12–12] | 23 [22–23] | 22 [22–23] | 18 [18–18] | 17 [17–17] | 17 [17–17] |

#### Process count by type with all tabs open

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| broker | 1 [1–1] | 2 [2–2] | 2 [2–2] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| crashpad | 2 [2–2] | 2 [2–2] | 2 [2–2] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| gpu-process | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| renderer | 23 [22–23] | 25 [25–26] | 21 [20–21] | 16 [15–16] | 16 [15–16] | 21 [20–21] |
| renderer:browser-ui | 1 [1–1] | 1 [1–1] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| renderer:extension | 0 [0–0] | 0 [0–0] | 1 [1–1] | 0 [0–0] | 1 [1–1] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 0 [0–0] | 0 [0–0] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| utility:CdmServiceBroker | 0 [0–0] | 1 [1–1] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| utility:NetworkService | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| utility:StorageService | 1 [1–1] | 1 [1–1] | 1 [1–1] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| zygote | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] | 3 [3–3] |

#### Local 1080p60 VP9 video, 20 s of playback

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU, % of one core | 27.5 [27.5–27.8] | 27.4 [27.4–27.6] | 27.5 [27.2–27.7] | 26.6 [26.6–26.6] | 27.1 [27.0–27.3] | 26.7 [26.6–26.8] |
| Frames presented | 1201 [1201–1202] | 1201 [1200–1201] | 1201 [1200–1201] | 1202 [1201–1202] | 1202 [1201–1202] | 1202 [1201–1203] |
| Frames dropped | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] | 0 [0–0] |
| PSS while playing, MiB | 409 [408–411] | 463 [462–465] | 482 [480–485] | 388 [386–389] | 452 [451–455] | 388 [383–388] |

#### Speedometer 3.1 (higher is better)

| Metric | chromium | cft | cft-ubol | fly | fly-ext | fly-noblock |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Score | 22.40 [22.40–22.50] | 31.30 [31.10–31.50] | 31.50 [31.20–31.60] | 32.60 [32.40–32.80] | 30.60 [30.10–30.80] | 32.60 [32.50–32.70] |

:::

::: details Run 3: three rounds, nine sites without CNN

Samples: 9, failed: 0

#### Cold start, empty profile, one local tab

| Metric | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| Window to local page loaded, ms | 249 [244–296] | 413 [407–419] | 390 [377–401] |
| CPU used until loaded, s | 0.63 [0.62–0.68] | 0.68 [0.67–0.70] | 0.67 [0.64–0.67] |
| CPU in the 15 s after loading, s | 0.39 [0.32–0.39] | 0.29 [0.26–0.30] | 0.27 [0.27–0.30] |
| Sampled peak PSS during start-up, MiB | 335 [334–338] | 299 [299–302] | 299 [299–300] |
| PSS 15 s after loading, MiB | 332 [309–334] | 297 [297–300] | 298 [298–298] |
| Processes | 13 [13–13] | 8 [8–8] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.42 [0.41–0.42] | 0.28 [0.28–0.32] | 0.28 [0.27–0.32] |
| Profile on disk after first run, MiB | 4.4 [4.4–4.4] | 12.2 [12.2–12.6] | 6.1 [5.7–6.1] |

#### Warm start, same profile relaunched, one local tab

| Metric | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| Window to local page loaded, ms | 231 [228–237] | 624 [617–627] | 375 [358–383] |
| CPU used until loaded, s | 0.53 [0.53–0.53] | 1.22 [1.20–1.24] | 0.60 [0.60–0.64] |
| Sampled peak PSS during start-up, MiB | 287 [286–287] | 308 [264–389] | 265 [260–268] |
| PSS 15 s after loading, MiB | 284 [284–285] | 262 [262–306] | 261 [257–262] |
| Anonymous memory (cgroup), MiB | 162 [161–164] | 251 [248–291] | 146 [144–146] |
| Processes | 12 [12–12] | 8 [8–8] | 8 [8–8] |
| Idle CPU over 30 s, % of one core | 0.11 [0.11–0.12] | 0.15 [0.09–0.34] | 0.14 [0.12–0.16] |

#### Browsing 9 real sites, one tab each

| Metric | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| CPU time for the whole browse, s | 23.2 [23.0–23.6] | 15.6 [15.5–15.7] | 18.5 [18.5–18.7] |
| Median page load (navigate to load event), ms | 1014 [935–1100] | 794 [716–809] | 849 [834–894] |
| Pages that reached load within 30 s | 9 [9–9] | 9 [9–9] | 9 [9–9] |
| Requests started | 1295 [1287–1296] | 1171 [1162–1176] | 1343 [1326–1369] |
| Requests blocked by the client | 0 [0–0] | 53 [52–54] | 0 [0–0] |
| Other failed requests | 23 [20–31] | 26 [15–26] | 26 [24–30] |
| Data received, MiB | 25.6 [25.2–25.7] | 23.5 [23.3–23.6] | 27.6 [27.4–27.9] |
| Sampled peak PSS while browsing, MiB | 1677 [1598–1687] | 1577 [1534–1597] | 1740 [1710–1765] |
| PSS with all tabs open (20 s settle), MiB | 1346 [1346–1353] | 1182 [1182–1188] | 1299 [1288–1317] |
| Anonymous memory (cgroup), MiB | 1119 [1117–1129] | 982 [982–992] | 1087 [1077–1108] |
| Processes | 29 [29–29] | 20 [20–20] | 23 [23–24] |
| Idle CPU with all tabs open, % of one core | 2.04 [2.01–2.21] | 1.28 [1.17–1.49] | 1.99 [1.49–2.00] |
| PSS after Fly hibernates inactive tabs, MiB | – | 415 [412–422] | 416 [415–431] |
| PSS 10 s after closing all but one tab, MiB | 521 [520–522] | 382 [378–394] | 360 [357–381] |
| Processes after closing | 14 [14–14] | 10 [10–10] | 9 [9–10] |
| Profile on disk after browsing, MiB | 164.5 [164.1–164.6] | 44.1 [43.5–44.6] | 42.3 [41.6–42.3] |

#### PSS by process type with all tabs open, MiB

| Metric | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| broker | 17 [17–17] | 15 [15–15] | 15 [15–15] |
| crashpad | 2 [2–2] | 0 [0–0] | 0 [0–0] |
| gpu-process | 149 [148–149] | 148 [145–149] | 144 [143–145] |
| renderer | 1086 [1086–1090] | 934 [933–935] | 1051 [1043–1067] |
| renderer:browser-ui | 30 [30–31] | 0 [0–0] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 33 [32–40] | 32 [31–32] |
| utility:NetworkService | 44 [41–44] | 34 [34–35] | 40 [37–40] |
| utility:StorageService | 7 [7–7] | 0 [0–0] | 0 [0–0] |
| zygote | 12 [12–13] | 18 [18–19] | 18 [17–18] |

#### Process count by type with all tabs open

| Metric | chromium | fly | fly-noblock |
| --- | ---: | ---: | ---: |
| broker | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| crashpad | 2 [2–2] | 0 [0–0] | 0 [0–0] |
| gpu-process | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| renderer | 19 [19–19] | 13 [13–13] | 16 [16–17] |
| renderer:browser-ui | 1 [1–1] | 0 [0–0] | 0 [0–0] |
| renderer:fly-ui | 0 [0–0] | 1 [1–1] | 1 [1–1] |
| utility:NetworkService | 1 [1–1] | 1 [1–1] | 1 [1–1] |
| utility:StorageService | 1 [1–1] | 0 [0–0] | 0 [0–0] |
| zygote | 3 [3–3] | 3 [3–3] | 3 [3–3] |

:::
