import { isCurrentGuestPointerEntry } from './sidebarPointer.js';
import { NEW_TAB_URL, isNewTab, refreshNewTab } from './newTab.js';
import { hasActiveOverlay } from './focus.js';
import { state } from './state.js';
import { getActiveWebview, removeWebview, getWorkspacePartition, isDarkMode, isPdfUrl, getPdfViewerUrl, extractOriginalUrl, isTargetPrivateDomain } from './utils.js';
import { loadWebviewSource } from './extensions.js';
import { isSplitActive, applySplitLayout, handleSplitTabSelection, handleTabRemovalInSplit, getSplitState } from './splitView.js';
import { 
    setTargetUrl, 
    updateSecurityStatus, 
    updateStatusBarFromActiveView, 
    handleGuestInputFocus, 
    handleGuestHintsState,
    handleGuestPassthroughState,
    updateReaderViewButtonUI
} from './statusBar.js';
import { resumeView } from './tabSuspension.js';
import { buildUserScriptWrapper } from './userScriptShim.js';

export function applyCSSThemeToView() {
    // Intentionally no-op to preserve native website themes
}

function getLiveTabIndex(webview, wsName) {
    const views = state.activeViewsCache[wsName];
    if (Array.isArray(views)) {
        const liveIdx = views.indexOf(webview);
        if (liveIdx !== -1) return liveIdx;
    }
    return -1;
}

export function createWebView(url, currentWS, idx) {
    if (!state.activeViewsCache[currentWS]) state.activeViewsCache[currentWS] = [];
    const webview = document.createElement('webview');
    state.activeViewsCache[currentWS][idx] = webview;
    webview.setAttribute('preload', window.flyAPI.getWebviewPreloadPath());
    webview.setAttribute('allowpopups', '');
    
    if (state.globalPrivateModeActive || isTargetPrivateDomain(url)) {
        webview.setAttribute('partition', 'FlyPrivateProfile');
    } else {
        webview.setAttribute('partition', getWorkspacePartition(currentWS));
    }
    
    const initialSrc = isNewTab(url) ? NEW_TAB_URL : isPdfUrl(url) ? getPdfViewerUrl(url) : url;
    loadWebviewSource(webview, currentWS, initialSrc);

    webview.addEventListener('will-navigate', (e) => {
        webview.__flyNavigationEpoch = (webview.__flyNavigationEpoch || 0) + 1;
        if (webview.__flyInjectedScriptIds) webview.__flyInjectedScriptIds.clear();
        if (isPdfUrl(e.url)) {
            e.preventDefault();
            webview.setAttribute('src', getPdfViewerUrl(e.url));
        }
    });

    const syncTitle = (newTitle) => {
        if (!newTitle || newTitle === 'about:blank' || newTitle === 'Loading...') return;
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        if (!state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS] = [];
        state.activeTitlesCache[currentWS][liveIdx] = newTitle;
        if (!state.sessionState.tab_titles) state.sessionState.tab_titles = {};
        if (!state.sessionState.tab_titles[currentWS]) state.sessionState.tab_titles[currentWS] = [];
        state.sessionState.tab_titles[currentWS][liveIdx] = newTitle;
        window.flyAPI.saveSession(state.sessionState);

        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi && state.sessionState.current_workspace === currentWS) {
            const titleEl = targetLi.querySelector('.tab-title');
            if (titleEl) {
                titleEl.textContent = newTitle.length > 24 ? newTitle.slice(0, 24) + "..." : newTitle;
            }
            targetLi.title = `${newTitle}\n${state.sessionState.workspaces[currentWS]?.[liveIdx] || ''}`;
        }
    };
    
    webview.addEventListener('page-title-updated', (e) => {
        syncTitle(e.title);
    });

    webview.addEventListener('page-favicon-updated', (e) => {
        if (e.favicons && e.favicons.length > 0) {
            const iconUrl = e.favicons[0];
            const liveIdx = getLiveTabIndex(webview, currentWS, idx);
            if (liveIdx < 0) return;

            if (!state.tabFavicons) state.tabFavicons = {};
            if (!state.tabFavicons[currentWS]) state.tabFavicons[currentWS] = [];
            state.tabFavicons[currentWS][liveIdx] = iconUrl;
            if (!state.sessionState.tab_favicons) state.sessionState.tab_favicons = {};
            if (!state.sessionState.tab_favicons[currentWS]) state.sessionState.tab_favicons[currentWS] = [];
            state.sessionState.tab_favicons[currentWS][liveIdx] = iconUrl;
            window.flyAPI.saveSession(state.sessionState);

            const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
            if (targetLi && state.sessionState.current_workspace === currentWS) {
                const favImg = targetLi.querySelector('.tab-favicon');
                const favFallback = targetLi.querySelector('.tab-fallback-icon');
                if (favImg) {
                    favImg.src = iconUrl;
                    favImg.style.display = 'block';
                    if (favFallback) favFallback.style.display = 'none';
                }
            }
        }
    });

    webview.addEventListener('did-navigate', async (e) => {
        webview.__flyNavigationEpoch = (webview.__flyNavigationEpoch || 0) + 1;
        if (webview.__flyInjectedScriptIds) webview.__flyInjectedScriptIds.clear();
        webview.__flyDarkKey = null;
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        const cleanUrl = isNewTab(e.url) ? 'about:blank' : extractOriginalUrl(e.url);
        if (state.sessionState.workspaces[currentWS] && state.sessionState.workspaces[currentWS][liveIdx] !== undefined) {
            state.sessionState.workspaces[currentWS][liveIdx] = cleanUrl;
            window.flyAPI.saveSession(state.sessionState);
        }
        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const domain = targetLi.querySelector('.tab-domain');
            if (domain) { try { domain.textContent = isNewTab(webview.getURL()) ? 'fly / new tab' : new URL(webview.getURL()).hostname || 'local page'; } catch {} }
        }
        if (state.sessionState.current_workspace === currentWS) {
            const activeListItem = document.querySelector('#TabList li.selected');
            const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
            if (liveIdx === currentSelectedIdx) {
                updateSecurityStatus(cleanUrl);
            }
        }
    });

    webview.addEventListener('did-navigate-in-page', async (e) => {
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        const cleanUrl = isNewTab(e.url) ? 'about:blank' : extractOriginalUrl(e.url);
        if (state.sessionState.workspaces[currentWS] && state.sessionState.workspaces[currentWS][liveIdx] !== undefined) {
            state.sessionState.workspaces[currentWS][liveIdx] = cleanUrl;
            window.flyAPI.saveSession(state.sessionState);
        }
        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const domain = targetLi.querySelector('.tab-domain');
            if (domain) { try { domain.textContent = isNewTab(webview.getURL()) ? 'fly / new tab' : new URL(webview.getURL()).hostname || 'local page'; } catch {} }
        }
        if (state.sessionState.current_workspace === currentWS) {
            const activeListItem = document.querySelector('#TabList li.selected');
            const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
            if (liveIdx === currentSelectedIdx) {
                updateSecurityStatus(cleanUrl);
            }
        }
        webview.executeJavaScript(`
            (function() {
                try {
                    document.querySelectorAll('video').forEach(v => {
                        const prevDisplay = v.style.display;
                        v.style.display = 'none';
                        void v.offsetHeight; // force synchronous reflow
                        v.style.display = prevDisplay;
                    });
                } catch (e) {}
            })();
        `, false).catch(() => {});
    });

    webview.addEventListener('new-window', (e) => {
        e.preventDefault();
        const targetUrl = e.url;
        
        if (!state.sessionState.workspaces[currentWS]) {
            state.sessionState.workspaces[currentWS] = [];
        }
        
        state.sessionState.workspaces[currentWS].push(targetUrl);
        window.flyAPI.saveSession(state.sessionState);
        
        const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
        if (!state.tabPinned[currentWS]) state.tabPinned[currentWS] = [];
        state.tabPinned[currentWS][newTargetIdx] = false;
        renderWorkspaceUI(newTargetIdx);
    });

    // Electron destroys a guest that calls window.close() but leaves this element
    // blank and holding keyboard focus, so no shortcut reaches Fly. Close the tab
    // as the page asked; a tab Fly cannot close here reloads when next selected.
    webview.addEventListener('close', () => {
        const idx = getLiveTabIndex(webview, currentWS);
        if (idx === -1) {
            removeWebview(webview);
            return;
        }
        if (currentWS === state.sessionState.current_workspace && !state.dashboardActive && !state.tabPinned?.[currentWS]?.[idx]) {
            handleTabRemoval(idx);
            return;
        }
        const selected = currentWS === state.sessionState.current_workspace &&
            document.querySelectorAll('#TabList li')[idx]?.classList.contains('selected');
        removeWebview(webview);
        state.activeViewsCache[currentWS][idx] = null;
        if (!state.tabRestoreStates[currentWS]) state.tabRestoreStates[currentWS] = [];
        state.tabRestoreStates[currentWS][idx] = {
            url: state.sessionState.workspaces[currentWS]?.[idx],
            title: state.activeTitlesCache[currentWS]?.[idx] || 'Tab',
            scrollX: 0,
            scrollY: 0,
            sessionStorage: null,
        };
        state.sessionState.tab_restore_states = state.tabRestoreStates;
        window.flyAPI.saveSession(state.sessionState);
        if (selected && !state.dashboardActive) switchTabFocus(idx);
    });

    webview.addEventListener('render-process-gone', (e) => {
        if (e.reason !== 'clean-exit' && e.reason !== 'killed') {
            setTimeout(() => {
                webview.reload();
            }, 500);
        }
    });

    webview.addEventListener('found-in-page', (e) => {
        if (e.result) {
            const countEl = document.getElementById('FindMatchCount');
            if (countEl) {
                const activeMatch = e.result.activeMatchOrdinal || 0;
                const totalMatches = e.result.matches || 0;
                countEl.textContent = totalMatches > 0 ? `${activeMatch}/${totalMatches}` : '0/0';
            }
        }
    });

    webview.addEventListener('media-started-playing', () => {
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;
        if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
        state.tabMediaAudible[currentWS][liveIdx] = true;
        const isMuted = typeof webview.isAudioMuted === 'function' ? webview.isAudioMuted() : !!state.tabAudioMuted[currentWS]?.[liveIdx];
        updateTabMediaIndicator(currentWS, liveIdx, true, isMuted);
    });
    
    webview.addEventListener('media-paused', () => {
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;
        if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
        state.tabMediaAudible[currentWS][liveIdx] = false;
        const isMuted = typeof webview.isAudioMuted === 'function' ? webview.isAudioMuted() : !!state.tabAudioMuted[currentWS]?.[liveIdx];
        updateTabMediaIndicator(currentWS, liveIdx, false, isMuted);
    });

    webview.addEventListener('did-start-loading', () => {
        webview.style.opacity = '1';
        webview.__flyNavigationEpoch = (webview.__flyNavigationEpoch || 0) + 1;
        if (webview.__flyInjectedScriptIds) webview.__flyInjectedScriptIds.clear();
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx >= 0) {
            updateTabMediaIndicator(currentWS, liveIdx, false, false);
        }
    });

    webview.addEventListener('dom-ready', async () => {
        document.dispatchEvent(new Event('fly-guest-ready'));
        webview.__flyKeyboardMode = {};
        refreshNewTab(webview);
        webview.style.opacity = '1';
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const domain = targetLi.querySelector('.tab-domain');
            if (domain) { try { domain.textContent = isNewTab(webview.getURL()) ? 'fly / new tab' : new URL(webview.getURL()).hostname || 'local page'; } catch {} }
        }

        try {
            const liveTitle = webview.getTitle();
            if (liveTitle && liveTitle !== 'about:blank') {
                syncTitle(liveTitle);
            }
        } catch (err) {}

        try {
            const hinterCode = await window.flyAPI.readHinterCode();
            if (hinterCode && typeof webview.executeJavaScript === 'function') {
                const safeExecutionWrapper = `try { ${hinterCode} } catch(e) {}`;
                webview.executeJavaScript(safeExecutionWrapper, false).catch(() => {});
            }
        } catch (err) {}

        injectMatchingUserContent(webview).catch(() => {});


        if (state.sessionState.current_workspace === currentWS) {
            const activeListItem = document.querySelector('#TabList li.selected');
            const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
            if (liveIdx === currentSelectedIdx) {
                // The page can close itself while the hinter loads, destroying its guest.
                try { updateSecurityStatus(webview.getURL()); } catch { return; }
                updateStatusBarFromActiveView();
            }
        }
    });

    webview.addEventListener('did-finish-load', () => {
        try {
            const liveTitle = webview.getTitle();
            if (liveTitle && liveTitle !== 'about:blank') {
                syncTitle(liveTitle);
            }
        } catch (err) {}
    });

    webview.addEventListener('did-stop-loading', () => {
        try {
            const liveTitle = webview.getTitle();
            if (liveTitle && liveTitle !== 'about:blank') {
                syncTitle(liveTitle);
                return;
            }
        } catch (err) {}

        webview.executeJavaScript('document.title', false).then(docTitle => {
            if (docTitle && docTitle.trim() && docTitle !== 'about:blank') {
                syncTitle(docTitle.trim());
            } else {
                const liveIdx = getLiveTabIndex(webview, currentWS, idx);
                if (liveIdx >= 0 && (!state.activeTitlesCache[currentWS]?.[liveIdx] || state.activeTitlesCache[currentWS][liveIdx] === 'Loading...')) {
                    try {
                        const parsed = new URL(webview.getURL() || url);
                        if (parsed.hostname) {
                            syncTitle(parsed.hostname);
                        }
                    } catch (e) {}
                }
            }
        }).catch(() => {});
    });

    webview.addEventListener('update-target-url', (e) => {
        const activeListItem = document.querySelector('#TabList li.selected');
        const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (state.sessionState.current_workspace === currentWS && (liveIdx === currentSelectedIdx || isSplitActive(currentWS))) {
            setTargetUrl(e.url || '');
        }
    });

    webview.addEventListener('ipc-message', (e) => {
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        if (e.channel === 'guest-pointer-enter') {
            const sidebar = document.getElementById('Sidebar');
            // Guest IPC can arrive after the pointer has already returned to the
            // host. Never let an old guest entry reverse a current sidebar hover.
            if (webview.getClientRects().length && isCurrentGuestPointerEntry(e.args?.[0])) {
                sidebar.classList.remove('pointer-inside');
            }
        } else if (e.channel === 'guest-input-focus') {
            handleGuestInputFocus(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'guest-hints-state') {
            handleGuestHintsState(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'guest-passthrough-state') {
            handleGuestPassthroughState(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'reader-view-toggled') {
            const data = e.args[0] || {};
            webview.__flyReaderActive = !!data.active;
            updateReaderViewButtonUI(!!data.active);
            if (data.error) {
                setTargetUrl(`Reader View: ${data.error}`, 3000);
            } else if (data.active) {
                const titleStr = data.title ? `: ${data.title.slice(0, 45)}...` : '';
                setTargetUrl(`Reader view active (Escape to exit)${titleStr}`, 3000);
            } else {
                setTargetUrl('Reader View Closed', 2000);
            }
        } else if (e.channel === 'keyboard-action') {
            // Finish the guest input event before an action can destroy its own webview.
            const action = e.args[0];
            setTimeout(() => window.dispatchKeyboardAction(action), 0);
        } else if (e.channel === 'normal-mode-action') {
            const action = e.args[0];
            const extra = e.args[1];
            handleNormalModeAction(action, extra, webview, currentWS, liveIdx);
        }
    });

    webview.addEventListener('focus', () => {
        if (isSplitActive(currentWS)) {
            const split = getSplitState(currentWS);
            const liveIdx = getLiveTabIndex(webview, currentWS, idx);
            if (liveIdx === split.primaryIdx && split.activePane !== 'primary') {
                split.activePane = 'primary';
                applySplitLayout();
            } else if (liveIdx === split.secondaryIdx && split.activePane !== 'secondary') {
                split.activePane = 'secondary';
                applySplitLayout();
            }
        }
    });
    
    return webview;
}

export function handleNormalModeAction(action, extra, webview, currentWS, idx) {
    switch (action) {
        case 'spawn-tab':
            spawnNewBlankTab();
            break;
        case 'remove-tab':
            handleTabRemoval();
            break;
        case 'toggle-address':
            if (typeof window.displayAddressOverlay === 'function') {
                window.displayAddressOverlay();
            }
            break;
        case 'reload-tab':
            if (webview && typeof webview.reload === 'function') {
                webview.reload();
            }
            break;
        case 'force-reload-tab':
            if (webview && typeof webview.reloadIgnoringCache === 'function') {
                webview.reloadIgnoringCache();
            }
            break;
        case 'history-back':
            navigateFrameBack();
            break;
        case 'history-forward':
            navigateFrameForward();
            break;
        case 'toggle-find':
            if (typeof window.toggleInPageSearch === 'function') {
                window.toggleInPageSearch();
            }
            break;
        case 'trigger-hints':
            if (typeof window.triggerLinkHints === 'function') {
                window.triggerLinkHints();
            }
            break;
        case 'toggle-dashboard':
            if (typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
            break;
        case 'focus-sidebar':
            window.focusSidebar?.();
            break;
        case 'yank-url': {
            const urlToYank = extra || (webview && typeof webview.getURL === 'function' ? webview.getURL() : '') || (state.sessionState.workspaces[currentWS]?.[idx]);
            if (urlToYank) {
                if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
                    navigator.clipboard.writeText(urlToYank).catch(() => {});
                }
                setTargetUrl(`Yanked URL to clipboard: ${urlToYank}`, 2500);
            }
            break;
        }
        case 'toggle-reader-view':
            toggleReaderViewOnActiveTab();
            break;
        case 'toggle-tab-audio-mute':
            toggleTabAudioMute(currentWS, idx);
            break;
        case 'toggle-global-audio-mute':
            toggleGlobalAudioMute();
            break;
        case 'toggle-pin-tab':
            togglePinActiveTab();
            break;
        case 'escape':
            if (state.bookmarksActive && typeof window.toggleBookmarksOverlay === 'function') {
                window.toggleBookmarksOverlay();
            } else if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            } else if (state.historyActive && typeof window.toggleHistoryOverlay === 'function') {
                window.toggleHistoryOverlay();
            }
            break;
    }
}

export async function toggleReaderViewOnActiveTab() {
    const activeWv = getActiveWebview();
    if (!activeWv || typeof activeWv.executeJavaScript !== 'function') return;
    try {
        const readerCode = await window.flyAPI.readReaderCode();
        if (!readerCode) return;
        const res = await activeWv.executeJavaScript(readerCode, false);
        if (res) {
            activeWv.__flyReaderActive = !!res.active;
            updateReaderViewButtonUI(!!res.active);
            if (res.error) {
                setTargetUrl(`Reader View: ${res.error}`, 3000);
            } else if (res.active) {
                const titleStr = res.title ? `: ${res.title.slice(0, 45)}...` : '';
                setTargetUrl(`Reader view active (Escape to exit)${titleStr}`, 3500);
            } else {
                setTargetUrl('Reader View Closed', 2000);
            }
        }
    } catch (err) {
        console.error('Failed to execute reader view:', err);
    }
}

function extractHostname(url) {
    try {
        return new URL(url).hostname.toLowerCase();
    } catch {
        return '';
    }
}

export async function injectMatchingUserContent(webview) {
    if (!webview || typeof webview.getURL !== 'function') return;
    const currentUrl = webview.getURL();
    if (!currentUrl || currentUrl.startsWith('about:') || currentUrl.startsWith('devtools:')) return;
    if (!window.flyAPI || typeof window.flyAPI.getUserContentForUrl !== 'function') return;

    const navEpoch = webview.__flyNavigationEpoch || 0;

    try {
        const content = await window.flyAPI.getUserContentForUrl(currentUrl);
        if (!content) return;

        // Abort if page navigated or started loading a new destination while awaiting IPC lookup
        if (webview.__flyNavigationEpoch !== navEpoch) return;
        if (typeof webview.getURL === 'function' && webview.getURL() !== currentUrl) return;

        if (!webview.__flyInjectedCSSKeys) webview.__flyInjectedCSSKeys = [];
        if (typeof webview.removeInsertedCSS === 'function' && webview.__flyInjectedCSSKeys.length > 0) {
            for (const key of webview.__flyInjectedCSSKeys) {
                try { await webview.removeInsertedCSS(key); } catch (e) {}
            }
            webview.__flyInjectedCSSKeys = [];
        }

        if (Array.isArray(content.styles)) {
            for (const item of content.styles) {
                if (item.css && typeof webview.insertCSS === 'function') {
                    try {
                        const key = await webview.insertCSS(item.css);
                        if (key) webview.__flyInjectedCSSKeys.push(key);
                    } catch (e) {}
                }
            }
        }

        if (!webview.__flyInjectedScriptIds) {
            webview.__flyInjectedScriptIds = new Set();
        }

        if (Array.isArray(content.scripts)) {
            for (const item of content.scripts) {
                const scriptId = item.scriptId || item.filename || item.name;
                if (webview.__flyInjectedScriptIds.has(scriptId)) {
                    continue;
                }
                if (item.code && typeof webview.executeJavaScript === 'function') {
                    webview.__flyInjectedScriptIds.add(scriptId);
                    const scriptWrapper = buildUserScriptWrapper(item);
                    webview.executeJavaScript(scriptWrapper, false).catch(() => {});
                }
            }
        }
    } catch (err) {}
}

export async function reapplyActiveUserStyles() {
    const currentWS = state.sessionState.current_workspace;
    const views = state.activeViewsCache[currentWS] || [];
    for (const wv of views) {
        if (!wv || typeof wv.getURL !== 'function' || typeof wv.insertCSS !== 'function') continue;
        const currentUrl = wv.getURL();
        if (!currentUrl || currentUrl.startsWith('about:') || currentUrl.startsWith('devtools:')) continue;
        if (!window.flyAPI || typeof window.flyAPI.getUserContentForUrl !== 'function') continue;

        try {
            const content = await window.flyAPI.getUserContentForUrl(currentUrl);
            if (!content) continue;

            if (!wv.__flyInjectedCSSKeys) wv.__flyInjectedCSSKeys = [];
            if (typeof wv.removeInsertedCSS === 'function' && wv.__flyInjectedCSSKeys.length > 0) {
                for (const key of wv.__flyInjectedCSSKeys) {
                    try { await wv.removeInsertedCSS(key); } catch (e) {}
                }
                wv.__flyInjectedCSSKeys = [];
            }

            if (Array.isArray(content.styles)) {
                for (const item of content.styles) {
                    if (item.css) {
                        try {
                            const key = await wv.insertCSS(item.css);
                            if (key) wv.__flyInjectedCSSKeys.push(key);
                        } catch (e) {}
                    }
                }
            }
        } catch (err) {}
    }
}

export function getWorkspaceBadgeText(wsName) {
    if (!wsName) return '1';
    const match = wsName.match(/(\d+)$/);
    if (match) {
        const num = match[1];
        const prefix = wsName.trim()[0].toUpperCase();
        return num.length <= 2 ? (wsName.length <= 3 ? wsName : `${prefix}${num}`) : num.slice(0, 2);
    }
    return wsName.slice(0, 2).toUpperCase();
}

export function getFaviconUrl(urlStr) {
    if (!urlStr) return null;
    try {
        const parsed = new URL(urlStr);
        if (parsed.protocol === 'http:' || parsed.protocol === 'https:') {
            return `https://www.google.com/s2/favicons?domain=${parsed.hostname}&sz=32`;
        }
    } catch (e) {}
    return null;
}

export function renderWorkspaceUI(targetTabToFocus = null) {
    const currentWS = state.sessionState.current_workspace;
    const wsLabel = document.getElementById('WorkspaceLabel');
    if (wsLabel) {
        wsLabel.textContent = currentWS;
        wsLabel.title = `Workspace: ${currentWS} (Container: ${getWorkspacePartition(currentWS)})`;
    }
    const wsBadge = document.getElementById('WorkspaceBadge');
    if (wsBadge) {
        wsBadge.textContent = getWorkspaceBadgeText(currentWS);
        wsBadge.title = `Workspace: ${currentWS} (Container: ${getWorkspacePartition(currentWS)})`;
    }
    const wsHeader = document.getElementById('WorkspaceHeader');
    if (wsHeader && !wsHeader._hasClickListener) {
        wsHeader._hasClickListener = true;
        wsHeader.addEventListener('click', () => {
            if (typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
        });
    }
    const tabList = document.getElementById('TabList');
    tabList.innerHTML = '';

    const urls = state.sessionState.workspaces[currentWS] || [];

    const welcomeEl = document.getElementById('EmptyWorkspaceWelcome');
    const container = document.getElementById('webview-container');
    const allWebviews = container.querySelectorAll('webview');
    allWebviews.forEach((wv) => wv.style.display = 'none');

    if (urls.length === 0) {
        if (welcomeEl) {
            welcomeEl.style.display = 'flex';
            const nameEl = document.getElementById('WelcomeWorkspaceName');
            if (nameEl) nameEl.textContent = currentWS;
        }
        updateStatusBarFromActiveView();
        return;
    } else {
        if (welcomeEl) {
            welcomeEl.style.display = 'none';
        }
    }

    if (!state.activeViewsCache[currentWS]) state.activeViewsCache[currentWS] = [];
    if (!state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS] = [];
    if (!state.tabRestoreStates[currentWS]) state.tabRestoreStates[currentWS] = [];
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    if (!state.tabFavicons) state.tabFavicons = {};
    if (!state.tabFavicons[currentWS]) state.tabFavicons[currentWS] = [];
    urls.forEach((url, idx) => {
        let cachedTitle = state.activeTitlesCache[currentWS]?.[idx];
        if (!cachedTitle || cachedTitle === 'Loading...') {
            if (state.tabRestoreStates[currentWS]?.[idx]?.title) {
                cachedTitle = state.tabRestoreStates[currentWS][idx].title;
                if (!state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS] = [];
                state.activeTitlesCache[currentWS][idx] = cachedTitle;
            } else {
                const existingWv = state.activeViewsCache[currentWS]?.[idx];
                if (existingWv && typeof existingWv.getTitle === 'function') {
                    try {
                        const liveTitle = existingWv.getTitle();
                        if (liveTitle && liveTitle !== 'about:blank') {
                            cachedTitle = liveTitle;
                            if (!state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS] = [];
                            state.activeTitlesCache[currentWS][idx] = cachedTitle;
                        }
                    } catch (e) {}
                }
            }
        }
        if (!cachedTitle) {
            cachedTitle = isNewTab(url) ? "New tab · Fly" : "Loading...";
        }
        const cachedFavicon = state.tabFavicons[currentWS]?.[idx] || null;

        const li = document.createElement('li');
        const isInitiallySelected = (idx === (targetTabToFocus !== null ? targetTabToFocus : 0));
        li.setAttribute('tabindex', isInitiallySelected ? '0' : '-1');
        const isPinned = !!state.tabPinned?.[currentWS]?.[idx];
        li.className = 'tab-item';
        li.title = `${cachedTitle}\n${url}`;
        if (isPinned) li.classList.add('tab-pinned');

        // Favicon wrapper
        const favWrapper = document.createElement('div');
        favWrapper.className = 'tab-favicon-wrapper';

        const favImg = document.createElement('img');
        favImg.className = 'tab-favicon';
        favImg.alt = '';

        const favFallback = document.createElement('i');
        favFallback.className = 'fa-solid fa-globe tab-fallback-icon';

        const audioDot = document.createElement('span');
        audioDot.className = 'tab-audio-dot';
        audioDot.title = 'Playing audio';
        audioDot.innerHTML = '<i class="fa-solid fa-volume-high"></i>';

        const pinDot = document.createElement('span');
        pinDot.className = 'tab-pin-dot';
        pinDot.title = 'Pinned tab';
        pinDot.innerHTML = '<i class="fa-solid fa-thumbtack"></i>';

        favWrapper.appendChild(favImg);
        favWrapper.appendChild(favFallback);
        if (state.tabMediaAudible[currentWS]?.[idx]) favWrapper.appendChild(audioDot);
        if (isPinned) favWrapper.appendChild(pinDot);

        const initialFav = cachedFavicon || getFaviconUrl(url);
        if (initialFav) {
            favImg.src = initialFav;
            favImg.onload = () => {
                favImg.style.display = 'block';
                favFallback.style.display = 'none';
            };
            favImg.onerror = () => {
                favImg.style.display = 'none';
                favFallback.style.display = 'inline-block';
            };
        } else {
            favImg.style.display = 'none';
            favFallback.style.display = 'inline-block';
        }

        li.appendChild(favWrapper);

        const titleSpan = document.createElement('span');
        titleSpan.className = 'tab-title';
        titleSpan.textContent = cachedTitle.length > 24 ? cachedTitle.slice(0, 24) + "..." : cachedTitle;
        const copy = document.createElement('div');
        copy.className = 'tab-copy';
        const domain = document.createElement('span');
        domain.className = 'tab-domain';
        try { domain.textContent = isNewTab(url) ? 'fly / new tab' : new URL(url).hostname || 'local page'; } catch { domain.textContent = ''; }
        copy.append(titleSpan, domain);
        li.appendChild(copy);
        if (isPinned) {
            const pinBadge = document.createElement('span');
            pinBadge.className = 'tab-pin-badge';
            pinBadge.title = 'Pinned tab (click to unpin)';
            pinBadge.innerHTML = '<i class="fa-solid fa-thumbtack"></i>';
            pinBadge.addEventListener('click', (e) => {
                e.stopPropagation();
                togglePinTab(currentWS, idx);
            });
            li.appendChild(pinBadge);
        }

        const isAudible = !!state.tabMediaAudible[currentWS]?.[idx];
        const isMuted = !!state.tabAudioMuted[currentWS]?.[idx];

        if (isAudible || isMuted) {
            const mediaBadge = document.createElement('span');
            mediaBadge.className = 'tab-media-badge' + (isMuted ? ' is-muted' : '');
            mediaBadge.title = isMuted ? 'Tab audio muted (click to unmute)' : 'Tab playing audio (click to mute)';
            mediaBadge.innerHTML = isMuted ? '<i class="fa-solid fa-volume-xmark"></i>' : '<i class="fa-solid fa-volume-high"></i>';
            mediaBadge.addEventListener('click', (e) => {
                e.stopPropagation();
                toggleTabAudioMute(currentWS, idx);
            });
            li.appendChild(mediaBadge);
        }

        if (!isPinned) {
            const closeButton = document.createElement('button');
            closeButton.type = 'button';
            closeButton.className = 'tab-close-btn';
            closeButton.tabIndex = -1;
            closeButton.title = 'Close tab';
            closeButton.setAttribute('aria-label', 'Close tab');
            closeButton.innerHTML = '<i class="fa-solid fa-xmark"></i>';
            closeButton.addEventListener('click', (e) => {
                e.stopPropagation();
                if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                    window.toggleDashboardView();
                }
                handleTabRemoval(idx);
            });
            li.appendChild(closeButton);
        }

        li.addEventListener('click', (e) => {
            if (e.target.closest('.tab-media-badge') || e.target.closest('.tab-pin-badge') || e.target.closest('.tab-close-btn')) return;
            if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
            switchTabFocus(idx);
        });

        // Middle click closes the tab, as in other browsers; mousedown is cancelled so Linux does not start autoscroll.
        li.addEventListener('mousedown', (e) => {
            if (e.button === 1) e.preventDefault();
        });
        li.addEventListener('auxclick', (e) => {
            if (e.button !== 1) return;
            e.preventDefault();
            if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
            handleTabRemoval(idx);
        });

        li.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                    window.toggleDashboardView();
                }
                switchTabFocus(idx);
            }
        });

        tabList.appendChild(li);

    });

    if (!state.dashboardActive) {
        if (isSplitActive(currentWS)) {
            applySplitLayout();
        } else {
            const focusIdx = targetTabToFocus !== null ? targetTabToFocus : 0;
            switchTabFocus(focusIdx);
        }
    }
}

let pendingTabFocus = null;
// The host window blurs when a page takes focus, so a pending programmatic focus is obsolete.
if (typeof window !== 'undefined') window.addEventListener('blur', () => { clearTimeout(pendingTabFocus); pendingTabFocus = null; });

export function switchTabFocus(targetIdx) {
    const tabItems = document.querySelectorAll('#TabList li');
    const currentWS = state.sessionState.current_workspace;

    if (tabItems.length === 0) return;

    if (targetIdx >= tabItems.length) {
        targetIdx = Math.max(0, tabItems.length - 1);
    }
    if (targetIdx < 0) {
        targetIdx = 0;
    }

    if (!state.tabHistory) state.tabHistory = {};
    if (!Array.isArray(state.tabHistory[currentWS])) state.tabHistory[currentWS] = [];
    const history = state.tabHistory[currentWS];
    const existingPos = history.indexOf(targetIdx);
    if (existingPos !== -1) {
        history.splice(existingPos, 1);
    }
    history.push(targetIdx);

    if (isSplitActive(currentWS)) {
        if (handleSplitTabSelection(targetIdx)) {
            return;
        }
    }

    tabItems.forEach((item, idx) => {
        if (idx === targetIdx) {
            item.classList.add('selected');
            item.setAttribute('tabindex', '0');
        } else {
            item.classList.remove('selected');
            item.setAttribute('tabindex', '-1');
        }
    });

    if (!state.activeViewsCache[currentWS]?.[targetIdx] || state.tabRestoreStates[currentWS]?.[targetIdx]) {
        ensureTabLoaded(currentWS, targetIdx);
    }

    const currentWSViews = state.activeViewsCache[currentWS] || [];

    const allWebviews = document.getElementById('webview-container').querySelectorAll('webview');
    allWebviews.forEach((wv) => wv.style.display = 'none');
    
    if (!state.dashboardActive && currentWSViews[targetIdx]) {
        currentWSViews[targetIdx].style.display = 'flex';
        refreshNewTab(currentWSViews[targetIdx]);
        

        const targetView = currentWSViews[targetIdx];
        clearTimeout(pendingTabFocus);
        pendingTabFocus = setTimeout(async () => {
            await resumeView(targetView);
            pendingTabFocus = null;
            // A click inside a page already moved focus there; a second focus request racing it
            // leaves Chromium routing keys to the host while the page looks focused.
            if (document.activeElement === targetView) return;
            if (state.sessionState.current_workspace !== currentWS || state.activeViewsCache[currentWS]?.[targetIdx] !== targetView || !targetView.isConnected) return;
            const selected = document.querySelector('#TabList li.selected');
            if ([...document.querySelectorAll('#TabList li')].indexOf(selected) !== targetIdx) return;
            const currentFocused = document.activeElement;
            const sidebarHasFocus = document.getElementById('Sidebar').contains(currentFocused);
            const addressBar = document.getElementById('WideAddressBar');
            const addressBarIsActive = addressBar && (addressBar.style.display === 'block' || currentFocused === addressBar);

            // A pending quickmark prompt reads its key in the host, so the page must not take it.
            if (!sidebarHasFocus && !hasActiveOverlay() && !addressBarIsActive && !state.awaitingQuickmarkKey) {
                targetView.focus();
            }
        }, 50);
    }

    updateStatusBarFromActiveView();
}

export async function spawnNewBlankTab() {
    const currentWS = state.sessionState.current_workspace;
    if (!state.sessionState.workspaces[currentWS]) state.sessionState.workspaces[currentWS] = [];

    const defaultUrl = 'about:blank';

    state.sessionState.workspaces[currentWS].push(defaultUrl);
    window.flyAPI.saveSession(state.sessionState);
    
    const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
    if (!state.tabRestoreStates[currentWS]) state.tabRestoreStates[currentWS] = [];
    state.tabRestoreStates[currentWS][newTargetIdx] = null;
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    state.tabMediaAudible[currentWS][newTargetIdx] = false;
    if (!state.tabPinned[currentWS]) state.tabPinned[currentWS] = [];
    state.tabPinned[currentWS][newTargetIdx] = false;

    renderWorkspaceUI(newTargetIdx);
    if (typeof window.displayAddressOverlay === 'function') {
        window.displayAddressOverlay();
    }
}

export function spawnTabWithUrl(url) {
    if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
        window.toggleDashboardView();
    }
    const addressBar = document.getElementById('WideAddressBar');
    if (addressBar && addressBar.style.display !== 'none') {
        addressBar.style.display = 'none';
    }
    const bookmarksOverlay = document.getElementById('BookmarksOverlay');
    if (bookmarksOverlay && bookmarksOverlay.style.display !== 'none') {
        if (typeof window.toggleBookmarksOverlay === 'function' && state.bookmarksActive) {
            window.toggleBookmarksOverlay();
        } else {
            bookmarksOverlay.style.display = 'none';
            state.bookmarksActive = false;
        }
    }

    const currentWS = state.sessionState.current_workspace;
    if (!state.sessionState.workspaces[currentWS]) state.sessionState.workspaces[currentWS] = [];
    state.sessionState.workspaces[currentWS].push(url || "https://duckduckgo.com");
    window.flyAPI.saveSession(state.sessionState);
    
    const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
    if (!state.tabRestoreStates[currentWS]) state.tabRestoreStates[currentWS] = [];
    state.tabRestoreStates[currentWS][newTargetIdx] = null;
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    state.tabMediaAudible[currentWS][newTargetIdx] = false;
    if (!state.tabPinned[currentWS]) state.tabPinned[currentWS] = [];
    state.tabPinned[currentWS][newTargetIdx] = false;

    renderWorkspaceUI(newTargetIdx);
}

// Closed tabs are kept only in memory and cleared when private mode changes.
const closedTabs = [];
function rememberClosedTab(workspace, index) {
    const url = state.sessionState.workspaces[workspace]?.[index];
    if (!url) return;
    closedTabs.push({url, workspace});
    if (closedTabs.length > 20) closedTabs.shift();
}
export function clearClosedTabs() { closedTabs.length = 0; }
export function reopenClosedTab() {
    const tab = closedTabs.pop();
    if (!tab) { setTargetUrl('No closed tabs to reopen', 2000); return; }
    if (state.sessionState.workspaces[tab.workspace]) {
        state.sessionState.current_workspace = tab.workspace;
        renderWorkspaceUI();
    }
    spawnTabWithUrl(tab.url);
}

// Session metadata can share arrays with live caches after saving.
function removeTabMetadata(workspace, index) {
    const arrays = new Set([
        state.activeTitlesCache, state.tabRestoreStates,
        state.tabMediaAudible, state.tabAudioMuted,
        state.tabFavicons, state.tabPinned, state.sessionState.tab_titles,
        state.sessionState.tab_restore_states,
        state.sessionState.tab_favicons, state.sessionState.tab_pinned
    ].map(cache => cache?.[workspace]).filter(Array.isArray));
    for (const array of arrays) array.splice(index, 1);
}

export function handleTabRemoval(explicitIdx) {
    if (state.dashboardActive) {
        const currentItem = state.dashboardItems[state.dashboardSelectionIdx];
        if (!currentItem) return;
        const [type, wsName, idx] = currentItem.payload;
        
        if (type === 'tab') {
            if (state.tabPinned?.[wsName]?.[idx]) {
                if (typeof setTargetUrl === 'function') {
                    setTargetUrl('Tab is pinned. Unpin before closing.', 3000);
                }
                return;
            }
            if (state.sessionState.workspaces[wsName].length > 1) {
                rememberClosedTab(wsName, idx);
                state.sessionState.workspaces[wsName].splice(idx, 1);
                if (state.activeViewsCache[wsName] && state.activeViewsCache[wsName][idx]) {
                    removeWebview(state.activeViewsCache[wsName][idx]);
                    state.activeViewsCache[wsName].splice(idx, 1);
                } else if (state.activeViewsCache[wsName]) {
                    state.activeViewsCache[wsName].splice(idx, 1);
                }
                removeTabMetadata(wsName, idx);
                if (state.tabHistory && Array.isArray(state.tabHistory[wsName])) {
                    state.tabHistory[wsName] = state.tabHistory[wsName]
                        .filter(hIdx => hIdx !== idx)
                        .map(hIdx => (hIdx > idx ? hIdx - 1 : hIdx));
                }

                state.sessionState.tab_pinned = state.tabPinned;
                window.flyAPI.saveSession(state.sessionState);
                renderWorkspaceUI();
                if (typeof window.buildDashboardTree === 'function') {
                    window.buildDashboardTree();
                }
            }
        } else if (type === 'workspace') {
            const totalWorkspaces = Object.keys(state.sessionState.workspaces);
            if (totalWorkspaces.length > 1) {
                if (wsName === state.sessionState.current_workspace) {
                    const fallbackWS = totalWorkspaces.find(k => k !== wsName);
                    state.sessionState.current_workspace = fallbackWS;
                }
                if (state.activeViewsCache[wsName]) {
                    state.activeViewsCache[wsName].forEach(removeWebview);
                    delete state.activeViewsCache[wsName];
                }
                delete state.activeTitlesCache[wsName];
                delete state.tabRestoreStates[wsName];
                delete state.tabMediaAudible[wsName];
                delete state.tabAudioMuted[wsName];
                delete state.tabFavicons?.[wsName];
                delete state.tabHistory?.[wsName];
                delete state.tabPinned?.[wsName];
                if (state.sessionState.tab_pinned?.[wsName]) delete state.sessionState.tab_pinned[wsName];
                if (state.sessionState.tab_titles?.[wsName]) delete state.sessionState.tab_titles[wsName];
                if (state.sessionState.tab_restore_states?.[wsName]) delete state.sessionState.tab_restore_states[wsName];
                if (state.sessionState.tab_favicons?.[wsName]) delete state.sessionState.tab_favicons[wsName];
                delete state.sessionState.workspaces[wsName];
                delete state.sessionState.workspace_partitions?.[wsName];
                
                window.flyAPI.saveSession(state.sessionState);
                renderWorkspaceUI();
                if (typeof window.buildDashboardTree === 'function') {
                    window.buildDashboardTree();
                }
            }
        }
        return;
    }

    const currentWS = state.sessionState.current_workspace;
    const tabElements = Array.from(document.querySelectorAll('#TabList li'));
    const activeListItem = document.querySelector('#TabList li.selected');
    let currentIdx = (typeof explicitIdx === 'number' && explicitIdx >= 0)
        ? explicitIdx
        : (activeListItem ? tabElements.indexOf(activeListItem) : -1);
    if (currentIdx === -1) return;

    const tabs = state.sessionState.workspaces[currentWS] || [];
    if (tabs.length === 0 || currentIdx >= tabs.length) return;

    if (state.tabPinned?.[currentWS]?.[currentIdx]) {
        if (typeof setTargetUrl === 'function') {
            setTargetUrl('Tab is pinned. Unpin before closing.', 3000);
        }
        return;
    }

    rememberClosedTab(currentWS, currentIdx);
    tabs.splice(currentIdx, 1);
    handleTabRemovalInSplit(currentIdx);
    if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
        removeWebview(state.activeViewsCache[currentWS][currentIdx]);
        state.activeViewsCache[currentWS].splice(currentIdx, 1);
    } else if (state.activeViewsCache[currentWS]) {
        state.activeViewsCache[currentWS].splice(currentIdx, 1);
    }
    removeTabMetadata(currentWS, currentIdx);

    state.sessionState.tab_pinned = state.tabPinned;
    window.flyAPI.saveSession(state.sessionState);

    const selectedIdx = tabElements.indexOf(activeListItem);
    let nextTargetIdx = selectedIdx >= 0 && selectedIdx !== currentIdx
        ? selectedIdx - (selectedIdx > currentIdx ? 1 : 0) : null;

    if (tabs.length > 0) {
        if (state.tabHistory && Array.isArray(state.tabHistory[currentWS])) {
            const updatedHistory = [];
            for (const histIdx of state.tabHistory[currentWS]) {
                if (histIdx === currentIdx) {
                    continue;
                }
                const adjusted = histIdx > currentIdx ? histIdx - 1 : histIdx;
                if (adjusted >= 0 && adjusted < tabs.length && !updatedHistory.includes(adjusted)) {
                    updatedHistory.push(adjusted);
                }
            }
            state.tabHistory[currentWS] = updatedHistory;

            // Prioritise returning to an already awake tab from recent history
            for (let i = updatedHistory.length - 1; nextTargetIdx === null && i >= 0; i--) {
                const candidate = updatedHistory[i];
                const isAwake = state.activeViewsCache[currentWS]?.[candidate] && !state.tabRestoreStates[currentWS]?.[candidate];
                if (isAwake) {
                    nextTargetIdx = candidate;
                    break;
                }
            }

            // Fallback to the most recently visited tab in history even if not loaded
            if (nextTargetIdx === null && updatedHistory.length > 0) {
                nextTargetIdx = updatedHistory[updatedHistory.length - 1];
            }
        }

        // If no tab in history was found, look for any tab that is already awake
        if (nextTargetIdx === null) {
            for (let i = 0; i < tabs.length; i++) {
                const isAwake = state.activeViewsCache[currentWS]?.[i] && !state.tabRestoreStates[currentWS]?.[i];
                if (isAwake) {
                    nextTargetIdx = i;
                    break;
                }
            }
        }

        // Ultimate fallback: adjacent tab
        if (nextTargetIdx === null) {
            nextTargetIdx = Math.min(currentIdx, tabs.length - 1);
        }
    }

    renderWorkspaceUI(nextTargetIdx);
}

export function navigateFrameBack() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;

    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
        state.activeViewsCache[currentWS][currentIdx].goBack();
    }
}

export function navigateFrameForward() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;

    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
        state.activeViewsCache[currentWS][currentIdx].goForward();
    }
}

export function toggleGlobalMediaPlayback() {
    Object.values(state.activeViewsCache).forEach(workspaceViews => {
        workspaceViews.forEach(wv => {
            if (wv && typeof wv.executeJavaScript === 'function') {
                wv.executeJavaScript(`
                    (function() {
                        try {
                            const mediaElements = Array.from(document.querySelectorAll('video, audio'));
                            if (mediaElements.length === 0) return false;
                            
                            const hasPlaying = mediaElements.some(m => !m.paused && !m.ended && m.readyState > 2);
                            
                            mediaElements.forEach(m => {
                                if (hasPlaying) {
                                    m.pause();
                                } else {
                                    m.play().catch(() => {});
                                }
                            });
                            return true;
                        } catch (e) { return false; }
                    })();
                `, false).catch(() => {});
            }
        });
    });
    if (typeof setTargetUrl === 'function') {
        setTargetUrl('Toggled global media playback', 2000);
    }
}

export function toggleTabAudioMute(workspaceId, tabIdx) {
    const ws = workspaceId || state.sessionState.current_workspace;
    const views = state.activeViewsCache[ws];
    if (!views || !views[tabIdx]) return;

    const wv = views[tabIdx];
    if (!wv || typeof wv.setAudioMuted !== 'function') return;

    if (!state.tabAudioMuted[ws]) state.tabAudioMuted[ws] = [];
    const currentlyMuted = typeof wv.isAudioMuted === 'function' ? wv.isAudioMuted() : !!state.tabAudioMuted[ws][tabIdx];
    const newMutedState = !currentlyMuted;

    wv.setAudioMuted(newMutedState);
    state.tabAudioMuted[ws][tabIdx] = newMutedState;

    const isAudible = state.tabMediaAudible[ws]?.[tabIdx] || false;
    updateTabMediaIndicator(ws, tabIdx, isAudible, newMutedState);

    const title = state.activeTitlesCache[ws]?.[tabIdx] || 'Tab';
    const statusMsg = newMutedState ? `Muted audio: ${title}` : `Unmuted audio: ${title}`;
    if (typeof setTargetUrl === 'function') {
        setTargetUrl(statusMsg, 2500);
    }
}

export function toggleActiveTabAudioMute() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;
    const tabIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    if (tabIdx >= 0) {
        toggleTabAudioMute(currentWS, tabIdx);
    }
}

export function toggleGlobalAudioMute() {
    let anyUnmuted = false;
    Object.entries(state.activeViewsCache).forEach(([wsName, views]) => {
        views.forEach((wv) => {
            if (wv && typeof wv.isAudioMuted === 'function') {
                if (!wv.isAudioMuted()) {
                    anyUnmuted = true;
                }
            }
        });
    });

    const targetMuted = anyUnmuted;

    Object.entries(state.activeViewsCache).forEach(([wsName, views]) => {
        if (!state.tabAudioMuted[wsName]) state.tabAudioMuted[wsName] = [];
        views.forEach((wv, idx) => {
            if (wv && typeof wv.setAudioMuted === 'function') {
                wv.setAudioMuted(targetMuted);
                state.tabAudioMuted[wsName][idx] = targetMuted;
                const isAudible = state.tabMediaAudible[wsName]?.[idx] || false;
                updateTabMediaIndicator(wsName, idx, isAudible, targetMuted);
            }
        });
    });

    const statusMsg = targetMuted ? 'Muted audio globally across all tabs' : 'Unmuted audio globally across all tabs';
    if (typeof setTargetUrl === 'function') {
        setTargetUrl(statusMsg, 2500);
    }
}

export function moveTabState(ws, fromIdx, toIdx) {
    if (fromIdx === toIdx) return;
    const totalTabs = state.sessionState.workspaces[ws]?.length || 0;
    if (totalTabs === 0) return;
    if (fromIdx < 0 || fromIdx >= totalTabs || toIdx < 0 || toIdx >= totalTabs) return;

    const movedArrays = new Set();

    function moveInArray(arr, defaultVal = null) {
        if (!Array.isArray(arr) || movedArrays.has(arr)) return;
        movedArrays.add(arr);

        while (arr.length < totalTabs) {
            arr.push(defaultVal);
        }

        const [item] = arr.splice(fromIdx, 1);
        arr.splice(toIdx, 0, item);
    }

    moveInArray(state.sessionState.workspaces[ws], '');
    moveInArray(state.activeViewsCache[ws], null);
    moveInArray(state.activeTitlesCache[ws], 'Loading...');
    moveInArray(state.tabRestoreStates[ws], null);
    moveInArray(state.tabMediaAudible[ws], false);
    moveInArray(state.tabAudioMuted[ws], false);
    if (state.tabFavicons) moveInArray(state.tabFavicons[ws], null);
    if (state.tabPinned) moveInArray(state.tabPinned[ws], false);

    if (state.sessionState.tab_titles) moveInArray(state.sessionState.tab_titles[ws], 'Loading...');
    if (state.sessionState.tab_restore_states) moveInArray(state.sessionState.tab_restore_states[ws], null);
    if (state.sessionState.tab_favicons) moveInArray(state.sessionState.tab_favicons[ws], null);
    if (state.sessionState.tab_pinned) moveInArray(state.sessionState.tab_pinned[ws], false);

    state.sessionState.tab_titles = state.activeTitlesCache;
    state.sessionState.tab_restore_states = state.tabRestoreStates;
    state.sessionState.tab_favicons = state.tabFavicons;
    state.sessionState.tab_pinned = state.tabPinned;

    if (state.tabHistory && Array.isArray(state.tabHistory[ws])) {
        state.tabHistory[ws] = state.tabHistory[ws].map(hIdx => {
            if (hIdx === fromIdx) return toIdx;
            if (fromIdx < toIdx && hIdx > fromIdx && hIdx <= toIdx) return hIdx - 1;
            if (fromIdx > toIdx && hIdx >= toIdx && hIdx < fromIdx) return hIdx + 1;
            return hIdx;
        });
    }

    if (isSplitActive(ws)) {
        const split = getSplitState(ws);
        if (split) {
            const remap = (idx) => {
                if (idx === fromIdx) return toIdx;
                if (fromIdx < toIdx && idx > fromIdx && idx <= toIdx) return idx - 1;
                if (fromIdx > toIdx && idx >= toIdx && idx < fromIdx) return idx + 1;
                return idx;
            };
            split.primaryIdx = remap(split.primaryIdx);
            split.secondaryIdx = remap(split.secondaryIdx);
        }
    }
}

export function togglePinTab(workspaceId, tabIdx) {
    const ws = workspaceId || state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[ws] || [];
    if (tabIdx < 0 || tabIdx >= urls.length) return;

    if (!state.tabPinned[ws]) state.tabPinned[ws] = [];
    while (state.tabPinned[ws].length < urls.length) {
        state.tabPinned[ws].push(false);
    }

    const wasPinned = !!state.tabPinned[ws][tabIdx];
    const isNowPinned = !wasPinned;

    const remainingPinnedCount = state.tabPinned[ws].filter((p, i) => p && i !== tabIdx).length;
    const targetIdx = remainingPinnedCount;

    state.tabPinned[ws][tabIdx] = isNowPinned;
    if (tabIdx !== targetIdx) {
        moveTabState(ws, tabIdx, targetIdx);
    }

    if (!state.sessionState.tab_pinned) state.sessionState.tab_pinned = {};
    state.sessionState.tab_pinned[ws] = [...state.tabPinned[ws]];
    window.flyAPI.saveSession(state.sessionState);

    renderWorkspaceUI(targetIdx);

    const title = state.activeTitlesCache[ws]?.[targetIdx] || 'Tab';
    const statusMsg = isNowPinned ? `Pinned tab: ${title}` : `Unpinned tab: ${title}`;
    if (typeof setTargetUrl === 'function') {
        setTargetUrl(statusMsg, 2500);
    }
}

export function togglePinActiveTab() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;
    const tabIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    if (tabIdx >= 0) {
        togglePinTab(currentWS, tabIdx);
    }
}

export function updateTabMediaIndicator(workspaceId, tabIdx, isAudible, isMutedOverride) {
    if (state.sessionState.current_workspace !== workspaceId) return;

    const tabList = document.querySelectorAll('#TabList li');
    const targetLi = tabList[tabIdx];
    if (!targetLi) return;

    let mediaBadge = targetLi.querySelector('.tab-media-badge');
    const isMuted = isMutedOverride !== undefined ? isMutedOverride : !!state.tabAudioMuted[workspaceId]?.[tabIdx];

    if (isAudible || isMuted) {
        if (!mediaBadge) {
            mediaBadge = document.createElement('span');
            mediaBadge.className = 'tab-media-badge' + (isMuted ? ' is-muted' : '');
            targetLi.appendChild(mediaBadge);
        } else {
            mediaBadge.className = 'tab-media-badge' + (isMuted ? ' is-muted' : '');
        }

        mediaBadge.title = isMuted ? 'Tab audio muted (click to unmute)' : 'Tab playing audio (click to mute)';
        mediaBadge.innerHTML = isMuted ? '<i class="fa-solid fa-volume-xmark"></i>' : '<i class="fa-solid fa-volume-high"></i>';

        if (!mediaBadge.__muteListenerAttached) {
            mediaBadge.__muteListenerAttached = true;
            mediaBadge.addEventListener('click', (e) => {
                e.stopPropagation();
                const currentWS = state.sessionState.current_workspace;
                const liveIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(targetLi);
                if (liveIdx >= 0) {
                    toggleTabAudioMute(currentWS, liveIdx);
                }
            });
        }
    } else {
        if (mediaBadge) {
            mediaBadge.remove();
        }
    }
}

export function ensureTabLoaded(currentWS, idx) {
    if (!currentWS) currentWS = state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[currentWS];
    if (!urls || idx < 0 || idx >= urls.length) return null;

    if (state.activeViewsCache[currentWS]?.[idx]) {
        if (state.tabRestoreStates[currentWS]) {
            state.tabRestoreStates[currentWS][idx] = null;
        }
        return state.activeViewsCache[currentWS][idx];
    }

    const restoreRecord = state.tabRestoreStates[currentWS]?.[idx];
    const targetUrl = restoreRecord?.url || urls[idx];
    const scrollX = restoreRecord?.scrollX || 0;
    const scrollY = restoreRecord?.scrollY || 0;
    const savedSessionStorage = restoreRecord?.sessionStorage || null;

    const container = document.getElementById('webview-container');
    const webview = createWebView(targetUrl, currentWS, idx);

    if (savedSessionStorage || scrollX > 0 || scrollY > 0) {
        const restoreState = () => {
            if (savedSessionStorage) {
                webview.executeJavaScript(`(function() {
                    try {
                        const data = JSON.parse(${JSON.stringify(savedSessionStorage)});
                        for (const k in data) {
                            if (!window.sessionStorage.getItem(k)) {
                                window.sessionStorage.setItem(k, data[k]);
                            }
                        }
                    } catch (e) {}
                })()`, false).catch(() => {});
            }
            if (scrollX > 0 || scrollY > 0) {
                webview.executeJavaScript(`window.scrollTo(${scrollX}, ${scrollY});`, false).catch(() => {});
                setTimeout(() => {
                    webview.executeJavaScript(`window.scrollTo(${scrollX}, ${scrollY});`, false).catch(() => {});
                }, 300);
            }
        };
        webview.addEventListener('dom-ready', restoreState, { once: true });
    }

    if (!state.activeViewsCache[currentWS]) state.activeViewsCache[currentWS] = [];
    state.activeViewsCache[currentWS][idx] = webview;
    container.appendChild(webview);

    if (state.tabRestoreStates[currentWS]) {
        state.tabRestoreStates[currentWS][idx] = null;
    }


    state.sessionState.tab_restore_states = state.tabRestoreStates;
    window.flyAPI.saveSession(state.sessionState);

    return webview;
}
