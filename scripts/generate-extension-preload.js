const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
let source = fs.readFileSync(require.resolve('electron-chrome-extensions/preload'), 'utf8');
if (!source.includes('managed: local,') || !source.includes('      Object.freeze(chrome);')) {
    throw new Error('Review the extension preload adapter for this dependency version');
}
const eventsStart = source.indexOf('  var listenerMap =');
const eventsEnd = source.indexOf('  // src/renderer/index.ts', eventsStart);
if (eventsStart < 0 || eventsEnd < 0 || (source.match(/if \(true\)/g) || []).length !== 3) {
    throw new Error('Review extension listener cleanup and debug logging for this dependency version');
}
// The dependency removes the caller's callback, but registered an anonymous
// wrapper. Retain that wrapper so removal actually releases it, and implement
// Chrome's duplicate-listener semantics instead of accumulating callbacks.
source = source.slice(0, eventsStart) + `  var listenerMap = new Map();
  var addExtensionListener = (extensionId, name, callback) => {
    let callbacks = listenerMap.get(name);
    if (!callbacks) {
      callbacks = new Map();
      listenerMap.set(name, callbacks);
      import_electron.ipcRenderer.send("crx-add-listener", extensionId, name);
    }
    if (callbacks.has(callback)) return;
    const listener = (_event, ...args) => callback(...args);
    callbacks.set(callback, listener);
    import_electron.ipcRenderer.on(formatIpcName(name), listener);
  };
  var removeExtensionListener = (extensionId, name, callback) => {
    const callbacks = listenerMap.get(name);
    const listener = callbacks?.get(callback);
    if (!listener) return;
    import_electron.ipcRenderer.removeListener(formatIpcName(name), listener);
    callbacks.delete(callback);
    if (callbacks.size === 0) {
      listenerMap.delete(name);
      import_electron.ipcRenderer.send("crx-remove-listener", extensionId, name);
    }
  };

` + source.slice(eventsEnd);
// These remaining guards belong to unconditional development console logging.
source = source.replaceAll('if (true) {', 'if (false) {');
source = source.replace('      Object.freeze(chrome);', `      // Extensions such as uBlock prefer browser, then assign it back to chrome.
      // Both aliases must reference the API object extended by this preload.
      globalThis.browser = chrome;
      Object.freeze(chrome);`).replace('managed: local,', `managed: {
                get: (keys, callback) => {
                  if (typeof keys === 'function') callback = keys;
                  if (callback) queueMicrotask(() => callback({}));
                  return Promise.resolve({});
                }
              },`);
source = '// Generated from electron-chrome-extensions 4.9.0 (GPL-3.0).\n// Run node scripts/generate-extension-preload.js after dependency changes.\n' + source;
const file = path.join(root, 'extension-api.preload.js');
if (process.argv.includes('--check')) {
    if (fs.readFileSync(file, 'utf8') !== source) throw new Error('Run node scripts/generate-extension-preload.js');
} else fs.writeFileSync(file, source);
