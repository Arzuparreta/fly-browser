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
| `toggle-dashboard` | partial | Native searchable workspace/private/empty desktop picker tested; full dashboard product layout remains pending. |
| `reload-active-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `remove-tab` | partial | Pinned protection, beforeunload, close/reopen and native menu tested; complete all-input-path acceptance remains pending. |
| `focus-sidebar` | partial | Native panel focus and Escape back to editable page tested; complete accessibility, all-input-path and platform acceptance remains pending. |
| `focus-nav-buttons` | partial | Expands the sidebar and focuses its new tab/back/forward controls (the Chromium toolbar is hidden). |
| `focus-webview` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `trigger-hints` | partial | Native hint input/activation and visibility tested; complete document/platform coverage remains pending. |
| `toggle-help` | partial | Native preferences/keyboard editor implemented; not the complete product help surface. |
| `toggle-private-mode` | partial | Native OTR profiles, exit cancellation and isolation tested; complete private services/recovery acceptance remains pending. |
| `toggle-palette` | partial | Native search, keyboard customization, cancellation and profile routing tested; remaining actions pending. |
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
| `toggle-reader-view` | partial | Chromium DOM distiller in place, following the Fly theme; toggling returns to the article. Electron's own reader styles are not reproduced. |
| `toggle-pin-tab` | partial | Native pinned state and close protection tested; complete input-path acceptance remains pending. |
| `open-extension-actions` | pending | No native Fly action implementation yet. |
| `toggle-window-controls` | partial | Three owned Xvfb/Openbox desktop cases and all 62 services pass at ee16406; full-suite/platform acceptance remains pending. |
| `reset-zoom` | partial | Native profile zoom routed; shared hostname zoom contract remains pending. |
| `toggle-sidebar-pin` | partial | Native button, command and live settings share persistent pinning; full product/platform acceptance remains pending. |
| `toggle-split-horizontal` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `cycle-split-orientation` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `close-split-view` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `hibernate-tabs` | partial | Native retained freeze/resume and protected cases tested; remaining lifecycle/platform acceptance pending. |
| `wake-all-tabs` | partial | Native workspace-scoped resume tested; remaining lifecycle/platform acceptance pending. |
| `open-scripts-dir` | partial | Opens `<user data>/scripts`; script injection lands with the user content block. |
| `open-styles-dir` | partial | Opens `<user data>/styles`; style injection lands with the user content block. |
| `open-script-storage-dir` | partial | Opens `<user data>/script-storage`. |
| `reload-user-content` | pending | No native Fly action implementation yet. |
| `test-google-signin` | partial | Opens Google sign-in in a fresh off-the-record profile. |
| `toggle-theme` | partial | Native light/dark and website synchronization tested; custom palette remains pending. |
| `check-for-updates` | partial | Checks the latest GitHub release at start and on demand; a newer release is announced and opened on demand. No in-place install yet. |
| `open-diagnostics` | partial | Diagnostics dialog: versions, platform, workspaces and recorded events (home paths redacted), copy report, Task Manager. |
| `toggle-diagnostics-recording` | partial | Toggles in-memory recording of Fly status events (`enable_diagnostics`). |
| `go-back` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `go-forward` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `next-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `previous-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `duplicate-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `next-workspace` | partial | Native workspace routing present; complete configured input-path acceptance remains pending. |
| `previous-workspace` | partial | Native workspace routing present; complete configured input-path acceptance remains pending. |
| `new-workspace` | partial | Fresh native profile creation tested, including async retirement guards; full product acceptance remains pending. |
| `force-reload-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `stop-loading` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `zoom-in` | partial | Native profile zoom routed; shared hostname zoom contract remains pending. |
| `zoom-out` | partial | Native profile zoom routed; shared hostname zoom contract remains pending. |
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
| `email_handler` | pending | Stored and validated; Electron uses it for its page share menu, which native Fly does not have yet. |
| `search_engine` | partial | Typed app-level service and native Search preference tab; directed validation underway. Import and full acceptance remain pending. |
| `spellchecker_language` | partial | Appearance tab; applied to every workspace profile's Chromium spellcheck dictionaries. |
| `theme` | partial | Native light/dark, existing hidden/private pages tested; custom colors remain pending. |
| `interface_scale` | partial | Appearance tab; "large" scales every Fly font by 1.25. Widths do not scale. |
| `sync_website_theme` | partial | Native renderer color scheme synchronization tested without replacing page APIs. |
| `trusted_domains` | not applicable | Electron needed it for its Google sign-in workarounds; native Chromium is accepted directly. Kept for import. |
| `tab_suspend_timeout_minutes` | partial | Off and 5/15/30/60-minute native deadlines tested with retained documents and protected cases. |
| `sidebar_auto_collapse` | partial | Default enabled, native live Appearance control and recovery policy tested; import and full platform acceptance remain pending. |
| `show_status_bar` | partial | Live native footer and persistent choice tested. |
| `show_clock` | partial | Live native clock, private context and no per-tick writes tested. |
| `show_frequent_sites` | partial | Typed native choice and live Appearance control, derived ranking, bookmark exclusions and explicit hiding implemented; new runtime cases and import/platform acceptance remain pending. |
| `clock_format` | partial | Three existing formats and native preference changes tested. |
| `theme_colors` | partial | Appearance tab: accent, main, sidebar and text colour per theme; reset to defaults. |
| `enable_diagnostics` | partial | Performance tab and action; events stay in memory. |
| `show_window_controls` | partial | Follow-up native preference, preservation/policy cases and real desktop controls tested; full-suite/platform acceptance pending. |
| `custom_search_aliases` | partial | Twelve built-ins, custom override/edit/removal and native settings; recovery/policy/encoding service cases pass. UI, import and platform acceptance remain incomplete. |
| `last_seen_version` | partial | Written on the first start of a version; an upgrade opens What's New. |
| `desktop_integration_offered` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_enabled` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_by_workspace` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_bundled` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_disabled` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_unload_grace_seconds` | pending | Fly setting/import/UI behavior not implemented yet. |
| `extensions_sources` | pending | Fly setting/import/UI behavior not implemented yet. |

## Completion gates

All-platform native UI, migration, recovery, installable packages, product and
engine update rehearsals remain pending. macOS object coverage excludes the
unimplemented desktop adapter. Electron remains the production runtime.
