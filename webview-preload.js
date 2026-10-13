// webview-preload.js

const { ipcRenderer, webFrame, contextBridge } = require('electron');

ipcRenderer.on('toggle-reader-view', () => {
    try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-reader-view'); } catch (err) {}
});

window.addEventListener('mise-reader-closed', () => {
    try { ipcRenderer.sendToHost('reader-view-toggled', { active: false }); } catch (err) {}
});

window.addEventListener('mise-reader-export-notes', (e) => {
    try { ipcRenderer.sendToHost('reader-export-notes', e.detail); } catch (err) {}
});

window.addEventListener('mise-reader-export-external', (e) => {
    try { ipcRenderer.sendToHost('reader-export-external', e.detail); } catch (err) {}
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
let lastGTime = 0;

// Host passthrough mode synchronisation
ipcRenderer.on('set-passthrough-mode', (_event, enabled) => {
    isPassthroughMode = !!enabled;
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
    const active = document.activeElement;
    if (active && active !== document.body && active !== document.documentElement) {
        const style = window.getComputedStyle(active);
        if (/(auto|scroll)/.test(style.overflow + style.overflowY)) {
            return active;
        }
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

// Modal navigation engine keydown listener
window.addEventListener('keydown', (e) => {
    // Passthrough toggle chord: Shift + Escape
    if (e.shiftKey && e.key === 'Escape') {
        e.preventDefault();
        e.stopPropagation();
        isPassthroughMode = !isPassthroughMode;
        try { ipcRenderer.sendToHost('guest-passthrough-state', isPassthroughMode); } catch (err) {}
        return;
    }

    // In passthrough mode, let all keystrokes pass directly to guest application
    if (isPassthroughMode) {
        return;
    }

    const activeEl = document.activeElement;
    const deepActive = getDeepActiveElement();
    const inEditable = isEventInEditable(e);

    // Escape key exits reader view, input fields and explicit insert mode
    if (e.key === 'Escape') {
        const readerOverlay = document.getElementById('mise-reader-overlay');
        if (readerOverlay) {
            e.preventDefault();
            e.stopPropagation();
            e.stopImmediatePropagation();
            readerOverlay.remove();
            document.documentElement.style.overflow = '';
            try { ipcRenderer.sendToHost('reader-view-toggled', { active: false }); } catch (err) {}
            return;
        }
        if (inEditable) {
            if (deepActive && typeof deepActive.blur === 'function') {
                deepActive.blur();
            }
            if (activeEl && typeof activeEl.blur === 'function') {
                activeEl.blur();
            }
            e.preventDefault();
            e.stopPropagation();
            e.stopImmediatePropagation();
        }
        if (isExplicitInsert) {
            isExplicitInsert = false;
            e.preventDefault();
            e.stopPropagation();
            e.stopImmediatePropagation();
            try { ipcRenderer.sendToHost('guest-input-focus', false); } catch (err) {}
            return;
        }
        try { ipcRenderer.sendToHost('guest-input-focus', false); } catch (err) {}
        try { ipcRenderer.sendToHost('normal-mode-action', 'escape'); } catch (err) {}
        return;
    }

    // F9 toggles Reader View directly
    if (e.key === 'F9') {
        e.preventDefault();
        e.stopPropagation();
        e.stopImmediatePropagation();
        try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-reader-view'); } catch (err) {}
        return;
    }

    // In editable elements or explicit insert mode, allow normal typing
    if (inEditable || isExplicitInsert) {
        return;
    }

    // Modifiers check: pass through standard chords to host keybinds
    if (e.ctrlKey || e.altKey || e.metaKey) {
        return;
    }

    // Pass through if link hints overlay is actively consuming keystrokes
    if (document.getElementById('mise-hint-layer')) {
        return;
    }

    const key = e.key;

    function consumeEvent(evt) {
        evt.preventDefault();
        evt.stopPropagation();
        evt.stopImmediatePropagation();
    }

    // Smooth page scrolling
    if (key === 'j') {
        consumeEvent(e);
        scrollPageBy(0, 80);
        return;
    }
    if (key === 'k') {
        consumeEvent(e);
        scrollPageBy(0, -80);
        return;
    }
    if (key === 'd') {
        consumeEvent(e);
        try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-dark-reader'); } catch (err) {}
        return;
    }
    if (key === 'e') {
        consumeEvent(e);
        try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-reader-view'); } catch (err) {}
        return;
    }
    if (key === 'u') {
        consumeEvent(e);
        scrollPageBy(0, -Math.floor(window.innerHeight * 0.5));
        return;
    }
    if (key === 'h') {
        consumeEvent(e);
        scrollPageBy(-80, 0);
        return;
    }
    if (key === 'l') {
        consumeEvent(e);
        scrollPageBy(80, 0);
        return;
    }
    if (key === 'g') {
        consumeEvent(e);
        const now = Date.now();
        if (now - lastGTime < 500) {
            scrollPageTo(0, 0);
            lastGTime = 0;
        } else {
            lastGTime = now;
        }
        return;
    }
    if (key === 'G') {
        consumeEvent(e);
        const target = getScrollTarget();
        const maxY = Math.max(
            document.body ? document.body.scrollHeight : 0,
            document.documentElement ? document.documentElement.scrollHeight : 0,
            target ? (target.scrollHeight || 0) : 0
        );
        scrollPageTo(0, maxY);
        return;
    }

    // Explicit insert mode entry
    if (key === 'i') {
        consumeEvent(e);
        isExplicitInsert = true;
        try { ipcRenderer.sendToHost('guest-input-focus', true); } catch (err) {}
        return;
    }

    // Browser navigation and actions dispatched to host
    switch (key) {
        case 't':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'spawn-tab'); } catch (err) {}
            break;
        case 'x':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'remove-tab'); } catch (err) {}
            break;
        case 'o':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-address'); } catch (err) {}
            break;
        case 'r':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'reload-tab'); } catch (err) {}
            break;
        case 'R':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'force-reload-tab'); } catch (err) {}
            break;
        case 'H':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'history-back'); } catch (err) {}
            break;
        case 'L':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'history-forward'); } catch (err) {}
            break;
        case '/':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-find'); } catch (err) {}
            break;
        case 'f':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'trigger-hints'); } catch (err) {}
            break;
        case 'w':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-dashboard'); } catch (err) {}
            break;
        case 's':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'focus-sidebar'); } catch (err) {}
            break;
        case 'm':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-tab-audio-mute'); } catch (err) {}
            break;
        case 'M':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-global-audio-mute'); } catch (err) {}
            break;
        case 'p':
            consumeEvent(e);
            try { ipcRenderer.sendToHost('normal-mode-action', 'toggle-pin-tab'); } catch (err) {}
            break;
        case 'y':
            consumeEvent(e);
            try {
                let currentUrl = window.location.href;
                if (currentUrl.includes('/assets/pdfjs/viewer.html') && currentUrl.includes('file=')) {
                    try {
                        const parsed = new URL(currentUrl);
                        const fileParam = parsed.searchParams.get('file');
                        if (fileParam) currentUrl = fileParam;
                    } catch (urlErr) {}
                }
                navigator.clipboard.writeText(currentUrl).catch(() => {});
                ipcRenderer.sendToHost('normal-mode-action', 'yank-url', currentUrl);
            } catch (err) {}
            break;
    }
}, true);
