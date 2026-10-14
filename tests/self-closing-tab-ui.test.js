const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-self-closing-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => {
        response.setHeader('Content-Type', 'text/html');
        response.end(`<!doctype html><title>${request.url}</title><a id="next" href="/next">Next</a>`);
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
        const loaded = url => page.waitForFunction(url => [...document.querySelectorAll('webview')]
            .some(view => { try { return !view.isLoading() && view.getURL() === url; } catch (e) { return false; } }), url);
        // Opens a selected tab through the same path as links and popups
        const open = async url => {
            await application.evaluate(({BrowserWindow}, url) => BrowserWindow.getAllWindows()[0].webContents
                .send('master-shortcut', 'spawn-tab-with-url', url), url);
            await loaded(url);
        };
        await open(fixture + '/kept');
        await open(fixture + '/closing');
        const selectedGuest = () => page.evaluate(() => {
            const index = [...document.querySelectorAll('#TabList li')].findIndex(li => li.classList.contains('selected'));
            const session = window.flyAPI.getSession();
            return session.then(s => ({index, url: s.workspaces[s.current_workspace][index]}));
        });
        const liveFocusedGuest = () => page.evaluate(() => {
            const view = document.activeElement;
            if (!view || view.tagName !== 'WEBVIEW') return null;
            try { return view.getURL(); } catch (e) { return null; }
        });
        const inSelected = code => page.evaluate(code => {
            const views = [...document.querySelectorAll('webview')].filter(v => v.offsetParent !== null);
            return views[0].executeJavaScript(code, true);
        }, code);

        // A page that closes itself after navigating, as OAuth callbacks do, closes its tab
        await inSelected(`document.getElementById('next').click()`);
        await loaded(fixture + '/next');
        await inSelected('window.close()').catch(() => {});
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 1);
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        assert.equal(await liveFocusedGuest(), fixture + '/kept');
        assert.deepEqual(await selectedGuest(), {index: 0, url: fixture + '/kept'});

        // Keyboard shortcuts reach Fly again: Ctrl+W from the remaining page closes it
        const guestId = await page.evaluate(() => document.activeElement.getWebContentsId());
        await application.evaluate(({webContents}, id) => webContents.fromId(id).sendInputEvent({type: 'keyDown', keyCode: 'W', modifiers: ['control']}), guestId);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 0);

        // A pinned tab that closes itself keeps its place and loads again
        await open(fixture + '/pinned');
        await page.evaluate(() => window.togglePinActiveTab());
        await inSelected('window.close()').catch(() => {});
        await page.waitForFunction(url => {
            const view = document.activeElement;
            if (view.tagName !== 'WEBVIEW') return false;
            try { return !view.isLoading() && view.getURL() === url; } catch (e) { return false; }
        }, fixture + '/pinned');
        assert.equal(await page.locator('#TabList li').count(), 1);
        assert.deepEqual(await selectedGuest(), {index: 0, url: fixture + '/pinned'});
        assert.deepEqual(errors, []);
        console.log('Self-closing pages close their tab and return keyboard focus; pinned tabs reload in place.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
