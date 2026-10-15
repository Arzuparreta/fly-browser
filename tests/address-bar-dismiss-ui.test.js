const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-address-dismiss-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Dismiss fixture</title><input id="editable" style="margin:200px 0 0 300px">'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length); return view?.getWebContentsId() > 0 && !!view.getURL() && !view.isLoading();} catch {return false;}});
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `http://127.0.0.1:${server.address().port}`);
        const bar = page.locator('#WideAddressBar');
        const open = async () => {
            await page.keyboard.press('Control+l');
            await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
            await bar.fill('discarded');
        };
        const guestValue = () => page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('document.getElementById("editable").value'));

        // A click in the page closes the bar and the page keeps the click and the typing.
        await open();
        const point = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('(()=>{const r=document.getElementById("editable").getBoundingClientRect(); return {x:r.x+10,y:r.y+10};})()'));
        await page.locator('webview:visible').click({position: point});
        await bar.waitFor({state: 'hidden'});
        await page.keyboard.type('ok');
        await page.waitForFunction(async () => await [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('document.getElementById("editable").value') === 'ok');
        assert.equal(await guestValue(), 'ok');
        assert.equal(await page.locator('#AddressSuggestions').isVisible(), false);

        // A click on browser chrome outside the bar closes it and still runs that control.
        await open();
        await page.mouse.move(10, 100);
        await page.locator('#back-btn').click();
        await bar.waitFor({state: 'hidden'});

        // Clicks inside the bar keep it open; reopening starts from the page address, not the discarded text.
        await open();
        await bar.click();
        assert(await bar.isVisible());
        await page.keyboard.press('Escape');
        await bar.waitFor({state: 'hidden'});
        await page.keyboard.press('Control+l');
        await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
        assert.notEqual(await bar.inputValue(), 'discarded');
        await page.keyboard.press('Escape');

        // Opening from the command palette must not be closed by the palette handing focus back.
        await page.keyboard.press('Control+p');
        await page.waitForFunction(() => document.getElementById('CommandPaletteOverlay').contains(document.activeElement));
        await page.keyboard.type('Toggle Floating Address Bar');
        await page.keyboard.press('Enter');
        await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
        await page.waitForTimeout(300);
        assert(await bar.isVisible(), 'The bar opened from the palette must stay open');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'WideAddressBar');
        await page.keyboard.press('Escape');

        // Leaving the window, as with Alt+Tab, keeps the bar open and focused.
        await open();
        await page.evaluate(() => window.dispatchEvent(new Event('blur')));
        await application.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()[0].blur());
        await page.waitForTimeout(200);
        assert(await bar.isVisible(), 'Losing window focus must not close the bar');
        assert.deepEqual(errors, []);
        console.log('Address bar dismiss UI checks passed');
    } finally {
        await application?.close();
        server.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exit(1); });
