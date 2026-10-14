// tests/config-writes.test.js
// Guards against config.json being read half-written and reset to defaults

const assert = require('assert');
const fs = require('fs');
const path = require('path');

console.log('Running Fly config write safety tests...');

const main = fs.readFileSync(path.join(__dirname, '..', 'main.js'), 'utf8');

{
    const directWrites = main.match(/(writeFileSync|writeFile)\(CONFIG_PATH/g) || [];
    assert.strictEqual(directWrites.length, 0, 'config.json must only be written through writeConfigFile');
    assert(/function writeConfigFile\(cfg\)[\s\S]*?renameSync\(tmpPath, CONFIG_PATH\)/.test(main), 'writeConfigFile writes a temporary file and renames it into place');
    console.log('✓ config.json is replaced atomically');
}

console.log('All config write safety tests passed.');
