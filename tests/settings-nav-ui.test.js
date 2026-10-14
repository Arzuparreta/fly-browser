const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-settings-nav-'));
    const entry = createElectronFixture(root, temporary);

    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        const active = () => page.evaluate(() => document.activeElement.id);
        const visiblePanes = () => page.evaluate(() => [...document.querySelectorAll('.settings-pane')].filter(pane => pane.getClientRects().length).map(pane => pane.id));
        const extensionsPath = () => page.locator('#ExtensionsDirectoryPath').textContent();

        assert.equal(await page.locator('.keyboard-row').count(), 0, 'Hidden keyboard rows must not be built at startup');

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

        // Search filters every section in place; the matching rows stay live controls.
        const visibleRows = () => page.evaluate(() => [...document.querySelectorAll('#SettingsContent :is(.setting-item, .alias-card)')].filter(row => row.getClientRects().length).map(row => row.querySelector('.setting-label, .alias-prefix')?.textContent.trim()));
        await page.locator('#SettingsTab-general').focus();
        await page.keyboard.press('/');
        assert.equal(await active(), 'SettingsSearch');
        await page.keyboard.type('gpu');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-performance']);
        assert.deepEqual(await visibleRows(), ['Hardware GPU acceleration']);
        assert(await page.locator('#setting-save-config-btn').isVisible(), 'Restart-only results keep their save action');
        assert.equal(await page.locator('#SettingsTab-performance').getAttribute('data-count'), '1');
        assert.equal(await page.locator('#SettingsTab-general').evaluate(tab => tab.classList.contains('no-match')), true);
        assert.equal(await page.evaluate(() => CSS.highlights.get('settings-search')?.size), 1);
        // Option names and synonyms match, across sections.
        await page.locator('#SettingsSearch').fill('kagi');
        assert.deepEqual(await visibleRows(), ['Default search engine', 'k']);
        await page.locator('#SettingsSearch').fill('incognito');
        assert.deepEqual(await visibleRows(), ['Global private browsing']);
        await page.locator('#SettingsSearch').fill('sidebar colour');
        assert.deepEqual(await visibleRows(), ['Sidebar background']);
        // Enter moves to the first result, which can be changed in place.
        await page.locator('#SettingsSearch').fill('spellchecker');
        await page.locator('#SettingsSearch').press('Enter');
        assert.equal(await active(), 'setting-spellchecker-language');
        // Keyboard matches are summarised and open the editor already filtered.
        await page.locator('#SettingsSearch').fill('dashboard');
        assert.match(await page.locator('#SettingsKeyboardMatches').textContent(), /^\d+ matching action/);
        assert(!(await page.locator('#KeyboardRows').isVisible()), 'The full binding list stays out of search results');
        await page.locator('#SettingsKeyboardShow').click();
        assert.deepEqual(await visiblePanes(), ['SettingsPane-keyboard']);
        assert.equal(await active(), 'KeyboardSearch');
        assert.equal(await page.locator('#KeyboardSearch').inputValue(), 'dashboard');
        assert.equal(await page.locator('#SettingsSearch').inputValue(), '');
        assert(await page.locator('.keyboard-row').count() >= 1);
        await page.locator('#KeyboardSearch').fill('');
        // No results, then Escape clears the search before it closes Preferences.
        await page.keyboard.press('Control+f');
        assert.equal(await active(), 'SettingsSearch');
        assert(!(await page.locator('#FindBarOverlay').isVisible()), 'Find inside Preferences must not open the page find bar');
        await page.keyboard.type('zzzz');
        assert.deepEqual(await visiblePanes(), []);
        assert.match(await page.locator('#SettingsSearchStatus').textContent(), /No settings match/);
        await page.keyboard.press('Escape');
        assert(await page.locator('#PreferencesOverlay').isVisible());
        assert.equal(await page.locator('#SettingsSearch').inputValue(), '');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-keyboard']);
        assert.equal(await page.evaluate(() => CSS.highlights.has('settings-search')), false);
        // Choosing a section while searching leaves search mode with every row restored.
        await page.locator('#SettingsSearch').fill('clock');
        await page.locator('#SettingsTab-appearance').click();
        assert.deepEqual(await visiblePanes(), ['SettingsPane-appearance']);
        assert.equal(await page.locator('#SettingsPane-appearance .setting-item:visible').count(), 12);
        // Closing clears any search, so reopening starts from the remembered section.
        await page.locator('#SettingsSearch').fill('cookies');
        await page.locator('#CloseSettingsBtn').click();
        await page.evaluate(() => window.executeBrowserAction('toggle-help'));
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.equal(await page.locator('#SettingsSearch').inputValue(), '');
        assert.deepEqual(await visiblePanes(), ['SettingsPane-appearance']);

        // Direct entry points open their own pane.
        await page.keyboard.press('Escape');
        await page.locator('#PreferencesOverlay').waitFor({state: 'hidden'});
        await page.locator('#menu-btn').focus();
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
        assert(await page.locator('#SettingsSearch').isVisible(), 'Search stays available in compact windows');
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
        // Each search keystroke filters and lays out synchronously.
        await page.evaluate(() => window.executeBrowserAction('toggle-help'));
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        const searchTimings = await page.evaluate(() => {
            const input = document.getElementById('SettingsSearch');
            const samples = [];
            for (const value of ['t', 'ta', 'tab', 'tab ', 'tab h', 'c', 'co', 'col', 'colo', 'colour', '', 'm', 'me', 'mem']) {
                const start = performance.now();
                input.value = value;
                input.dispatchEvent(new Event('input'));
                document.getElementById('SettingsContent').getBoundingClientRect();
                samples.push(performance.now() - start);
            }
            return samples.sort((a, b) => a - b);
        });
        console.log('Search keystroke filter and layout (ms, sorted):', searchTimings.map(value => value.toFixed(1)).join(' '));
        assert(searchTimings[7] < 16, 'Search must fit within a frame');
        // Upstream options live in the fork's grouped panes and remain searchable and persistent.
        await page.locator('#SettingsSearch').fill('custom search aliases');
        assert(await page.locator('#new-alias-key').isVisible());
        await page.locator('#new-alias-key').fill('fixture');
        await page.locator('#new-alias-url').fill('https://example.invalid/?q=%s');
        await page.locator('#add-search-alias-btn').click();
        for (let i = 0; i < 100; i++) {
            if ((await page.evaluate(() => window.flyAPI.getBrowserSettings())).custom_search_aliases.fixture) break;
            await page.waitForTimeout(50);
        }
        assert.equal((await page.evaluate(() => window.flyAPI.getBrowserSettings())).custom_search_aliases.fixture, 'https://example.invalid/?q=%s');
        assert(await page.locator('.search-alias-badge').filter({hasText: 'fixture'}).isVisible());
        assert.equal(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), true);
        await page.locator('#SettingsSearch').focus();
        await page.mouse.move(690, 580);
        await page.waitForTimeout(400);
        await page.screenshot({path: '/tmp/fly-upstream-settings.png'});
        assert.deepEqual(errors, []);
        console.log('Preferences index, search, keyboard navigation, single-pane layout, lazy extension scan and entry points passed.');
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
