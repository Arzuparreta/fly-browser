// tests/extensions.test.js
// Verification for opt-in extension support: manifest classification, id derivation,
// config gating and the guarantees the zero-cost-when-disabled design relies on

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const extensions = require('../extensions.js');
const keybinds = require('../keybinds.js');

console.log('Running Fly extension support tests...');

const tmpRoot = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-ext-test-'));
function writeExtension(folder, manifest, files = {}) {
    const dir = path.join(tmpRoot, folder);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify(manifest));
    for (const [name, content] of Object.entries(files)) {
        fs.mkdirSync(path.dirname(path.join(dir, name)), { recursive: true });
        fs.writeFileSync(path.join(dir, name), content);
    }
    return dir;
}

// 1. Background classification drives the cost labels shown in Preferences
{
    const { classifyBackground } = extensions;
    assert.strictEqual(classifyBackground({ manifest_version: 3 }), 'none');
    assert.strictEqual(classifyBackground({ manifest_version: 3, background: { service_worker: 'sw.js' } }), 'worker');
    assert.strictEqual(classifyBackground({ manifest_version: 2, background: { scripts: ['bg.js'] } }), 'persistent', 'MV2 pages are persistent unless stated otherwise');
    assert.strictEqual(classifyBackground({ manifest_version: 2, background: { scripts: ['bg.js'], persistent: false } }), 'event');
    assert.strictEqual(classifyBackground({ manifest_version: 2, background: { page: 'bg.html', persistent: true } }), 'persistent');
    console.log('✓ Background types classified (none, worker, event, persistent)');
}

// 2. Unpacked ids follow Chromium's derivation so storage survives restarts
{
    const { computeExtensionId } = extensions;
    const a = computeExtensionId('/home/user/.config/fly-browser/extensions/darkreader', {});
    const b = computeExtensionId('/home/user/.config/fly-browser/extensions/darkreader', {});
    const moved = computeExtensionId('/home/user/.config/fly-browser/extensions/darkreader-2', {});
    assert.match(a, /^[a-p]{32}$/, 'ids use 32 characters in the a..p range');
    assert.strictEqual(a, b, 'same path gives the same id');
    assert.notStrictEqual(a, moved, 'moving a folder changes the id, which is why the docs warn about renaming');

    const key = Buffer.from('fixed public key bytes').toString('base64');
    const keyedA = computeExtensionId('/one/place', { key });
    const keyedB = computeExtensionId('/another/place', { key });
    assert.strictEqual(keyedA, keyedB, 'a manifest key pins the id regardless of folder');

    // Electron 44 assigned this id when loading an unpacked extension from this path (recorded before the Fly rename)
    assert.strictEqual(
        computeExtensionId('/tmp/mise-id-vector/ext', {}),
        'agbidmmoanllbbjofeaokeofbhjccbjh',
        'matches the id Electron assigns to an unpacked extension'
    );
    console.log('✓ Extension ids match Chromium derivation (path and manifest key)');
}

// 3. Manifest description: localised names, popups, options, commands, warnings
{
    const dir = writeExtension('sample', {
        manifest_version: 3,
        name: '__MSG_appName__',
        version: '1.2.3',
        default_locale: 'en',
        action: { default_popup: 'popup.html', default_icon: { 16: 'i16.png', 32: 'i32.png' } },
        options_ui: { page: 'options.html' },
        background: { service_worker: 'sw.js' },
        permissions: ['storage', 'declarativeNetRequest'],
        commands: {
            toggle: { description: '__MSG_toggle__' },
            _execute_action: { description: 'Open popup' }
        }
    }, {
        '_locales/en/messages.json': JSON.stringify({ appName: { message: 'Sample Extension' }, toggle: { message: 'Toggle it' } }),
        'i32.png': 'png'
    });

    const info = extensions.describeExtension('sample', dir);
    assert.strictEqual(info.name, 'Sample Extension', 'localised __MSG_ names are resolved');
    assert.strictEqual(info.version, '1.2.3');
    assert.strictEqual(info.background, 'worker');
    assert.strictEqual(info.hasPopup, true);
    assert.strictEqual(info.optionsPage, 'options.html');
    assert.deepStrictEqual(info.commands, [{ name: 'toggle', description: 'Toggle it' }], 'reserved _execute_ commands are hidden');
    assert(info.warnings.some(w => w.includes('declarativeNetRequest')), 'unsupported DNR is flagged');
    assert(info.icon && info.icon.startsWith('data:image/png;base64,'), 'icon picks the 32px entry');
    console.log('✓ Manifest description resolves locale, popup, options, commands and warnings');
}

// 4. Firefox builds, persistent pages and broken folders are reported, never thrown
{
    const ff = writeExtension('ff', {
        manifest_version: 2,
        name: 'FF',
        version: '1',
        background: { page: 'bg.html' },
        browser_specific_settings: { gecko: { id: 'x@y' } }
    });
    const info = extensions.describeExtension('ff', ff);
    assert(info.warnings.some(w => w.startsWith('Firefox build')), 'Firefox builds are flagged');
    assert(info.warnings.some(w => w.includes('background process alive')), 'persistent pages are flagged');

    fs.mkdirSync(path.join(tmpRoot, 'broken'), { recursive: true });
    const broken = extensions.describeExtension('broken', path.join(tmpRoot, 'broken'));
    assert(broken.error, 'missing manifest returns an error entry instead of throwing');

    const escape = writeExtension('escape', { manifest_version: 3, name: 'E', version: '1', icons: { 32: '../../etc/passwd' } });
    assert.strictEqual(extensions.describeExtension('escape', escape).icon, null, 'icon paths cannot escape the extension folder');
    console.log('✓ Firefox builds, persistent pages, broken folders and path escapes handled');
}

// 5. Config gating: disabled means nothing loads, workspaces opt in individually
{
    const { isEnabled, getWorkspaceFolders, getGraceMs, isEligiblePartition, isValidFolderName } = extensions;
    assert.strictEqual(isEnabled({}), false, 'missing key means disabled');
    assert.strictEqual(isEnabled({ extensions_enabled: 'true' }), false, 'only a real boolean enables it');
    assert.strictEqual(isEnabled({ extensions_enabled: true }), true);

    const cfg = { extensions_by_workspace: { Work: ['darkreader', 'darkreader', '../evil', '.hidden', 'ok'] } };
    assert.deepStrictEqual(getWorkspaceFolders(cfg, 'Work'), ['darkreader', 'ok'], 'deduplicated, traversal and hidden names dropped');
    assert.deepStrictEqual(getWorkspaceFolders(cfg, 'Personal'), [], 'other workspaces get nothing');

    assert.strictEqual(getGraceMs({}), 60000, 'default grace period is 60 seconds');
    assert.strictEqual(getGraceMs({ extensions_unload_grace_seconds: 5 }), 5000);
    assert.strictEqual(getGraceMs({ extensions_unload_grace_seconds: -1 }), 60000, 'invalid values fall back to the default');

    assert.strictEqual(isEligiblePartition('persist:work'), true);
    assert.strictEqual(isEligiblePartition('FlyPrivateProfile'), false, 'private browsing never loads extensions');
    assert.strictEqual(isValidFolderName('a/b'), false);
    console.log('✓ Config gating, workspace scoping, grace period and private mode exclusion verified');
}

// 6. Zero cost when disabled: the API layer is only required lazily
{
    const source = fs.readFileSync(path.join(__dirname, '..', 'extensions.js'), 'utf8');
    const topLevelRequires = source.split('\n').filter(line => /^const .*require\(/.test(line));
    assert(!topLevelRequires.some(line => line.includes('electron-chrome-extensions')), 'electron-chrome-extensions must not be required at startup');
    assert(!topLevelRequires.some(line => line.includes("require('electron')")), 'electron APIs are resolved lazily so tests and startup stay cheap');

    const main = fs.readFileSync(path.join(__dirname, '..', 'main.js'), 'utf8');
    assert(/extensions_enabled: true/.test(main), 'uBlock support is enabled in DEFAULT_CONFIG');
    console.log('✓ API layer loads lazily and uBlock support ships enabled');
}

// 7. The keyboard action exists but takes no key from pages by default
{
    const metadata = keybinds.getActionMetadata();
    assert(metadata['open-extension-actions'], 'open-extension-actions metadata must exist');
    assert.deepStrictEqual(keybinds.DEFAULT_KEYBINDS['open-extension-actions'], [], 'no default key binding');
    console.log('✓ Extension actions keyboard entry registered without a default binding');
}

// Bundled installs, updates and removals must preserve profile decisions.
{
    const source = path.join(tmpRoot, 'bundles');
    const target = path.join(tmpRoot, 'profile-extensions');
    fs.mkdirSync(path.join(source, 'blocker'), { recursive: true });
    fs.writeFileSync(path.join(source, 'blocker', 'manifest.json'), JSON.stringify({ manifest_version: 2, name: 'Blocker', version: '1' }));
    let cfg = extensions.provisionBundledExtensions(target, source, {});
    assert(fs.existsSync(path.join(target, 'blocker', 'manifest.json')));
    assert.deepStrictEqual(extensions.getWorkspaceFolders(cfg, 'New workspace'), ['blocker']);
    cfg = { ...cfg, extensions_disabled: ['blocker'], extensions_by_workspace: { Work: [] } };
    fs.writeFileSync(path.join(source, 'blocker', 'manifest.json'), JSON.stringify({ manifest_version: 2, name: 'Blocker', version: '2' }));
    cfg = extensions.provisionBundledExtensions(target, source, cfg);
    assert.equal(cfg.extensions_bundled.blocker.version, '2');
    assert.deepStrictEqual(extensions.getWorkspaceFolders(cfg, 'New workspace'), []);
    assert.deepStrictEqual(extensions.getWorkspaceFolders({ ...cfg, extensions_disabled: [] }, 'Work'), [], 'An explicit empty selection must stay empty');
    const beforeId = extensions.describeExtension('blocker', path.join(target, 'blocker')).id;
    fs.rmSync(path.join(target, 'blocker'), { recursive: true });
    cfg = extensions.provisionBundledExtensions(target, source, cfg);
    assert.equal(cfg.extensions_bundled.blocker.removed, true);
    cfg = extensions.provisionBundledExtensions(target, source, cfg);
    assert(!fs.existsSync(path.join(target, 'blocker')), 'Deleted bundles must never be recreated');
    fs.cpSync(path.join(source, 'blocker'), path.join(target, 'blocker'), { recursive: true });
    assert.equal(extensions.describeExtension('blocker', path.join(target, 'blocker')).id, beforeId, 'Manual reinstall keeps storage identity');
    assert.equal(extensions.provisionBundledExtensions(target, source, cfg), cfg);

    const existing = path.join(tmpRoot, 'manual');
    fs.mkdirSync(path.join(existing, 'blocker'), { recursive: true });
    fs.writeFileSync(path.join(existing, 'blocker', 'manifest.json'), '{"name":"User copy","version":"99"}');
    const userCfg = extensions.provisionBundledExtensions(existing, source, {});
    assert.equal(userCfg.extensions_bundled.blocker.managed, false);
    assert.equal(extensions.describeExtension('blocker', path.join(existing, 'blocker')).version, '99');
    console.log('✓ Bundled installs preserve disabled state, empty workspace selections, removals, stable ids and manual copies');
}

fs.rmSync(tmpRoot, { recursive: true, force: true });
console.log('All extension support tests passed.');

// One host can subscribe to many extension events without repeated destruction
// callbacks. Destruction must still remove all of its subscriptions.
{
    const { EventEmitter } = require('node:events');
    const { deduplicateHostObservers } = require('../extensions');
    const hosts = [new EventEmitter(), new EventEmitter()];
    const router = {
        listeners: [],
        observeListenerHost(host) {
            host.once('destroyed', () => { this.listeners = this.listeners.filter(entry => entry.host !== host); });
        }
    };
    deduplicateHostObservers(router);
    for (let index = 0; index < 40; index++) {
        for (const host of hosts) {
            router.listeners.push({host, event: index});
            router.observeListenerHost(host);
        }
    }
    assert.equal(hosts[0].listenerCount('destroyed'), 1);
    assert.equal(hosts[1].listenerCount('destroyed'), 1);
    hosts[0].emit('destroyed');
    assert.equal(router.listeners.length, 40);
    assert(router.listeners.every(entry => entry.host === hosts[1]));
    hosts[1].emit('destroyed');
    assert.equal(router.listeners.length, 0);
    console.log('Extension hosts share one destruction observer and retain complete cleanup.');
}

// Listener removal must release the registered wrapper, preserve other callbacks
// and unsubscribe the host only after its last callback has gone.
{
    const {EventEmitter} = require('node:events');
    const vm = require('node:vm');
    const source = fs.readFileSync(path.join(__dirname, '../extension-api.preload.js'), 'utf8');
    const start = source.indexOf('  var listenerMap =');
    const end = source.indexOf('  // src/renderer/index.ts', start);
    const ipcRenderer = new EventEmitter(), subscriptions = [];
    ipcRenderer.send = (...args) => subscriptions.push(args);
    const context = {import_electron: {ipcRenderer}, formatIpcName: name => `crx-${name}`};
    vm.createContext(context);
    vm.runInContext(source.slice(start, end), context);
    let a = 0, b = 0;
    const first = value => {a += value;}, second = value => {b += value;};
    for (let i = 0; i < 100; i++) {
        context.addExtensionListener('extension', 'tabs.onUpdated', first);
    }
    context.addExtensionListener('extension', 'tabs.onUpdated', second);
    assert.equal(ipcRenderer.listenerCount('crx-tabs.onUpdated'), 2);
    ipcRenderer.emit('crx-tabs.onUpdated', {}, 1);
    assert.deepEqual([a, b], [1, 1]);
    context.removeExtensionListener('extension', 'tabs.onUpdated', first);
    context.removeExtensionListener('extension', 'tabs.onUpdated', first);
    ipcRenderer.emit('crx-tabs.onUpdated', {}, 1);
    assert.deepEqual([a, b], [1, 2]);
    context.removeExtensionListener('extension', 'tabs.onUpdated', second);
    assert.equal(ipcRenderer.listenerCount('crx-tabs.onUpdated'), 0);
    assert.deepEqual(subscriptions, [
        ['crx-add-listener', 'extension', 'tabs.onUpdated'],
        ['crx-remove-listener', 'extension', 'tabs.onUpdated']
    ]);
    console.log('Extension callbacks deduplicate and detach without leaving wrappers or host subscriptions.');
}

// An address entered while uBlock starts must win over the restored address.
// Closing or hibernating a waiting view must not start it later.
(async () => {
    const vm = require('node:vm');
    const source = fs.readFileSync(path.join(__dirname, '../modules/extensions.js'), 'utf8');
    const start = source.indexOf('export function loadWebviewSource');
    const end = source.indexOf('// Called whenever the active view changes', start);
    const pending = [], navigations = [];
    const context = {
        workspaceHasExtensions: () => true,
        isPersistentPartition: partition => partition.startsWith('persist:'),
        window: {flyAPI: {extensionsEnsure: () => new Promise(resolve => pending.push(resolve))}}
    };
    vm.createContext(context);
    vm.runInContext(source.slice(start, end).replace('export ', ''), context);
    const view = {isConnected: true, getAttribute: () => 'persist:work', setAttribute: (_name, url) => navigations.push(url)};
    context.loadWebviewSource(view, 'Work', 'https://restored.example/');
    context.loadWebviewSource(view, 'Work', 'https://typed.example/');
    assert.equal(navigations.length, 0, 'Navigation waits for the extension without a bypass timer');
    pending[1](); await Promise.resolve();
    pending[0](); await Promise.resolve();
    assert.deepEqual(navigations, ['https://typed.example/']);
    context.loadWebviewSource(view, 'Work', 'https://closed.example/');
    view.isConnected = false;
    pending[2](); await Promise.resolve();
    assert.deepEqual(navigations, ['https://typed.example/']);
    console.log('Startup navigation waits for filtering, preserves the latest address and leaves closed views alone.');
})().catch(error => {console.error(error); process.exitCode = 1;});
