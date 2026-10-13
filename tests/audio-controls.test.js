// tests/audio-controls.test.js
// Automated verification for audio controls, keybind mappings, and conflict-free defaults

const assert = require('assert');
const keybinds = require('../keybinds.js');

console.log('Running Mise audio controls and keybind regression tests...');

// 1. Verify audio mute keybindings are registered
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    assert(defaults['toggle-tab-audio-mute'], 'toggle-tab-audio-mute must be present in default keybinds');
    
    const bindings = Array.isArray(defaults['toggle-tab-audio-mute']) 
        ? defaults['toggle-tab-audio-mute'] 
        : [defaults['toggle-tab-audio-mute']];
    
    assert(bindings.includes('Ctrl+Shift+M'), 'toggle-tab-audio-mute must include Ctrl+Shift+M');
    assert(bindings.includes('F8'), 'toggle-tab-audio-mute must include F8');

    console.log('✓ Tab audio mute default keybinds registered');
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
    assert(metadata['toggle-tab-audio-mute'], 'toggle-tab-audio-mute metadata must exist');
    assert(metadata['toggle-global-audio-mute'], 'toggle-global-audio-mute metadata must exist');
    assert.strictEqual(metadata['toggle-tab-audio-mute'].category, 'Web Interaction');
    assert.strictEqual(metadata['toggle-global-audio-mute'].category, 'Web Interaction');

    console.log('✓ Audio control action metadata verified');
}

// 4. Verify input event translation to key combination
{
    const eventM = {
        type: 'keyDown',
        key: 'm',
        code: 'KeyM',
        control: true,
        shift: true,
        alt: false,
        meta: false
    };
    const comboM = keybinds.inputEventToKeyCombination(eventM);
    assert.strictEqual(comboM, 'Ctrl+Shift+M', 'Input event should resolve to Ctrl+Shift+M');

    const eventF8 = {
        type: 'keyDown',
        key: 'F8',
        code: 'F8',
        control: false,
        shift: false,
        alt: false,
        meta: false
    };
    const comboF8 = keybinds.inputEventToKeyCombination(eventF8);
    assert.strictEqual(comboF8, 'F8', 'Input event should resolve to F8');

    console.log('✓ Key combination normalization for audio shortcuts verified');
}

console.log('\nAll audio controls & keybind tests passed successfully!\n');
