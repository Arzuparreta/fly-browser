// Fly owns installs, storage identity and workspace lifetime for every source.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const STORE_ORIGIN = 'https://chromewebstore.google.com';
const registeredSessions = new WeakSet();
let runtime;

function registerStoreSession(session) {
    if (!session.isPersistent() || registeredSessions.has(session)) return;
    registeredSessions.add(session);
    session.registerPreloadScript({ id: 'fly-chrome-web-store', type: 'frame',
        filePath: path.join(__dirname, 'extension-store.preload.js') });
}

function initializeStore(options) {
    const { ipcMain, dialog, app, session } = require('electron');
    const { getConfig, saveConfig, directory, getMainWindow, getWorkspaces, getWorkspacePartition } = options;
    let tail = Promise.resolve();
    const serialize = fn => {
        const job = tail.then(fn);
        tail = job.catch(() => {});
        return job;
    };
    const prompt = async details => {
        const win = getMainWindow();
        if (!win || win.isDestroyed()) return false;
        const result = await dialog.showMessageBox(win, { type: 'question', buttons: ['Cancel', details.button || 'Install'],
            defaultId: 0, cancelId: 0, noLink: true, title: details.title, message: details.message, detail: details.detail });
        return result.response === 1;
    };
    const workspaceForSession = target => Object.keys(getWorkspaces()).find(name =>
        session.fromPartition(getWorkspacePartition(name)) === target);
    const mainSender = event => {
        const win = getMainWindow();
        return win && !win.isDestroyed() && event.sender === win.webContents && event.senderFrame === event.sender.mainFrame;
    };
    const storeSender = event => {
        const win = getMainWindow();
        return win && !win.isDestroyed() && event.sender.getType() === 'webview' &&
            event.sender.hostWebContents === win.webContents && event.senderFrame === event.sender.mainFrame &&
            event.senderFrame.origin === STORE_ORIGIN && workspaceForSession(event.sender.session) !== undefined;
    };
    const info = folder => options.describe(folder, path.join(directory, folder));
    const installed = () => options.scan().filter(item => !item.error);
    const findStore = id => installed().find(item => item.id === id);
    const notify = () => {
        const win = getMainWindow();
        if (win && !win.isDestroyed()) win.webContents.send('master-shortcut', 'extensions-installed-changed');
    };

    async function install({ input, kind, workspace, updateFolder }) {
        const packages = require('./extensionPackages');
        if (!Object.hasOwn(getWorkspaces(), workspace)) throw new Error('Choose an existing workspace to install extensions. Private browsing is not supported.');
        fs.mkdirSync(directory, { recursive: true });
        const temporary = fs.mkdtempSync(path.join(directory, '.install-'));
        try {
            const source = kind === 'store' ? { type: 'store', id: packages.storeId(input) } : { type: 'local', kind };
            let unpacked;
            if (kind === 'store') {
                const bytes = await packages.downloadStorePackage(source.id, process.versions.chrome);
                unpacked = packages.unpackPackage(bytes, path.join(temporary, 'unpacked'), source.id);
            } else if (kind === 'folder') {
                unpacked = packages.copyUnpacked(input, path.join(temporary, 'unpacked'));
            } else {
                if (fs.statSync(input).size > packages.MAX_PACKAGE) throw new Error('Extension package exceeds 64 MB.');
                unpacked = packages.unpackPackage(fs.readFileSync(input), path.join(temporary, 'unpacked'));
            }
            const { root, manifest } = unpacked;
            const keyedId = manifest.key ? packages.extensionId(Buffer.from(manifest.key, 'base64')) : null;
            const existing = keyedId && installed().find(item => item.id === keyedId);
            const folder = updateFolder || existing?.folder || (kind === 'store' ? source.id :
                `local-${crypto.createHash('sha256').update(path.resolve(input)).digest('hex').slice(0, 24)}`);
            const destination = path.join(directory, folder);
            const previous = fs.existsSync(destination) ? info(folder) : null;
            if (previous?.error) throw new Error('An invalid extension already occupies this location. Remove it first.');
            const nextId = keyedId || packages.extensionId(Buffer.from(destination));
            if (previous && previous.id !== nextId) throw new Error('The new package has a different extension identity.');
            if (updateFolder && previous?.version === manifest.version) return { unchanged: true, name: previous.name };
            if (updateFolder && previous && compareVersions(manifest.version, previous.version) < 0) {
                throw new Error('The store returned an older version; the installed extension was kept.');
            }
            const described = options.describe(folder, root);
            const required = [...(manifest.permissions || []), ...(manifest.host_permissions || []),
                ...(manifest.content_scripts || []).flatMap(script => script.matches || [])];
            const optional = [...(manifest.optional_permissions || []), ...(manifest.optional_host_permissions || [])];
            const accepted = await prompt({ title: previous ? 'Update extension' : 'Install extension',
                button: previous ? 'Update' : 'Install', message: `${described.name} ${manifest.version}`,
                detail: [`Source: ${kind === 'store' ? 'Chrome Web Store' : path.basename(input)}`,
                    previous ? 'Existing workspace selections and settings will be preserved.' : `Active in: ${workspace}. Other workspaces can be selected in Preferences → Extensions.`,
                    !getConfig().extensions_enabled ? 'Chrome extensions are switched off. The extension will stay inactive until you enable them.' : '',
                    `Permissions and site access:\n${[...new Set(required)].join('\n') || 'None requested'}`,
                    optional.length ? `Optional permissions:\n${[...new Set(optional)].join('\n')}` : '',
                    ...(described.warnings || []), 'Already open tabs need a reload to apply changed content scripts.'].filter(Boolean).join('\n\n') });
            if (!accepted) return { cancelled: true };

            const cfg = getConfig();
            const sources = { ...(cfg.extensions_sources || {}), [folder]: source };
            const byWorkspace = { ...(cfg.extensions_by_workspace || {}) };
            if (!previous) {
                const defaults = Object.entries(cfg.extensions_bundled || {}).filter(([, entry]) => entry && !entry.removed).map(([name]) => name);
                byWorkspace[workspace] = [...new Set([...(byWorkspace[workspace] || defaults), folder])];
            }
            const bundled = { ...(cfg.extensions_bundled || {}) };
            if (bundled[folder]) bundled[folder] = { ...bundled[folder], managed: false, removed: false };
            const patch = { extensions_sources: sources, extensions_by_workspace: byWorkspace, extensions_bundled: bundled,
                extensions_disabled: previous ? (cfg.extensions_disabled || []) : (cfg.extensions_disabled || []).filter(name => name !== folder) };
            const backup = path.join(temporary, 'previous');
            await options.unload(folder);
            let swapped = false;
            try {
                if (previous) fs.renameSync(destination, backup);
                fs.renameSync(root, destination);
                swapped = true;
                saveConfig(patch);
                await options.apply();
                // Controllers only exist for workspaces that opted into extensions.
                // Ensure the installation workspace also receives its first extension.
                await options.ensure(workspace, getWorkspacePartition(workspace));
                const error = options.loadError(folder);
                if (error) throw new Error(error);
            } catch (error) {
                await options.unload(folder);
                if (swapped) fs.rmSync(destination, { recursive: true, force: true });
                if (fs.existsSync(backup)) fs.renameSync(backup, destination);
                saveConfig({ extensions_sources: cfg.extensions_sources || {}, extensions_by_workspace: cfg.extensions_by_workspace || {},
                    extensions_bundled: cfg.extensions_bundled || {}, extensions_disabled: cfg.extensions_disabled || [] });
                await options.apply();
                throw error;
            }
            notify();
            return { installed: true, folder, id: info(folder).id, name: described.name, version: manifest.version };
        } finally {
            fs.rmSync(temporary, { recursive: true, force: true });
        }
    }

    const safe = async fn => { try { return await fn(); } catch (error) { return { error: error.message }; } };
    ipcMain.handle('extensions-install', (event, request = {}) => {
        if (!mainSender(event)) return null;
        return safe(() => serialize(async () => {
            if (!['store', 'folder', 'package'].includes(request.kind)) throw new Error('Unknown extension source.');
            let input = request.input;
            if (request.kind !== 'store') {
                const result = await dialog.showOpenDialog(getMainWindow(), { title: request.kind === 'folder' ? 'Select unpacked Chrome extension' : 'Select extension package',
                    properties: [request.kind === 'folder' ? 'openDirectory' : 'openFile'],
                    ...(request.kind === 'package' ? { filters: [{ name: 'Chrome extensions', extensions: ['zip', 'crx'] }] } : {}) });
                if (result.canceled || !result.filePaths[0]) return { cancelled: true };
                input = result.filePaths[0];
            }
            return install({ input, kind: request.kind, workspace: request.workspace });
        }));
    });
    ipcMain.handle('extensions-update', (event, folder, workspace) => {
        if (!mainSender(event)) return null;
        return safe(() => serialize(async () => {
            const source = getConfig().extensions_sources?.[folder];
            if (source?.type !== 'store') throw new Error('For local extensions, select the updated package or original folder again.');
            if (!options.validFolder(folder) || !fs.existsSync(path.join(directory, folder))) throw new Error('Extension is no longer installed.');
            return install({ input: source.id, kind: 'store', workspace, updateFolder: folder });
        }));
    });

    // The remote store receives Chrome's public management shape, never local
    // profile paths, folder names, source records or Fly's runtime diagnostics.
    const managementInfo = item => ({ id: item.id, name: item.name, version: item.version,
        description: '', shortName: item.name, enabled: item.enabled && !!getConfig().extensions_enabled,
        type: 'extension', installType: 'normal', mayDisable: true, isApp: false, icons: [],
        permissions: [], hostPermissions: [], optionsUrl: item.optionsPage ? `chrome-extension://${item.id}/${item.optionsPage}` : '' });
    const handles = {
        'chromeWebstore.beginInstall': (event, details) => serialize(async () => {
            try {
                const result = await install({ input: details?.id, kind: 'store', workspace: workspaceForSession(event.sender.session) });
                if (result.cancelled) return { result: 'user_cancelled' };
                const item = findStore(result.id);
                if (item && !event.senderFrame.isDestroyed()) event.senderFrame.send('chrome.management.onInstalled', managementInfo(item));
                return { result: 'success' };
            } catch (error) { return { result: 'install_error', message: error.message }; }
        }),
        'chromeWebstore.completeInstall': (_event, id) => findStore(id) ? 'success' : 'install_error',
        'chromeWebstore.install': (event, id) => handles['chromeWebstore.beginInstall'](event, { id }).then(result => result.result),
        'chromeWebstore.getExtensionStatus': (_event, id) => {
            const item = findStore(id);
            return item ? (item.enabled && getConfig().extensions_enabled ? 'enabled' : 'disabled') : 'installable';
        },
        'chromeWebstore.getFullChromeVersion': () => ({ version_number: process.versions.chrome, app_name: 'Fly' }),
        'chromeWebstore.getMV2DeprecationStatus': () => 'inactive',
        'chromeWebstore.getBrowserLogin': () => '',
        'chromeWebstore.getStoreLogin': () => '',
        'chromeWebstore.setStoreLogin': () => true,
        'chromeWebstore.getIsLauncherEnabled': () => false,
        'chromeWebstore.enableAppLauncher': () => false,
        'chromeWebstore.getReferrerChain': () => '',
        'chromeWebstore.getWebGLStatus': () => app.getGPUFeatureStatus().webgl?.startsWith('enabled') ? 'webgl_allowed' : 'webgl_blocked',
        'chromeWebstore.isInIncognitoMode': () => false,
        'chromeWebstore.isPendingCustodianApproval': () => false,
        'chrome.management.getAll': () => installed().map(managementInfo),
        'chrome.management.setEnabled': (event, id, enabled) => serialize(async () => {
            const item = findStore(id);
            if (!item) return false;
            const disabled = new Set(getConfig().extensions_disabled || []);
            if (enabled) disabled.delete(item.folder); else disabled.add(item.folder);
            saveConfig({ extensions_disabled: [...disabled] });
            await options.apply();
            notify(); return true;
        }),
        'chrome.management.uninstall': (event, id) => serialize(async () => {
            const item = findStore(id);
            if (!item || !(await prompt({ title: 'Remove extension', button: 'Remove', message: `Remove ${item.name}?`, detail: 'This removes the extension from every workspace. Open tabs keep injected scripts until reloaded.' }))) return 'user_cancelled';
            const result = await options.remove(item.folder);
            if (result.error) throw new Error(result.error);
            if (!event.sender.isDestroyed()) event.sender.send('chrome.management.onUninstalled', id);
            notify(); return 'success';
        })
    };
    for (const [channel, handler] of Object.entries(handles)) {
        ipcMain.handle(channel, (event, ...args) => {
            if (!storeSender(event)) return null;
            return handler(event, ...args);
        });
    }
    runtime = { serialize };
}

function compareVersions(a, b) {
    const left = a.split('.').map(Number), right = b.split('.').map(Number);
    for (let i = 0; i < 4; i++) {
        const delta = (left[i] || 0) - (right[i] || 0);
        if (delta) return Math.sign(delta);
    }
    return 0;
}

module.exports = { initializeStore, registerStoreSession, compareVersions,
    serialize: fn => runtime ? runtime.serialize(fn) : fn() };
