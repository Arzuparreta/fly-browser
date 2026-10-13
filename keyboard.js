// Shared shortcut grammar for the main process and the settings editor.
(function (root, factory) {
    if (typeof module === 'object' && module.exports) module.exports = factory();
    else root.MiseKeyboard = factory();
})(typeof globalThis !== 'undefined' ? globalThis : this, function () {
    const aliases = {control: 'Ctrl', ctrl: 'Ctrl', option: 'Alt', alt: 'Alt', shift: 'Shift',
        cmd: 'Meta', command: 'Meta', super: 'Meta', meta: 'Meta'};
    const named = {esc: 'Escape', escape: 'Escape', return: 'Enter', enter: 'Enter',
        del: 'Delete', delete: 'Delete', tab: 'Tab', space: 'Space', plus: 'Plus',
        arrowleft: 'ArrowLeft', arrowright: 'ArrowRight', arrowup: 'ArrowUp', arrowdown: 'ArrowDown',
        pageup: 'PageUp', pagedown: 'PageDown', home: 'Home', end: 'End', backspace: 'Backspace',
        insert: 'Insert', contextmenu: 'ContextMenu'};
    function normalise(value) {
        if (typeof value !== 'string' || !value.trim() || value.length > 80) return '';
        if (value === ' ') return 'Space';
        const parts = value.trim().replace(/\+\+$/, '+Plus').split('+');
        const mods = new Set();
        let key = '';
        for (const part of parts) {
            const token = part.trim();
            const mod = aliases[token.toLowerCase()];
            if (mod) {
                if (mods.has(mod)) return '';
                mods.add(mod);
            } else {
                if (key || !token) return '';
                key = named[token.toLowerCase()] || (/^f(?:[1-9]|1\d|2[0-4])$/i.test(token) ? token.toUpperCase() :
                    token.length === 1 ? token.toUpperCase() : '');
                if (!key) return '';
            }
        }
        if (key === 'Plus' || key.length === 1 && !/[a-z0-9]/i.test(key)) mods.delete('Shift');
        return key ? [...['Ctrl', 'Alt', 'Shift', 'Meta'].filter(m => mods.has(m)), key].join('+') : '';
    }
    function eventCombo(event) {
        if (!event || event.isComposing || event.key === 'Dead' || event.key === 'Process' ||
            event.getModifierState?.('AltGraph')) return '';
        let key = event.key;
        if (typeof key !== 'string') return '';
        if (Object.hasOwn(aliases, key.toLowerCase())) return '';
        // Match the user's layout. Shifted punctuation is represented by the visible character.
        if (key === '+') key = 'Plus';
        if (key === ' ') key = 'Space';
        const mods = [];
        if (event.ctrlKey || event.control) mods.push('Ctrl');
        if (event.altKey || event.alt) mods.push('Alt');
        const shift = event.shiftKey || event.shift;
        if (shift && (event.ctrlKey || event.control || event.altKey || event.alt || event.metaKey || event.meta) && /^Digit[0-9]$/.test(event.code || '')) key = event.code.slice(5);
        if (shift && !(key.length === 1 && !/[a-z0-9]/i.test(key)) && key !== 'Plus') mods.push('Shift');
        if (event.metaKey || event.meta) mods.push('Meta');
        return normalise([...mods, key].join('+'));
    }
    function display(value, scope) {
        if (scope !== 'normal') return value;
        return value.split(' ').map(stroke => stroke.length === 1 && stroke.toLowerCase() !== stroke.toUpperCase() ? stroke.toLowerCase() : stroke).join(' ');
    }
    function compile(table) {
        const result = new Map();
        for (const [action, values] of Object.entries(table)) {
            for (const value of Array.isArray(values) ? values : [values]) if (value) result.set(value, action);
        }
        return result;
    }
    function repeatable(action) {
        return /^(scroll-|half-page-|zoom-|next-tab$|previous-tab$)/.test(action || '');
    }
    function validateConfig(input, metadata) {
        if (!input || input.version !== 2 || typeof input !== 'object') return {ok: false, error: 'Unsupported keyboard configuration.'};
        const config = {version: 2, shortcuts: {}, normal: {}};
        for (const scope of ['shortcuts', 'normal']) {
            const table = input[scope];
            if (!table || typeof table !== 'object' || Array.isArray(table)) return {ok: false, error: `Invalid ${scope} table.`};
            const occupied = new Map();
            for (const [id, raw] of Object.entries(table)) {
                if (!Object.hasOwn(metadata, id)) return {ok: false, error: `Unknown action: ${id}`};
                const values = raw === '' || raw === null ? [] : Array.isArray(raw) ? raw : [raw];
                if (values.length > 8) return {ok: false, error: 'Use at most eight bindings per action.'};
                const normalised = [];
                for (const value of values) {
                    if (typeof value !== 'string') return {ok: false, error: `Invalid binding for ${metadata[id].label}.`};
                    const strokes = value.trim().split(/\s+/).map(normalise);
                    if (strokes.some(s => !s) || strokes.length > (scope === 'normal' ? 2 : 1)) return {ok: false, error: `Invalid binding: ${value}`};
                    const combo = strokes.join(' ');
                    if (scope === 'shortcuts' && !/^(Ctrl|Alt|Meta)\+|^F\d+$/.test(combo) && combo !== 'Shift+Escape') {
                        return {ok: false, error: 'Browser shortcuts need Ctrl, Alt, Meta or a function key. Use page keys for bare letters.'};
                    }
                    if (strokes.includes('Escape') || combo === 'Tab' || combo === 'Shift+Tab') return {ok: false, error: 'Escape and Tab are reserved for cancelling and focus navigation.'};
                    if (normalised.includes(combo)) continue;
                    for (const [other, owner] of occupied) {
                        if (other === combo || other.startsWith(combo + ' ') || combo.startsWith(other + ' ')) {
                            return {ok: false, error: `${combo} conflicts with ${metadata[owner].label} (${scope === 'normal' ? 'page keys' : 'browser shortcuts'}).`};
                        }
                    }
                    occupied.set(combo, id);
                    normalised.push(combo);
                }
                config[scope][id] = normalised;
            }
        }
        if (!config.shortcuts['toggle-passthrough']?.length) return {ok: false, error: 'Keep a browser shortcut for passthrough so you can return to NORMAL mode.'};
        // Global chords take precedence; reject page bindings that would never be delivered.
        const globals = compile(config.shortcuts);
        for (const values of Object.values(config.normal)) for (const combo of values) {
            if (combo.split(' ').some(stroke => globals.has(stroke))) return {ok: false, error: `${combo} is already a browser shortcut. Remove that assignment first.`};
        }
        return {ok: true, config};
    }
    return {normalise, eventCombo, display, compile, repeatable, validateConfig};
});
