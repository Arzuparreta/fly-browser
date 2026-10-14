import { hasActiveOverlay } from './focus.js';
import { state } from './state.js';
import { getActiveWebview, focusActiveWebview, removeWebview, getWorkspacePartition, isDarkMode, isPdfUrl, getPdfViewerUrl, extractOriginalUrl, isTargetPrivateDomain } from './utils.js';
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
import { buildUserScriptWrapper } from './userScriptShim.js';
import { reapplyWebviewDarkReader } from './darkReader.js';
import { exportArticleToNotes, exportArticleToExternalApp, openNotesInExternalApp } from './overlays/notes.js';

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
    webview.setAttribute('preload', window.miseAPI.getWebviewPreloadPath());
    webview.setAttribute('allowpopups', '');
    
    if (state.globalPrivateModeActive || isTargetPrivateDomain(url)) {
        webview.setAttribute('partition', 'MisePrivateProfile');
    } else {
        webview.setAttribute('partition', getWorkspacePartition(currentWS));
    }
    
    const initialSrc = isPdfUrl(url) ? getPdfViewerUrl(url) : url;
    loadWebviewSource(webview, currentWS, initialSrc);

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
        if (!state.tabPinned[currentWS]) state.tabPinned[currentWS] = [];
        state.tabPinned[currentWS][newTargetIdx] = false;
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
        webview.__miseNavigationEpoch = (webview.__miseNavigationEpoch || 0) + 1;
        if (webview.__miseInjectedScriptIds) webview.__miseInjectedScriptIds.clear();
        const liveIdx = getLiveTabIndex(webview, currentWS, idx);
        if (liveIdx >= 0) {
            if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
            state.tabActivityTimestamps[currentWS][liveIdx] = Date.now();
            updateTabMediaIndicator(currentWS, liveIdx, false, false);
        }
    });

    webview.addEventListener('dom-ready', async () => {
        webview.__miseKeyboardMode = {};
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

        if (e.channel === 'guest-pointer-enter') {
            if (webview.getClientRects().length) document.getElementById('Sidebar').classList.remove('pointer-inside');
        } else if (e.channel === 'guest-input-focus') {
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
                setTargetUrl(`Reader view active (Escape to exit)${titleStr}`, 3000);
            } else {
                setTargetUrl('Reader View Closed', 2000);
            }
        } else if (e.channel === 'keyboard-action') {
            // Finish the guest input event before an action can destroy its own webview.
            const action = e.args[0];
            setTimeout(() => window.dispatchKeyboardAction(action), 0);
        } else if (e.channel === 'reader-export-notes') {
            exportArticleToNotes(e.args[0]);
        } else if (e.channel === 'reader-export-external') {
            exportArticleToExternalApp(e.args[0]);
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
        case 'toggle-tab-audio-mute':
            toggleTabAudioMute(currentWS, idx);
            break;
        case 'toggle-global-audio-mute':
            toggleGlobalAudioMute();
            break;
        case 'toggle-pin-tab':
            togglePinActiveTab();
            break;
        case 'export-reader-to-notes':
            exportActiveTabToNotes();
            break;
        case 'export-reader-to-external':
            exportActiveTabToExternal();
            break;
        case 'open-notes-external':
            openNotesInExternalApp();
            break;
        case 'escape':
            if (state.notesActive && typeof window.toggleNotesOverlay === 'function') {
                window.toggleNotesOverlay();
            } else if (state.bookmarksActive && typeof window.toggleBookmarksOverlay === 'function') {
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
                setTargetUrl(`Reader view active (Escape to exit)${titleStr}`, 3500);
            } else {
                setTargetUrl('Reader View Closed', 2000);
            }
        }
    } catch (err) {
        console.error('Failed to execute reader view:', err);
    }
}

export async function exportActiveTabToNotes() {
    const activeWv = getActiveWebview();
    if (!activeWv || typeof activeWv.executeJavaScript !== 'function') return;
    try {
        const extractScript = `(function() {
            var overlay = document.getElementById('mise-reader-overlay');
            if (overlay && overlay.shadowRoot) {
                var title = overlay.shadowRoot.querySelector('.article-title')?.textContent || document.title;
                var byline = overlay.shadowRoot.querySelector('.article-byline')?.textContent || '';
                var body = overlay.shadowRoot.querySelector('.article-body')?.innerText || '';
                var pill = overlay.shadowRoot.querySelector('.article-domain-pill')?.textContent || window.location.hostname;
                return {
                    isReader: true,
                    title: title,
                    byline: byline,
                    url: window.location.href,
                    siteName: pill,
                    textContent: body
                };
            }
            var selected = window.getSelection() ? window.getSelection().toString() : '';
            return {
                isReader: false,
                title: document.title,
                url: window.location.href,
                siteName: window.location.hostname,
                textContent: selected || ''
            };
        })()`;
        const articleData = await activeWv.executeJavaScript(extractScript, false);
        if (articleData) {
            await exportArticleToNotes(articleData);
        }
    } catch (err) {
        console.error('Failed to export tab to notes:', err);
    }
}

export async function exportActiveTabToExternal() {
    const activeWv = getActiveWebview();
    if (!activeWv || typeof activeWv.executeJavaScript !== 'function') return;
    try {
        const extractScript = `(function() {
            var overlay = document.getElementById('mise-reader-overlay');
            if (overlay && overlay.shadowRoot) {
                var title = overlay.shadowRoot.querySelector('.article-title')?.textContent || document.title;
                var byline = overlay.shadowRoot.querySelector('.article-byline')?.textContent || '';
                var body = overlay.shadowRoot.querySelector('.article-body')?.innerText || '';
                var pill = overlay.shadowRoot.querySelector('.article-domain-pill')?.textContent || window.location.hostname;
                return {
                    isReader: true,
                    title: title,
                    byline: byline,
                    url: window.location.href,
                    siteName: pill,
                    textContent: body
                };
            }
            var selected = window.getSelection() ? window.getSelection().toString() : '';
            return {
                isReader: false,
                title: document.title,
                url: window.location.href,
                siteName: window.location.hostname,
                textContent: selected || ''
            };
        })()`;
        const articleData = await activeWv.executeJavaScript(extractScript, false);
        if (articleData) {
            await exportArticleToExternalApp(articleData);
        }
    } catch (err) {
        console.error('Failed to export tab to external app:', err);
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
        const isPinned = !!state.tabPinned?.[currentWS]?.[idx];
        li.className = 'tab-item';
        li.title = `${cachedTitle}\n${url}`;
        if (isSleeping) li.classList.add('tab-sleeping');
        if (isPinned) li.classList.add('tab-pinned');

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

        const pinDot = document.createElement('span');
        pinDot.className = 'tab-pin-dot';
        pinDot.title = 'Pinned tab';
        pinDot.innerHTML = '<i class="fa-solid fa-thumbtack"></i>';

        favWrapper.appendChild(favImg);
        favWrapper.appendChild(favFallback);
        if (isSleeping) favWrapper.appendChild(sleepDot);
        if (state.tabMediaAudible[currentWS]?.[idx]) favWrapper.appendChild(audioDot);
        if (isPinned) favWrapper.appendChild(pinDot);
        if (idx < 9) {
            const indexBadge = document.createElement('span');
            indexBadge.className = 'tab-index-badge';
            indexBadge.textContent = String(idx + 1);
            favWrapper.appendChild(indexBadge);
        }

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

        const shieldBtn = document.createElement('button');
        shieldBtn.className = 'tab-shield-btn';
        shieldBtn.innerHTML = '<i class="fa-solid fa-shield-halved shield-on"></i>';
        li.appendChild(shieldBtn);
        
        li.addEventListener('click', (e) => {
            if (e.target.closest('.tab-shield-btn') || e.target.closest('.tab-media-badge') || e.target.closest('.tab-pin-badge')) return;
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

        const targetView = currentWSViews[targetIdx];
        clearTimeout(pendingTabFocus);
        pendingTabFocus = setTimeout(() => {
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

            if (!sidebarHasFocus && !hasActiveOverlay() && !addressBarIsActive) {
                window.miseAllowWebviewFocus = true;
                targetView.focus();
            }
        }, 50);
    }

    updateStatusBarFromActiveView();
}

export function jumpToTabByIndex(oneBasedIdx) {
    const currentWS = state.sessionState.current_workspace;
    const tabs = state.sessionState.workspaces[currentWS] || [];
    if (tabs.length === 0) return;

    let targetIdx = oneBasedIdx - 1;
    // Standard browser convention: 9 jumps to 9th tab, or last tab if fewer than 9
    if (oneBasedIdx === 9 && targetIdx >= tabs.length) {
        targetIdx = tabs.length - 1;
    }

    if (targetIdx < 0 || targetIdx >= tabs.length) return;

    if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
        window.toggleDashboardView();
    }

    switchTabFocus(targetIdx);

    const tabItems = document.querySelectorAll('#TabList li');
    if (tabItems && tabItems[targetIdx]) {
        tabItems.forEach((item, idx) => item.setAttribute('tabindex', idx === targetIdx ? '0' : '-1'));
        const sidebar = document.getElementById('Sidebar');
        const isSidebarFocused = document.activeElement && sidebar && sidebar.contains(document.activeElement);
        if (isSidebarFocused) {
            tabItems[targetIdx].focus();
        } else {
            focusActiveWebview();
        }
    }
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

// Session metadata can share arrays with the live caches after hibernation or saving.
function removeTabMetadata(workspace, index) {
    const arrays = new Set([
        state.activeTitlesCache, state.tabSleepStates, state.tabActivityTimestamps,
        state.tabMediaAudible, state.tabAudioMuted, state.tabDarkReaderStates,
        state.tabFavicons, state.tabPinned, state.sessionState.tab_titles,
        state.sessionState.tab_sleep_states, state.sessionState.tab_dark_reader,
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

                state.sessionState.tab_dark_reader = state.tabDarkReaderStates;
                state.sessionState.tab_pinned = state.tabPinned;
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
                    state.activeViewsCache[wsName].forEach(removeWebview);
                    delete state.activeViewsCache[wsName];
                }
                delete state.activeTitlesCache[wsName];
                delete state.tabSleepStates[wsName];
                delete state.tabActivityTimestamps[wsName];
                delete state.tabMediaAudible[wsName];
                delete state.tabAudioMuted[wsName];
                delete state.tabFavicons?.[wsName];
                delete state.tabDarkReaderStates?.[wsName];
                delete state.tabHistory?.[wsName];
                delete state.tabPinned?.[wsName];
                if (state.sessionState.tab_pinned?.[wsName]) delete state.sessionState.tab_pinned[wsName];
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

    state.sessionState.tab_dark_reader = state.tabDarkReaderStates;
    state.sessionState.tab_pinned = state.tabPinned;
    window.miseAPI.saveSession(state.sessionState);
    window.miseAllowWebviewFocus = true;

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
    moveInArray(state.tabSleepStates[ws], null);
    moveInArray(state.tabActivityTimestamps[ws], 0);
    moveInArray(state.tabMediaAudible[ws], false);
    moveInArray(state.tabAudioMuted[ws], false);
    if (state.tabFavicons) moveInArray(state.tabFavicons[ws], null);
    if (state.tabDarkReaderStates) moveInArray(state.tabDarkReaderStates[ws], false);
    if (state.tabPinned) moveInArray(state.tabPinned[ws], false);

    if (state.sessionState.tab_titles) moveInArray(state.sessionState.tab_titles[ws], 'Loading...');
    if (state.sessionState.tab_sleep_states) moveInArray(state.sessionState.tab_sleep_states[ws], null);
    if (state.sessionState.tab_favicons) moveInArray(state.sessionState.tab_favicons[ws], null);
    if (state.sessionState.tab_pinned) moveInArray(state.sessionState.tab_pinned[ws], false);

    state.sessionState.tab_titles = state.activeTitlesCache;
    state.sessionState.tab_sleep_states = state.tabSleepStates;
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
    window.miseAPI.saveSession(state.sessionState);

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
            const shieldBtn = targetLi.querySelector('.tab-shield-btn');
            if (shieldBtn) {
                targetLi.insertBefore(mediaBadge, shieldBtn);
            } else {
                targetLi.appendChild(mediaBadge);
            }
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
            removeWebview(webview);
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
            if (state.tabPinned?.[wsName]?.[i]) {
                continue;
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
                    if (state.tabPinned?.[wsName]?.[i]) {
                        continue;
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
