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
        const snapshot = () => page.evaluate(() => ({views: document.querySelectorAll('webview').length, selected: [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected')), focused: [...document.querySelectorAll('#TabList li')].indexOf(document.activeElement)}));
        const initial = await snapshot();
        assert.equal(initial.views, 1);
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
        await page.keyboard.press('Control+1');
        await page.waitForFunction(() => document.querySelector('#TabList li').classList.contains('selected'));
        assert.equal((await snapshot()).focused, 0);
        await page.keyboard.press('Alt+9');
        await page.waitForFunction(() => document.querySelector('#TabList li:last-child').classList.contains('selected'));
        await page.keyboard.down('Control');
        assert(await page.evaluate(() => document.body.classList.contains('show-tab-badges')));
        await page.keyboard.up('Control');
        assert(!(await page.evaluate(() => document.body.classList.contains('show-tab-badges'))));
        assert.deepEqual(errors, []);
        console.log('Sidebar cursor: sleeping navigation, closing without waking, activation, merged shortcuts and badge cleanup passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
