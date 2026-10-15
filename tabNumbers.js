// Tab numbers show while the modifier of the tab selection shortcuts (Alt+1–9 by default) is held
// on its own, whether the page or Fly's interface has the keyboard. Chromium drops the key releases
// that follow a shortcut Fly takes from a page, so every event other than a fresh press of that
// modifier hides them; they can never outlive the key. The delay keeps quick chords such as Alt+S
// from flashing them.
const MODIFIERS = [['Control', 'control', 'Ctrl'], ['Alt', 'alt', 'Alt'], ['Shift', 'shift', 'Shift'], ['Meta', 'meta', 'Meta']];

function createTabNumbers({modifiers, send, delay = 150}) {
    let shown = false;
    let timer = null;
    const set = value => {
        clearTimeout(timer);
        timer = null;
        if (value !== shown) send(shown = value);
    };
    // passthrough: the page receives every shortcut, so the numbers would point at nothing.
    function track(input, passthrough = false) {
        if (input.type !== 'keyDown' && input.type !== 'keyUp') return;
        const pressed = input.type === 'keyDown' && MODIFIERS.some(([key]) => key === input.key);
        // Right Alt is AltGr on many layouts, used for typing characters such as @ and #.
        if (!pressed || passthrough || input.code === 'AltRight') return set(false);
        if (input.isAutoRepeat) return;
        const held = MODIFIERS.filter(([key, flag]) => key === input.key || input[flag]).map(([, , name]) => name).join('+');
        if (!modifiers().has(held)) return set(false);
        if (!shown && !timer) timer = setTimeout(() => set(true), delay);
    }
    return {track, hide: () => set(false)};
}

module.exports = {createTabNumbers};
