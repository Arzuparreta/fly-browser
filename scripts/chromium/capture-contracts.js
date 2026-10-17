#!/usr/bin/env node
'use strict';
// Read repository code only. Never launch Electron or inspect a user profile.
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const keybinds = require('../../keybinds');
const keyboard = require('../../keyboard');

const ROOT = path.resolve(__dirname, '../..');
const read = file => fs.readFileSync(path.join(ROOT, file), 'utf8');
const sha = text => crypto.createHash('sha256').update(text).digest('hex');

function declaration(text, name, end = '\n};') {
    const marker = `const ${name} = `;
    const start = text.indexOf(marker);
    const finish = text.indexOf(end, start);
    if (start < 0 || finish < 0) throw new Error(`Cannot locate ${name}; update contract capture`);
    return text.slice(start, finish + end.length).replace(/^export /, '');
}

const sources = [
    'main.js', 'keybinds.js', 'keyboard.js', 'renderer.js', 'preload.js', 'webview-preload.js',
    'workspacePartitions.js', 'modules/state.js', 'modules/webview.js', 'modules/splitView.js',
    'modules/tabRestoration.js', 'modules/tabTransfer.js', 'modules/keyboardSettings.js',
    'modules/overlays/commandPalette.js', 'modules/overlays/dashboard.js', 'modules/overlays/bookmarks.js',
    'modules/theme.js', 'modules/clock.js', 'bookmarkStore.js', 'siteZoom.js', 'frequentSites.js',
    'userContent.js', 'extensions.js', 'extensionStore.js', 'extensionPackages.js',
    'profileMigration.js', 'diagnostics.js', 'desktopIntegration.js', 'appImageUpdate.js',
    'launch.sh', 'scripts/fly-install',
];
const contents = Object.fromEntries(sources.map(file => [file, read(file)]));
function references(term) {
    const refs = [];
    for (const [file, text] of Object.entries(contents)) {
        const line = text.split('\n').findIndex(value => value.includes(term));
        if (line !== -1) refs.push({file, line: line + 1});
    }
    return refs;
}
const defaults = vm.runInNewContext(
    declaration(contents['main.js'], 'DEFAULT_THEME_COLORS') + '\n' +
    declaration(contents['main.js'], 'DEFAULT_CONFIG') + '\nJSON.stringify(DEFAULT_CONFIG)',
    Object.create(null), {timeout: 1000});
const commands = vm.runInNewContext(
    declaration(contents['modules/overlays/commandPalette.js'], 'COMMAND_DEFINITIONS', '\n];') +
    '\nCOMMAND_DEFINITIONS.map(({id, actionId, title}) => ({id, actionId: actionId || id, title}))',
    Object.create(null), {timeout: 1000});
const testFiles = fs.readdirSync(path.join(ROOT, 'tests')).filter(file => file.endsWith('.test.js'));
function testAnchors(id) {
    return testFiles.filter(file => read('tests/' + file).includes(id)).map(file => 'tests/' + file);
}
const bindings = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA).config;
const actions = Object.entries(keybinds.ACTION_METADATA).map(([id, meta]) => ({
    id, ...meta, shortcuts: bindings.shortcuts[id] || [], normal: bindings.normal[id] || [],
    milestone: /suspend|hibernate|wake-|scripts|styles|storage|user-content|media|audio|reader|extension|private|download|signin/.test(id) ? 'M3' : 'M2',
    acceptance: /split/.test(id) ? ['A07'] : /workspace/.test(id) ? ['A02', 'A05', 'A06'] :
        /suspend|hibernate|wake-/.test(id) ? ['A10'] : /extension/.test(id) ? ['A11', 'A12'] :
        /media|audio/.test(id) ? ['A09'] : /script|style|storage|user-content/.test(id) ? ['A13'] :
        /private/.test(id) ? ['A17'] : /download|reader|signin/.test(id) ? ['A16'] :
        /bookmark|history/.test(id) ? ['A14'] : /theme|zoom|clock/.test(id) ? ['A15'] : ['A02', 'A03', 'A04'],
    source: references(id), baselineTestAnchors: testAnchors(id), nativeStatus: 'pending', nativeEvidence: [],
}));
const preferences = Object.entries(JSON.parse(defaults)).map(([id, value]) => ({
    id, default: value, source: references(id), baselineTestAnchors: testAnchors(id),
    milestone: id.startsWith('extensions_') ? 'M3' : 'M2',
    acceptance: id.startsWith('extensions_') ? ['A11', 'A12', 'A19'] :
        id === 'trusted_domains' ? ['A17'] : id === 'tab_suspend_timeout_minutes' ? ['A10'] :
        id === 'desktop_integration_offered' ? ['A21'] : ['A15', 'A19'],
    nativeStatus: 'pending', nativeEvidence: [],
}));
preferences.push({id: 'extensions_sources', default: {}, source: references('extensions_sources'),
    baselineTestAnchors: testAnchors('extensions_sources'), milestone: 'M3',
    acceptance: ['A11', 'A19'], nativeStatus: 'pending', nativeEvidence: [],
    note: 'Written by the extension installer, absent from DEFAULT_CONFIG'});
const sessionFields = [...new Set(Object.values(contents).flatMap(text =>
    [...text.matchAll(/(?:sessionState|sessionData|freshSession)\.([a-z_]+)/g)].map(match => match[1])))].sort();
const persistence = [
    {id: 'config.json', root: 'legacy-config', contract: 'Defaults, explicit choices, unknown fields; tab_sleep_timeout_minutes is a legacy alias', fields: preferences.map(p => p.id)},
    {id: 'session.json', root: 'legacy-config', contract: 'Workspace URL arrays, stable partition identity and parallel tab metadata', fields: sessionFields},
    {id: 'session.json.bak', root: 'legacy-config', contract: 'Rolling session recovery journal'},
    {id: 'session_sentinel.json', root: 'legacy-config', contract: 'clean_exit and crash recovery; never treat as a clean session snapshot'},
    {id: 'bookmarks.json', root: 'legacy-config', contract: 'App-wide bookmarks, folders, order, quickmarks and unknown item fields'},
    {id: 'history.json', root: 'legacy-config', contract: 'App-wide visits, titles, timestamps; private visits excluded'},
    {id: 'keybinds.json', root: 'legacy-config', contract: 'keybinds.resolveKeybinds: legacy and versioned changesOnly inputs; explicit empty bindings retained'},
    {id: 'site-zoom.json', root: 'legacy-config', contract: 'Hostname levels; private zoom memory-only'},
    {id: 'hidden-sites.json', root: 'legacy-config', contract: 'Hidden frequent-site origins, distinct from history/bookmarks'},
    {id: 'scripts/', root: 'legacy-config', contract: 'Local user-script source, grants, matching and run timing'},
    {id: 'styles/', root: 'legacy-config', contract: 'Local CSS source and matching'},
    {id: 'script-storage/', root: 'legacy-config', contract: 'Per-script JSON; validate filename boundaries'},
    {id: 'extensions/', root: 'legacy-config', contract: 'Packages and manifests; source/identity selections live in config.extensions_sources and config.extensions_bundled'},
    {id: 'Partitions/', root: 'electron-user-data', contract: 'Cookies, local/session storage, IndexedDB, workers, extension data; format-aware import only'},
    {id: 'Local Storage/', root: 'electron-user-data', contract: 'Host UI mise_recent_commands; reader __mise_reader_prefs__ is website-origin storage within its partition; no raw LevelDB copy'},
];
const features = [
    ['Native launch, URL routing, single instance', 'M1/M5', ['A01', 'A21'], ['main.js', 'launch.sh', 'desktopIntegration.js'], ['launcher', 'desktop-integration']],
    ['Tab creation, ordering, pins, close/reopen, numbering and context menus', 'M2', ['A02'], ['modules/webview.js', 'modules/tabActions.js', 'tabNumbers.js', 'tabContextMenu.js'], ['pinned-tabs', 'tab-context-menu', 'tab-numbers', 'tab-restoration-ui']],
    ['Workspace create, rename, delete and stable identity', 'M1/M2', ['A05'], ['workspacePartitions.js', 'modules/overlays/dashboard.js'], ['workspace-partitions', 'workspace-partitions-ui']],
    ['Cross-workspace tab moves and in-flight cancellation', 'M1/M2', ['A06'], ['modules/tabTransfer.js', 'modules/tabActions.js'], ['workspace-partitions-ui']],
    ['Split orientations, pane focus, swap and close', 'M1/M2', ['A07'], ['modules/splitView.js'], ['split-view-ui']],
    ['NORMAL/INSERT/PASSTHROUGH, hints, IME/AltGr and recorder', 'M2', ['A03'], ['keybinds.js', 'keyboard.js', 'hinter.js', 'modules/keyboardSettings.js'], ['keyboard', 'keyboard-ui', 'legacy-keybinds-ui']],
    ['Sidebar expansion, pointer/focus, resize, high DPI and window controls', 'M2', ['A04'], ['renderer.js', 'modules/sidebarPointer.js', 'modules/windowControls.js', 'modules/focus.js'], ['sidebar-pointer', 'collapsed-sidebar-ui', 'interface-scale-ui']],
    ['Address bar, aliases, suggestions and calculator', 'M2', ['A14'], ['modules/navigation.js', 'modules/overlays/searchEngine.js', 'modules/calculator.js'], ['search-aliases', 'address-bar-dismiss-ui', 'empty-search-ui']],
    ['Palette, dashboard, overlay cancellation, focus and accessibility', 'M2', ['A03', 'A04'], ['modules/overlays/commandPalette.js', 'modules/overlays/dashboard.js', 'modules/focus.js'], ['overlay-state', 'overlay-state-ui', 'navigation-focus-ui']],
    ['New tab, bookmarks/folders, quickmarks, history, hiding frequent sites', 'M2', ['A14'], ['assets/new-tab.js', 'bookmarkStore.js', 'frequentSites.js', 'modules/overlays/bookmarks.js', 'modules/overlays/history.js'], ['bookmark-store', 'bookmarks-ui', 'frequent-sites', 'new-tab-bookmarks-ui']],
    ['Settings navigation, search, live preferences and atomic writes', 'M2', ['A15', 'A19'], ['main.js', 'modules/settingsNav.js', 'modules/settingsTab.js'], ['config-writes', 'settings-nav-ui', 'settings-tabs-ui']],
    ['Website theme, custom palette, UI scale, hostname zoom and clock', 'M2', ['A15'], ['modules/theme.js', 'siteZoom.js', 'modules/clock.js'], ['site-zoom', 'clock', 'website-theme-ui', 'interface-scale-ui']],
    ['Native visibility and media, paused/playing, mute and Pie reproduction', 'M1/M3', ['A08', 'A09'], ['modules/webview.js', 'main.js'], ['audio-controls']],
    ['Freeze/resume eligibility, memory discard distinction and protection', 'M3', ['A10'], ['tabSuspension.js', 'modules/tabSuspension.js'], ['tab-suspension', 'tab-suspension-ui']],
    ['Extensions, store/ZIP/CRX/unpacked, update, options/popups and native IDs', 'M1/M3', ['A11'], ['extensions.js', 'extensionStore.js', 'extensionPackages.js', 'modules/extensions.js'], ['extensions', 'extension-store', 'extension-packages', 'extension-install-ui']],
    ['Maintained native-rule blocker, restore ordering, exceptions and choices', 'M3/M4', ['A12', 'A19'], ['extensions.js', 'assets/extensions/ublock-origin/manifest.json'], ['blocking-startup-ui', 'extensions-ui']],
    ['Local scripts/styles, matching, grants, per-script storage and hot reload', 'M3', ['A13'], ['userContent.js', 'modules/userScriptShim.js'], []],
    ['PDF URL/MIME handling, reader, print and downloads', 'M3', ['A16'], ['main.js', 'modules/utils.js', 'modules/readerDistiller.js'], []],
    ['OAuth, popups and origin/profile routing', 'M3', ['A16', 'A17'], ['main.js', 'security.js'], ['auth-security', 'oauth-popup-ui']],
    ['Permissions, private mode, internal page capability boundaries and vault exclusion', 'M3/M4', ['A17', 'A19'], ['main.js', 'preload.js', 'security.js'], ['auth-security']],
    ['Clipboard-mediated terminal, external schemes and hostile paths', 'M3', ['A18'], ['modules/overlays/commandPalette.js', 'main.js', 'closedOutput.js'], ['closed-output', 'auth-security']],
    ['Migration, unknown fields, interruption/retry, rollback and recovery', 'M4', ['A19', 'A20'], ['profileMigration.js', 'main.js', 'modules/tabRestoration.js'], ['profile-migration', 'config-writes', 'tab-restoration-ui']],
    ['Diagnostics and distinct product/engine/build identity', 'M2/M5', ['A15', 'A21'], ['diagnostics.js', 'modules/overlays/diagnostics.js'], ['identity-ui']],
    ['Packages, launcher/installer/AUR, verified updates and failure recovery', 'M5', ['A21'], ['scripts/fly-install', 'scripts/aur.js', 'appImageUpdate.js', 'desktopIntegration.js'], ['fly-install', 'aur-package', 'appimage-update', 'desktop-integration']],
    ['Clean rebuild, patch provenance, licences and engine update rehearsal', 'M0/M5', ['A22'], ['scripts/chromium/engine.py', 'scripts/release.js'], ['release-tooling']],
].map(([id, milestone, acceptance, source, tests]) => ({id, milestone, acceptance, source,
    baselineTestAnchors: tests.map(name => 'tests/' + name + '.test.js'), nativeStatus: 'pending', nativeEvidence: []}));

function capture() {
    for (const feature of features) for (const file of [...feature.source, ...feature.baselineTestAnchors]) {
        if (!fs.existsSync(path.join(ROOT, file))) throw new Error(`Missing feature anchor: ${file}`);
    }
    return {
        schema: 1,
        baseline: 'a477efd0cd47c24d8053ce2be013446054268fe9',
        warning: 'Repository contract inventory. Source/test anchors are discovery aids, not proof of behavioural coverage or native acceptance.',
        sourceDigests: Object.fromEntries(sources.map(file => [file, sha(contents[file])])),
        actions, paletteEntries: JSON.parse(JSON.stringify(commands)), tabSelectionModifier: bindings.tabs, features,
        preferences, persistence: persistence.map(item => ({...item, milestone: 'M4', acceptance: ['A19', 'A20'], nativeStatus: 'pending', nativeEvidence: []})),
    };
}

function markdown(data) {
    const link = anchor => `[${anchor.file}:${anchor.line}](https://github.com/Arzuparreta/fly-browser/blob/${data.baseline}/${anchor.file}#L${anchor.line})`;
    const rows = data.actions.map(item => `| \`${item.id}\` | ${item.shortcuts.join(', ') || '—'} | ${item.normal.join(', ') || '—'} | ${item.milestone} / ${item.acceptance.join(', ')} | pending | ${link(item.source[0])} |`);
    const prefs = data.preferences.map(item => `| \`${item.id}\` | \`${JSON.stringify(item.default).replaceAll('|', '&#124;')}\` | ${item.milestone} / ${item.acceptance.join(', ')} | pending |`);
    return `# Baseline command, preference and data contracts

Generated by \`node scripts/chromium/capture-contracts.js\` from repository
loaders and catalogues. No user data is read. The machine-readable companion
is \`chromium/baseline-contracts.json\`, including source hashes and test discovery
anchors. These anchors do not prove coverage. Native implementation and observed
acceptance evidence must be recorded separately in the implementation record.

## Product features

The listed Electron tests are baseline discovery anchors; an empty list is a
coverage gap, not permission to omit the behaviour. Native evidence is pending
for every row. The matrix in the approved plan defines the required outcomes.

| Feature | Destination / acceptance | Current source | Native |
| --- | --- | --- | --- |
${data.features.map(item => `| ${item.id} | ${item.milestone} / ${item.acceptance.join(', ')} | ${item.source.map(file => `\`${file}\``).join(', ')} | pending |`).join('\n')}

## Actions

| Action | Browser defaults | Page defaults | Destination / acceptance | Native | Source |
| --- | --- | --- | --- | --- | --- |
${rows.join('\n')}

Tab selection uses \`${data.tabSelectionModifier}\` with 1–9. Page modes,
700 ms sequences, editable/shadow/iframe boundaries, hints, passthrough, IME and
AltGr are part of A03, including currently unbound actions.

## Palette catalogue

The palette exposes all catalogue actions dynamically and these explicit entries:

| Entry | Action |
| --- | --- |
${data.paletteEntries.map(item => `| \`${item.id}\` | \`${item.actionId}\` |`).join('\n')}

Extension-generated commands and the clipboard-mediated terminal fallback are
dynamic; cover them with A11 and A18. All explicit palette entries and catalogue
actions need native implementations, including actions unbound by default.

## Preferences

Nested theme palette defaults are included in full. Unknown fields and the
legacy \`tab_sleep_timeout_minutes\` alias need explicit migration handling.

| Preference | Baseline default | Destination / acceptance | Native |
| --- | --- | --- | --- |
${prefs.join('\n')}

## Persisted inputs

Two independently located roots are required: \`legacy-config\` is derived from
the current loader's home configuration path; \`electron-user-data\` is resolved
by Electron per platform. Never assume they coincide. Neither root has been read.

| Input | Root | Contract | Native import |
| --- | --- | --- | --- |
${data.persistence.map(item => `| \`${item.id}\` | ${item.root} | ${item.contract} | pending |`).join('\n')}

Observed session fields: ${sessionFields.map(field => `\`${field}\``).join(', ')}.
Additional nested and unknown fields must be preserved in the import record.
Split states currently live in renderer state; verify observed restart behaviour
before promising a legacy split-layout import. Closed-tab recovery is memory-only.
`;
}

function main(check) {
    const data = capture();
    for (const [file, value] of [
        ['chromium/baseline-contracts.json', JSON.stringify(data, null, 2) + '\n'],
        ['docs/chromium/parity-ledger.md', markdown(data)],
    ]) {
        if (check) {
            if (read(file) !== value) throw new Error(`Contract inventory drift: run node scripts/chromium/capture-contracts.js (${file})`);
        } else fs.writeFileSync(path.join(ROOT, file), value);
    }
    console.log(`Chromium baseline: ${actions.length} actions, ${commands.length} palette entries, ${preferences.length} preferences, ${persistence.length} persisted inputs`);
}

if (require.main === module) main(process.argv.includes('--check'));
module.exports = {capture};
