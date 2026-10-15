const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-empty-search-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => {
        response.setHeader('Content-Type', 'text/html');
        response.end('<!doctype html><title>Search results</title><input id="search"><a href="/result">Result</a>');
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
        await page.evaluate(url => window.flyAPI.updateBrowserSettings({search_engine: url + '/?q=%s'}), fixture);
        for (const [input, expected] of [['empty workspace search', fixture + '/?q=empty%20workspace%20search'], [fixture + '/direct', fixture + '/direct']]) {
            await page.evaluate(() => {
                const count = document.querySelectorAll('#TabList li').length;
                for (let index = 0; index < count; index++) window.executeBrowserAction('remove-tab');
            });
            assert.equal(await page.locator('#TabList li').count(), 0);
            await page.locator('#split-toggle-btn').focus();
            await page.keyboard.press('Control+l');
            await page.locator('#WideAddressBar').fill(input);
            await page.locator('#WideAddressBar').press('Enter');
            await page.waitForFunction(expected => {
                const view = document.querySelector('webview');
                // A webview throws until dom-ready, which a fresh tab may not have reached yet.
                try { return view && !view.isLoading() && view.getURL() === expected; } catch (e) { return false; }
            }, expected);
            assert.equal(await page.locator('#TabList li').count(), 1);
            await page.locator('#WideAddressBar').waitFor({state: 'hidden'});
            await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
            const session = await page.evaluate(() => window.flyAPI.getSession());
            assert.deepEqual(session.workspaces[session.current_workspace], [expected]);
        }
        assert.deepEqual(errors, []);
        console.log('Empty workspace address search and direct navigation create one focused, saved tab.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
