import { recordSidebarPointerEntry } from './modules/sidebarPointer.js';
import { openWorkspaceMoveDialog } from './modules/tabActions.js';
import { initialiseOverlayFocus, initialiseSidebarPointerFocus, prepareOverlay, hasActiveOverlay } from './modules/focus.js';
import { focusSettingsSearch } from './modules/settingsNav.js';
import { state } from './modules/state.js';
import { 
    isDarkMode, 
    applyThemeToOverlayElement, 
    escapeHtml, 
    isTargetScript, 
    getActiveWebview, 
    focusActiveWebview,
    removeWebview,
    isTargetPrivateDomain 
} from './modules/utils.js';
import { 
    applyThemeVisuals, 
    syncVisualSettingsInputs 
} from './modules/theme.js';

import { 
    renderWorkspaceUI, 
    switchTabFocus, 
    moveTabState,
    reopenClosedTab,
    clearClosedTabs,
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
    commandActions,
    toggleCommandPaletteView, 
    openCommandPalette,
    closeCommandPalette,
    togglePreferencesView,
    openPreferencesPane,
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
    displayAddressOverlay, 
    handleNavigation, 
    setupAddressBarAutocomplete, 
    renderSuggestions, 
    updateSuggestionHighlight, 
    selectSuggestion, 
    hideSuggestions,
    dismissAddressBar
} from './modules/navigation.js';

import { openTransientShareModal, closeTransientShareModal } from './modules/overlays/shareModal.js';
import { initSearchEnginePreference, renderSearchAliasesUI } from './modules/overlays/searchEngine.js';
import { initDownloadShelf, toggleDownloadShelf } from './modules/downloads.js';
import {
    setExtensionsConfig,
    handleExtensionSelectTab,
    handleExtensionCloseTab,
    refreshStatusBarActions,
    closeExtensionPopup
} from './modules/extensions.js';
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
window.toggleReaderViewOnActiveTab = toggleReaderViewOnActiveTab;
window.toggleDashboardView = toggleDashboardView;
window.displayAddressOverlay = displayAddressOverlay;
window.triggerLinkHints = triggerLinkHints;
window.toggleInPageSearch = toggleInPageSearch;
window.renderSearchAliasesUI = renderSearchAliasesUI;
window.toggleDownloadShelf = toggleDownloadShelf;
window.toggleStatusBar = toggleStatusBar;
window.toggleClock = toggleClock;
window.toggleWindowControls = toggleWindowControls;
window.cycleClockFormat = cycleClockFormat;
window.togglePassthroughMode = togglePassthroughMode;
window.recalculateStatusMode = recalculateMode;
window.updateStatusBarFromActiveView = updateStatusBarFromActiveView;
window.toggleSidebarExpansion = toggleSidebarPin;
window.toggleSidebarPin = toggleSidebarPin;
window.applySidebarMode = applySidebarMode;
window.toggleInterfaceTheme = toggleInterfaceTheme;
window.applyThemeVisuals = applyThemeVisuals;
window.togglePreferencesView = togglePreferencesView;
window.openPreferencesPane = openPreferencesPane;
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
        if (window.flyAPI && typeof window.flyAPI.clearGoogleAuthCookies === 'function') {
            await window.flyAPI.clearGoogleAuthCookies('FlyPrivateProfile');
        }
        setTargetUrl('Opening clean Google sign-in test in volatile partition...', 3500);
        spawnTabWithUrl('https://accounts.google.com/ServiceLogin?fly_test_auth=1');
    } catch (err) {
        console.error('Failed to launch clean Google sign-in test:', err);
    }
}
window.testCleanGoogleSignIn = testCleanGoogleSignIn;

export function focusSidebar() {
    const activeEl = document.activeElement;
    const sidebar = document.getElementById('Sidebar');
    if (sidebar?.contains(activeEl)) {
        sidebar.classList.remove('expanded', 'pointer-inside');
        activeEl.blur();
        focusActiveWebview();
        return;
    }
    const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
    const target = selectedTab || document.getElementById('back-btn') || document.getElementById('toggle-nav-btn');
    target?.focus();
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
            const openRelease = () => {
                if (window.flyAPI && typeof window.flyAPI.openExternal === 'function') {
                    window.flyAPI.openExternal(updateInfo.releaseUrl);
                }
            };
            actionBtn.style.display = 'inline-block';
            actionBtn.disabled = false;
            actionBtn.textContent = 'Download release';
            actionBtn.onclick = openRelease;
            // An AppImage replaces itself and restarts; everything else links to the release.
            if (updateInfo.canInstall && typeof window.flyAPI?.installUpdate === 'function') {
                actionBtn.textContent = 'Update and restart';
                actionBtn.onclick = async () => {
                    actionBtn.disabled = true;
                    textEl.textContent = `Downloading Fly ${updateInfo.latestVersion}…`;
                    const stopProgress = window.flyAPI.onUpdateProgress?.(percent => {
                        textEl.textContent = `Downloading Fly ${updateInfo.latestVersion}… ${percent}%`;
                    });
                    const result = await window.flyAPI.installUpdate().catch(err => ({ ok: false, error: err.message }));
                    stopProgress?.();
                    if (result && result.ok) {
                        textEl.textContent = `Restarting into Fly ${updateInfo.latestVersion}…`;
                        return;
                    }
                    textEl.textContent = `The update could not be installed${result?.error ? ` (${result.error})` : ''}. Download it from the release page instead.`;
                    actionBtn.disabled = false;
                    actionBtn.textContent = 'Download release';
                    actionBtn.onclick = openRelease;
                };
            }
        }
    }

    // Sidebar update icon indicator
    const sidebarIndicator = document.getElementById('update-indicator-btn');
    if (sidebarIndicator) {
        sidebarIndicator.style.display = 'inline-flex';
        sidebarIndicator.title = `Fly ${updateInfo.latestVersion} available. Click to review release.`;
        sidebarIndicator.onclick = () => openPreferencesPane('about');
    }
}

export async function triggerUpdateCheck(manual = false) {
    const checkBtn = document.getElementById('CheckForUpdatesBtn');
    if (checkBtn) {
        checkBtn.disabled = true;
        checkBtn.textContent = 'Checking...';
    }

    try {
        if (window.flyAPI && typeof window.flyAPI.checkForUpdates === 'function') {
            const result = await window.flyAPI.checkForUpdates(manual);
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
                        textEl.textContent = `Fly v${result?.currentVersion || ''} is currently up to date.`;
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
    state.sessionState = await window.flyAPI.getSession();
    
    // Check crash recovery sentinel
    let isCrashRecovery = false;
    try {
        if (window.flyAPI && typeof window.flyAPI.getSessionRecoveryStatus === 'function') {
            const status = await window.flyAPI.getSessionRecoveryStatus();
            isCrashRecovery = !!(status && status.wasCrash);
        }
    } catch (e) {}

    // Restore cached titles, sleeping tab states, and favicons
    if (state.sessionState.tab_titles) {
        state.activeTitlesCache = { ...state.sessionState.tab_titles };
    }
    if (state.sessionState.tab_favicons) {
        state.tabFavicons = { ...state.sessionState.tab_favicons };
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
    if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
        try {
            const cfg = await window.flyAPI.getBrowserSettings();
            if (cfg) {
                initialCfg = cfg;
                setExtensionsConfig(cfg);
                if (cfg.theme) savedTheme = cfg.theme;
                if (typeof cfg.sidebar_auto_collapse === 'boolean') {
                    sidebarAutoCollapse = cfg.sidebar_auto_collapse;
                }
            }
        } catch (e) {}
    }

    const interfaceScale = document.getElementById('setting-interface-scale');
    let savedInterfaceScale = initialCfg?.interface_scale === 'large' ? 'large' : 'normal';
    interfaceScale.value = savedInterfaceScale;
    interfaceScale.addEventListener('change', async () => {
        const status = document.getElementById('interface-scale-status');
        const value = interfaceScale.value;
        interfaceScale.disabled = true;
        status.textContent = '';
        try {
            const saved = await window.flyAPI.updateBrowserSettings({ interface_scale: value });
            if (!saved) throw new Error('Could not save interface scale');
            savedInterfaceScale = value;
            status.textContent = 'Saved';
        } catch {
            interfaceScale.value = savedInterfaceScale;
            status.textContent = 'Could not save interface size. Please try again.';
        } finally {
            interfaceScale.disabled = false;
            interfaceScale.focus({ preventScroll: true });
        }
    });

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

    syncWebsiteTheme = initialCfg?.sync_website_theme !== false;
    applyWebsiteTheme(syncWebsiteTheme ? savedTheme : 'light');

    setupEventListeners();
    renderWorkspaceUI();
    initializeTabSleepManager();
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

    if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
        window.flyAPI.getBrowserSettings().then((cfg) => {
            if (cfg && cfg.show_status_bar === false) {
                toggleStatusBar(false);
            }
        }).catch(() => {});
    }

    if (window.flyAPI && typeof window.flyAPI.getAppVersion === 'function') {
        window.flyAPI.getAppVersion().then((info) => {
            const badge = document.getElementById('FlyVersionBadge');
            const welcomeBadge = document.getElementById('WelcomeRuntimeVersions');

            const appVer = typeof info === 'object' ? info.version : info;
            const electronVer = typeof info === 'object' ? info.electron : '';
            const chromeVer = typeof info === 'object' ? info.chrome : '';

            if (appVer) {
                let text = `Fly v${appVer}`;
                if (electronVer && chromeVer) {
                    text += ` · Electron ${electronVer} · Chromium ${chromeVer}`;
                }
                if (badge) badge.textContent = text;
                if (welcomeBadge) welcomeBadge.textContent = text;

                // Prompt What's New tab on version bump
                if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
                    window.flyAPI.getBrowserSettings().then((cfg) => {
                        const lastSeen = cfg?.last_seen_version;
                        if (!lastSeen) {
                            // First run on machine: record version silently
                            window.flyAPI.updateBrowserSettings({ last_seen_version: appVer }).catch(() => {});
                        } else if (lastSeen !== appVer) {
                            // Version update: spawn What's New tab and record version
                            window.flyAPI.updateBrowserSettings({ last_seen_version: appVer }).catch(() => {});
                            if (typeof window.flyAPI.getWhatsNewPath === 'function') {
                                const whatsNewPath = window.flyAPI.getWhatsNewPath();
                                // The page lists every release after the one this profile last ran.
                                if (whatsNewPath) spawnTabWithUrl(`${whatsNewPath}?from=${encodeURIComponent(lastSeen)}`);
                            }
                        }
                    }).catch(() => {});
                }
            }
        }).catch(() => {});
    }

    window.openWhatsNewTab = () => {
        if (window.flyAPI && typeof window.flyAPI.getWhatsNewPath === 'function') {
            const whatsNewPath = window.flyAPI.getWhatsNewPath();
            if (whatsNewPath) spawnTabWithUrl(whatsNewPath);
        }
    };

    if (window.flyAPI && typeof window.flyAPI.onUpdateAvailable === 'function') {
        window.flyAPI.onUpdateAvailable((info) => {
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

    if (window.flyAPI && typeof window.flyAPI.signalRendererReady === 'function') {
        window.flyAPI.signalRendererReady();
    }
}

function setupEventListeners() {
    const sidebar = document.getElementById('Sidebar');
    initialiseSidebarPointerFocus(sidebar, focusActiveWebview);
    // Retain hover on window exit; host or guest entry clears it over content.
    document.addEventListener('mouseover', (event) => {
        const inside = sidebar.contains(event.target);
        // Internal pointer transitions must not undo keyboard contraction.
        if (inside && sidebar.contains(event.relatedTarget)) return;
        if (inside) recordSidebarPointerEntry(event);
        sidebar.classList.toggle('pointer-inside', inside);
    });

    document.getElementById('reader-view-btn')?.addEventListener('click', toggleReaderViewOnActiveTab);
    document.getElementById('toggle-nav-btn').addEventListener('click', displayAddressOverlay);
    document.getElementById('split-toggle-btn')?.addEventListener('click', () => toggleSplitView());
    document.getElementById('split-toggle-btn')?.addEventListener('contextmenu', (e) => {
        e.preventDefault();
        cycleSplitOrientation();
    });
    for (const [id, navigate] of [['back-btn', navigateFrameBack], ['forward-btn', navigateFrameForward]]) {
        document.getElementById(id).addEventListener('click', navigate);
    }

    document.getElementById('WelcomeNewTabBtn')?.addEventListener('click', spawnNewBlankTab);
    document.getElementById('WelcomeBookmarksBtn')?.addEventListener('click', toggleBookmarksOverlay);
    document.getElementById('WelcomePaletteBtn')?.addEventListener('click', toggleCommandPaletteView);
    document.getElementById('WelcomeDashboardBtn')?.addEventListener('click', toggleDashboardView);

    if (window.flyAPI && typeof window.flyAPI.onUserContentUpdated === 'function') {
        window.flyAPI.onUserContentUpdated((kind) => {
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
        else if (e.key === 'Escape') { e.preventDefault(); e.stopPropagation(); hideWorkspaceInputDialog(); }
    });
    
    document.getElementById('WorkspaceInputBox').addEventListener('keydown', event => {
        if (event.key === 'Escape') { event.preventDefault(); event.stopPropagation(); hideWorkspaceInputDialog(); }
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
            e.preventDefault();
            addressBar.style.display = 'none';
            hideSuggestions();
            focusActiveWebview();
        }
    });

    // A press outside the bar discards it like Escape. A press in a page reaches only the
    // guest, so focus leaving browser chrome for a webview stands in for that press: the bar
    // never stays open, unreachable by Escape, while a page holds the keyboard. The webview
    // becomes active only after the focusout; leaving the window keeps the active element.
    // The navigation button toggles the bar on click, so it closes the bar itself.
    document.addEventListener('mousedown', event => {
        if (addressBar.style.display === 'block' && !event.target.closest('#AddressBarContainer, #toggle-nav-btn')) dismissAddressBar();
    }, true);
    document.addEventListener('focusout', () => setTimeout(() => {
        if (addressBar.style.display === 'block' && document.activeElement?.tagName === 'WEBVIEW') dismissAddressBar();
    }));

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

    window.executeBrowserAction = (action, ...args) => {
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
            case 'toggle-downloads': toggleDownloadShelf(); break;
            // Inside Preferences, find searches the settings rather than the page behind them.
            case 'toggle-find': if (state.preferencesActive) focusSettingsSearch(); else toggleInPageSearch(); break;
            case 'remove-tab': handleTabRemoval(); break;
            case 'toggle-zen-mode':
            case 'toggle-sidebar-pin': toggleSidebarPin(); break;
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
            case 'open-keyboard-settings':
                (async () => {
                    await openPreferencesPane('keyboard');
                    document.getElementById('KeyboardSearch').focus({preventScroll: true});
                })(); break;
            case 'toggle-history': toggleHistoryOverlay(); break;
            case 'toggle-status-bar': toggleStatusBar(); break;
            case 'toggle-clock': toggleClock(); break;
            case 'cycle-clock-format': cycleClockFormat(); break;
            case 'toggle-window-controls': toggleWindowControls(args[0]); break;
            case 'open-diagnostics':
            case 'toggle-diagnostics': toggleDiagnosticsView(); break;
            case 'toggle-diagnostics-recording': toggleDiagnosticsRecording(); break;
            case 'toggle-reader-view': toggleReaderViewOnActiveTab(); break;
            case 'go-back':
            case 'go-back-signal': navigateFrameBack(); break;
            case 'go-forward':
            case 'go-forward-signal': navigateFrameForward(); break;
            case 'toggle-private-mode': {
                clearClosedTabs();
                state.globalPrivateModeActive = args[0];
                handlePrivateBrowsingStateShift(state.globalPrivateModeActive);
                break;
            }
            case 'hibernate-inactive-tabs': hibernateInactiveTabs(); break;
            case 'move-tab-to-workspace': openWorkspaceMoveDialog(); break;
            case 'reopen-closed-tab': reopenClosedTab(); break;
            case 'move-tab-left': case 'move-tab-right': {
                const items = [...document.querySelectorAll('#TabList li')];
                const index = items.findIndex(item => item.classList.contains('selected'));
                const target = index + (action === 'move-tab-left' ? -1 : 1);
                const workspace = state.sessionState.current_workspace;
                if (target >= 0 && target < items.length && !!state.tabPinned[workspace]?.[index] === !!state.tabPinned[workspace]?.[target]) {
                    moveTabState(workspace, index, target);
                    renderWorkspaceUI(target);
                    window.flyAPI.saveSession(state.sessionState);
                }
                break;
            }
            case 'print-page': getActiveWebview()?.print(); break;
            case 'toggle-notifications': document.getElementById('noti-toggle-btn').click(); break;
            case 'force-reload-tab': getActiveWebview()?.reloadIgnoringCache(); break;
            case 'stop-loading': getActiveWebview()?.stop(); break;
            case 'zoom-in':
            case 'zoom-out': {
                const view = getActiveWebview();
                if (view) view.setZoomLevel(Math.max(-5, Math.min(5, view.getZoomLevel() + (action === 'zoom-in' ? 0.5 : -0.5))));
                break;
            }
            case 'yank-url': {
                const view = getActiveWebview();
                if (view) {
                    let url = view.getURL();
                    try {
                        const parsed = new URL(url);
                        if (parsed.protocol === 'file:' && parsed.pathname.endsWith('/assets/pdfjs/viewer.html')) url = parsed.searchParams.get('file') || url;
                    } catch (_) {}
                    navigator.clipboard.writeText(url).then(() => setTargetUrl('Page URL copied', 2000)).catch(() => setTargetUrl('Could not copy the page URL', 2000));
                }
                break;
            }
            case 'next-tab':
            case 'previous-tab': {
                const items = [...document.querySelectorAll('#TabList li')];
                if (items.length) {
                    const index = items.findIndex(item => item.classList.contains('selected'));
                    switchTabFocus((index + (action === 'next-tab' ? 1 : -1) + items.length) % items.length);
                }
                break;
            }
            case 'duplicate-tab': {
                const view = getActiveWebview();
                if (view) spawnTabWithUrl(view.getURL());
                break;
            }
            case 'next-workspace':
            case 'previous-workspace': {
                const names = Object.keys(state.sessionState.workspaces);
                const index = names.indexOf(state.sessionState.current_workspace);
                if (names.length) {
                    state.sessionState.current_workspace = names[(index + (action === 'next-workspace' ? 1 : -1) + names.length) % names.length];
                    renderWorkspaceUI(0);
                    window.flyAPI.saveSession(state.sessionState);
                }
                break;
            }
            case 'new-workspace':
                if (!state.dashboardActive) toggleDashboardView();
                showWorkspaceInputDialog(); break;
            case 'toggle-passthrough': togglePassthroughMode(); break;
            case 'scroll-down': case 'scroll-up': case 'scroll-left': case 'scroll-right':
            case 'half-page-down': case 'half-page-up': case 'page-top': case 'page-bottom': case 'enter-insert':
                if (action === 'enter-insert') focusActiveWebview();
                getActiveWebview()?.send('keyboard-local-action', action); break;
            default: {
                if (/^tab-[1-9]$/.test(action)) {
                    const count = document.querySelectorAll('#TabList li').length;
                    const index = Number(action.slice(4));
                    if (count) switchTabFocus(index === 9 ? count - 1 : Math.min(index - 1, count - 1));
                } else {
                    commandActions[action]?.();
                }
                break;
            }
            case 'open-extension-actions': openCommandPalette('Extensions'); break;
            case 'extension-select-tab': handleExtensionSelectTab(args[0]); break;
            case 'extension-close-tab': handleExtensionCloseTab(args[0]); break;
            case 'extensions-actions-updated': refreshStatusBarActions(); break;
            case 'close-extension-popup': closeExtensionPopup(); break;
        }
    };
    window.flyAPI.onMasterShortcut(window.executeBrowserAction);
    initialiseOverlayFocus({
        WideAddressBar: {active: () => addressBar.style.display === 'block', close: displayAddressOverlay},
        FindBarOverlay: {active: () => findActive, close: toggleInPageSearch},
        DashboardOverlay: {active: () => state.dashboardActive, close: toggleDashboardView},
        PreferencesOverlay: {active: () => state.preferencesActive, close: togglePreferencesView},
        CommandPaletteOverlay: {active: () => state.paletteActive, close: toggleCommandPaletteView},
        BookmarksOverlay: {active: () => state.bookmarksActive, close: toggleBookmarksOverlay},
        HistoryOverlay: {active: () => state.historyActive, close: toggleHistoryOverlay},
        DiagnosticsOverlay: {active: () => state.diagnosticsActive, close: toggleDiagnosticsView},
        ShareModalOverlay: {active: () => document.getElementById('ShareModalOverlay').style.display === 'flex', close: closeTransientShareModal}
    });
    for (const id of ['WorkspaceHeader', 'SidebarClock', 'WelcomeNewTabBtn', 'WelcomeBookmarksBtn', 'WelcomePaletteBtn', 'WelcomeDashboardBtn']) {
        const control = document.getElementById(id);
        if (!control) continue;
        control.tabIndex = 0;
        control.setAttribute('role', 'button');
        control.addEventListener('keydown', event => {
            if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); control.click(); }
        });
    }

    let browserKeys = new Map(), pageKeys = new Map(), pagePrefixes = new Set();
    let keyboardActions = new Set();
    const nativeKeyboardActions = new Set(['toggle-fullscreen', 'toggle-native-menu-visibility', 'toggle-menu-bar', 'toggle-private-mode']);
    window.dispatchKeyboardAction = action => {
        if (!keyboardActions.has(action)) return;
        if (nativeKeyboardActions.has(action)) window.flyAPI.executeKeyboardAction(action);
        else window.executeBrowserAction(action);
    };
    let pageStroke = '', pageStrokeTime = 0;
    window.addEventListener('blur', () => { pageStroke = ''; });
    window.addEventListener('focusin', () => { pageStroke = ''; });
    const applyKeyboardConfig = config => {
        keyboardActions = new Set(Object.keys(config.shortcuts));
        browserKeys = window.FlyKeyboard.compile(config.shortcuts);
        pageKeys = window.FlyKeyboard.compile(config.normal);
        pagePrefixes = new Set([...pageKeys.keys()].filter(key => key.includes(' ')).map(key => key.split(' ')[0]));
        pageStroke = '';
        window.describeKeyboardAction = (action, label) => {
            const chords = config.shortcuts[action] || [];
            const pageKeys = config.normal[action] || [];
            const bindings = [...chords, ...pageKeys.map(key => `${window.FlyKeyboard.display(key, 'normal')} in NORMAL mode`)];
            return label + (bindings.length ? ` (${bindings.join(' / ')})` : ' (unassigned)');
        };
        const controls = {'WorkspaceHeader': ['toggle-dashboard', 'Workspace dashboard'],
            'split-toggle-btn': ['toggle-split', 'Toggle split view'], 'pin-sidebar-btn': ['toggle-zen-mode', 'Toggle sidebar expansion'],
            'reader-view-btn': ['toggle-reader-view', 'Toggle reader view'],
            'back-btn': ['go-back', 'Go back'], 'forward-btn': ['go-forward', 'Go forward'],
            'settings-btn': ['toggle-help', 'Settings'],
            'toggle-nav-btn': ['spawn-tab', 'New tab'], 'noti-toggle-btn': ['toggle-notifications', 'Toggle web notifications']};
        for (const [id, [action, label]] of Object.entries(controls)) {
            const control = document.getElementById(id);
            if (control) control.title = window.describeKeyboardAction(action, label);
        }
        const welcome = {WelcomeNewTabBtn: 'spawn-tab', WelcomeBookmarksBtn: 'toggle-bookmarks', WelcomePaletteBtn: 'toggle-palette', WelcomeDashboardBtn: 'toggle-dashboard'};
        for (const [id, action] of Object.entries(welcome)) {
            const badge = document.getElementById(id)?.querySelector('.shortcut-key');
            if (badge) badge.textContent = config.shortcuts[action]?.join(' / ') || 'Unassigned';
        }
    };
    window.flyAPI.getKeyboardConfig().then(applyKeyboardConfig);
    window.flyAPI.onKeyboardConfigChanged(applyKeyboardConfig);
    window.addEventListener('keydown', event => {
        // Recover a trusted key misrouted to the host while the DOM still focuses a guest.
        // Send it through the guest's native input path so its own modes/editors decide.
        if (event.isTrusted && !event.isComposing && !['Dead', 'Process'].includes(event.key) && !event.getModifierState?.('AltGraph') && document.activeElement?.tagName === 'WEBVIEW' && (event.target === document.body || event.target === document.activeElement)) {
            const view = document.activeElement;
            event.preventDefault(); event.stopImmediatePropagation();
            view.blur(); view.focus();
            const modifiers = [['ctrlKey', 'control'], ['altKey', 'alt'], ['shiftKey', 'shift'], ['metaKey', 'meta']].filter(([flag]) => event[flag]).map(([, name]) => name);
            const keyCode = ({ArrowDown: 'Down', ArrowUp: 'Up', ArrowLeft: 'Left', ArrowRight: 'Right', ' ': 'Space'})[event.key] || (event.key.length === 1 ? event.key.toUpperCase() : event.key);
            const inputs = [{type: 'keyDown', keyCode, modifiers}];
            if (event.key.length === 1 && !event.ctrlKey && !event.altKey && !event.metaKey) inputs.push({type: 'char', keyCode: event.key, modifiers});
            inputs.push({type: 'keyUp', keyCode, modifiers});
            for (const input of inputs) view.sendInputEvent(input).catch(() => {});
            return;
        }
        if (document.querySelector('dialog[open]') || !event.isTrusted || event.isComposing || event.defaultPrevented || state.awaitingQuickmarkKey) return;
        if (event.key === 'Escape' && addressBar.style.display === 'block') {
            event.preventDefault(); event.stopImmediatePropagation(); displayAddressOverlay(); return;
        }
        // A non-empty settings search clears itself before Escape closes Preferences.
        if (event.key === 'Escape' && state.preferencesActive && !(event.target.id === 'SettingsSearch' && event.target.value)) {
            event.preventDefault(); event.stopImmediatePropagation(); togglePreferencesView(); return;
        }
        const combo = window.FlyKeyboard.eventCombo(event);
        let action = browserKeys.get(combo);
        if (!action && !hasActiveOverlay() && !event.target.closest('input, textarea, select, [contenteditable], [role="textbox"], [role="searchbox"], [role="combobox"]') && document.getElementById('TabList').contains(event.target) &&
            handleSidebarPageAction(pageKeys.get(combo))) {
            event.preventDefault(); event.stopImmediatePropagation(); return;
        }
        if (!action) {
            // Page bindings also work when browser navigation controls retain focus.
            const mode = getActiveWebview()?.__flyKeyboardMode || {};
            const target = event.target;
            if (!combo || ['Escape', 'Tab', 'Enter', ' ', 'Delete', 'ArrowUp', 'ArrowDown', 'ArrowLeft', 'ArrowRight'].includes(event.key) || hasActiveOverlay() || mode.passthrough || mode.input || mode.hints ||
                target.tagName === 'WEBVIEW' || target.isContentEditable || target.closest('input, textarea, select, [role="textbox"], [role="searchbox"], [role="combobox"]')) {
                pageStroke = ''; return;
            }
            const now = Date.now();
            action = pageStroke && now - pageStrokeTime < 700 ? pageKeys.get(pageStroke + ' ' + combo) : null;
            pageStroke = '';
            action ||= pageKeys.get(combo);
            if (!action && pagePrefixes.has(combo)) {
                event.preventDefault(); event.stopImmediatePropagation();
                if (!event.repeat) { pageStroke = combo; pageStrokeTime = now; }
                return;
            }
        } else pageStroke = '';
        if (!action) return;
        event.preventDefault(); event.stopImmediatePropagation();
        if (!event.repeat || window.FlyKeyboard.repeatable(action)) window.dispatchKeyboardAction(action);
    }, true);

    const paletteInput = document.getElementById('PaletteInput');
    paletteInput.addEventListener('input', (e) => {
        filterPaletteCommands(e.target.value);
    });

    paletteInput.addEventListener('keydown', handlePaletteInputNavigation);

    document.getElementById('settings-btn').onclick = () => togglePreferencesView();

    const pinSidebarBtn = document.getElementById('pin-sidebar-btn');
    if (pinSidebarBtn) {
        pinSidebarBtn.onclick = () => toggleSidebarPin();
    }

    // The workspace control and the navigation buttons share one row.
    const topNavIds = ['WorkspaceHeader', 'back-btn', 'forward-btn', 'toggle-nav-btn', 'split-toggle-btn'];
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
            } else if (e.key === 'ArrowDown') {
                e.preventDefault();
                const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
                if (selectedTab) selectedTab.focus();
            } else if (e.key === 'ArrowUp') {
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

    const bottomButtons = ['pin-sidebar-btn', 'reader-view-btn', 'noti-toggle-btn', 'settings-btn'];
    bottomButtons.forEach((id, idx) => {
        const btn = document.getElementById(id);
        if (!btn) return;

        btn.addEventListener('keydown', (e) => {
            if (state.dashboardActive || state.bookmarksActive || state.paletteActive || state.preferencesActive || state.historyActive) return;
            if (e.key === 'ArrowRight') {
                e.preventDefault();
                const nextBtn = document.getElementById(bottomButtons[idx + 1]);
                if (nextBtn) nextBtn.focus();
            } else if (e.key === 'ArrowLeft') {
                e.preventDefault();
                const prevBtn = document.getElementById(bottomButtons[idx - 1]);
                if (prevBtn) prevBtn.focus();
            } else if (e.key === 'ArrowUp') {
                e.preventDefault();
                const selectedTab = document.querySelector('#TabList li.selected') || document.querySelector('#TabList li');
                if (selectedTab) {
                    selectedTab.focus();
                } else {
                    const backBtn = document.getElementById('back-btn');
                    if (backBtn) backBtn.focus();
                }
            } else if (e.key === 'Escape') {
                e.preventDefault();
                const sidebar = document.getElementById('Sidebar');
                if (sidebar) sidebar.classList.remove('expanded');
                focusActiveWebview();
            }
        });
    });

    const tabListContainer = document.getElementById('TabList');
    function handleSidebarPageAction(action) {
        const item = document.activeElement?.closest('#TabList li');
        if (!item) return false;
        const items = [...tabListContainer.querySelectorAll('li')];
        const index = items.indexOf(item);
        if (action === 'scroll-down' || action === 'scroll-up') {
            const next = index + (action === 'scroll-down' ? 1 : -1);
            if (items[next]) {
                items.forEach((tab, at) => { tab.tabIndex = at === next ? 0 : -1; });
                items[next].focus();
            } else document.getElementById(next < 0 ? 'back-btn' : 'pin-sidebar-btn')?.focus();
        } else if (action === 'scroll-left') {
            document.getElementById('back-btn')?.focus();
        } else if (action === 'scroll-right' || action === 'enter-insert') {
            switchTabFocus(index);
            focusActiveWebview();
        } else if (action === 'remove-tab') {
            handleTabRemoval(index);
            const remaining = tabListContainer.querySelectorAll('li');
            remaining[Math.min(index, remaining.length - 1)]?.focus();
        } else if (action === 'yank-url') {
            const url = state.sessionState.workspaces[state.sessionState.current_workspace]?.[index];
            if (url) { navigator.clipboard.writeText(url).catch(() => {}); setTargetUrl(`Yanked URL to clipboard: ${url}`, 2500); }
        } else return false;
        return true;
    }
    tabListContainer.addEventListener('keydown', event => {
        if (hasActiveOverlay() || event.target.closest('input, textarea, select, [contenteditable]')) return;
        if (event.target.tagName === 'BUTTON' && (event.key === 'Enter' || event.key === ' ')) return;
        if (event.key === 'Escape') {
            event.preventDefault();
            document.getElementById('Sidebar').classList.remove('expanded');
            focusActiveWebview();
            return;
        }
        const item = event.target.closest('li');
        const items = [...tabListContainer.querySelectorAll('li')];
        if (!item) return;
        const index = items.indexOf(item);
        const structural = {ArrowDown: 'scroll-down', ArrowUp: 'scroll-up', ArrowLeft: 'scroll-left', ArrowRight: 'scroll-right', Delete: 'remove-tab'}[event.key];
        if (structural && handleSidebarPageAction(structural)) event.preventDefault();
        else if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); switchTabFocus(index); }
        else if (!event.ctrlKey && !event.altKey && !event.metaKey && !event.shiftKey && /^[1-9]$/.test(event.key)) {
            event.preventDefault(); jumpToTabByIndex(Number(event.key));
        }
    });
    window.addEventListener('keydown', event => {
        if (event.key === 'Control') document.body.classList.add('show-tab-badges');
    });
    window.addEventListener('keyup', event => {
        if (event.key === 'Control') document.body.classList.remove('show-tab-badges');
    });
    window.addEventListener('blur', () => document.body.classList.remove('show-tab-badges'));

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
            if (e.key === 'Escape') {
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
            if (state.dashboardActive || state.paletteActive || state.preferencesActive || state.bookmarksActive || state.historyActive || state.diagnosticsActive) return;
            
            const overlayIds = [
                'WideAddressBar', 'AddressSuggestions', 'PaletteInput', 'PaletteList', 'PreferencesOverlay',
                'BookmarksOverlay', 'BookmarkSearchInput', 'HistoryOverlay',
                'HistorySearchInput',
                'DashboardOverlay', 'ShareModalOverlay', 'FindBarOverlay',
                'DiagnosticsOverlay'
            ];
            for (const id of overlayIds) {
                const el = document.getElementById(id);
                if (el && (e.relatedTarget === el || el.contains(e.relatedTarget))) return;
            }

            if (window.flyAllowWebviewFocus) {
                window.flyAllowWebviewFocus = false;
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
            const success = await window.flyAPI.purgeHistory();
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
        state.activeViewsCache[currentWS].forEach(removeWebview);
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
    await window.flyAPI.clearDomainCookies({ urlStr, isPrivate: isTargetPrivate, workspace: currentWS });
    
    window.flyAllowWebviewFocus = true;
    focusActiveWebview();
}

async function executeGlobalCacheWipe() {
    const currentWS = state.sessionState.current_workspace;
    await window.flyAPI.clearActiveCache({ isPrivate: state.globalPrivateModeActive, workspace: currentWS });
    
    window.flyAllowWebviewFocus = true;
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
  
// Websites follow the interface theme through prefers-color-scheme unless turned off in Preferences.
// Only pages that declare their own dark mode react; nothing is forced on the rest.
let syncWebsiteTheme = true;

function applyWebsiteTheme(theme) {
    const websiteTheme = theme === 'light' ? 'light' : 'dark';
    const container = document.getElementById('webview-container');
    if (container) {
        container.classList.remove('theme-light', 'theme-dark');
        container.classList.add(`theme-${websiteTheme}`);
    }
    if (window.flyAPI && typeof window.flyAPI.setNativeTheme === 'function') {
        window.flyAPI.setNativeTheme(websiteTheme);
    }
}

function setWebsiteThemeSync(enabled) {
    syncWebsiteTheme = enabled;
    const interfaceTheme = document.body.classList.contains('light-mode') ? 'light' : 'dark';
    applyWebsiteTheme(enabled ? interfaceTheme : 'light');
    if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
        window.flyAPI.updateBrowserSettings({ sync_website_theme: enabled }).catch(() => {});
    }
}
window.setWebsiteThemeSync = setWebsiteThemeSync;

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
    if (syncWebsiteTheme) applyWebsiteTheme(newTheme);

    // Keep the Preferences theme buttons in sync if open
    document.querySelectorAll('[data-theme-choice]').forEach((button) => {
        button.setAttribute('aria-pressed', String(button.dataset.themeChoice === newTheme));
    });
    
    // Persist setting to disk asynchronously in the background without blocking the UI
    if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
        window.flyAPI.getBrowserSettings().then((cfg) => {
            const currentCfg = cfg || {};
            currentCfg.theme = newTheme;
            return window.flyAPI.updateBrowserSettings(currentCfg);
        }).catch(() => {});
    }
}

document.getElementById('noti-toggle-btn').addEventListener('click', async () => {
    const btn = document.getElementById('noti-toggle-btn');
    const isMuted = !!btn.querySelector('.fa-bell-slash');
    const targetState = isMuted; // If muted, enable notifications; otherwise, mute

    if (window.flyAPI && typeof window.flyAPI.toggleGlobalNotifications === 'function') {
        await window.flyAPI.toggleGlobalNotifications(targetState);
    }

    if (targetState) {
        btn.innerHTML = '<i class="fa-solid fa-bell"></i>';
    } else {
        btn.innerHTML = '<i class="fa-solid fa-bell-slash"></i>';
    }

    window.flyAllowWebviewFocus = true;
    focusActiveWebview();
});

document.addEventListener('DOMContentLoaded', initializeBrowser);

function toggleInPageSearch() {
    const overlay = document.getElementById('FindBarOverlay');
    const input = document.getElementById('FindInput');
    const count = document.getElementById('FindMatchCount');
    
    findActive = !findActive;
    
    if (findActive) {
        prepareOverlay('FindBarOverlay');
        overlay.style.display = 'flex';
        input.value = '';
        count.textContent = '';
        setTimeout(() => {
            if (!findActive) return;
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

export function applySidebarMode(autoCollapse) {
    const body = document.body;
    const sidebar = document.getElementById('Sidebar');
    const pinBtn = document.getElementById('pin-sidebar-btn');
    body.classList.toggle('sidebar-pinned', !autoCollapse);
    body.classList.toggle('sidebar-auto-collapse', autoCollapse);
    sidebar?.classList.remove('expanded');
    if (pinBtn) {
        pinBtn.classList.toggle('pin-active', !autoCollapse);
        pinBtn.setAttribute('aria-pressed', String(!autoCollapse));
    }
    const setting = document.getElementById('setting-sidebar-collapse-toggle');
    if (setting) setting.checked = autoCollapse;
    // A pin click must not leave focus holding the unpinned sidebar open.
    if (autoCollapse && sidebar?.contains(document.activeElement)) {
        document.activeElement.blur();
        focusActiveWebview();
    }
}

// Preserve callers of the old expansion action without a second state toggle.
export {toggleSidebarPin as toggleSidebarExpansion};

export async function toggleSidebarPin() {
    const isCurrentlyPinned = document.body.classList.contains('sidebar-pinned');
    const newAutoCollapse = isCurrentlyPinned;
    applySidebarMode(newAutoCollapse);
    if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
        try {
            await window.flyAPI.updateBrowserSettings({sidebar_auto_collapse: newAutoCollapse});
        } catch (e) {}
    }
}
