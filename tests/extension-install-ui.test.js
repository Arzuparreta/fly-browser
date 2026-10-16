const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const http = require('node:http');
const Zip = require('adm-zip');
const createElectronFixture = require('./electron-fixture');
const { extensionId } = require('../extensionPackages');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-extension-install-ui-'));
    const entry = createElectronFixture(root, temporary);
    if (process.env.FLY_TEST_PACKAGED === '1') {
        const bootstrap = path.join(temporary, 'main.js');
        const archive = path.join(root, 'dist/linux-unpacked/resources/app.asar');
        fs.writeFileSync(bootstrap, fs.readFileSync(bootstrap, 'utf8')
            .replace(JSON.stringify(root), JSON.stringify(archive))
            .replace(JSON.stringify(path.join(root, 'main.js')), JSON.stringify(path.join(archive, 'main.js'))));
    }
    const configDir = path.join(temporary, '.config/fly-browser');
    fs.mkdirSync(configDir, { recursive: true });
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({ extensions_enabled: false,
        extensions_by_workspace: { Work: [], Other: [] }, extensions_bundled: { 'ublock-origin': { removed: true } } }));
    const server = http.createServer((req, res) => res.end('<!doctype html><title>Install fixture</title><p>Extension test page</p>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({ current_workspace: 'Work', workspaces: { Work: [url], Other: [url] } }));
    const local = path.join(temporary, 'local'); fs.mkdirSync(local);
    const manifest = version => ({ manifest_version: 2, name: 'Install fixture', version,
        permissions: ['storage'], content_scripts: [{ matches: ['http://127.0.0.1/*'], js: ['content.js'] }],
        background: { scripts: ['background.js'], persistent: true }, browser_action: { default_popup: 'popup.html' }, options_page: 'options.html' });
    const write = version => {
        fs.writeFileSync(path.join(local, 'manifest.json'), JSON.stringify(manifest(version)));
        fs.writeFileSync(path.join(local, 'content.js'), `document.documentElement.dataset.flyInstalled = '${version}';`);
        fs.writeFileSync(path.join(local, 'background.js'), 'chrome.storage.local.get("kept", x => { if (!x.kept) chrome.storage.local.set({kept: "preserved"}); });');
        fs.writeFileSync(path.join(local, 'popup.html'), '<!doctype html><p>Installed popup</p>');
        fs.writeFileSync(path.join(local, 'options.html'), '<!doctype html><title>Installed options</title><p>Options</p>');
    };
    write('1.0');
    const archive = new Zip();
    archive.addLocalFolder(local, 'wrapper');
    const zipPath = path.join(temporary, 'fixture.zip'); archive.writeZip(zipPath);
    const badPath = path.join(temporary, 'bad.zip'); fs.writeFileSync(badPath, 'not a ZIP');
    const keys = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
    const key = keys.publicKey.export({ type: 'spki', format: 'der' });
    const storeId = extensionId(key);
    const storeZip = new Zip(); storeZip.addLocalFolder(local);
    const payload = storeZip.toBuffer();
    const signature = crypto.sign('sha1', payload, keys.privateKey);
    const header = Buffer.alloc(16); header.write('Cr24'); header.writeUInt32LE(2, 4); header.writeUInt32LE(key.length, 8); header.writeUInt32LE(signature.length, 12);
    const crxBytes = Buffer.concat([header, key, signature, payload]);
    const crxPath = path.join(temporary, 'fixture.crx'); fs.writeFileSync(crxPath, crxBytes);
    let application;
    const pageErrors = [];
    const launch = async () => {
        application = await _electron.launch({ executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry] });
        const page = await application.firstWindow(); page.setDefaultTimeout(20000);
        page.on('pageerror', error => pageErrors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        await application.evaluate(({ dialog }) => {
            globalThis.installDialogs = [];
            globalThis.installAccept = true;
            dialog.showMessageBox = async (_win, details) => { globalThis.installDialogs.push(details); return { response: globalThis.installAccept ? 1 : 0 }; };
            dialog.showOpenDialog = async () => ({ canceled: false, filePaths: [globalThis.installPath] });
        });
        return page;
    };
    const preferences = async page => { await page.evaluate(() => window.openPreferencesPane('extensions')); await page.locator('#setting-extensions-package-btn').waitFor({ state: 'visible' }); };
    const state = page => page.evaluate(() => window.flyAPI.extensionsGetState({ scan: true }));
    const install = async (page, kind, filename) => {
        await application.evaluate((_electron, filename) => { globalThis.installPath = filename; }, filename);
        await page.locator(`#setting-extensions-${kind}-btn`).click();
        await page.waitForFunction(() => !document.getElementById('setting-extensions-package-btn').disabled);
        return page.locator('#ExtensionsInstallStatus').textContent();
    };
    const reloadAndCheck = async (page, version) => {
        await page.evaluate(() => document.querySelector('#webview-container webview').reload());
        await page.waitForFunction(version => document.querySelector('#webview-container webview').executeJavaScript(`document.documentElement.dataset.flyInstalled === ${JSON.stringify(version)}`), version);
    };
    try {
        let page = await launch();
        await preferences(page);
        await application.evaluate(() => { globalThis.installAccept = false; });
        assert.match(await install(page, 'folder', local), /cancelled/);
        assert.equal((await state(page)).installed.length, 0);
        await application.evaluate(() => { globalThis.installAccept = true; });
        assert.match(await install(page, 'folder', local), /installed/);
        let current = await state(page);
        const folder = current.installed[0].folder, id = current.installed[0].id;
        assert.equal(current.enabled, false, 'Installing must preserve global disable');
        assert.deepEqual(current.byWorkspace, { Work: [folder], Other: [] });
        assert(current.runtime.workspaces.every(ws => ws.loaded.length === 0));
        const detail = await application.evaluate(() => globalThis.installDialogs.at(-1).detail);
        assert.match(detail, /storage/); assert.match(detail, /127.0.0.1/);
        await page.locator('label:has(#setting-extensions-toggle)').click();
        await reloadAndCheck(page, '1.0');
        write('2.0');
        await preferences(page);
        assert.match(await install(page, 'folder', local), /installed/);
        current = await state(page);
        assert.equal(current.installed[0].id, id);
        assert.deepEqual(current.byWorkspace, { Work: [folder], Other: [] });
        await reloadAndCheck(page, '2.0');
        assert.equal(await application.evaluate(({ webContents }, id) => webContents.getAllWebContents().find(wc => wc.getType() === 'backgroundPage' && wc.getURL().includes(id))
            .executeJavaScript('new Promise(resolve => chrome.storage.local.get("kept", value => resolve(value.kept)))'), id), 'preserved');
        await preferences(page);
        // A valid manifest rejected by this Chromium version restores previous code.
        const broken = { ...manifest('3.0'), minimum_chrome_version: '9999.0' };
        fs.writeFileSync(path.join(local, 'manifest.json'), JSON.stringify(broken));
        const failure = await install(page, 'folder', local);
        assert(!failure.includes('installed.'), 'Incompatible replacement must fail loading');
        assert.equal((await state(page)).installed.find(item => item.folder === folder).version, '2.0');
        write('4.0');
        await install(page, 'folder', local);
        const before = (await state(page)).installed.length;
        assert(!/installed\./.test(await install(page, 'package', badPath)));
        assert.equal((await state(page)).installed.length, before);
        assert.match(await install(page, 'package', zipPath), /installed/);
        assert.equal((await state(page)).installed.length, before + 1);
        assert.match(await install(page, 'package', crxPath), /installed/);
        assert((await state(page)).installed.some(item => item.id === storeId), 'CRX keeps its signed identity');
        // Remove the manual signed copy before exercising store installation of that identity.
        const signedFolder = (await state(page)).installed.find(item => item.id === storeId).folder;
        await page.evaluate(folder => window.flyAPI.extensionsRemove(folder), signedFolder);

        // Use a store-origin fixture for deterministic API, actual signed download,
        // source tracking and store management tests. Optional live mode below uses Google's page and package.
        const partition = JSON.parse(fs.readFileSync(path.join(configDir, 'session.json'))).workspace_partitions.Work;
        await application.evaluate(({ session, net }, { partition, storeId, bytes }) => {
            const ses = session.fromPartition(partition);
            globalThis.realFetch = globalThis.fetch;
            globalThis.fetch = async (url, options) => String(url).startsWith('https://clients2.google.com/service/update2/crx')
                ? new Response(Buffer.from(bytes, 'base64')) : globalThis.realFetch(url, options);
            ses.protocol.handle('https', request => request.url.startsWith('https://chromewebstore.google.com/')
                ? new Response(`<!doctype html><title>Store fixture</title><button id="install">Add to Fly</button><p id="result"></p><script>document.getElementById('install').onclick=async()=>{const result=await chrome.webstorePrivate.beginInstallWithManifest3({id:'${storeId}'});document.getElementById('result').textContent=result === '' ? await chrome.webstorePrivate.completeInstall('${storeId}') : result;};</script>`, { headers: { 'content-type': 'text/html' } })
                : net.fetch(request, { bypassCustomProtocolHandlers: true }));
        }, { partition, storeId, bytes: crxBytes.toString('base64') });
        await page.locator('#setting-extensions-store-btn').click();
        await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].some(view => view.getURL().startsWith('https://chromewebstore.google.com/') && !view.isLoading()));
        const inStore = script => page.evaluate(script => [...document.querySelectorAll('#webview-container webview')].find(view => view.getURL().startsWith('https://chromewebstore.google.com/')).executeJavaScript(script), script);
        assert.equal(await inStore('typeof chrome.webstorePrivate.beginInstallWithManifest3'), 'function');
        await inStore('document.getElementById("install").click(); void 0;');
        await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].find(view => view.getURL().startsWith('https://chromewebstore.google.com/')).executeJavaScript('document.getElementById("result").textContent === "success"'));
        const stored = (await state(page)).installed.find(item => item.id === storeId);
        assert.deepEqual(stored.source, { type: 'store', id: storeId });
        assert.equal(await inStore(`chrome.webstorePrivate.getExtensionStatus('${storeId}', '{}')`), 'enabled');
        await inStore(`chrome.management.setEnabled('${storeId}', false)`);
        assert.equal(await inStore(`chrome.webstorePrivate.getExtensionStatus('${storeId}', '{}')`), 'disabled');
        await preferences(page);
        await page.locator(`.ext-update-btn[data-folder="${stored.folder}"]`).click();
        await page.waitForFunction(() => document.getElementById('ExtensionsInstallStatus').textContent.includes('up to date'));
        assert.equal((await state(page)).installed.find(item => item.id === storeId).enabled, false);
        assert.equal(await inStore(`chrome.management.getAll().then(items => items.find(item => item.id === '${storeId}').enabled)`), false);
        await inStore(`new Promise(resolve => chrome.management.uninstall('${storeId}', {}, resolve))`);
        assert(!(await state(page)).installed.some(item => item.id === storeId));
        assert.equal((await page.evaluate(() => window.flyAPI.getBrowserSettings())).extensions_sources[stored.folder], undefined);
        assert.equal(await inStore(`chrome.webstorePrivate.beginInstallWithManifest3({id:'${storeId}'})`), '');
        await inStore(`chrome.management.setEnabled('${storeId}', false)`);

        if (process.env.FLY_TEST_LIVE_STORE === '1') {
            await application.evaluate(({ session }, partition) => {
                session.fromPartition(partition).protocol.unhandle('https');
                globalThis.fetch = globalThis.realFetch;
            }, partition);
            await page.locator('#setting-extensions-store-btn').click();
            const darkReader = 'https://chromewebstore.google.com/detail/dark-reader/eimadpbcbfnmbkopoojfekhnkhdbieeh';
            await page.evaluate(url => {
                const views = [...document.querySelectorAll('#webview-container webview')];
                views.at(-1).src = url;
            }, darkReader);
            await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].some(view => view.getURL().includes('/dark-reader/') && !view.isLoading()));
            const live = script => page.evaluate(script => [...document.querySelectorAll('#webview-container webview')].find(view => view.getURL().includes('/dark-reader/')).executeJavaScript(script), script);
            await page.waitForTimeout(3000);
            await live('[...document.querySelectorAll("button")].find(button => /^Reject all$/i.test(button.textContent.trim()))?.click(); void 0;');
            await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].find(view => view.getURL().includes('/dark-reader/'))?.executeJavaScript('[...document.querySelectorAll("button")].some(button => /Add to (Chrome|Fly)/i.test(button.textContent))'));
            console.log('Live Chrome Web Store buttons:', await live('[...document.querySelectorAll("button")].map(button => button.textContent)'));
            await live('[...document.querySelectorAll("button")].find(button => /Add to (Chrome|Fly)/i.test(button.textContent)).click(); void 0;');
            await page.waitForFunction(() => window.flyAPI.extensionsGetState({ scan: true }).then(state => state.installed.some(item => item.id === 'eimadpbcbfnmbkopoojfekhnkhdbieeh')), null, { timeout: 90000 });
            await page.waitForFunction(() => window.flyAPI.extensionsGetState().then(state => state.runtime.workspaces.some(ws => ws.workspace === 'Work' && ws.loaded.some(item => item.id === 'eimadpbcbfnmbkopoojfekhnkhdbieeh'))));
            await page.waitForFunction(() => [...document.querySelectorAll('#webview-container webview')].find(view => view.getURL().includes('/dark-reader/'))?.executeJavaScript('[...document.querySelectorAll("button")].some(button => /Remove from (Chrome|Fly)/i.test(button.textContent))'));
            console.log('Live Google page → actual store button → signed Dark Reader download → loaded in Fly passed.');
        }
        await application.close(); application = null;
        page = await launch();
        const restarted = await state(page);
        assert.equal(restarted.installed.find(item => item.id === id).version, '4.0');
        assert.equal(restarted.installed.find(item => item.id === storeId).enabled, false);
        assert.deepEqual(pageErrors, []);
        console.log('Installation UI: permission cancellation, folder/ZIP/CRX, global disable, workspace scoping, updates/storage/identity, bad package, store bridge and management, disabled updates and restart passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
