const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-split-view-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => response.end(`<!doctype html><title>${request.url}</title>${request.url}`));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}`;
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length); return view?.getWebContentsId() > 0 && !!view.getURL() && !view.isLoading();} catch {return false;}});
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `${base}/first`);
        const visible = () => page.evaluate(() => [...document.querySelectorAll('#webview-container webview')].filter(view => getComputedStyle(view).display !== 'none').map(view => new URL(view.getURL()).pathname));
        const tabs = () => page.evaluate(() => document.querySelectorAll('#TabList li').length);

        // With one tab, toggling split opens a second tab beside it and asks for its address.
        assert.equal(await tabs(), 1);
        await page.evaluate(() => window.executeBrowserAction('toggle-split'));
        await page.waitForFunction(() => document.getElementById('webview-container').classList.contains('split-active'));
        assert.equal(await tabs(), 2);
        await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
        await page.locator('#WideAddressBar').fill(`${base}/second`);
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].some(view => view.getURL().endsWith('/second') && !view.isLoading()));
        assert.deepEqual((await visible()).sort(), ['/first', '/second']);

        // Leaving split shows only the selected tab, whichever pane it was in.
        for (const index of [0, 1]) {
            await page.evaluate(() => { const split = document.getElementById('webview-container').classList.contains('split-active'); if (!split) window.executeBrowserAction('toggle-split'); });
            await page.waitForFunction(() => document.getElementById('webview-container').classList.contains('split-active'));
            await page.mouse.move(500, await page.evaluate(() => innerHeight - 10));
            await page.mouse.move(10, 100);
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().right >= 259);
            const box = await page.locator('#TabList li').nth(index).boundingBox();
            await page.mouse.click(box.x + box.width / 2, box.y + box.height / 2);
            await page.mouse.move(600, 500);
            await page.waitForFunction(index => document.querySelectorAll('#TabList li')[index].classList.contains('selected'), index);
            await page.evaluate(() => window.executeBrowserAction('toggle-split'));
            await page.waitForFunction(() => !document.getElementById('webview-container').classList.contains('split-active'));
            assert.deepEqual(await visible(), [index ? '/second' : '/first'], `Leaving split with tab ${index + 1} selected`);
        }
        assert.deepEqual(errors, []);
        console.log('Split view opens from a single tab and leaves showing only the selected tab.');
    } finally {
        await application?.close();
        server.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exit(1); });
