const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-settings-nav-'));
    const entry = path.join(temporary, 'start.cjs');
    fs.writeFileSync(entry, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))}); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction && document.querySelectorAll('.keyboard-row').length > 0);
        const active = () => page.evaluate(() => document.activeElement.id);
        const visiblePanes = () => page.evaluate(() => [...document.querySelectorAll('.settings-pane')].filter(pane => pane.getClientRects().length).map(pane => pane.id));
        const extensionsPath = () => page.locator('#ExtensionsDirectoryPath').textContent();

        // Opening focuses the selected index entry; exactly one pane is laid out.
        await page.locator('#menu-btn').focus();
        await page.keyboard.press('Control+h');
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.equal(await active(), 'SettingsTab-general');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-general']);
        assert.equal(await page.locator('#SettingsTab-general').getAttribute('aria-selected'), 'true');
        const placeholder = await extensionsPath();

        // Arrow keys move and select, wrap at the ends, and Home/End jump.
        const order = ['general', 'appearance', 'keyboard', 'privacy', 'extensions', 'performance', 'diagnostics', 'about'];
        for (const pane of order.slice(1, 4)) {
            await page.keyboard.press('ArrowDown');
            assert.equal(await active(), `SettingsTab-${pane}`);
            assert.deepEqual(await visiblePanes(), [`SettingsPane-${pane}`]);
        }
        assert.equal(await extensionsPath(), placeholder, 'The extensions scan must wait for its own pane');
        await page.keyboard.press('ArrowDown');
        await page.waitForFunction(text => document.getElementById('ExtensionsDirectoryPath').textContent !== text, placeholder);
        await page.keyboard.press('End');
        assert.equal(await active(), 'SettingsTab-about');
        await page.keyboard.press('ArrowDown');
        assert.equal(await active(), 'SettingsTab-general');
        await page.keyboard.press('ArrowUp');
        assert.equal(await active(), 'SettingsTab-about');
        await page.keyboard.press('Home');
        assert.equal(await active(), 'SettingsTab-general');

        // Only the selected entry is in the Tab sequence, so Tab enters the pane directly.
        await page.keyboard.press('Tab');
        assert.equal(await active(), 'SearchEngineSelect');
        await page.locator('#CloseSettingsBtn').focus();
        await page.keyboard.press('Shift+Tab');
        assert(await page.evaluate(() => document.getElementById('PreferencesOverlay').contains(document.activeElement)), 'Shift+Tab must stay inside Preferences');

        // Switching panes resets scrolling, and the last pane is remembered after closing.
        await page.locator('#SettingsTab-keyboard').click();
        await page.locator('#SettingsContent').evaluate(content => { content.scrollTop = 600; });
        await page.locator('#SettingsTab-privacy').click();
        assert.equal(await page.locator('#SettingsContent').evaluate(content => content.scrollTop), 0);
        await page.keyboard.press('Escape');
        await page.locator('#PreferencesOverlay').waitFor({state: 'hidden'});
        await page.evaluate(() => window.executeBrowserAction('toggle-help'));
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.equal(await active(), 'SettingsTab-privacy');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-privacy']);

        // The keyboard editor keeps its search and save bars pinned while its list scrolls.
        await page.locator('#SettingsTab-keyboard').click();
        await page.locator('#SettingsContent').evaluate(content => { content.scrollTop = content.scrollHeight / 2; });
        const content = await page.locator('#SettingsContent').boundingBox();
        const toolbar = await page.locator('.keyboard-toolbar').boundingBox();
        const saveBar = await page.locator('.keyboard-save-bar').boundingBox();
        assert(Math.abs(toolbar.y - content.y) < 1, 'Keyboard search bar must stay at the top');
        assert(saveBar.y + saveBar.height <= content.y + content.height + 1, 'Keyboard save bar must stay visible');

        // Direct entry points open their own pane.
        await page.keyboard.press('Escape');
        await page.locator('#PreferencesOverlay').waitFor({state: 'hidden'});
        await page.keyboard.press('Control+,');
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.equal(await active(), 'KeyboardSearch');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-keyboard']);
        await page.evaluate(() => window.executeBrowserAction('toggle-palette'));
        await page.locator('#PaletteInput').fill('extension settings');
        await page.locator('#PaletteInput').press('Enter');
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.deepEqual(await visiblePanes(), ['SettingsPane-extensions']);
        await page.keyboard.press('Escape');
        await page.locator('#PreferencesOverlay').waitFor({state: 'hidden'});
        await page.evaluate(async () => {const {displayUpdateNotification} = await import('./renderer.js'); displayUpdateNotification({updateAvailable: true, currentVersion: '0.14.0', latestVersion: '99.0.0', releaseUrl: 'https://example.invalid/release'});});
        await page.locator('#menu-btn').focus();
        await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 260);
        await page.locator('#update-indicator-btn').click();
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.deepEqual(await visiblePanes(), ['SettingsPane-about']);
        assert(await page.locator('#UpdateNotificationBanner').isVisible(), 'The About pane shows the available release');

        // Compact windows keep an icon index with accessible names.
        await page.setViewportSize({width: 700, height: 600});
        assert.equal(await page.locator('#SettingsTab-appearance').getAttribute('title'), 'Appearance');
        assert(!(await page.locator('#SettingsTab-appearance span').isVisible()));
        assert(await page.locator('#SettingsTab-appearance').isVisible());
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);

        // Opening is a display change: measure the synchronous cost of the toggle.
        await page.keyboard.press('Escape');
        const timings = await page.evaluate(async () => {
            const samples = [];
            for (let i = 0; i < 10; i++) {
                const start = performance.now();
                window.togglePreferencesView();
                document.getElementById('PreferencesOverlay').getBoundingClientRect();
                samples.push(performance.now() - start);
                await new Promise(resolve => requestAnimationFrame(resolve));
                await window.togglePreferencesView();
                await new Promise(resolve => requestAnimationFrame(resolve));
            }
            return samples.sort((a, b) => a - b);
        });
        console.log('Preferences open and layout (ms, sorted):', timings.map(value => value.toFixed(1)).join(' '));
        assert(timings[4] < 50, 'Opening Preferences must stay cheap');
        assert.deepEqual(errors, []);
        console.log('Preferences index, keyboard navigation, single-pane layout, lazy extension scan and entry points passed.');
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
