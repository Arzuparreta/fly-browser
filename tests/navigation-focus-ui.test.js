const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-navigation-focus-'));
    const entry = path.join(temporary, 'start.cjs');
    fs.writeFileSync(entry, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))}); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    const server = http.createServer((request, response) => {
        const url = new URL(request.url, 'http://localhost');
        const send = () => {
            response.setHeader('Content-Type', 'text/html');
            response.end('<!doctype html><title>Search results</title><input id="search" style="margin-left:260px"><a href="/result">First result</a><a href="/another">Second result</a>');
        };
        setTimeout(send, url.searchParams.get('q') === 'slow' ? 300 : 0);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const fixture = `http://127.0.0.1:${server.address().port}`;
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.evaluate(async url => {
            await window.miseAPI.updateBrowserSettings({search_engine: url + '/?q=%s'});
            const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
            await view.loadURL(url);
        }, fixture);
        const snapshot = () => page.evaluate(async () => {
            const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
            return {host: document.activeElement.tagName, hostId: document.activeElement.id,
                guest: await view.executeJavaScript('({focused:document.hasFocus(), active:document.activeElement.tagName, hints:!!document.getElementById("mise-hint-layer"), ready:typeof window.toggleHints === "function"})')};
        });
        const waitGuest = async predicate => {
            for (let attempt = 0; attempt < 150; attempt++) {
                if (await predicate()) return;
                await page.waitForTimeout(20);
            }
            throw new Error('Timed out waiting for guest state');
        };
        const ready = query => waitGuest(() => page.evaluate(async query => {
            const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
            return !view.isLoading() && view.getURL().includes(`q=${query}`) && await view.executeJavaScript('typeof window.toggleHints === "function"');
        }, query));
        for (const query of ['first', 'second', 'third']) {
            await page.locator('#menu-btn').focus();
            await page.keyboard.press('Control+l');
            await page.locator('#WideAddressBar').fill(query);
            await page.locator('#WideAddressBar').press('Enter');
            await ready(query);
            const before = await snapshot();
            await page.keyboard.press('f');
            await waitGuest(async () => (await snapshot()).guest.hints).catch(() => {});
            const after = await snapshot();
            console.log('Search focus', {query, before, after});
            assert(after.guest.hints, 'Ctrl+L search results must accept f without clicking the guest');
            await page.keyboard.press('Escape');
        }
        // Cancelling or toggling the address bar closed must also return page controls.
        for (const closeKey of ['Escape', 'Control+l']) {
            await page.keyboard.press('Control+l');
            await page.locator('#WideAddressBar').fill('not submitted');
            await page.keyboard.press(closeKey);
            assert.equal((await snapshot()).host, 'WEBVIEW');
            await page.keyboard.press('f');
            await waitGuest(async () => (await snapshot()).guest.hints);
            await page.keyboard.press('Escape');
        }
        // Text editing remains local to the page; f is never made a global browser key.
        await page.locator('webview:visible').click({position: {x: 280, y: 20}});
        await page.keyboard.type('ff');
        const typed = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('document.getElementById("search").value'));
        assert.equal(typed, 'ff'); assert(!(await snapshot()).guest.hints);
        await page.keyboard.press('Escape');

        // A slow result must not steal focus back after the user opens settings.
        await page.keyboard.press('Control+l');
        await page.locator('#WideAddressBar').fill('slow');
        await page.locator('#WideAddressBar').press('Enter');
        await page.keyboard.press('Control+h');
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        await ready('slow');
        assert(await page.evaluate(() => document.getElementById('PreferencesOverlay').contains(document.activeElement)));
        await page.keyboard.press('Escape');
        // Host and guest focus return asynchronously; keys sent before that reach the host.
        await waitGuest(async () => { const state = await snapshot(); return state.host === 'WEBVIEW' && state.guest.focused; });
        await page.keyboard.press('f');
        await waitGuest(async () => (await snapshot()).guest.hints);
        await page.keyboard.press('Escape');

        // Loading the old page in the background must leave the new tab's address input alone.
        const sourceId = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).getWebContentsId());
        await page.keyboard.press('Control+l');
        await page.locator('#WideAddressBar').fill('slow');
        await page.locator('#WideAddressBar').press('Enter');
        await page.keyboard.press('Control+t');
        await page.locator('#WideAddressBar').waitFor({state: 'visible'});
        await waitGuest(() => application.evaluate(({webContents}, id) => !webContents.fromId(id).isLoading(), sourceId));
        assert.equal(await page.evaluate(() => document.activeElement.id), 'WideAddressBar');
        await page.keyboard.press('Escape');
        // Pointer history navigation releases sidebar focus; keyboard activation keeps it.
        const historyReady = path => waitGuest(() => page.evaluate(path => {
            const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
            return !view.isLoading() && view.getURL().endsWith(path);
        }, path));
        await page.evaluate(async url => {
            const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
            await view.loadURL(url + '/history-first');
            await view.loadURL(url + '/history-second');
        }, fixture);
        for (const [button, destination] of [['back-btn', '/history-first'], ['forward-btn', '/history-second']]) {
            await page.mouse.move(10, 100);
            await page.locator('#' + button).click();
            await historyReady(destination);
            assert.equal((await snapshot()).host, 'WEBVIEW', 'Pointer history navigation must return page focus');
            // Use host content for pointer exit; guest entry is covered by the overlay suite.
            await page.mouse.move(500, await page.evaluate(() => innerHeight - 10));
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
            assert.equal(await page.locator('#' + button).evaluate(button => button.matches(':focus')), false);
        }
        for (const [button, destination, key] of [['back-btn', '/history-first', 'Enter'], ['forward-btn', '/history-second', 'Space']]) {
            await page.locator('#' + button).focus();
            await page.locator('#' + button).press(key);
            await historyReady(destination);
            assert.equal((await snapshot()).hostId, button, 'Keyboard history navigation must retain button focus');
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        }
        await page.keyboard.press('Control+Shift+z');
        await page.waitForFunction(() => document.body.classList.contains('sidebar-pinned'));
        await page.locator('#back-btn').click();
        await historyReady('/history-first');
        await page.mouse.move(500, await page.evaluate(() => innerHeight - 10));
        assert.equal((await snapshot()).host, 'WEBVIEW');
        assert(await page.locator('#Sidebar').evaluate(sidebar => sidebar.getBoundingClientRect().width >= 259));
        await page.keyboard.press('Control+Shift+z');
        await page.waitForFunction(() => document.body.classList.contains('sidebar-auto-collapse'));
        assert.deepEqual(errors, []);
        console.log('Search navigation focus and hint activation passed.');
    } finally {
        if (application) await application.close();
        server.close(); fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
