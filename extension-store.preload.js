// A narrow Chrome Web Store bridge. No Fly UI or filesystem APIs enter the page.
// Main-process handlers independently enforce origin, top frame and tab ownership.
if (location.origin === 'https://chromewebstore.google.com' && process.isMainFrame) {
    const { contextBridge, ipcRenderer, webFrame } = require('electron');
    const Result = Object.fromEntries(['SUCCESS', 'USER_CANCELLED', 'INSTALL_ERROR', 'UNKNOWN_ERROR', 'MANIFEST_ERROR',
        'INVALID_ID', 'INVALID_ICON_URL', 'ICON_ERROR', 'INSTALL_IN_PROGRESS', 'ALREADY_INSTALLED', 'BLOCKED_BY_POLICY',
        'FEATURE_DISABLED', 'UNSUPPORTED_EXTENSION_TYPE', 'USER_GESTURE_REQUIRED', 'MISSING_DEPENDENCIES',
        'LAUNCH_IN_PROGRESS', 'BLOCKED_FOR_CHILD_ACCOUNT', 'BLOCKLISTED'].map(key => [key, key.toLowerCase()]));
    const api = {
        Result,
        ExtensionInstallStatus: Object.fromEntries(['INSTALLABLE', 'ENABLED', 'DISABLED', 'BLOCKED_BY_POLICY', 'DEPRECATED_MANIFEST_VERSION'].map(key => [key, key.toLowerCase()])),
        MV2DeprecationStatus: { INACTIVE: 'inactive', WARNING: 'warning', SOFT_DISABLE: 'soft_disable' },
        WebGlStatus: { WEBGL_ALLOWED: 'webgl_allowed', WEBGL_BLOCKED: 'webgl_blocked' }
    };
    const call = (channel, callbackIndex) => async (...args) => {
        const callback = args[callbackIndex];
        const result = await ipcRenderer.invoke(channel, ...args.slice(0, callbackIndex));
        if (typeof callback === 'function') callback(result);
        return result;
    };
    for (const [method, count] of Object.entries({ completeInstall: 1, getExtensionStatus: 2, getFullChromeVersion: 0,
        getMV2DeprecationStatus: 0, getBrowserLogin: 0, getStoreLogin: 0, setStoreLogin: 1, getIsLauncherEnabled: 0,
        enableAppLauncher: 1, getReferrerChain: 0, getWebGLStatus: 0, isInIncognitoMode: 0,
        isPendingCustodianApproval: 1, install: 1 })) api[method] = call(`chromeWebstore.${method}`, count);
    api.beginInstallWithManifest3 = async (details, callback) => {
        const response = await ipcRenderer.invoke('chromeWebstore.beginInstall', details);
        const result = response?.result ?? 'unknown_error';
        await webFrame.executeJavaScript(`chrome.runtime.lastError = chrome.extension.lastError = ${JSON.stringify(response?.message ? { message: response.message } : null)}; void 0;`);
        if (typeof callback === 'function') callback(result);
        return result;
    };
    const event = channel => {
        const listeners = new Map();
        return {
            addListener: callback => {
                if (listeners.has(callback)) return;
                const listener = (_event, value) => callback(value);
                listeners.set(callback, listener); ipcRenderer.on(channel, listener);
            },
            removeListener: callback => { const listener = listeners.get(callback); if (listener) ipcRenderer.removeListener(channel, listener); listeners.delete(callback); },
            hasListener: callback => listeners.has(callback)
        };
    };
    contextBridge.exposeInMainWorld('flyWebStore', {
        api, management: { getAll: call('chrome.management.getAll', 0), setEnabled: call('chrome.management.setEnabled', 2),
            uninstall: (id, options, callback) => call('chrome.management.uninstall', 2)(id, typeof options === 'object' ? options : {}, typeof options === 'function' ? options : callback),
            onInstalled: event('chrome.management.onInstalled'), onUninstalled: event('chrome.management.onUninstalled') }
    });
    webFrame.executeJavaScript(`(() => {
        const chrome = globalThis.chrome ||= {};
        chrome.runtime ||= {}; chrome.extension ||= {}; chrome.management ||= {};
        chrome.runtime.getManifest ||= () => ({});
        chrome.webstorePrivate = flyWebStore.api;
        Object.assign(chrome.management, flyWebStore.management);
    })();`).catch(error => console.error('Chrome Web Store bridge:', error));
}
