const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
let source = fs.readFileSync(require.resolve('electron-chrome-extensions/preload'), 'utf8');
if (!source.includes('managed: local,') || !source.includes('      Object.freeze(chrome);')) {
    throw new Error('Review the extension preload adapter for this dependency version');
}
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
