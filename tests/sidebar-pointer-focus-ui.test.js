const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-sidebar-pointer-'));
    const entry = createElectronFixture(root, temporary);

    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Overlay fixture</title><input id="editable" style="margin-left:260px"><button>Hint target</button>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {return [...document.querySelectorAll('webview')].find(view => view.getClientRects().length)?.getWebContentsId() > 0;} catch {return false;}});
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `http://127.0.0.1:${server.address().port}`);

        const expand = async () => {
            await page.mouse.move(500, await page.evaluate(() => innerHeight - 10));
            await page.mouse.move(10, 100);
            await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 260);
        };
        const contract = async () => {
            await page.mouse.move(500, await page.evaluate(() => innerHeight - 10));
            await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 36);
            assert.equal(await page.evaluate(() => document.getElementById('Sidebar').contains(document.activeElement)), false);
        };
        const click = async selector => {
            await expand();
            if (selector.endsWith('.split-tab-badge')) {
                // Decorative split badges pass pointer events through to the tab.
                const box = await page.locator(selector).boundingBox();
                await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
            } else await page.locator(selector).click();
            assert.equal(await page.evaluate(() => document.getElementById('Sidebar').contains(document.activeElement)), false, selector);
        };
        await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `http://127.0.0.1:${server.address().port}/second`);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length >= 2);
        // Exercise real actions, including clicks on nested clock text and icon targets.
        for (const selector of ['#SidebarClockDate', '#SidebarClockTime', '#SidebarClock i', '#split-toggle-btn', '#TabList li.selected .split-tab-badge', '#split-toggle-btn', '#reader-view-btn', '#theme-toggle-btn', '#noti-toggle-btn', '#TabList li.selected .tab-title']) {
            await click(selector);
            await contract();
            console.log('Pointer action contracts:', selector);
        }
        for (const [selector, surface] of [['#WorkspaceBadge', 'DashboardOverlay'], ['#menu-btn', 'DashboardOverlay'], ['#toggle-nav-btn', 'WideAddressBar'], ['#settings-btn', 'PreferencesOverlay']]) {
            console.log('Opening surface:', selector);
            await click(selector);
            await page.waitForFunction(id => document.getElementById(id).contains(document.activeElement), surface, {timeout: 5000}).catch(async error => {console.log(await page.evaluate(() => ({focus: document.activeElement.outerHTML.slice(0,200), surfaces: [...document.querySelectorAll('[id$=Overlay], #WideAddressBar')].filter(el => el.getClientRects().length).map(el => el.id)}))); throw error;});
            await contract();
            await page.keyboard.press('Escape');
        }
        await page.evaluate(async () => {const {displayUpdateNotification} = await import('./renderer.js'); displayUpdateNotification({updateAvailable: true, currentVersion: '0.14.0', latestVersion: '99.0.0', releaseUrl: 'https://example.invalid/release'});});
        await click('#update-indicator-btn');
        await page.waitForFunction(() => document.getElementById('PreferencesOverlay').contains(document.activeElement));
        await contract(); await page.keyboard.press('Escape');
        await click('#pin-sidebar-btn');
        await page.mouse.move(500, 400);
        assert(await page.evaluate(() => document.body.classList.contains('sidebar-pinned')));
        await click('#pin-sidebar-btn');
        await contract();
        // Keyboard activation still keeps the clock focused and the sidebar expanded.
        for (const key of ['Enter', 'Space']) {
            await expand();
            await page.locator('#SidebarClock').focus();
            await page.keyboard.press(key);
            await page.mouse.move(500, 400);
            assert.equal(await page.evaluate(() => document.activeElement.id), 'SidebarClock');
            assert.equal(await page.locator('#Sidebar').evaluate(el => el.offsetWidth), 260);
        }
        // A pointer click releases focus even when keyboard navigation preceded it.
        await click('#SidebarClock');
        await contract();
        await page.evaluate(async () => {
            const {hibernateTab} = await import('./modules/webview.js');
            const {state} = await import('./modules/state.js');
            const items = [...document.querySelectorAll('#TabList li')];
            const inactive = items.findIndex(li => !li.classList.contains('selected'));
            await hibernateTab(state.sessionState.current_workspace, inactive);
        });
        await click('#TabList li.tab-sleeping .tab-sleep-badge');
        await contract();
        // Newly rendered pin/media badges stop click propagation but share the policy.
        await page.evaluate(() => window.executeBrowserAction('toggle-pin-tab'));
        await click('#TabList li.selected .tab-pin-badge');
        await contract();
        await page.evaluate(() => window.executeBrowserAction('toggle-tab-audio-mute'));
        await click('#TabList li.selected .tab-media-badge');
        await contract();
        // Audit every static control, including window/update controls, at pointer-down.
        // Cancel activation here so the close/minimise buttons cannot end this audit.
        await page.evaluate(() => {
            document.getElementById('WindowControls').style.display = 'flex';
            document.getElementById('update-indicator-btn').style.display = 'inline-flex';
            document.getElementById('Sidebar').addEventListener('click', event => {event.preventDefault(); event.stopImmediatePropagation();}, true);
        });
        const controls = await page.locator('#Sidebar button, #Sidebar [role="button"], #TabList li').evaluateAll(els => els.map(el => el.id ? '#' + el.id : '#TabList li.selected'));
        for (const selector of [...new Set(controls)]) {
            await expand();
            await page.locator('#SidebarClock').focus();
            await click(selector);
            await contract();
        }
        // Future editable sidebar controls retain native pointer focus and text selection.
        await page.evaluate(() => {
            const input = document.createElement('input'); input.id = 'sidebar-editable';
            document.getElementById('Sidebar').prepend(input);
        });
        await expand(); await page.locator('#sidebar-editable').click();
        await page.keyboard.type('editable');
        assert.equal(await page.locator('#sidebar-editable').inputValue(), 'editable');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'sidebar-editable');
        await page.evaluate(() => {document.getElementById('sidebar-editable').remove(); while(document.querySelector('#TabList li')) window.executeBrowserAction('remove-tab');});
        await page.locator('webview:visible').waitFor({state:'hidden'});
        await click('#SidebarClock'); await contract();
        assert.deepEqual(errors, []);
        console.log('Sidebar pointer focus: actions, all static controls, nested/dynamic badges, keyboard, pinning, editable controls and empty workspace passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive:true,force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
