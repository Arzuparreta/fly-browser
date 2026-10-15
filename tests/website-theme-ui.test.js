const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// Websites with their own dark mode follow the interface live; pages without one are left alone.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-website-theme-'));
    const entry = createElectronFixture(root, temporary);
    const configPath = path.join(temporary, '.config/fly-browser/config.json');
    const pages = {
        '/aware': '<!doctype html><meta name="color-scheme" content="light dark"><style>body{background:#fff}@media (prefers-color-scheme: dark){body{background:#000}}</style><title>Aware</title>',
        '/plain': '<!doctype html><title>Plain</title><p>No dark mode here</p>'
    };
    const server = http.createServer((request, response) => response.end(pages[request.url] || ''));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}`;
    let application;
    const launch = async () => {
        application = await _electron.launch({
            executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry],
            // Playwright otherwise emulates a light colour scheme on every page it attaches to.
            colorScheme: 'no-override'
        });
        const page = await application.firstWindow();
        page.setDefaultTimeout(15000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    const open = async (page, url) => {
        await application.evaluate(({ BrowserWindow }, url) => BrowserWindow.getAllWindows()[0].webContents.send('master-shortcut', 'spawn-tab-with-url', url), url);
        await page.waitForFunction(url => [...document.querySelectorAll('webview')].some(v => { try { return v.getURL() === url && !v.isLoading(); } catch { return false; } }), url);
    };
    const inPage = (page, url, script) => page.evaluate(([url, script]) => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).executeJavaScript(script), [url, script]);
    const scheme = (page, url) => inPage(page, url, "matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'");
    const background = (page, url) => inPage(page, url, 'getComputedStyle(document.body).backgroundColor');
    const waitScheme = (page, url, expected) => page.waitForFunction(([url, expected]) => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).executeJavaScript("matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light'").then(value => value === expected), [url, expected]);
    const aware = `${base}/aware`, plain = `${base}/plain`;
    try {
        let page = await launch();
        await open(page, aware);
        await open(page, plain);
        const plainCanvas = await inPage(page, plain, "getComputedStyle(document.documentElement).colorScheme + ' ' + getComputedStyle(document.body).color");
        assert.equal(await scheme(page, aware), 'dark', 'Websites start with the dark interface');
        assert.equal(await background(page, aware), 'rgb(0, 0, 0)');
        await inPage(page, aware, "window.reloaded = false; true");

        await page.evaluate(() => window.toggleInterfaceTheme());
        await waitScheme(page, aware, 'light');
        assert.equal(await background(page, aware), 'rgb(255, 255, 255)');
        assert.equal(await inPage(page, aware, 'window.reloaded'), false, 'The page updates in place, without a reload');
        assert.equal(await inPage(page, plain, "getComputedStyle(document.documentElement).colorScheme + ' ' + getComputedStyle(document.body).color"), plainCanvas, 'Pages without a dark mode are untouched');

        await page.evaluate(() => window.toggleInterfaceTheme());
        await waitScheme(page, aware, 'dark');
        assert.equal(await inPage(page, plain, "getComputedStyle(document.documentElement).colorScheme + ' ' + getComputedStyle(document.body).color"), plainCanvas, 'Pages without a dark mode stay untouched in dark');

        await page.evaluate(() => window.executeBrowserAction('toggle-help'));
        await page.locator('#SettingsTab-appearance').click();
        const pressed = () => page.evaluate(() => [...document.querySelectorAll('[data-theme-choice]')].map(b => `${b.textContent}:${b.getAttribute('aria-pressed')}`).join(' '));
        assert.equal(await pressed(), 'Light:false Dark:true', 'The theme buttons show the current theme');
        await page.locator('[data-theme-choice="light"]').click();
        await waitScheme(page, aware, 'light');
        assert.equal(await pressed(), 'Light:true Dark:false', 'Choosing Light deselects Dark');
        await page.locator('[data-theme-choice="light"]').click();
        assert.equal(await page.evaluate(() => document.body.classList.contains('light-mode')), true, 'Choosing the current theme again keeps it');
        await page.locator('[data-theme-choice="dark"]').click();
        await waitScheme(page, aware, 'dark');
        assert.equal(await pressed(), 'Light:false Dark:true', 'Choosing Dark deselects Light');
        const setting = page.locator('#setting-website-theme-toggle');
        assert.equal(await setting.isChecked(), true, 'Matching is on by default');
        await page.locator('label.toggle-switch:has(#setting-website-theme-toggle)').click();
        await waitScheme(page, aware, 'light');
        await page.waitForFunction(() => window.flyAPI.getBrowserSettings().then(cfg => cfg.sync_website_theme === false));
        await page.evaluate(() => window.toggleInterfaceTheme());
        await page.evaluate(() => window.toggleInterfaceTheme());
        assert.equal(await scheme(page, aware), 'light', 'Interface changes no longer reach websites');
        assert.equal(await page.locator('#theme-toggle-btn').count(), 0, 'The separate website theme button is gone');

        await application.close(); application = null;
        assert.equal(JSON.parse(fs.readFileSync(configPath)).sync_website_theme, false);
        page = await launch();
        await open(page, aware);
        assert.equal(await scheme(page, aware), 'light', 'An unsynced profile restarts with the light preference');
        await page.evaluate(() => window.setWebsiteThemeSync(true));
        await waitScheme(page, aware, 'dark');
        console.log('Websites with a dark mode follow the interface live; others are never altered.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
