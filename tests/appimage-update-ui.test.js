const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// The update banner: an AppImage offers to update and restart, shows progress,
// and falls back to the release page when the update fails. Other installs keep
// the release link. The main process is stubbed; appimage-update.test.js tests
// the replacement itself.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-update-ui-'));
    const entry = createElectronFixture(root, temporary);
    let application;
    try {
        application = await _electron.launch({ executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry] });
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);

        // install-update answers as scripted, reporting progress first.
        await application.evaluate(({ ipcMain, BrowserWindow }) => {
            global.updateAnswers = [];
            global.updateCalls = 0;
            ipcMain.removeHandler('install-update');
            ipcMain.handle('install-update', async () => {
                global.updateCalls++;
                const window = BrowserWindow.getAllWindows()[0];
                window.webContents.send('update-progress', 37);
                await new Promise(resolve => global.releaseUpdate = resolve);
                return global.updateAnswers.shift();
            });
        });
        const announce = canInstall => application.evaluate(({ BrowserWindow }, canInstall) => {
            BrowserWindow.getAllWindows()[0].webContents.send('update-available', { updateAvailable: true, currentVersion: '1.0.0', latestVersion: 'v1.1.0', releaseUrl: 'https://example.invalid/release', canInstall });
        }, canInstall);
        const finish = answer => application.evaluate((electron, answer) => { global.updateAnswers.push(answer); global.releaseUpdate(); }, answer);
        const text = () => page.locator('#UpdateNotificationText').textContent();
        const button = page.locator('#UpdateActionBtn');

        await page.keyboard.press('Control+h');
        await page.locator('#SettingsTab-about').click();

        // Not an AppImage: the release link, as before.
        await announce(false);
        await page.waitForFunction(() => document.getElementById('UpdateNotificationBanner').style.display === 'flex');
        assert.equal(await button.textContent(), 'Download release');

        // An AppImage: update in place, with progress, by keyboard.
        await announce(true);
        await page.waitForFunction(() => document.getElementById('UpdateActionBtn').textContent === 'Update and restart');
        await button.focus();
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.getElementById('UpdateNotificationText').textContent === 'Downloading Fly v1.1.0… 37%');
        assert.equal(await button.isDisabled(), true, 'A second press cannot start another download');
        await finish({ ok: false, error: 'The latest release has no AppImage update' });
        await page.waitForFunction(() => document.getElementById('UpdateActionBtn').textContent === 'Download release' && !document.getElementById('UpdateActionBtn').disabled);
        assert.equal(await text(), 'The update could not be installed (The latest release has no AppImage update). Download it from the release page instead.');

        // A fresh notice offers the update again; success says Fly is restarting.
        await announce(true);
        await page.waitForFunction(() => document.getElementById('UpdateActionBtn').textContent === 'Update and restart');
        await button.click();
        await page.waitForFunction(() => document.getElementById('UpdateNotificationText').textContent.endsWith('37%'));
        await finish({ ok: true });
        await page.waitForFunction(() => document.getElementById('UpdateNotificationText').textContent === 'Restarting into Fly v1.1.0…');
        assert.equal(await application.evaluate(() => global.updateCalls), 2);
        assert.deepEqual(errors, []);
    } finally {
        await application?.close().catch(() => {});
        fs.rmSync(temporary, { recursive: true, force: true });
    }
    console.log('The update banner updates an AppImage in place with progress and falls back to the release page.');
})().catch(error => { console.error(error); process.exit(1); });
