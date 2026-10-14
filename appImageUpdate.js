// Updates a running AppImage in place, the one Fly package that can replace
// itself. The AUR package updates with the system, and the unsigned macOS and
// Windows builds keep the release link.
//
// electron-updater finds the release, downloads it (differentially when it
// can) and checks it against the SHA-512 in latest-linux.yml. This module puts
// the file in place and restarts Fly from it.
//
// Neither ready-made restart works for an AppImage. electron-updater's starts
// the new AppImage before this one has quit, so the new instance finds Fly's
// single-instance lock still held and exits. app.relaunch() hands the restart
// to a helper running from this AppImage's mount, which disappears as Fly
// exits. A shell outside the mount waits for this process instead.
'use strict';
const fs = require('fs');
const path = require('path');
const { spawn: spawnProcess } = require('child_process');

// Set by the AppImage runtime for the mount it started; the new AppImage sets its own.
const MOUNT_VARIABLES = ['APPDIR', 'APPIMAGE', 'ARGV0', 'OWD'];

// Where the new release goes: over the old file when its name carries no
// version (the user chose it), otherwise beside it under the release's own
// name, as electron-updater does.
function destinationFor(current, downloaded) {
    if (!/\d+\.\d+\.\d+/.test(path.basename(current))) return current;
    return path.join(path.dirname(current), path.basename(downloaded));
}

// Copies first and renames over, so an interrupted update leaves the old
// AppImage working.
function replaceAppImage(current, downloaded) {
    const destination = destinationFor(current, downloaded);
    const temporary = path.join(path.dirname(destination), `.${path.basename(destination)}.${process.pid}.part`);
    try {
        fs.copyFileSync(downloaded, temporary);
        fs.chmodSync(temporary, 0o755);
        fs.renameSync(temporary, destination);
    } catch (err) {
        fs.rmSync(temporary, { force: true });
        throw err;
    }
    if (destination !== current) fs.rmSync(current, { force: true });
    fs.rmSync(downloaded, { force: true });
    return destination;
}

// updater: electron-updater's autoUpdater. onProgress receives whole percents.
// Resolves to the new AppImage's path, or null when no newer release has one.
async function downloadAndReplace({ updater, appImage, onProgress = () => {} }) {
    const progress = info => onProgress(Math.floor(info.percent || 0));
    updater.on('download-progress', progress);
    try {
        const result = await updater.checkForUpdates();
        if (!result || !result.isUpdateAvailable) return null;
        const [downloaded] = await updater.downloadUpdate();
        if (!downloaded) throw new Error('The update was not downloaded');
        return replaceAppImage(appImage, downloaded);
    } finally {
        updater.removeListener('download-progress', progress);
    }
}

const WAIT = 'while kill -0 "$1" 2>/dev/null; do sleep 0.2; done';
// Chromium leaves descriptors open across exec, and those into the old mount
// would keep it, and the deleted AppImage, alive for as long as the new Fly
// runs. Only bash can close descriptors above 9, so a shell without it skips this.
const CLOSE_INHERITED = 'for fd in /proc/$$/fd/*; do fd=${fd##*/}; [ "$fd" -gt 2 ] && eval "exec $fd<&-"; done 2>/dev/null';

// Starts the AppImage at `destination` once process `pid` has exited.
function restartInto(destination, { pid = process.pid, env = process.env, spawn = spawnProcess, exists = fs.existsSync } = {}) {
    const cleanEnv = { ...env };
    for (const name of MOUNT_VARIABLES) delete cleanEnv[name];
    const bash = ['/bin/bash', '/usr/bin/bash'].find(file => exists(file));
    const script = bash ? `${WAIT}; ${CLOSE_INHERITED}; exec "$2"` : `${WAIT}; exec "$2"`;
    const child = spawn(bash || '/bin/sh', ['-c', script, 'fly-update', String(pid), destination], {
        detached: true,
        stdio: 'ignore',
        env: cleanEnv
    });
    child.unref();
}

module.exports = { destinationFor, replaceAppImage, downloadAndReplace, restartInto };
