const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-quickmark-'));
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
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `${base}/marked`);
        await page.locator('webview:visible').click({position: {x: 300, y: 300}});
        const tabs = () => page.evaluate(() => document.querySelectorAll('#TabList li').length);
        const runFromPalette = async title => {
            await page.keyboard.press('Control+p');
            await page.waitForFunction(() => document.getElementById('CommandPaletteOverlay').contains(document.activeElement));
            await page.keyboard.type(title);
            await page.waitForFunction(title => document.querySelector('#PaletteList .palette-item.selected')?.textContent.includes(title), title);
            await page.keyboard.press('Enter');
            await page.waitForFunction(() => window.getComputedStyle(document.getElementById('CommandPaletteOverlay')).display === 'none');
        };

        // The palette actions wait for their key in the host instead of handing it to the page.
        await runFromPalette('Set Quickmark');
        await page.waitForTimeout(200);
        await page.keyboard.press('m');
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        const before = await tabs();
        await runFromPalette('Jump to Quickmark');
        await page.waitForTimeout(200);
        await page.keyboard.press('m');
        await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count + 1, before);
        await page.waitForFunction(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length)?.getURL().endsWith('/marked'));

        // A tab switch's delayed page focus must not take the key of a prompt started right after it.
        for (const withBar of [false, true]) {
            if (withBar) {
                await page.keyboard.press('Control+l');
                await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
            }
            const count = await tabs();
            await page.keyboard.press('Control+PageUp');
            await page.keyboard.press('Control+j');
            await page.waitForTimeout(150);
            await page.keyboard.press('m');
            await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count + 1, count);
        }
        assert.equal(await page.evaluate(() => window.flyAPI.readQuickmarks().then(marks => marks.m?.url)), `${base}/marked`);
        assert.deepEqual(errors, []);
        console.log('Quickmarks set and jump from the palette and right after a tab switch.');
    } finally {
        await application?.close();
        server.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exit(1); });
