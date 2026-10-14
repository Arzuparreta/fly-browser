# Adoption and review guide

This proposal brings the application's changes from the fork's personal branch to upstream 0.15.0, including the newer direct tab-jump/sidebar-cursor work at 2f8ca5f. The goal is a keyboard-accessible browser with editable bindings and discoverable Preferences, while keeping default browsing suitable for limited hardware. The [performance audit](./performance-audit.md) gives reproducible comparisons and explicit limits; the [fix ledger](./fork-fixes.md) separates inherited repairs from new capabilities.

## Review order and ownership

| Review block | Start here | Behaviour and boundaries | Evidence |
| :--- | :--- | :--- | :--- |
| Default browsing costs | security.js, modules/clock.js, modules/statusBar.js, webview-preload.js | One shared pending filter-engine build, per-session attachment; unchanged clock/badge DOM is retained; hints report actual transitions. No added polling or metrics in normal launches. | adblocker-startup, clock and keyboard unit tests; process-tree benchmark, real filter fetch counts and hints IPC counts. |
| Keyboard model | keyboard.js, keybinds.js, scripts/sync-keyboard.js, webview-preload.js | Dependency-free shared grammar, separate browser/page maps, configurable two-stroke sequences. Compile on configuration changes. Local browser actions dispatch in the renderer; only native operations take the main-process route. Preserve IME, AltGr, editable regions and passthrough. Sandbox grammar is generated. | keyboard.test.js, generated drift checks; [reference](./reference/keybinds.md) and [coverage](./keyboard-review.md). |
| Keyboard editor | modules/keyboardSettings.js, main.js binding IPC, preload.js | Record, validate, reassign, save/discard/import/export. Atomic write precedes activation. Host-only mutation. Build rows on opening and controls on expansion; release them on closing. | keyboard-ui: conflicts, recorder, live updates, unsaved draft preservation and editable pages. |
| Preferences | index.html, modules/settingsNav.js, style.css, modules/overlays/commandPalette.js | Section index, cross-section search and keyboard hand-off. Search runs on user input; no background indexing worker. Existing values remain in the same config file. | settings-nav-ui: persistence, search, section keys, compact viewport. Updated screenshots in the guide. |
| Focus and browser panels | modules/focus.js, modules/navigation.js, renderer.js, modules/overlays/* | One browser panel owns focus; shared cancellation and focus containment. Ignore stale asynchronous loads. Pointer and keyboard sidebar behaviour remain distinct. | navigation-focus, overlay-state, sidebar-pointer-focus and tab-close-typing desktop suites. |
| Tab state | modules/tabActions.js, modules/tabTransfer.js, modules/webview.js | Bounded in-memory reopen buffer; metadata moves together; moving workspaces destroys the old guest and recreates in the destination container. Sidebar cursor does not wake tabs. Shared metadata arrays are removed once on close. | keyboard state tests, tab-jump production-function tests and sidebar-cursor-ui sleeping-tab assertions. |
| Optional extensions | extensions.js, modules/extensions.js, main.js, preload.js | Disabled by default; compatibility library is required only when enabled. Explicit opt-in, local extension install/remove and permission/origin restrictions. No Web Store installer or full Chrome compatibility promise. | extensions.test.js, [capabilities and limits](./guide/extensions.md); benchmark asserts the compatibility library stays unloaded when disabled. |
| Delivery and maintenance | package.json, .github/workflows/verify.yml, tests/electron-fixture.js, tests/packaged-ui.test.js | Include required local modules in the actual ASAR. Tests use disposable application packages/profiles. Normal test command includes generated-file checks; Playwright remains optional and external. | Linux directory-package launch and archive inspection; npm test and docs build. |

The upstream proposal preserves the original repository's contribution and AI-policy documents. The fork's personal-work-log process belongs to the fork. Application/runtime files match the validated personal revision; generated benchmark JSON is marked as generated so it can be collapsed during review. New modules follow the existing CommonJS main-process and ES-module renderer split; there is no new application framework.

## Changes users will notice

New-profile defaults adopt conventional browser close/find/bookmark/tab-cycle chords. Existing explicit bindings, including disabled bindings and alternate chords, migrate without silently adding defaults over them. The generated reference lists every default. Ctrl+1–9 from the new upstream work coexists with Alt+1–9: Ctrl+9 means ninth-or-last, while Alt+9 always means last. Both families are editable.

Preferences is sectioned and searchable. Its Keyboard section exposes browser shortcuts and NORMAL page keys independently. A draft survives closing Preferences; only Save changes active/disk bindings. Passthrough retains a recovery chord. Native Tab, Escape and contextual arrows/Enter/Delete retain their UI roles.

Opaque appearance and disabled extensions remain the clean-profile defaults used in the audit. Optional transparency/blur, enabled extensions and arbitrary websites can cost resources and are outside the default-parity claim. No personal profile, telemetry or production benchmark collector is included.

## Security and persistence to review

Guest context isolation, sandboxing and disabled Node integration remain enabled. Guests receive the public keyboard table, not file or Node handles. The browser main frame alone can mutate bindings or invoke the privileged keyboard dispatcher. Extension IPC validates senders; the extension guide describes the supported permission boundary and unimplemented APIs.

Workspaces still own separate partitions. Cross-workspace moves intentionally reload rather than carrying a live guest across containers. Private-mode changes clear the in-memory closed-tab buffer; it stores at most twenty URLs and never persists navigation history. Keyboard saves are atomic, invalid files are left intact, and write failures retain the previous active configuration.

## Validation and adoption limits

Run npm test and npm run docs:build first. Run npm run test:keyboard-ui on a real desktop with external Playwright available through MISE_PLAYWRIGHT_MODULE, followed by tests/sidebar-footer-ui.test.js. The [audit](./performance-audit.md#reproduction) documents clean-profile benchmark and packaged-smoke commands. These checks create temporary homes and user-data directories.

Measurements cover Linux/X11 on a Ryzen 7 desktop and host-renderer 6× CPU throttling. They do not establish small-RAM/ARM, battery, Wayland or other-platform parity. The benchmark exposes sample ranges and cold-start variability; it does not justify a universal zero-byte or zero-latency claim. Optional extension compatibility is a separate feature decision, with no claim that enabled third-party code has zero cost.

The review blocks above can be assessed independently, but keyboard grammar, catalogue, generated preload and generated reference must move together. Preference markup, search index and layout also form one coherent block. Any selective adoption should rerun generated checks, desktop focus tests and the clean-profile comparison on the resulting tree.
