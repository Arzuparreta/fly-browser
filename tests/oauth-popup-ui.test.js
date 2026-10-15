const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// The opener page stands in for a site's "Continue with Google" button, the popup
// for the provider, and the callback for the redirect that reports back and closes.
const pages = {
    '/': `<!doctype html><title>Opener</title><a id="blank" href="/linked" target="_blank">Linked</a><script>
        document.cookie = 'state=opener-state; path=/';
        addEventListener('message', event => { if (event.origin === location.origin) document.title = 'received ' + event.data; });
    </script>`,
    '/callback': `<!doctype html><title>Callback</title><script>
        window.opener.postMessage(document.cookie, location.origin);
        window.close();
    </script>`,
    '/linked': '<!doctype html><title>Linked</title>',
    '/plain': '<!doctype html><title>Plain</title>',
    '/isolated': '<!doctype html><title>Isolated</title>'
};

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-oauth-popup-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => {
        const pathname = new URL(request.url, 'http://localhost').pathname;
        if (pathname === '/authorise') {
            response.writeHead(302, {Location: '/callback'});
            return response.end();
        }
        response.setHeader('Content-Type', 'text/html');
        response.end(pages[pathname] || '');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const fixture = `http://127.0.0.1:${server.address().port}`;
    let application;
    try {
        application = await _electron.launch({
            executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]
        });
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.evaluate(() => {
            const count = document.querySelectorAll('#TabList li').length;
            for (let index = 0; index < count; index++) window.executeBrowserAction('remove-tab');
        });
        await page.locator('#split-toggle-btn').focus();
        await page.keyboard.press('Control+l');
        await page.locator('#WideAddressBar').fill(fixture + '/');
        await page.locator('#WideAddressBar').press('Enter');
        await page.waitForFunction(url => {
            const view = document.querySelector('webview');
            // A webview throws until dom-ready, which a fresh tab may not have reached yet.
            try { return view && !view.isLoading() && view.getURL() === url; } catch (e) { return false; }
        }, fixture + '/');
        const inGuest = code => page.evaluate(code => document.querySelector('webview').executeJavaScript(code, true), code);
        const windowCount = () => application.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows().length);
        const tabCount = () => page.locator('#TabList li').count();

        // A sign-in popup keeps its opener and the workspace session, then closes itself
        const popupOpened = application.waitForEvent('window');
        popupOpened.catch(() => {});
        assert.equal(await inGuest(`!!window.open('/authorise', 'signin', 'width=480,height=600')`), true);
        const popup = await popupOpened;
        const popupClosed = popup.isClosed() ? Promise.resolve() : popup.waitForEvent('close');
        await page.waitForFunction(() => document.querySelector('webview').getTitle() === 'received state=opener-state');
        await popupClosed;
        assert.equal(await windowCount(), 1);
        assert.equal(await tabCount(), 1);

        // The close-tab shortcut closes a popup without touching the tabs behind it
        const staying = application.waitForEvent('window');
        staying.catch(() => {});
        await inGuest(`!!window.open('/linked', 'stay', 'width=400,height=400')`);
        const stayingPopup = await staying;
        await stayingPopup.waitForLoadState();
        const stayingClosed = stayingPopup.waitForEvent('close');
        // Playwright's keys bypass before-input-event, so send native input as the keyboard suite does
        await application.evaluate(({BrowserWindow}) => BrowserWindow.getAllWindows()
            .find(win => win.webContents.getURL().endsWith('/linked'))
            .webContents.sendInputEvent({type: 'keyDown', keyCode: 'W', modifiers: ['control']}));
        await stayingClosed;
        assert.equal(await windowCount(), 1);
        assert.equal(await tabCount(), 1);

        // Links, plain window.open calls and noopener popups still open as tabs
        await inGuest(`document.getElementById('blank').click()`);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 2);
        assert.equal(await inGuest(`window.open('/plain')`), null);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 3);
        assert.equal(await inGuest(`window.open('/isolated', 'isolated', 'width=480,height=600,noopener')`), null);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 4);
        assert.equal(await windowCount(), 1);
        const session = await page.evaluate(() => window.flyAPI.getSession());
        assert.deepEqual(session.workspaces[session.current_workspace], ['/', '/linked', '/plain', '/isolated'].map(p => fixture + p));
        assert.deepEqual(errors, []);
        console.log('Sign-in popups report back to their opener; links and noopener windows open as tabs.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
