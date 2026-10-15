// extensions.js
// Chrome extension support for Fly Browser. Bundled uBlock is enabled by default.
// Extensions are scoped to the workspaces that enable them. They load when such a
// workspace gets its first live tab and unload after a grace period once every tab
// in it has been closed, so an idle workspace costs nothing.
//
// Chrome APIs that Electron lacks (tabs, windows, action, contextMenus, commands)
// come from electron-chrome-extensions, which is only required once the first
// extension actually loads.

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

const DEFAULT_GRACE_SECONDS = 60;
const CREATE_TAB_TIMEOUT_MS = 5000;
const MAX_ICON_BYTES = 64 * 1024;
const ACTION_UPDATE_DEBOUNCE_MS = 250;

let extensionsDir = null;
let getConfig = () => ({});
let saveConfig = () => {};
const bundledDir = path.join(__dirname.endsWith('.asar') ? `${__dirname}.unpacked` : __dirname, 'assets', 'extensions');
let setNetworkExtensions = () => {};
let getMainWindow = () => null;
let partitionForWorkspace = (name) => `persist:${name}`;
let logEvent = () => {};

// partition -> controller, created on the first ensure for an opted-in workspace
const controllers = new Map();
let activePartition = null;
let openPopup = null;
const popupWebContents = new Set();
let actionUpdateTimer = null;

// MANIFEST HELPERS (pure, covered by tests/extensions.test.js)

// Chromium derives an unpacked extension's id from its absolute path, or from the
// manifest "key" when present. Same mapping in both cases: sha256, first 32 hex
// digits, each digit shifted into the a..p range.
function idFromHash(buffer) {
    const hex = crypto.createHash('sha256').update(buffer).digest('hex').slice(0, 32);
    return [...hex].map(d => String.fromCharCode(97 + parseInt(d, 16))).join('');
}

function computeExtensionId(absPath, manifest) {
    if (manifest && typeof manifest.key === 'string' && manifest.key.trim()) {
        try { return idFromHash(Buffer.from(manifest.key.trim(), 'base64')); } catch (e) {}
    }
    return idFromHash(Buffer.from(absPath));
}

function readLocaleMessages(extPath, manifest) {
    const locale = manifest && manifest.default_locale;
    if (!locale) return null;
    try {
        const raw = fs.readFileSync(path.join(extPath, '_locales', locale, 'messages.json'), 'utf8');
        return JSON.parse(raw);
    } catch (e) {
        return null;
    }
}

function localise(value, messages) {
    if (typeof value !== 'string') return value;
    const match = value.match(/^__MSG_(.+)__$/);
    if (!match || !messages) return value;
    const key = Object.keys(messages).find(k => k.toLowerCase() === match[1].toLowerCase());
    return key && messages[key] && messages[key].message ? messages[key].message : value;
}

// none: content scripts only. worker: MV3 service worker, Chromium stops it when idle.
// persistent: MV2 background page that stays alive while loaded. event: MV2 lazy page.
function classifyBackground(manifest) {
    const bg = manifest && manifest.background;
    if (!bg) return 'none';
    if (bg.service_worker) return 'worker';
    if (bg.page || (Array.isArray(bg.scripts) && bg.scripts.length)) {
        if (manifest.manifest_version === 2 && bg.persistent === false) return 'event';
        return 'persistent';
    }
    return 'none';
}

function getActionManifest(manifest) {
    if (!manifest) return null;
    return manifest.manifest_version === 3 ? manifest.action || null : manifest.browser_action || null;
}

function collectWarnings(manifest) {
    const warnings = [];
    const perms = [].concat(manifest.permissions || [], manifest.optional_permissions || []);
    const background = classifyBackground(manifest);

    if (background === 'persistent') {
        warnings.push('Keeps a background process alive for as long as it is loaded.');
    }
    if (perms.some(p => typeof p === 'string' && p.startsWith('declarativeNetRequest'))) {
        warnings.push('Uses declarativeNetRequest. Electron exposes the API but does not enforce its rules, so they will not apply.');
    }
    if (perms.includes('webRequestBlocking')) {
        warnings.push('Network filtering is handled by this extension; Fly leaves request hooks available to it.');
    }
    if (perms.includes('nativeMessaging')) {
        warnings.push('Native messaging hosts are not configured for Fly and will probably fail.');
    }
    if (manifest.side_panel || perms.includes('sidePanel')) {
        warnings.push('Side panels are not supported.');
    }
    const gecko = (manifest.browser_specific_settings && manifest.browser_specific_settings.gecko) ||
        (manifest.applications && manifest.applications.gecko);
    if (gecko) {
        warnings.push('Firefox build. Works only if it avoids Firefox-only APIs; prefer the Chrome build.');
    }
    return warnings;
}

function pickIconPath(manifest) {
    const action = getActionManifest(manifest);
    const candidates = [action && action.default_icon, manifest.icons];
    for (const set of candidates) {
        if (typeof set === 'string') return set;
        if (set && typeof set === 'object') {
            const sizes = Object.keys(set).map(Number).filter(n => !Number.isNaN(n)).sort((a, b) => a - b);
            if (!sizes.length) continue;
            const best = sizes.find(s => s >= 32) || sizes[sizes.length - 1];
            return set[String(best)];
        }
    }
    return null;
}

function readIconDataUrl(extPath, relPath) {
    if (!relPath) return null;
    const resolved = path.resolve(extPath, relPath.replace(/^\//, ''));
    if (!resolved.startsWith(extPath + path.sep)) return null;
    try {
        const stat = fs.statSync(resolved);
        if (stat.size > MAX_ICON_BYTES) return null;
        const ext = path.extname(resolved).toLowerCase();
        const mime = ext === '.svg' ? 'image/svg+xml' : ext === '.jpg' || ext === '.jpeg' ? 'image/jpeg' : 'image/png';
        return `data:${mime};base64,${fs.readFileSync(resolved).toString('base64')}`;
    } catch (e) {
        return null;
    }
}

function describeExtension(folder, extPath) {
    let manifest;
    try {
        manifest = JSON.parse(fs.readFileSync(path.join(extPath, 'manifest.json'), 'utf8'));
    } catch (e) {
        return { folder, path: extPath, error: 'manifest.json is missing or invalid' };
    }
    const messages = readLocaleMessages(extPath, manifest);
    const action = getActionManifest(manifest);
    const optionsPage = (manifest.options_ui && manifest.options_ui.page) || manifest.options_page || null;
    const commands = Object.entries(manifest.commands || {})
        .filter(([name]) => !name.startsWith('_execute_'))
        .map(([name, details]) => ({ name, description: localise((details && details.description) || name, messages) }));

    // Chromium resolves symlinks before deriving the id, so match it
    let realPath = extPath;
    try { realPath = fs.realpathSync(extPath); } catch (e) {}

    return {
        folder,
        path: extPath,
        id: computeExtensionId(realPath, manifest),
        name: localise(manifest.name, messages) || folder,
        version: manifest.version || '',
        manifestVersion: manifest.manifest_version,
        background: classifyBackground(manifest),
        hasAction: !!action,
        hasPopup: !!(action && action.default_popup),
        optionsPage,
        commands,
        warnings: collectWarnings(manifest),
        icon: readIconDataUrl(extPath, pickIconPath(manifest))
    };
}

function isValidFolderName(folder) {
    return typeof folder === 'string' && folder.length > 0 && folder === path.basename(folder) && !folder.startsWith('.');
}

// CONFIG HELPERS

function isEnabled(cfg) {
    return !!(cfg && cfg.extensions_enabled === true);
}

function getWorkspaceFolders(cfg, workspace) {
    const map = (cfg && cfg.extensions_by_workspace) || {};
    const defaults = Object.entries((cfg && cfg.extensions_bundled) || {})
        .filter(([, entry]) => entry && !entry.removed).map(([folder]) => folder);
    const list = Array.isArray(map[workspace]) ? map[workspace] : defaults;
    const disabled = (cfg && cfg.extensions_disabled) || [];
    return [...new Set(list.filter(folder => isValidFolderName(folder) && !disabled.includes(folder)))];
}

function getGraceMs(cfg) {
    const seconds = Number(cfg && cfg.extensions_unload_grace_seconds);
    return (Number.isFinite(seconds) && seconds >= 0 ? seconds : DEFAULT_GRACE_SECONDS) * 1000;
}

// Only persistent workspace partitions qualify. Electron refuses to load extensions
// into in-memory sessions, which rules out the private profile.
function isEligiblePartition(partition) {
    return typeof partition === 'string' && partition.startsWith('persist:');
}

// Profile-owned copies keep stable identities. Missing copies become durable
// removal records; upgrades never change enabled state or workspace selections.
function provisionBundledExtensions(directory, sourceDirectory, cfg) {
    const records = { ...(cfg.extensions_bundled || {}) };
    let changed = false;
    if (!fs.existsSync(sourceDirectory)) return cfg;
    for (const folder of fs.readdirSync(sourceDirectory)) {
        if (!isValidFolderName(folder)) continue;
        const source = path.join(sourceDirectory, folder);
        if (!fs.existsSync(path.join(source, 'manifest.json'))) continue;
        const destination = path.join(directory, folder);
        const previous = records[folder];
        if (previous && previous.removed) continue;
        if (previous && !fs.existsSync(destination)) {
            records[folder] = { ...previous, removed: true };
            changed = true;
            continue;
        }
        const version = JSON.parse(fs.readFileSync(path.join(source, 'manifest.json'), 'utf8')).version;
        if (!previous && fs.existsSync(destination)) {
            records[folder] = { version, managed: false, removed: false };
            changed = true;
            continue;
        }
        if (previous && (!previous.managed || previous.version === version)) continue;
        fs.mkdirSync(directory, { recursive: true });
        const staging = fs.mkdtempSync(path.join(directory, '.bundle-'));
        const backup = staging + '-previous';
        try {
            fs.cpSync(source, staging, { recursive: true });
            if (fs.existsSync(destination)) fs.renameSync(destination, backup);
            try { fs.renameSync(staging, destination); } catch (error) {
                if (fs.existsSync(backup)) fs.renameSync(backup, destination);
                throw error;
            }
            fs.rmSync(backup, { recursive: true, force: true });
        } finally {
            fs.rmSync(staging, { recursive: true, force: true });
        }
        records[folder] = { version, managed: true, removed: false };
        changed = true;
    }
    return changed ? { ...cfg, extensions_bundled: records } : cfg;
}

function provisionBundles() {
    const cfg = getConfig();
    const next = provisionBundledExtensions(extensionsDir, bundledDir, cfg);
    if (next !== cfg) saveConfig(next);
    return next;
}

// RUNTIME

function electron() {
    return require('electron');
}

function scanExtensions() {
    provisionBundles();
    if (!extensionsDir || !fs.existsSync(extensionsDir)) return [];
    let entries = [];
    try {
        entries = fs.readdirSync(extensionsDir, { withFileTypes: true });
    } catch (e) {
        return [];
    }
    return entries
        .filter(entry => entry.isDirectory() && isValidFolderName(entry.name))
        .map(entry => ({ ...describeExtension(entry.name, path.join(extensionsDir, entry.name)),
            bundled: !!getConfig().extensions_bundled?.[entry.name],
            enabled: !(getConfig().extensions_disabled || []).includes(entry.name) }))
        .sort((a, b) => String(a.name).localeCompare(String(b.name)));
}

function sendToRenderer(action, ...args) {
    const win = getMainWindow();
    if (win && !win.isDestroyed() && win.webContents) {
        win.webContents.send('master-shortcut', action, ...args);
    }
}

function controllerForSession(targetSession) {
    for (const ctrl of controllers.values()) {
        if (ctrl.session === targetSession) return ctrl;
    }
    return null;
}

function createController(workspace, partition) {
    const ctrl = {
        workspace,
        partition,
        session: electron().session.fromPartition(partition),
        ece: null,
        loaded: new Map(),
        errors: {},
        tabs: new Set(),
        activeTabId: null,
        graceTimer: null,
        pendingCreates: [],
        suppressSelect: false,
        queue: Promise.resolve()
    };
    controllers.set(partition, ctrl);
    ctrl.session.extensions.on('extension-unloaded', (_event, extension) => {
        const folder = [...ctrl.loaded].find(([, entry]) => entry.id === extension.id)?.[0];
        if (!folder) return;
        // runtime.reload unloads unpacked extensions in Electron without loading
        // them again. Honour the current profile policy before restoring one.
        ctrl.loaded.delete(folder);
        updateNetworkExtensions(ctrl);
        enqueue(ctrl, () => {
            const cfg = getConfig();
            if (isEnabled(cfg) && ctrl.tabs.size > 0) {
                return syncController(ctrl, getWorkspaceFolders(cfg, ctrl.workspace));
            }
        });
    });
    return ctrl;
}

// Serialises load and unload work per workspace so rapid switching cannot interleave
function enqueue(ctrl, task) {
    const run = ctrl.queue.then(task, task);
    ctrl.queue = run.catch(() => {});
    return run;
}

function withSuppressedSelect(ctrl, fn) {
    ctrl.suppressSelect = true;
    try { return fn(); } finally { ctrl.suppressSelect = false; }
}

// electron-chrome-extensions 4.9 observes a host once per subscribed event.
// Its destruction callback already removes every event for that host, so one
// observer is sufficient even after listeners are removed and registered again.
function deduplicateHostObservers(router) {
    if (!router || typeof router.observeListenerHost !== 'function') return;
    const observed = new WeakSet();
    const observe = router.observeListenerHost;
    router.observeListenerHost = function(host) {
        if (observed.has(host)) return;
        observe.call(this, host);
        observed.add(host);
    };
}

function ensureApiLayer(ctrl) {
    if (ctrl.ece) return ctrl.ece;
    const started = Date.now();
    const { ElectronChromeExtensions } = require('electron-chrome-extensions');
    ctrl.ece = new ElectronChromeExtensions({
        license: 'GPL-3.0',
        session: ctrl.session,
        createTab: (details) => createTabForExtension(ctrl, details),
        selectTab: (wc) => {
            if (ctrl.suppressSelect || !wc || wc.isDestroyed()) return;
            sendToRenderer('extension-select-tab', wc.id);
        },
        removeTab: (wc) => {
            // Also called when a tab is destroyed; only act on live tabs
            if (!wc || wc.isDestroyed() || popupWebContents.has(wc.id)) return;
            sendToRenderer('extension-close-tab', wc.id);
        }
    });

    deduplicateHostObservers(ctrl.ece.ctx?.router);

    // Keep the existing API backend, adapting the browser alias and managed storage.
    for (const script of ctrl.session.getPreloadScripts()) {
        if (script.id === 'crx-mv2-preload' || script.id === 'crx-mv3-preload') ctrl.session.unregisterPreloadScript(script.id);
    }
    for (const type of ['frame', 'service-worker']) {
        ctrl.session.registerPreloadScript({ id: `fly-extensions-${type}`, type, filePath: path.join(__dirname, 'extension-api.preload.js') });
    }

    const browserAction = ctrl.ece.api && ctrl.ece.api.browserAction;
    if (browserAction && typeof browserAction.onUpdate === 'function') {
        const original = browserAction.onUpdate.bind(browserAction);
        browserAction.onUpdate = () => {
            original();
            scheduleActionUpdate();
        };
    }
    logEvent('info', 'extensions', `Extension API layer attached to ${ctrl.workspace} in ${Date.now() - started}ms`);
    return ctrl.ece;
}

function scheduleActionUpdate() {
    if (actionUpdateTimer) return;
    actionUpdateTimer = setTimeout(() => {
        actionUpdateTimer = null;
        sendToRenderer('extensions-actions-updated');
    }, ACTION_UPDATE_DEBOUNCE_MS);
}

function createTabForExtension(ctrl, details) {
    if (activePartition !== ctrl.partition) {
        return Promise.reject(new Error('Fly only opens extension tabs in the workspace currently in view'));
    }
    const win = getMainWindow();
    return new Promise((resolve, reject) => {
        const pending = { resolve: (wc) => resolve([wc, win]), timer: null };
        pending.timer = setTimeout(() => {
            ctrl.pendingCreates = ctrl.pendingCreates.filter(p => p !== pending);
            reject(new Error('Timed out waiting for Fly to open the tab'));
        }, CREATE_TAB_TIMEOUT_MS);
        ctrl.pendingCreates.push(pending);
        sendToRenderer('spawn-tab-with-url', details.url || 'about:blank');
    });
}

function cancelGrace(ctrl) {
    if (ctrl.graceTimer) {
        clearTimeout(ctrl.graceTimer);
        ctrl.graceTimer = null;
    }
}

function scheduleUnload(ctrl) {
    if (ctrl.graceTimer || ctrl.loaded.size === 0) return;
    ctrl.graceTimer = setTimeout(() => {
        ctrl.graceTimer = null;
        if (ctrl.tabs.size === 0) {
            enqueue(ctrl, () => unloadAll(ctrl, 'no live tabs'));
        }
    }, getGraceMs(getConfig()));
}

function unloadOne(ctrl, folder) {
    const entry = ctrl.loaded.get(folder);
    if (!entry) return;
    if (openPopup && openPopup.partition === ctrl.partition && openPopup.extensionId === entry.id) {
        sendToRenderer('close-extension-popup');
        openPopup = null;
    }
    ctrl.loaded.delete(folder);
    try { ctrl.session.extensions.removeExtension(entry.id); } catch (e) {}
    updateNetworkExtensions(ctrl);
}

function unloadAll(ctrl, reason) {
    if (ctrl.loaded.size === 0) return;
    const count = ctrl.loaded.size;
    for (const folder of [...ctrl.loaded.keys()]) unloadOne(ctrl, folder);
    logEvent('info', 'extensions', `Unloaded ${count} extension(s) from ${ctrl.workspace} (${reason})`);
    sendToRenderer('extensions-actions-updated');
}

function updateNetworkExtensions(ctrl) {
    setNetworkExtensions(ctrl.session, [...ctrl.loaded.values()].some(entry => entry.network));
}

async function initialiseBackgroundPage(ctrl, extension, folder) {
    if (extension.manifest.manifest_version !== 2 || classifyBackground(extension.manifest) !== 'persistent') return;
    const extensionId = extension.id;
    let page;
    const deadline = Date.now() + 5000;
    while (!page && Date.now() < deadline) {
        page = electron().webContents.getAllWebContents().find(wc =>
            wc.getType() === 'backgroundPage' && wc.session === ctrl.session &&
            wc.getURL().startsWith(`chrome-extension://${extensionId}/`));
        if (!page) await new Promise(resolve => setTimeout(resolve, 25));
    }
    if (!page) throw new Error('Extension background page unavailable');
    const hasApis = () => page.executeJavaScript('!!(globalThis.chrome && chrome.contextMenus && chrome.windows)');
    if (!await hasApis()) throw new Error('Extension background APIs unavailable');
    // loadExtension only means that the background page can start. uBlock cancels
    // requests until its lists are ready. Check its final startup marker (including
    // unsuspending network requests) before navigating restored pages. A bounded
    // startup probe also tolerates uBlock replacing its first background document.
    if (folder === 'ublock-origin') {
        const deadline = Date.now() + 30000;
        while (Date.now() < deadline) {
            const background = electron().webContents.getAllWebContents().find(wc =>
                !wc.isDestroyed() && wc.getType() === 'backgroundPage' && wc.session === ctrl.session &&
                wc.getURL().startsWith(`chrome-extension://${extensionId}/`));
            if (background) {
                let timer;
                try {
                    const ready = await Promise.race([
                        background.executeJavaScript("import('./js/background.js').then(({default: u}) => u.readyToFilter && u.supportStats.allReadyAfter !== '?' && vAPI.net.suspendDepth === 0)"),
                        new Promise(resolve => { timer = setTimeout(() => resolve(false), 1000); })
                    ]).catch(() => false);
                    if (ready) return;
                } finally {
                    clearTimeout(timer);
                }
            }
            await new Promise(resolve => setTimeout(resolve, 25));
        }
        throw new Error('uBlock Origin did not finish initialising');
    }
}

async function syncController(ctrl, wanted) {
    for (const folder of [...ctrl.loaded.keys()]) {
        if (!wanted.includes(folder)) unloadOne(ctrl, folder);
    }
    for (const folder of wanted) {
        if (ctrl.loaded.has(folder)) continue;
        const extPath = path.join(extensionsDir, folder);
        if (!fs.existsSync(path.join(extPath, 'manifest.json'))) {
            ctrl.errors[folder] = 'Folder or manifest.json not found';
            continue;
        }
        ensureApiLayer(ctrl);
        const started = Date.now();
        try {
            const manifest = JSON.parse(fs.readFileSync(path.join(extPath, 'manifest.json'), 'utf8'));
            const network = (manifest.permissions || []).includes('webRequest');
            // Release native hooks before the extension registers its listeners.
            if (network) setNetworkExtensions(ctrl.session, true);
            const ext = await ctrl.session.extensions.loadExtension(extPath, { allowFileAccess: false });
            try { await initialiseBackgroundPage(ctrl, ext, folder); } catch (error) {
                ctrl.session.extensions.removeExtension(ext.id);
                throw error;
            }
            ctrl.loaded.set(folder, { id: ext.id, name: ext.name, network, loadMs: Date.now() - started });
            updateNetworkExtensions(ctrl);
            delete ctrl.errors[folder];
        } catch (err) {
            unloadOne(ctrl, folder);
            updateNetworkExtensions(ctrl);
            ctrl.errors[folder] = err && err.message ? err.message : String(err);
            logEvent('warn', 'extensions', `Failed to load ${folder} in ${ctrl.workspace}: ${ctrl.errors[folder]}`);
        }
    }
    if (ctrl.loaded.size > 0) adoptExistingTabs(ctrl);
    sendToRenderer('extensions-actions-updated');
}

// Tabs that were already open when a workspace enabled its first extension. Their
// pages need a reload before content scripts appear, which the UI points out.
function adoptExistingTabs(ctrl) {
    const win = getMainWindow();
    if (!win || win.isDestroyed()) return;
    for (const wc of electron().webContents.getAllWebContents()) {
        if (wc.isDestroyed() || wc.getType() !== 'webview' || wc.session !== ctrl.session) continue;
        if (wc.hostWebContents !== win.webContents || popupWebContents.has(wc.id)) continue;
        attachTabWebContents(wc);
    }
}

// Called by the renderer right before a tab's src is set, so content scripts that
// run at document_start are in place for the very first navigation.
async function ensureForWorkspace(workspace, partition) {
    let cfg = getConfig();
    if (!isEnabled(cfg) || !isEligiblePartition(partition)) return { active: false };
    if (partition !== partitionForWorkspace(workspace)) return { active: false };

    cfg = provisionBundles();
    const wanted = getWorkspaceFolders(cfg, workspace);
    let ctrl = controllers.get(partition);
    if (!ctrl && wanted.length === 0) return { active: false };
    if (!ctrl) ctrl = createController(workspace, partition);
    ctrl.workspace = workspace;
    cancelGrace(ctrl);

    await enqueue(ctrl, () => syncController(ctrl, wanted));
    // Protects against a tab that never attaches; attaching cancels it again
    if (ctrl.tabs.size === 0) scheduleUnload(ctrl);
    return { active: ctrl.loaded.size > 0, count: ctrl.loaded.size };
}

function getTabWebContents(wcId) {
    const wc = electron().webContents.fromId(Number(wcId));
    const win = getMainWindow();
    if (!wc || wc.isDestroyed() || wc.getType() !== 'webview') return null;
    if (!win || win.isDestroyed() || wc.hostWebContents !== win.webContents) return null;
    return wc;
}

function attachTabWebContents(wc) {
    const ctrl = controllerForSession(wc.session);
    if (!ctrl || !ctrl.ece || ctrl.tabs.has(wc.id) || popupWebContents.has(wc.id)) return false;

    ctrl.tabs.add(wc.id);
    cancelGrace(ctrl);
    withSuppressedSelect(ctrl, () => ctrl.ece.addTab(wc, getMainWindow()));
    const id = wc.id;
    wc.once('destroyed', () => {
        ctrl.tabs.delete(id);
        if (ctrl.activeTabId === id) ctrl.activeTabId = null;
        if (ctrl.tabs.size === 0) scheduleUnload(ctrl);
    });

    const pending = ctrl.pendingCreates.shift();
    if (pending) {
        clearTimeout(pending.timer);
        pending.resolve(wc);
    }
    return true;
}

// wcId is 0 when the view in focus belongs to a workspace without extensions, which
// keeps chrome.tabs.create from opening tabs in a container the user is not looking at
function focusTab(wcId) {
    const wc = wcId ? getTabWebContents(wcId) : null;
    const ctrl = wc ? controllerForSession(wc.session) : null;
    activePartition = ctrl ? ctrl.partition : null;
    if (!ctrl || !ctrl.ece || !ctrl.tabs.has(wc.id)) return false;
    ctrl.activeTabId = wc.id;
    withSuppressedSelect(ctrl, () => ctrl.ece.selectTab(wc));
    return true;
}

function findLoaded(ctrl, extensionId) {
    for (const [folder, entry] of ctrl.loaded) {
        if (entry.id === extensionId) return { folder, ...entry };
    }
    return null;
}

// Popups render inside Fly's own window as a <webview>. Separate popup windows
// cannot be positioned on Wayland and get tiled by Hyprland or sway.
function resolveActionTarget(extensionId, partition) {
    const ctrl = controllers.get(partition);
    if (!ctrl || !ctrl.ece || !findLoaded(ctrl, extensionId)) {
        return { error: 'Extension is not loaded in this workspace' };
    }
    const browserAction = ctrl.ece.api && ctrl.ece.api.browserAction;
    if (!browserAction) return { error: 'Action API unavailable' };
    const tabId = ctrl.activeTabId != null ? ctrl.activeTabId : -1;
    const popupUrl = browserAction.getPopupUrl(extensionId, tabId);
    if (popupUrl && popupUrl.startsWith(`chrome-extension://${extensionId}/`)) {
        openPopup = { partition, extensionId, url: popupUrl };
        return { kind: 'popup', url: popupUrl, partition };
    }
    try {
        browserAction.activateClick({ eventType: 'click', extensionId, tabId });
        return { kind: 'clicked' };
    } catch (err) {
        return { error: err.message };
    }
}

function attachPopupWebContents(wc) {
    popupWebContents.add(wc.id);
    const ctrl = controllerForSession(wc.session);
    if (ctrl && ctrl.tabs.has(wc.id)) ctrl.tabs.delete(wc.id);
    wc.on('before-input-event', (event, input) => {
        if (input.type === 'keyDown' && input.key === 'Escape') {
            event.preventDefault();
            sendToRenderer('close-extension-popup');
        }
    });
    const id = wc.id;
    wc.once('destroyed', () => {
        popupWebContents.delete(id);
        openPopup = null;
    });
    return true;
}

// Wired to the main window's did-attach-webview, so tabs register in the main process
// without a renderer round trip that could race the extension load. Guests that
// attach while a workspace is still loading are picked up by adoptExistingTabs.
function handleWebviewAttached(wc, src) {
    if (controllers.size === 0 || !wc || wc.isDestroyed()) return;
    if (openPopup && openPopup.url && src === openPopup.url) {
        attachPopupWebContents(wc);
        return;
    }
    attachTabWebContents(wc);
}

// A renamed workspace keeps its partition, so its extension selection and its loaded
// extensions follow it to the new name. A selection left under the new name by a deleted
// workspace is dropped.
function renameWorkspace(from, to) {
    const cfg = getConfig();
    const map = { ...((cfg && cfg.extensions_by_workspace) || {}) };
    if (Object.prototype.hasOwnProperty.call(map, from) || Object.prototype.hasOwnProperty.call(map, to)) {
        const list = map[from];
        delete map[from];
        delete map[to];
        if (list !== undefined) map[to] = list;
        saveConfig({ extensions_by_workspace: map });
    }
    for (const ctrl of controllers.values()) {
        if (ctrl.workspace === from) ctrl.workspace = to;
    }
    return true;
}

function runCommand(extensionId, partition, commandName) {
    const ctrl = controllers.get(partition);
    if (!ctrl || !ctrl.ece || !findLoaded(ctrl, extensionId)) return false;
    const router = ctrl.ece.ctx && ctrl.ece.ctx.router;
    if (!router || typeof router.sendEvent !== 'function') return false;
    const tab = ctrl.activeTabId != null ? ctrl.ece.ctx.store.tabDetailsCache.get(ctrl.activeTabId) : undefined;
    router.sendEvent(extensionId, 'commands.onCommand', commandName, tab);
    return true;
}

function getActionStates(partition) {
    const ctrl = controllers.get(partition);
    if (!ctrl || !ctrl.ece || ctrl.loaded.size === 0) return [];
    const browserAction = ctrl.ece.api && ctrl.ece.api.browserAction;
    if (!browserAction) return [];
    const state = browserAction.getState();
    const tabKey = ctrl.activeTabId != null ? String(ctrl.activeTabId) : null;
    const result = [];
    for (const [folder, entry] of ctrl.loaded) {
        const action = state.actions.find(a => a.id === entry.id);
        if (!action) continue;
        const perTab = (tabKey && action.tabs && action.tabs[tabKey]) || {};
        result.push({
            folder,
            id: entry.id,
            title: perTab.title || action.title || entry.name,
            badgeText: perTab.text !== undefined ? perTab.text : action.text || '',
            badgeColor: perTab.color || action.color || null
        });
    }
    return result;
}

function getContextMenuItems(wc, params) {
    const ctrl = controllers.size ? controllerForSession(wc.session) : null;
    if (!ctrl || !ctrl.ece || ctrl.loaded.size === 0 || !ctrl.tabs.has(wc.id)) return [];
    try { return ctrl.ece.getContextMenuItems(wc, params) || []; } catch (e) { return []; }
}

// Settings changes apply immediately: workspaces with live tabs sync now, idle ones
// drop whatever they no longer need.
async function applyConfig() {
    const cfg = isEnabled(getConfig()) ? provisionBundles() : getConfig();
    const jobs = [];
    for (const ctrl of controllers.values()) {
        if (!isEnabled(cfg)) {
            jobs.push(enqueue(ctrl, () => unloadAll(ctrl, 'extensions disabled')));
            continue;
        }
        const wanted = getWorkspaceFolders(cfg, ctrl.workspace);
        if (ctrl.tabs.size > 0) {
            jobs.push(enqueue(ctrl, () => syncController(ctrl, wanted)));
        } else {
            jobs.push(enqueue(ctrl, () => {
                for (const folder of [...ctrl.loaded.keys()]) {
                    if (!wanted.includes(folder)) unloadOne(ctrl, folder);
                }
            }));
        }
    }
    await Promise.all(jobs);
}

// Re-reads extension code from disk. Pages that already ran the old content scripts
// keep them until reloaded, so the caller reports how many tabs are affected.
async function reloadAll() {
    let affectedTabs = 0;
    const cfg = getConfig();
    const jobs = [];
    for (const ctrl of controllers.values()) {
        if (ctrl.loaded.size === 0) continue;
        affectedTabs += ctrl.tabs.size;
        jobs.push(enqueue(ctrl, async () => {
            unloadAll(ctrl, 'reload requested');
            if (isEnabled(cfg) && ctrl.tabs.size > 0) {
                await syncController(ctrl, getWorkspaceFolders(cfg, ctrl.workspace));
            }
        }));
    }
    await Promise.all(jobs);
    return { affectedTabs };
}

async function removeExtension(folder) {
    if (!isValidFolderName(folder)) return { error: 'Invalid extension folder' };
    const cfg = getConfig();
    const records = { ...(cfg.extensions_bundled || {}) };
    if (records[folder]) records[folder] = { ...records[folder], removed: true };
    const byWorkspace = Object.fromEntries(Object.entries(cfg.extensions_by_workspace || {})
        .map(([ws, list]) => [ws, Array.isArray(list) ? list.filter(item => item !== folder) : []]));
    // Persist first so interrupted removals cannot reinstall bundled code.
    saveConfig({ ...cfg, extensions_bundled: records, extensions_by_workspace: byWorkspace,
        extensions_disabled: [...new Set([...(cfg.extensions_disabled || []), folder])] });
    await applyConfig();
    try {
        fs.rmSync(path.join(extensionsDir, folder), { recursive: true, force: true });
        return { removed: true };
    } catch (error) {
        return { error: error.message };
    }
}

// METRICS

// PSS splits shared pages between the processes that map them, so the sum is the
// real footprint. Falls back to the working set off Linux.
function readProcessMemoryKb(metric) {
    if (process.platform === 'linux') {
        try {
            const rollup = fs.readFileSync(`/proc/${metric.pid}/smaps_rollup`, 'utf8');
            const match = rollup.match(/^Pss:\s+(\d+)/m);
            if (match) return Number(match[1]);
        } catch (e) {}
    }
    return metric.memory && metric.memory.workingSetSize ? metric.memory.workingSetSize : 0;
}

function collectMetrics() {
    const { app, webContents } = electron();
    const metrics = app.getAppMetrics();
    const memoryByPid = new Map(metrics.map(m => [m.pid, readProcessMemoryKb(m)]));
    const ownedPids = new Set();
    const backgroundPages = new Map();

    for (const wc of webContents.getAllWebContents()) {
        let pid = 0;
        try { pid = wc.getOSProcessId(); } catch (e) {}
        if (!pid) continue;
        ownedPids.add(pid);
        if (wc.getType() === 'backgroundPage') {
            const host = (() => { try { return new URL(wc.getURL()).host; } catch (e) { return ''; } })();
            backgroundPages.set(`${host}|${pid}`, { host, pid, session: wc.session });
        }
    }

    // Renderer processes owned by no WebContents are extension service workers
    const workerPids = metrics.filter(m => m.type === 'Tab' && !ownedPids.has(m.pid)).map(m => m.pid);
    const totalKb = metrics.reduce((sum, m) => sum + (memoryByPid.get(m.pid) || 0), 0);
    const workerKb = workerPids.reduce((sum, pid) => sum + (memoryByPid.get(pid) || 0), 0);
    return { memoryByPid, backgroundPages, totalKb, workerKb, workerProcesses: workerPids.length };
}

function getRuntimeState(withMetrics) {
    const metrics = withMetrics ? collectMetrics() : null;
    const toMb = (kb) => Math.round((kb / 1024) * 10) / 10;
    const workspaces = [];

    for (const ctrl of controllers.values()) {
        let running = {};
        try {
            for (const info of Object.values(ctrl.session.serviceWorkers.getAllRunning())) {
                const match = /^chrome-extension:\/\/([a-p]{32})\//.exec(info.scope || '');
                if (match) running[match[1]] = true;
            }
        } catch (e) {}

        const loaded = [...ctrl.loaded].map(([folder, entry]) => {
            const item = { folder, id: entry.id, name: entry.name, loadMs: entry.loadMs, workerRunning: !!running[entry.id] };
            if (metrics) {
                for (const page of metrics.backgroundPages.values()) {
                    if (page.host === entry.id && page.session === ctrl.session) {
                        item.backgroundPageMb = toMb(metrics.memoryByPid.get(page.pid) || 0);
                    }
                }
            }
            return item;
        });

        workspaces.push({
            workspace: ctrl.workspace,
            partition: ctrl.partition,
            state: ctrl.loaded.size === 0 ? 'unloaded' : ctrl.graceTimer ? 'grace' : 'active',
            liveTabs: ctrl.tabs.size,
            loaded,
            errors: { ...ctrl.errors }
        });
    }

    return {
        workspaces,
        metrics: metrics ? {
            totalMb: toMb(metrics.totalKb),
            serviceWorkerMb: toMb(metrics.workerKb),
            serviceWorkerProcesses: metrics.workerProcesses,
            method: process.platform === 'linux' ? 'PSS' : 'working set'
        } : null
    };
}

// IPC

function initializeExtensions(options) {
    const { ipcMain, shell } = electron();
    extensionsDir = path.join(options.configDir, 'extensions');
    getConfig = options.getConfig;
    saveConfig = options.saveConfig;
    setNetworkExtensions = options.setNetworkExtensions || setNetworkExtensions;
    getMainWindow = options.getMainWindow;
    partitionForWorkspace = options.getWorkspacePartition;
    logEvent = options.logEvent || logEvent;
    if (isEnabled(getConfig())) provisionBundles();

    const fromMainWindow = (event) => {
        const win = getMainWindow();
        return !!(win && !win.isDestroyed() && event.sender === win.webContents);
    };
    const guard = (handler) => (event, ...args) => (fromMainWindow(event) ? handler(...args) : null);

    ipcMain.handle('extensions-ensure', guard((workspace, partition) => ensureForWorkspace(String(workspace || ''), String(partition || ''))));
    ipcMain.handle('extensions-rename-workspace', guard((from, to) => renameWorkspace(String(from), String(to))));
    ipcMain.on('extensions-tab-focused', (event, wcId) => { if (fromMainWindow(event)) focusTab(wcId); });
    ipcMain.handle('extensions-activate', guard((extensionId, partition) => resolveActionTarget(String(extensionId), String(partition))));
    ipcMain.handle('extensions-run-command', guard((extensionId, partition, name) => runCommand(String(extensionId), String(partition), String(name))));
    ipcMain.handle('extensions-get-actions', guard((partition) => getActionStates(String(partition))));
    // The renderer asks for the cheap form at startup and the full form only while
    // the Preferences section is open.
    ipcMain.handle('extensions-get-state', guard((options = {}) => {
        const installed = options.scan ? scanExtensions() : [];
        const cfg = getConfig();
        return {
            bundled: cfg.extensions_bundled || {},
            disabled: cfg.extensions_disabled || [],
            enabled: isEnabled(cfg),
            directory: extensionsDir,
            byWorkspace: (cfg && cfg.extensions_by_workspace) || {},
            graceSeconds: getGraceMs(cfg) / 1000,
            installed,
            runtime: getRuntimeState(!!options.metrics)
        };
    }));
    ipcMain.handle('extensions-remove', guard((folder) => removeExtension(String(folder || ''))));
    ipcMain.handle('extensions-apply-config', guard(() => applyConfig()));
    ipcMain.handle('extensions-reload', guard(() => reloadAll()));
    ipcMain.handle('extensions-open-dir', guard(async () => {
        try { fs.mkdirSync(extensionsDir, { recursive: true }); } catch (e) {}
        return shell.openPath(extensionsDir);
    }));
}

module.exports = {
    initializeExtensions,
    handleWebviewAttached,
    getContextMenuItems,
    // Exported for tests
    deduplicateHostObservers,
    computeExtensionId,
    classifyBackground,
    collectWarnings,
    describeExtension,
    localise,
    pickIconPath,
    isEnabled,
    isEligiblePartition,
    isValidFolderName,
    getWorkspaceFolders,
    getGraceMs,
    provisionBundledExtensions
};
