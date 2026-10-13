// BEGIN GENERATED KEYBOARD CORE
// Shared shortcut grammar for the main process and the settings editor.
(function (root, factory) {
    if (false) module.exports = factory();
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
// END GENERATED KEYBOARD CORE
// webview-preload.js

const { ipcRenderer, webFrame, contextBridge } = require('electron');

ipcRenderer.on('toggle-reader-view', () => {
    try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-reader-view'); } catch (err) {}
});

window.addEventListener('mise-reader-closed', () => {
    try { ipcRenderer.sendToHost('reader-view-toggled', { active: false }); } catch (err) {}
});

// Expose minimal, secure GM bridge for userscript execution
try {
    if (contextBridge && typeof contextBridge.exposeInMainWorld === 'function') {
        contextBridge.exposeInMainWorld('__miseGMBridge', {
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
        contextBridge.exposeInMainWorld('__misePDFBridge', {
            loadPdfData: (token, url) => ipcRenderer.invoke('read-pdf-data', { token, url }),
            savePdfFile: (token, options) => ipcRenderer.invoke('save-pdf-file', { token, options })
        });
    }
} catch (err) {}

// Ask the main process whether this site is on the user's trusted-domain
// allowlist (banking, shopping, etc.). Trusted sites skip fingerprint
// spoofing so their fraud-detection checks see a consistent, real device
let isTrustedSite = false;
try {
    isTrustedSite = !!ipcRenderer.sendSync('is-trusted-domain', window.location.hostname);
} catch (e) {
    isTrustedSite = false;
}

if (!isTrustedSite) {

    const codeToInject = `(function() {
        if (window.__miseFingerprintPatchesApplied) return;
        window.__miseFingerprintPatchesApplied = true;

        // Privacy signals
        Object.defineProperty(navigator, 'doNotTrack', { get: () => '1', configurable: true });
        Object.defineProperty(navigator, 'globalPrivacyControl', { get: () => true, configurable: true });

        // Domain-bound hardware specification standardisation
        const hostStr = String(window.location.hostname || '');
        let hostHash = 0;
        for (let i = 0; i < hostStr.length; i++) {
            hostHash = (Math.imul(31, hostHash) + hostStr.charCodeAt(i)) | 0;
        }
        hostHash = Math.abs(hostHash);

        const domainConcurrency = (hostHash % 2 === 0) ? 8 : 12;
        Object.defineProperty(navigator, 'hardwareConcurrency', { get: () => domainConcurrency, configurable: true });
        Object.defineProperty(navigator, 'deviceMemory', { get: () => 8, configurable: true });
        Object.defineProperty(navigator, 'platform', { get: () => 'Linux x86_64', configurable: true });
        Object.defineProperty(navigator, 'language', { get: () => 'en-GB', configurable: true });
        Object.defineProperty(navigator, 'languages', { get: () => Object.freeze(['en-GB', 'en-US', 'en']), configurable: true });

        // Domain-bound plugin farbling for cross-domain randomization detection
        try {
            if (navigator.plugins && navigator.plugins.length) {
                const isEvenDomain = (hostHash % 2 === 0);
                const origPlugin0 = navigator.plugins[0];
                if (origPlugin0) {
                    const descSuffix = isEvenDomain ? ' ' : '';
                    const proxiedPlugin0 = new Proxy(origPlugin0, {
                        get(target, prop) {
                            if (prop === 'description') {
                                return target.description + descSuffix;
                            }
                            return target[prop];
                        }
                    });
                    const proxiedPlugins = new Proxy(navigator.plugins, {
                        get(target, prop) {
                            if (prop === '0' || prop === 0) return proxiedPlugin0;
                            return target[prop];
                        }
                    });
                    Object.defineProperty(navigator, 'plugins', { get: () => proxiedPlugins, configurable: true });
                }
            }
        } catch (e) {}

        // User Agent standardisation matching stable browser releases
        const cleanUserAgent = (navigator.userAgent || '')
            .replace(new RegExp('mise-browser/[0-9.]+\\\\s*', 'gi'), '')
            .replace(new RegExp('Electron/[0-9.]+\\\\s*', 'gi'), '')
            .replace(new RegExp('Chrome/(\\\\d+)\\\\.[\\\\d.]+', 'i'), 'Chrome/$1.0.0.0')
            .trim();
        Object.defineProperty(navigator, 'userAgent', { get: () => cleanUserAgent, configurable: true });
        Object.defineProperty(navigator, 'appVersion', { get: () => cleanUserAgent.replace(new RegExp('^Mozilla/'), ''), configurable: true });

        try {
            const origResolvedOptions = Intl.DateTimeFormat.prototype.resolvedOptions;
            Intl.DateTimeFormat.prototype.resolvedOptions = function() {
                const res = origResolvedOptions.apply(this, arguments);
                res.timeZone = 'Europe/London';
                return res;
            };
        } catch (e) {}

        const chromeMatch = (navigator.userAgent || '').match(new RegExp('Chrome/(\\\\d+)\\\\.([\\\\d.]+)'));
        const chromeMajor = chromeMatch ? chromeMatch[1] : (typeof process !== 'undefined' && process.versions && process.versions.chrome ? process.versions.chrome.split('.')[0] : '132');
        const chromeFull = chromeMajor + '.0.0.0';

        if (navigator.userAgentData) {
            Object.defineProperty(navigator, 'userAgentData', {
                get: function() {
                    return {
                        brands: [
                            { brand: 'Chromium', version: chromeMajor },
                            { brand: 'Google Chrome', version: chromeMajor },
                            { brand: 'Not-A.Brand', version: '99' }
                        ],
                        mobile: false,
                        platform: 'Linux',
                        getHighEntropyValues: function() {
                            return Promise.resolve({
                                architecture: 'x86',
                                bitness: '64',
                                brands: [
                                    { brand: 'Chromium', version: chromeMajor },
                                    { brand: 'Google Chrome', version: chromeMajor }
                                ],
                                mobile: false,
                                model: '',
                                platform: 'Linux',
                                platformVersion: '',
                                uaFullVersion: chromeFull
                            });
                        }
                    };
                }
            });
        }

        // WebGL Spoofing and Extension Shuffle on prototypes (matching Brave's Brave~Brave baseline)
        const spoofWebGlPrototype = (proto) => {
            if (!proto || !proto.getParameter) return;
            const origGetParam = proto.getParameter;
            proto.getParameter = function(param) {
                if (param === 37445 || param === 0x9245) return 'Google Inc. (Intel)';
                if (param === 37446 || param === 0x9246) return 'ANGLE (Intel, Mesa Intel(R) UHD Graphics 620 (KBL GT2), OpenGL 4.6)';
                if (param === 7936 || param === 0x1F00) return 'WebKit';
                if (param === 7937 || param === 0x1F01) return 'WebKit WebGL';
                return origGetParam.apply(this, arguments);
            };

            const origGetExt = proto.getExtension;
            proto.getExtension = function(name) {
                if (name === 'WEBGL_debug_renderer_info') {
                    return {
                        UNMASKED_VENDOR_WEBGL: 37445,
                        UNMASKED_RENDERER_WEBGL: 37446
                    };
                }
                return origGetExt.apply(this, arguments);
            };

            const origGetSupportedExtensions = proto.getSupportedExtensions;
            proto.getSupportedExtensions = function() {
                const list = origGetSupportedExtensions.apply(this, arguments);
                if (!list) return list;
                const shuffled = list.slice();
                let seed = hostHash;
                for (let i = shuffled.length - 1; i > 0; i--) {
                    seed = (Math.imul(seed, 9301) + 49297) % 233280;
                    const j = Math.floor((seed / 233280) * (i + 1));
                    [shuffled[i], shuffled[j]] = [shuffled[j], shuffled[i]];
                }
                return shuffled;
            };

            if (proto.readPixels) {
                const origReadPixels = proto.readPixels;
                proto.readPixels = function() {
                    origReadPixels.apply(this, arguments);
                    const pixels = arguments[6];
                    if (pixels && pixels.length) {
                        const delta = (hostHash % 2 === 0 ? 1 : -1);
                        for (let i = 0; i < pixels.length; i += 32) {
                            pixels[i] = Math.min(255, Math.max(0, pixels[i] + delta));
                        }
                    }
                };
            }
        };

        if (typeof WebGLRenderingContext !== 'undefined') spoofWebGlPrototype(WebGLRenderingContext.prototype);
        if (typeof WebGL2RenderingContext !== 'undefined') spoofWebGlPrototype(WebGL2RenderingContext.prototype);

        // Canvas Farbling (Domain-bound noise)
        const origGetImageData = CanvasRenderingContext2D.prototype.getImageData;
        const origToDataURL = HTMLCanvasElement.prototype.toDataURL;
        const origToBlob = HTMLCanvasElement.prototype.toBlob;

        const applyCanvasNoise = (canvas) => {
            try {
                const ctx = canvas.getContext('2d');
                if (!ctx) return;
                const w = Math.min(canvas.width, 256);
                const h = Math.min(canvas.height, 64);
                if (w <= 0 || h <= 0) return;

                const imgData = origGetImageData.call(ctx, 0, 0, w, h);
                const d = imgData.data;
                let touched = 0;
                const step = 4 * (1 + (hostHash % 3));
                const start = 4 * (hostHash % 4);
                const delta = (hostHash % 2 === 0 ? 1 : -1);

                for (let i = start; i < d.length; i += step) {
                    if (d[i + 3] > 0) {
                        d[i] = Math.min(255, Math.max(0, d[i] + delta));
                        touched++;
                        if (touched >= 30) break;
                    }
                }
                if (touched === 0 && d.length >= 4) {
                    d[0] = (hostHash % 250) + 1;
                    d[3] = 1;
                }
                ctx.putImageData(imgData, 0, 0);
            } catch (e) {}
        };

        HTMLCanvasElement.prototype.toDataURL = function() {
            try {
                const ctx2d = this.getContext('2d');
                if (ctx2d) {
                    applyCanvasNoise(this);
                } else {
                    const gl = this.getContext('webgl2') || this.getContext('webgl') || this.getContext('experimental-webgl');
                    if (gl && typeof gl.clear === 'function') {
                        const prevScissorEnabled = gl.isEnabled(gl.SCISSOR_TEST);
                        const prevScissorBox = gl.getParameter(gl.SCISSOR_BOX);
                        gl.enable(gl.SCISSOR_TEST);
                        gl.scissor(0, 0, 1, 1);
                        const jitterColor = ((hostHash % 17) + 1) * 0.002;
                        gl.clearColor(jitterColor, jitterColor, jitterColor, 0.02);
                        gl.clear(gl.COLOR_BUFFER_BIT);
                        gl.scissor(prevScissorBox[0], prevScissorBox[1], prevScissorBox[2], prevScissorBox[3]);
                        if (!prevScissorEnabled) gl.disable(gl.SCISSOR_TEST);
                    }
                }
            } catch (e) {}
            return origToDataURL.apply(this, arguments);
        };

        HTMLCanvasElement.prototype.toBlob = function(callback, type, quality) {
            try {
                const ctx2d = this.getContext('2d');
                if (ctx2d) {
                    applyCanvasNoise(this);
                }
            } catch (e) {}
            return origToBlob.apply(this, arguments);
        };

        CanvasRenderingContext2D.prototype.getImageData = function() {
            const imgData = origGetImageData.apply(this, arguments);
            if (imgData && imgData.data) {
                const d = imgData.data;
                const step = 4 * (1 + (hostHash % 3));
                const start = 4 * (hostHash % 4);
                const delta = (hostHash % 2 === 0 ? 1 : -1);
                for (let i = start; i < d.length; i += step) {
                    if (d[i + 3] > 0) {
                        d[i] = Math.min(255, Math.max(0, d[i] + delta));
                    }
                }
            }
            return imgData;
        };

        // Audio Farbling
        if (typeof AudioBuffer !== 'undefined') {
            const origGetChannelData = AudioBuffer.prototype.getChannelData;
            AudioBuffer.prototype.getChannelData = function(channel) {
                const results = origGetChannelData.apply(this, arguments);
                if (results && results.length >= 100) {
                    const start = Math.min(results.length - 1, 4500);
                    const end = Math.min(results.length, 5000);
                    const jitter = ((hostHash % 29) + 1) * 0.000002;
                    for (let i = start; i < end; i += 5) {
                        results[i] = results[i] + jitter;
                    }
                }
                return results;
            };
        }
    })();`;

    try {
        webFrame.executeJavaScript(codeToInject);
    } catch (e) {
        console.error('Failed to inject privacy spoofing script:', e);
    }
}

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
    if (tag.includes('-') && (tag.includes('SEARCH') || tag.includes('INPUT') || tag.includes('TEXT') || tag.includes('FIELD') || tag.includes('EDITOR') || tag.includes('COMPOSER'))) {
        return true;
    }
    if (typeof el.closest === 'function') {
        const parentEditable = el.closest('[contenteditable="true"], input, textarea, [role="textbox"], [role="searchbox"]');
        if (parentEditable) return true;
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
let globalKeys = MiseKeyboard.compile(keyboardConfig?.shortcuts || {});
let normalKeys = MiseKeyboard.compile(keyboardConfig?.normal || {});
let normalPrefixes = new Set([...normalKeys.keys()].filter(key => key.includes(' ')).map(key => key.split(' ')[0]));
let pendingStroke = '';
let pendingTime = 0;
ipcRenderer.on('keyboard-config-changed', (_event, config) => {
    keyboardConfig = config;
    globalKeys = MiseKeyboard.compile(config.shortcuts);
    normalKeys = MiseKeyboard.compile(config.normal);
    normalPrefixes = new Set([...normalKeys.keys()].filter(key => key.includes(' ')).map(key => key.split(' ')[0]));
    pendingStroke = '';
});

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
    const notifyHintsState = () => {
        const active = !!document.getElementById('mise-hint-layer');
        try { ipcRenderer.sendToHost('guest-hints-state', active); } catch (err) {}
    };

    const attachHintObserver = () => {
        if (!document.body) return;
        const observer = new MutationObserver(notifyHintsState);
        observer.observe(document.body, { childList: true });
    };

    if (document.body) {
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
    const combo = MiseKeyboard.eventCombo(e);
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
        if (!e.repeat || MiseKeyboard.repeatable(action)) {
            if (localPageActions[action]) localPageActions[action]();
            else ipcRenderer.sendToHost('keyboard-action', action);
        }
        return;
    }
    // Hints own Escape, Backspace and their alphabet while the overlay is visible.
    if (document.getElementById('mise-hint-layer')) { pendingStroke = ''; return; }
    const inEditable = isEventInEditable(e);
    if (e.key === 'Escape') {
        pendingStroke = '';
        const reader = document.getElementById('mise-reader-overlay');
        if (reader) {
            consume(); window.dispatchEvent(new CustomEvent('mise-reader-close')); reader.remove(); document.documentElement.style.overflow = '';
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
    if (e.repeat && !MiseKeyboard.repeatable(action)) return;
    if (localPageActions[action]) localPageActions[action]();
    else ipcRenderer.sendToHost('keyboard-action', action);
}, true);
window.addEventListener('blur', () => { pendingStroke = ''; });
