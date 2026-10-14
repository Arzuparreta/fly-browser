const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-legacy-keybinds-'));
    const entry = createElectronFixture(root, temporary);
    // An untouched profile from an upstream release that seeded its defaults into keybinds.json.
    const config = path.join(temporary, '.config', 'mise-browser');
    fs.mkdirSync(config, {recursive: true});
    fs.copyFileSync(path.join(__dirname, 'fixtures', 'upstream-keybinds-2f8ca5f.json'), path.join(config, 'keybinds.json'));
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Legacy fixture</title><p>page</p>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        for (let index = 0; index < 3; index++) {
            await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `http://127.0.0.1:${server.address().port}/${index}`);
            await page.waitForTimeout(150);
        }
        const state = () => page.evaluate(() => ({
            selected: [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected')),
            sidebarFocused: document.getElementById('Sidebar').matches(':focus-within'),
            badges: document.body.classList.contains('show-tab-badges')
        }));
        const count = await page.locator('#TabList li').count();
        const before = await state();
        // Exercise the host dispatcher; guest-routed chords share the same compiled table in the main process.
        await page.evaluate(() => document.activeElement?.blur());
        await page.keyboard.press('Control+Tab');
        await page.waitForFunction(previous => [...document.querySelectorAll('#TabList li')].findIndex(item => item.classList.contains('selected')) !== previous, before.selected, {timeout: 3000}).catch(() => {});
        assert.deepEqual(await state(), {selected: (before.selected + 1) % count, sidebarFocused: false, badges: false},
            'Ctrl+Tab cycles tabs instead of focusing the sidebar navigation');
        assert.deepEqual(errors, []);
        console.log('Seeded upstream bindings: Ctrl+Tab cycles tabs and leaves the sidebar collapsed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
