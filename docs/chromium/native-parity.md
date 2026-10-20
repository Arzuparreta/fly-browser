# Native action and preference parity

This reviewed inventory complements the generated [baseline ledger](./parity-ledger.md).
It describes native implementation, not release readiness. Every row remains
partial or pending until the relevant complete acceptance outcomes pass.

Accepted Linux evidence through `bbb8a09` is archived in
`/mnt/storage/fly-chromium/evidence/runtime-bbb8a09-before-window-controls`:
59 service tests, 71 browser declarations including two PRE stages, both media
fixtures and independent audit pass. PR 126 is merged as `9089776`: all three
platform service and object matrices pass and their six artifacts are audited.
The window-control checkpoint `5d55d9a` passes 62 services, all 74 browser
declarations, both media fixtures and independent audit on Linux. The sidebar
follow-up `9606b31` passes the complete Linux run: 65 services, all 78 browser
stages, both media fixtures and independent audit. Its archive is
`/mnt/storage/fly-chromium/evidence/runtime-9606b31-sidebar-accepted`.
Latest-head hosted checks and other-platform runtime/package acceptance remain
pending.

Action routes live in `chromium/overlay/fly/ui/native_commands.cc` and
`ui/fly_window_controller.cc`. Tests are in `test/workspace_browser_test.cc`;
keyboard/core contracts also have dedicated `*_unittest.cc` files.
A native upstream route alone does not establish Fly UX parity.

## Actions

| Action | Status | Implementation and remaining acceptance |
| --- | --- | --- |
| `toggle-menu-bar` | partial | Fly settings menu (startup settings, tab suspension, sidebar options, user content folders, downloads, task manager, diagnostics, What's New, updates, preferences, restart); Chromium's app menu is no longer used. |
| `spawn-tab` | partial | Product blank tabs and native profile/session identity tested; complete all-input-path acceptance remains pending. |
| `toggle-address` | partial | Floating native address/search, aliases, bangs, workspace lookup, bookmarks and calculator pass the complete ab4b496 Linux suite. History suggestions, platform path handling and import require further acceptance. |
| `toggle-dashboard` | partial | Electron's dashboard: every workspace with its tabs (pinned marked), Rename/Delete per workspace, opening a listed tab and Move per tab; New workspace asks for the name. Drag-and-drop of tabs between workspaces is replaced by Move. |
| `reload-active-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `remove-tab` | partial | Pinned protection, beforeunload, close/reopen and native menu tested; complete all-input-path acceptance remains pending. |
| `focus-sidebar` | partial | Native panel focus and Escape back to editable page tested; complete accessibility, all-input-path and platform acceptance remains pending. |
| `focus-nav-buttons` | partial | Expands the sidebar and focuses its new tab/back/forward controls (the Chromium toolbar is hidden). |
| `focus-webview` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `trigger-hints` | partial | Native hint input/activation and visibility tested; complete document/platform coverage remains pending. |
| `toggle-help` | partial | Opens Preferences with Electron's sections (General, Appearance, Keyboard, Privacy & security, Extensions, Performance, Diagnostics, About), its settings search and the section shown last. |
| `toggle-private-mode` | partial | Native OTR profiles, exit cancellation and isolation tested; complete private services/recovery acceptance remains pending. |
| `toggle-palette` | partial | Electron's Command Centre: its 46 commands with texts generated from COMMAND_DEFINITIONS, category chips, match count, grouped list, inspector card, recently used commands, extension commands and the clipboard terminal fallback. |
| `toggle-find` | partial | Native command routed; complete pointer/keyboard find-bar acceptance pending. |
| `toggle-devtools` | partial | Selected-profile docked native DevTools tested; full platform acceptance remains pending. |
| `toggle-zen-mode` | partial | Toggles native automatic expansion/pinning without hover viewport reflow; full product/platform acceptance remains pending. |
| `add-bookmark` | partial | Shared ordered flat Fly bookmarks tested; complete product/migration acceptance remains pending. |
| `toggle-bookmarks` | partial | Shared ordered flat Fly bookmarks; private/destination identity and deletion tested. Frequent-site ranking/hiding/display integration is in development; complete runtime acceptance remains pending. |
| `toggle-global-media` | partial | Native sessions plus fallback players tested across hidden workspaces; full platform acceptance remains pending. |
| `toggle-tab-audio-mute` | partial | Native mute and extension reason reporting tested; full platform acceptance remains pending. |
| `toggle-global-audio-mute` | partial | Native mute reasons and hidden/private tabs tested; full platform acceptance remains pending. |
| `toggle-history` | partial | Shared projection of authorized native profile histories, selector, destination navigation and confirmed clearing implemented; new runtime cases, frequent sites, import and platform acceptance remain pending. |
| `toggle-downloads` | partial | Downloads picker across workspace profiles: open, pause/resume, cancel/clear and the full `chrome://downloads` page. |
| `toggle-status-bar` | partial | Native hover, security/private context and persistence tested; full platform acceptance remains pending. |
| `toggle-clock` | partial | Native live settings and no tick writes tested on Linux; full platform acceptance remains pending. |
| `cycle-clock-format` | partial | Native formatting/settings persistence tested; full platform acceptance remains pending. |
| `toggle-split` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `switch-split-focus` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `swap-split-panes` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `toggle-reader-view` | partial | Electron's reader (modules/readerDistiller.js with Readability) runs in the page's isolated world: same overlay, toolbar and per-site preferences; the reader button follows its state, including Escape. |
| `toggle-pin-tab` | partial | Native pinned state and close protection tested; complete input-path acceptance remains pending. |
| `open-extension-actions` | partial | Picker of the workspace's extension actions (popups anchored to their footer buttons), extension settings, `chrome://extensions` and the Chrome Web Store. |
| `toggle-window-controls` | partial | Three owned Xvfb/Openbox desktop cases and all 62 services pass at ee16406; full-suite/platform acceptance remains pending. |
| `reset-zoom` | partial | siteZoom.js: one level per hostname shared by every normal workspace (private ones separate); also from the footer. |
| `toggle-sidebar-pin` | partial | Native button, command and live settings share persistent pinning; full product/platform acceptance remains pending. |
| `toggle-split-horizontal` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `cycle-split-orientation` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `close-split-view` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `hibernate-tabs` | partial | Native retained freeze/resume and protected cases tested; remaining lifecycle/platform acceptance pending. |
| `wake-all-tabs` | partial | Native workspace-scoped resume tested; remaining lifecycle/platform acceptance pending. |
| `open-scripts-dir` | partial | Opens `<user data>/scripts`; files there run through Fly User Content. |
| `open-styles-dir` | partial | Opens `<user data>/styles`; files there run through Fly User Content. |
| `open-script-storage-dir` | partial | Opens `<user data>/script-storage`. |
| `reload-user-content` | partial | Rereads scripts/styles and reloads Fly User Content in every workspace; folder edits also reload automatically. |
| `test-google-signin` | partial | Opens Google sign-in in a fresh off-the-record profile. |
| `toggle-theme` | partial | Native light/dark and website synchronization; custom colours and sidebar/overlay opacity in Preferences → Appearance. |
| `check-for-updates` | partial | Checks the latest GitHub release at start and on demand; Preferences → About installs a release's native package in place (SHA-256 checked) and restarts, or opens the release page when it has none. |
| `open-diagnostics` | partial | Electron's Diagnostics & Health Hub: System & Runtimes, Processes & Memory (Compact RAM), the event log with filters, and the sanitised Markdown report with copy, JSON export and the issues page. |
| `toggle-diagnostics-recording` | partial | Toggles in-memory recording (`enable_diagnostics`); events carry level and category and appear in the hub's log. |
| `go-back` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `go-forward` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `next-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `previous-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `duplicate-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `next-workspace` | partial | Native workspace routing present; complete configured input-path acceptance remains pending. |
| `previous-workspace` | partial | Native workspace routing present; complete configured input-path acceptance remains pending. |
| `new-workspace` | partial | Asks for the name as Electron, then creates a fresh native profile (async retirement guards tested). |
| `force-reload-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `stop-loading` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `zoom-in` | partial | siteZoom.js steps of 0.5 levels (-5 to 5) for keys, commands and Ctrl+wheel, shared per hostname by normal workspaces. |
| `zoom-out` | partial | siteZoom.js steps of 0.5 levels (-5 to 5) for keys, commands and Ctrl+wheel, shared per hostname by normal workspaces. |
| `yank-url` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `toggle-passthrough` | partial | Native keyboard mode routing implemented; complete IME/AltGr and extension-key acceptance remains pending. |
| `enter-insert` | partial | Native keyboard mode routing implemented; complete IME/AltGr and extension-key acceptance remains pending. |
| `scroll-down` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `scroll-up` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `scroll-left` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `scroll-right` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `half-page-down` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `half-page-up` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `page-top` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `page-bottom` | partial | Native focused-page scrolling, editable suppression and cross-origin frame routing tested; full platform acceptance remains pending. |
| `show-whats-new` | partial | Opens the packaged What's New page (or the release list when absent); after an upgrade it opens with `?from=`. |
| `toggle-native-menu-visibility` | partial | Native Fly has no menu bar; the action opens the Fly settings menu. |
| `move-tab-to-workspace` | partial | Five Linux browser cases pass at bbb8a09; crash recovery and all-platform UI acceptance remain pending. |
| `open-keyboard-settings` | partial | Native recorder/drafts/conflict/cancellation tested; IME/AltGr/platform acceptance remains pending. |
| `reopen-closed-tab` | partial | Native historical restore and Fly identity tested; browser crash recovery remains pending. |
| `move-tab-left` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `move-tab-right` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `toggle-fullscreen` | partial | Native command routed; full desktop/page fullscreen and restore acceptance pending. |
| `print-page` | partial | Native command routed; print/PDF dialog acceptance pending. |
| `toggle-notifications` | partial | Sets the notification permission default (ask/block) in every workspace profile; footer bell follows it. |

## Preferences

Implemented groups use `preferences/preferences_service.{h,cc}`, preserving
unknown metadata and rejecting malformed/future stores. The native settings UI
lives in `ui/keyboard_editor.cc`. Unimplemented preferences are not silently
declared supported because Chromium has a related global flag or policy.

| Preference | Status | Implementation and remaining acceptance |
| --- | --- | --- |
| `disable_gpu` | partial | Settings menu and Performance tab; applied at the next start (`--disable-gpu`). |
| `background_throttling` | partial | Settings menu and Performance tab; off adds `--disable-background-timer-throttling` at the next start. |
| `process_limit` | partial | 1–8 (default 4), settings menu and Performance tab; `--renderer-process-limit` at the next start. Site isolation still takes precedence. |
| `email_handler` | partial | Preferences → General with Electron's choices or any compose URL; used by the page Share menu. |
| `search_engine` | partial | Typed app-level service and native Search preference tab; directed validation underway. Import and full acceptance remain pending. |
| `spellchecker_language` | partial | Appearance tab; applied to every workspace profile's Chromium spellcheck dictionaries. |
| `theme` | partial | Native light/dark, existing hidden/private pages tested; custom colours and opacities in Appearance. |
| `interface_scale` | partial | Appearance → Interface size; "large" scales Fly's fonts, icons, buttons, rows, sidebar/rail widths and overlays by 1.25. |
| `sync_website_theme` | partial | Native renderer color scheme synchronization tested without replacing page APIs. |
| `trusted_domains` | not applicable | Electron needed it for its Google sign-in workarounds; native Chromium is accepted directly. Kept for import. |
| `tab_suspend_timeout_minutes` | partial | Off and 5/15/30/60-minute native deadlines tested with retained documents and protected cases. |
| `sidebar_auto_collapse` | partial | Default enabled, native live Appearance control and recovery policy tested; import and full platform acceptance remain pending. |
| `show_status_bar` | partial | Live native footer and persistent choice tested. |
| `show_clock` | partial | Live native clock, private context and no per-tick writes tested. |
| `show_frequent_sites` | partial | Typed native choice and live Appearance control, derived ranking, bookmark exclusions and explicit hiding implemented; new runtime cases and import/platform acceptance remain pending. |
| `clock_format` | partial | Three existing formats and native preference changes tested. |
| `theme_colors` | partial | Appearance: accent, canvas, sidebar and text colour per theme with swatches, sidebar_opacity and overlay_opacity, reset to defaults. |
| `enable_diagnostics` | partial | Performance tab and action; events stay in memory. |
| `show_window_controls` | partial | Follow-up native preference, preservation/policy cases and real desktop controls tested; full-suite/platform acceptance pending. |
| `custom_search_aliases` | partial | Twelve built-ins, custom override/edit/removal and native settings; recovery/policy/encoding service cases pass. UI, import and platform acceptance remain incomplete. |
| `last_seen_version` | partial | Written on the first start of a version; an upgrade opens What's New. |
| `desktop_integration_offered` | partial | Offered once on the first start of a package no menu entry lists (menu entry, optionally default browser); Preferences → About manages both. |
| `extensions_enabled` | partial | Global switch (Preferences → Extensions): off unloads bundled extensions and suspends the user's own, restoring exactly those. |
| `extensions_by_workspace` | partial | Each workspace is a Chromium profile, so installed extensions are per workspace natively; the bundled blocker can be switched off per workspace. Electron selections still need import. |
| `extensions_bundled` | partial | uBlock Origin Lite (MV3, pinned in `chromium/bundled-extensions.json`) replaces the MV2 uBlock Origin Chromium no longer runs; loaded as a Fly component with its Web Store identity. Removal is remembered. |
| `extensions_disabled` | partial | Disabling uses Chromium's own per-profile state; bundled switches are stored by Fly. Import from Electron pending. |
| `extensions_unload_grace_seconds` | not applicable | Chromium suspends MV3 service workers itself; nothing to configure. Kept for import. |
| `extensions_sources` | partial | Store installs come from the Chrome Web Store with Chromium's updater; local packages through `chrome://extensions`. |

## Import from Electron Fly

`fly/import/legacy_import.cc` covers `config.json`, `session.json` (workspaces,
order, tabs, pins, active workspace), partitions' site data, `bookmarks.json`,
`history.json`, `keybinds.json` (version 2 and legacy tables), `site-zoom.json`,
`hidden-sites.json`, `scripts/`, `styles/`, `script-storage/` and the bundled
blocker choices. Not imported: `quickmarks.json` (retired), Electron's
`Local Storage` of the interface (recent commands), extensions installed by hand
(listed for reinstallation), tab scroll positions and session storage records.

## Completion gates

All-platform native UI, migration, recovery, installable packages, product and
engine update rehearsals remain pending. macOS object coverage excludes the
unimplemented desktop adapter. Electron remains the production runtime.
