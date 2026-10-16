// Renderer side of Chrome extension support. See extensions.js in the main
// process for the lifecycle. With extensions disabled, every entry point here
// returns after a single property check.

import { state } from './state.js';
import { getWorkspacePartition, escapeHtml } from './utils.js';
import { setTargetUrl } from './statusBar.js';
import { spawnTabWithUrl, switchTabFocus, handleTabRemoval } from './webview.js';

const POPUP_MIN = { width: 200, height: 60 };
const POPUP_MAX = { width: 800, height: 600 };
const POPUP_MEASURE_DELAYS_MS = [0, 150, 400, 900, 1800];

const BACKGROUND_LABELS = {
    none: { text: 'No background', cls: 'ext-kind-light', title: 'Content scripts only. No process of its own.' },
    worker: { text: 'Service worker', cls: 'ext-kind-normal', title: 'Runs a process only while handling events. Chromium stops it after about 30 seconds idle.' },
    event: { text: 'Event page', cls: 'ext-kind-normal', title: 'MV2 event page. Loaded on demand.' },
    persistent: { text: 'Persistent', cls: 'ext-kind-heavy', title: 'MV2 background page that keeps a process alive for as long as it is loaded.' }
};

let installedCache = [];
let popupState = null;
let installBusy = false;

export async function handleExtensionsInstalledChanged() {
    installedCache = [];
    setExtensionsConfig(await window.flyAPI.getBrowserSettings());
    await ensureLiveWorkspaces();
    if (!document.getElementById('SettingsPane-extensions')?.hidden) await syncExtensionsPreferences();
    await refreshStatusBarActions();
}

async function runInstall(operation) {
    if (installBusy) return;
    installBusy = true;
    const status = document.getElementById('ExtensionsInstallStatus');
    const buttons = document.querySelectorAll('.ext-install-actions button, .ext-update-btn');
    buttons.forEach(button => { button.disabled = true; });
    if (status) status.textContent = 'Preparing extension… A permission dialog will appear before installation.';
    try {
        const result = await operation();
        if (status) status.textContent = result?.error ? result.error : result?.cancelled ? 'Installation cancelled.' : result?.unchanged
            ? `${result.name} is up to date.` : `${result?.name || 'Extension'} installed. Reload already open tabs to apply its content scripts.`;
        await handleExtensionsInstalledChanged();
    } catch (error) {
        if (status) status.textContent = error.message;
    } finally {
        installBusy = false;
        document.querySelectorAll('.ext-install-actions button, .ext-update-btn').forEach(button => { button.disabled = false; });
    }
}

// CONFIG STATE

export function setExtensionsConfig(cfg) {
    state.extensions = {
        enabled: !!(cfg && cfg.extensions_enabled === true),
        byWorkspace: (cfg && cfg.extensions_by_workspace) || {},
        bundled: (cfg && cfg.extensions_bundled) || {},
        disabled: (cfg && cfg.extensions_disabled) || []
    };
}

// Mirrors renameWorkspace in extensions.js. Called before the renamed workspace's tabs are
// re-created, so they and the main process see the selection under the new name.
export function renameWorkspaceExtensions(from, to) {
    const ext = state.extensions;
    if (ext) {
        const byWorkspace = { ...ext.byWorkspace };
        const list = byWorkspace[from];
        delete byWorkspace[from];
        delete byWorkspace[to];
        if (list !== undefined) byWorkspace[to] = list;
        ext.byWorkspace = byWorkspace;
    }
    window.flyAPI.extensionsRenameWorkspace(from, to).catch(() => {});
}

function workspaceFolders(workspace, ext = state.extensions) {
    if (!ext) return [];
    const defaults = Object.entries(ext.bundled || {}).filter(([, entry]) => entry && !entry.removed).map(([folder]) => folder);
    const list = Array.isArray(ext.byWorkspace[workspace]) ? ext.byWorkspace[workspace] : defaults;
    return list.filter(folder => !(ext.disabled || []).includes(folder));
}

export function workspaceHasExtensions(workspace) {
    const ext = state.extensions;
    if (!ext || !ext.enabled) return false;
    const list = workspaceFolders(workspace, ext);
    return Array.isArray(list) && list.length > 0;
}

function currentPartition() {
    return getWorkspacePartition(state.sessionState.current_workspace);
}

function isPersistentPartition(partition) {
    return typeof partition === 'string' && partition.startsWith('persist:');
}

// TAB LIFECYCLE HOOKS

// Sets the webview source once the workspace's extensions are loaded, so content
// scripts declared for document_start apply to the first navigation. Without
// extensions for this workspace the source is set synchronously, as before.
export function loadWebviewSource(webview, workspace, src) {
    const navigation = {};
    webview.__flyPendingNavigation = navigation;
    const partition = webview.getAttribute('partition');
    if (!isPersistentPartition(partition) || !workspaceHasExtensions(workspace)) {
        webview.setAttribute('src', src);
        return;
    }

    const finish = () => {
        // A tab can be closed while its extension starts.
        if (webview.isConnected && webview.__flyPendingNavigation === navigation) webview.setAttribute('src', src);
    };
    window.flyAPI.extensionsEnsure(workspace, partition).then(finish, finish);
}

// Called whenever the active view changes so chrome.tabs.query({ active: true })
// and the status bar reflect the tab the user is looking at.
export function notifyActiveView(webview, workspace) {
    if (!workspaceHasExtensions(workspace)) {
        if (state.extensions && state.extensions.enabled) {
            hideStatusBarActions();
            try { window.flyAPI.extensionsTabFocused(0); } catch (e) {}
        }
        return;
    }
    if (webview) {
        const send = () => {
            try { window.flyAPI.extensionsTabFocused(webview.getWebContentsId()); } catch (e) {}
            refreshStatusBarActions();
        };
        try {
            webview.getWebContentsId();
            send();
        } catch (e) {
            webview.addEventListener('did-attach', send, { once: true });
        }
    } else {
        refreshStatusBarActions();
    }
}

function findTabByWebContentsId(wcId) {
    for (const [ws, views] of Object.entries(state.activeViewsCache || {})) {
        if (!Array.isArray(views)) continue;
        for (let i = 0; i < views.length; i++) {
            const wv = views[i];
            if (!wv) continue;
            try {
                if (wv.getWebContentsId() === wcId) return { ws, idx: i };
            } catch (e) {}
        }
    }
    return null;
}

export function handleExtensionSelectTab(wcId) {
    const target = findTabByWebContentsId(wcId);
    if (!target || target.ws !== state.sessionState.current_workspace) return;
    const selected = document.querySelector('#TabList li.selected');
    const currentIdx = selected ? Array.from(document.querySelectorAll('#TabList li')).indexOf(selected) : -1;
    if (currentIdx !== target.idx) switchTabFocus(target.idx);
}

export function handleExtensionCloseTab(wcId) {
    const target = findTabByWebContentsId(wcId);
    if (!target || target.ws !== state.sessionState.current_workspace) return;
    switchTabFocus(target.idx);
    handleTabRemoval();
}

// After a settings change, workspaces with live tabs load or drop extensions now
// instead of waiting for their next new tab.
async function ensureLiveWorkspaces() {
    const jobs = [];
    for (const [ws, views] of Object.entries(state.activeViewsCache || {})) {
        if (!workspaceHasExtensions(ws)) continue;
        if (!Array.isArray(views) || !views.some(Boolean)) continue;
        jobs.push(window.flyAPI.extensionsEnsure(ws, getWorkspacePartition(ws)).catch(() => {}));
    }
    await Promise.all(jobs);
}

// STATUS BAR

function hideStatusBarActions() {
    const bar = document.getElementById('StatusBarExtensions');
    if (bar) {
        bar.hidden = true;
        bar.innerHTML = '';
    }
}

async function ensureInstalledCache() {
    if (installedCache.length) return installedCache;
    try {
        const res = await window.flyAPI.extensionsGetState({ scan: true });
        installedCache = (res && res.installed) || [];
    } catch (e) {}
    return installedCache;
}

export async function refreshStatusBarActions() {
    const bar = document.getElementById('StatusBarExtensions');
    if (!bar) return;
    const workspace = state.sessionState.current_workspace;
    if (!workspaceHasExtensions(workspace)) {
        hideStatusBarActions();
        return;
    }

    let actions = [];
    try { actions = (await window.flyAPI.extensionsGetActions(currentPartition())) || []; } catch (e) {}
    const installed = await ensureInstalledCache();
    const visible = actions.filter(a => installed.find(i => i.id === a.id && i.hasAction));

    if (!visible.length) {
        hideStatusBarActions();
        return;
    }

    bar.innerHTML = visible.map(action => {
        const info = installed.find(i => i.id === action.id) || {};
        const icon = info.icon
            ? `<img src="${info.icon}" alt="">`
            : '<i class="fa-solid fa-puzzle-piece"></i>';
        const badge = action.badgeText
            ? `<span class="status-extension-badge"${action.badgeColor ? ` style="background:${escapeHtml(String(action.badgeColor))}"` : ''}>${escapeHtml(String(action.badgeText))}</span>`
            : '';
        return `<button type="button" class="status-extension-btn" data-extension-id="${escapeHtml(action.id)}" title="${escapeHtml(action.title || info.name || '')}">${icon}${badge}</button>`;
    }).join('');
    bar.hidden = false;

    bar.querySelectorAll('.status-extension-btn').forEach(btn => {
        btn.onclick = () => activateExtension(btn.dataset.extensionId, btn.getBoundingClientRect());
    });
}

// POPUPS

export function closeExtensionPopup() {
    if (!popupState) return;
    const { overlay } = popupState;
    popupState = null;
    try { overlay.remove(); } catch (e) {}
}

function placePopup(container, anchorRect) {
    const statusBar = document.getElementById('StatusBar');
    const barHeight = statusBar && statusBar.offsetParent !== null ? statusBar.offsetHeight : 0;
    container.style.bottom = `${barHeight + 8}px`;
    if (anchorRect) {
        const right = Math.max(8, window.innerWidth - anchorRect.right);
        container.style.right = `${right}px`;
    } else {
        container.style.right = '12px';
    }
}

async function measurePopup(webview) {
    if (!popupState || popupState.webview !== webview) return;
    try {
        const [w, h] = await webview.executeJavaScript('[document.documentElement.scrollWidth, document.documentElement.scrollHeight]');
        const width = Math.min(POPUP_MAX.width, window.innerWidth - 26, Math.max(POPUP_MIN.width, Number(w) || 0));
        const height = Math.min(POPUP_MAX.height, window.innerHeight - 80, Math.max(POPUP_MIN.height, Number(h) || 0));
        webview.style.width = `${width}px`;
        webview.style.height = `${height}px`;
    } catch (e) {}
}

function showPopup(url, partition, anchorRect) {
    closeExtensionPopup();

    const overlay = document.createElement('div');
    overlay.id = 'ExtensionPopupOverlay';
    const container = document.createElement('div');
    container.className = 'extension-popup';
    const webview = document.createElement('webview');
    webview.setAttribute('partition', partition);
    webview.style.width = `${POPUP_MIN.width}px`;
    webview.style.height = `${POPUP_MIN.height}px`;

    overlay.addEventListener('mousedown', (e) => {
        if (e.target === overlay) closeExtensionPopup();
    });
    webview.addEventListener('close', closeExtensionPopup);
    webview.addEventListener('did-finish-load', () => {
        POPUP_MEASURE_DELAYS_MS.forEach(delay => setTimeout(() => measurePopup(webview), delay));
        webview.focus();
    });

    // src is set before attaching so the main process can tell the popup from a tab
    webview.setAttribute('src', url);
    container.appendChild(webview);
    overlay.appendChild(container);
    placePopup(container, anchorRect);
    document.body.appendChild(overlay);
    popupState = { overlay, webview };
}

export async function activateExtension(extensionId, anchorRect = null) {
    const partition = currentPartition();
    let res = null;
    try { res = await window.flyAPI.extensionsActivate(extensionId, partition); } catch (e) {}
    if (!res || res.error) {
        showStatusNote((res && res.error) || 'Extension is not available in this workspace.');
        return;
    }
    if (res.kind === 'popup') showPopup(res.url, res.partition, anchorRect);
    if (res.kind === 'options') {
        closeExtensionPopup();
        const info = (await ensureInstalledCache()).find(item => item.id === extensionId);
        if (info?.optionsPage) openExtensionOptions(info);
        else openExtensionsPreferences();
    }
    setTimeout(refreshStatusBarActions, 300);
}

function showStatusNote(text) {
    setTargetUrl(text, 3000);
}

// COMMAND PALETTE

// Awaited when the palette opens so per-extension entries are present on first use
export async function prepareExtensionPaletteCommands() {
    if (!workspaceHasExtensions(state.sessionState.current_workspace)) return;
    await ensureInstalledCache();
}

export function getExtensionPaletteCommands() {
    const commands = [{
        id: 'extensions-open-settings',
        title: 'Extension Settings',
        desc: 'Enable Chrome extensions per workspace and see what they cost',
        details: 'Opens the Extensions section in Preferences. uBlock Origin is enabled by default in new profiles. Extensions load in selected workspaces and unload when those workspaces go idle.',
        tip: 'Install from Chrome Web Store, a ZIP or CRX package, or an unpacked folder.',
        category: 'Extensions',
        icon: 'fa-solid fa-puzzle-piece',
        keywords: ['extensions', 'addons', 'chrome', 'plugins', 'settings'],
        action: () => openExtensionsPreferences()
    }];

    if (!state.extensions || !state.extensions.enabled) return commands;

    commands.push({
        id: 'extensions-open-dir',
        title: 'Open Extensions Directory',
        desc: 'Browse ~/.config/fly-browser/extensions in file manager',
        details: 'Each unpacked extension goes in its own folder with its manifest.json at the top level.',
        tip: 'Run "Reload Extensions" after updating an extension on disk.',
        category: 'Extensions',
        icon: 'fa-solid fa-folder-open',
        keywords: ['extensions', 'folder', 'directory', 'unpacked'],
        action: () => window.flyAPI.extensionsOpenDir()
    }, {
        id: 'extensions-reload',
        title: 'Reload Extensions',
        desc: 'Re-read extension code from disk in every workspace using it',
        details: 'Unloads and reloads extensions in workspaces with live tabs. Pages that already ran the old content scripts need a reload.',
        tip: 'Use this after replacing an extension folder with a newer version.',
        category: 'Extensions',
        icon: 'fa-solid fa-rotate',
        keywords: ['extensions', 'reload', 'refresh', 'update'],
        action: async () => {
            installedCache = [];
            const res = await window.flyAPI.extensionsReload();
            showStatusNote(res && res.affectedTabs ? `Extensions reloaded. Reload ${res.affectedTabs} open tab(s) to refresh content scripts.` : 'Extensions reloaded.');
            refreshStatusBarActions();
        }
    });

    const workspace = state.sessionState.current_workspace;
    if (!workspaceHasExtensions(workspace)) return commands;
    const enabledFolders = workspaceFolders(workspace);

    for (const info of installedCache.filter(i => enabledFolders.includes(i.folder))) {
        if (info.hasAction) {
            commands.push({
                id: `extension-activate-${info.id}`,
                title: `${info.name}: ${info.hasPopup ? 'Open Popup' : 'Open Settings'}`,
                desc: info.hasPopup ? 'Open the extension popup for the active tab' : 'Open extension options or extension preferences',
                details: `${info.name} ${info.version}`,
                tip: 'Press Esc or click outside the popup to close it.',
                category: 'Extensions',
                icon: 'fa-solid fa-puzzle-piece',
                keywords: ['extension', 'popup', info.name.toLowerCase()],
                action: () => activateExtension(info.id)
            });
        }
        for (const cmd of info.commands || []) {
            commands.push({
                id: `extension-command-${info.id}-${cmd.name}`,
                title: `${info.name}: ${cmd.description}`,
                desc: 'Extension keyboard command, dispatched to the active tab',
                details: `Sends chrome.commands.onCommand("${cmd.name}") to ${info.name}.`,
                tip: 'Bind it to a key through the extension if it supports custom shortcuts.',
                category: 'Extensions',
                icon: 'fa-solid fa-keyboard',
                keywords: ['extension', 'command', info.name.toLowerCase(), cmd.name.toLowerCase()],
                action: async () => {
                    const ok = await window.flyAPI.extensionsRunCommand(info.id, currentPartition(), cmd.name);
                    if (!ok) showStatusNote(`${info.name} is not loaded in this workspace yet.`);
                }
            });
        }
        if (info.optionsPage) {
            commands.push({
                id: `extension-options-${info.id}`,
                title: `${info.name}: Options`,
                desc: 'Open the extension options page in a new tab',
                details: 'Options are stored per workspace, like everything else the extension saves.',
                tip: 'The options tab counts as a live tab and keeps the extension loaded while open.',
                category: 'Extensions',
                icon: 'fa-solid fa-gear',
                keywords: ['extension', 'options', 'settings', info.name.toLowerCase()],
                action: () => openExtensionOptions(info)
            });
        }
    }
    return commands;
}

function openExtensionOptions(info) {
    const page = String(info.optionsPage || '').replace(/^\//, '');
    if (!page) return;
    spawnTabWithUrl(`chrome-extension://${info.id}/${page}`);
}

// PREFERENCES

function openExtensionsPreferences() {
    if (typeof window.openPreferencesPane === 'function') window.openPreferencesPane('extensions');
}

function formatMb(value) {
    return typeof value === 'number' ? `${value.toFixed(1)} MB` : '';
}

function renderRuntimeSummary(runtime) {
    const target = document.getElementById('ExtensionsRuntimeSummary');
    if (!target) return;
    if (!runtime) {
        target.innerHTML = '';
        return;
    }
    const rows = runtime.workspaces.map(ws => {
        const stateLabel = ws.state === 'grace' ? 'unloading soon' : ws.state;
        const loaded = ws.loaded.map(l => {
            const parts = [escapeHtml(l.name || l.folder)];
            if (l.backgroundPageMb !== undefined) parts.push(`background page ${formatMb(l.backgroundPageMb)}`);
            if (l.workerRunning) parts.push('service worker running');
            return parts.join(' · ');
        }).join('<br>') || 'Nothing loaded';
        return `<div class="ext-runtime-row"><span class="ext-runtime-ws">${escapeHtml(ws.workspace)}</span><span class="ext-runtime-state ext-state-${escapeHtml(ws.state)}">${escapeHtml(stateLabel)}</span><span class="ext-runtime-tabs">${ws.liveTabs} live tab(s)</span><div class="ext-runtime-loaded">${loaded}</div></div>`;
    }).join('');

    const m = runtime.metrics;
    const totals = m
        ? `<div class="ext-runtime-totals">Fly total: <strong>${formatMb(m.totalMb)}</strong> · extension service workers: ${m.serviceWorkerProcesses} running, ${formatMb(m.serviceWorkerMb)} <span class="ext-runtime-method">(${escapeHtml(m.method)})</span></div>`
        : '';
    target.innerHTML = totals + (rows || '<div class="ext-runtime-empty">No workspace has loaded extensions since Fly started.</div>');
}

function renderInstalledList(res) {
    const list = document.getElementById('ExtensionsInstalledList');
    if (!list) return;
    const workspaces = Object.keys(state.sessionState.workspaces || {});
    if (!res.installed.length) {
        list.innerHTML = '<div class="ext-empty">No extensions installed. Browse Chrome Web Store or choose a package or folder above.</div>';
        return;
    }

    list.innerHTML = res.installed.map(info => {
        if (info.error) {
            return `<div class="ext-card ext-card-error"><div class="ext-card-head"><span class="ext-name">${escapeHtml(info.folder)}</span></div><div class="ext-warning">${escapeHtml(info.error)}</div></div>`;
        }
        const kind = BACKGROUND_LABELS[info.background] || BACKGROUND_LABELS.none;
        const icon = info.icon ? `<img class="ext-icon" src="${info.icon}" alt="">` : '<i class="fa-solid fa-puzzle-piece ext-icon"></i>';
        const warnings = (info.warnings || []).map(w => `<div class="ext-warning"><i class="fa-solid fa-triangle-exclamation"></i> ${escapeHtml(w)}</div>`).join('');
        const chips = workspaces.map(ws => {
            const checked = workspaceFolders(ws).includes(info.folder);
            return `<label class="ext-ws-chip"><input type="checkbox" data-folder="${escapeHtml(info.folder)}" data-workspace="${escapeHtml(ws)}"${checked ? ' checked' : ''}${res.enabled && info.enabled ? '' : ' disabled'}> ${escapeHtml(ws)}</label>`;
        }).join('');
        const options = info.optionsPage ? `<button type="button" class="settings-action-btn ext-options-btn" data-folder="${escapeHtml(info.folder)}">Options</button>` : '';
        const controls = `<label class="ext-ws-chip"><input type="checkbox" class="ext-enabled-toggle" data-folder="${escapeHtml(info.folder)}"${info.enabled ? ' checked' : ''}> Enabled</label><button type="button" class="settings-action-btn ext-remove-btn" data-folder="${escapeHtml(info.folder)}">Remove</button>`;
        const source = info.source?.type === 'store' ? `<a href="https://chromewebstore.google.com/detail/${escapeHtml(info.source.id)}" class="ext-source-link">Chrome Web Store</a> <button type="button" class="settings-action-btn ext-update-btn" data-folder="${escapeHtml(info.folder)}"${installBusy ? ' disabled' : ''}>Check for update</button>` : info.bundled ? 'Included with Fly' : 'Local installation · install the updated package or original folder again to update';
        const errors = res.runtime.workspaces.filter(ws => ws.errors?.[info.folder]).map(ws => `<div class="ext-warning">${escapeHtml(ws.workspace)}: ${escapeHtml(ws.errors[info.folder])}</div>`).join('');
        return `<div class="ext-card" data-extension-folder="${escapeHtml(info.folder)}">
            <div class="ext-card-head">${icon}<span class="ext-name">${escapeHtml(info.name)}</span><span class="ext-version">${escapeHtml(info.version)} · MV${escapeHtml(String(info.manifestVersion || '?'))}</span><span class="ext-kind ${kind.cls}" title="${escapeHtml(kind.title)}">${kind.text}</span>${info.bundled ? '<span class="ext-kind">Included</span>' : ''}${options}${controls}</div>
            <div class="ext-source">${source}</div>
            ${warnings}${errors}
            <div class="ext-ws-chips"><span class="ext-ws-label">Active in:</span>${chips}</div>
        </div>`;
    }).join('');

    list.querySelectorAll('.ext-source-link').forEach(link => {
        link.onclick = event => { event.preventDefault(); if (typeof window.togglePreferencesView === 'function') window.togglePreferencesView(); spawnTabWithUrl(link.href); };
    });
    list.querySelectorAll('.ext-update-btn').forEach(button => {
        button.onclick = () => runInstall(() => window.flyAPI.extensionsUpdate(button.dataset.folder, state.sessionState.current_workspace));
    });

    list.querySelectorAll('input[type="checkbox"][data-workspace]').forEach(box => {
        box.onchange = () => saveWorkspaceSelection(box.dataset.workspace, box.dataset.folder, box.checked);
    });
    list.querySelectorAll('.ext-enabled-toggle').forEach(box => {
        box.onchange = async () => {
            const current = (await window.flyAPI.getBrowserSettings()) || {};
            const disabled = new Set(current.extensions_disabled || []);
            if (box.checked) disabled.delete(box.dataset.folder); else disabled.add(box.dataset.folder);
            await persistConfig({ extensions_disabled: [...disabled] });
            await syncExtensionsPreferences();
            refreshStatusBarActions();
        };
    });
    list.querySelectorAll('.ext-remove-btn').forEach(btn => {
        btn.onclick = async () => {
            btn.disabled = true;
            const result = await window.flyAPI.extensionsRemove(btn.dataset.folder);
            setExtensionsConfig(await window.flyAPI.getBrowserSettings());
            installedCache = [];
            setPreferencesNote(result?.error || 'Extension removed. Reload open tabs to clear injected scripts.');
            await syncExtensionsPreferences();
            refreshStatusBarActions();
        };
    });
    list.querySelectorAll('.ext-options-btn').forEach(btn => {
        btn.onclick = () => {
            const info = res.installed.find(i => i.folder === btn.dataset.folder);
            if (!info) return;
            const enabledHere = workspaceHasExtensions(state.sessionState.current_workspace) &&
                workspaceFolders(state.sessionState.current_workspace).includes(info.folder);
            if (!enabledHere) {
                setPreferencesNote('Enable this extension for the current workspace to open its options.');
                return;
            }
            if (typeof window.togglePreferencesView === 'function') window.togglePreferencesView();
            openExtensionOptions(info);
        };
    });
}

function setPreferencesNote(text) {
    const note = document.getElementById('ExtensionsSettingsNote');
    if (!note) return;
    note.textContent = text;
    note.classList.add('visible');
    setTimeout(() => note.classList.remove('visible'), 4000);
}

export async function syncExtensionsPreferences() {
    const toggle = document.getElementById('setting-extensions-toggle');
    if (!toggle || !window.flyAPI || typeof window.flyAPI.extensionsGetState !== 'function') return;
    let res;
    try { res = await window.flyAPI.extensionsGetState({ scan: true, metrics: true }); } catch (e) { return; }
    if (!res) return;

    setExtensionsConfig({ extensions_enabled: res.enabled, extensions_by_workspace: res.byWorkspace,
        extensions_bundled: res.bundled, extensions_disabled: res.disabled });
    toggle.checked = !!res.enabled;
    installedCache = res.installed || [];
    const dirLabel = document.getElementById('ExtensionsDirectoryPath');
    if (dirLabel) dirLabel.textContent = res.directory;
    const grace = document.getElementById('ExtensionsGraceSeconds');
    if (grace) grace.textContent = String(res.graceSeconds);

    renderInstalledList(res);
    renderRuntimeSummary(res.enabled ? res.runtime : null);
}

async function persistConfig(patch) {
    const current = (await window.flyAPI.getBrowserSettings()) || {};
    const merged = { ...current, ...patch };
    await window.flyAPI.updateBrowserSettings(merged);
    setExtensionsConfig(merged);
    await window.flyAPI.extensionsApplyConfig();
    setExtensionsConfig(await window.flyAPI.getBrowserSettings());
    await ensureLiveWorkspaces();
}

async function saveWorkspaceSelection(workspace, folder, enabled) {
    const current = (await window.flyAPI.getBrowserSettings()) || {};
    const map = { ...(current.extensions_by_workspace || {}) };
    const list = new Set(Array.isArray(map[workspace]) ? map[workspace] : Object.entries(current.extensions_bundled || {}).filter(([, entry]) => entry && !entry.removed).map(([name]) => name));
    if (enabled) list.add(folder); else list.delete(folder);
    map[workspace] = [...list];

    await persistConfig({ extensions_by_workspace: map });
    setPreferencesNote(enabled
        ? 'Saved. Already open tabs in that workspace need a reload for content scripts.'
        : 'Saved. Unloaded from that workspace.');
    await syncExtensionsPreferences();
    refreshStatusBarActions();
}

export function setupExtensionsPreferencesListeners() {
    const browse = document.getElementById('setting-extensions-store-btn');
    if (browse) browse.onclick = () => {
        if (typeof window.togglePreferencesView === 'function') window.togglePreferencesView();
        spawnTabWithUrl('https://chromewebstore.google.com/');
    };
    for (const kind of ['package', 'folder']) {
        const button = document.getElementById(`setting-extensions-${kind}-btn`);
        if (button) button.onclick = () => runInstall(() => window.flyAPI.extensionsInstall({ kind, workspace: state.sessionState.current_workspace }));
    }
    const form = document.getElementById('ExtensionsStoreForm');
    if (form) form.onsubmit = event => {
        event.preventDefault();
        const input = document.getElementById('ExtensionsStoreLink').value.trim();
        runInstall(() => window.flyAPI.extensionsInstall({ kind: 'store', input, workspace: state.sessionState.current_workspace }));
    };
    const toggle = document.getElementById('setting-extensions-toggle');
    if (toggle) {
        toggle.onchange = async () => {
            await persistConfig({ extensions_enabled: toggle.checked });
            setPreferencesNote(toggle.checked
                ? 'Extensions enabled. Included extensions are ready in workspaces without a saved selection.'
                : 'Extensions disabled and unloaded everywhere. Open tabs keep injected scripts until reloaded.');
            await syncExtensionsPreferences();
            refreshStatusBarActions();
        };
    }
    const openDir = document.getElementById('setting-extensions-open-dir-btn');
    if (openDir) openDir.onclick = () => window.flyAPI.extensionsOpenDir();
    const reload = document.getElementById('setting-extensions-reload-btn');
    if (reload) {
        reload.onclick = async () => {
            installedCache = [];
            const res = await window.flyAPI.extensionsReload();
            setPreferencesNote(res && res.affectedTabs
                ? `Reloaded. Reload ${res.affectedTabs} open tab(s) to refresh content scripts.`
                : 'Reloaded.');
            await syncExtensionsPreferences();
        };
    }
    const refresh = document.getElementById('setting-extensions-refresh-btn');
    if (refresh) refresh.onclick = () => syncExtensionsPreferences();
}
