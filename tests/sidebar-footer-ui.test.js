const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-overlay-state-'));
    const entry = createElectronFixture(root, temporary);

    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Overlay fixture</title><input id="editable" style="margin-left:260px"><button>Hint target</button>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        page.setDefaultNavigationTimeout(10000);
        await page.bringToFront();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {return [...document.querySelectorAll('webview')].find(view => view.getClientRects().length)?.getWebContentsId() > 0;} catch {return false;}});
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `http://127.0.0.1:${server.address().port}`);

        await page.mouse.move(500, 400);
        await page.evaluate(() => document.activeElement.blur());
        await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 36);
        const rect = () => page.locator('#pin-sidebar-btn').boundingBox();
        const collapsed = await rect();
        await page.locator('#pin-sidebar-btn').focus();
        await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 260);
        const expanded = await rect();
        assert.equal(collapsed.x, expanded.x); assert.equal(collapsed.y, expanded.y);
        const centres = await page.locator('.bottom-theme-layout button').evaluateAll(buttons => buttons.map(button => {const r=button.getBoundingClientRect();return r.x+r.width/2;}));
        assert.equal(centres.length, 6);
        assert(Math.abs((centres[0]+centres[5])/2 - 129.5) < 1);
        for(let i=2;i<6;i++) assert(Math.abs((centres[i]-centres[i-1])-(centres[1]-centres[0])) < 0.1);
        await page.locator('#noti-toggle-btn').focus(); await page.keyboard.press('ArrowRight');
        assert.equal(await page.evaluate(() => document.activeElement.id), 'settings-btn');
        await page.keyboard.press('Enter'); await page.locator('#PreferencesOverlay').waitFor({state:'visible'});
        await page.keyboard.press('Escape');
        await page.locator('#pin-sidebar-btn').click();
        await page.waitForFunction(() => document.body.classList.contains('sidebar-pinned') && document.getElementById('Sidebar').offsetWidth === 260);
        const pinned=await rect(); assert.equal(pinned.x,collapsed.x); assert.equal(pinned.y,collapsed.y);
        const fullClock = await page.locator('#SidebarClock').boundingBox();
        await page.evaluate(async () => {
            const {displayUpdateNotification} = await import('./renderer.js');
            displayUpdateNotification({updateAvailable: true, currentVersion: '0.15.0', latestVersion: '99.0.0', releaseUrl: 'https://example.invalid/release'});
        });
        assert.deepEqual(await rect(),pinned);
        const inlineUpdate = async () => {
            const {clock, update, time} = await page.evaluate(() => {
                const rect = id => document.getElementById(id).getBoundingClientRect().toJSON();
                return {clock: rect('SidebarClock'), update: rect('update-indicator-btn'), time: rect('SidebarClockTime')};
            });
            assert(clock.width < fullClock.width, 'The clock yields width to the update action');
            assert(clock.x + clock.width < update.x, 'Clock and update must not overlap');
            assert(Math.abs(clock.y + clock.height / 2 - update.y - update.height / 2) < 0.1, 'Both badges share a vertical centre');
            assert(time.x >= clock.x && time.x + time.width <= clock.x + clock.width, `Time stays inside its badge: ${JSON.stringify({clock, time, update})}`);
        };
        for (let i = 0; i < 3; i++) {
            await inlineUpdate();
            await page.locator('#SidebarClock').click();
        }
        await page.screenshot({path: '/tmp/fly-inline-update.png'});
        await page.evaluate(async () => {const {setClockVisibility} = await import('./modules/clock.js'); setClockVisibility(false);});
        assert(await page.locator('#update-indicator-btn').isVisible(), 'Disabling the clock leaves the update action available');
        await page.evaluate(async () => {const {setClockVisibility} = await import('./modules/clock.js'); setClockVisibility(true);});
        await inlineUpdate();
        await page.locator('#update-indicator-btn').focus();
        await page.keyboard.press('Enter');
        await page.locator('#PreferencesOverlay').waitFor({state: 'visible'});
        assert.equal(await page.locator('#SettingsTab-about').getAttribute('aria-selected'), 'true');
        await page.keyboard.press('Escape');
        await page.locator('#pin-sidebar-btn').click();
        await page.mouse.move(500, 400);
        await page.evaluate(() => document.activeElement.blur());
        await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 36);
        assert(!(await page.locator('#update-indicator-btn').isVisible()));
        await page.locator('#pin-sidebar-btn').focus();
        await page.waitForFunction(() => document.getElementById('Sidebar').offsetWidth === 260);
        await inlineUpdate();
        await page.evaluate(() => {document.getElementById('update-indicator-btn').style.display = 'none';});
        assert(Math.abs((await page.locator('#SidebarClock').boundingBox()).width - fullClock.width) < 1, 'Without an update the clock regains the full row');
        console.log('Sidebar footer: six equally spaced controls, stable pin, settings keyboard activation and update indicator passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive:true,force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
