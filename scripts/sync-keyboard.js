// Sandboxed Electron preloads cannot require local modules. Embed the shared, dependency-free grammar.
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const file = path.join(root, 'webview-preload.js');
const start = '// BEGIN GENERATED KEYBOARD CORE\n';
const end = '// END GENERATED KEYBOARD CORE\n';
let source = fs.readFileSync(file, 'utf8');
if (source.includes(start)) source = source.slice(source.indexOf(end) + end.length);
const bundled = start + fs.readFileSync(path.join(root, 'keyboard.js'), 'utf8').replace("if (typeof module === 'object' && module.exports)", 'if (false)') + end + source;
if (process.argv.includes('--check')) {
    if (fs.readFileSync(file, 'utf8') !== bundled) throw new Error('Run node scripts/sync-keyboard.js after editing keyboard.js');
} else fs.writeFileSync(file, bundled);
