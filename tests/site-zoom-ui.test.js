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
        const guestZoom = () => page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomFactor(), url);
        await page.evaluate(() => window.executeBrowserAction('zoom-in'));
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomLevel() === 0.5, url);
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url + 'second');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && v.getZoomLevel() === 0.5; } catch { return false; } }), url + 'second');
        await openSettings(page);
        await choose(page, 'large');
        assert(Math.abs(await guestZoom() - Math.pow(1.2, 0.5)) < 0.001);
        await application.close(); application = null;
        page = await launch();
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
        await page.evaluate(() => window.executeBrowserAction('reset-zoom'));
        await page.waitForFunction(() => [...document.querySelectorAll('webview')].every(v => { try { return v.getZoomLevel() === 0; } catch { return true; } }));
        console.log('Site zoom UI passed');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
