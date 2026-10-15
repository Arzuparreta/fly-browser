const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// The close button and middle click on a sidebar tab close that tab, keep the selection elsewhere and spare pinned tabs.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-close-button-'));
    const entry = createElectronFixture(root, temporary);
    const server = http.createServer((request, response) => {
        response.setHeader('Content-Type', 'text/html');
        response.end(`<!doctype html><title>${request.url}</title>`);
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
        await page.evaluate(() => {
            const count = document.querySelectorAll('#TabList li').length;
            for (let index = 0; index < count; index++) window.executeBrowserAction('remove-tab');
        });
        for (const name of ['one', 'two', 'three']) {
            await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `${fixture}/${name}`);
            await page.waitForFunction(count => document.querySelectorAll('#TabList li').length === count, ['one', 'two', 'three'].indexOf(name) + 1);
        }
        const urls = () => page.evaluate(() => window.flyAPI.getSession().then(s => s.workspaces[s.current_workspace]));
        const selectedIndex = () => page.evaluate(() => [...document.querySelectorAll('#TabList li')].findIndex(li => li.classList.contains('selected')));
        const tab = index => page.locator('#TabList li').nth(index);
        // Rows keep their full width under the collapsed strip, so the first hover
        // targets the favicon the strip shows; it is repeated once the row is revealed.
        const hover = async index => {
            await tab(index).hover({position: {x: 14, y: 20}});
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().right > 200);
            await page.mouse.move(0, 0);
            await tab(index).hover();
        };
        assert.equal(await selectedIndex(), 2);

        // Hidden until the tab is hovered, so unselected rows stay quiet
        await hover(0);
        assert.equal(await tab(0).locator('.tab-close-btn').isVisible(), true);
        assert.equal(await tab(1).locator('.tab-close-btn').isVisible(), false);
        assert.equal(await tab(2).locator('.tab-close-btn').isVisible(), true, 'The selected tab keeps its close button');

        // Closing a background tab leaves the selected one selected
        await tab(0).locator('.tab-close-btn').click();
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 2);
        assert.deepEqual(await urls(), [`${fixture}/two`, `${fixture}/three`]);
        assert.equal(await selectedIndex(), 1);

        // Closing the selected tab moves the selection to a remaining tab
        await hover(1);
        await tab(1).locator('.tab-close-btn').click();
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 1);
        assert.deepEqual(await urls(), [`${fixture}/two`]);
        assert.equal(await selectedIndex(), 0);

        // Middle click closes a tab without selecting it first
        await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `${fixture}/four`);
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 2);
        await hover(0);
        await tab(0).click({button: 'middle'});
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 1);
        assert.deepEqual(await urls(), [`${fixture}/four`]);
        assert.equal(await selectedIndex(), 0);

        // Pinned tabs cannot be closed, so they offer no close button and ignore middle click
        await page.evaluate(() => window.togglePinActiveTab());
        await page.waitForFunction(() => document.querySelector('#TabList li').classList.contains('tab-pinned'));
        assert.equal(await tab(0).locator('.tab-close-btn').count(), 0);
        await hover(0);
        await tab(0).click({button: 'middle'});
        await page.waitForTimeout(200);
        assert.equal(await page.locator('#TabList li').count(), 1);
        await page.evaluate(() => window.togglePinActiveTab());
        await page.waitForFunction(() => !!document.querySelector('#TabList li .tab-close-btn'));
        await hover(0);

        // Closing the last tab leaves the workspace empty
        await tab(0).locator('.tab-close-btn').click();
        await page.waitForFunction(() => document.querySelectorAll('#TabList li').length === 0);
        assert.deepEqual(errors, []);
        console.log('Tab close buttons and middle click close background, selected and last tabs; pinned tabs stay.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
