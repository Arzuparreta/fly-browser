// modules/statusBar.js
// Status and mode bar controller for link target previews, connection security, container partition badges, and modal states

import { state } from './state.js';
import { getWorkspacePartition } from './utils.js';
import { isSplitActive, getSplitState } from './splitView.js';
import { updateDarkReaderButtonUI, isWebviewDarkReaderActive } from './darkReader.js';
import { notifyActiveView } from './extensions.js';

let statusBarEl = null;
let modeBadgeEl = null;
let targetUrlEl = null;
let securityBadgeEl = null;
let partitionBadgeEl = null;
let lastSecurityUrl = Symbol('uninitialised');
let lastPartitionKey = null;

let isStatusBarEnabled = true;
let isGuestInputFocused = false;
let isHostInputFocused = false;
let isHintsModeActive = false;
let isPassthroughModeActive = false;
let targetUrlClearTimer = null;
let currentMode = 'NORMAL';

export function initStatusBar() {
    statusBarEl = document.getElementById('StatusBar');
    modeBadgeEl = document.getElementById('StatusBarMode');
    targetUrlEl = document.getElementById('StatusBarUrl');
    securityBadgeEl = document.getElementById('StatusBarSecurity');
    partitionBadgeEl = document.getElementById('StatusBarPartition');

    if (!statusBarEl) return;

    for (const badge of [partitionBadgeEl, modeBadgeEl]) {
        if (!badge) continue;
        badge.tabIndex = 0; badge.setAttribute('role', 'button');
        badge.addEventListener('keydown', event => {
            if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); badge.click(); }
        });
    }

    // Attach click handler on partition badge to open Workspace Dashboard
    if (partitionBadgeEl) {
        partitionBadgeEl.addEventListener('click', () => {
            if (typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
        });
    }

    // Attach click handler on mode badge to cancel hints or toggle passthrough if active
    if (modeBadgeEl) {
        modeBadgeEl.addEventListener('click', () => {
            if (isHintsModeActive && typeof window.triggerLinkHints === 'function') {
                window.triggerLinkHints();
            } else if (isPassthroughModeActive) {
                togglePassthroughMode(false);
            } else if (currentMode === 'NORMAL') {
                togglePassthroughMode(true);
            }
        });
    }

    // Host input focus tracking for Insert mode
    window.addEventListener('focusin', (e) => {
        const t = e.target;
        if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) {
            isHostInputFocused = true;
            recalculateMode();
        }
    });

    window.addEventListener('focusout', (e) => {
        const t = e.target;
        if (t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable)) {
            isHostInputFocused = false;
            recalculateMode();
        }
    });

    // Initial state synchronisation
    updateStatusBarFromActiveView();
}

export function setTargetUrl(url, timeoutMs = 0) {
    if (targetUrlClearTimer) {
        clearTimeout(targetUrlClearTimer);
        targetUrlClearTimer = null;
    }

    if (!targetUrlEl) targetUrlEl = document.getElementById('StatusBarUrl');
    if (!targetUrlEl) return;

    if (!url) {
        targetUrlEl.textContent = '';
        targetUrlEl.title = '';
        return;
    }

    targetUrlEl.textContent = url;
    targetUrlEl.title = url;

    if (timeoutMs > 0) {
        targetUrlClearTimer = setTimeout(() => {
            if (targetUrlEl && targetUrlEl.textContent === url) {
                targetUrlEl.textContent = '';
                targetUrlEl.title = '';
            }
            targetUrlClearTimer = null;
        }, timeoutMs);
    }
}

export function updateSecurityStatus(url) {
    if (!securityBadgeEl) securityBadgeEl = document.getElementById('StatusBarSecurity');
    if (!securityBadgeEl || lastSecurityUrl === url) return;
    lastSecurityUrl = url;

    if (!url || url === 'about:blank') {
        securityBadgeEl.innerHTML = '<i class="fa-solid fa-circle-minus"></i><span class="status-badge-text">BLANK</span>';
        securityBadgeEl.className = 'status-badge status-badge-security security-neutral';
        securityBadgeEl.title = 'Empty page';
        return;
    }

    try {
        const parsed = new URL(url);
        if (parsed.protocol === 'https:') {
            securityBadgeEl.innerHTML = '<i class="fa-solid fa-lock"></i><span class="status-badge-text">HTTPS</span>';
            securityBadgeEl.className = 'status-badge status-badge-security security-secure';
            securityBadgeEl.title = `Secure connection (TLS) | Host: ${parsed.hostname}`;
        } else if (parsed.protocol === 'http:') {
            securityBadgeEl.innerHTML = '<i class="fa-solid fa-lock-open"></i><span class="status-badge-text">HTTP</span>';
            securityBadgeEl.className = 'status-badge status-badge-security security-insecure';
            securityBadgeEl.title = `Insecure connection (HTTP) | Host: ${parsed.hostname}`;
        } else if (parsed.protocol === 'file:') {
            securityBadgeEl.innerHTML = '<i class="fa-solid fa-file-code"></i><span class="status-badge-text">FILE</span>';
            securityBadgeEl.className = 'status-badge status-badge-security security-local';
            securityBadgeEl.title = `Local file: ${parsed.pathname}`;
        } else {
            const proto = parsed.protocol.replace(':', '').toUpperCase();
            securityBadgeEl.innerHTML = `<i class="fa-solid fa-globe"></i><span class="status-badge-text">${proto}</span>`;
            securityBadgeEl.className = 'status-badge status-badge-security security-neutral';
            securityBadgeEl.title = `Protocol: ${parsed.protocol}`;
        }
    } catch {
        securityBadgeEl.innerHTML = '<i class="fa-solid fa-globe"></i><span class="status-badge-text">WEB</span>';
        securityBadgeEl.className = 'status-badge status-badge-security security-neutral';
        securityBadgeEl.title = url;
    }
}

export function updatePartitionBadge(workspaceName, isPrivate) {
    if (!partitionBadgeEl) partitionBadgeEl = document.getElementById('StatusBarPartition');
    if (!partitionBadgeEl) return;

    const privateProfile = isPrivate || state.globalPrivateModeActive;
    const ws = privateProfile ? null : workspaceName || state.sessionState.current_workspace || 'Workspace 1';
    const key = privateProfile ? 'private' : `workspace:${ws}`;
    if (key === lastPartitionKey) return;
    lastPartitionKey = key;

    if (privateProfile) {
        partitionBadgeEl.innerHTML = '<i class="fa-solid fa-user-secret"></i><span class="status-badge-text">Private Profile</span>';
        partitionBadgeEl.className = 'status-badge status-badge-partition partition-private';
        partitionBadgeEl.title = 'Partition: FlyPrivateProfile (Click to manage workspaces)';
        return;
    }

    const partitionStr = getWorkspacePartition(ws);
    partitionBadgeEl.innerHTML = `<i class="fa-solid fa-box-archive"></i><span class="status-badge-text">${ws}</span>`;
    partitionBadgeEl.className = 'status-badge status-badge-partition partition-container';
    partitionBadgeEl.title = `Container partition: ${partitionStr} (Click to manage workspaces)`;
}

export function setMode(modeName) {
    currentMode = modeName || 'NORMAL';
    if (!modeBadgeEl) modeBadgeEl = document.getElementById('StatusBarMode');
    if (!modeBadgeEl) return;

    modeBadgeEl.textContent = currentMode;
    modeBadgeEl.className = `status-badge status-badge-mode mode-${currentMode.toLowerCase()}`;
    modeBadgeEl.title = `Current browser mode: ${currentMode}`;
}

export function getMode() {
    return currentMode;
}

export function recalculateMode() {
    const currentWS = state.sessionState.current_workspace;
    let nextMode = 'NORMAL';

    if (isPassthroughModeActive) {
        nextMode = 'PASSTHROUGH';
    } else if (isHintsModeActive) {
        nextMode = 'HINTS';
    } else if (isHostInputFocused || isGuestInputFocused) {
        nextMode = 'INSERT';
    } else if (isSplitActive(currentWS)) {
        nextMode = 'SPLIT';
    } else {
        nextMode = 'NORMAL';
    }

    setMode(nextMode);
}

export function togglePassthroughMode(force) {
    if (typeof force === 'boolean') {
        isPassthroughModeActive = force;
    } else {
        isPassthroughModeActive = !isPassthroughModeActive;
    }
    recalculateMode();

    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;
    const activeWv = state.activeViewsCache[currentWS]?.[currentIdx];
    if (activeWv && typeof activeWv.send === 'function') {
        activeWv.__flyKeyboardMode ||= {};
        activeWv.__flyKeyboardMode.passthrough = isPassthroughModeActive;
        try {
            activeWv.send('set-passthrough-mode', isPassthroughModeActive);
        } catch (err) {}
    }

    if (isPassthroughModeActive) {
        setTargetUrl(window.describeKeyboardAction?.('toggle-passthrough', 'Passthrough mode enabled; toggle to return') || 'Passthrough mode enabled', 2500);
    } else {
        setTargetUrl('NORMAL mode restored', 1500);
    }
    return isPassthroughModeActive;
}

export function handleGuestPassthroughState(isActive, wsName, tabIdx) {
    const view = state.activeViewsCache[wsName]?.[tabIdx];
    if (view) { view.__flyKeyboardMode ||= {}; view.__flyKeyboardMode.passthrough = !!isActive; }
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;

    if (wsName === currentWS && tabIdx === currentIdx) {
        isPassthroughModeActive = !!isActive;
        recalculateMode();
        if (isPassthroughModeActive) {
            setTargetUrl(window.describeKeyboardAction?.('toggle-passthrough', 'Passthrough mode enabled; toggle to return') || 'Passthrough mode enabled', 2500);
        } else {
            setTargetUrl('NORMAL mode restored', 1500);
        }
    }
}

export function isPassthroughActive() {
    return isPassthroughModeActive;
}

export function handleGuestInputFocus(isFocused, wsName, tabIdx) {
    const view = state.activeViewsCache[wsName]?.[tabIdx];
    if (view) { view.__flyKeyboardMode ||= {}; view.__flyKeyboardMode.input = !!isFocused; }
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;

    if (wsName === currentWS && tabIdx === currentIdx) {
        isGuestInputFocused = !!isFocused;
        recalculateMode();
    }
}

export function handleGuestHintsState(isActive, wsName, tabIdx) {
    const view = state.activeViewsCache[wsName]?.[tabIdx];
    if (view) { view.__flyKeyboardMode ||= {}; view.__flyKeyboardMode.hints = !!isActive; }
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;

    if (wsName === currentWS && tabIdx === currentIdx) {
        isHintsModeActive = !!isActive;
        recalculateMode();
    }
}

export function updateStatusBarFromActiveView() {
    const currentWS = state.sessionState.current_workspace;
    const isPrivate = !!state.globalPrivateModeActive;
    updatePartitionBadge(currentWS, isPrivate);

    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;
    const urls = state.sessionState.workspaces[currentWS] || [];
    const activeUrl = urls[currentIdx] || '';

    updateSecurityStatus(activeUrl);
    setTargetUrl('');

    const activeWv = state.activeViewsCache[currentWS]?.[currentIdx];
    const mode = activeWv?.__flyKeyboardMode || {};
    isGuestInputFocused = !!mode.input;
    isHintsModeActive = !!mode.hints;
    isPassthroughModeActive = !!mode.passthrough;
    recalculateMode();
    updateDarkReaderButtonUI(isWebviewDarkReaderActive(activeWv));
    updateReaderViewButtonUI(!!(activeWv && activeWv.__flyReaderActive));
    notifyActiveView(activeWv, currentWS);
}

export function updateReaderViewButtonUI(enabled) {
    const btn = document.getElementById('reader-view-btn');
    if (!btn) return;
    if (enabled) {
        btn.classList.add('active');
        btn.title = window.describeKeyboardAction?.('toggle-reader-view', 'Reader view active') || 'Reader view active';
    } else {
        btn.classList.remove('active');
        btn.title = window.describeKeyboardAction?.('toggle-reader-view', 'Toggle reader view') || 'Toggle reader view';
    }
}

export function toggleStatusBar(force) {
    if (!statusBarEl) statusBarEl = document.getElementById('StatusBar');
    if (!statusBarEl) return;

    if (typeof force === 'boolean') {
        isStatusBarEnabled = force;
    } else {
        isStatusBarEnabled = !isStatusBarEnabled;
    }

    statusBarEl.style.display = isStatusBarEnabled ? 'flex' : 'none';

    // Synchronise with settings if API available
    if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
        window.flyAPI.updateBrowserSettings({ show_status_bar: isStatusBarEnabled }).catch(() => {});
    }

    // Update preferences toggle checkbox if present
    const toggleInput = document.getElementById('setting-status-bar-toggle');
    if (toggleInput) {
        toggleInput.checked = isStatusBarEnabled;
    }

    return isStatusBarEnabled;
}

export function isStatusBarVisible() {
    return isStatusBarEnabled;
}
