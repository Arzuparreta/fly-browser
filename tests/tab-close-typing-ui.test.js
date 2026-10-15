const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const createElectronFixture = require('./electron-fixture');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
// Closing a tab with a page key and clicking straight into the next page's field must not lose typing.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-close-typing-'));
    const entry = createElectronFixture(root, temporary);

    const server = http.createServer((_request, response) => response.end('<!doctype html><input id="text" style="margin-left:260px"><news-text tabindex="0" id="noneditable">Article</news-text><div style="height:2000px"></div>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const fixture = `http://127.0.0.1:${server.address().port}`;
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        const guest = script => page.evaluate(script => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript(script), script);
        // waitForFunction does not await async predicates, so guest state is polled from here.
        const until = async (check, message) => {
            for (let attempt = 0; attempt < 250; attempt++) { if (await check().catch(() => false)) return; await page.waitForTimeout(20); }
            throw new Error(message);
        };
        const rounds = 5;
        for (let round = 0; round < rounds; round++) {
            for (let count = 0; count < 2; count++) {
                await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), fixture);
                await page.waitForTimeout(100);
                await until(() => page.evaluate(() => {
                    const view = [...document.querySelectorAll('webview')].find(view => view.getClientRects().length);
                    return !view.isLoading() && view.executeJavaScript('!!document.getElementById("text")');
                }), 'Fixture tab did not load');
            }
            const tabs = await page.locator('#TabList li').count();
            await page.locator('webview:visible').click({position: {x: 350, y: 300}});
            await guest('document.getElementById("noneditable").focus()');
            // The key removes the page that received it, so Chromium can drop the acknowledgement
            // Playwright awaits; the closed tab below is what proves the key arrived.
            page.keyboard.press('x').catch(() => {});
            await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count - 1, tabs);
            await until(() => guest('!!document.getElementById("text")'), 'Next tab is not ready');
            // No settling delay: the click races the browser's deferred focus of the new tab.
            await page.locator('webview:visible').click({position: {x: 280, y: 20}});
            await page.keyboard.press('a');
            await page.waitForTimeout(150);
            await page.keyboard.press('b');
            await until(async () => await guest('document.getElementById("text").value') === 'ab', `Round ${round + 1}: typing after closing a tab must reach the page field`);
            assert.equal(await page.locator('#TabList li').count(), tabs - 1, 'Typing must not run page bindings');
            await guest('document.getElementById("text").value = ""');
        }
        assert.deepEqual(errors, []);
        console.log(`Typing straight after closing a tab reached the page in ${rounds} of ${rounds} rounds.`);
    } finally {
        if (application) await application.close();
        server.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
