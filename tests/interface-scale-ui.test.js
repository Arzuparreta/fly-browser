const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-interface-scale-'));
    const entry = createElectronFixture(root, temporary);
    const configPath = path.join(temporary, '.config/fly-browser/config.json');
    const server = http.createServer((request, response) => {
        response.end('<!doctype html><title>Scale fixture</title><input value="Editable page"><p>Website text</p>');
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
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).setZoomFactor(1.1), url);
        await openSettings(page);
        await choose(page, 'large');
        assert.equal(await zoom(), 1.25);
        assert(Math.abs(await guestZoom() - 1.1) < 0.001, 'Interface scaling must preserve website zoom');
        assert.equal(JSON.parse(fs.readFileSync(configPath)).interface_scale, 'large');
        assert.equal(await page.locator('#setting-interface-scale').evaluate(el => el === document.activeElement), true);

        // A small laptop viewport must still expose the size selector and close button.
        await page.setViewportSize({ width: 1024, height: 640 });
        for (const selector of ['#setting-interface-scale', '#CloseSettingsBtn']) {
            const box = await page.locator(selector).boundingBox();
            const viewport = await page.evaluate(() => ({ width: innerWidth, height: innerHeight }));
            assert(box.x >= 0 && box.y >= 0 && box.x + box.width <= viewport.width + 1 && box.y + box.height <= viewport.height + 1, `${selector} must fit`);
        }
        const capture = await application.evaluate(async ({ BrowserWindow }) => (await BrowserWindow.getAllWindows()[0].webContents.capturePage()).toPNG().toString('base64'));
        fs.writeFileSync('/tmp/fly-interface-large.png', Buffer.from(capture, 'base64'));
        await application.close(); application = null;
        page = await launch();
        assert.equal(await zoom(), 1.25, 'Large must survive restart');
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url + 'new');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && !v.isLoading(); } catch { return false; } }), url + 'new');
        assert.equal(await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomFactor(), url + 'new'), 1, 'New pages must start at normal website zoom in Large mode');
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).loadURL(url + '/next'), url + 'new');
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && !v.isLoading(); } catch { return false; } }), url + 'new/next');
        assert.equal(await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).getZoomFactor(), url + 'new/next'), 1, 'Navigation must preserve independent page scaling');
        await openSettings(page);
        assert.equal(await page.locator('#setting-interface-scale').inputValue(), 'large');
        await choose(page, 'normal');
        assert.equal(await zoom(), 1);
        assert.equal(JSON.parse(fs.readFileSync(configPath)).interface_scale, 'normal');
        const pid = await application.evaluate(() => process.pid);
        const blockedWrite = `${configPath}.${pid}.tmp`;
        fs.mkdirSync(blockedWrite);
        await page.locator('#setting-interface-scale').selectOption('large');
        await page.waitForFunction(() => document.getElementById('interface-scale-status').textContent.includes('Could not save'));
        assert.equal(await page.locator('#setting-interface-scale').inputValue(), 'normal');
        assert.equal(await zoom(), 1, 'Failed saves must leave the active scale unchanged');
        fs.rmdirSync(blockedWrite);
        await application.close(); application = null;
        const cfg = JSON.parse(fs.readFileSync(configPath));
        cfg.interface_scale = 'invalid';
        fs.writeFileSync(configPath, JSON.stringify(cfg));
        page = await launch();
        assert.equal(await zoom(), 1, 'Invalid stored values must safely fall back to Normal');
        assert.equal(await page.locator('#setting-interface-scale').inputValue(), 'normal');
        console.log('Interface scale UI passed');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
