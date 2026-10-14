// tests/tab-jump.test.js
// Automated verification for direct tab jumping, keybind mappings, and shared shortcut grammar

const assert = require('assert');
const keybinds = require('../keybinds.js');

console.log('Running Mise direct tab jumping and cursor navigation regression tests...');

// 1. Verify jump-to-tab-1 through jump-to-tab-9 keybindings are registered
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    for (let i = 1; i <= 9; i++) {
        const action = `jump-to-tab-${i}`;
        assert(defaults[action], `${action} must be present in default keybinds`);

        const bindings = Array.isArray(defaults[action])
            ? defaults[action]
            : [defaults[action]];

        assert(bindings.includes(`Ctrl+${i}`), `${action} must include Ctrl+${i}`);
    }

    console.log('✓ Tab jump default keybinds (Ctrl+1 to Ctrl+9) registered');
}

// 2. New upstream chords must coexist with the fork's editable shortcut map.
{
    const keyboard = require('../keyboard');
    const result = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA);
    assert(result.ok, result.error);
    assert(result.config.shortcuts['focus-sidebar'].includes('Alt+S'));
    console.log('✓ Merged defaults are conflict-free and retain sidebar focus');
}

// 3. Verify action metadata
{
    const metadata = keybinds.getActionMetadata();
    for (let i = 1; i <= 9; i++) {
        const action = `jump-to-tab-${i}`;
        assert(metadata[action], `${action} metadata must exist`);
        assert.strictEqual(metadata[action].category, 'Navigation & Workspaces');
        assert(metadata[action].label.includes(`Tab ${i}`), `${action} label must reference Tab ${i}`);
    }

    console.log('✓ Tab jump action metadata verified');
}

// 4. Verify input event translation to key combination
{
    for (let i = 1; i <= 9; i++) {
        const event = {
            type: 'keyDown',
            key: String(i),
            code: `Digit${i}`,
            control: true,
            shift: false,
            alt: false,
            meta: false
        };
        const combo = require('../keyboard').eventCombo(event);
        assert.strictEqual(combo, `Ctrl+${i}`, `Input event should resolve to Ctrl+${i}`);

        const action = keybinds.getActionForInput(event);
        assert.strictEqual(action, `jump-to-tab-${i}`, `Event for Ctrl+${i} must resolve to jump-to-tab-${i}`);
    }

    console.log('✓ Input event translation and action dispatch for Ctrl+1 to Ctrl+9 verified');
}

// Exercise the production boundary and focus handling rather than a copied calculation.
{
    const fs = require('node:fs');
    const vm = require('node:vm');
    const source = fs.readFileSync(require.resolve('../modules/webview.js'), 'utf8');
    const fn = source.slice(source.indexOf('export function jumpToTabByIndex'), source.indexOf('export async function spawnNewBlankTab')).replace('export ', '');
    for (const [count, requested, expected] of [[0, 1, null], [4, 1, 0], [4, 3, 2], [4, 5, null], [4, 9, 3], [10, 9, 8], [4, 0, null]]) {
        for (const sidebarFocused of [false, true]) {
            let selected = null, focused = null;
            const items = Array.from({length: count}, (_, index) => ({setAttribute() {}, focus() { focused = index; }}));
            const context = {state: {sessionState: {current_workspace: 'Work', workspaces: {Work: items}}},
                document: {activeElement: {}, querySelectorAll: () => items, getElementById: () => ({contains: () => sidebarFocused})},
                window: {}, switchTabFocus: index => { selected = index; }, focusActiveWebview: () => { focused = 'page'; }};
            vm.runInNewContext(fn + `; jumpToTabByIndex(${requested});`, context);
            assert.equal(selected, expected);
            assert.equal(focused, expected === null ? null : sidebarFocused ? expected : 'page');
        }
    }
    console.log('✓ Production tab bounds, ninth-tab fallback and focus routing verified');
}
console.log('All direct tab jumping tests passed.');

// Closing must splice each shared session/live array exactly once.
{
    const vm = require('node:vm');
    const source = require('node:fs').readFileSync(require.resolve('../modules/webview.js'), 'utf8');
    const fn = source.slice(source.indexOf('function removeTabMetadata'), source.indexOf('export function handleTabRemoval'));
    const sleeping = ['first', 'second', 'third', null];
    const pinned = [false, true, false, false];
    const titles = ['one', 'two', 'three', 'four'];
    const state = {tabSleepStates: {Work: sleeping}, tabPinned: {Work: pinned}, activeTitlesCache: {Work: titles}, sessionState: {tab_sleep_states: {Work: sleeping}, tab_pinned: {Work: pinned}}};
    vm.runInNewContext(fn + '; removeTabMetadata("Work", 1);', {state});
    assert.deepEqual(sleeping, ['first', 'third', null]);
    assert.deepEqual(pinned, [false, false, false]);
    assert.deepEqual(titles, ['one', 'three', 'four']);
}
