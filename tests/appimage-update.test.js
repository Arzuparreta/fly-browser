// An AppImage update replaces the file the user runs. A mistake here leaves
// them with no browser, so test where the new file goes and that a failure
// keeps the old one.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { EventEmitter } = require('node:events');
const { destinationFor, replaceAppImage, downloadAndReplace, restartInto } = require('../appImageUpdate');

// A versioned name follows the release; a name the user chose is kept.
assert.equal(destinationFor('/apps/Fly-0.16.1-x86_64.AppImage', '/cache/Fly-0.17.0-x86_64.AppImage'), '/apps/Fly-0.17.0-x86_64.AppImage');
assert.equal(destinationFor('/apps/fly.AppImage', '/cache/Fly-0.17.0-x86_64.AppImage'), '/apps/fly.AppImage');

// The restart waits for this process from outside the AppImage's mount and
// leaves the new AppImage to describe its own mount.
const spawned = [];
const restart = shells => restartInto('/apps/Fly 2.AppImage', { pid: 4242, env: { HOME: '/home/me', APPDIR: '/tmp/.mount_x', APPIMAGE: '/apps/Fly.AppImage', ARGV0: 'Fly', OWD: '/home/me' }, exists: file => shells.includes(file), spawn: (command, args, options) => {
    spawned.push({ command, args, options });
    return { unref() { spawned.push('unref'); } };
} });
restart(['/usr/bin/bash']);
assert.equal(spawned[0].command, '/usr/bin/bash');
assert.match(spawned[0].args[1], /^while kill -0 "\$1".*exec \$fd<&-.*exec "\$2"$/, 'Waits, closes inherited descriptors, then starts the new AppImage');
assert.deepEqual(spawned[0].args.slice(2), ['fly-update', '4242', '/apps/Fly 2.AppImage']);
assert.deepEqual(spawned[0].options, { detached: true, stdio: 'ignore', env: { HOME: '/home/me' } });
assert.equal(spawned[1], 'unref');
restart([]);
assert.equal(spawned[2].command, '/bin/sh');
assert.doesNotMatch(spawned[2].args[1], /<&-/, 'A POSIX shell cannot close descriptors above 9');

// The scripts themselves run: they wait for the process and start the target.
const { execFileSync } = require('node:child_process');
for (const shell of ['/bin/sh', '/bin/bash', '/usr/bin/bash'].filter(fs.existsSync)) {
    let started;
    restartInto('/bin/true', { pid: 999999999, env: {}, exists: file => file === shell, spawn: (command, args) => {
        started = execFileSync(command, [args[0], args[1].replace(/exec "\$2"$/, 'echo "started $2"'), ...args.slice(2)], { encoding: 'utf8' });
        return { unref() {} };
    } });
    assert.equal(started, 'started /bin/true\n', shell);
}

class FakeUpdater extends EventEmitter {
    constructor(result, downloaded) { super(); this.result = result; this.downloaded = downloaded; }
    async checkForUpdates() { return this.result; }
    async downloadUpdate() {
        this.emit('download-progress', { percent: 41.7 });
        this.emit('download-progress', { percent: 100 });
        return [this.downloaded];
    }
}

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-appimage-update-'));
    try {
        const apps = path.join(temporary, 'My Apps');
        const cache = path.join(temporary, 'cache');
        fs.mkdirSync(apps);
        fs.mkdirSync(cache);
        const current = path.join(apps, 'Fly-0.16.1-x86_64.AppImage');
        const write = (file, content) => { fs.writeFileSync(file, content); return file; };

        // A newer release: the file takes the release's name, is executable, and
        // the old file and the download are gone.
        write(current, 'old');
        let downloaded = write(path.join(cache, 'Fly-0.17.0-x86_64.AppImage'), 'new');
        const percents = [];
        const updater = new FakeUpdater({ isUpdateAvailable: true }, downloaded);
        const destination = await downloadAndReplace({ updater, appImage: current, onProgress: percent => percents.push(percent) });
        assert.equal(destination, path.join(apps, 'Fly-0.17.0-x86_64.AppImage'));
        assert.equal(fs.readFileSync(destination, 'utf8'), 'new');
        assert.equal(fs.statSync(destination).mode & 0o777, 0o755);
        assert.equal(fs.existsSync(current), false);
        assert.equal(fs.existsSync(downloaded), false);
        assert.deepEqual(percents, [41, 100]);
        assert.equal(updater.listenerCount('download-progress'), 0, 'Progress listeners do not pile up across attempts');
        assert.deepEqual(fs.readdirSync(apps), ['Fly-0.17.0-x86_64.AppImage'], 'No partial file is left behind');

        // A name the user chose is replaced in place.
        const custom = write(path.join(apps, 'fly.AppImage'), 'old');
        downloaded = write(path.join(cache, 'Fly-0.18.0-x86_64.AppImage'), 'newer');
        assert.equal(replaceAppImage(custom, downloaded), custom);
        assert.equal(fs.readFileSync(custom, 'utf8'), 'newer');

        // Nothing newer: nothing downloaded or touched.
        let downloads = 0;
        const idle = new FakeUpdater({ isUpdateAvailable: false }, null);
        idle.downloadUpdate = async () => { downloads++; return []; };
        assert.equal(await downloadAndReplace({ updater: idle, appImage: custom }), null);
        assert.equal(downloads, 0);

        // A failed copy keeps the old AppImage and leaves no partial file.
        fs.writeFileSync(current, 'old');
        await assert.rejects(downloadAndReplace({ updater: new FakeUpdater({ isUpdateAvailable: true }, path.join(cache, 'missing-1.0.0.AppImage')), appImage: current }));
        assert.equal(fs.readFileSync(current, 'utf8'), 'old');
        assert.equal(fs.readdirSync(apps).some(name => name.endsWith('.part')), false);
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
    console.log('An AppImage update lands under the right name, keeps the old file on failure and leaves nothing partial.');
})().catch(error => { console.error(error); process.exit(1); });
