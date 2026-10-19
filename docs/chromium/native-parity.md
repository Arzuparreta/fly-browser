# Native action and preference parity

This reviewed inventory complements the generated [baseline ledger](./parity-ledger.md).
It describes native implementation, not release readiness. Every row remains
partial or pending until the relevant complete acceptance outcomes pass.

Accepted Linux evidence through `bbb8a09` is archived in
`/mnt/storage/fly-chromium/evidence/runtime-bbb8a09-before-window-controls`:
59 service tests, 71 browser declarations including two PRE stages, both media
fixtures and independent audit pass. PR 126 hosted validation is still pending.
The window-control checkpoint `5d55d9a` passes 62 services, all 74 browser
declarations, both media fixtures and independent audit on Linux. The sidebar
follow-up passes 65 services and 20/21 directed stages at `c536b6e`. The remaining
pointer-preferences focus case is fixed at `761b5bd`, with all four affected
directed cases passing together. Full-suite/platform checks remain pending.

Action routes live in `chromium/overlay/fly/ui/native_commands.cc` and
`ui/fly_window_controller.cc`. Tests are in `test/workspace_browser_test.cc`;
keyboard/core contracts also have dedicated `*_unittest.cc` files.
A native upstream route alone does not establish Fly UX parity.

## Actions

| Action | Status | Implementation and remaining acceptance |
| --- | --- | --- |
| `toggle-menu-bar` | partial | Native Chromium app menu opens; the complete Fly settings menu remains pending. |
| `spawn-tab` | partial | Product blank tabs and native profile/session identity tested; complete all-input-path acceptance remains pending. |
| `toggle-address` | partial | Native upstream location bar only; floating Fly UI, aliases, calculator and suggestion contracts remain pending. |
| `toggle-dashboard` | partial | Native searchable workspace/private/empty desktop picker tested; full dashboard product layout remains pending. |
| `reload-active-tab` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `remove-tab` | partial | Pinned protection, beforeunload, close/reopen and native menu tested; complete all-input-path acceptance remains pending. |
| `focus-sidebar` | partial | Native panel focus and Escape back to editable page tested; complete accessibility, all-input-path and platform acceptance remains pending. |
| `focus-nav-buttons` | partial | Native toolbar focus; final Fly navigation control layout remains pending. |
| `focus-webview` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `trigger-hints` | partial | Native hint input/activation and visibility tested; complete document/platform coverage remains pending. |
| `toggle-help` | partial | Native preferences/keyboard editor implemented; not the complete product help surface. |
| `toggle-private-mode` | partial | Native OTR profiles, exit cancellation and isolation tested; complete private services/recovery acceptance remains pending. |
| `toggle-palette` | partial | Native search, keyboard customization, cancellation and profile routing tested; remaining actions pending. |
| `toggle-find` | partial | Native command routed; complete pointer/keyboard find-bar acceptance pending. |
| `toggle-devtools` | partial | Selected-profile docked native DevTools tested; full platform acceptance remains pending. |
| `toggle-zen-mode` | partial | Toggles native automatic expansion/pinning without hover viewport reflow; full product/platform acceptance remains pending. |
| `add-bookmark` | partial | Shared ordered flat Fly bookmarks tested; complete product/migration acceptance remains pending. |
| `toggle-bookmarks` | partial | Shared ordered flat Fly bookmarks; private/destination identity and deletion tested. Frequent-site integration remains pending. |
| `toggle-global-media` | partial | Native sessions plus fallback players tested across hidden workspaces; full platform acceptance remains pending. |
| `toggle-tab-audio-mute` | partial | Native mute and extension reason reporting tested; full platform acceptance remains pending. |
| `toggle-global-audio-mute` | partial | Native mute reasons and hidden/private tabs tested; full platform acceptance remains pending. |
| `toggle-history` | pending | No native Fly action implementation yet. |
| `toggle-downloads` | pending | No native Fly action implementation yet. |
| `toggle-status-bar` | partial | Native hover, security/private context and persistence tested; full platform acceptance remains pending. |
| `toggle-clock` | partial | Native live settings and no tick writes tested on Linux; full platform acceptance remains pending. |
| `cycle-clock-format` | partial | Native formatting/settings persistence tested; full platform acceptance remains pending. |
| `toggle-split` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `switch-split-focus` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `swap-split-panes` | partial | Native split API routed; both-pane visibility/basic focus tested. All orientations/input paths, layout restore and platform acceptance remain pending. |
| `toggle-reader-view` | pending | No native Fly action implementation yet. |
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
| `open-scripts-dir` | pending | No native Fly action implementation yet. |
| `open-styles-dir` | pending | No native Fly action implementation yet. |
| `open-script-storage-dir` | pending | No native Fly action implementation yet. |
| `reload-user-content` | pending | No native Fly action implementation yet. |
| `test-google-signin` | pending | No native Fly action implementation yet. |
| `toggle-theme` | partial | Native light/dark and website synchronization tested; custom palette remains pending. |
| `check-for-updates` | pending | No native Fly action implementation yet. |
| `open-diagnostics` | pending | No native Fly action implementation yet. |
| `toggle-diagnostics-recording` | pending | No native Fly action implementation yet. |
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
| `show-whats-new` | pending | No native Fly action implementation yet. |
| `toggle-native-menu-visibility` | pending | No native Fly action implementation yet. |
| `move-tab-to-workspace` | partial | Five Linux browser cases pass at bbb8a09; crash recovery and all-platform UI acceptance remain pending. |
| `open-keyboard-settings` | partial | Native recorder/drafts/conflict/cancellation tested; IME/AltGr/platform acceptance remains pending. |
| `reopen-closed-tab` | partial | Native historical restore and Fly identity tested; browser crash recovery remains pending. |
| `move-tab-left` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `move-tab-right` | partial | Native command route exists; dedicated full keyboard, mouse and palette acceptance remains pending. |
| `toggle-fullscreen` | partial | Native command routed; full desktop/page fullscreen and restore acceptance pending. |
| `print-page` | partial | Native command routed; print/PDF dialog acceptance pending. |
| `toggle-notifications` | pending | No native Fly action implementation yet. |

## Preferences

Implemented groups use `preferences/preferences_service.{h,cc}`, preserving
unknown metadata and rejecting malformed/future stores. The native settings UI
lives in `ui/keyboard_editor.cc`. Unimplemented preferences are not silently
declared supported because Chromium has a related global flag or policy.

| Preference | Status | Implementation and remaining acceptance |
| --- | --- | --- |
| `disable_gpu` | pending | Fly setting/import/UI behavior not implemented yet. |
| `background_throttling` | pending | Fly setting/import/UI behavior not implemented yet. |
| `process_limit` | pending | Fly setting/import/UI behavior not implemented yet. |
| `email_handler` | pending | Fly setting/import/UI behavior not implemented yet. |
| `search_engine` | pending | Fly setting/import/UI behavior not implemented yet. |
| `spellchecker_language` | pending | Fly setting/import/UI behavior not implemented yet. |
| `theme` | partial | Native light/dark, existing hidden/private pages tested; custom colors remain pending. |
| `interface_scale` | pending | Fly setting/import/UI behavior not implemented yet. |
| `sync_website_theme` | partial | Native renderer color scheme synchronization tested without replacing page APIs. |
| `trusted_domains` | pending | Fly setting/import/UI behavior not implemented yet. |
| `tab_suspend_timeout_minutes` | partial | Off and 5/15/30/60-minute native deadlines tested with retained documents and protected cases. |
| `sidebar_auto_collapse` | partial | Default enabled, native live Appearance control and recovery policy tested; import and full platform acceptance remain pending. |
| `show_status_bar` | partial | Live native footer and persistent choice tested. |
| `show_clock` | partial | Live native clock, private context and no per-tick writes tested. |
| `show_frequent_sites` | pending | Fly setting/import/UI behavior not implemented yet. |
| `clock_format` | partial | Three existing formats and native preference changes tested. |
| `theme_colors` | pending | Fly setting/import/UI behavior not implemented yet. |
| `enable_diagnostics` | pending | Fly setting/import/UI behavior not implemented yet. |
| `show_window_controls` | partial | Follow-up native preference, preservation/policy cases and real desktop controls tested; full-suite/platform acceptance pending. |
| `custom_search_aliases` | pending | Fly setting/import/UI behavior not implemented yet. |
| `last_seen_version` | pending | Fly setting/import/UI behavior not implemented yet. |
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
