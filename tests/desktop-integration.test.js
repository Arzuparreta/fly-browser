// Fly writes into the user's application menu and default-browser setting only
// when it runs from an AppImage and is asked to. Test that it finds the entries
// packages install, never takes over one it did not write, and follows the
// AppImage when it moves.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createDesktopIntegration, execArgument, DESKTOP_ID, ICON_SIZES } = require('../desktopIntegration');

const root = path.join(__dirname, '..');
const template = fs.readFileSync(path.join(root, 'fly-browser.desktop'), 'utf8');

// Exec arguments survive spaces, quotes and the characters the spec reserves.
assert.equal(execArgument('/home/me/.local/share/fly-browser/fly-browser.AppImage'), '/home/me/.local/share/fly-browser/fly-browser.AppImage');
assert.equal(execArgument('/home/my user/Fly.AppImage'), '"/home/my user/Fly.AppImage"');
assert.equal(execArgument('/a"b$c`d\\e%f'), '"/a\\\\"b\\\\$c\\\\`d\\\\\\\\e%%f"');

function fakeDesktop() {
    const calls = [];
    const state = { browser: 'firefox.desktop', settingsWorks: true, missing: false };
    const run = async (command, args) => {
        calls.push([command, ...args]);
        if (state.missing) return { ok: false, missing: true, stdout: '' };
        if (command === 'xdg-settings' && args[0] === 'get') return { ok: true, missing: false, stdout: state.browser };
        if (command === 'xdg-settings' && args[0] === 'set') {
            if (!state.settingsWorks) return { ok: false, missing: false, stdout: '' };
            state.browser = args[2];
            return { ok: true, missing: false, stdout: '' };
        }
        if (command === 'xdg-mime' && args[0] === 'default') { state.browser = args[1]; return { ok: true, missing: false, stdout: '' }; }
        if (command === 'xdg-mime' && args[0] === 'query') return { ok: true, missing: false, stdout: state.browser };
        return { ok: true, missing: false, stdout: '' };
    };
    return { calls, state, run };
}

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-desktop-'));
    try {
        const home = path.join(temporary, 'home');
        const system = path.join(temporary, 'system');
        const resources = path.join(temporary, 'resources');
        fs.mkdirSync(path.join(resources, 'icons'), { recursive: true });
        for (const size of ICON_SIZES) fs.writeFileSync(path.join(resources, 'icons', `${size}x${size}.png`), `icon ${size}`);
        const downloads = path.join(home, 'My Downloads');
        fs.mkdirSync(downloads, { recursive: true });
        const first = path.join(downloads, 'Fly-1.0.0-x86_64.AppImage');
        fs.writeFileSync(first, '');
        const env = { HOME: home, XDG_DATA_DIRS: system };
        const desktop = fakeDesktop();
        const make = (appImage, extra = {}) => createDesktopIntegration({ env, platform: 'linux', appImage, resourcesDir: resources, template, run: desktop.run, ...extra });

        // Not Linux, or not an AppImage: nothing to offer and nothing written.
        assert.deepEqual(await make(first, { platform: 'darwin' }).status(), { supported: false });
        const source = make(undefined);
        assert.equal(source.shouldOffer(), false);
        await assert.rejects(source.install(), /Only an AppImage/);
        await assert.rejects(source.makeDefault(), /no desktop entry/);
        assert.equal(source.refresh(), false);

        // A fresh AppImage is offered the entry and can add it.
        const integration = make(first);
        assert.equal(integration.shouldOffer(), true);
        let status = await integration.status();
        assert.equal(status.entry, null);
        assert.equal(status.canInstall, true);
        assert.equal(status.isDefault, false);
        status = await integration.install();
        const entryFile = path.join(home, '.local/share/applications', DESKTOP_ID);
        assert.deepEqual(status.entry, { path: entryFile, ours: true });
        assert.equal(integration.shouldOffer(), false);
        const link = integration.paths.link;
        assert.equal(fs.readlinkSync(link), first);
        const entry = fs.readFileSync(entryFile, 'utf8');
        assert.match(entry, new RegExp(`^Exec=${link.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')} %u$`, 'm'), 'Exec starts the link, so no space reaches xdg-utils');
        assert.match(entry, /^TryExec=.*fly-browser\.AppImage$/m, 'A deleted AppImage hides the entry instead of leaving it broken');
        assert.match(entry, /^Name=Fly$/m);
        assert.match(entry, /^StartupWMClass=fly-browser$/m);
        assert.equal((entry.match(/^Exec=/gm) || []).length, 1);
        for (const size of ICON_SIZES) {
            assert.equal(fs.readFileSync(path.join(home, `.local/share/icons/hicolor/${size}x${size}/apps/fly-browser.png`), 'utf8'), `icon ${size}`);
        }
        assert.ok(desktop.calls.some(call => call[0] === 'update-desktop-database'));

        // Nothing changes on a start from the same file.
        assert.equal(integration.refresh(), false);

        // A newer AppImage elsewhere takes the entry over at its first start.
        const second = path.join(temporary, 'Applications', 'Fly-1.1.0-x86_64.AppImage');
        fs.mkdirSync(path.dirname(second), { recursive: true });
        fs.writeFileSync(second, '');
        const moved = make(second);
        assert.equal(moved.refresh(), true);
        assert.equal(fs.readlinkSync(link), second);
        assert.equal(moved.refresh(), false);
        // An entry from an older Fly is rewritten to the shipped one.
        fs.writeFileSync(entryFile, entry.replace('Name=Fly', 'Name=Old Fly'));
        assert.equal(moved.refresh(), true);
        assert.equal(fs.readFileSync(entryFile, 'utf8'), entry);

        // Default browser: xdg-settings first, the MIME defaults when it refuses.
        status = await moved.makeDefault();
        assert.equal(status.isDefault, true);
        assert.deepEqual(desktop.calls.filter(call => call[0] === 'xdg-settings' && call[1] === 'set').pop(), ['xdg-settings', 'set', 'default-web-browser', DESKTOP_ID]);
        desktop.state.browser = 'firefox.desktop';
        desktop.state.settingsWorks = false;
        status = await moved.makeDefault();
        assert.equal(status.isDefault, true);
        assert.deepEqual(desktop.calls.pop().slice(0, 3), ['xdg-settings', 'get', 'default-web-browser']);
        assert.ok(desktop.calls.some(call => call.join(' ') === `xdg-mime default ${DESKTOP_ID} x-scheme-handler/http x-scheme-handler/https text/html application/xhtml+xml`));

        // Removal takes away exactly what Fly added.
        status = await moved.remove();
        assert.equal(status.entry, null);
        assert.equal(fs.existsSync(entryFile), false);
        assert.equal(fs.existsSync(link), false);
        assert.equal(fs.existsSync(path.join(home, '.local/share/icons/hicolor/48x48/apps/fly-browser.png')), false);
        assert.equal(fs.existsSync(second), true, 'The AppImage itself is never touched');

        // Making Fly the default adds the entry it needs first.
        desktop.state.settingsWorks = true;
        desktop.state.browser = 'firefox.desktop';
        status = await moved.makeDefault();
        assert.equal(status.isDefault, true);
        assert.equal(status.entry.ours, true);
        await moved.remove();

        // An entry Fly did not write is found, never offered over and never removed.
        fs.mkdirSync(path.join(system, 'applications'), { recursive: true });
        fs.writeFileSync(path.join(system, 'applications', DESKTOP_ID), template);
        const packaged = make(second);
        assert.equal(packaged.shouldOffer(), false);
        status = await packaged.status();
        assert.deepEqual(status.entry, { path: path.join(system, 'applications', DESKTOP_ID), ours: false });
        assert.equal(status.canMakeDefault, true);
        fs.mkdirSync(path.dirname(entryFile), { recursive: true });
        fs.writeFileSync(entryFile, template);
        await assert.rejects(packaged.remove(), /not added by Fly/);
        assert.equal(packaged.refresh(), false);
        assert.equal(fs.readFileSync(entryFile, 'utf8'), template);

        // Without xdg-utils the default cannot be read or changed.
        desktop.state.missing = true;
        status = await packaged.status();
        assert.equal(status.defaultBrowser, undefined);
        assert.equal(status.canMakeDefault, false);
    } finally { fs.rmSync(temporary, { recursive: true, force: true }); }
    console.log('Desktop integration finds packaged entries, follows a moved AppImage and removes only what it added.');
})().catch(error => { console.error(error); process.exit(1); });
