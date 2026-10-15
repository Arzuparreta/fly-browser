const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-numbers-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Numbers fixture</title><input id="text">'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        for (let index = 0; index < 10; index++) {
            await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `http://127.0.0.1:${server.address().port}/${index}`);
            await page.waitForTimeout(100);
        }
        const count = await page.locator('#TabList li').count();
        assert(count > 9);
        const ready = () => page.waitForFunction(() => {
            try {
                const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
                return view?.getWebContentsId() > 0 && view.getURL() !== '' && !view.isLoading();
            } catch { return false; }
        });
        await ready();
        await page.evaluate(() => window.executeBrowserAction('focus-webview'));
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');

        // Real key events through Electron's input pipeline, so before-input-event sees them as it does
        // the keyboard's. Without a guest id they go to Fly's own window.
        const send = (input, guest = true) => page.evaluate(guest => guest ? [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).getWebContentsId() : 0, guest)
            .then(id => application.evaluate(({webContents, BrowserWindow}, [id, input]) => (id ? webContents.fromId(id) : BrowserWindow.getAllWindows()[0].webContents).sendInputEvent(input), [id, input]));
        const altDown = {type: 'keyDown', keyCode: 'Alt', modifiers: ['alt']};
        const altUp = {type: 'keyUp', keyCode: 'Alt'};
        const shown = () => page.evaluate(() => document.body.classList.contains('show-tab-numbers'));
        const waitShown = value => page.waitForFunction(value => document.body.classList.contains('show-tab-numbers') === value, value);
        const selected = () => page.evaluate(() => [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected')));

        // Holding Alt in a page shows numbers 1–8 on the first tabs and 9 on the last one, over the favicon.
        const opacity = () => page.locator('#TabList li .tab-favicon-wrapper').first().evaluate(wrapper => getComputedStyle(wrapper.querySelector('.tab-favicon')).opacity);
        const before = await opacity();
        await send(altDown);
        assert.equal(await shown(), false, 'A quick chord does not flash the numbers');
        await waitShown(true);
        const labels = await page.locator('#TabList li .tab-favicon-wrapper').evaluateAll(wrappers => wrappers.map(wrapper => getComputedStyle(wrapper, '::after').content));
        assert.deepEqual(labels.slice(0, 8), Array(8).fill('counter(tab)'));
        assert.deepEqual(labels.slice(8, -1), Array(count - 9).fill('none'));
        assert.equal(labels.at(-1), '"9"');
        assert.equal(await opacity(), before, 'The favicon is not dimmed');
        await send(altUp);
        await waitShown(false);

        // Alt+1 selects the first tab and hides the numbers, although the page never reports the release.
        await send(altDown);
        await waitShown(true);
        await send({type: 'keyDown', keyCode: '1', modifiers: ['alt']});
        await page.waitForFunction(() => document.querySelector('#TabList li').classList.contains('selected'));
        assert.equal(await shown(), false);
        await ready();
        await send({type: 'keyDown', keyCode: 'Alt', modifiers: ['alt', 'isAutoRepeat']});
        await page.waitForTimeout(300);
        assert.equal(await shown(), false, 'Only a fresh press shows them again');
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        await send(altDown);
        await waitShown(true);
        await application.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].emit('blur'));
        await waitShown(false);

        // The same holds with the keyboard in Fly's own interface.
        await page.evaluate(() => window.executeBrowserAction('focus-sidebar'));
        await page.waitForFunction(() => document.activeElement.id === 'Sidebar');
        await send(altDown, false);
        await waitShown(true);
        await send(altUp, false);
        await waitShown(false);
        await send({type: 'keyDown', keyCode: 'Alt', modifiers: ['alt']}, false);
        await send({type: 'keyDown', keyCode: 'S', modifiers: ['alt']}, false);
        await page.waitForTimeout(300);
        assert.equal(await shown(), false, 'Another key in the chord keeps them hidden');
        await send(altUp, false);

        // A page in passthrough mode receives Alt+1 itself, so no numbers are shown.
        await page.locator('webview:visible').evaluate(view => view.focus());
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        const first = await selected();
        await send({type: 'keyDown', keyCode: 'Escape', modifiers: ['shift']});
        await send({type: 'keyUp', keyCode: 'Escape', modifiers: ['shift']});
        await send(altDown);
        await page.waitForTimeout(300);
        assert.equal(await shown(), false);
        await send({type: 'keyDown', keyCode: '9', modifiers: ['alt']});
        await page.waitForTimeout(200);
        assert.equal(await selected(), first);
        assert.deepEqual(errors, []);
        console.log('Tab numbers: shown while Alt is held in pages and the interface, 1–8 and 9 on the last tab, hidden by any other key, blur and passthrough.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exit(1); });
