const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const keybinds = require('../keybinds');
const {createTabNumbers} = require('../tabNumbers');

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const key = (type, key, code, flags = {}) => ({type, key, code, isAutoRepeat: false, ...flags});
const altDown = key('keyDown', 'Alt', 'AltLeft', {alt: true});
const altUp = key('keyUp', 'Alt', 'AltLeft');

(async () => {
    // The default tab selection shortcuts are Alt+1–9; their modifier shows the numbers.
    keybinds.initializeKeybinds(fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-numbers-')));
    assert.deepEqual([...keybinds.tabNumberModifiers()], ['Alt']);
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-numbers-'));
    const shortcuts = {};
    for (let index = 1; index <= 9; index++) shortcuts[`tab-${index}`] = [`Ctrl+Shift+${index}`];
    shortcuts['tab-9'].push('F2');
    assert.equal(keybinds.saveKeybinds(directory, {version: 2, shortcuts: {...keybinds.getKeybinds(), ...shortcuts}, normal: keybinds.getKeyboardConfig().normal}).ok, true);
    assert.deepEqual([...keybinds.tabNumberModifiers()], ['Ctrl+Shift'], 'Rebound shortcuts move the numbers to their modifier; function keys have none');

    let modifiers = new Set(['Alt']);
    const sent = [];
    const numbers = createTabNumbers({modifiers: () => modifiers, send: shown => sent.push(shown), delay: 20});

    // Holding the modifier shows the numbers after the delay; releasing it hides them.
    numbers.track(altDown);
    assert.deepEqual(sent, []);
    await wait(40);
    assert.deepEqual(sent, [true]);
    numbers.track(key('keyDown', 'Alt', 'AltLeft', {alt: true, isAutoRepeat: true}));
    numbers.track(altUp);
    assert.deepEqual(sent, [true, false]);

    // A quick chord never shows them.
    sent.length = 0;
    numbers.track(altDown);
    numbers.track(key('keyDown', 's', 'KeyS', {alt: true}));
    await wait(40);
    assert.deepEqual(sent, []);
    numbers.track(altUp);

    // Selecting a tab hides them, and they stay hidden while the modifier repeats: the page that
    // received Alt+1 never reports the release, so only a fresh press may show them again.
    numbers.track(altDown);
    await wait(40);
    numbers.track(key('keyDown', '1', 'Digit1', {alt: true}));
    numbers.track(key('keyDown', 'Alt', 'AltLeft', {alt: true, isAutoRepeat: true}));
    await wait(40);
    assert.deepEqual(sent, [true, false]);
    numbers.track(altDown);
    await wait(40);
    assert.deepEqual(sent, [true, false, true]);
    numbers.hide();
    assert.deepEqual(sent, [true, false, true, false], 'Losing the window focus hides them');

    // Right Alt (AltGr on many layouts), other modifiers and passthrough pages never show them.
    sent.length = 0;
    numbers.track(key('keyDown', 'Alt', 'AltRight', {alt: true}));
    numbers.track(key('keyDown', 'Control', 'ControlLeft', {control: true}));
    numbers.track(key('keyDown', 'Alt', 'AltLeft', {alt: true, control: true}));
    numbers.track(altDown, true);
    await wait(40);
    assert.deepEqual(sent, []);

    // A two-modifier chord shows them once both are held.
    modifiers = new Set(['Ctrl+Shift']);
    numbers.track(key('keyDown', 'Control', 'ControlLeft', {control: true}));
    numbers.track(key('keyDown', 'Shift', 'ShiftLeft', {control: true, shift: true}));
    await wait(40);
    assert.deepEqual(sent, [true]);
    numbers.track(key('keyUp', 'Shift', 'ShiftLeft', {control: true}));
    assert.deepEqual(sent, [true, false]);
    console.log('Tab numbers follow the tab selection modifier, never outlive it and ignore AltGr and passthrough.');
})().catch(error => { console.error(error); process.exit(1); });
