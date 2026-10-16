const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-extensions-ui-'));
    const entry = createElectronFixture(root, temporary);
    const configDir = path.join(temporary, '.config', 'fly-browser');
    fs.mkdirSync(configDir, { recursive: true });
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({extensions_enabled: false, interface_scale: process.env.FLY_POPUP_TEST_SCALE || 'normal'}));
    let blockedHits = 0;
    const server = http.createServer((req, res) => {
        if (req.url === '/document') {
            res.setHeader('Content-Type', 'application/pdf');
            res.end('%PDF-1.1\n1 0 obj<</Type/Catalog>>endobj\ntrailer<</Root 1 0 R>>\n%%EOF');
        } else if (req.url.startsWith('/fly-block-me.js')) {
            blockedHits++;
            res.setHeader('Content-Type', 'text/javascript');
            res.end('window.unblocked = true;');
        } else {
            res.end('<!doctype html><title>Extension fixture</title><input id="text"><script src="/fly-block-me.js"></script><div id="fly-ad">Ad fixture</div>');
        }
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({ current_workspace: 'Work', workspaces: { Work: [url] } }));
    let application;
    const pageErrors = [];
    const launch = async () => {
        application = await _electron.launch({ executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry] });
        const page = await application.firstWindow();
        page.on('pageerror', error => pageErrors.push(error.message));
        page.on('console', message => { if(message.type() === 'error') console.error('CONSOLE', message.text()); });
        page.setDefaultTimeout(15000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    const preferences = async page => {
        await page.evaluate(() => window.openPreferencesPane('extensions'));
        await page.locator('[data-extension-folder="ublock-origin"]').waitFor();
    };
    const extensionState = page => page.evaluate(() => window.flyAPI.extensionsGetState({ scan: true }));
    const backgroundScript = async code => application.evaluate(({ webContents }, script) => {
        const background = webContents.getAllWebContents().find(wc => wc.getType() === 'backgroundPage' && wc.getURL().startsWith('chrome-extension://'));
        if (!background) throw new Error('uBlock background page missing');
        return Promise.race([background.executeJavaScript(script), new Promise((_, reject) => setTimeout(() => reject(new Error('Background script timed out')), 5000))]);
    }, code);
    const reloadPage = page => page.evaluate(() => document.querySelector('#webview-container webview').reload());
    const waitUntil = async predicate => {
        const end = Date.now() + 45000;
        while (Date.now() < end) {
            if (await predicate()) return;
            await new Promise(resolve => setTimeout(resolve, 100));
        }
        console.error('Extension state at timeout:', JSON.stringify((await application.firstWindow().then(extensionState)).runtime));
        throw new Error('Timed out waiting for extension state');
    };
    try {
        let page = await launch();
        assert.equal(await page.locator('#dark-reader-btn').count(), 0);
        assert.equal(await application.evaluate(({ webContents }) => webContents.getAllWebContents().filter(wc => wc.getType() === 'backgroundPage').length), 0);
        assert(!fs.existsSync(path.join(configDir, 'extensions')), 'Disabled startup must not scan or copy bundles');
        await preferences(page);
        let state = await extensionState(page);
        assert.equal(state.enabled, false);
        assert.deepEqual(state.installed.map(info => info.folder), ['ublock-origin']);
        assert.equal(state.installed[0].version, '1.75.0');
        const id = state.installed[0].id;
        await page.locator('label:has(#setting-extensions-toggle)').click();
        await waitUntil(async () => (await extensionState(page)).runtime.workspaces.some(ws => ws.loaded.some(ext => ext.id === id)));
        await waitUntil(async () => { try { return await backgroundScript("import('./js/background.js').then(({default: u}) => !!u.readyToFilter)"); } catch { return false; } });
        console.log('uBlock loaded and ready');
        await backgroundScript("import('./js/background.js').then(({default: u}) => u.appendUserFilters('/fly-block-me.js$script\\n127.0.0.1###fly-ad'))");
        await page.keyboard.press('Escape');
        blockedHits = 0;
        await reloadPage(page);
        await waitUntil(async () => page.evaluate(() => document.querySelector('#webview-container webview').executeJavaScript("document.title === 'Extension fixture' && getComputedStyle(document.getElementById('fly-ad')).display === 'none'")));
        assert.equal(blockedHits, 0, 'uBlock must block a rule absent from the native blocker');
        assert.equal(await page.evaluate(() => document.querySelector('#webview-container webview').executeJavaScript('!!window.unblocked')), false);
        assert.equal(await page.evaluate(() => window.flyAPI.extensionsEnsure('Work', 'FlyPrivateProfile')).then(result => result.active), false);
        await waitUntil(async () => page.locator('#StatusBarExtensions button').count());
        await page.locator('#StatusBarExtensions button').click();
        await page.locator('#ExtensionPopupOverlay').waitFor({ state: 'visible' });
        await page.waitForFunction(() => document.activeElement === document.querySelector('#ExtensionPopupOverlay webview'));
        await page.waitForFunction(() => {
            const popup = document.querySelector('.extension-popup');
            const guest = popup.querySelector('webview');
            const bounds = popup.getBoundingClientRect();
            const content = guest.getBoundingClientRect();
            return bounds.width >= 25 && bounds.height >= 25 &&
                bounds.width >= content.width && bounds.height >= content.height &&
                bounds.top >= 0 && bounds.right <= innerWidth &&
                document.elementFromPoint(content.x + 10, content.y + 10) === guest;
        });
        await waitUntil(async () => page.evaluate(() => document.querySelector('#ExtensionPopupOverlay webview')
            .executeJavaScript("document.body.innerText.length > 10")));
        // Replace the loaded popup with deterministic extension content. Root scroll
        // dimensions include the viewport, so the old polling approach cannot shrink
        // and adds body margins again on each tick.
        const setPopupContent = (width, height) => page.evaluate(([width, height]) =>
            document.querySelector('#ExtensionPopupOverlay webview').executeJavaScript(`
                document.querySelectorAll('style, link[rel="stylesheet"]').forEach(node => node.remove());
                document.body.style.cssText = 'margin: 8px; height: auto';
                document.body.innerHTML = '<div style="width: ${width}px; height: ${height}px; background: white"></div>';
            `), [width, height]);
        const expectPopupSize = async (width, height) => {
            const scale = process.env.FLY_POPUP_TEST_SCALE === 'large' ? 1.25 : 1;
            width /= scale;
            height /= scale;
            await page.waitForFunction(([width, height]) => {
                const rect = document.querySelector('#ExtensionPopupOverlay webview').getBoundingClientRect();
                return Math.abs(rect.width - width) < 1 && Math.abs(rect.height - height) < 1;
            }, [width, height]);
            // Cover all of the former delayed measurements and assert no creeping growth.
            await page.waitForTimeout(2000);
            const rect = await page.locator('#ExtensionPopupOverlay webview').boundingBox();
            assert(Math.abs(rect.width - width) < 1 && Math.abs(rect.height - height) < 1,
                `Popup drifted: ${JSON.stringify(rect)}; expected ${width} x ${height}`);
        };
        await setPopupContent(320, 180);
        await expectPopupSize(336, 196);
        await setPopupContent(480, 280);
        await expectPopupSize(496, 296);
        await setPopupContent(240, 100);
        await expectPopupSize(256, 116);
        await setPopupContent(80, 30);
        await expectPopupSize(96, 46);
        await setPopupContent(1100, 900);
        const limits = await page.evaluate(() => [Math.min(800, innerWidth - 26), Math.min(600, innerHeight - 80)]);
        const scale = process.env.FLY_POPUP_TEST_SCALE === 'large' ? 1.25 : 1;
        await expectPopupSize(limits[0] * scale, limits[1] * scale);
        await setPopupContent(320, 180);
        await expectPopupSize(336, 196);
        await page.evaluate(() => document.querySelector('#ExtensionPopupOverlay webview').sendInputEvent({ type: 'keyDown', keyCode: 'Escape' }));
        await page.locator('#ExtensionPopupOverlay').waitFor({ state: 'detached' });
        await backgroundScript("chrome.browserAction.setPopup({ popup: '' })");
        await page.locator('#StatusBarExtensions button').click();
        await waitUntil(async () => page.evaluate(() => [...document.querySelectorAll('#webview-container webview')]
            .some(guest => guest.getAttribute('src')?.includes('/dashboard.html'))));
        assert.equal(await page.locator('#ExtensionPopupOverlay').count(), 0, 'An action without a popup opens its options in a tab');
        await page.evaluate(target => document.querySelector('#webview-container webview').setAttribute('src', target + 'document'), url);
        await waitUntil(async () => page.evaluate(() => document.querySelector('#webview-container webview').getURL().includes('/assets/pdfjs/viewer.html')));
        await page.evaluate(target => document.querySelector('#webview-container webview').setAttribute('src', target), url);
        await preferences(page);
        await page.locator('.ext-enabled-toggle[data-folder="ublock-origin"]').uncheck();
        await waitUntil(async () => (await extensionState(page)).runtime.workspaces.every(ws => ws.loaded.length === 0));
        await page.keyboard.press('Escape');
        await reloadPage(page);
        await waitUntil(async () => blockedHits > 0);
        await application.close(); application = null;
        page = await launch();
        await preferences(page);
        assert.equal(await page.locator('.ext-enabled-toggle').isChecked(), false, 'Disabled state survives restart');
        await page.locator('.ext-enabled-toggle').check();
        await waitUntil(async () => (await extensionState(page)).runtime.workspaces.some(ws => ws.loaded.length));
        assert.equal((await extensionState(page)).installed[0].id, id);
        await waitUntil(async () => { try { return await backgroundScript("import('./js/background.js').then(({default: u}) => !!u.readyToFilter)"); } catch { return false; } });
        const savedFilters = await backgroundScript("import('./js/background.js').then(({default: u}) => u.loadUserFilters())");
        assert(savedFilters.content.includes('/fly-block-me.js$script'), 'uBlock settings survive unload and restart');
        await page.locator('.ext-remove-btn[data-folder="ublock-origin"]').click();
        await page.locator('[data-extension-folder="ublock-origin"]').waitFor({ state: 'detached' });
        assert(!fs.existsSync(path.join(configDir, 'extensions', 'ublock-origin')));
        await application.close(); application = null;
        page = await launch();
        const removed = await extensionState(page);
        assert.deepEqual(removed.installed, []);
        assert.equal(removed.bundled['ublock-origin'].removed, true, 'Removal survives restart and rescans');
        assert(removed.runtime.workspaces.every(ws => !ws.loaded.length));
        assert.deepEqual(pageErrors, [], 'Extension handover must not break native IPC in other sessions');
        console.log('Bundled uBlock blocks network and cosmetic rules; popup, disabled startup, private exclusion, persistent disable/remove and stable storage identity passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
