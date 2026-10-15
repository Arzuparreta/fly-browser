const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-sidebar-cursor-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Cursor fixture</title><input id="text">'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', '--ozone-platform=x11', entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        for (let index = 0; index < 3; index++) {
            await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `http://127.0.0.1:${server.address().port}/${index}`);
            await page.waitForTimeout(150);
        }
        await page.evaluate(async () => { const {hibernateInactiveTabs} = await import('./modules/webview.js'); await hibernateInactiveTabs(); });
        await page.waitForFunction(() => {
            try {
                const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
                return view?.getWebContentsId() > 0 && view.getURL().startsWith('http://127.0.0.1:') && !view.isLoading();
            } catch { return false; }
        });
        const snapshot = () => page.evaluate(() => ({views: document.querySelectorAll('webview').length, selected: [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected')), focused: [...document.querySelectorAll('#TabList li')].indexOf(document.activeElement)}));
        const initial = await snapshot();
        assert.equal(initial.views, 1);
        for (const pinned of [false, true]) {
            await page.evaluate(pinned => window.applySidebarMode(!pinned), pinned);
            for (const key of ['s', 'Alt+s']) {
                await page.mouse.move(500, 400);
                await page.evaluate(() => window.executeBrowserAction('focus-webview'));
                await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
                await page.keyboard.press(key);
                await page.waitForFunction(() => document.activeElement.id === 'Sidebar');
                assert.deepEqual(await snapshot(), {...initial, focused: -1}, 'Opening focuses the sidebar without selecting or waking a tab');
                assert.equal(await page.locator('#Sidebar :focus').count(), 0, 'No tab or toolbar control acquires focus on entry');
                assert.equal(await page.locator('#Sidebar').evaluate(sidebar => getComputedStyle(sidebar).outlineStyle), 'none');
                await page.keyboard.press('k');
                assert.deepEqual(await snapshot(), {...initial, focused: initial.selected - 1}, 'Page-key navigation starts from the selected tab without waking the cursor target');
                assert.notEqual(await page.locator('#TabList li:focus').evaluate(tab => getComputedStyle(tab).outlineStyle), 'none');
                await page.keyboard.press('ArrowDown');
                assert.deepEqual(await snapshot(), {...initial, focused: initial.selected});
                await page.keyboard.press(key);
                await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
                await page.waitForFunction(pinned => document.getElementById('Sidebar').getBoundingClientRect().right === (pinned ? 260 : 36), pinned);

                await page.keyboard.press(key);
                await page.waitForFunction(() => document.activeElement.id === 'Sidebar');
                await page.keyboard.press('Tab');
                assert.equal(await page.evaluate(() => document.activeElement.id), 'WorkspaceHeader');
                await page.keyboard.press('ArrowDown');
                assert.deepEqual(await snapshot(), {...initial, focused: initial.selected});
                // Leaving the sidebar must not force focus back onto the selected tab.
                await page.locator('webview:visible').evaluate(view => view.focus());
                await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
                await page.keyboard.press(key);
                await page.waitForFunction(() => document.activeElement.id === 'Sidebar');
                await page.keyboard.press('Enter');
                await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
            }
        }
        await page.evaluate(() => window.applySidebarMode(true));
        await page.keyboard.press('Control+p');
        await page.locator('#PaletteInput').fill('Toggle Sidebar Focus');
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.activeElement.id === 'Sidebar');
        assert.deepEqual(await snapshot(), {...initial, focused: -1}, 'Palette entry uses the same neutral sidebar focus');
        await page.keyboard.press('Escape');
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        await page.locator('#TabList li').first().focus();
        await page.keyboard.press('j');
        assert.deepEqual(await snapshot(), {...initial, focused: 1});
        await page.keyboard.press('ArrowDown');
        assert.deepEqual(await snapshot(), {...initial, focused: 2});
        await page.keyboard.press('k');
        assert.deepEqual(await snapshot(), {...initial, focused: 1});
        // Closing a sleeping cursor target must preserve the active page and its one view.
        await page.keyboard.press('x');
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 3);
        assert.deepEqual(await snapshot(), {views: 1, selected: initial.selected - 1, focused: 1});
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.querySelectorAll('webview').length === 2);
        assert.equal((await snapshot()).selected, 1);
        assert.equal((await snapshot()).focused, 1);
        // Ctrl+1 and bare digits no longer select tabs, and tabs carry no number badges.
        await page.keyboard.press('Control+1');
        await page.keyboard.press('1');
        await page.waitForTimeout(200);
        assert.equal((await snapshot()).selected, 1);
        assert.equal(await page.locator('.tab-index-badge').count(), 0);
        await page.keyboard.press('Alt+1');
        await page.waitForFunction(() => document.querySelector('#TabList li').classList.contains('selected'));
        await page.keyboard.press('Alt+9');
        await page.waitForFunction(() => document.querySelector('#TabList li:last-child').classList.contains('selected'));
        assert.deepEqual(errors, []);
        console.log('Sidebar cursor: neutral entry, explicit keyboard focus, natural exit, sleeping navigation, closing without waking, activation and Alt tab selection passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
