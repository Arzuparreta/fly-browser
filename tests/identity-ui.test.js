const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-identity-'));
    const server = http.createServer((req, res) => res.end('<!doctype html><title>Search result</title><input aria-label="Editable page">'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const fixture = `http://127.0.0.1:${server.address().port}`;
    let app;
    try {
        app = await _electron.launch({executablePath:path.join(root, 'node_modules/electron/dist/electron'), args:['--no-sandbox', '--disable-gpu', '--ozone-platform=x11', createElectronFixture(root, temporary)]});
        const page = await app.firstWindow();
        page.setDefaultTimeout(15000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => { const view = document.querySelector('webview'); try { return view && !view.isLoading() && view.getURL().endsWith('/assets/new-tab.html'); } catch { return false; } });
        await page.evaluate(async () => { const {refreshNewTabs} = await import('./modules/newTab.js'); await refreshNewTabs(); });
        assert.match(await page.evaluate(() => document.querySelector('webview').executeJavaScript('document.getElementById("memory").textContent')), /^\d+ MB$/);
        assert.equal(await page.evaluate(() => getComputedStyle(document.body).getPropertyValue('--bg-main').trim()), '#121211');
        assert(await page.evaluate(() => document.fonts.check('16px "IBM Plex Sans"')));
        assert(await page.evaluate(() => document.fonts.check('16px "Font Awesome 6 Free"')));
        assert.equal(await page.locator('link[href^="https:"]').count(), 0);
        await page.evaluate(async () => { if (!document.body.classList.contains('sidebar-pinned')) await window.toggleSidebarPin(); });
        await page.waitForFunction(() => Math.abs(document.getElementById('Sidebar').getBoundingClientRect().right - 260) < 1);
        await page.waitForTimeout(300);
        assert.equal(await page.locator('#Sidebar .fly-brand').count(), 0);
        assert.equal(await page.locator('#WindowControls img').count(), 0);
        await page.evaluate(() => window.executeBrowserAction('toggle-window-controls', true));
        assert.equal(await page.locator('#WindowControls .win-btn').count(), 3);
        assert.equal(await page.locator('#WindowControls .window-controls-brand').count(), 0);
        await page.evaluate(() => window.executeBrowserAction('toggle-window-controls', false));
        await page.evaluate(async () => { await window.toggleSidebarPin(); document.activeElement?.blur(); });
        await page.mouse.move(700, 500);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().right <= 37);
        await page.evaluate(() => window.toggleSidebarPin());
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().right >= 259);
        await page.screenshot({path:'/tmp/fly-identity-chasis.png'});
        await page.evaluate(async () => {
            document.body.classList.remove('dark-mode');
            document.body.classList.add('light-mode');
            const {applyThemeVisuals} = await import('./modules/theme.js');
            applyThemeVisuals({});
        });
        await page.evaluate(async () => { const {refreshNewTabs} = await import('./modules/newTab.js'); await refreshNewTabs(); });
        assert(await page.evaluate(() => document.querySelector('webview').executeJavaScript('document.body.classList.contains("light")')));
        assert(await page.evaluate(() => document.querySelector('webview').executeJavaScript('document.documentElement.scrollWidth <= innerWidth')));
        await page.screenshot({path:'/tmp/fly-identity-papel.png'});
        fs.writeFileSync('/tmp/fly-identity-guest.png', Buffer.from(await app.evaluate(async ({webContents}) => { const view = webContents.getAllWebContents().find(w => w.getType() === 'webview'); return (await view.capturePage()).toPNG().toString('base64'); }), 'base64'));
        // Searches use the browser address bar; the local page has no duplicate search.
        assert.equal(await page.evaluate(() => document.querySelector('webview').executeJavaScript('document.querySelectorAll("form, input").length')), 0);
        await page.evaluate(async fixture => window.flyAPI.updateBrowserSettings({search_engine:fixture+'/?q=%s'}), fixture);
        await page.keyboard.press('Control+l');
        await page.locator('#WideAddressBar').fill('fly identity');
        await page.keyboard.press('Enter');
        await page.waitForFunction(fixture => document.querySelector('webview').getURL() === fixture+'/?q=fly%20identity', fixture);
        await page.waitForFunction(() => document.querySelector('.tab-domain')?.textContent === '127.0.0.1');
        const session = await page.evaluate(() => window.flyAPI.getSession());
        assert.equal(session.workspaces[session.current_workspace][0], fixture+'/?q=fly%20identity');
        // Custom colours survive, while the other theme retains its own defaults.
        assert.equal(await page.evaluate(async () => {
            const {getActiveThemeColors} = await import('./modules/theme.js');
            return getActiveThemeColors({theme_colors:{dark:{accent:'#abcdef'}}}, 'dark').accent;
        }), '#abcdef');
        assert.deepEqual(errors, []);
        console.log('Fly identity: both themes, bundled fonts/icons, local new tab, configured search, session and custom colours pass.');
    } finally {
        if (app) await app.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive:true, force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
