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
    const server = http.createServer((request, response) => {
        if (request.url === '/frame') return response.end('<!doctype html><body style="margin:0;background:#ccc">Cross-origin frame</body>');
        // Plain text, a press whose default is cancelled and a cross-origin frame all move focus into the guest.
        response.end(`<!doctype html><title>Dismiss fixture</title><body style="margin:0"><p style="height:100px;margin:0">Plain text</p><div style="height:100px" onmousedown="event.preventDefault()">Cancelled press</div><iframe src="http://localhost:${server.address().port}/frame" style="width:100%;height:100px;border:0;display:block"></iframe><input id="editable" style="margin:40px 0 0 300px"></body>`);
    });
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
        // Keep fixture clicks clear of both the floating bar and an expanded sidebar.
        const safeX = (await page.locator('webview:visible').boundingBox()).width - 24;
        const open = async () => {
            await page.keyboard.press('Control+l');
            await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
            await bar.fill('discarded');
        };
        const guestValue = () => page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('document.getElementById("editable").value'));

        // Focus reaching the page from other browser chrome also closes the bar.
        await page.mouse.move(700, 400);
        for (let round = 0; round < 3; round++) {
            // Alt+S straight after the bar takes focus must not be undone by its pending select().
            await page.keyboard.press('Control+l');
            await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
            await page.keyboard.press('Alt+s');
            await page.waitForTimeout(300);
            assert(await page.evaluate(() => document.getElementById('Sidebar').contains(document.activeElement)), 'Alt+S moves focus to the sidebar');
            assert(await bar.isVisible());
            await page.mouse.click(700, 400);
            await bar.waitFor({state: 'hidden'});
        }

        // A click in the page closes the bar and the page keeps the click and the typing.
        await open();
        const point = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('(()=>{const r=document.getElementById("editable").getBoundingClientRect(); return {x:r.x+10,y:r.y+10};})()'));
        await page.locator('webview:visible').click({position: point});
        await bar.waitFor({state: 'hidden'});
        await page.keyboard.type('ok');
        await page.waitForFunction(async () => await [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('document.getElementById("editable").value') === 'ok');
        assert.equal(await guestValue(), 'ok');
        assert.equal(await page.locator('#AddressSuggestions').isVisible(), false);

        await page.keyboard.press('Escape');
        for (const y of [50, 150, 250]) {
            await open();
            await page.locator('webview:visible').click({position: {x: safeX, y}});
            await bar.waitFor({state: 'hidden'});
        }
        // Playwright's synthetic keys skip before-input-event, so inside a cross-origin frame they
        // never reach Fly's shortcuts (real keys do); return to the page document first.
        await page.locator('webview:visible').click({position: {x: safeX, y: 50}});

        // Clicks inside the bar keep it open; reopening after a page click starts from the page address.
        await open();
        await bar.click();
        assert(await bar.isVisible());
        await page.locator('webview:visible').click({position: {x: safeX, y: 50}});
        await bar.waitFor({state: 'hidden'});
        await page.keyboard.press('Control+l');
        await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
        assert.notEqual(await bar.inputValue(), 'discarded');

        // A press on the suggestion list that selects nothing keeps focus in the bar, so a page click still closes it.
        await bar.fill('1+1');
        await page.locator('#AddressSuggestions').waitFor({state: 'visible'});
        const list = await page.locator('#AddressSuggestions').boundingBox();
        await page.mouse.click(list.x + list.width / 2, list.y + 2);
        assert(await bar.isVisible());
        assert.equal(await page.evaluate(() => document.activeElement.id), 'WideAddressBar');
        await page.locator('webview:visible').click({position: {x: safeX, y: 50}});
        await bar.waitFor({state: 'hidden'});

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

        // A click on browser chrome outside the bar closes it and still runs that control; going back leaves the fixture page, so this runs last.
        await open();
        await page.mouse.move(10, 100);
        const before = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).getURL());
        await page.locator('#back-btn').click();
        await bar.waitFor({state: 'hidden'});
        await page.waitForFunction(before => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).getURL() !== before, before);
        // Both mouse entry points toggle the bar without an outside-press reopen race.
        for (const selector of ['#toggle-nav-btn', '#search-address-btn']) {
            await page.mouse.move(10, 100);
            await page.locator(selector).click();
            await page.waitForFunction(() => document.activeElement.id === 'WideAddressBar');
            await page.locator(selector).click();
            await bar.waitFor({state: 'hidden'});
        }
        assert.deepEqual(errors, []);
        console.log('Address bar dismiss UI checks passed');
    } finally {
        await application?.close();
        server.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exit(1); });
