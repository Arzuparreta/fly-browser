const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-site-zoom-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => {
        response.end('<!doctype html><title>Site zoom fixture</title><input value="Editable page"><p>Website text</p>');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    let application;
    const launch = async () => {
        application = await _electron.launch({
            executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]
        });
        const page = await application.firstWindow();
        await application.evaluate(({ BrowserWindow }) => {
            const window = BrowserWindow.getAllWindows()[0];
            window.unmaximize();
            window.setBounds({ x: 0, y: 0, width: 1280, height: 900 });
        });
        page.setDefaultTimeout(15000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    const zoom = () => application.evaluate(({ BrowserWindow }) => BrowserWindow.getAllWindows()[0].webContents.getZoomFactor());
    const openSettings = async page => {
        await page.evaluate(() => window.executeBrowserAction('toggle-help'));
        await page.locator('#SettingsTab-appearance').click();
    };
    const choose = async (page, value) => {
        await page.locator('#setting-interface-scale').selectOption(value);
        await page.waitForFunction(() => document.getElementById('interface-scale-status').textContent === 'Saved');
    };
    try {
        let page = await launch();
        assert.equal(await zoom(), 1);
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url);
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && !v.isLoading(); } catch { return false; } }), url);
        assert.equal(await application.evaluate(({ webContents }, url) =>
            webContents.getAllWebContents().find(v => v.getType() === 'webview' && v.getURL() === url).getZoomMode(), url),
        'isolated', 'Page scaling must use native isolated zoom, with hostname sharing managed by Fly');
        const guestZoom = () => page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomFactor(), url);
        const viewportWidth = () => page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).executeJavaScript('window.innerWidth'), url);
        let indicator = page.locator('#StatusBarZoom');
        await indicator.waitFor({ state: 'hidden' });
        const originalWidth = await viewportWidth();
        assert(originalWidth > 100, `Fixture needs a laid-out viewport, got ${originalWidth}`);
        const expectRenderedZoom = async factor => {
            await page.waitForFunction(async ({ url, width }) => {
                const view = [...document.querySelectorAll('webview')].find(v => v.getURL() === url);
                return Math.abs(await view.executeJavaScript('window.innerWidth') - width) <= 2;
            }, { url, width: originalWidth / factor });
        };
        await page.evaluate(() => window.executeBrowserAction('zoom-in'));
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomLevel() === 0.5, url);
        await expectRenderedZoom(Math.pow(1.2, 0.5));
        await page.waitForFunction(() => {
            const indicator = document.getElementById('StatusBarZoom');
            return !indicator.hidden && indicator.textContent === 'Zoom 110%';
        });
        await page.evaluate(() => window.executeBrowserAction('zoom-out'));
        await expectRenderedZoom(1);
        await page.evaluate(() => window.executeBrowserAction('zoom-out'));
        await expectRenderedZoom(Math.pow(1.2, -0.5));
        await page.waitForFunction(() => document.getElementById('StatusBarZoom').textContent === 'Zoom 91%');
        await page.evaluate(() => window.executeBrowserAction('reset-zoom'));
        await expectRenderedZoom(1);
        await page.waitForFunction(() => document.getElementById('StatusBarZoom').textContent === 'Zoom 100%');
        const barBounds = await page.locator('#StatusBar').boundingBox();
        const zoomBounds = await indicator.boundingBox();
        assert(Math.abs(zoomBounds.x + zoomBounds.width / 2 - barBounds.x - barBounds.width / 2) < 1, 'Zoom feedback must be centered in the bar');
        if (process.env.FLY_ZOOM_SCREENSHOT) await page.screenshot({ path: process.env.FLY_ZOOM_SCREENSHOT });
        await indicator.waitFor({ state: 'hidden' });
        await page.evaluate(() => window.executeBrowserAction('zoom-in'));
        await expectRenderedZoom(Math.pow(1.2, 0.5));
        await application.evaluate(({ webContents }, url) => {
            const guest = webContents.getAllWebContents().find(v => v.getType() === 'webview' && v.getURL() === url);
            for (let i = 0; i < 9; i++) guest.emit('zoom-changed', {}, 'in');
            if (guest.getZoomLevel() !== 5) throw new Error(`Rapid zoom lost steps: ${guest.getZoomLevel()}`);
        }, url);
        await expectRenderedZoom(Math.pow(1.2, 5));
        await page.evaluate(() => window.executeBrowserAction('reset-zoom'));
        await expectRenderedZoom(1);
        await page.evaluate(() => window.executeBrowserAction('zoom-in'));
        await expectRenderedZoom(Math.pow(1.2, 0.5));
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url + 'second');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && v.getZoomLevel() === 0.5; } catch { return false; } }), url + 'second');
        await openSettings(page);
        await choose(page, 'large');
        assert(Math.abs(await guestZoom() - Math.pow(1.2, 0.5)) < 0.001);
        await application.close(); application = null;
        page = await launch();
        indicator = page.locator('#StatusBarZoom');
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url + 'restart');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && !v.isLoading() && v.getZoomLevel() === 0.5; } catch { return false; } }), url + 'restart');
        await application.evaluate(async ({ webContents }, url) => {
            const guest = webContents.getAllWebContents().find(v => v.getType() === 'webview' && v.getURL() === url);
            await guest.loadURL('data:text/html,<input value="Editable page">');
            if (guest.getZoomLevel() !== 0) throw new Error('Unrelated page inherited site zoom');
            await guest.loadURL(url);
            if (guest.getZoomLevel() !== 0.5) throw new Error('Returning to site lost saved zoom');
            guest.emit('zoom-changed', {}, 'in');
        }, url + 'restart');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].filter(v => { try { return v.getURL().startsWith(url); } catch { return false; } }).every(v => v.getZoomLevel() === 1), url);
        await page.waitForFunction(() => !document.getElementById('StatusBarZoom').hidden && document.getElementById('StatusBarZoom').textContent === 'Zoom 120%');
        const preferenceFile = path.join(temporary, '.config', 'fly-browser', 'site-zoom.json');
        const saved = fs.readFileSync(preferenceFile, 'utf8');
        await indicator.waitFor({ state: 'hidden' });
        await page.evaluate(url => {
            for (const id of ['private-zoom-first', 'private-zoom-peer']) {
                const view = document.createElement('webview');
                view.id = id;
                view.setAttribute('partition', 'FlyPrivateProfile');
                view.src = url + id;
                document.body.appendChild(view);
            }
        }, url);
        await page.waitForFunction(() => ['private-zoom-first', 'private-zoom-peer'].every(id => {
            try { const view = document.getElementById(id); return !view.isLoading() && view.getURL().startsWith('http:') && view.getZoomLevel() === 0; } catch { return false; }
        }));
        await page.evaluate(() => window.flyAPI.changeSiteZoom(document.getElementById('private-zoom-first').getWebContentsId(), 'zoom-in'));
        await page.waitForFunction(() => ['private-zoom-first', 'private-zoom-peer'].every(id => document.getElementById(id).getZoomLevel() === 0.5));
        assert.equal(fs.readFileSync(preferenceFile, 'utf8'), saved, 'Private zoom must leave disk unchanged');
        assert(await indicator.isHidden(), 'Background zoom must not show feedback for the active tab');
        assert.equal(await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomLevel(), url + 'restart'), 1, 'Private zoom must not affect normal tabs');
        await page.evaluate(() => window.flyAPI.changeSiteZoom(document.getElementById('private-zoom-peer').getWebContentsId(), 'reset-zoom'));
        await page.waitForFunction(() => ['private-zoom-first', 'private-zoom-peer'].every(id => document.getElementById(id).getZoomLevel() === 0));
        assert.equal(fs.readFileSync(preferenceFile, 'utf8'), saved, 'Private reset must leave disk unchanged');
        await page.evaluate(() => ['private-zoom-first', 'private-zoom-peer'].forEach(id => document.getElementById(id).remove()));
        await page.evaluate(() => window.executeBrowserAction('reset-zoom'));
        await page.waitForFunction(() => [...document.querySelectorAll('webview')].every(v => { try { return v.getZoomLevel() === 0; } catch { return true; } }));
        console.log('Site zoom UI passed');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
