const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn, spawnSync} = require('node:child_process');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-launcher-'));
try {
    fs.writeFileSync(path.join(temporary, 'launch.sh'), fs.readFileSync(path.join(__dirname, '../legacy/electron/launch.sh'), 'utf8').replace('/../..', ''));
    fs.mkdirSync(path.join(temporary, 'node_modules/.bin'), {recursive:true});
    for (const file of ['package.json', 'package-lock.json', 'node_modules/.package-lock.json']) fs.writeFileSync(path.join(temporary, file), '{}');
    const future = new Date(Date.now()+10000);
    fs.utimesSync(path.join(temporary, 'node_modules/.package-lock.json'), future, future);
    fs.writeFileSync(path.join(temporary, 'node_modules/.bin/install-electron'), '#!/bin/sh\nexit 0\n', {mode:0o755});
    fs.writeFileSync(path.join(temporary, 'node_modules/.bin/electron'), '#!/bin/sh\nprintf "%s\\n" "$@"\necho diagnostic >&2\nexit 17\n', {mode:0o755});
    const display = {...process.env, DISPLAY:':0'};
    const run = spawnSync('bash', [path.join(temporary, 'launch.sh'), 'https://example.org/a b'], {encoding:'utf8', env:display});
    assert.equal(run.status, 17, 'Launcher must wait for Electron and preserve its exit status');
    assert.equal(run.stdout, '.\n--log-level=3\nhttps://example.org/a b\n');
    assert.equal(run.stderr, 'diagnostic\n');
    console.log('Source launcher preserves arguments, diagnostics and Electron exit status without backgrounding.');

    // Without a display Electron would exit silently, so the launcher says why instead.
    const headless = {...process.env}; delete headless.DISPLAY; delete headless.WAYLAND_DISPLAY;
    const noDisplay = spawnSync('bash', [path.join(temporary, 'launch.sh')], {encoding:'utf8', env:headless});
    assert.equal(noDisplay.status, 1);
    assert.equal(noDisplay.stdout, '', 'Electron must not start without a display');
    assert.match(noDisplay.stderr, /neither DISPLAY nor WAYLAND_DISPLAY is set.*DISPLAY=:0 \.\/launch\.sh/);
    const wayland = spawnSync('bash', [path.join(temporary, 'launch.sh')], {encoding:'utf8', env:{...headless, WAYLAND_DISPLAY:'wayland-0'}});
    assert.equal(wayland.status, 17, 'A Wayland session alone is enough');
    console.log('Source launcher explains a missing display instead of exiting silently.');

    // A Fly running from this checkout must not have node_modules reinstalled under it.
    const real = fs.realpathSync(temporary);
    fs.mkdirSync(path.join(real, 'node_modules/electron/dist'), {recursive:true});
    // Node stands in for Electron: unlike multi-call coreutils (sleep on
    // Ubuntu 26.04), it does not depend on the name it is started under.
    fs.symlinkSync(process.execPath, path.join(real, 'node_modules/electron/dist/electron'));
    fs.mkdirSync(path.join(real, 'bin'));
    fs.writeFileSync(path.join(real, 'bin/npm'), '#!/bin/sh\necho npm-ran\nexit 0\n', {mode:0o755});
    const past = new Date(Date.now()-10000);
    fs.utimesSync(path.join(real, 'node_modules/.package-lock.json'), past, past);
    const running = spawn(path.join(real, 'node_modules/electron/dist/electron'), ['-e', 'setTimeout(() => {}, 30000)'], {stdio:'ignore'});
    try {
        const env = {...display, PATH:`${path.join(real, 'bin')}:${process.env.PATH}`};
        const blocked = spawnSync('bash', [path.join(real, 'launch.sh')], {encoding:'utf8', env});
        assert.equal(blocked.status, 1, 'Launcher must refuse to reinstall while Fly runs from this checkout');
        assert.doesNotMatch(blocked.stdout, /npm-ran/);
        assert.match(blocked.stderr, /already running from this folder/);
    } finally { running.kill(); }
    const free = spawnSync('bash', [path.join(real, 'launch.sh')], {encoding:'utf8', env:{...display, PATH:`${path.join(real, 'bin')}:${process.env.PATH}`}});
    assert.match(free.stdout, /npm-ran/, 'Launcher must reinstall once Fly is closed');
    console.log('Source launcher does not reinstall dependencies under a running Fly.');
} finally { fs.rmSync(temporary, {recursive:true, force:true}); }
