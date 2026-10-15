const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// Fly running as an AppImage: offered the menu entry once, and able to add,
// remove and make itself the default from Preferences. Every XDG directory is
// redirected into a temporary home, so the real desktop is never touched.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-desktop-ui-'));
    const entry = createElectronFixture(root, temporary);
    const fakeAppImage = name => {
        const file = path.join(temporary, 'My Apps', name);
        fs.mkdirSync(path.dirname(file), { recursive: true });
        fs.writeFileSync(file, '#!/bin/sh\n', { mode: 0o755 });
        return file;
    };
    const dataHome = path.join(temporary, 'share');
    const configHome = path.join(temporary, 'xdg-config');
    const entryFile = path.join(dataHome, 'applications', 'fly-browser.desktop');
    const link = path.join(dataHome, 'fly-browser', 'fly-browser.AppImage');
    const launch = appImage => _electron.launch({
        executablePath: path.join(root, 'node_modules/electron/dist/electron'),
        args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry],
        env: { ...process.env, HOME: temporary, XDG_DATA_HOME: dataHome, XDG_CONFIG_HOME: configHome, XDG_DATA_DIRS: path.join(temporary, 'system'), XDG_CURRENT_DESKTOP: '', APPIMAGE: appImage }
    });
    // Answers the first-start question as the user would, and records that it was asked.
    const answer = (application, response) => application.evaluate(({ dialog }, response) => {
        global.flyOffers = [];
        dialog.showMessageBox = async (window, options) => {
            global.flyOffers.push(options.message);
            return { response, checkboxChecked: false };
        };
    }, response);
    const offers = application => application.evaluate(() => global.flyOffers);

    // Another browser is the default to begin with.
    fs.mkdirSync(path.join(temporary, 'system/applications'), { recursive: true });
    fs.writeFileSync(path.join(temporary, 'system/applications/other-browser.desktop'), '[Desktop Entry]\nType=Application\nName=Other\nExec=/bin/sh %u\nMimeType=x-scheme-handler/http;x-scheme-handler/https;text/html;\n');
    fs.mkdirSync(configHome, { recursive: true });
    fs.writeFileSync(path.join(configHome, 'mimeapps.list'), '[Default Applications]\nx-scheme-handler/http=other-browser.desktop\nx-scheme-handler/https=other-browser.desktop\ntext/html=other-browser.desktop\n');

    let application;
    try {
        const first = fakeAppImage('Fly-1.0.0-x86_64.AppImage');
        application = await launch(first);
        await answer(application, 0);
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);

        // The first start asks once; accepting adds the entry for this file.
        for (let i = 0; i < 100 && !fs.existsSync(entryFile); i++) await page.waitForTimeout(100);
        assert.deepEqual(await offers(application), ['Add Fly to your applications menu?']);
        assert.equal(fs.readlinkSync(link), first);
        assert.match(fs.readFileSync(entryFile, 'utf8'), /^X-Fly-AppImage=true$/m);
        assert.ok(fs.existsSync(path.join(dataHome, 'icons/hicolor/256x256/apps/fly-browser.png')));
        assert.equal(JSON.parse(fs.readFileSync(path.join(temporary, '.config/fly-browser/config.json'), 'utf8')).desktop_integration_offered, true);

        // Preferences → About shows it, and every action is reachable by keyboard.
        await page.locator('#split-toggle-btn').focus();
        await page.keyboard.press('Control+h');
        await page.locator('#PreferencesOverlay').waitFor({ state: 'visible' });
        await page.locator('#SettingsTab-about').click();
        const entryButton = page.locator('#DesktopEntryBtn');
        const defaultButton = page.locator('#DefaultBrowserBtn');
        await page.waitForFunction(() => document.getElementById('DesktopEntryBtn').textContent === 'Remove from menu' && !document.getElementById('DesktopEntryBtn').disabled);
        assert.match(await page.locator('#DesktopEntryStatus').textContent(), /Listed in Rofi/);
        assert.equal(await page.locator('#DefaultBrowserStatus').textContent(), 'Links from other applications open in other-browser.');
        assert.equal(await defaultButton.isVisible(), true);

        await defaultButton.focus();
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.getElementById('DefaultBrowserStatus').textContent === 'Fly opens links from other applications.');
        assert.equal(await defaultButton.isVisible(), false);
        assert.match(fs.readFileSync(path.join(configHome, 'mimeapps.list'), 'utf8'), /x-scheme-handler\/https=fly-browser\.desktop/);

        await entryButton.click();
        await page.waitForFunction(() => document.getElementById('DesktopEntryBtn').textContent === 'Add to menu' && !document.getElementById('DesktopEntryBtn').disabled);
        assert.equal(fs.existsSync(entryFile), false);
        assert.equal(fs.existsSync(link), false);
        assert.equal(fs.existsSync(first), true, 'The AppImage itself is never touched');
        await entryButton.click();
        await page.waitForFunction(() => document.getElementById('DesktopEntryBtn').textContent === 'Remove from menu');
        assert.equal(fs.readlinkSync(link), first);

        // Searching Preferences finds the rows and clearing it keeps them.
        await page.locator('#SettingsSearch').fill('rofi');
        assert.equal(await page.locator('#DesktopEntryStatus').isVisible(), true);
        await page.locator('#SettingsSearch').fill('');
        assert.deepEqual(errors, []);
        await application.close();

        // A newer AppImage somewhere else takes the entry over and is not asked again.
        const second = fakeAppImage('Fly-1.1.0-x86_64.AppImage');
        application = await launch(second);
        await answer(application, 1);
        const again = await application.firstWindow();
        await again.waitForFunction(() => !!window.executeBrowserAction);
        await again.waitForTimeout(3500);
        assert.equal(fs.readlinkSync(link), second);
        assert.deepEqual(await offers(application), []);
    } finally {
        await application?.close().catch(() => {});
        fs.rmSync(temporary, { recursive: true, force: true });
    }
    console.log('An AppImage is offered the menu entry once, manages it from Preferences and keeps it when moved.');
})().catch(error => { console.error(error); process.exit(1); });
