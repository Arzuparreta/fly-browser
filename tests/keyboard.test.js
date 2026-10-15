const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const keyboard = require('../keyboard');
const keybinds = require('../keybinds');
const clone = value => JSON.parse(JSON.stringify(value));
const defaults = keyboard.validateConfig(keybinds.getDefaults(), keybinds.ACTION_METADATA).config;
assert(defaults, 'Every shipped default must be valid and conflict-free');

assert.equal(keyboard.normalise('control+shift+a'), 'Ctrl+Shift+A');
assert.equal(keyboard.normalise('Meta+Ctrl+return'), 'Ctrl+Meta+Enter');
assert.equal(keyboard.normalise('Ctrl++'), 'Ctrl+Plus');
assert.equal(keyboard.normalise('Ctrl+Shift+Plus'), 'Ctrl+Plus');
assert.equal(keyboard.normalise('Ctrl+A+B'), '');
assert.equal(keyboard.normalise('Ctrl+Ctrl+A'), '');
assert.equal(keyboard.eventCombo({key: '+', ctrlKey: true, shiftKey: true}), 'Ctrl+Plus');
assert.equal(keyboard.eventCombo({key: ')', code: 'Digit0', ctrlKey: true, shiftKey: true}), 'Ctrl+Shift+0');
assert.equal(keyboard.eventCombo({key: 'ñ', code: 'Semicolon', ctrlKey: true}), 'Ctrl+Ñ');
assert.equal(keyboard.eventCombo({key: 'Dead', ctrlKey: true}), '');
assert.equal(keyboard.eventCombo({key: 'a', isComposing: true}), '');
assert.equal(keyboard.eventCombo({key: '@', ctrlKey: true, altKey: true, getModifierState: key => key === 'AltGraph'}), '');

for (const mutate of [
    config => { config.shortcuts['spawn-tab'] = 'Ctrl+W'; },
    config => { config.normal['scroll-up'] = 'G'; },
    config => { config.normal['spawn-tab'] = 'Ctrl+T'; },
    config => { config.shortcuts['spawn-tab'] = 'A'; },
    config => { config.normal['scroll-down'] = 'Escape'; },
    config => { config.shortcuts.fake = 'Ctrl+A'; },
    config => { config.shortcuts['spawn-tab'] = [42]; }
]) {
    const config = clone(defaults); mutate(config);
    assert.equal(keyboard.validateConfig(config, keybinds.ACTION_METADATA).ok, false);
}

const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-keyboard-test-'));
try {
    const legacyFile = path.join(directory, 'keybinds.json');
    fs.writeFileSync(legacyFile, JSON.stringify({
        'focus-webview': 'Ctrl+Shift+Y', 'remove-tab': 'Ctrl+Q', 'focus-nav-buttons': ['Ctrl+Tab'],
        'reload-active-tab': '', 'trigger-hints': ['Ctrl+F', 'Ctrl+G']
    }));
    keybinds.initializeKeybinds(directory);
    assert.deepEqual(keybinds.getKeybinds()['focus-webview'], ['Ctrl+Shift+Y']);
    assert.deepEqual(keybinds.getKeybinds()['reload-active-tab'], []);
    assert(!keybinds.getKeybinds()['next-tab'].includes('Ctrl+Tab'), 'New defaults cannot steal legacy bindings');
    assert(!keybinds.getKeybinds()['toggle-find'].includes('Ctrl+F'));

    // Pre-version-2 Fly seeded keybinds.json with its whole default table on first launch.
    // Those seeded values are not user choices and must not pin obsolete defaults.
    const seeded = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'upstream-keybinds-2f8ca5f.json'), 'utf8'));
    fs.writeFileSync(legacyFile, JSON.stringify(seeded));
    keybinds.initializeKeybinds(directory);
    assert.deepEqual(keybinds.getKeyboardConfig(), defaults, 'An untouched upstream profile receives the current defaults');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: 'Tab', control: true}), 'next-tab');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: 'w', control: true}), 'remove-tab');
    fs.writeFileSync(legacyFile, JSON.stringify({...seeded, 'toggle-dark-reader-domain': 'Alt+Shift+E', 'focus-sidebar': 'Ctrl+M',
        'toggle-find': ['F3', 'Ctrl+Shift+F'], 'remove-tab': 'Ctrl+Q', 'focus-nav-buttons': 'Ctrl+Tab', 'toggle-bookmarks': ''}));
    keybinds.initializeKeybinds(directory);
    const mixed = keybinds.getKeyboardConfig();
    assert.equal(mixed.warning, undefined, 'Seeded values of retired actions do not reject the profile');
    assert.deepEqual(mixed.shortcuts['focus-sidebar'], defaults.shortcuts['focus-sidebar'], 'Older seeded defaults are recognised');
    assert.deepEqual(mixed.shortcuts['toggle-find'], defaults.shortcuts['toggle-find'], 'Reordered seeded chords remain defaults');
    assert.deepEqual(mixed.shortcuts['remove-tab'], ['Ctrl+Q']);
    assert.deepEqual(mixed.shortcuts['focus-nav-buttons'], ['Ctrl+Tab'], 'A changed seeded value is a user choice');
    assert(!mixed.shortcuts['next-tab'].includes('Ctrl+Tab'));
    assert.deepEqual(mixed.shortcuts['toggle-bookmarks'], [], 'Disabling a seeded binding is a user choice');

    // Saves store only changes, so later default improvements still reach the profile.
    const stored = () => JSON.parse(fs.readFileSync(legacyFile, 'utf8'));
    assert.equal(keybinds.saveKeybinds(directory, defaults).ok, true);
    assert.deepEqual(stored(), {version: 2, changesOnly: true, shortcuts: {}, normal: {}});
    const disabled = clone(defaults); disabled.shortcuts['reload-active-tab'] = [];
    disabled.normal['scroll-down'] = ['Z', 'ArrowDown'];
    assert.equal(keybinds.saveKeybinds(directory, disabled).ok, true);
    assert.deepEqual(stored(), {version: 2, changesOnly: true, shortcuts: {'reload-active-tab': []}, normal: {'scroll-down': ['Z', 'ArrowDown']}});
    const previousBookmarks = keybinds.DEFAULT_KEYBINDS['toggle-bookmarks'];
    try {
        keybinds.DEFAULT_KEYBINDS['toggle-bookmarks'] = 'Ctrl+Alt+B';
        keybinds.initializeKeybinds(directory);
        assert.deepEqual(keybinds.getKeybinds()['toggle-bookmarks'], ['Ctrl+Alt+B'], 'A changed default reaches a saved profile');
        // Complete tables saved by earlier version 2 releases pinned every default.
        fs.copyFileSync(path.join(__dirname, 'fixtures', 'fork-keybinds-079b538.json'), legacyFile);
        keybinds.initializeKeybinds(directory);
        assert.deepEqual(keybinds.getKeybinds()['toggle-bookmarks'], ['Ctrl+Alt+B'], 'A changed default reaches a complete-table profile');
        assert.deepEqual({...keybinds.getKeyboardConfig(), shortcuts: {...keybinds.getKeyboardConfig().shortcuts, 'toggle-bookmarks': defaults.shortcuts['toggle-bookmarks']}}, defaults);
    } finally { keybinds.DEFAULT_KEYBINDS['toggle-bookmarks'] = previousBookmarks; }
    const complete = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'fork-keybinds-079b538.json'), 'utf8'));
    complete.shortcuts['remove-tab'] = ['Ctrl+Q'];
    complete.shortcuts['focus-nav-buttons'] = ['Ctrl+Shift+N', 'Ctrl+Tab'];
    complete.shortcuts['next-tab'] = ['Ctrl+PageDown'];
    fs.writeFileSync(legacyFile, JSON.stringify(complete));
    keybinds.initializeKeybinds(directory);
    assert.deepEqual(keybinds.getKeybinds()['remove-tab'], ['Ctrl+Q'], 'Changes in a complete table remain choices');
    assert.deepEqual(keybinds.getKeybinds()['focus-nav-buttons'], defaults.shortcuts['focus-nav-buttons'], 'Migrated upstream seeds in a complete table are not choices');
    // A changes-only file lists choices only, even when one equals an earlier default.
    fs.writeFileSync(legacyFile, JSON.stringify({version: 2, changesOnly: true, shortcuts: {'focus-nav-buttons': ['Ctrl+Shift+N', 'Ctrl+Tab']}, normal: {}}));
    keybinds.initializeKeybinds(directory);
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: 'Tab', control: true}), 'focus-nav-buttons');
    // Bindings for removed actions are dropped instead of rejecting the whole profile.
    fs.writeFileSync(legacyFile, JSON.stringify({version: 2, changesOnly: true, shortcuts: {'toggle-site-shields': ['Ctrl+Shift+Y'], 'toggle-notes': ['Alt+N'], 'export-reader-to-notes': ['Ctrl+Shift+E'], 'open-notes-external': ['Ctrl+Shift+O'], 'export-reader-to-external': ['Alt+Shift+X'], 'remove-tab': ['Ctrl+Q']}, normal: {}}));
    keybinds.initializeKeybinds(directory);
    assert.equal(keybinds.getKeyboardConfig().warning, undefined);
    assert.deepEqual(keybinds.getKeybinds()['remove-tab'], ['Ctrl+Q']);
    for (const id of ['toggle-site-shields', 'toggle-notes', 'export-reader-to-notes', 'open-notes-external', 'export-reader-to-external']) assert(!Object.hasOwn(keybinds.getKeybinds(), id));
    // A choice that collides with a default takes the chord instead of rejecting the whole profile.
    fs.writeFileSync(legacyFile, JSON.stringify({version: 2, changesOnly: true, shortcuts: {'focus-sidebar': ['Ctrl+PageDown']}, normal: {'trigger-hints': ['G'], 'toggle-find': ['J']}}));
    keybinds.initializeKeybinds(directory);
    const collided = keybinds.getKeyboardConfig();
    assert.equal(collided.warning, undefined);
    assert.deepEqual(collided.shortcuts['next-tab'], ['Ctrl+Tab']);
    assert.deepEqual(collided.normal['page-top'], [], 'A page prefix choice removes the colliding default sequence');
    assert.deepEqual(collided.normal['scroll-down'], defaults.normal['scroll-down'].filter(combo => combo !== 'J'));
    assert.deepEqual(keybinds.resolveKeybinds(complete).config.shortcuts['remove-tab'], ['Ctrl+Q'], 'Imports use the startup interpretation');
    assert.deepEqual(keybinds.getKeyboardConfig(), collided, 'Resolving an import leaves the active profile unchanged');
    assert.equal(keybinds.saveKeybinds(directory, disabled).ok, true);
    keybinds.initializeKeybinds(directory);
    assert.deepEqual(keybinds.getKeyboardConfig(), disabled);
    const snapshot = fs.readFileSync(path.join(directory, 'keybinds.json'), 'utf8');
    const before = keybinds.getKeyboardConfig();
    const rename = fs.renameSync;
    try {
        fs.renameSync = () => { throw new Error('Simulated write failure'); };
        assert.equal(keybinds.saveKeybinds(directory, defaults).ok, false);
    } finally { fs.renameSync = rename; }
    assert.deepEqual(keybinds.getKeyboardConfig(), before);
    assert.equal(fs.readFileSync(path.join(directory, 'keybinds.json'), 'utf8'), snapshot);
    assert.equal(fs.existsSync(path.join(directory, 'keybinds.json.tmp')), false);
    for (const invalid of [null, [], {version: 3}, {version: 2, shortcuts: []}]) assert.equal(keybinds.saveKeybinds(directory, invalid).ok, false);
    const returned = keybinds.getKeybinds(); returned['spawn-tab'].push('Ctrl+B');
    assert(!keybinds.getKeybinds()['spawn-tab'].includes('Ctrl+B'), 'Callers cannot mutate active bindings');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: 'w', control: true, isAutoRepeat: true}), null);
} finally { fs.rmSync(directory, {recursive: true, force: true}); }

// Custom component names do not imply editing, but real semantics still protect input.
const editableSource = fs.readFileSync(path.join(__dirname, '..', 'webview-preload.js'), 'utf8');
const editableContext = {};
vm.createContext(editableContext);
vm.runInContext(editableSource.slice(editableSource.indexOf('function isEditableElement'), editableSource.indexOf('// Deep inspection helper')), editableContext);
const element = (tagName, options = {}) => ({nodeType: 1, tagName, getAttribute: () => '', closest: () => null, ...options});
assert.equal(editableContext.isEditableElement(element('NEWS-TEXT')), false);
assert.equal(editableContext.isEditableElement(element('INPUT', {type: 'checkbox'})), false);
assert.equal(editableContext.isEditableElement(element('INPUT', {type: 'text'})), true);
assert.equal(editableContext.isEditableElement(element('CUSTOM-EDITOR', {getAttribute: () => 'textbox'})), true);
assert.equal(editableContext.isEditableElement(element('DIV', {isContentEditable: true})), true);

// Exercise the actual sandboxed guest handler, including the embedded shared grammar.
const source = fs.readFileSync(path.join(__dirname, '..', 'webview-preload.js'), 'utf8');
const handlers = {}, messages = [], scrolls = [];
let editable = false, hints = false, time = 1000, blurred = 0;
const context = {
    module: {exports: {}}, Date: {now: () => time},
    ipcRenderer: {send: () => {}, sendSync: () => clone(defaults), on: (name, callback) => { handlers[name] = callback; }, sendToHost: (...args) => messages.push(args)},
    window: {innerHeight: 800, addEventListener: (name, callback) => { handlers[name] = callback; }},
    document: {getElementById: id => id === 'fly-hint-layer' && hints ? {} : null,
        documentElement: {scrollHeight: 9000}, activeElement: {blur: () => blurred++}},
    isEventInEditable: () => editable, getDeepActiveElement: () => ({blur: () => blurred++}),
    getScrollTarget: () => ({clientHeight: 800, scrollHeight: 9000}),
    scrollPageBy: (x, y) => scrolls.push([x, y]), scrollPageTo: (x, y) => scrolls.push(['to', x, y])
};
vm.createContext(context);
vm.runInContext(source.slice(0, source.indexOf('// END GENERATED KEYBOARD CORE')), context);
assert(context.FlyKeyboard, 'The grammar must be global in the sandbox even when module exists');
vm.runInContext(source.slice(source.indexOf('let isPassthroughMode'), source.indexOf('// Host passthrough mode synchronisation')), context);
vm.runInContext(source.slice(source.indexOf('const localPageActions')), context);
function press(key, extra = {}) {
    const event = {key, isTrusted: true, preventDefault() {this.consumed = true;}, stopPropagation() {}, stopImmediatePropagation() {}, ...extra};
    handlers.keydown(event); return event;
}
assert(press('d').consumed); assert.deepEqual(scrolls.pop(), [0, 400]);
assert(press('g').consumed); time += 200; press('g'); assert.deepEqual(scrolls.pop(), ['to', 0, 0]);
press('g'); time += 800; press('g'); assert.equal(scrolls.length, 0, 'Expired sequence must not execute');
handlers.blur(); press('g'); press('y'); press('g'); assert.equal(scrolls.length, 0, 'Unrelated key clears a sequence');
handlers.blur(); editable = true; assert(!press('j').consumed); assert(press('f', {ctrlKey: true}).consumed);
assert.deepEqual(messages.pop(), ['keyboard-action', 'toggle-find']);
press('Escape'); assert.equal(blurred, 2); editable = false;
hints = true; assert(!press('j').consumed); assert(!press('Escape').consumed); hints = false;
press('Escape', {shiftKey: true}); assert(!press('w', {ctrlKey: true}).consumed);
press('Escape', {shiftKey: true}); assert(press('w', {ctrlKey: true}).consumed);
press('i'); assert(!press('j').consumed); press('Escape'); assert(press('j').consumed); scrolls.length = 0;
assert(!press('j', {isComposing: true}).consumed); assert(!press('j', {isTrusted: false}).consumed);
const custom = clone(defaults); custom.normal['scroll-down'] = ['Z']; custom.shortcuts['toggle-reader-view'] = [];
handlers['keyboard-config-changed']({}, custom);
assert(!press('j').consumed); assert(press('z').consumed); assert.deepEqual(scrolls.pop(), [0, 80]);
assert(!press('F9').consumed, 'Removing F9 must remove all handlers for F9');
press('x', {repeat: true}); assert.notEqual(messages.at(-1)?.[1], 'remove-tab');
console.log('Keyboard grammar, migration, transactional writes and guest modal controls passed.');

// Ordinary page mutations must not send cross-process hints notifications.
let hintLayer = false, mutationCallback;
const hintMessages = [];
const hintContext = {
    document: {documentElement: {}, getElementById: () => hintLayer ? {} : null},
    ipcRenderer: {sendToHost: (...args) => hintMessages.push(args)},
    MutationObserver: class {constructor(callback) {mutationCallback = callback;} observe() {}}
};
vm.createContext(hintContext);
vm.runInContext(source.slice(source.indexOf('// Guest hints tracking'), source.indexOf('// Scroll target detection')), hintContext);
for (let i = 0; i < 100; i++) mutationCallback();
assert.equal(hintMessages.length, 0);
hintLayer = true; mutationCallback(); mutationCallback();
hintLayer = false; mutationCallback(); mutationCallback();
assert.deepEqual(hintMessages, [['guest-hints-state', true], ['guest-hints-state', false]]);
console.log('Hints IPC only reports real state transitions.');

// Native guest chords work across frames, suppress destructive repeats and honour passthrough.
keybinds.initializeKeybinds(path.join(os.tmpdir(), 'fly-nonexistent-keyboard-profile'));
const mainSource = fs.readFileSync(path.join(__dirname, '..', 'main.js'), 'utf8');
const nativeHandlers = {}, nativeActions = [], nativeMessages = [];
const contents = {on: (name, callback) => { nativeHandlers[name] = callback; }, send: (...args) => nativeMessages.push(args)};
const nativeContext = {keybinds, keyboard, webContents: contents, guestPassthrough: new WeakMap(), guestRightAlt: new WeakMap(), handleAppAction: action => nativeActions.push(action)};
vm.createContext(nativeContext);
vm.runInContext(mainSource.slice(mainSource.indexOf('        // Native interception'), mainSource.indexOf("        webContents.on('render-process-gone'")), nativeContext);
function nativePress(key, extra = {}) {
    const event = {preventDefault() { this.consumed = true; }};
    nativeHandlers['before-input-event'](event, {type: 'keyDown', key, ...extra});
    return event;
}
assert(nativePress('w', {control: true}).consumed);
assert.equal(nativeActions.pop(), 'remove-tab');
assert(nativePress('w', {control: true, isAutoRepeat: true}).consumed);
assert.equal(nativeActions.length, 0);
assert(nativePress('Escape', {shift: true}).consumed);
assert.deepEqual(nativeMessages.pop(), ['set-passthrough-mode', true]);
assert(!nativePress('w', {control: true}).consumed);
assert(nativePress('Escape', {shift: true}).consumed);
assert(nativePress('w', {control: true}).consumed);
assert(!nativePress('w', {control: true, isComposing: true}).consumed);
assert(!nativePress('j').consumed, 'Native interception must never consume bare page keys');
console.log('Native guest chord and passthrough routing passed.');
const statusSource = fs.readFileSync(path.join(__dirname, '..', 'modules/statusBar.js'), 'utf8').replace(/^import .*;\n/gm, '').replace(/export /g, '');
const tabA = {}, tabB = {}, listA = {}, listB = {};
let selectedList = listA;
const statusContext = {
    state: {sessionState: {current_workspace: 'A', workspaces: {A: ['https://example.org', 'https://example.com']}}, activeViewsCache: {A: [tabA, tabB]}},
    URL, window: {}, document: {getElementById: () => null, querySelector: () => selectedList, querySelectorAll: () => [listA, listB]},
    notifyActiveView: () => {}, isSplitActive: () => false, getWorkspacePartition: () => 'A', updateDarkReaderButtonUI: () => {}, isWebviewDarkReaderActive: () => false
};
vm.createContext(statusContext); vm.runInContext(statusSource, statusContext);
statusContext.handleGuestPassthroughState(true, 'A', 0);
assert.equal(statusContext.getMode(), 'PASSTHROUGH');
selectedList = listB; statusContext.updateStatusBarFromActiveView();
assert.equal(statusContext.getMode(), 'NORMAL');
selectedList = listA; statusContext.updateStatusBarFromActiveView();
assert.equal(statusContext.getMode(), 'PASSTHROUGH', 'Switching back must restore the actual guest mode');
statusContext.handleGuestPassthroughState(false, 'A', 0);
statusContext.handleGuestInputFocus(true, 'A', 0);
selectedList = listB; statusContext.updateStatusBarFromActiveView();
selectedList = listA; statusContext.updateStatusBarFromActiveView();
assert.equal(statusContext.getMode(), 'INSERT');
console.log('Guest mode indicators survive tab switches.');
// Repeated status refreshes must preserve badge children when their values match.
const badgeWrites = {StatusBarSecurity: 0, StatusBarPartition: 0};
const badges = Object.fromEntries(Object.keys(badgeWrites).map(id => {
    let html = '';
    return [id, {get innerHTML() {return html;}, set innerHTML(value) {html = value; badgeWrites[id]++;}}];
}));
statusContext.document.getElementById = id => badges[id] || null;
statusContext.updateSecurityStatus('https://example.org');
statusContext.updateSecurityStatus('https://example.org');
assert.equal(badgeWrites.StatusBarSecurity, 1);
statusContext.updateSecurityStatus('about:blank');
assert.equal(badgeWrites.StatusBarSecurity, 2);
assert.match(badges.StatusBarSecurity.innerHTML, /BLANK/);
statusContext.updatePartitionBadge('A', false);
statusContext.updatePartitionBadge('A', false);
assert.equal(badgeWrites.StatusBarPartition, 1);
statusContext.updatePartitionBadge('B', false);
assert.equal(badgeWrites.StatusBarPartition, 2);
statusContext.updatePartitionBadge('B', true);
assert.match(badges.StatusBarPartition.innerHTML, /Private Profile/);
statusContext.updatePartitionBadge('B', false);
assert.equal(badgeWrites.StatusBarPartition, 4, 'Returning from private mode must refresh the container');
console.log('Status refreshes preserve unchanged badges and update URL/workspace/private changes.');
const transferSource = fs.readFileSync(path.join(__dirname, '..', 'modules/tabTransfer.js'), 'utf8').replace('export ', '');
const transferContext = {}; vm.createContext(transferContext); vm.runInContext(transferSource, transferContext);
let removedView = 0;
const titles = {A: ['One', 'Two'], B: ['Pinned']};
const pinned = {A: [false, false], B: [true]};
const moving = {
    sessionState: {workspaces: {A: ['one', 'two'], B: ['pinned']}, tab_titles: titles, tab_pinned: pinned},
    activeViewsCache: {A: [null, {remove: () => removedView++}], B: [{remove() {}}]},
    activeTitlesCache: titles, tabPinned: pinned,
    tabAudioMuted: {A: [false, true], B: [false]}, tabFavicons: {A: ['icon1', 'icon2'], B: ['iconB']},
    tabHistory: {A: [0, 1], B: [0]}, splitStates: {A: {primaryIdx: 0, secondaryIdx: 1}}
};
assert.equal(transferContext.transferTab(moving, 'A', 1, 'B', 0), 1, 'Unpinned tab cannot be inserted before pinned tabs');
assert.equal(removedView, 1, 'The moved webview must be destroyed, never reused in another container');
assert.equal(JSON.stringify(moving.sessionState.workspaces), JSON.stringify({A: ['one'], B: ['pinned', 'two']}));
assert.equal(JSON.stringify(titles), JSON.stringify({A: ['One'], B: ['Pinned', 'Two']}));
assert.equal(JSON.stringify(pinned), JSON.stringify({A: [false], B: [true, false]}));
assert.equal(moving.tabAudioMuted.B[1], true);
assert.equal(moving.tabFavicons.B[1], 'icon2');
assert.equal(moving.activeViewsCache.B[1], null);
assert.equal(moving.splitStates.A, undefined);
assert.strictEqual(moving.sessionState.tab_titles, moving.activeTitlesCache, 'Persistent aliases must not move twice');
assert.strictEqual(moving.sessionState.tab_favicons, moving.tabFavicons);
const transferSnapshot = JSON.stringify(moving);
assert.equal(transferContext.transferTab(moving, 'A', 99, 'B'), null);
assert.equal(transferContext.transferTab(moving, 'A', 0, '__proto__'), null);
assert.equal(JSON.stringify(moving), transferSnapshot);
console.log('Workspace transfers preserve metadata, pin order and container isolation.');
const paletteSource = fs.readFileSync(path.join(__dirname, '..', 'modules/overlays/commandPalette.js'), 'utf8').replace(/^import .*;\n/gm, '').replace(/export /g, '');
const paletteDispatched = [];
const paletteContext = {
    window: {flyAPI: {getActionMetadata: () => ({then: callback => callback(keybinds.ACTION_METADATA)}),
        onKeyboardConfigChanged: () => {}, executeKeyboardAction: action => paletteDispatched.push(action)}},
    document: {readyState: 'loading', addEventListener: () => {}, getElementById: () => null}
};
vm.createContext(paletteContext); vm.runInContext(paletteSource, paletteContext);
const commands = vm.runInContext('COMMAND_DEFINITIONS', paletteContext);
paletteContext.window.dispatchKeyboardAction = action => paletteDispatched.push(action);
const rawActions = vm.runInContext('commandActions', paletteContext);
const rendererSource = fs.readFileSync(path.join(__dirname, '..', 'renderer.js'), 'utf8');
{
    const local = [], native = [];
    const route = rendererSource.slice(rendererSource.indexOf('    let keyboardActions ='), rendererSource.indexOf('    let pageStroke ='));
    const context = {Set, actionNames: Object.keys(keybinds.ACTION_METADATA), window: {executeBrowserAction: action => local.push(action), flyAPI: {executeKeyboardAction: action => native.push(action)}}};
    vm.runInNewContext(route + '; keyboardActions = new Set(actionNames); window.dispatchKeyboardAction("toggle-help"); window.dispatchKeyboardAction("trigger-hints"); window.dispatchKeyboardAction("toggle-private-mode"); window.dispatchKeyboardAction("toggle-fullscreen"); window.dispatchKeyboardAction("__proto__"); window.dispatchKeyboardAction("unknown");', context);
    assert.deepEqual(local, ['toggle-help', 'trigger-hints']);
    assert.deepEqual(native, ['toggle-private-mode', 'toggle-fullscreen']);
}

// Execute the renderer dispatcher to catch fallthrough between unrelated actions.
const selectedTabs = [], fallbackActions = [], openedPalettes = [];
let tabCount = 3;
const dispatchContext = {
    window: {}, document: {querySelectorAll: () => Array(tabCount).fill({})},
    switchTabFocus: index => selectedTabs.push(index),
    commandActions: {'test-fallback': () => fallbackActions.push('test-fallback')},
    openCommandPalette: query => openedPalettes.push(query)
};
vm.createContext(dispatchContext);
vm.runInContext(rendererSource.slice(rendererSource.indexOf('    window.executeBrowserAction ='), rendererSource.indexOf('    window.flyAPI.onMasterShortcut')), dispatchContext);
for (const count of [0, 3, 10]) {
    tabCount = count;
    for (let number = 1; number <= 9; number++) {
        dispatchContext.window.executeBrowserAction(`tab-${number}`);
        assert.deepEqual(openedPalettes, [], 'Numbered tab actions must not open the command palette');
        if (count) assert.equal(selectedTabs.pop(), number === 9 ? count - 1 : Math.min(number - 1, count - 1));
        else assert.equal(selectedTabs.length, 0);
    }
}
dispatchContext.window.executeBrowserAction('test-fallback');
assert.deepEqual(fallbackActions, ['test-fallback']);
dispatchContext.window.executeBrowserAction('unknown-action');
assert.deepEqual(openedPalettes, [], 'Fallback and unknown actions must not open the command palette');
dispatchContext.window.executeBrowserAction('open-extension-actions');
assert.deepEqual(openedPalettes, ['Extensions'], 'Explicit extension actions must still open the command palette');
console.log('Tab selection and fallback actions do not fall through into the command palette.');
for (const command of commands) {
    const id = command.actionId || command.id;
    assert(Object.hasOwn(keybinds.ACTION_METADATA, id), `Palette action ${id} must be in the catalogue`);
    command.action(); assert.equal(paletteDispatched.pop(), id, 'Palette execution must use canonical dispatch');
    assert(rawActions[id] || rendererSource.includes(`case '${id}':`) || mainSource.includes(`action === '${id}'`) || /^tab-[1-9]$/.test(id), `Action ${id} must have a concrete handler`);
}
const discoverableActions = new Set(commands.flatMap(command => [command.id, command.actionId].filter(Boolean)));
for (const id of Object.keys(keybinds.ACTION_METADATA)) {
    assert(discoverableActions.has(id), `Catalogue action ${id}, including compatibility aliases, must be discoverable`);
}
assert.equal(commands.filter(command => ['toggle-sidebar-pin', 'toggle-zen-mode'].includes(command.id)).length, 1, 'Sidebar aliases must share one palette entry');
vm.runInContext("commandRegistry['toggle-sidebar-pin']()", paletteContext);
assert.equal(paletteDispatched.pop(), 'toggle-zen-mode', 'The legacy pin palette id must use the expansion action');
console.log('Every palette and binding action shares canonical dispatch and has a handler.');
const keyboardHandlers = {}, keyboardEvents = {}, bridgeActions = [];
let saveCalls = 0;
const frame = {}, chrome = {mainFrame: frame, send() {}};
const bridgeContext = {mainWindow: {isDestroyed: () => false, webContents: chrome},
    ipcMain: {handle: (name, callback) => {keyboardHandlers[name] = callback;}, on: (name, callback) => {keyboardEvents[name] = callback;}},
    keybinds: {...keybinds, saveKeybinds: () => { saveCalls++; return {ok: true}; }},
    webContents: {getAllWebContents: () => []}, CONFIG_DIR: 'unused', handleAppAction: action => bridgeActions.push(action)
};
vm.createContext(bridgeContext);
vm.runInContext(mainSource.slice(mainSource.indexOf('const guestPassthrough'), mainSource.indexOf("ipcMain.handle('read-bookmarks'")), bridgeContext);
const guestEvent = {sender: {getType: () => 'webview'}, senderFrame: {}};
const nestedEvent = {sender: chrome, senderFrame: {}};
assert.equal(keyboardHandlers['save-keybinds'](guestEvent, defaults).ok, false);
assert.equal(keyboardHandlers['save-keybinds'](nestedEvent, defaults).ok, false);
assert.equal(saveCalls, 0);
const hostEvent = {sender: chrome, senderFrame: frame};
assert.equal(keyboardHandlers['save-keybinds'](hostEvent, defaults).ok, true);
assert.equal(saveCalls, 1);
keyboardEvents['keyboard-action'](guestEvent, 'spawn-tab');
keyboardEvents['keyboard-action'](nestedEvent, 'spawn-tab');
keyboardEvents['keyboard-action'](hostEvent, 'made-up-action');
keyboardEvents['keyboard-action'](hostEvent, {toString: null});
keyboardEvents['keyboard-action'](hostEvent, null);
assert.equal(bridgeActions.length, 0);
keyboardEvents['keyboard-action'](hostEvent, 'spawn-tab');
assert.deepEqual(bridgeActions, ['spawn-tab']);
console.log('Guests and nested frames cannot mutate bindings or use host action dispatch.');
// Right Alt with Control is AltGr on many layouts and must not activate a user's Ctrl+Alt binding.
nativeContext.keybinds = {...keybinds, getActionForInput: (input, includeRepeat) => input.key === 'b' && input.control && input.alt ? 'spawn-tab' : keybinds.getActionForInput(input, includeRepeat)};
nativePress('AltGraph', {code: 'AltRight', control: true, alt: true});
assert(!nativePress('b', {control: true, alt: true}).consumed);
nativePress('AltGraph', {type: 'keyUp', code: 'AltRight'});
assert(nativePress('b', {control: true, alt: true}).consumed);
assert.equal(nativeActions.pop(), 'spawn-tab');
nativeContext.keybinds = keybinds;
const invalidDirectory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-invalid-keyboard-'));
const invalidFile = path.join(invalidDirectory, 'keybinds.json');
fs.writeFileSync(invalidFile, '{');
const logError = console.error;
try {
    console.error = () => {};
    keybinds.initializeKeybinds(invalidDirectory);
    assert(keybinds.getKeyboardConfig().warning);
    assert.equal(fs.readFileSync(invalidFile, 'utf8'), '{');
} finally { console.error = logError; fs.rmSync(invalidDirectory, {recursive: true, force: true}); }
console.log('Native AltGr and invalid-profile recovery checks passed.');
