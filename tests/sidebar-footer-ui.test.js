const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-overlay-state-'));
    const entry = path.join(temporary, 'start.cjs');
    fs.writeFileSync(entry, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))}); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Overlay fixture</title><input id="editable" style="margin-left:260px"><button>Hint target</button>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
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
        await page.waitForFunction(() => document.body.classList.contains('sidebar-pinned'));
        const pinned=await rect(); assert.equal(pinned.x,collapsed.x); assert.equal(pinned.y,collapsed.y);
        await page.evaluate(() => {const b=document.getElementById('update-indicator-btn');b.style.display='inline-flex';});
        assert.deepEqual(await rect(),pinned);
        console.log('Sidebar footer: six equally spaced controls, stable pin, settings keyboard activation and update indicator passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive:true,force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
