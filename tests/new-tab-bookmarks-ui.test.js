const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const root = path.resolve(__dirname, '..');
const createFixture = require('./electron-fixture');
(async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-new-tab-bookmarks-'));
    const server = http.createServer((req, res) => req.url === '/favicon.ico' ? (res.writeHead(200, {'Content-Type':'image/svg+xml'}), res.end('<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16"><rect width="16" height="16" fill="red"/></svg>')) : res.end('<!doctype html><title>Saved destination</title>Bookmark opened'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const destination = `http://127.0.0.1:${server.address().port}/saved`;
    const configDir = path.join(directory, '.config/fly-browser');
    fs.mkdirSync(configDir, {recursive:true});
    const bookmarks = [{url:destination, title:'My saved page'}, {url:'https://example.com', title:'<script>literal text</script>'},
        {url:'javascript:alert(1)', title:'Unsafe'}, {url:'invalid'}, null,
        ...Array.from({length:20}, (_, index) => ({url:`https://example.com/${index}`, title:'Long bookmark title '.repeat(12)}))];
    fs.writeFileSync(path.join(configDir, 'bookmarks.json'), JSON.stringify(bookmarks));
    fs.writeFileSync(path.join(configDir, 'history.json'), JSON.stringify([{url:'https://frequent.example', timestamp:Date.now()}]));
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({show_frequent_sites:false}));
    let app;
    try {
        app = await _electron.launch({executablePath:path.join(root, 'node_modules/electron/dist/electron'), args:['--no-sandbox','--disable-gpu','--ozone-platform=x11', createFixture(root, directory)]});
        const page = await app.firstWindow();
        page.setDefaultTimeout(15000);
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {return document.querySelector('webview').getURL().endsWith('/assets/new-tab.html') && !document.querySelector('webview').isLoading();} catch {return false;}});
        const refresh = () => page.evaluate(async () => (await import('./modules/newTab.js')).refreshNewTabs());
        const guest = expression => page.evaluate(expression => document.querySelector('webview').executeJavaScript(expression), expression);
        await refresh();
        assert.equal(await guest('document.querySelectorAll("#bookmarks img, #bookmarks .bookmark-icon").length'), 0);
        assert.equal(await guest('document.querySelectorAll("form, input, #address-hint").length'), 0);
        assert(await guest('Math.abs(document.querySelector("#bookmarks a").getBoundingClientRect().left - document.getElementById("clock").getBoundingClientRect().left) < 2'));
        assert(await guest('!!(document.getElementById("saved-bookmarks").compareDocumentPosition(document.getElementById("frequent-sites")) & Node.DOCUMENT_POSITION_FOLLOWING)'));
        const newTabUrl = await page.evaluate(() => document.querySelector('webview').getURL());
        assert.equal(await guest('document.querySelectorAll("#bookmarks a").length'), 22);
        assert.equal(await guest('document.querySelectorAll("#bookmarks script").length'), 0);
        assert.equal(await guest('document.getElementById("frequent-sites").hidden'), true);
        assert.equal(await guest('document.getElementById("saved-bookmarks").hidden'), false);
        // Guest zoom is independent of interface zoom. The sidebar clearance
        // must therefore come from host geometry at both interface scales.
        for (const scale of ['normal', 'large']) {
            await page.evaluate(scale => window.flyAPI.updateBrowserSettings({interface_scale:scale}), scale);
            for (const width of [1100, 1600]) {
                await app.evaluate(({BrowserWindow}, width) => BrowserWindow.getAllWindows()[0].setContentSize(width, 900), width);
                await page.evaluate(() => window.executeBrowserAction('focus-sidebar'));
                await page.waitForTimeout(300);
                const geometry = await page.evaluate(() => {
                    const sidebar = document.getElementById('Sidebar').getBoundingClientRect();
                    const view = document.querySelector('webview').getBoundingClientRect();
                    return {sidebarRight:sidebar.right, viewLeft:view.left, viewWidth:view.width};
                });
                assert(geometry.viewLeft >= geometry.sidebarRight - 1, `${scale}: expanded sidebar must not cover the new tab`);
                assert(geometry.viewWidth > 0);
                await page.evaluate(() => window.executeBrowserAction('focus-sidebar'));
                await page.waitForTimeout(300);
                assert(Math.abs(await page.locator('webview').evaluate(view => view.getBoundingClientRect().left) - geometry.viewLeft) < 1, 'Opening and closing the sidebar must not move new-tab content');
                await page.waitForFunction(async () => {
                    const view = document.querySelector('webview');
                    const width = view.getBoundingClientRect().width * window.flyAPI.getInterfaceZoomFactor() / view.getZoomFactor();
                    return Math.abs(await view.executeJavaScript('innerWidth') - width) < 2;
                });
                assert(await guest('document.documentElement.scrollWidth <= innerWidth'));
            }
        }
        await page.evaluate(() => window.flyAPI.updateBrowserSettings({interface_scale:'normal'}));
        for (const width of [1100, 650]) {
            await app.evaluate(({BrowserWindow}, width) => BrowserWindow.getAllWindows()[0].setContentSize(width, 800), width);
            assert(await guest('document.documentElement.scrollWidth <= innerWidth'));
            await page.waitForTimeout(100);
            assert(await guest('Math.abs(document.querySelector("#bookmarks a").getBoundingClientRect().left - document.getElementById("clock").getBoundingClientRect().left) < 2'));
        }
        await guest('document.querySelector("#bookmarks a").click()');
        await page.waitForFunction(url => document.querySelector('webview').getURL() === url, destination);
        assert.equal(await page.locator('webview').evaluate(view => view.classList.contains('new-tab-page')), false, 'Navigating to a website must restore the full page width');
        assert.equal(await page.locator('#TabList li').count(), 1);
        await page.evaluate(async url => {
            await window.flyAPI.saveBookmarks([{url:'https://updated.example', title:'Updated bookmark'}]);
            await document.querySelector('webview').loadURL(url);
        }, newTabUrl);
        await refresh();
        assert.equal(await guest('document.querySelector("#bookmarks a .bookmark-title").textContent'), 'Updated bookmark');
        assert(await guest('Math.abs(document.querySelector("#bookmarks a").getBoundingClientRect().left - document.getElementById("clock").getBoundingClientRect().left) < 2'));
        await page.evaluate(async () => { await window.flyAPI.updateBrowserSettings({show_frequent_sites:true}); await (await import('./modules/newTab.js')).refreshNewTabs(); });
        assert(await guest('Math.abs(document.getElementById("bookmarks-label").getBoundingClientRect().left - document.getElementById("clock").getBoundingClientRect().left) < 2'));
        assert(await guest('Math.abs(document.querySelector("#bookmarks a").getBoundingClientRect().left - document.getElementById("sites").getBoundingClientRect().left) < 2'));
        if (process.env.FLY_BOOKMARKS_SCREENSHOT) {
            const png = await app.evaluate(async ({webContents}) => (await webContents.getAllWebContents().find(view => view.getType() === 'webview').capturePage()).toPNG().toString('base64'));
            fs.writeFileSync(process.env.FLY_BOOKMARKS_SCREENSHOT, Buffer.from(png, 'base64'));
        }
        await page.evaluate(async () => { await window.flyAPI.saveBookmarks([]); await (await import('./modules/newTab.js')).refreshNewTabs(); });
        assert.equal(await guest('document.getElementById("bookmarks-empty").hidden'), false);
        await page.evaluate(url => new Promise(resolve => {
            const view = document.createElement('webview'); view.setAttribute('partition', 'FlyPrivateProfile');
            view.addEventListener('did-finish-load', async () => {await (await import('./modules/newTab.js')).refreshNewTab(view); resolve();}, {once:true});
            view.src = url; document.body.append(view);
        }), newTabUrl);
        assert.equal(await page.evaluate(() => document.querySelector('webview[partition="FlyPrivateProfile"]').executeJavaScript('document.getElementById("saved-bookmarks").hidden')), true);
        assert.deepEqual(errors, []);
        console.log('New-tab bookmarks: local collection, safe titles/URLs, layout, same-tab links, refreshed data, empty and private states pass.');
    } finally {
        if (app) await app.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(directory, {recursive:true, force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
