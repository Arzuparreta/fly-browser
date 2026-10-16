const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const Module = require('node:module');
const vm = require('node:vm');
const Zip = require('adm-zip');
const packages = require('../extensionPackages');
const extensions = require('../extensions');
const { initializeStore, registerStoreSession } = require('../extensionStore');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-store-test-'));
    const directory = path.join(temporary, 'extensions');
    const source = path.join(temporary, 'source'); fs.mkdirSync(source);
    const localManifest = { name: 'Store test', manifest_version: 3, version: '1.0', permissions: ['storage'] };
    fs.writeFileSync(path.join(source, 'manifest.json'), JSON.stringify(localManifest));
    const keys = crypto.generateKeyPairSync('rsa', { modulusLength: 2048 });
    const publicKey = keys.publicKey.export({ type: 'spki', format: 'der' });
    const id = packages.extensionId(publicKey);
    const signed = version => {
        const archive = new Zip(); archive.addFile('manifest.json', Buffer.from(JSON.stringify({ ...localManifest, version })));
        const payload = archive.toBuffer(); const signature = crypto.sign('sha1', payload, keys.privateKey);
        const header = Buffer.alloc(16); header.write('Cr24'); header.writeUInt32LE(2, 4);
        header.writeUInt32LE(publicKey.length, 8); header.writeUInt32LE(signature.length, 12);
        return Buffer.concat([header, publicKey, signature, payload]);
    };
    const handlers = new Map();
    const mainFrame = {}; const webContents = { mainFrame, send() {} };
    const win = { webContents, isDestroyed: () => false };
    const mainEvent = { sender: webContents, senderFrame: mainFrame };
    const workspaceSession = {}, otherSession = {};
    const frame = { origin: 'https://chromewebstore.google.com', isDestroyed: () => false, send() {} };
    const guest = { hostWebContents: webContents, mainFrame: frame, session: workspaceSession,
        getType: () => 'webview', isDestroyed: () => false, send() {} };
    const storeEvent = { sender: guest, senderFrame: frame };
    let cfg = { extensions_enabled: false, extensions_by_workspace: { Work: [], Other: [] }, unrelated: 'kept' };
    let accept = true, bytes = signed('1.0'), lastPrompt;
    const electron = {
        ipcMain: { handle: (channel, handler) => handlers.set(channel, handler) },
        app: { getGPUFeatureStatus: () => ({ webgl: 'enabled' }) },
        session: { fromPartition: partition => partition === 'persist:Work' ? workspaceSession : otherSession },
        dialog: { showMessageBox: async (_win, details) => { lastPrompt = details; return { response: accept ? 1 : 0 }; },
            showOpenDialog: async () => ({ canceled: false, filePaths: [source] }) }
    };
    const originalLoad = Module._load;
    const originalDownload = packages.downloadStorePackage;
    Module._load = function(name, ...args) { return name === 'electron' ? electron : originalLoad.call(this, name, ...args); };
    packages.downloadStorePackage = async () => bytes;
    const scan = () => fs.existsSync(directory) ? fs.readdirSync(directory, { withFileTypes: true })
        .filter(entry => entry.isDirectory() && !entry.name.startsWith('.'))
        .map(entry => ({ ...extensions.describeExtension(entry.name, path.join(directory, entry.name)),
            source: cfg.extensions_sources?.[entry.name], enabled: !(cfg.extensions_disabled || []).includes(entry.name) })) : [];
    let simulateFailure = false;
    try {
        let registrations = 0;
        const persistent = { isPersistent: () => true, registerPreloadScript: () => registrations++ };
        registerStoreSession(persistent); registerStoreSession(persistent);
        registerStoreSession({ isPersistent: () => false, registerPreloadScript: () => { throw new Error('Private preload registered'); } });
        assert.equal(registrations, 1);
        initializeStore({ directory, getConfig: () => cfg, saveConfig: patch => { cfg = { ...cfg, ...patch }; },
            getMainWindow: () => win, getWorkspaces: () => ({ Work: [], Other: [] }), getWorkspacePartition: name => `persist:${name}`,
            describe: extensions.describeExtension, scan, validFolder: extensions.isValidFolderName,
            unload: async () => {}, apply: async () => {}, ensure: async () => {},
            loadError: () => simulateFailure ? 'Replacement could not load' : null,
            remove: async folder => { fs.rmSync(path.join(directory, folder), { recursive: true }); return { removed: true }; } });
        const invoke = (channel, event, ...args) => handlers.get(channel)(event, ...args);
        // No privileged operations for similarly named origins, subframes,
        // unowned tabs or sessions that are not persistent workspaces.
        for (const event of [
            { sender: guest, senderFrame: { ...frame, origin: 'https://chromewebstore.google.com.evil' } },
            { sender: { ...guest, mainFrame: { ...frame, origin: 'https://evil.test' } }, senderFrame: { ...frame, origin: 'https://evil.test' } },
            { sender: guest, senderFrame: { ...frame } },
            { sender: { ...guest, hostWebContents: {} }, senderFrame: frame },
            { sender: { ...guest, session: {} }, senderFrame: frame }
        ]) {
            assert.equal(await invoke('chromeWebstore.beginInstall', event, { id }), null);
            assert.equal(await invoke('extensions-install', event, { kind: 'folder', workspace: 'Work' }), null);
        }
        const spoofedFrame = { ...frame, origin: 'https://chromewebstore.google.com.evil' };
        assert.equal(await invoke('chromeWebstore.beginInstall', { sender: { ...guest, mainFrame: spoofedFrame }, senderFrame: spoofedFrame }, { id }), null);
        assert.equal(await invoke('extensions-install', { sender: webContents, senderFrame: {} }, {}), null);
        assert((await invoke('extensions-install', mainEvent, { kind: 'store', input: id, workspace: 'Missing' })).error);
        accept = false;
        assert.equal((await invoke('chromeWebstore.beginInstall', storeEvent, { id })).result, 'user_cancelled');
        assert.equal(scan().length, 0);
        accept = true;
        assert.equal((await invoke('chromeWebstore.beginInstall', storeEvent, { id, manifest: '{"permissions":[]}' })).result, '');
        assert.match(lastPrompt.detail, /storage/, 'Confirmation uses the downloaded manifest, not store-page data');
        assert.equal(cfg.extensions_enabled, false);
        assert.equal(cfg.unrelated, 'kept');
        assert.deepEqual(cfg.extensions_by_workspace, { Work: [id], Other: [] });
        assert.deepEqual(cfg.extensions_sources[id], { type: 'store', id });
        const publicInfo = (await invoke('chrome.management.getAll', storeEvent))[0];
        assert.equal(publicInfo.path, undefined);
        assert.equal(publicInfo.folder, undefined);
        assert.equal(publicInfo.source, undefined);
        cfg.extensions_disabled = [id];
        bytes = signed('2.0');
        assert.equal((await invoke('extensions-update', mainEvent, id, 'Work')).installed, true);
        assert.deepEqual(cfg.extensions_disabled, [id]);
        assert.equal(scan()[0].id, id);
        assert.equal(scan()[0].version, '2.0');
        assert.equal((await invoke('extensions-update', mainEvent, id, 'Work')).unchanged, true);
        bytes = signed('1.0');
        assert.match((await invoke('extensions-update', mainEvent, id, 'Work')).error, /older version/);
        bytes = signed('3.0'); accept = false;
        assert.equal((await invoke('extensions-update', mainEvent, id, 'Work')).cancelled, true);
        assert.equal(scan()[0].version, '2.0');
        accept = true; simulateFailure = true;
        assert.match((await invoke('extensions-update', mainEvent, id, 'Work')).error, /could not load/);
        assert.equal(scan()[0].version, '2.0', 'Failed updates restore previous files');
        assert.deepEqual(cfg.extensions_disabled, [id]);
        assert.deepEqual(cfg.extensions_by_workspace, { Work: [id], Other: [] });
        assert.equal(cfg.unrelated, 'kept');
        simulateFailure = false;
        bytes = Buffer.from('not a signed CRX');
        assert.equal((await invoke('chromeWebstore.beginInstall', storeEvent, { id })).result, 'install_error');
        assert.equal(scan()[0].version, '2.0');
        assert.deepEqual(fs.readdirSync(directory), [id], 'Cancelled and failed operations leave no staging directories');
        // Execute the real preload: functions must never cross the IPC clone boundary,
        // and the empty begin-install success sentinel must survive both API forms.
        let bridge, response = { result: '' };
        const calls = [];
        vm.runInNewContext(fs.readFileSync(path.join(__dirname, '../extension-store.preload.js'), 'utf8'), {
            location: { origin: 'https://chromewebstore.google.com' }, process: { isMainFrame: true }, console,
            require: name => {
                assert.equal(name, 'electron');
                return {
                    contextBridge: { exposeInMainWorld: (_name, value) => { bridge = value; } },
                    ipcRenderer: { invoke: async (channel, ...args) => {
                        structuredClone(args);
                        calls.push([channel, ...args]);
                        return channel === 'chromeWebstore.beginInstall' ? response : 'success';
                    } },
                    webFrame: { executeJavaScript: async () => {} }
                };
            }
        });
        let callbackResult;
        assert.equal(await bridge.api.beginInstallWithManifest3({ id }, result => { callbackResult = result; }), '');
        assert.equal(callbackResult, '');
        response = { result: 'user_cancelled' };
        assert.equal(await bridge.api.beginInstallWithManifest3({ id }), 'user_cancelled');
        response = null;
        assert.equal(await bridge.api.beginInstallWithManifest3({ id }), 'unknown_error');
        callbackResult = undefined;
        await bridge.api.install(id, result => { callbackResult = result; });
        assert.equal(callbackResult, 'success');
        assert.deepEqual(calls.at(-1), ['chromeWebstore.install', id]);
        assert.equal(await bridge.api.install(id), 'success');
        assert.deepEqual(calls.at(-1), ['chromeWebstore.install', id]);
        console.log('Extension manager: IPC trust boundary, actual-package permissions, cancelled/disabled installs, scoped selections, store sources, update checks/downgrades, stable identity and rollback passed.');
    } finally {
        Module._load = originalLoad;
        packages.downloadStorePackage = originalDownload;
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
