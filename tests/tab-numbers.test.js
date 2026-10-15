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
    // One modifier selects tabs by number (Alt by default) and is the one that shows the numbers.
    keybinds.initializeKeybinds(fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-numbers-')));
    assert.equal(keybinds.tabModifier(), 'Alt');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: '1', code: 'Digit1', alt: true}), 'tab-1');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: '9', code: 'Digit9', alt: true}), 'tab-9');
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-numbers-'));
    const config = tabs => ({...keybinds.getKeyboardConfig(), tabs});
    assert.equal(keybinds.saveKeybinds(directory, config('shift+ctrl')).ok, true);
    assert.equal(keybinds.tabModifier(), 'Ctrl+Shift');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: '!', code: 'Digit1', control: true, shift: true}), 'tab-1');
    assert.equal(keybinds.getActionForInput({type: 'keyDown', key: '1', code: 'Digit1', alt: true}), null);
    assert.equal(keybinds.saveKeybinds(directory, config('')).ok, true);
    assert.equal(keybinds.tabModifier(), '', 'Off');
    keybinds.initializeKeybinds(directory);
    assert.equal(keybinds.tabModifier(), '', 'Off survives a restart');

    let modifier = 'Alt';
    const sent = [];
    const numbers = createTabNumbers({modifier: () => modifier, send: shown => sent.push(shown), delay: 20});

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

    // A two-modifier chord shows them once both are held; with tab selection off nothing does.
    modifier = 'Ctrl+Shift';
    numbers.track(key('keyDown', 'Control', 'ControlLeft', {control: true}));
    numbers.track(key('keyDown', 'Shift', 'ShiftLeft', {control: true, shift: true}));
    await wait(40);
    assert.deepEqual(sent, [true]);
    numbers.track(key('keyUp', 'Shift', 'ShiftLeft', {control: true}));
    assert.deepEqual(sent, [true, false]);
    modifier = '';
    numbers.track(altDown);
    await wait(40);
    assert.deepEqual(sent, [true, false]);
    console.log('Tab numbers follow the tab selection modifier, never outlive it and ignore AltGr, passthrough and Off.');
})().catch(error => { console.error(error); process.exit(1); });
