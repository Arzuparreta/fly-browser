import { state } from './state.js';
import { getActiveWebview, focusActiveWebview, getWorkspacePartition, isDarkMode, isPdfUrl, getPdfViewerUrl, extractOriginalUrl, isTargetPrivateDomain } from './utils.js';
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
import { buildUserScriptWrapper } from './userScriptShim.js';
import { reapplyWebviewDarkReader } from './darkReader.js';

export function applyCSSThemeToView() {
    // Intentionally no-op to preserve native website themes
}

function getLiveTabIndex(webview, wsName, fallbackIdx) {
    const views = state.activeViewsCache[wsName];
    if (Array.isArray(views)) {
        const liveIdx = views.indexOf(webview);
        if (liveIdx !== -1) return liveIdx;
    }
    return fallbackIdx;
}

export function createWebView(url, currentWS, idx) {
    const webview = document.createElement('webview');
    webview.setAttribute('preload', window.miseAPI.getWebviewPreloadPath());
    webview.setAttribute('allowpopups', '');
    
    if (state.globalPrivateModeActive || isTargetPrivateDomain(url)) {
        webview.setAttribute('partition', 'MisePrivateProfile');
    } else {
        webview.setAttribute('partition', getWorkspacePartition(currentWS));
    }
    
    const initialSrc = isPdfUrl(url) ? getPdfViewerUrl(url) : url;
    webview.setAttribute('src', initialSrc);

    webview.addEventListener('will-navigate', (e) => {
        webview.__miseNavigationEpoch = (webview.__miseNavigationEpoch || 0) + 1;
        if (webview.__miseInjectedScriptIds) webview.__miseInjectedScriptIds.clear();
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
        window.miseAPI.saveSession(state.sessionState);

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
            window.miseAPI.saveSession(state.sessionState);

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
        webview.__miseNavigationEpoch = (webview.__miseNavigationEpoch || 0) + 1;
        if (webview.__miseInjectedScriptIds) webview.__miseInjectedScriptIds.clear();
        webview.__miseDarkKey = null;
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        const cleanUrl = extractOriginalUrl(e.url);
        if (state.sessionState.workspaces[currentWS] && state.sessionState.workspaces[currentWS][liveIdx] !== undefined) {
            state.sessionState.workspaces[currentWS][liveIdx] = cleanUrl;
            window.miseAPI.saveSession(state.sessionState);
        }
        if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
        state.tabActivityTimestamps[currentWS][liveIdx] = Date.now();
        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const cfg = (await window.miseAPI.getBrowserSettings()) || {};
            updateTabShieldStatus(targetLi, cleanUrl, cfg.trusted_domains || []);
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

        const cleanUrl = extractOriginalUrl(e.url);
        if (state.sessionState.workspaces[currentWS] && state.sessionState.workspaces[currentWS][liveIdx] !== undefined) {
            state.sessionState.workspaces[currentWS][liveIdx] = cleanUrl;
            window.miseAPI.saveSession(state.sessionState);
        }
        if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
        state.tabActivityTimestamps[currentWS][liveIdx] = Date.now();
        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const cfg = (await window.miseAPI.getBrowserSettings()) || {};
            updateTabShieldStatus(targetLi, cleanUrl, cfg.trusted_domains || []);
        }
        if (state.sessionState.current_workspace === currentWS) {
            const activeListItem = document.querySelector('#TabList li.selected');
            const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
            if (liveIdx === currentSelectedIdx) {
                updateSecurityStatus(cleanUrl);
            }
        }
        if (webview.__miseDarkReaderEnabled) {
            reapplyWebviewDarkReader(webview).catch(() => {});
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
        const inheritsDarkReader = !!webview.__miseDarkReaderEnabled;
        
        if (!state.sessionState.workspaces[currentWS]) {
            state.sessionState.workspaces[currentWS] = [];
        }
        
        state.sessionState.workspaces[currentWS].push(targetUrl);
        window.miseAPI.saveSession(state.sessionState);
        
        const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
        if (inheritsDarkReader) {
            if (!state.tabDarkReaderStates) state.tabDarkReaderStates = {};
            if (!state.tabDarkReaderStates[currentWS]) state.tabDarkReaderStates[currentWS] = [];
            state.tabDarkReaderStates[currentWS][newTargetIdx] = true;
        }
        renderWorkspaceUI(newTargetIdx);
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
        updateTabMediaIndicator(currentWS, liveIdx, true);
    });
    
    webview.addEventListener('media-paused', () => {
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;
        if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
        state.tabMediaAudible[currentWS][liveIdx] = false;
        updateTabMediaIndicator(currentWS, liveIdx, false);
    });

    webview.addEventListener('did-start-loading', () => {
        webview.style.opacity = '1';
        webview.__miseNavigationEpoch = (webview.__miseNavigationEpoch || 0) + 1;
        if (webview.__miseInjectedScriptIds) webview.__miseInjectedScriptIds.clear();
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx >= 0) {
            if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
            state.tabActivityTimestamps[currentWS][liveIdx] = Date.now();
            updateTabMediaIndicator(currentWS, liveIdx, false);
        }
    });

    webview.addEventListener('dom-ready', async () => {
        webview.style.opacity = '1';
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx < 0) return;

        const targetLi = document.querySelectorAll('#TabList li')[liveIdx];
        if (targetLi) {
            const cfg = (await window.miseAPI.getBrowserSettings()) || {};
            updateTabShieldStatus(targetLi, webview.getURL(), cfg.trusted_domains || []);
        }

        try {
            const liveTitle = webview.getTitle();
            if (liveTitle && liveTitle !== 'about:blank') {
                syncTitle(liveTitle);
            }
        } catch (err) {}

        try {
            const hinterCode = await window.miseAPI.readHinterCode();
            if (hinterCode && typeof webview.executeJavaScript === 'function') {
                const safeExecutionWrapper = `try { ${hinterCode} } catch(e) {}`;
                webview.executeJavaScript(safeExecutionWrapper, false).catch(() => {});
            }
        } catch (err) {}

        injectMatchingUserContent(webview).catch(() => {});

        if (webview.__miseDarkReaderEnabled) {
            reapplyWebviewDarkReader(webview).catch(() => {});
        }

        if (state.sessionState.current_workspace === currentWS) {
            const activeListItem = document.querySelector('#TabList li.selected');
            const currentSelectedIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
            if (liveIdx === currentSelectedIdx) {
                updateSecurityStatus(webview.getURL());
                updateStatusBarFromActiveView();
            }
        }
    });

    webview.addEventListener('did-finish-load', () => {
        if (webview.__miseDarkReaderEnabled && !webview.__miseDarkKey) {
            reapplyWebviewDarkReader(webview).catch(() => {});
        }
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

        if (e.channel === 'guest-input-focus') {
            handleGuestInputFocus(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'guest-hints-state') {
            handleGuestHintsState(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'guest-passthrough-state') {
            handleGuestPassthroughState(!!e.args[0], currentWS, liveIdx);
        } else if (e.channel === 'reader-view-toggled') {
            const data = e.args[0] || {};
            webview.__miseReaderActive = !!data.active;
            updateReaderViewButtonUI(!!data.active);
            if (data.error) {
                setTargetUrl(`Reader View: ${data.error}`, 3000);
            } else if (data.active) {
                const titleStr = data.title ? `: ${data.title.slice(0, 45)}...` : '';
                setTargetUrl(`Reader View Active (F9 to exit)${titleStr}`, 3000);
            } else {
                setTargetUrl('Reader View Closed', 2000);
            }
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
            if (typeof window.focusSidebar === 'function') {
                window.focusSidebar();
            } else {
                const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
                if (selectedTab) selectedTab.focus();
            }
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
        case 'toggle-dark-reader':
            if (typeof window.toggleDarkReaderOnActiveTab === 'function') {
                window.toggleDarkReaderOnActiveTab();
            }
            break;
        case 'toggle-reader-view':
            toggleReaderViewOnActiveTab();
            break;
    }
}

export async function toggleReaderViewOnActiveTab() {
    const activeWv = getActiveWebview();
    if (!activeWv || typeof activeWv.executeJavaScript !== 'function') return;
    try {
        const readerCode = await window.miseAPI.readReaderCode();
        if (!readerCode) return;
        const res = await activeWv.executeJavaScript(readerCode, false);
        if (res) {
            activeWv.__miseReaderActive = !!res.active;
            updateReaderViewButtonUI(!!res.active);
            if (res.error) {
                setTargetUrl(`Reader View: ${res.error}`, 3000);
            } else if (res.active) {
                const titleStr = res.title ? `: ${res.title.slice(0, 45)}...` : '';
                setTargetUrl(`Reader View Active (F9 or Esc to exit)${titleStr}`, 3500);
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

export function updateTabShieldStatus(tabLi, url, trustedDomains = []) {
    const shieldBtn = tabLi.querySelector('.tab-shield-btn');
    if (!shieldBtn) return;

    const host = extractHostname(url);
    if (!host || url.startsWith('about:') || url.startsWith('file:')) {
        shieldBtn.style.display = 'none';
        return;
    }

    shieldBtn.style.display = 'inline-flex';
    const isTrusted = trustedDomains.some(d => host === d || host.endsWith('.' + d));
    const icon = shieldBtn.querySelector('i');

    if (isTrusted) {
        icon.className = 'fa-solid fa-shield shield-off';
        shieldBtn.title = 'Shields Down (Site is in Trusted Sites)';
    } else {
        icon.className = 'fa-solid fa-shield-halved shield-on';
        shieldBtn.title = 'Shields Active (Full Protection)';
    }
}

export async function injectMatchingUserContent(webview) {
    if (!webview || typeof webview.getURL !== 'function') return;
    const currentUrl = webview.getURL();
    if (!currentUrl || currentUrl.startsWith('about:') || currentUrl.startsWith('devtools:')) return;
    if (!window.miseAPI || typeof window.miseAPI.getUserContentForUrl !== 'function') return;

    const navEpoch = webview.__miseNavigationEpoch || 0;

    try {
        const content = await window.miseAPI.getUserContentForUrl(currentUrl);
        if (!content) return;

        // Abort if page navigated or started loading a new destination while awaiting IPC lookup
        if (webview.__miseNavigationEpoch !== navEpoch) return;
        if (typeof webview.getURL === 'function' && webview.getURL() !== currentUrl) return;

        if (!webview.__miseInjectedCSSKeys) webview.__miseInjectedCSSKeys = [];
        if (typeof webview.removeInsertedCSS === 'function' && webview.__miseInjectedCSSKeys.length > 0) {
            for (const key of webview.__miseInjectedCSSKeys) {
                try { await webview.removeInsertedCSS(key); } catch (e) {}
            }
            webview.__miseInjectedCSSKeys = [];
        }

        if (Array.isArray(content.styles)) {
            for (const item of content.styles) {
                if (item.css && typeof webview.insertCSS === 'function') {
                    try {
                        const key = await webview.insertCSS(item.css);
                        if (key) webview.__miseInjectedCSSKeys.push(key);
                    } catch (e) {}
                }
            }
        }

        if (!webview.__miseInjectedScriptIds) {
            webview.__miseInjectedScriptIds = new Set();
        }

        if (Array.isArray(content.scripts)) {
            for (const item of content.scripts) {
                const scriptId = item.scriptId || item.filename || item.name;
                if (webview.__miseInjectedScriptIds.has(scriptId)) {
                    continue;
                }
                if (item.code && typeof webview.executeJavaScript === 'function') {
                    webview.__miseInjectedScriptIds.add(scriptId);
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
        if (!window.miseAPI || typeof window.miseAPI.getUserContentForUrl !== 'function') continue;

        try {
            const content = await window.miseAPI.getUserContentForUrl(currentUrl);
            if (!content) continue;

            if (!wv.__miseInjectedCSSKeys) wv.__miseInjectedCSSKeys = [];
            if (typeof wv.removeInsertedCSS === 'function' && wv.__miseInjectedCSSKeys.length > 0) {
                for (const key of wv.__miseInjectedCSSKeys) {
                    try { await wv.removeInsertedCSS(key); } catch (e) {}
                }
                wv.__miseInjectedCSSKeys = [];
            }

            if (Array.isArray(content.styles)) {
                for (const item of content.styles) {
                    if (item.css) {
                        try {
                            const key = await wv.insertCSS(item.css);
                            if (key) wv.__miseInjectedCSSKeys.push(key);
                        } catch (e) {}
                    }
                }
            }
        } catch (err) {}
    }
}

export function attachShieldToggleListener(tabLi, webview) {
    const shieldBtn = tabLi.querySelector('.tab-shield-btn');
    if (!shieldBtn) return;

    shieldBtn.addEventListener('click', async (e) => {
        e.stopPropagation();
        const currentUrl = webview.getURL();
        const host = extractHostname(currentUrl);
        if (!host) return;

        const cfg = (await window.miseAPI.getBrowserSettings()) || {};
        let domains = Array.isArray(cfg.trusted_domains) ? [...cfg.trusted_domains] : [];

        const isTrusted = domains.some(d => host === d || host.endsWith('.' + d));

        if (isTrusted) {
            domains = domains.filter(d => d !== host && !host.endsWith('.' + d));
        } else {
            domains.push(host);
        }

        cfg.trusted_domains = domains;
        await window.miseAPI.updateBrowserSettings(cfg);

        updateTabShieldStatus(tabLi, currentUrl, domains);
        webview.reload();
    });
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
    if (!state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS] = [];
    if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    if (!state.tabFavicons) state.tabFavicons = {};
    if (!state.tabFavicons[currentWS]) state.tabFavicons[currentWS] = [];
    urls.forEach((url, idx) => {
        let cachedTitle = state.activeTitlesCache[currentWS]?.[idx];
        if (!cachedTitle || cachedTitle === 'Loading...') {
            if (state.tabSleepStates[currentWS]?.[idx]?.title) {
                cachedTitle = state.tabSleepStates[currentWS][idx].title;
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
            cachedTitle = "Loading...";
        }
        const isSleeping = !!state.tabSleepStates[currentWS][idx];
        const cachedFavicon = state.tabFavicons[currentWS]?.[idx] || null;

        const li = document.createElement('li');
        const isInitiallySelected = (idx === (targetTabToFocus !== null ? targetTabToFocus : 0));
        li.setAttribute('tabindex', isInitiallySelected ? '0' : '-1');
        li.className = 'tab-item';
        li.title = `${cachedTitle}\n${url}`;
        if (isSleeping) li.classList.add('tab-sleeping');

        // Favicon wrapper
        const favWrapper = document.createElement('div');
        favWrapper.className = 'tab-favicon-wrapper';

        const favImg = document.createElement('img');
        favImg.className = 'tab-favicon';
        favImg.alt = '';

        const favFallback = document.createElement('i');
        favFallback.className = 'fa-solid fa-globe tab-fallback-icon';

        const sleepDot = document.createElement('span');
        sleepDot.className = 'tab-sleep-dot';
        sleepDot.title = 'Sleeping tab';
        sleepDot.innerHTML = '<i class="fa-solid fa-moon"></i>';

        const audioDot = document.createElement('span');
        audioDot.className = 'tab-audio-dot';
        audioDot.title = 'Playing audio';
        audioDot.innerHTML = '<i class="fa-solid fa-volume-high"></i>';

        favWrapper.appendChild(favImg);
        favWrapper.appendChild(favFallback);
        if (isSleeping) favWrapper.appendChild(sleepDot);
        if (state.tabMediaAudible[currentWS]?.[idx]) favWrapper.appendChild(audioDot);

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
        li.appendChild(titleSpan);

        if (isSleeping) {
            const sleepBadge = document.createElement('span');
            sleepBadge.className = 'tab-sleep-badge';
            sleepBadge.title = 'Sleeping tab to save memory (click to wake)';
            sleepBadge.innerHTML = '<i class="fa-solid fa-moon"></i>';
            li.appendChild(sleepBadge);
        }

        if (state.tabMediaAudible[currentWS][idx]) {
            const mediaBadge = document.createElement('span');
            mediaBadge.className = 'tab-media-badge';
            mediaBadge.innerHTML = ' 🔊';
            li.appendChild(mediaBadge);
        }

        const shieldBtn = document.createElement('button');
        shieldBtn.className = 'tab-shield-btn';
        shieldBtn.innerHTML = '<i class="fa-solid fa-shield-halved shield-on"></i>';
        li.appendChild(shieldBtn);
        
        li.addEventListener('click', (e) => {
            if (e.target.closest('.tab-shield-btn')) return;
            if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                window.toggleDashboardView();
            }
            switchTabFocus(idx);
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

        if (isSleeping) {
            state.activeViewsCache[currentWS][idx] = null;
            window.miseAPI.getBrowserSettings().then(cfg => {
                updateTabShieldStatus(li, url, cfg?.trusted_domains || []);
            }).catch(() => {});
        } else if (!state.activeViewsCache[currentWS][idx]) {
            const webview = createWebView(url, currentWS, idx);
            if (state.tabDarkReaderStates?.[currentWS]?.[idx]) {
                webview.__miseDarkReaderEnabled = true;
            }
            attachShieldToggleListener(li, webview);
            container.appendChild(webview);
            state.activeViewsCache[currentWS][idx] = webview;
        } else {
            const existingWebview = state.activeViewsCache[currentWS][idx];
            attachShieldToggleListener(li, existingWebview);
            window.miseAPI.getBrowserSettings().then(cfg => {
                updateTabShieldStatus(li, existingWebview.getURL(), cfg?.trusted_domains || []);
            }).catch(() => {});
        }
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

    if (!state.activeViewsCache[currentWS]?.[targetIdx] || state.tabSleepStates[currentWS]?.[targetIdx]) {
        wakeTab(currentWS, targetIdx);
    }

    const currentWSViews = state.activeViewsCache[currentWS] || [];

    const allWebviews = document.getElementById('webview-container').querySelectorAll('webview');
    allWebviews.forEach((wv) => wv.style.display = 'none');
    
    if (!state.dashboardActive && currentWSViews[targetIdx]) {
        currentWSViews[targetIdx].style.display = 'flex';
        
        if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
        state.tabActivityTimestamps[currentWS][targetIdx] = Date.now();

        setTimeout(() => {
            const currentFocused = document.activeElement;
            const sidebarHasFocus = document.getElementById('Sidebar').contains(currentFocused);
            const addressBar = document.getElementById('WideAddressBar');
            const addressBarIsActive = addressBar && (addressBar.style.display === 'block' || currentFocused === addressBar);

            if (!sidebarHasFocus && !state.paletteActive && !state.helpActive && !addressBarIsActive) {
                window.miseAllowWebviewFocus = true;
                currentWSViews[targetIdx].focus();
            }
        }, 50);
    }

    updateStatusBarFromActiveView();
}

export async function spawnNewBlankTab() {
    const currentWS = state.sessionState.current_workspace;
    if (!state.sessionState.workspaces[currentWS]) state.sessionState.workspaces[currentWS] = [];

    let defaultUrl = 'https://duckduckgo.com';
    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        try {
            const cfg = await window.miseAPI.getBrowserSettings();
            if (cfg && cfg.search_engine) {
                const parsedUrl = new URL(cfg.search_engine.split('?')[0]);
                defaultUrl = `${parsedUrl.protocol}//${parsedUrl.host}`;
            }
        } catch (err) {
            defaultUrl = 'https://duckduckgo.com';
        }
    }

    state.sessionState.workspaces[currentWS].push(defaultUrl);
    window.miseAPI.saveSession(state.sessionState);
    
    const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
    if (!state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS] = [];
    state.tabSleepStates[currentWS][newTargetIdx] = null;
    if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
    state.tabActivityTimestamps[currentWS][newTargetIdx] = Date.now();
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    state.tabMediaAudible[currentWS][newTargetIdx] = false;

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
    const notesOverlay = document.getElementById('NotesOverlay');
    if (notesOverlay && notesOverlay.style.display !== 'none') {
        if (typeof window.toggleNotesOverlay === 'function') {
            window.toggleNotesOverlay();
        } else {
            notesOverlay.style.display = 'none';
        }
    }

    const currentWS = state.sessionState.current_workspace;
    if (!state.sessionState.workspaces[currentWS]) state.sessionState.workspaces[currentWS] = [];
    state.sessionState.workspaces[currentWS].push(url || "https://duckduckgo.com");
    window.miseAPI.saveSession(state.sessionState);
    
    const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
    if (!state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS] = [];
    state.tabSleepStates[currentWS][newTargetIdx] = null;
    if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
    state.tabActivityTimestamps[currentWS][newTargetIdx] = Date.now();
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    state.tabMediaAudible[currentWS][newTargetIdx] = false;

    renderWorkspaceUI(newTargetIdx);
}

export function handleTabRemoval() {
    if (state.dashboardActive) {
        const currentItem = state.dashboardItems[state.dashboardSelectionIdx];
        if (!currentItem) return;
        const [type, wsName, idx] = currentItem.payload;
        
        if (type === 'tab') {
            if (state.sessionState.workspaces[wsName].length > 1) {
                state.sessionState.workspaces[wsName].splice(idx, 1);
                if (state.activeViewsCache[wsName] && state.activeViewsCache[wsName][idx]) {
                    state.activeViewsCache[wsName][idx].remove();
                    state.activeViewsCache[wsName].splice(idx, 1);
                } else if (state.activeViewsCache[wsName]) {
                    state.activeViewsCache[wsName].splice(idx, 1);
                }
                if (state.activeTitlesCache[wsName]) state.activeTitlesCache[wsName].splice(idx, 1);
                if (state.tabSleepStates[wsName]) state.tabSleepStates[wsName].splice(idx, 1);
                if (state.tabActivityTimestamps[wsName]) state.tabActivityTimestamps[wsName].splice(idx, 1);
                if (state.tabMediaAudible[wsName]) state.tabMediaAudible[wsName].splice(idx, 1);
                if (state.sessionState.tab_titles?.[wsName]) state.sessionState.tab_titles[wsName].splice(idx, 1);
                if (state.sessionState.tab_sleep_states?.[wsName]) state.sessionState.tab_sleep_states[wsName].splice(idx, 1);
                if (state.tabDarkReaderStates?.[wsName]) state.tabDarkReaderStates[wsName].splice(idx, 1);
                if (state.tabFavicons?.[wsName]) state.tabFavicons[wsName].splice(idx, 1);
                if (state.sessionState.tab_favicons?.[wsName]) state.sessionState.tab_favicons[wsName].splice(idx, 1);
                if (state.tabHistory && Array.isArray(state.tabHistory[wsName])) {
                    state.tabHistory[wsName] = state.tabHistory[wsName]
                        .filter(hIdx => hIdx !== idx)
                        .map(hIdx => (hIdx > idx ? hIdx - 1 : hIdx));
                }

                state.sessionState.tab_dark_reader = state.tabDarkReaderStates;
                window.miseAPI.saveSession(state.sessionState);
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
                    state.activeViewsCache[wsName].forEach(wv => { if (wv) wv.remove(); });
                    delete state.activeViewsCache[wsName];
                }
                delete state.activeTitlesCache[wsName];
                delete state.tabSleepStates[wsName];
                delete state.tabActivityTimestamps[wsName];
                delete state.tabMediaAudible[wsName];
                delete state.tabFavicons?.[wsName];
                delete state.tabDarkReaderStates?.[wsName];
                delete state.tabHistory?.[wsName];
                if (state.sessionState.tab_titles?.[wsName]) delete state.sessionState.tab_titles[wsName];
                if (state.sessionState.tab_sleep_states?.[wsName]) delete state.sessionState.tab_sleep_states[wsName];
                if (state.sessionState.tab_favicons?.[wsName]) delete state.sessionState.tab_favicons[wsName];
                delete state.sessionState.workspaces[wsName];
                
                window.miseAPI.saveSession(state.sessionState);
                renderWorkspaceUI();
                if (typeof window.buildDashboardTree === 'function') {
                    window.buildDashboardTree();
                }
            }
        }
        return;
    }

    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;

    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    const tabs = state.sessionState.workspaces[currentWS] || [];
    if (tabs.length === 0) return;

    tabs.splice(currentIdx, 1);
    handleTabRemovalInSplit(currentIdx);
    if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
        state.activeViewsCache[currentWS][currentIdx].remove();
        state.activeViewsCache[currentWS].splice(currentIdx, 1);
    } else if (state.activeViewsCache[currentWS]) {
        state.activeViewsCache[currentWS].splice(currentIdx, 1);
    }
    if (state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS].splice(currentIdx, 1);
    if (state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS].splice(currentIdx, 1);
    if (state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS].splice(currentIdx, 1);
    if (state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS].splice(currentIdx, 1);
    if (state.tabFavicons?.[currentWS]) state.tabFavicons[currentWS].splice(currentIdx, 1);
    if (state.sessionState.tab_titles?.[currentWS]) state.sessionState.tab_titles[currentWS].splice(currentIdx, 1);
    if (state.sessionState.tab_sleep_states?.[currentWS]) state.sessionState.tab_sleep_states[currentWS].splice(currentIdx, 1);
    if (state.tabDarkReaderStates?.[currentWS]) state.tabDarkReaderStates[currentWS].splice(currentIdx, 1);
    if (state.sessionState.tab_favicons?.[currentWS]) state.sessionState.tab_favicons[currentWS].splice(currentIdx, 1);

    state.sessionState.tab_dark_reader = state.tabDarkReaderStates;
    window.miseAPI.saveSession(state.sessionState);
    window.miseAllowWebviewFocus = true;

    let nextTargetIdx = null;

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
            for (let i = updatedHistory.length - 1; i >= 0; i--) {
                const candidate = updatedHistory[i];
                const isAwake = state.activeViewsCache[currentWS]?.[candidate] && !state.tabSleepStates[currentWS]?.[candidate];
                if (isAwake) {
                    nextTargetIdx = candidate;
                    break;
                }
            }

            // Fallback to the most recently visited tab in history even if sleeping
            if (nextTargetIdx === null && updatedHistory.length > 0) {
                nextTargetIdx = updatedHistory[updatedHistory.length - 1];
            }
        }

        // If no tab in history was found, look for any tab that is already awake
        if (nextTargetIdx === null) {
            for (let i = 0; i < tabs.length; i++) {
                const isAwake = state.activeViewsCache[currentWS]?.[i] && !state.tabSleepStates[currentWS]?.[i];
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
}

export function updateTabMediaIndicator(workspaceId, tabIdx, isAudible) {
    if (state.sessionState.current_workspace !== workspaceId) return;

    const tabList = document.querySelectorAll('#TabList li');
    const targetLi = tabList[tabIdx];
    if (!targetLi) return;

    let mediaBadge = targetLi.querySelector('.tab-media-badge');

    if (isAudible) {
        if (!mediaBadge) {
            mediaBadge = document.createElement('span');
            mediaBadge.className = 'tab-media-badge';
            mediaBadge.innerHTML = ' 🔊';
            const shieldBtn = targetLi.querySelector('.tab-shield-btn');
            if (shieldBtn) {
                targetLi.insertBefore(mediaBadge, shieldBtn);
            } else {
                targetLi.appendChild(mediaBadge);
            }
        }
    } else {
        if (mediaBadge) {
            mediaBadge.remove();
        }
    }
}

export function updateTabSleepUI(tabIdx, isSleeping) {
    const tabList = document.querySelectorAll('#TabList li');
    const targetLi = tabList[tabIdx];
    if (!targetLi) return;

    if (isSleeping) {
        targetLi.classList.add('tab-sleeping');
        let sleepBadge = targetLi.querySelector('.tab-sleep-badge');
        if (!sleepBadge) {
            sleepBadge = document.createElement('span');
            sleepBadge.className = 'tab-sleep-badge';
            sleepBadge.title = 'Sleeping tab to save memory (click to wake)';
            sleepBadge.innerHTML = '<i class="fa-solid fa-moon"></i>';
            const shieldBtn = targetLi.querySelector('.tab-shield-btn');
            if (shieldBtn) {
                targetLi.insertBefore(sleepBadge, shieldBtn);
            } else {
                targetLi.appendChild(sleepBadge);
            }
        }
    } else {
        targetLi.classList.remove('tab-sleeping');
        const sleepBadge = targetLi.querySelector('.tab-sleep-badge');
        if (sleepBadge) {
            sleepBadge.remove();
        }
    }
}

export function wakeTab(currentWS, idx) {
    if (!currentWS) currentWS = state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[currentWS];
    if (!urls || idx < 0 || idx >= urls.length) return null;

    if (state.activeViewsCache[currentWS]?.[idx]) {
        if (state.tabSleepStates[currentWS]) {
            state.tabSleepStates[currentWS][idx] = null;
        }
        if (currentWS === state.sessionState.current_workspace) {
            updateTabSleepUI(idx, false);
        }
        return state.activeViewsCache[currentWS][idx];
    }

    const sleepRecord = state.tabSleepStates[currentWS]?.[idx];
    const targetUrl = sleepRecord?.url || urls[idx];
    const scrollX = sleepRecord?.scrollX || 0;
    const scrollY = sleepRecord?.scrollY || 0;
    const savedSessionStorage = sleepRecord?.sessionStorage || null;

    const container = document.getElementById('webview-container');
    const webview = createWebView(targetUrl, currentWS, idx);
    if (sleepRecord?.darkReader || state.tabDarkReaderStates?.[currentWS]?.[idx]) {
        webview.__miseDarkReaderEnabled = true;
    }

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

    if (state.tabSleepStates[currentWS]) {
        state.tabSleepStates[currentWS][idx] = null;
    }

    if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
    state.tabActivityTimestamps[currentWS][idx] = Date.now();

    if (currentWS === state.sessionState.current_workspace) {
        const tabList = document.querySelectorAll('#TabList li');
        const targetLi = tabList[idx];
        if (targetLi) {
            attachShieldToggleListener(targetLi, webview);
            updateTabSleepUI(idx, false);
        }
    }

    state.sessionState.tab_sleep_states = state.tabSleepStates;
    window.miseAPI.saveSession(state.sessionState);

    return webview;
}

export async function hibernateTab(currentWS, idx) {
    if (!currentWS) currentWS = state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[currentWS];
    if (!urls || idx < 0 || idx >= urls.length) return false;

    if (state.tabSleepStates[currentWS] && state.tabSleepStates[currentWS][idx]) {
        return false;
    }

    if (currentWS === state.sessionState.current_workspace) {
        const tabItems = document.querySelectorAll('#TabList li');
        const activeLi = document.querySelector('#TabList li.selected');
        const activeIdx = activeLi ? Array.from(tabItems).indexOf(activeLi) : -1;
        if (idx === activeIdx && !state.dashboardActive) {
            return false;
        }
    }

    if (state.tabMediaAudible[currentWS]?.[idx]) {
        return false;
    }

    const webview = state.activeViewsCache[currentWS]?.[idx];
    if (webview) {
        if (typeof webview.isCurrentlyAudible === 'function' && webview.isCurrentlyAudible()) {
            if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
            state.tabMediaAudible[currentWS][idx] = true;
            return false;
        }
    }

    let scrollX = 0;
    let scrollY = 0;
    let finalUrl = urls[idx];
    let finalTitle = state.activeTitlesCache[currentWS]?.[idx] || 'Tab';
    let savedSessionStorage = null;
    let isDarkReader = false;

    if (webview) {
        isDarkReader = !!webview.__miseDarkReaderEnabled;
        if (!state.tabDarkReaderStates) state.tabDarkReaderStates = {};
        if (!state.tabDarkReaderStates[currentWS]) state.tabDarkReaderStates[currentWS] = [];
        state.tabDarkReaderStates[currentWS][idx] = isDarkReader;
        try {
            const scrollPos = await webview.executeJavaScript(`[window.scrollX || window.pageXOffset || 0, window.scrollY || window.pageYOffset || 0]`);
            if (Array.isArray(scrollPos)) {
                scrollX = scrollPos[0] || 0;
                scrollY = scrollPos[1] || 0;
            }
        } catch (err) {}

        try {
            const rawSession = await webview.executeJavaScript(`(function() {
                try {
                    const data = {};
                    for (let i = 0; i < window.sessionStorage.length; i++) {
                        const key = window.sessionStorage.key(i);
                        data[key] = window.sessionStorage.getItem(key);
                    }
                    return JSON.stringify(data);
                } catch (e) {
                    return null;
                }
            })()`);
            if (rawSession && rawSession !== '{}') {
                savedSessionStorage = rawSession;
            }
        } catch (err) {}

        try {
            const liveUrl = webview.getURL();
            if (liveUrl && !liveUrl.startsWith('about:blank')) {
                finalUrl = liveUrl;
                urls[idx] = finalUrl;
            }
            const liveTitle = webview.getTitle();
            if (liveTitle) {
                finalTitle = liveTitle;
                if (!state.activeTitlesCache[currentWS]) state.activeTitlesCache[currentWS] = [];
                state.activeTitlesCache[currentWS][idx] = finalTitle;
            }
        } catch (err) {}

        try {
            if (window.miseAPI && typeof window.miseAPI.flushSessionStore === 'function') {
                await window.miseAPI.flushSessionStore({ workspace: currentWS });
            }
        } catch (err) {}

        try {
            webview.remove();
        } catch (err) {}
        state.activeViewsCache[currentWS][idx] = null;
    }

    if (!state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS] = [];
    state.tabSleepStates[currentWS][idx] = {
        url: finalUrl,
        title: finalTitle,
        scrollX,
        scrollY,
        sessionStorage: savedSessionStorage,
        darkReader: isDarkReader,
        hibernatedAt: Date.now()
    };

    if (currentWS === state.sessionState.current_workspace) {
        updateTabSleepUI(idx, true);
    }

    state.sessionState.tab_sleep_states = state.tabSleepStates;
    state.sessionState.tab_titles = state.activeTitlesCache;
    window.miseAPI.saveSession(state.sessionState);

    return true;
}

export async function hibernateInactiveTabs() {
    const currentWS = state.sessionState.current_workspace;
    const tabItems = document.querySelectorAll('#TabList li');
    const activeLi = document.querySelector('#TabList li.selected');
    const activeIdx = activeLi ? Array.from(tabItems).indexOf(activeLi) : -1;
    const splitActive = isSplitActive(currentWS);
    const split = getSplitState(currentWS);

    for (const wsName of Object.keys(state.sessionState.workspaces || {})) {
        const urls = state.sessionState.workspaces[wsName] || [];
        for (let i = 0; i < urls.length; i++) {
            if (wsName === currentWS && !state.dashboardActive) {
                if (i === activeIdx) continue;
                if (splitActive && (i === split.primaryIdx || i === split.secondaryIdx)) continue;
            }
            if (state.tabSleepStates[wsName] && state.tabSleepStates[wsName][i]) {
                continue;
            }
            if (state.tabMediaAudible[wsName]?.[i]) {
                continue;
            }
            await hibernateTab(wsName, i);
        }
    }

    if (window.miseAPI && typeof window.miseAPI.compactMemory === 'function') {
        window.miseAPI.compactMemory().catch(() => {});
    }
}

export function wakeAllTabsInWorkspace(wsName) {
    if (!wsName) wsName = state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[wsName] || [];
    urls.forEach((_, idx) => {
        if (state.tabSleepStates[wsName]?.[idx]) {
            wakeTab(wsName, idx);
        }
    });
}

let sleepTimerId = null;

export function initializeTabSleepManager() {
    if (sleepTimerId) clearInterval(sleepTimerId);

    sleepTimerId = setInterval(async () => {
        try {
            const cfg = (await window.miseAPI.getBrowserSettings()) || {};
            const timeoutMinutes = cfg.tab_sleep_timeout_minutes !== undefined ? parseInt(cfg.tab_sleep_timeout_minutes, 10) : 15;
            if (timeoutMinutes <= 0) return;

            const timeoutMs = timeoutMinutes * 60 * 1000;
            const now = Date.now();
            const currentWS = state.sessionState.current_workspace;

            const tabItems = document.querySelectorAll('#TabList li');
            const activeLi = document.querySelector('#TabList li.selected');
            const activeIdx = activeLi ? Array.from(tabItems).indexOf(activeLi) : -1;
            const splitActive = isSplitActive(currentWS);
            const split = getSplitState(currentWS);

            for (const wsName of Object.keys(state.sessionState.workspaces || {})) {
                const urls = state.sessionState.workspaces[wsName] || [];
                for (let i = 0; i < urls.length; i++) {
                    if (wsName === currentWS && !state.dashboardActive) {
                        if (i === activeIdx) continue;
                        if (splitActive && (i === split.primaryIdx || i === split.secondaryIdx)) continue;
                    }
                    if (state.tabSleepStates[wsName] && state.tabSleepStates[wsName][i]) {
                        continue;
                    }
                    if (state.tabMediaAudible[wsName]?.[i]) {
                        continue;
                    }
                    const lastActive = state.tabActivityTimestamps[wsName]?.[i] || 0;
                    if (lastActive > 0 && (now - lastActive) >= timeoutMs) {
                        hibernateTab(wsName, i);
                    }
                }
            }
        } catch (err) {}
    }, 30000);
}
