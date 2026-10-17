const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const root = path.resolve(__dirname, '..');
const createFixture = require('./electron-fixture');
(async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-frequent-ui-'));
    const config = path.join(directory, '.config/fly-browser');
    fs.mkdirSync(config, {recursive:true});
    fs.writeFileSync(path.join(config, 'history.json'), JSON.stringify([
        {url:'https://example.com/a', timestamp:Date.now()},
        {url:'https://example.org/b', timestamp:Date.now() - 86400000}
    ]));
    const entry = createFixture(root, directory);
    const server = http.createServer((_req, res) => res.end('<title>Private visit</title>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const privateUrl = `http://127.0.0.1:${server.address().port}/private`;
    let app;
    try {
        const launch = async () => {
            app = await _electron.launch({executablePath:path.join(root, 'node_modules/electron/dist/electron'), args:['--no-sandbox','--disable-gpu','--ozone-platform=x11',entry]});
            const page = await app.firstWindow();
            await page.waitForFunction(() => !!window.executeBrowserAction);
            await page.waitForFunction(() => {try { const v = document.querySelector('webview'); return v && !v.isLoading() && v.getURL().endsWith('/assets/new-tab.html'); } catch { return false; }});
            await page.evaluate(async () => (await import('./modules/newTab.js')).refreshNewTabs());
            return page;
        };
        let page = await launch();
        const guest = expression => page.evaluate(expression => document.querySelector('webview').executeJavaScript(expression), expression);
        assert.equal(await guest('document.querySelectorAll(".site-row").length'), 2);
        // A bookmark save pushes both lists into the visible new tab without a manual refresh.
        await page.evaluate(() => window.flyAPI.saveBookmarks([{url:'https://example.com/saved-page', title:'Saved Example'}]));
        await page.waitForFunction(async () => document.querySelector('webview').executeJavaScript('document.querySelectorAll(".site-row").length === 1 && document.querySelector("#bookmarks a .bookmark-title")?.textContent === "Saved Example"'));
        assert.equal(await guest('document.querySelector("#sites a").hostname'), 'example.org');
        await page.evaluate(() => window.flyAPI.saveBookmarks([]));
        await page.waitForFunction(async () => document.querySelector('webview').executeJavaScript('document.querySelectorAll(".site-row").length === 2 && document.querySelectorAll("#bookmarks a").length === 0'));

        await page.evaluate(() => window.openPreferencesPane('appearance'));
        const toggle = () => page.locator('#setting-frequent-sites-toggle');
        assert(await toggle().isChecked());
        await page.locator('label[for="setting-frequent-sites-toggle"]').click();
        await page.waitForFunction(async () => (await window.flyAPI.getBrowserSettings()).show_frequent_sites === false);
        await app.close(); app = null;
        page = await launch();
        assert.equal(await guest('document.getElementById("frequent-sites").hidden'), true);
        assert.equal(await guest('document.querySelectorAll(".site-row").length'), 0);
        assert.equal((await page.evaluate(() => window.flyAPI.searchHistory(''))).length, 2);
        await page.evaluate(() => window.openPreferencesPane('appearance'));
        assert.equal(await toggle().isChecked(), false);
        await page.locator('label[for="setting-frequent-sites-toggle"]').click();
        await page.waitForFunction(async () => (await window.flyAPI.getBrowserSettings()).show_frequent_sites === true);
        await page.evaluate(() => window.executeBrowserAction('remove-tab'));
        await page.evaluate(async () => (await import('./modules/newTab.js')).refreshNewTabs());
        assert.equal(await guest('document.getElementById("frequent-sites").hidden'), false);
        assert.equal(await guest('document.querySelectorAll(".site-row").length'), 2);

        await guest('document.querySelector("button[data-hide-site]").click()');
        await page.waitForFunction(async () => (await window.flyAPI.getFrequentSites()).length === 1);
        await page.evaluate(async () => (await import('./modules/newTab.js')).refreshNewTabs());
        assert.equal(await guest('document.querySelector("#sites a").hostname'), 'example.org');
        assert.equal((await page.evaluate(() => window.flyAPI.searchHistory(''))).length, 2);
        await app.close(); app = null;
        page = await launch();
        assert.equal(await guest('document.querySelectorAll(".site-row").length'), 1);
        // A private guest must never write either full or in-page navigation to history.
        await page.evaluate(url => new Promise(resolve => {
            const view = document.createElement('webview');
            view.setAttribute('partition', 'FlyPrivateProfile');
            view.src = url;
            view.addEventListener('did-finish-load', async () => {await view.executeJavaScript('location.hash="secret"'); resolve();}, {once:true});
            document.body.append(view);
        }), privateUrl);
        await page.evaluate(() => new Promise(resolve => {
            const view = document.querySelector('webview[partition="FlyPrivateProfile"]');
            view.addEventListener('did-finish-load', async () => {await (await import('./modules/newTab.js')).refreshNewTab(view); resolve();}, {once:true});
            view.src = document.querySelector('webview').getURL();
        }));
        assert.equal(await page.evaluate(() => document.querySelector('webview[partition="FlyPrivateProfile"]').executeJavaScript('document.querySelectorAll(".site-row").length')), 0);
        assert(!(await page.evaluate(() => window.flyAPI.searchHistory(''))).some(item => item.url.includes('/private')));
        await page.evaluate(async () => { await window.flyAPI.purgeHistory(); await (await import('./modules/newTab.js')).refreshNewTabs(); });
        assert.equal(await guest('document.getElementById("empty").hidden'), false);
        assert.deepEqual(await page.evaluate(() => window.flyAPI.getFrequentSites()), []);
        console.log('Frequent sites Electron: rendering, removal, restart persistence, intact history, private navigation and purge pass.');
    } finally {
        if (app) await app.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(directory, {recursive:true, force:true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
