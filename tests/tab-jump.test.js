// tests/tab-jump.test.js
// Automated verification for direct tab jumping, keybind mappings, and Alt modifier exclusion

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

// 2. Strict Alt modifier guard: ensure no default keybinds use Alt modifier
{
    const defaults = keybinds.DEFAULT_KEYBINDS;
    for (const [action, binding] of Object.entries(defaults)) {
        const list = Array.isArray(binding) ? binding : [binding];
        for (const item of list) {
            if (!item) continue;
            assert(!item.toLowerCase().includes('alt'), `Action "${action}" binding "${item}" must not use Alt modifier`);
        }
    }

    console.log('✓ Verified no default keybindings use Alt modifier');
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
        const combo = keybinds.inputEventToKeyCombination(event);
        assert.strictEqual(combo, `Ctrl+${i}`, `Input event should resolve to Ctrl+${i}`);

        const action = keybinds.getActionForInput(event);
        assert.strictEqual(action, `jump-to-tab-${i}`, `Event for Ctrl+${i} must resolve to jump-to-tab-${i}`);
    }

    console.log('✓ Input event translation and action dispatch for Ctrl+1 to Ctrl+9 verified');
}

// 5. Verify tab jumping boundary calculations
{
    function computeTargetIdx(oneBasedIdx, totalTabs) {
        if (totalTabs === 0) return -1;
        let targetIdx = oneBasedIdx - 1;
        if (oneBasedIdx === 9 && targetIdx >= totalTabs) {
            targetIdx = totalTabs - 1;
        }
        if (targetIdx < 0 || targetIdx >= totalTabs) return -1;
        return targetIdx;
    }

    // 4 tabs open:
    assert.strictEqual(computeTargetIdx(1, 4), 0, 'Tab 1 maps to index 0');
    assert.strictEqual(computeTargetIdx(3, 4), 2, 'Tab 3 maps to index 2');
    assert.strictEqual(computeTargetIdx(4, 4), 3, 'Tab 4 maps to index 3');
    // Tab 5 does not exist with 4 tabs:
    assert.strictEqual(computeTargetIdx(5, 4), -1, 'Tab 5 should return -1 when only 4 tabs exist');
    // Tab 9 falls back to the last tab (index 3) matching standard browser behaviour:
    assert.strictEqual(computeTargetIdx(9, 4), 3, 'Tab 9 should fall back to last tab index');

    // 10 tabs open:
    assert.strictEqual(computeTargetIdx(9, 10), 8, 'Tab 9 maps to 9th tab (index 8) when 10 tabs exist');

    console.log('✓ Tab index boundary and fallback calculations verified');
}

console.log('\nAll direct tab jumping & cursor navigation tests passed successfully!');
