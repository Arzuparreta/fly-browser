// BEGIN GENERATED KEYBOARD CORE
// Shared shortcut grammar for the main process and the settings editor.
(function (root, factory) {
    if (false) module.exports = factory();
    else root.FlyKeyboard = factory();
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
    // Validated actions whose bindings differ from the validated defaults.
    function changesFrom(config, defaults) {
        const changes = {shortcuts: {}, normal: {}};
        for (const scope of ['shortcuts', 'normal']) for (const [id, values] of Object.entries(config[scope])) {
            if (JSON.stringify(values) !== JSON.stringify(defaults[scope][id] || [])) changes[scope][id] = values;
        }
        return changes;
    }
    return {normalise, eventCombo, display, compile, repeatable, validateConfig, changesFrom};
});
// END GENERATED KEYBOARD CORE
// webview-preload.js

const { ipcRenderer, webFrame, contextBridge } = require('electron');

// Guest pointer events do not reach the host document.
document.addEventListener('mouseenter', (event) => {
    if (event.isTrusted) ipcRenderer.sendToHost('guest-pointer-enter', performance.timeOrigin + event.timeStamp);
});

ipcRenderer.on('toggle-reader-view', () => {
    try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-reader-view'); } catch (err) {}
});

window.addEventListener('fly-reader-closed', () => {
    try { ipcRenderer.sendToHost('reader-view-toggled', { active: false }); } catch (err) {}
});

// Expose minimal, secure GM bridge for userscript execution
try {
    if (contextBridge && typeof contextBridge.exposeInMainWorld === 'function') {
        contextBridge.exposeInMainWorld('__flyGMBridge', {
            request: (token, details) => ipcRenderer.invoke('gm-xmlhttprequest', { token, details }),
            setStorageValue: (token, scriptId, key, value) => ipcRenderer.invoke('gm-storage-set', { token, scriptId, key, value }),
            deleteStorageValue: (token, scriptId, key) => ipcRenderer.invoke('gm-storage-delete', { token, scriptId, key }),
            getStorage: (token, scriptId) => ipcRenderer.invoke('gm-storage-get', { token, scriptId })
        });
    }
} catch (err) {}

// Expose minimal, secure PDF bridge exclusively to the vendored viewer
try {
    const isViewerPage = (function() {
        try {
            if (window.location.protocol !== 'file:') return false;
            const pathname = window.location.pathname || '';
            const normalised = pathname.replace(/\\/g, '/');
            return normalised.endsWith('/assets/pdfjs/viewer.html');
        } catch (e) {
            return false;
        }
    })();

    if (isViewerPage && contextBridge && typeof contextBridge.exposeInMainWorld === 'function') {
        contextBridge.exposeInMainWorld('__flyPDFBridge', {
            loadPdfData: (token, url) => ipcRenderer.invoke('read-pdf-data', { token, url }),
            savePdfFile: (token, options) => ipcRenderer.invoke('save-pdf-file', { token, options })
        });
    }
} catch (err) {}

// Privacy preferences do not alter page graphics, audio or hardware APIs.
webFrame.executeJavaScript(`(() => {
    Object.defineProperty(navigator, 'doNotTrack', {get: () => '1', configurable: true});
    Object.defineProperty(navigator, 'globalPrivacyControl', {get: () => true, configurable: true});
})()`).catch(() => {});

// Match the Electron identity sent in headers on Google's sign-in pages
try {
    if (window.location.hostname.endsWith('.google.com')) {
        const signInUserAgent = ipcRenderer.sendSync('get-google-signin-user-agent', window.location.hostname);
        if (signInUserAgent) {
            webFrame.executeJavaScript(`(function() {
                const ua = ${JSON.stringify(signInUserAgent)};
                Object.defineProperty(navigator, 'userAgent', { get: () => ua, configurable: true });
                Object.defineProperty(navigator, 'appVersion', { get: () => ua.replace(/^Mozilla\\//, ''), configurable: true });
            })();`);
        }
    }
} catch (e) {}

// Recursively resolve active element across shadow DOM and frame boundaries
function getDeepActiveElement() {
    let el = document.activeElement;
    while (el) {
        if (el.shadowRoot && el.shadowRoot.activeElement) {
            el = el.shadowRoot.activeElement;
        } else {
            try {
                if (el.contentDocument && el.contentDocument.activeElement) {
                    el = el.contentDocument.activeElement;
                    continue;
                }
            } catch (err) {}
            break;
        }
    }
    return el;
}

// Element classification helper for Insert mode and custom web components
function isEditableElement(el) {
    if (!el || el.nodeType !== 1) return false;
    const tag = (el.tagName || '').toUpperCase();
    const role = (el.getAttribute && el.getAttribute('role') || '').toLowerCase();

    if (tag === 'BUTTON' || role === 'button' || tag.includes('BUTTON')) {
        return false;
    }
    if (tag === 'INPUT' || tag === 'TEXTAREA' || tag === 'SELECT') {
        const type = (el.type || '').toLowerCase();
        if (['button', 'submit', 'reset', 'checkbox', 'radio', 'image'].includes(type)) {
            return false;
        }
        return true;
    }
    if (el.isContentEditable || el.contentEditable === 'true') {
        return true;
    }
    if (role === 'textbox' || role === 'searchbox' || role === 'combobox') {
        return true;
    }
    if (typeof el.closest === 'function') {
        const parentEditable = el.closest('[contenteditable="true"], input, textarea, [role="textbox"], [role="searchbox"]');
        if (parentEditable && parentEditable !== el) return isEditableElement(parentEditable);
    }
    return false;
}

// Deep inspection helper to verify if an event originated from an editable target
function isEventInEditable(e) {
    if (typeof e.composedPath === 'function') {
        const path = e.composedPath();
        for (let i = 0; i < path.length; i++) {
            const node = path[i];
            if (isEditableElement(node)) return true;
            if (node && node.shadowRoot && isEditableElement(node.shadowRoot.activeElement)) {
                return true;
            }
        }
    }
    if (isEditableElement(e.target)) return true;
    const deepActive = getDeepActiveElement();
    if (isEditableElement(deepActive)) return true;
    return false;
}

// Modal state variables
let isPassthroughMode = false;
let isExplicitInsert = false;
let keyboardConfig = ipcRenderer.sendSync('guest-keyboard-config');
let globalKeys = FlyKeyboard.compile(keyboardConfig?.shortcuts || {});
let normalKeys = FlyKeyboard.compile(keyboardConfig?.normal || {});
let normalPrefixes = new Set([...normalKeys.keys()].filter(key => key.includes(' ')).map(key => key.split(' ')[0]));
let pendingStroke = '';
let pendingTime = 0;
const keyboardConfigListeners = [];
ipcRenderer.on('keyboard-config-changed', (_event, config) => {
    keyboardConfig = config;
    globalKeys = FlyKeyboard.compile(config.shortcuts);
    normalKeys = FlyKeyboard.compile(config.normal);
    normalPrefixes = new Set([...normalKeys.keys()].filter(key => key.includes(' ')).map(key => key.split(' ')[0]));
    pendingStroke = '';
    for (const listener of keyboardConfigListeners) try { listener(); } catch (err) {}
});

// The bundled What's New page shows the reader's own bindings for the actions it names.
try {
    const isWhatsNewPage = window.location.protocol === 'file:' &&
        (window.location.pathname || '').replace(/\\/g, '/').endsWith('/assets/whats-new.html');
    if (isWhatsNewPage && contextBridge && typeof contextBridge.exposeInMainWorld === 'function') {
        const bindings = actions => Object.fromEntries(actions.filter(action => typeof action === 'string').map(action => [action, {
            shortcuts: [...(keyboardConfig?.shortcuts?.[action] || [])],
            page: (keyboardConfig?.normal?.[action] || []).map(key => FlyKeyboard.display(key, 'normal'))
        }]));
        contextBridge.exposeInMainWorld('__flyWhatsNewKeys', {
            bindings: actions => bindings(Array.isArray(actions) ? actions : []),
            onChange: callback => { if (typeof callback === 'function') keyboardConfigListeners.push(callback); }
        });
    }
} catch (err) {}

// Host passthrough mode synchronisation
ipcRenderer.on('set-passthrough-mode', (_event, enabled) => {
    isPassthroughMode = !!enabled;
    pendingStroke = '';
    ipcRenderer.send('guest-keyboard-mode', isPassthroughMode);
    ipcRenderer.sendToHost('guest-passthrough-state', isPassthroughMode);
});

// Guest input focus tracking for Insert mode indicator
document.addEventListener('focusin', (e) => {
    if (isEventInEditable(e)) {
        try { ipcRenderer.sendToHost('guest-input-focus', true); } catch (err) {}
    }
}, true);

document.addEventListener('focusout', () => {
    setTimeout(() => {
        const deepActive = getDeepActiveElement();
        if (!isEditableElement(deepActive) && !isEditableElement(document.activeElement) && !isExplicitInsert) {
            try { ipcRenderer.sendToHost('guest-input-focus', false); } catch (err) {}
        }
    }, 10);
}, true);

// Guest hints tracking for Hints mode indicator
try {
    let lastHintsActive = false;
    const notifyHintsState = () => {
        const active = !!document.getElementById('fly-hint-layer');
        if (active === lastHintsActive) return;
        lastHintsActive = active;
        try { ipcRenderer.sendToHost('guest-hints-state', active); } catch (err) {}
    };

    const attachHintObserver = () => {
        if (!document.documentElement) return;
        const observer = new MutationObserver(notifyHintsState);
        // The hinter mounts its layer directly under the document element.
        observer.observe(document.documentElement, { childList: true });
    };

    if (document.documentElement) {
        attachHintObserver();
    } else {
        document.addEventListener('DOMContentLoaded', attachHintObserver);
    }
} catch (err) {}

// Scroll target detection
function getScrollTarget() {
    let active = getDeepActiveElement();
    while (active && active !== document.body && active !== document.documentElement) {
        const style = window.getComputedStyle(active);
        if (/(auto|scroll)/.test(style.overflow + style.overflowY + style.overflowX) &&
            (active.scrollHeight > active.clientHeight || active.scrollWidth > active.clientWidth)) return active;
        active = active.parentElement || active.getRootNode?.().host;
    }
    return document.scrollingElement || document.documentElement || document.body || window;
}

function scrollPageBy(dx, dy) {
    const target = getScrollTarget();
    if (target && typeof target.scrollBy === 'function') {
        target.scrollBy({ left: dx, top: dy, behavior: 'smooth' });
    } else {
        window.scrollBy({ left: dx, top: dy, behavior: 'smooth' });
    }
}

function scrollPageTo(x, y) {
    const target = getScrollTarget();
    if (target && typeof target.scrollTo === 'function') {
        target.scrollTo({ left: x, top: y, behavior: 'smooth' });
    } else {
        window.scrollTo({ left: x, top: y, behavior: 'smooth' });
    }
}

const localPageActions = {
        'scroll-down': () => scrollPageBy(0, 80), 'scroll-up': () => scrollPageBy(0, -80),
        'scroll-left': () => scrollPageBy(-80, 0), 'scroll-right': () => scrollPageBy(80, 0),
        'half-page-down': () => scrollPageBy(0, Math.floor((getScrollTarget().clientHeight || window.innerHeight) / 2)),
        'half-page-up': () => scrollPageBy(0, -Math.floor((getScrollTarget().clientHeight || window.innerHeight) / 2)),
        'page-top': () => scrollPageTo(0, 0),
        'page-bottom': () => scrollPageTo(0, getScrollTarget().scrollHeight || document.documentElement.scrollHeight),
        'enter-insert': () => { isExplicitInsert = true; ipcRenderer.sendToHost('guest-input-focus', true); }
    };
ipcRenderer.on('keyboard-local-action', (_event, action) => { localPageActions[action]?.(); });

// Modal page controls use the same grammar and action catalogue as browser shortcuts.
window.addEventListener('keydown', e => {
    if (!e.isTrusted || e.isComposing || e.key === 'Dead' || e.key === 'Process') {
        pendingStroke = '';
        return;
    }
    const combo = FlyKeyboard.eventCombo(e);
    const consume = () => { e.preventDefault(); e.stopPropagation(); e.stopImmediatePropagation(); };
    let action = globalKeys.get(combo);
    if (action === 'toggle-passthrough') {
        consume();
        if (!e.repeat) {
            isPassthroughMode = !isPassthroughMode;
            pendingStroke = '';
            ipcRenderer.send('guest-keyboard-mode', isPassthroughMode);
            ipcRenderer.sendToHost('guest-passthrough-state', isPassthroughMode);
        }
        return;
    }
    if (isPassthroughMode) return;
    if (action) {
        consume();
        if (!e.repeat || FlyKeyboard.repeatable(action)) {
            if (localPageActions[action]) localPageActions[action]();
            else ipcRenderer.sendToHost('keyboard-action', action);
        }
        return;
    }
    // Hints own Escape, Backspace and their alphabet while the overlay is visible.
    if (document.getElementById('fly-hint-layer')) { pendingStroke = ''; return; }
    const inEditable = isEventInEditable(e);
    if (e.key === 'Escape') {
        ipcRenderer.sendToHost('guest-escape');
        pendingStroke = '';
        const reader = document.getElementById('fly-reader-overlay');
        if (reader) {
            consume(); window.dispatchEvent(new CustomEvent('fly-reader-close')); reader.remove(); document.documentElement.style.overflow = '';
            ipcRenderer.sendToHost('reader-view-toggled', {active: false});
        } else if (inEditable || isExplicitInsert) {
            consume();
            getDeepActiveElement()?.blur?.(); document.activeElement?.blur?.();
        }
        isExplicitInsert = false;
        ipcRenderer.sendToHost('guest-input-focus', false);
        return;
    }
    if (inEditable || isExplicitInsert || !combo) { pendingStroke = ''; return; }
    const now = Date.now();
    action = pendingStroke && now - pendingTime < 700 ? normalKeys.get(pendingStroke + ' ' + combo) : null;
    pendingStroke = '';
    if (!action) action = normalKeys.get(combo);
    if (!action && normalPrefixes.has(combo)) {
        consume();
        if (!e.repeat) { pendingStroke = combo; pendingTime = now; }
        return;
    }
    if (!action) return;
    consume();
    if (e.repeat && !FlyKeyboard.repeatable(action)) return;
    if (localPageActions[action]) localPageActions[action]();
    else ipcRenderer.sendToHost('keyboard-action', action);
}, true);
window.addEventListener('blur', () => { pendingStroke = ''; });
