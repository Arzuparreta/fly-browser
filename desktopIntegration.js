// Puts Fly where Linux desktops look for applications: an entry in the
// applications menu, which Rofi, Wofi, Fuzzel and every desktop menu read,
// and the default-browser setting.
//
// Packages (the AUR package, a source checkout's `npm run install-desktop`)
// install their own entry. An AppImage cannot, so Fly writes one for it in the
// user's data directory, marked as its own so it never touches anyone else's.
// The entry starts a fixed link, `~/.local/share/fly-browser/fly-browser.AppImage`,
// that points at the AppImage: when the file is moved or replaced by a newer
// release, the next start re-points the link and the menu keeps working.
// A fixed path also keeps spaces out of `Exec`, which xdg-utils cannot parse
// on window managers without a desktop environment.
'use strict';
const fs = require('fs');
const path = require('path');
const { execFile } = require('child_process');

const DESKTOP_ID = 'fly-browser.desktop';
const MARKER = 'X-Fly-AppImage=true';
const ICON_SIZES = [16, 24, 32, 48, 64, 128, 256, 512];
// The types xdg-settings claims when it makes a browser the default.
const BROWSER_TYPES = ['x-scheme-handler/http', 'x-scheme-handler/https', 'text/html', 'application/xhtml+xml'];

function runCommand(command, args) {
    return new Promise(resolve => {
        execFile(command, args, { timeout: 10000 }, (error, stdout) => {
            resolve({ ok: !error, missing: !!(error && error.code === 'ENOENT'), stdout: String(stdout || '').trim() });
        });
    });
}

function absolute(value) {
    return value && path.isAbsolute(value) ? value : null;
}

// A desktop-entry string value: backslashes, newlines and control characters escaped.
function entryString(value) {
    return String(value).replace(/\\/g, '\\\\').replace(/\n/g, '\\n').replace(/\r/g, '\\r').replace(/\t/g, '\\t');
}

// One Exec argument: plain when it can be, otherwise quoted by the Desktop
// Entry rules and then escaped as a string value.
function execArgument(value) {
    const escaped = /^[A-Za-z0-9_\/.+,:@=-]+$/.test(value) ? value : `"${value.replace(/[\\"`$]/g, c => '\\' + c)}"`;
    return entryString(escaped).replace(/%/g, '%%');
}

function createDesktopIntegration({ env = process.env, platform = process.platform, appImage = env.APPIMAGE, resourcesDir, template, run = runCommand } = {}) {
    const home = env.HOME || '';
    const dataHome = absolute(env.XDG_DATA_HOME) || path.join(home, '.local', 'share');
    const dataDirs = (env.XDG_DATA_DIRS || '/usr/local/share:/usr/share').split(':').filter(dir => absolute(dir));
    const userEntry = path.join(dataHome, 'applications', DESKTOP_ID);
    const link = path.join(dataHome, 'fly-browser', 'fly-browser.AppImage');
    const runningAppImage = platform === 'linux' && absolute(appImage) ? appImage : null;

    function read(file) {
        try { return fs.readFileSync(file, 'utf8'); } catch (e) { return null; }
    }

    function isOurs(content) {
        return !!content && content.split('\n').some(line => line.trim() === MARKER);
    }

    // The entry the desktop would use: the user's own first, then the system's.
    function findEntry() {
        for (const dir of [dataHome, ...dataDirs]) {
            const file = path.join(dir, 'applications', DESKTOP_ID);
            const content = read(file);
            if (content !== null) return { path: file, ours: isOurs(content) };
        }
        return null;
    }

    function linkTarget() {
        try { return fs.readlinkSync(link); } catch (e) { return null; }
    }

    function entryContent() {
        const lines = String(template || '').split('\n').filter(line => line.trim() && !/^(Exec|TryExec|X-Fly-AppImage)=/.test(line));
        if (lines[0] !== '[Desktop Entry]') throw new Error('The desktop entry template is missing');
        const exec = execArgument(link);
        return [...lines, `Exec=${exec} %u`, `TryExec=${entryString(link)}`, MARKER].join('\n') + '\n';
    }

    function writeAtomically(file, content) {
        fs.mkdirSync(path.dirname(file), { recursive: true });
        const temporary = `${file}.${process.pid}.tmp`;
        fs.writeFileSync(temporary, content, { mode: 0o644 });
        fs.renameSync(temporary, file);
    }

    function pointLink() {
        if (linkTarget() === runningAppImage) return false;
        fs.mkdirSync(path.dirname(link), { recursive: true });
        const temporary = `${link}.${process.pid}.tmp`;
        fs.rmSync(temporary, { force: true });
        fs.symlinkSync(runningAppImage, temporary);
        fs.renameSync(temporary, link);
        return true;
    }

    function iconPath(size) {
        return path.join(dataHome, 'icons', 'hicolor', `${size}x${size}`, 'apps', 'fly-browser.png');
    }

    async function refreshDatabase() {
        // Keeps the user's MIME cache current where one exists; harmless where not.
        await run('update-desktop-database', ['-q', path.dirname(userEntry)]);
    }

    async function status() {
        if (platform !== 'linux') return { supported: false };
        const entry = findEntry();
        const current = await defaultBrowser();
        return {
            supported: true,
            appImage: runningAppImage,
            entry,
            canInstall: !!runningAppImage,
            defaultBrowser: current,
            isDefault: current === DESKTOP_ID,
            canMakeDefault: current !== undefined && (!!entry || !!runningAppImage)
        };
    }

    // Prompting makes sense only for an AppImage that no entry launches yet.
    function shouldOffer() {
        return !!runningAppImage && findEntry() === null;
    }

    async function install() {
        if (!runningAppImage) throw new Error('Only an AppImage needs Fly to add its menu entry');
        pointLink();
        for (const size of ICON_SIZES) {
            const source = path.join(resourcesDir || '', 'icons', `${size}x${size}.png`);
            if (!fs.existsSync(source)) continue;
            fs.mkdirSync(path.dirname(iconPath(size)), { recursive: true });
            fs.copyFileSync(source, iconPath(size));
        }
        writeAtomically(userEntry, entryContent());
        await refreshDatabase();
        return status();
    }

    async function remove() {
        if (!isOurs(read(userEntry))) throw new Error('The menu entry was not added by Fly, so Fly leaves it alone');
        fs.rmSync(userEntry, { force: true });
        fs.rmSync(link, { force: true });
        for (const size of ICON_SIZES) fs.rmSync(iconPath(size), { force: true });
        await refreshDatabase();
        return status();
    }

    // At every start: an entry Fly wrote follows this AppImage wherever it now is,
    // and picks up changes to the shipped entry. Returns whether anything changed.
    function refresh() {
        if (!runningAppImage) return false;
        const content = read(userEntry);
        if (!isOurs(content)) return false;
        let changed = pointLink();
        const wanted = entryContent();
        if (content !== wanted) {
            writeAtomically(userEntry, wanted);
            changed = true;
        }
        return changed;
    }

    // The desktop id of the default browser, null when none is set, or
    // undefined when this system has no xdg-utils to ask.
    async function defaultBrowser() {
        const settings = await run('xdg-settings', ['get', 'default-web-browser']);
        if (settings.ok && settings.stdout) return settings.stdout;
        const mime = await run('xdg-mime', ['query', 'default', 'x-scheme-handler/https']);
        if (mime.ok) return mime.stdout || null;
        return settings.missing && mime.missing ? undefined : null;
    }

    async function makeDefault() {
        if (findEntry() === null) {
            if (!runningAppImage) throw new Error('Fly has no desktop entry to make the default');
            await install();
        }
        // xdg-settings uses the desktop's own setting where it knows one (KDE's,
        // for example). Without a desktop environment it refuses entries whose
        // Exec it cannot resolve, so the MIME defaults it would have written are
        // written directly instead.
        const settings = await run('xdg-settings', ['set', 'default-web-browser', DESKTOP_ID]);
        if (!settings.ok) {
            const mime = await run('xdg-mime', ['default', DESKTOP_ID, ...BROWSER_TYPES]);
            if (!mime.ok) throw new Error(mime.missing ? 'xdg-utils is not installed' : 'The desktop refused the change');
        }
        return status();
    }

    return { status, shouldOffer, install, remove, refresh, makeDefault, defaultBrowser, entryContent, paths: { userEntry, link } };
}

module.exports = { createDesktopIntegration, execArgument, entryString, DESKTOP_ID, ICON_SIZES };
