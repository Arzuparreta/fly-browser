// modules/userScriptShim.js
// Userscript execution wrapper and GM_* API shim for Fly Browser

export function buildUserScriptWrapper(item) {
    const token = item.token || '';
    const scriptId = item.scriptId || item.name || 'script';
    const initialStorage = item.storage || {};
    const meta = item.meta || { name: item.name || scriptId };

    const tokenJson = JSON.stringify(token);
    const scriptIdJson = JSON.stringify(scriptId);
    const storageJson = JSON.stringify(initialStorage);
    const metaJson = JSON.stringify(meta);
    const sourceUrlName = encodeURIComponent((item.name || scriptId).replace(/\s+/g, '_'));

    return `(function(__gm_token__, __gm_script_id__, __gm_initial_storage__, __gm_meta__) {
    'use strict';

    if (!window.__flyInjectedScripts) {
        try {
            Object.defineProperty(window, '__flyInjectedScripts', {
                value: new Set(),
                writable: true,
                configurable: true,
                enumerable: false
            });
        } catch (e) {
            window.__flyInjectedScripts = new Set();
        }
    }
    if (window.__flyInjectedScripts.has(__gm_script_id__)) {
        return;
    }
    window.__flyInjectedScripts.add(__gm_script_id__);

    var __gm_store = Object.assign({}, __gm_initial_storage__ || {});
    var __gm_listeners = new Map();
    var __gm_listener_id = 1;

    var GM_getValue = function(key, defaultValue) {
        if (key === undefined || key === null) return defaultValue;
        var k = String(key);
        return Object.prototype.hasOwnProperty.call(__gm_store, k) ? __gm_store[k] : defaultValue;
    };

    var GM_setValue = function(key, value) {
        if (key === undefined || key === null) return;
        var k = String(key);
        var oldVal = __gm_store[k];
        __gm_store[k] = value;
        for (var pair of __gm_listeners.entries()) {
            var listener = pair[1];
            if (listener.key === k) {
                try { listener.fn(k, oldVal, value, false); } catch (e) {}
            }
        }
        if (window.__flyGMBridge && typeof window.__flyGMBridge.setStorageValue === 'function') {
            window.__flyGMBridge.setStorageValue(__gm_token__, __gm_script_id__, k, value).catch(function() {});
        }
    };

    var GM_deleteValue = function(key) {
        if (key === undefined || key === null) return;
        var k = String(key);
        var oldVal = __gm_store[k];
        delete __gm_store[k];
        for (var pair of __gm_listeners.entries()) {
            var listener = pair[1];
            if (listener.key === k) {
                try { listener.fn(k, oldVal, undefined, false); } catch (e) {}
            }
        }
        if (window.__flyGMBridge && typeof window.__flyGMBridge.deleteStorageValue === 'function') {
            window.__flyGMBridge.deleteStorageValue(__gm_token__, __gm_script_id__, k).catch(function() {});
        }
    };

    var GM_listValues = function() {
        return Object.keys(__gm_store);
    };

    var GM_addValueChangeListener = function(name, callback) {
        var id = __gm_listener_id++;
        __gm_listeners.set(id, { key: String(name), fn: callback });
        return id;
    };

    var GM_removeValueChangeListener = function(id) {
        __gm_listeners.delete(id);
    };

    var GM_xmlhttpRequest = function(details) {
        if (!details || typeof details !== 'object') {
            throw new Error('GM_xmlhttpRequest requires an options object');
        }
        if (!details.url) {
            throw new Error('GM_xmlhttpRequest requires a url property');
        }

        var isAborted = false;

        if (typeof details.onreadystatechange === 'function') {
            try { details.onreadystatechange({ readyState: 1 }); } catch (e) {}
        }

        if (!window.__flyGMBridge || typeof window.__flyGMBridge.request !== 'function') {
            var bridgeErr = { isTimeout: false, message: 'Fly GM bridge unavailable', status: 0, statusText: 'Error' };
            if (typeof details.onerror === 'function') details.onerror(bridgeErr);
            return { abort: function() {} };
        }

        var payload = {
            method: (details.method || 'GET').toUpperCase(),
            url: details.url,
            headers: details.headers || {},
            data: details.data || details.body || null,
            timeout: Number(details.timeout) || 30000
        };

        window.__flyGMBridge.request(__gm_token__, payload)
            .then(function(res) {
                if (isAborted) return;

                var parsedResponse = res.responseText;
                if (details.responseType === 'json') {
                    try {
                        parsedResponse = JSON.parse(res.responseText);
                    } catch (e) {
                        parsedResponse = null;
                    }
                }

                var responseXML = null;
                if (details.responseType === 'document' || !details.responseType) {
                    try {
                        responseXML = new DOMParser().parseFromString(res.responseText, 'text/html');
                    } catch (e) {}
                }

                var fullResponse = {
                    status: res.status,
                    statusText: res.statusText,
                    readyState: 4,
                    responseText: res.responseText,
                    response: parsedResponse,
                    responseXML: responseXML,
                    responseHeaders: res.responseHeaders || '',
                    finalUrl: res.finalUrl || details.url,
                    context: details.context,
                    getResponseHeader: function(headerName) {
                        if (!headerName || !this.responseHeaders) return null;
                        var escaped = headerName.replace(/[-/\\\\^$*+?.()|[\\]{}]/g, '\\\\$&');
                        var re = new RegExp('^' + escaped + ':\\\\s*(.*)$', 'im');
                        var match = this.responseHeaders.match(re);
                        return match ? match[1].trim() : null;
                    },
                    getAllResponseHeaders: function() {
                        return this.responseHeaders || '';
                    }
                };

                if (typeof details.onreadystatechange === 'function') {
                    try { details.onreadystatechange(fullResponse); } catch (e) {}
                }
                if (typeof details.onload === 'function') {
                    try { details.onload(fullResponse); } catch (e) {}
                }
                if (typeof details.onloadend === 'function') {
                    try { details.onloadend(fullResponse); } catch (e) {}
                }
            })
            .catch(function(err) {
                if (isAborted) return;
                var errObj = {
                    status: (err && err.status) || 0,
                    statusText: (err && err.statusText) || 'Error',
                    readyState: 4,
                    responseText: '',
                    response: null,
                    context: details.context,
                    error: (err && err.message) || 'Request failed'
                };

                if (err && err.isTimeout) {
                    if (typeof details.ontimeout === 'function') {
                        try { details.ontimeout(errObj); } catch (e) {}
                    }
                } else {
                    if (typeof details.onerror === 'function') {
                        try { details.onerror(errObj); } catch (e) {}
                    }
                }
                if (typeof details.onloadend === 'function') {
                    try { details.onloadend(errObj); } catch (e) {}
                }
            });

        return {
            abort: function() {
                isAborted = true;
                if (typeof details.onabort === 'function') {
                    try { details.onabort({ readyState: 0, status: 0, statusText: 'Aborted' }); } catch (e) {}
                }
            }
        };
    };

    var GM_addStyle = function(css) {
        if (!css || typeof css !== 'string') return null;
        var style = document.createElement('style');
        style.setAttribute('type', 'text/css');
        style.textContent = css;
        var target = document.head || document.documentElement || document.body;
        if (target) {
            target.appendChild(style);
        }
        return style;
    };

    var __gm_menu_commands = new Map();
    var __gm_menu_id = 1;

    var GM_registerMenuCommand = function(caption, onClick, accessKey) {
        var id = __gm_menu_id++;
        __gm_menu_commands.set(id, { caption: caption, onClick: onClick, accessKey: accessKey });
        return id;
    };

    var GM_unregisterMenuCommand = function(id) {
        __gm_menu_commands.delete(id);
    };

    var GM_setClipboard = function(data) {
        if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
            navigator.clipboard.writeText(String(data)).catch(function() {});
        }
    };

    var GM_log = function() {
        var args = Array.prototype.slice.call(arguments);
        args.unshift('[Fly UserScript: ' + (__gm_meta__.name || __gm_script_id__) + ']');
        console.log.apply(console, args);
    };

    var GM_openInTab = function(url) {
        return window.open(url, '_blank');
    };

    var GM_notification = function(textOrOptions, title, image, onclick) {
        var opts = {};
        if (typeof textOrOptions === 'object' && textOrOptions !== null) {
            opts = textOrOptions;
        } else {
            opts = { text: String(textOrOptions || ''), title: title || '', image: image || '', onclick: onclick };
        }
        if (typeof Notification !== 'undefined' && Notification.permission === 'granted') {
            try {
                var notif = new Notification(opts.title || (__gm_meta__.name || 'Fly UserScript'), {
                    body: opts.text || '',
                    icon: opts.image || undefined
                });
                if (typeof opts.onclick === 'function') {
                    notif.onclick = opts.onclick;
                }
                return notif;
            } catch (e) {}
        }
        console.log('[Fly UserScript Notification]', opts.title || '', opts.text || '');
        return null;
    };

    var GM_info = {
        script: {
            name: __gm_meta__.name || __gm_script_id__,
            version: __gm_meta__.version || '1.0',
            description: __gm_meta__.description || '',
            author: __gm_meta__.author || '',
            matches: __gm_meta__.matches || [],
            includes: __gm_meta__.includes || [],
            excludes: __gm_meta__.excludes || [],
            connects: __gm_meta__.connects || [],
            grants: __gm_meta__.grants || [],
            runAt: __gm_meta__.runAt || 'document-end'
        },
        scriptHandler: 'Fly Browser',
        version: '0.26.0'
    };

    var unsafeWindow = window;

    var GM = {
        info: GM_info,
        getValue: function(key, defaultValue) { return Promise.resolve(GM_getValue(key, defaultValue)); },
        setValue: function(key, value) { GM_setValue(key, value); return Promise.resolve(); },
        deleteValue: function(key) { GM_deleteValue(key); return Promise.resolve(); },
        listValues: function() { return Promise.resolve(GM_listValues()); },
        xmlHttpRequest: function(details) {
            return new Promise(function(resolve, reject) {
                var d = Object.assign({}, details, {
                    onload: function(res) {
                        if (details && typeof details.onload === 'function') details.onload(res);
                        resolve(res);
                    },
                    onerror: function(err) {
                        if (details && typeof details.onerror === 'function') details.onerror(err);
                        reject(err);
                    },
                    ontimeout: function(err) {
                        if (details && typeof details.ontimeout === 'function') details.ontimeout(err);
                        reject(err);
                    }
                });
                GM_xmlhttpRequest(d);
            });
        },
        addStyle: function(css) { return Promise.resolve(GM_addStyle(css)); },
        registerMenuCommand: function(caption, onClick, accessKey) { return Promise.resolve(GM_registerMenuCommand(caption, onClick, accessKey)); },
        setClipboard: function(data, info) { return Promise.resolve(GM_setClipboard(data, info)); },
        openInTab: function(url, options) { return Promise.resolve(GM_openInTab(url, options)); },
        notification: function(options) { return Promise.resolve(GM_notification(options)); }
    };

    try {
        (function() {
${item.code}
        }).call(window);
    } catch (err) {
        console.error('[Fly UserScript: ' + (__gm_meta__.name || __gm_script_id__) + ']', err);
    }
})(${tokenJson}, ${scriptIdJson}, ${storageJson}, ${metaJson});
//# sourceURL=fly-userscript://${sourceUrlName}.user.js`;
}
