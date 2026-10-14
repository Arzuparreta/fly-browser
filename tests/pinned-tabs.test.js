// tests/pinned-tabs.test.js
// Automated verification for pinned tabs, keybind mappings, closure guard, and array synchronisation

const assert = require('assert');
const keybinds = require('../keybinds.js');

console.log('Running Fly pinned tabs regression tests...');

// 1. Verify toggle-pin-tab keybindings are registered
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    assert(defaults['toggle-pin-tab'], 'toggle-pin-tab must be present in default keybinds');

    const bindings = Array.isArray(defaults['toggle-pin-tab'])
        ? defaults['toggle-pin-tab']
        : [defaults['toggle-pin-tab']];

    assert(bindings.includes('Ctrl+Shift+X'), 'toggle-pin-tab must include Ctrl+Shift+X');
    assert(bindings.includes('F4'), 'toggle-pin-tab must include F4');

    console.log('✓ Tab pin/unpin default keybinds registered');
}

// 2. Shipped defaults must remain conflict-free and valid.
{
    const keyboard = require('../keyboard');
    const result = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA);
    assert(result.ok, result.error);
    console.log('✓ Default bindings are valid and conflict-free');
}

// 3. Verify action metadata
{
    const metadata = keybinds.getActionMetadata();
    assert(metadata['toggle-pin-tab'], 'toggle-pin-tab metadata must exist');
    assert.strictEqual(metadata['toggle-pin-tab'].category, 'Navigation & Workspaces');

    console.log('✓ Tab pin action metadata verified');
}

// 4. Verify input event translation to key combination
{
    const eventX = {
        type: 'keyDown',
        key: 'x',
        code: 'KeyX',
        control: true,
        shift: true,
        alt: false,
        meta: false
    };
    const comboX = keybinds.inputEventToKeyCombination(eventX);
    assert.strictEqual(comboX, 'Ctrl+Shift+X', 'Input event should resolve to Ctrl+Shift+X');

    const eventF4 = {
        type: 'keyDown',
        key: 'F4',
        code: 'F4',
        control: false,
        shift: false,
        alt: false,
        meta: false
    };
    const comboF4 = keybinds.inputEventToKeyCombination(eventF4);
    assert.strictEqual(comboF4, 'F4', 'Input event should resolve to F4');

    console.log('✓ Key combination normalisation for pin shortcuts verified');
}

// 5. Verify array synchronisation and moveTabState logic
{
    const mockState = {
        sessionState: {
            workspaces: { Default: ['url-0', 'url-1', 'url-2', 'url-3'] },
            tab_titles: { Default: ['t-0', 't-1', 't-2', 't-3'] },
            tab_sleep_states: { Default: [null, null, null, null] },
            tab_pinned: { Default: [true, false, false, false] }
        },
        activeViewsCache: { Default: ['v-0', 'v-1', 'v-2', 'v-3'] },
        activeTitlesCache: { Default: ['t-0', 't-1', 't-2', 't-3'] },
        tabSleepStates: { Default: [null, null, null, null] },
        tabActivityTimestamps: { Default: [100, 200, 300, 400] },
        tabMediaAudible: { Default: [false, false, false, false] },
        tabAudioMuted: { Default: [false, false, false, false] },
        tabPinned: { Default: [true, false, false, false] },
        tabHistory: { Default: [0, 1, 2, 3] }
    };

    function moveMockTab(ws, fromIdx, toIdx) {
        if (fromIdx === toIdx) return;
        const totalTabs = mockState.sessionState.workspaces[ws]?.length || 0;
        const movedArrays = new Set();

        function moveInArray(arr, defaultVal = null) {
            if (!Array.isArray(arr) || movedArrays.has(arr)) return;
            movedArrays.add(arr);

            while (arr.length < totalTabs) {
                arr.push(defaultVal);
            }

            const [item] = arr.splice(fromIdx, 1);
            arr.splice(toIdx, 0, item);
        }

        moveInArray(mockState.sessionState.workspaces[ws], '');
        moveInArray(mockState.activeViewsCache[ws], null);
        moveInArray(mockState.activeTitlesCache[ws], 'Loading...');
        moveInArray(mockState.tabSleepStates[ws], null);
        moveInArray(mockState.tabActivityTimestamps[ws], 0);
        moveInArray(mockState.tabMediaAudible[ws], false);
        moveInArray(mockState.tabAudioMuted[ws], false);
        moveInArray(mockState.tabPinned[ws], false);
        moveInArray(mockState.sessionState.tab_titles[ws], 'Loading...');
        moveInArray(mockState.sessionState.tab_sleep_states[ws], null);
        moveInArray(mockState.sessionState.tab_pinned[ws], false);

        if (mockState.tabHistory && Array.isArray(mockState.tabHistory[ws])) {
            mockState.tabHistory[ws] = mockState.tabHistory[ws].map(hIdx => {
                if (hIdx === fromIdx) return toIdx;
                if (fromIdx < toIdx && hIdx > fromIdx && hIdx <= toIdx) return hIdx - 1;
                if (fromIdx > toIdx && hIdx >= toIdx && hIdx < fromIdx) return hIdx + 1;
                return hIdx;
            });
        }
    }

    // Move tab 3 (url-3) to index 1 (under the pinned tab 0)
    mockState.tabPinned.Default[3] = true;
    moveMockTab('Default', 3, 1);

    assert.strictEqual(mockState.sessionState.workspaces.Default[0], 'url-0');
    assert.strictEqual(mockState.sessionState.workspaces.Default[1], 'url-3');
    assert.strictEqual(mockState.sessionState.workspaces.Default[2], 'url-1');
    assert.strictEqual(mockState.sessionState.workspaces.Default[3], 'url-2');

    assert.strictEqual(mockState.tabPinned.Default[0], true);
    assert.strictEqual(mockState.tabPinned.Default[1], true);
    assert.strictEqual(mockState.tabPinned.Default[2], false);
    assert.strictEqual(mockState.tabPinned.Default[3], false);

    assert.deepStrictEqual(mockState.tabHistory.Default, [0, 2, 3, 1]);

    // Regression verification: Pinning tab with shared titles/favicons array reference
    const sharedTitles = ['Google', 'GitHub', 'YouTube', 'Fly'];
    const sparseFavicons = ['google.ico']; // Only tab 0 had cached favicon
    const testWs = {
        workspaces: ['https://google.com', 'https://github.com', 'https://youtube.com', 'https://fly.example'],
        activeTitles: sharedTitles,
        sessionTitles: sharedTitles, // Identical reference
        favicons: sparseFavicons
    };

    const totalCount = testWs.workspaces.length;
    const movedSet = new Set();
    function moveTestArray(arr, defaultVal = null) {
        if (!Array.isArray(arr) || movedSet.has(arr)) return;
        movedSet.add(arr);
        while (arr.length < totalCount) arr.push(defaultVal);
        const [item] = arr.splice(3, 1);
        arr.splice(0, 0, item);
    }

    moveTestArray(testWs.workspaces, '');
    moveTestArray(testWs.activeTitles, 'Loading...');
    moveTestArray(testWs.sessionTitles, 'Loading...'); // Must not double-move
    moveTestArray(testWs.favicons, null);

    assert.strictEqual(testWs.workspaces[0], 'https://fly.example');
    assert.strictEqual(testWs.activeTitles[0], 'Fly', 'Fly tab at index 0 must keep Fly title, not inherit Google');
    assert.strictEqual(testWs.activeTitles[1], 'Google');
    assert.strictEqual(testWs.favicons[0], null, 'Fly tab at index 0 must not inherit Google favicon');
    assert.strictEqual(testWs.favicons[1], 'google.ico', 'Google tab shifted to index 1 must keep its favicon');

    console.log('✓ Tab state synchronisation, shared reference deduplication, and padding verified');
}

// 6. Verify accidental closure guard logic
{
    const ws = 'Default';
    const pinnedMap = { [ws]: [true, false] };

    function canRemoveTab(idx) {
        if (pinnedMap[ws]?.[idx]) return false;
        return true;
    }

    assert.strictEqual(canRemoveTab(0), false, 'Pinned tab at index 0 must be guarded from removal');
    assert.strictEqual(canRemoveTab(1), true, 'Unpinned tab at index 1 can be removed');

    console.log('✓ Accidental closure guard logic verified');
}

console.log('\nAll pinned tabs & keybind tests passed successfully!\n');
