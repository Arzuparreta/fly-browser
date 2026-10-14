import { state } from './modules/state.js';
import { 
    isDarkMode, 
    applyThemeToOverlayElement, 
    escapeHtml, 
    isTargetScript, 
    getActiveWebview, 
    focusActiveWebview,
    isTargetPrivateDomain 
} from './modules/utils.js';
import { 
    applyThemeVisuals, 
    syncVisualSettingsInputs 
} from './modules/theme.js';

import { 
    renderWorkspaceUI, 
    switchTabFocus, 
    jumpToTabByIndex,
    spawnNewBlankTab, 
    spawnTabWithUrl, 
    handleTabRemoval, 
    navigateFrameBack, 
    navigateFrameForward, 
    applyCSSThemeToView,
    toggleGlobalMediaPlayback,
    toggleTabAudioMute,
    toggleActiveTabAudioMute,
    toggleGlobalAudioMute,
    togglePinTab,
    togglePinActiveTab,
    initializeTabSleepManager,
    hibernateInactiveTabs,
    wakeTab,
    wakeAllTabsInWorkspace,
    reapplyActiveUserStyles,
    toggleReaderViewOnActiveTab,
    exportActiveTabToNotes,
    exportActiveTabToExternal
} from './modules/webview.js';

import { 
    loadBookmarksAndQuickmarks, 
    promptQuickmark, 
    addCurrentPageToBookmarks, 
    toggleBookmarksOverlay, 
    renderBookmarksList, 
    updateBookmarkVisualSelection, 
    deleteBookmark, 
    deleteQuickmark, 
    setupBookmarkOverlayListeners 
} from './modules/overlays/bookmarks.js';

import { 
    commandRegistry, 
    toggleCommandPaletteView, 
    closeCommandPalette,
    togglePreferencesView,
    filterPaletteCommands, 
    updatePaletteVisualSelection, 
    handlePaletteInputNavigation, 
    executePaletteSelection 
} from './modules/overlays/commandPalette.js';

import { 
    toggleDashboardView, 
    buildDashboardTree, 
    updateDashboardVisualSelection, 
    executeDashboardItemActivation, 
    showWorkspaceInputDialog, 
    hideWorkspaceInputDialog, 
    processWorkspaceCreation 
} from './modules/overlays/dashboard.js';

import { 
    toggleHistoryOverlay, 
    filterHistoryItems 
} from './modules/overlays/history.js';

import { 
    parseMarkdownToHtml, 
    renderNotesView, 
    toggleNotesOverlay, 
    setupNotesListeners,
    toggleExpandNotesOverlay,
    openNotesInExternalApp,
    appendSelectionToNotes
} from './modules/overlays/notes.js';

import { 
    displayAddressOverlay, 
    handleNavigation, 
    setupAddressBarAutocomplete, 
    renderSuggestions, 
    updateSuggestionHighlight, 
    selectSuggestion, 
    hideSuggestions 
} from './modules/navigation.js';

import { openTransientShareModal } from './modules/overlays/shareModal.js';
import { initSearchEnginePreference, renderSearchAliasesUI } from './modules/overlays/searchEngine.js';
import { initDownloadShelf, toggleDownloadShelf } from './modules/downloads.js';
import { 
    toggleSplitView, 
    cycleSplitOrientation, 
    switchSplitFocus, 
    swapSplitPanes, 
    closeSplitView, 
    isSplitActive, 
    applySplitLayout,
    openUrlInSplit 
} from './modules/splitView.js';

import { 
    initStatusBar, 
    toggleStatusBar, 
    updateStatusBarFromActiveView, 
    recalculateMode,
    togglePassthroughMode,
    setTargetUrl
} from './modules/statusBar.js';

import { 
    toggleDarkReaderOnActiveTab, 
    updateDarkReaderButtonUI 
} from './modules/darkReader.js';

import {
    initClock,
    toggleClock,
    cycleClockFormat
} from './modules/clock.js';

import {
    toggleDiagnosticsView,
    closeDiagnosticsView,
    toggleDiagnosticsRecording,
    setupDiagnosticsListeners
} from './modules/overlays/diagnostics.js';

import {
    initWindowControls,
    toggleWindowControls
} from './modules/windowControls.js';

// Attach functions needed across module boundaries to window
window.toggleDarkReaderOnActiveTab = toggleDarkReaderOnActiveTab;
window.toggleReaderViewOnActiveTab = toggleReaderViewOnActiveTab;
window.toggleDashboardView = toggleDashboardView;
window.displayAddressOverlay = displayAddressOverlay;
window.triggerLinkHints = triggerLinkHints;
window.toggleInPageSearch = toggleInPageSearch;
window.toggleNotesOverlay = toggleNotesOverlay;
window.toggleExpandNotesOverlay = toggleExpandNotesOverlay;
window.openNotesInExternalApp = openNotesInExternalApp;
window.appendSelectionToNotes = appendSelectionToNotes;
window.renderSearchAliasesUI = renderSearchAliasesUI;
window.exportActiveTabToNotes = exportActiveTabToNotes;
window.exportActiveTabToExternal = exportActiveTabToExternal;
window.toggleDownloadShelf = toggleDownloadShelf;
window.toggleStatusBar = toggleStatusBar;
window.toggleClock = toggleClock;
window.toggleWindowControls = toggleWindowControls;
window.cycleClockFormat = cycleClockFormat;
window.togglePassthroughMode = togglePassthroughMode;
window.recalculateStatusMode = recalculateMode;
window.updateStatusBarFromActiveView = updateStatusBarFromActiveView;
window.toggleZenMode = toggleZenMode;
window.toggleSidebarExpansion = toggleSidebarExpansion;
window.toggleSidebarPin = toggleSidebarPin;
window.applySidebarMode = applySidebarMode;
window.toggleInterfaceTheme = toggleInterfaceTheme;
window.toggleWebviewTheme = toggleWebviewTheme;
window.applyThemeVisuals = applyThemeVisuals;
window.togglePreferencesView = togglePreferencesView;
window.toggleHistoryOverlay = toggleHistoryOverlay;
window.handlePrivateBrowsingStateShift = handlePrivateBrowsingStateShift;
window.executeSurgicalCookieWipe = executeSurgicalCookieWipe;
window.executeGlobalCacheWipe = executeGlobalCacheWipe;
window.toggleDiagnosticsView = toggleDiagnosticsView;
window.closeDiagnosticsView = closeDiagnosticsView;
window.toggleDiagnosticsRecording = toggleDiagnosticsRecording;
window.toggleActiveDevTools = toggleActiveDevTools;
window.toggleCommandPaletteView = toggleCommandPaletteView;
window.closeCommandPalette = closeCommandPalette;
window.buildDashboardTree = buildDashboardTree;
window.toggleGlobalMediaPlayback = toggleGlobalMediaPlayback;
window.toggleTabAudioMute = toggleTabAudioMute;
window.toggleActiveTabAudioMute = toggleActiveTabAudioMute;
window.toggleGlobalAudioMute = toggleGlobalAudioMute;
window.togglePinTab = togglePinTab;
window.togglePinActiveTab = togglePinActiveTab;
window.toggleSplitView = toggleSplitView;
window.cycleSplitOrientation = cycleSplitOrientation;
window.switchSplitFocus = switchSplitFocus;
window.swapSplitPanes = swapSplitPanes;
window.closeSplitView = closeSplitView;
window.isSplitActive = isSplitActive;
window.applySplitLayout = applySplitLayout;
window.openUrlInSplit = openUrlInSplit;
window.reapplyActiveUserStyles = reapplyActiveUserStyles;
window.toggleBookmarksOverlay = toggleBookmarksOverlay;
window.promptQuickmark = promptQuickmark;
window.addCurrentPageToBookmarks = addCurrentPageToBookmarks;
window.focusSidebar = focusSidebar;

export async function testCleanGoogleSignIn() {
    try {
        if (window.miseAPI && typeof window.miseAPI.clearGoogleAuthCookies === 'function') {
            await window.miseAPI.clearGoogleAuthCookies('MisePrivateProfile');
        }
        setTargetUrl('Opening clean Google sign-in test in volatile partition...', 3500);
        spawnTabWithUrl('https://accounts.google.com/ServiceLogin?mise_test_auth=1');
    } catch (err) {
        console.error('Failed to launch clean Google sign-in test:', err);
    }
}
window.testCleanGoogleSignIn = testCleanGoogleSignIn;

export function focusSidebar() {
    const activeEl = document.activeElement;
    const isInsideTabList = activeEl && (activeEl.closest('#TabList') || activeEl.id === 'TabList');
    const isInsideNavAction = activeEl && activeEl.closest('.nav-action-layout');
    const isInsideBottomLayout = activeEl && activeEl.closest('.bottom-theme-layout');

    if (isInsideTabList) {
        const backBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
        if (backBtn) backBtn.focus();
    } else if (isInsideNavAction || isInsideBottomLayout) {
        const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
        if (selectedTab) selectedTab.focus();
    } else {
        const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
        if (selectedTab) {
            selectedTab.focus();
        } else {
            const navBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
            if (navBtn) navBtn.focus();
        }
    }
}

export function displayUpdateNotification(updateInfo) {
    if (!updateInfo || !updateInfo.updateAvailable) return;
    
    // Update notification banner in Preferences modal
    const banner = document.getElementById('UpdateNotificationBanner');
    const textEl = document.getElementById('UpdateNotificationText');
    const actionBtn = document.getElementById('UpdateActionBtn');
    
    if (banner && textEl) {
        textEl.textContent = `A newer release (${updateInfo.latestVersion}) is available. You are running v${updateInfo.currentVersion}.`;
        banner.style.display = 'flex';
        if (actionBtn) {
            actionBtn.style.display = 'inline-block';
            actionBtn.onclick = () => {
                if (window.miseAPI && typeof window.miseAPI.openExternal === 'function') {
                    window.miseAPI.openExternal(updateInfo.releaseUrl);
                }
            };
        }
    }

    // Sidebar update icon indicator
    const sidebarIndicator = document.getElementById('update-indicator-btn');
    if (sidebarIndicator) {
        sidebarIndicator.style.display = 'inline-flex';
        sidebarIndicator.title = `Mise ${updateInfo.latestVersion} available. Click to review release.`;
        sidebarIndicator.onclick = () => {
            if (typeof window.togglePreferencesView === 'function') {
                window.togglePreferencesView();
            }
        };
    }
}

export async function triggerUpdateCheck(manual = false) {
    const checkBtn = document.getElementById('CheckForUpdatesBtn');
    if (checkBtn) {
        checkBtn.disabled = true;
        checkBtn.textContent = 'Checking...';
    }

    try {
        if (window.miseAPI && typeof window.miseAPI.checkForUpdates === 'function') {
            const result = await window.miseAPI.checkForUpdates(manual);
            if (result && result.updateAvailable) {
                displayUpdateNotification(result);
                if (checkBtn) checkBtn.textContent = 'Update Available';
            } else {
                if (checkBtn) checkBtn.textContent = 'Up to Date';
                if (manual) {
                    const banner = document.getElementById('UpdateNotificationBanner');
                    const textEl = document.getElementById('UpdateNotificationText');
                    const actionBtn = document.getElementById('UpdateActionBtn');
                    if (banner && textEl) {
                        textEl.textContent = `Mise v${result?.currentVersion || ''} is currently up to date.`;
                        banner.style.display = 'flex';
                        if (actionBtn) actionBtn.style.display = 'none';
                        setTimeout(() => {
                            if (!result?.updateAvailable) banner.style.display = 'none';
                        }, 5000);
                    }
                }
            }
        }
    } catch (err) {
        if (checkBtn) checkBtn.textContent = 'Check Failed';
    } finally {
        setTimeout(() => {
            if (checkBtn) {
                checkBtn.disabled = false;
                checkBtn.textContent = 'Check for Updates';
            }
        }, 3000);
    }
}

window.checkForUpdates = triggerUpdateCheck;

let findActive = false;

async function initializeBrowser() {
    state.sessionState = await window.miseAPI.getSession();
    
    // Check crash recovery sentinel
    let isCrashRecovery = false;
    try {
        if (window.miseAPI && typeof window.miseAPI.getSessionRecoveryStatus === 'function') {
            const status = await window.miseAPI.getSessionRecoveryStatus();
            isCrashRecovery = !!(status && status.wasCrash);
        }
    } catch (e) {}

    // Restore cached titles, sleeping tab states, favicons, and dark reader flags
    if (state.sessionState.tab_titles) {
        state.activeTitlesCache = { ...state.sessionState.tab_titles };
    }
    if (state.sessionState.tab_favicons) {
        state.tabFavicons = { ...state.sessionState.tab_favicons };
    }
    if (state.sessionState.tab_dark_reader) {
        state.tabDarkReaderStates = { ...state.sessionState.tab_dark_reader };
    }
    if (state.sessionState.tab_pinned) {
        state.tabPinned = { ...state.sessionState.tab_pinned };
    }

    if (state.sessionState.tab_sleep_states && !isCrashRecovery) {
        state.tabSleepStates = { ...state.sessionState.tab_sleep_states };
        // Initialise any awake background tabs as sleeping on boot to conserve memory
        Object.keys(state.sessionState.workspaces || {}).forEach(ws => {
            const urls = state.sessionState.workspaces[ws] || [];
            if (!state.tabSleepStates[ws]) state.tabSleepStates[ws] = [];
            urls.forEach((u, i) => {
                if (ws !== state.sessionState.current_workspace || i !== 0) {
                    if (!state.tabSleepStates[ws][i]) {
                        state.tabSleepStates[ws][i] = {
                            url: u,
                            title: state.activeTitlesCache[ws]?.[i] || u,
                            scrollX: 0,
                            scrollY: 0,
                            darkReader: !!state.tabDarkReaderStates?.[ws]?.[i],
                            hibernatedAt: Date.now()
                        };
                    }
                } else {
                    state.tabSleepStates[ws][i] = null;
                }
            });
        });
    } else {
        // Crash recovery or initial cold boot: force ALL background tabs to sleep to eliminate memory spikes
        Object.keys(state.sessionState.workspaces || {}).forEach(ws => {
            const urls = state.sessionState.workspaces[ws] || [];
            const previousSleep = state.sessionState.tab_sleep_states?.[ws] || [];
            if (!state.tabSleepStates[ws]) state.tabSleepStates[ws] = [];
            urls.forEach((u, i) => {
                if (ws !== state.sessionState.current_workspace || i !== 0) {
                    const prev = previousSleep[i];
                    state.tabSleepStates[ws][i] = {
                        url: prev?.url || u,
                        title: prev?.title || state.activeTitlesCache[ws]?.[i] || u,
                        scrollX: prev?.scrollX || 0,
                        scrollY: prev?.scrollY || 0,
                        sessionStorage: prev?.sessionStorage || null,
                        darkReader: prev?.darkReader || !!state.tabDarkReaderStates?.[ws]?.[i],
                        hibernatedAt: Date.now()
                    };
                } else {
                    state.tabSleepStates[ws][i] = null;
                }
            });
        });
    }

    // Load persisted theme and sidebar preferences from configuration
    let savedTheme = 'dark';
    let sidebarAutoCollapse = true;
    let initialCfg = null;
    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        try {
            const cfg = await window.miseAPI.getBrowserSettings();
            if (cfg) {
                initialCfg = cfg;
                if (cfg.theme) savedTheme = cfg.theme;
                if (typeof cfg.sidebar_auto_collapse === 'boolean') {
                    sidebarAutoCollapse = cfg.sidebar_auto_collapse;
                }
            }
        } catch (e) {}
    }

    applySidebarMode(sidebarAutoCollapse);

    const body = document.body;

    if (savedTheme === 'light') {
        body.classList.remove('dark-mode');
        body.classList.add('light-mode');
    } else {
        body.classList.remove('light-mode');
        body.classList.add('dark-mode');
    }

    applyThemeVisuals(initialCfg, savedTheme);

    currentWebviewTheme = initialCfg?.webview_theme || 'dark';
    updateWebviewThemeButtonUI(currentWebviewTheme);

    const container = document.getElementById('webview-container');
    if (container) {
        container.classList.remove('theme-light', 'theme-dark');
        container.classList.add(currentWebviewTheme === 'light' ? 'theme-light' : 'theme-dark');
    }

    if (window.miseAPI && typeof window.miseAPI.setNativeTheme === 'function') {
        window.miseAPI.setNativeTheme(currentWebviewTheme);
    }

    setupEventListeners();
    renderWorkspaceUI();
    initializeTabSleepManager();
    setupNotesListeners();
    setupAddressBarAutocomplete();
    setupBookmarkOverlayListeners();
    setupDiagnosticsListeners();
    initSearchEnginePreference();
    initDownloadShelf();
    initStatusBar();
    initClock();
    initWindowControls(initialCfg);

    if (isCrashRecovery) {
        setTimeout(() => {
            setTargetUrl('Session restored after unexpected shutdown', 4000);
        }, 400);
    }

    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        window.miseAPI.getBrowserSettings().then((cfg) => {
            if (cfg && cfg.show_status_bar === false) {
                toggleStatusBar(false);
            }
        }).catch(() => {});
    }

    if (window.miseAPI && typeof window.miseAPI.getAppVersion === 'function') {
        window.miseAPI.getAppVersion().then((info) => {
            const badge = document.getElementById('MiseVersionBadge');
            const welcomeBadge = document.getElementById('WelcomeRuntimeVersions');

            const appVer = typeof info === 'object' ? info.version : info;
            const electronVer = typeof info === 'object' ? info.electron : '';
            const chromeVer = typeof info === 'object' ? info.chrome : '';

            if (appVer) {
                let text = `Mise v${appVer}`;
                if (electronVer && chromeVer) {
                    text += ` · Electron ${electronVer} · Chromium ${chromeVer}`;
                }
                if (badge) badge.textContent = text;
                if (welcomeBadge) welcomeBadge.textContent = text;

                // Prompt What's New tab on version bump
                if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
                    window.miseAPI.getBrowserSettings().then((cfg) => {
                        const lastSeen = cfg?.last_seen_version;
                        if (!lastSeen) {
                            // First run on machine: record version silently
                            window.miseAPI.updateBrowserSettings({ last_seen_version: appVer }).catch(() => {});
                        } else if (lastSeen !== appVer) {
                            // Version update: spawn What's New tab and record version
                            window.miseAPI.updateBrowserSettings({ last_seen_version: appVer }).catch(() => {});
                            if (typeof window.miseAPI.getWhatsNewPath === 'function') {
                                const whatsNewPath = window.miseAPI.getWhatsNewPath();
                                if (whatsNewPath) spawnTabWithUrl(whatsNewPath);
                            }
                        }
                    }).catch(() => {});
                }
            }
        }).catch(() => {});
    }

    window.openWhatsNewTab = () => {
        if (window.miseAPI && typeof window.miseAPI.getWhatsNewPath === 'function') {
            const whatsNewPath = window.miseAPI.getWhatsNewPath();
            if (whatsNewPath) spawnTabWithUrl(whatsNewPath);
        }
    };

    if (window.miseAPI && typeof window.miseAPI.onUpdateAvailable === 'function') {
        window.miseAPI.onUpdateAvailable((info) => {
            displayUpdateNotification(info);
        });
    }

    document.getElementById('WhatsNewOpenBtn')?.addEventListener('click', () => {
        if (typeof window.openWhatsNewTab === 'function') {
            window.openWhatsNewTab();
            if (typeof window.togglePreferencesView === 'function') {
                window.togglePreferencesView();
            }
        }
    });

    document.getElementById('CheckForUpdatesBtn')?.addEventListener('click', () => {
        triggerUpdateCheck(true);
    });

    if (window.miseAPI && typeof window.miseAPI.signalRendererReady === 'function') {
        window.miseAPI.signalRendererReady();
    }
}

function setupEventListeners() {
    document.getElementById('reader-view-btn')?.addEventListener('click', toggleReaderViewOnActiveTab);
    document.getElementById('dark-reader-btn')?.addEventListener('click', toggleDarkReaderOnActiveTab);
    document.getElementById('theme-toggle-btn').addEventListener('click', toggleWebviewTheme);
    document.getElementById('toggle-nav-btn').addEventListener('click', displayAddressOverlay);
    document.getElementById('split-toggle-btn')?.addEventListener('click', () => toggleSplitView());
    document.getElementById('split-toggle-btn')?.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        cycleSplitOrientation();
    });
    document.getElementById('back-btn').addEventListener('click', navigateFrameBack);
    document.getElementById('forward-btn').addEventListener('click', navigateFrameForward);
    document.getElementById('menu-btn').addEventListener('click', toggleDashboardView);

    document.getElementById('WelcomeNewTabBtn')?.addEventListener('click', spawnNewBlankTab);
    document.getElementById('WelcomeBookmarksBtn')?.addEventListener('click', toggleBookmarksOverlay);
    document.getElementById('WelcomePaletteBtn')?.addEventListener('click', toggleCommandPaletteView);
    document.getElementById('WelcomeNotesBtn')?.addEventListener('click', toggleNotesOverlay);

    if (window.miseAPI && typeof window.miseAPI.onUserContentUpdated === 'function') {
        window.miseAPI.onUserContentUpdated((kind) => {
            if (kind === 'styles' || kind === 'all') {
                reapplyActiveUserStyles();
            }
        });
    }
    
    document.getElementById('add-ws-btn').addEventListener('click', showWorkspaceInputDialog);
    document.getElementById('submit-ws-btn').addEventListener('click', processWorkspaceCreation);
    document.getElementById('cancel-ws-btn').addEventListener('click', hideWorkspaceInputDialog);
    
    document.getElementById('NewWorkspaceTitleInput').addEventListener('keydown', (e) => {
        if (e.key === 'Enter') processWorkspaceCreation();
        else if (e.key === 'Escape') hideWorkspaceInputDialog();
    });
    
    const addressBar = document.getElementById('WideAddressBar');
    addressBar.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') {
            if (state.addressSelectionIdx >= 0 && state.addressSuggestions[state.addressSelectionIdx]) {
                selectSuggestion(state.addressSuggestions[state.addressSelectionIdx]);
            } else {
                handleNavigation(addressBar.value.trim());
            }
        } else if (e.key === 'Escape') {
            addressBar.style.display = 'none';
            hideSuggestions();
        }
    });

    addressBar.addEventListener('focus', () => {
        requestAnimationFrame(() => {
            addressBar.select();
        });
    });
	
    addressBar.addEventListener('mouseup', (e) => {
        if (document.activeElement === addressBar && addressBar.selectionStart !== addressBar.selectionEnd) {
            e.preventDefault();
        }
    });

    window.miseAPI.onMasterShortcut((action, ...args) => {
        switch (action) {
            case 'spawn-tab': spawnNewBlankTab(); break;
            case 'spawn-tab-with-url': spawnTabWithUrl(args[0]); break;
            case 'open-link-in-split': openUrlInSplit(args[0]); break;
            case 'open-transient-share': openTransientShareModal(args[0]); break;
            case 'toggle-address': displayAddressOverlay(); break;
            case 'toggle-dashboard': toggleDashboardView(); break;
            case 'reload-active-tab': {
                const activeWv = getActiveWebview();
                if (activeWv) activeWv.reload();
                break;
            }
            case 'toggle-devtools': toggleActiveDevTools(); break;
            case 'toggle-bookmarks': toggleBookmarksOverlay(); break;
            case 'toggle-notes': toggleNotesOverlay(); break;
            case 'save-selection-to-notes': appendSelectionToNotes(args[0]); break;
            case 'export-reader-to-notes': exportActiveTabToNotes(); break;
            case 'export-reader-to-external': exportActiveTabToExternal(); break;
            case 'open-notes-external': openNotesInExternalApp(); break;
            case 'toggle-downloads': toggleDownloadShelf(); break;
            case 'toggle-find': toggleInPageSearch(); break;
            case 'remove-tab': handleTabRemoval(); break;
            case 'toggle-zen-mode': toggleSidebarExpansion(); break;
            case 'toggle-sidebar-collapse-mode': applySidebarMode(args[0]); break;
            case 'set-quickmark': promptQuickmark('set'); break;
            case 'jump-quickmark': promptQuickmark('jump'); break;
            case 'add-bookmark': addCurrentPageToBookmarks(); break;
            case 'toggle-global-media': toggleGlobalMediaPlayback(); break;
            case 'toggle-tab-audio-mute': toggleActiveTabAudioMute(); break;
            case 'toggle-global-audio-mute': toggleGlobalAudioMute(); break;
            case 'toggle-pin-tab': togglePinActiveTab(); break;
            case 'jump-to-tab-1': jumpToTabByIndex(1); break;
            case 'jump-to-tab-2': jumpToTabByIndex(2); break;
            case 'jump-to-tab-3': jumpToTabByIndex(3); break;
            case 'jump-to-tab-4': jumpToTabByIndex(4); break;
            case 'jump-to-tab-5': jumpToTabByIndex(5); break;
            case 'jump-to-tab-6': jumpToTabByIndex(6); break;
            case 'jump-to-tab-7': jumpToTabByIndex(7); break;
            case 'jump-to-tab-8': jumpToTabByIndex(8); break;
            case 'jump-to-tab-9': jumpToTabByIndex(9); break;
            case 'jump-to-tab': jumpToTabByIndex(args[0]); break;
            case 'toggle-split': toggleSplitView(); break;
            case 'switch-split-focus': switchSplitFocus(); break;
            case 'swap-split-panes': swapSplitPanes(); break;
            case 'close-split': closeSplitView(); break;
            case 'delete-bookmark-entry': {
                if (state.bookmarksActive) {
                    if (state.bookmarkActiveColumn === 'quickmarks' && state.filteredQuickmarksCache[state.quickmarkSelectionIdx]) {
                        deleteQuickmark(state.filteredQuickmarksCache[state.quickmarkSelectionIdx].key);
                    } else if (state.filteredBookmarksCache[state.bookmarkSelectionIdx]) {
                        deleteBookmark(state.filteredBookmarksCache[state.bookmarkSelectionIdx].url);
                    }
                }
                break;
            }
            case 'focus-sidebar': {
                focusSidebar();
                break;
            }
            case 'focus-nav-buttons': {
                const backBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
                if (backBtn) backBtn.focus();
                break;
            }
            case 'focus-webview': focusActiveWebview(); break;
            case 'trigger-hints': triggerLinkHints(); break;
            case 'toggle-palette': toggleCommandPaletteView(); break;
            case 'toggle-help': togglePreferencesView(); break;
            case 'toggle-history': toggleHistoryOverlay(); break;
            case 'toggle-status-bar': toggleStatusBar(); break;
            case 'toggle-clock': toggleClock(); break;
            case 'cycle-clock-format': cycleClockFormat(); break;
            case 'toggle-window-controls': toggleWindowControls(args[0]); break;
            case 'open-diagnostics':
            case 'toggle-diagnostics': toggleDiagnosticsView(); break;
            case 'toggle-diagnostics-recording': toggleDiagnosticsRecording(); break;
            case 'toggle-dark-reader': toggleDarkReaderOnActiveTab(); break;
            case 'toggle-reader-view': toggleReaderViewOnActiveTab(); break;
            case 'go-back':
            case 'go-back-signal': navigateFrameBack(); break;
            case 'go-forward':
            case 'go-forward-signal': navigateFrameForward(); break;
            case 'toggle-private-mode': {
                state.globalPrivateModeActive = args[0];
                handlePrivateBrowsingStateShift(state.globalPrivateModeActive);
                break;
            }
            case 'hibernate-inactive-tabs': hibernateInactiveTabs(); break;
        }
    });

    const paletteInput = document.getElementById('PaletteInput');
    paletteInput.addEventListener('input', (e) => {
        filterPaletteCommands(e.target.value);
    });

    paletteInput.addEventListener('keydown', handlePaletteInputNavigation);

    const pinSidebarBtn = document.getElementById('pin-sidebar-btn');
    if (pinSidebarBtn) {
        pinSidebarBtn.onclick = () => toggleSidebarPin();
    }

    const topNavIds = ['back-btn', 'forward-btn', 'toggle-nav-btn', 'split-toggle-btn', 'menu-btn'];
    topNavIds.forEach((id, idx) => {
        const btn = document.getElementById(id);
        if (!btn) return;
        
        btn.addEventListener('keydown', (e) => {
            if (e.key === 'ArrowRight') {
                e.preventDefault();
                const nextIdx = (idx + 1) % topNavIds.length;
                document.getElementById(topNavIds[nextIdx]).focus();
            } else if (e.key === 'ArrowLeft') {
                e.preventDefault();
                const prevIdx = (idx - 1 + topNavIds.length) % topNavIds.length;
                document.getElementById(topNavIds[prevIdx]).focus();
            } else if (e.key === 'ArrowDown' || (e.key === 'Tab' && !e.shiftKey)) {
                e.preventDefault();
                const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
                if (selectedTab) selectedTab.focus();
            } else if (e.key === 'ArrowUp' || (e.key === 'Tab' && e.shiftKey)) {
                e.preventDefault();
                const notiBtn = document.getElementById('noti-toggle-btn') || document.getElementById('pin-sidebar-btn');
                if (notiBtn) notiBtn.focus();
            } else if (e.key === 'Escape') {
                e.preventDefault();
                const sidebar = document.getElementById('Sidebar');
                if (sidebar) sidebar.classList.remove('expanded');
                focusActiveWebview();
            }
        });
    });

    const bottomButtons = ['pin-sidebar-btn', 'reader-view-btn', 'dark-reader-btn', 'theme-toggle-btn', 'noti-toggle-btn'];
    bottomButtons.forEach((id, idx) => {
        const btn = document.getElementById(id);
        if (!btn) return;

        btn.addEventListener('keydown', (e) => {
            if (state.dashboardActive || state.bookmarksActive || state.paletteActive || state.preferencesActive || state.historyActive || state.notesActive) return;
            if (e.key === 'ArrowRight' || (e.key === 'Tab' && !e.shiftKey && idx < bottomButtons.length - 1)) {
                e.preventDefault();
                const nextBtn = document.getElementById(bottomButtons[idx + 1]);
                if (nextBtn) nextBtn.focus();
            } else if (e.key === 'ArrowLeft' || (e.key === 'Tab' && e.shiftKey && idx > 0)) {
                e.preventDefault();
                const prevBtn = document.getElementById(bottomButtons[idx - 1]);
                if (prevBtn) prevBtn.focus();
            } else if (e.key === 'ArrowUp' || (e.key === 'Tab' && e.shiftKey && idx === 0)) {
                e.preventDefault();
                const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
                if (selectedTab) {
                    selectedTab.focus();
                } else {
                    const backBtn = document.getElementById('back-btn');
                    if (backBtn) backBtn.focus();
                }
            } else if (e.key === 'Tab' && !e.shiftKey && idx === bottomButtons.length - 1) {
                e.preventDefault();
                const backBtn = document.getElementById('back-btn');
                if (backBtn) backBtn.focus();
            } else if (e.key === 'Escape') {
                e.preventDefault();
                const sidebar = document.getElementById('Sidebar');
                if (sidebar) sidebar.classList.remove('expanded');
                focusActiveWebview();
            }
        });
    });

    const tabListContainer = document.getElementById('TabList');
    tabListContainer.addEventListener('keydown', (e) => {
        if (state.dashboardActive || state.bookmarksActive || state.paletteActive || state.preferencesActive || state.historyActive || state.notesActive) return;
        if (e.key === 'Escape') {
            e.preventDefault();
            const sidebar = document.getElementById('Sidebar');
            if (sidebar) sidebar.classList.remove('expanded');
            focusActiveWebview();
            return;
        }

        if (e.key === 'Tab') {
            e.preventDefault();
            if (e.shiftKey) {
                const backBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
                if (backBtn) backBtn.focus();
            } else {
                const pinBtn = document.getElementById('pin-sidebar-btn');
                if (pinBtn) pinBtn.focus();
            }
            return;
        }

        const tabItems = Array.from(document.querySelectorAll('#TabList li'));
        const activeListItem = document.querySelector('#TabList li.selected');
        const focusedListItem = document.activeElement ? document.activeElement.closest('#TabList li') : null;
        let currentIdx = tabItems.indexOf(focusedListItem || activeListItem);
        if (currentIdx === -1) currentIdx = 0;

        if (e.key === 'ArrowDown' || e.key === 'j') {
            e.preventDefault();
            if (currentIdx < tabItems.length - 1) {
                currentIdx++;
                tabItems.forEach((item, idx) => item.setAttribute('tabindex', idx === currentIdx ? '0' : '-1'));
                tabItems[currentIdx].focus();
            } else {
                const pinBtn = document.getElementById('pin-sidebar-btn');
                if (pinBtn) pinBtn.focus();
            }
        } else if (e.key === 'ArrowUp' || e.key === 'k') {
            e.preventDefault();
            if (currentIdx > 0) {
                currentIdx--;
                tabItems.forEach((item, idx) => item.setAttribute('tabindex', idx === currentIdx ? '0' : '-1'));
                tabItems[currentIdx].focus();
            } else {
                const backBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
                if (backBtn) backBtn.focus();
            }
        } else if (e.key === 'Enter' || e.key === ' ') {
            e.preventDefault();
            const targetIdx = focusedListItem ? tabItems.indexOf(focusedListItem) : currentIdx;
            if (targetIdx !== -1) {
                if (state.dashboardActive && typeof window.toggleDashboardView === 'function') {
                    window.toggleDashboardView();
                }
                switchTabFocus(targetIdx);
            }
        } else if (e.key === 'ArrowLeft' || e.key === 'h') {
            e.preventDefault();
            const backBtn = document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
            if (backBtn) backBtn.focus();
        } else if (e.key === 'ArrowRight' || e.key === 'l' || e.key === 'i') {
            e.preventDefault();
            const targetIdx = focusedListItem ? tabItems.indexOf(focusedListItem) : currentIdx;
            if (targetIdx !== -1) {
                switchTabFocus(targetIdx);
            }
            focusActiveWebview();
        } else if (!e.ctrlKey && !e.altKey && !e.metaKey && !e.shiftKey && /^[1-9]$/.test(e.key)) {
            e.preventDefault();
            const num = parseInt(e.key, 10);
            jumpToTabByIndex(num);
            const updatedItems = Array.from(document.querySelectorAll('#TabList li'));
            const targetIdx = Math.min(num - 1, updatedItems.length - 1);
            if (targetIdx >= 0 && updatedItems[targetIdx]) {
                updatedItems.forEach((item, idx) => item.setAttribute('tabindex', idx === targetIdx ? '0' : '-1'));
                updatedItems[targetIdx].focus();
            }
        } else if (e.key === 't') {
            e.preventDefault();
            spawnNewBlankTab();
        } else if (e.key === 'x') {
            e.preventDefault();
            const targetIdx = focusedListItem ? tabItems.indexOf(focusedListItem) : currentIdx;
            handleTabRemoval(targetIdx !== -1 ? targetIdx : undefined);
        } else if (e.key === 'o') {
            e.preventDefault();
            displayAddressOverlay();
        } else if (e.key === 'w') {
            e.preventDefault();
            toggleDashboardView();
        } else if (e.key === 'y') {
            e.preventDefault();
            const targetIdx = focusedListItem ? tabItems.indexOf(focusedListItem) : currentIdx;
            const currentWS = state.sessionState.current_workspace;
            const urls = state.sessionState.workspaces[currentWS] || [];
            const activeUrl = urls[targetIdx] || '';
            if (activeUrl) {
                navigator.clipboard.writeText(activeUrl).catch(() => {});
                setTargetUrl(`Yanked URL to clipboard: ${activeUrl}`, 2500);
            }
        }
    });

    window.addEventListener('keydown', (e) => {
        if (e.key === 'Control' && !document.body.classList.contains('show-tab-badges')) {
            document.body.classList.add('show-tab-badges');
        }
        if (e.shiftKey && e.key === 'Escape') {
            e.preventDefault();
            togglePassthroughMode();
        }
    });

    window.addEventListener('keyup', (e) => {
        if (e.key === 'Control') {
            document.body.classList.remove('show-tab-badges');
        }
    });

    window.addEventListener('blur', () => {
        document.body.classList.remove('show-tab-badges');
    });

    const findInput = document.getElementById('FindInput');
    if (findInput) {
        findInput.addEventListener('input', (e) => {
            const text = e.target.value;
            const activeWv = getActiveWebview();
            if (!activeWv) return;

            if (text.length > 0) {
                activeWv.findInPage(text);
            } else {
                activeWv.stopFindInPage('clearSelection');
                document.getElementById('FindMatchCount').textContent = '';
            }
        });

        findInput.addEventListener('keydown', (e) => {
            const activeWv = getActiveWebview();
            const isCtrl = e.control || e.metaKey;
            const key = e.key.toLowerCase();

            if (e.key === 'Escape' || (isCtrl && key === 's')) {
                e.preventDefault();
                toggleInPageSearch();
            } else if (e.key === 'Enter') {
                e.preventDefault();
                if (!activeWv) return;
                activeWv.findInPage(findInput.value, { forward: !e.shiftKey, findNext: true });
            }
        });
    }

    const overlay = document.getElementById('DashboardOverlay');
    overlay.addEventListener('keydown', (e) => {
        if (!state.dashboardActive) return;
        if (e.key === 'Escape') {
            e.preventDefault();
            toggleDashboardView();
            return;
        }

        if (state.dashboardItems.length === 0) return;
        if (document.activeElement === document.getElementById('NewWorkspaceTitleInput') || document.activeElement.classList.contains('dashboard-rename-input')) return;

        if (e.key === 'ArrowDown') {
            e.preventDefault();
            state.dashboardSelectionIdx = (state.dashboardSelectionIdx + 1) % state.dashboardItems.length;
            updateDashboardVisualSelection();
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            state.dashboardSelectionIdx = (state.dashboardSelectionIdx - 1 + state.dashboardItems.length) % state.dashboardItems.length;
            updateDashboardVisualSelection();
        } else if (e.key === 'Enter') {
            e.preventDefault();
            const targetItem = state.dashboardItems[state.dashboardSelectionIdx];
            if (targetItem) executeDashboardItemActivation(targetItem.payload);
        } else if (e.key === 'Delete') {
            e.preventDefault();
            handleTabRemoval();
        }
    });

    document.getElementById('Sidebar').addEventListener('focusout', (e) => {
        if (e.relatedTarget && !document.getElementById('Sidebar').contains(e.relatedTarget)) {
            if (state.dashboardActive || state.paletteActive || state.preferencesActive || state.bookmarksActive || state.historyActive || state.notesActive || state.diagnosticsActive) return;
            
            const overlayIds = [
                'PaletteInput', 'PaletteList', 'PreferencesOverlay',
                'BookmarksOverlay', 'BookmarkSearchInput', 'HistoryOverlay',
                'HistorySearchInput', 'NotesOverlay', 'NotesTextArea',
                'DashboardOverlay', 'ShareModalOverlay', 'FindBarOverlay',
                'DiagnosticsOverlay'
            ];
            for (const id of overlayIds) {
                const el = document.getElementById(id);
                if (el && (e.relatedTarget === el || el.contains(e.relatedTarget))) return;
            }

            if (window.miseAllowWebviewFocus) {
                window.miseAllowWebviewFocus = false;
                return;
            }
            e.preventDefault();
            const selectedTab = document.querySelector('#TabList li.selected');
            if (selectedTab) selectedTab.focus();
        }
    });

    const historySearchInput = document.getElementById('HistorySearchInput');
    historySearchInput.addEventListener('input', (e) => {
        filterHistoryItems(e.target.value.trim());
    });

    document.getElementById('CloseHistoryBtn').addEventListener('click', toggleHistoryOverlay);

    document.getElementById('PurgeHistoryBtn').addEventListener('click', async () => {
        const confirmPurge = confirm("Are you sure you want to permanently delete all history?");
        if (confirmPurge) {
            const success = await window.miseAPI.purgeHistory();
            if (success) {
                filterHistoryItems('');
            }
        }
    });

    document.getElementById('HistoryOverlay').addEventListener('click', (e) => {
        if (e.target.id === 'HistoryOverlay') {
            toggleHistoryOverlay();
        }
    });

    loadBookmarksAndQuickmarks();
}

function handlePrivateBrowsingStateShift(isPrivate) {
    const sidebar = document.getElementById('Sidebar');
    if (isPrivate) {
        sidebar.style.borderRight = '2px solid #ff5555';
    } else {
        sidebar.style.borderRight = '1px solid var(--border)';
    }
    
    const currentWS = state.sessionState.current_workspace;
    if (state.activeViewsCache[currentWS]) {
        state.activeViewsCache[currentWS].forEach(wv => { if (wv) wv.remove(); });
        state.activeViewsCache[currentWS] = [];
    }
    
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : 0;
    renderWorkspaceUI(currentIdx);
}

async function executeSurgicalCookieWipe() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return;

    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    const urlStr = state.sessionState.workspaces[currentWS][currentIdx] || "";
    
    const isTargetPrivate = state.globalPrivateModeActive || isTargetPrivateDomain(urlStr);
    await window.miseAPI.clearDomainCookies({ urlStr, isPrivate: isTargetPrivate, workspace: currentWS });
    
    window.miseAllowWebviewFocus = true;
    focusActiveWebview();
}

async function executeGlobalCacheWipe() {
    const currentWS = state.sessionState.current_workspace;
    await window.miseAPI.clearActiveCache({ isPrivate: state.globalPrivateModeActive, workspace: currentWS });
    
    window.miseAllowWebviewFocus = true;
    focusActiveWebview();
}

function triggerLinkHints() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (activeListItem) {
        const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
        const activeWv = state.activeViewsCache[currentWS]?.[currentIdx];
        if (activeWv && typeof activeWv.executeJavaScript === 'function') {
            activeWv.executeJavaScript("if (typeof window.toggleHints === 'function') { window.toggleHints(); }").catch(() => {});
        }
    }
}
  
let currentWebviewTheme = 'dark';

function updateWebviewThemeButtonUI(theme) {
    const button = document.getElementById('theme-toggle-btn');
    if (!button) return;
    if (theme === 'light') {
        button.innerHTML = '<i class="fa-solid fa-sun"></i>';
        button.title = 'Toggle Website Dark/Light Theme (Website Light active)';
    } else {
        button.innerHTML = '<i class="fa-solid fa-moon"></i>';
        button.title = 'Toggle Website Dark/Light Theme (Website Dark active)';
    }
}

let webviewThemeSaveTimer = null;

function toggleWebviewTheme() {
    currentWebviewTheme = (currentWebviewTheme === 'light') ? 'dark' : 'light';
    updateWebviewThemeButtonUI(currentWebviewTheme);

    const container = document.getElementById('webview-container');
    if (container) {
        container.classList.remove('theme-light', 'theme-dark');
        container.classList.add(currentWebviewTheme === 'light' ? 'theme-light' : 'theme-dark');
    }

    const activeView = getActiveWebview();
    if (activeView) {
        activeView.style.opacity = '0.75';
        setTimeout(() => {
            activeView.style.opacity = '1';
        }, 140);
    }

    if (window.miseAPI && typeof window.miseAPI.setNativeTheme === 'function') {
        window.miseAPI.setNativeTheme(currentWebviewTheme);
    }

    if (webviewThemeSaveTimer) clearTimeout(webviewThemeSaveTimer);
    webviewThemeSaveTimer = setTimeout(() => {
        if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
            window.miseAPI.getBrowserSettings().then((cfg) => {
                const currentCfg = cfg || {};
                currentCfg.webview_theme = currentWebviewTheme;
                return window.miseAPI.updateBrowserSettings(currentCfg);
            }).catch(() => {});
        }
    }, 400);
}

function toggleInterfaceTheme() {
    const body = document.body;
    const isDark = body.classList.contains('dark-mode');
    const newTheme = isDark ? 'light' : 'dark';
    
    // Instantly swap UI classes on body
    if (newTheme === 'light') {
        body.classList.remove('dark-mode');
        body.classList.add('light-mode');
    } else {
        body.classList.remove('light-mode');
        body.classList.add('dark-mode');
    }
    
    // Synchronously apply visual theme variables and sync inputs with zero latency
    applyThemeVisuals(null, newTheme);
    syncVisualSettingsInputs(null);

    // Keep Preferences checkbox switch in sync if open
    const themeToggle = document.getElementById('setting-theme-toggle');
    if (themeToggle) {
        themeToggle.checked = (newTheme === 'dark');
    }
    
    // Persist setting to disk asynchronously in the background without blocking the UI
    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        window.miseAPI.getBrowserSettings().then((cfg) => {
            const currentCfg = cfg || {};
            currentCfg.theme = newTheme;
            return window.miseAPI.updateBrowserSettings(currentCfg);
        }).catch(() => {});
    }
}

document.getElementById('noti-toggle-btn').addEventListener('click', async () => {
    const btn = document.getElementById('noti-toggle-btn');
    const isMuted = !!btn.querySelector('.fa-bell-slash');
    const targetState = isMuted; // If muted, enable notifications; otherwise, mute

    if (window.miseAPI && typeof window.miseAPI.toggleGlobalNotifications === 'function') {
        await window.miseAPI.toggleGlobalNotifications(targetState);
    }

    if (targetState) {
        btn.innerHTML = '<i class="fa-solid fa-bell"></i>';
    } else {
        btn.innerHTML = '<i class="fa-solid fa-bell-slash"></i>';
    }

    window.miseAllowWebviewFocus = true;
    focusActiveWebview();
});

document.addEventListener('DOMContentLoaded', initializeBrowser);

function toggleInPageSearch() {
    const overlay = document.getElementById('FindBarOverlay');
    const input = document.getElementById('FindInput');
    const count = document.getElementById('FindMatchCount');
    
    findActive = !findActive;
    
    if (findActive) {
        overlay.style.display = 'flex';
        input.value = '';
        count.textContent = '';
        setTimeout(() => {
            input.focus();
            input.select();
        }, 20);
    } else {
        overlay.style.display = 'none';
        const activeWv = getActiveWebview();
        if (activeWv) {
            activeWv.stopFindInPage('clearSelection');
        }
        focusActiveWebview();
    }
}

function toggleActiveDevTools() {
    const activeWv = getActiveWebview();
    if (!activeWv) return;

    if (activeWv.isDevToolsOpened()) {
        activeWv.closeDevTools();
    } else {
        activeWv.openDevTools();
    }
}

function toggleZenMode() {
    document.body.classList.toggle('zen-mode');
}

export function applySidebarMode(autoCollapse) {
    const body = document.body;
    const pinBtn = document.getElementById('pin-sidebar-btn');
    if (autoCollapse) {
        body.classList.remove('sidebar-pinned');
        body.classList.add('sidebar-auto-collapse');
        if (pinBtn) pinBtn.classList.remove('pin-active');
    } else {
        body.classList.remove('sidebar-auto-collapse');
        body.classList.add('sidebar-pinned');
        if (pinBtn) pinBtn.classList.add('pin-active');
        const sidebar = document.getElementById('Sidebar');
        if (sidebar) sidebar.classList.remove('expanded');
    }
}

export function toggleSidebarExpansion() {
    const sidebar = document.getElementById('Sidebar');
    if (!sidebar) return;
    sidebar.classList.toggle('expanded');
    if (sidebar.classList.contains('expanded')) {
        const selectedTab = document.querySelector('#TabList li.selected');
        if (selectedTab) selectedTab.focus();
    } else {
        focusActiveWebview();
    }
}

export async function toggleSidebarPin() {
    const isCurrentlyPinned = document.body.classList.contains('sidebar-pinned');
    const newAutoCollapse = isCurrentlyPinned;
    applySidebarMode(newAutoCollapse);
    if (window.miseAPI && typeof window.miseAPI.getBrowserSettings === 'function') {
        try {
            const cfg = (await window.miseAPI.getBrowserSettings()) || {};
            cfg.sidebar_auto_collapse = newAutoCollapse;
            await window.miseAPI.updateBrowserSettings(cfg);
        } catch (e) {}
    }
}
