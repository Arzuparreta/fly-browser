const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-settings-tabs-'));
    const entry = createElectronFixture(root, temporary);
    const configDir = path.join(temporary, '.config/fly-browser');
    const folder = path.join(configDir, 'extensions/embedded');
    fs.mkdirSync(folder, { recursive: true });
    fs.writeFileSync(path.join(folder, 'manifest.json'), JSON.stringify({
        manifest_version: 2, name: 'Embedded options fixture', version: require('../package.json').version,
        options_ui: { page: 'options.html' }, browser_action: {}
    }));
    fs.writeFileSync(path.join(folder, 'options.html'), '<!doctype html><html><body style="width:400px;height:200px"><label>Fixture option <input id="option"></label></body></html>');
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({
        sidebar_auto_collapse: false, extensions_enabled: true, extensions_by_workspace: { Work: ['ublock-origin', 'embedded'], Other: [] }
    }));
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({ current_workspace: 'Work', workspaces: { Work: ['about:blank'], Other: ['about:blank'] } }));
    let application;
    const errors = [];
    const launch = async () => {
        application = await _electron.launch({ executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry] });
        const page = await application.firstWindow();
        page.on('pageerror', error => errors.push(error.message));
        page.on('console', message => { if (message.type() === 'error') console.error(message.text()); });
        page.setDefaultTimeout(20000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    try {
        let page = await launch();
        await page.evaluate(() => window.openPreferencesPane('extensions'));
        const settingsTab = () => page.locator('#TabList li').filter({ hasText: 'Settings · Fly' });
        await page.locator('.ext-options-btn[data-folder="ublock-origin"]').waitFor();
        await page.waitForFunction(async () => (await window.flyAPI.extensionsGetState({ scan: false })).runtime.workspaces
            .some(workspace => workspace.loaded.some(extension => extension.name === 'uBlock Origin')));
        assert.equal(await settingsTab().count(), 1);
        assert.equal(await page.locator('#webview-container webview').count(), 1, 'Settings never create a guest webview');
        await page.locator('.ext-options-btn[data-folder="ublock-origin"]').click();
        await page.locator('#PreferencesOverlay').waitFor({ state: 'hidden' });
        await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].some(view => view.getAttribute('src')?.includes('/dashboard.html')));
        assert.equal(await settingsTab().count(), 1, 'Opening extension options keeps the settings tab');
        await page.evaluate(() => window.executeBrowserAction('remove-tab'));
        await page.locator('#PreferencesOverlay').waitFor({ state: 'visible' });
        assert(await page.locator('#SettingsPane-extensions').isVisible(), 'Closing uBlock returns to the extensions section');

        // A second opening reuses the options tab, and the settings entry point reuses its own tab.
        await page.locator('.ext-options-btn[data-folder="ublock-origin"]').click();
        await page.locator('#PreferencesOverlay').waitFor({ state: 'hidden' });
        await page.evaluate(() => window.openPreferencesPane('extensions'));
        await page.locator('.ext-options-btn[data-folder="ublock-origin"]').click();
        await page.locator('#PreferencesOverlay').waitFor({ state: 'hidden' });
        assert.equal(await page.locator('#TabList li').count(), 3);
        await page.evaluate(() => window.executeBrowserAction('remove-tab'));
        await page.locator('#PreferencesOverlay').waitFor({ state: 'visible' });

        // Embedded options are a dialog above the same settings tab.
        await page.locator('.ext-options-btn[data-folder="embedded"]').click();
        await page.locator('.extension-options-overlay [role="dialog"]').waitFor();
        await page.waitForFunction(() => document.activeElement === document.querySelector('#ExtensionPopupOverlay webview'));
        assert(await page.locator('#PreferencesOverlay').isVisible());
        assert.equal(await page.locator('#TabList li').count(), 2);
        const guestBounds = await page.locator('#ExtensionPopupOverlay webview').boundingBox();
        assert(guestBounds.width >= 200 && guestBounds.height >= 60);
        await page.evaluate(() => document.querySelector('#ExtensionPopupOverlay webview').sendInputEvent({ type: 'keyDown', keyCode: 'Escape' }));
        await page.locator('#ExtensionPopupOverlay').waitFor({ state: 'detached' });
        assert(await page.locator('#SettingsPane-extensions').isVisible());
        assert(await page.evaluate(() => document.getElementById('PreferencesOverlay').contains(document.activeElement)));
        await page.locator('.ext-options-btn[data-folder="embedded"]').click();
        await page.locator('.extension-options-close').focus();
        await page.keyboard.press('Escape');
        await page.locator('#ExtensionPopupOverlay').waitFor({ state: 'detached' });
        assert(await page.locator('#SettingsPane-extensions').isVisible(), 'Host Escape closes only the child dialog');
        await page.locator('.ext-options-btn[data-folder="embedded"]').click();
        await page.locator('.extension-options-close').click();
        await page.locator('#ExtensionPopupOverlay').waitFor({ state: 'detached' });

        // Switching away and back retains the selected pane, including after restarting.
        await page.locator('#SettingsTab-privacy').click();
        await page.locator('#TabList li').first().click();
        await page.locator('#PreferencesOverlay').waitFor({ state: 'hidden' });
        await settingsTab().click();
        assert(await page.locator('#SettingsPane-privacy').isVisible());
        await page.evaluate(async () => (await import('./modules/navigation.js')).handleNavigation('ws Other'));
        await page.locator('#PreferencesOverlay').waitFor({ state: 'hidden' });
        await page.evaluate(() => window.openPreferencesPane('appearance'));
        assert.equal(await settingsTab().count(), 1);
        await page.evaluate(async () => (await import('./modules/navigation.js')).handleNavigation('ws Work'));
        await settingsTab().click();
        assert(await page.locator('#SettingsPane-privacy').isVisible(), 'Each workspace retains its own settings section');
        await application.close(); application = null;
        page = await launch();
        await settingsTab().click();
        await page.locator('#SettingsPane-privacy').waitFor({ state: 'visible' });
        assert.equal(await settingsTab().count(), 1);
        await page.locator('#CloseSettingsBtn').click();
        assert.equal(await settingsTab().count(), 0);
        assert.deepEqual(errors, []);
        console.log('Settings tabs preserve section and survive restart; uBlock options return to their parent; embedded options close in place.');
    } catch (error) {
        console.log('UI state', JSON.stringify(await (await application.firstWindow()).evaluate(async () => ({
            tabs: [...document.querySelectorAll('#TabList li')].map(tab => tab.title),
            views: [...document.querySelectorAll('#webview-container webview')].map(view => ({src: view.getAttribute('src'), partition: view.getAttribute('partition')})),
            runtime: (await window.flyAPI.extensionsGetState({ scan: false })).runtime
        }))));
        throw error;
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
