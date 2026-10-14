const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-launcher-'));
try {
    fs.copyFileSync(path.join(__dirname, '../launch.sh'), path.join(temporary, 'launch.sh'));
    fs.mkdirSync(path.join(temporary, 'node_modules/.bin'), {recursive:true});
    for (const file of ['package.json', 'package-lock.json', 'node_modules/.package-lock.json']) fs.writeFileSync(path.join(temporary, file), '{}');
    const future = new Date(Date.now()+10000);
    fs.utimesSync(path.join(temporary, 'node_modules/.package-lock.json'), future, future);
    fs.writeFileSync(path.join(temporary, 'node_modules/.bin/install-electron'), '#!/bin/sh\nexit 0\n', {mode:0o755});
    fs.writeFileSync(path.join(temporary, 'node_modules/.bin/electron'), '#!/bin/sh\nprintf "%s\\n" "$@"\necho diagnostic >&2\nexit 17\n', {mode:0o755});
    const run = spawnSync('bash', [path.join(temporary, 'launch.sh'), 'https://example.org/a b'], {encoding:'utf8'});
    assert.equal(run.status, 17, 'Launcher must wait for Electron and preserve its exit status');
    assert.equal(run.stdout, '.\n--log-level=3\nhttps://example.org/a b\n');
    assert.equal(run.stderr, 'diagnostic\n');
    console.log('Source launcher preserves arguments, diagnostics and Electron exit status without backgrounding.');
} finally { fs.rmSync(temporary, {recursive:true, force:true}); }
