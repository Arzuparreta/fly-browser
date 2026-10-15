// modules/splitView.js
// Dual-split view controller for side-by-side (1x2) or stacked comparison

import { state } from './state.js';
import { wakeTab, renderWorkspaceUI, spawnNewBlankTab, switchTabFocus } from './webview.js';
import { loadWebviewSource } from './extensions.js';

export function getSplitState(wsName) {
    if (!wsName) wsName = state.sessionState.current_workspace;
    if (!state.splitStates[wsName]) {
        state.splitStates[wsName] = {
            enabled: false,
            mode: 'vertical',
            primaryIdx: 0,
            secondaryIdx: 1,
            activePane: 'primary'
        };
    }
    return state.splitStates[wsName];
}

export function isSplitActive(wsName) {
    const s = getSplitState(wsName);
    return !!s.enabled;
}

export function applySplitLayout() {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    const container = document.getElementById('webview-container');
    const splitBtn = document.getElementById('split-toggle-btn');
    const urls = state.sessionState.workspaces[currentWS] || [];
    const currentWSViews = state.activeViewsCache[currentWS] || [];

    if (!split.enabled || urls.length < 2 || state.dashboardActive) {
        const leavingSplit = !!container?.classList.contains('split-active');
        if (container) {
            container.classList.remove('split-active', 'split-vertical', 'split-horizontal');
        }
        if (splitBtn) {
            splitBtn.classList.remove('active');
            splitBtn.title = window.describeKeyboardAction?.('toggle-split', 'Toggle split view') || 'Toggle split view';
        }

        currentWSViews.forEach((wv) => {
            if (wv) {
                wv.classList.remove('split-pane-primary', 'split-pane-secondary', 'split-pane-active');
            }
        });

        document.querySelectorAll('#TabList li').forEach((li) => {
            li.classList.remove('split-pane-1-tab', 'split-pane-2-tab');
            const existingBadge = li.querySelector('.split-tab-badge');
            if (existingBadge) existingBadge.remove();
        });

        if (typeof window.recalculateStatusMode === 'function') {
            window.recalculateStatusMode();
        }
        // Both panes keep display: flex, so the later one in the DOM would cover the selected tab.
        if (leavingSplit && !state.dashboardActive) {
            const selectedIdx = [...document.querySelectorAll('#TabList li')].findIndex(li => li.classList.contains('selected'));
            if (selectedIdx >= 0) switchTabFocus(selectedIdx);
        }
        return false;
    }

    // Validate indices against workspace bounds
    if (split.primaryIdx >= urls.length) split.primaryIdx = 0;
    if (split.secondaryIdx >= urls.length || split.secondaryIdx === split.primaryIdx) {
        split.secondaryIdx = (split.primaryIdx + 1) % urls.length;
    }

    // Ensure both split webviews are awake
    if (state.tabSleepStates[currentWS]?.[split.primaryIdx]) {
        wakeTab(currentWS, split.primaryIdx);
    }
    if (state.tabSleepStates[currentWS]?.[split.secondaryIdx]) {
        wakeTab(currentWS, split.secondaryIdx);
    }

    // Apply layout classes to webview container
    if (container) {
        container.classList.add('split-active');
        container.classList.remove('split-vertical', 'split-horizontal');
        container.classList.add('split-' + split.mode);
    }

    if (splitBtn) {
        splitBtn.classList.add('active');
        const modeLabel = split.mode === 'vertical' ? 'Side-by-Side (1x2)' : 'Stacked (2x1)';
        splitBtn.title = window.describeKeyboardAction?.('toggle-split', `Split view active: ${modeLabel}`) || `Split view active: ${modeLabel}`;
    }

    // Update webview styles and positioning
    currentWSViews.forEach((wv, idx) => {
        if (!wv) return;

        if (idx === split.primaryIdx) {
            wv.style.display = 'flex';
            wv.classList.add('split-pane-primary');
            wv.classList.remove('split-pane-secondary');
            if (split.activePane === 'primary') {
                wv.classList.add('split-pane-active');
            } else {
                wv.classList.remove('split-pane-active');
            }
        } else if (idx === split.secondaryIdx) {
            wv.style.display = 'flex';
            wv.classList.add('split-pane-secondary');
            wv.classList.remove('split-pane-primary');
            if (split.activePane === 'secondary') {
                wv.classList.add('split-pane-active');
            } else {
                wv.classList.remove('split-pane-active');
            }
        } else {
            wv.style.display = 'none';
            wv.classList.remove('split-pane-primary', 'split-pane-secondary', 'split-pane-active');
        }
    });

    // Update tab list indicators and badges
    const tabItems = document.querySelectorAll('#TabList li');
    tabItems.forEach((li, idx) => {
        li.classList.remove('split-pane-1-tab', 'split-pane-2-tab');
        const existingBadge = li.querySelector('.split-tab-badge');
        if (existingBadge) existingBadge.remove();

        const favWrapper = li.querySelector('.tab-favicon-wrapper');
        const badgeMount = favWrapper || li;

        if (idx === split.primaryIdx) {
            li.classList.add('split-pane-1-tab');
            if (split.activePane === 'primary') {
                li.classList.add('selected');
                li.setAttribute('tabindex', '0');
            } else {
                li.classList.remove('selected');
                li.setAttribute('tabindex', '-1');
            }

            const badge = document.createElement('span');
            badge.className = 'split-tab-badge split-badge-1';
            badge.title = 'Split Pane 1 (Primary)';
            badge.textContent = '1';
            badgeMount.appendChild(badge);
        } else if (idx === split.secondaryIdx) {
            li.classList.add('split-pane-2-tab');
            if (split.activePane === 'secondary') {
                li.classList.add('selected');
                li.setAttribute('tabindex', '0');
            } else {
                li.classList.remove('selected');
                li.setAttribute('tabindex', '-1');
            }

            const badge = document.createElement('span');
            badge.className = 'split-tab-badge split-badge-2';
            badge.title = 'Split Pane 2 (Secondary)';
            badge.textContent = '2';
            badgeMount.appendChild(badge);
        } else {
            li.classList.remove('selected');
            li.setAttribute('tabindex', '-1');
        }
    });

    // Ensure focus is given to active pane
    const activeIdx = (split.activePane === 'secondary') ? split.secondaryIdx : split.primaryIdx;
    if (currentWSViews[activeIdx]) {
        setTimeout(() => {
            const currentFocused = document.activeElement;
            const sidebarHasFocus = document.getElementById('Sidebar')?.contains(currentFocused);
            const addressBar = document.getElementById('WideAddressBar');
            const addressBarIsActive = addressBar && (addressBar.style.display === 'block' || currentFocused === addressBar);

            if (!sidebarHasFocus && !state.paletteActive && !state.helpActive && !addressBarIsActive) {
                window.flyAllowWebviewFocus = true;
                currentWSViews[activeIdx].focus();
            }
        }, 30);
    }

    if (typeof window.recalculateStatusMode === 'function') {
        window.recalculateStatusMode();
    }

    return true;
}

export async function toggleSplitView(modeOverride = null) {
    const currentWS = state.sessionState.current_workspace;
    const urls = state.sessionState.workspaces[currentWS] || [];
    const split = getSplitState(currentWS);

    if (urls.length < 2) {
        await spawnNewBlankTab();
        const updatedUrls = state.sessionState.workspaces[currentWS] || [];
        if (updatedUrls.length >= 2) {
            split.enabled = true;
            split.mode = modeOverride || 'vertical';
            split.primaryIdx = 0;
            split.secondaryIdx = 1;
            split.activePane = 'secondary';
            applySplitLayout();
        }
        return;
    }

    if (!split.enabled) {
        split.enabled = true;
        split.mode = modeOverride || 'vertical';
        const activeLi = document.querySelector('#TabList li.selected');
        const activeIdx = activeLi ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeLi) : 0;
        split.primaryIdx = Math.max(0, activeIdx);
        split.secondaryIdx = (split.primaryIdx + 1 < urls.length) ? split.primaryIdx + 1 : (split.primaryIdx > 0 ? split.primaryIdx - 1 : 1);
        split.activePane = 'primary';
    } else if (modeOverride && split.mode !== modeOverride) {
        split.mode = modeOverride;
    } else {
        split.enabled = false;
    }

    applySplitLayout();
}

export function cycleSplitOrientation() {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);

    if (!split.enabled) {
        toggleSplitView('vertical');
    } else if (split.mode === 'vertical') {
        split.mode = 'horizontal';
        applySplitLayout();
    } else {
        closeSplitView();
    }
}

export function switchSplitFocus() {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    if (!split.enabled) return;

    split.activePane = (split.activePane === 'primary') ? 'secondary' : 'primary';
    applySplitLayout();
}

export function swapSplitPanes() {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    if (!split.enabled) return;

    const temp = split.primaryIdx;
    split.primaryIdx = split.secondaryIdx;
    split.secondaryIdx = temp;
    applySplitLayout();
}

export function closeSplitView() {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    if (!split.enabled) return;

    const targetIdx = (split.activePane === 'secondary') ? split.secondaryIdx : split.primaryIdx;
    split.enabled = false;
    applySplitLayout();

    if (typeof window.switchTabFocus === 'function') {
        window.switchTabFocus(targetIdx);
    }
}

export function handleSplitTabSelection(targetIdx) {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    if (!split.enabled) return false;

    if (targetIdx === split.primaryIdx) {
        split.activePane = 'primary';
    } else if (targetIdx === split.secondaryIdx) {
        split.activePane = 'secondary';
    } else {
        if (split.activePane === 'primary') {
            split.primaryIdx = targetIdx;
        } else {
            split.secondaryIdx = targetIdx;
        }
    }

    applySplitLayout();
    return true;
}

export function handleTabRemovalInSplit(removedIdx) {
    const currentWS = state.sessionState.current_workspace;
    const split = getSplitState(currentWS);
    if (!split.enabled) return;

    const urls = state.sessionState.workspaces[currentWS] || [];
    if (urls.length < 2) {
        split.enabled = false;
        applySplitLayout();
        return;
    }

    if (split.primaryIdx === removedIdx) {
        split.primaryIdx = (split.secondaryIdx === 0) ? 1 : 0;
        split.activePane = 'secondary';
    } else if (split.primaryIdx > removedIdx) {
        split.primaryIdx--;
    }

    if (split.secondaryIdx === removedIdx) {
        split.secondaryIdx = (split.primaryIdx === 0) ? 1 : 0;
        split.activePane = 'primary';
    } else if (split.secondaryIdx > removedIdx) {
        split.secondaryIdx--;
    }

    if (split.primaryIdx === split.secondaryIdx) {
        split.secondaryIdx = (split.primaryIdx + 1) % urls.length;
    }

    applySplitLayout();
}

export async function openUrlInSplit(url) {
    if (!url) return;
    const currentWS = state.sessionState.current_workspace;
    if (!state.sessionState.workspaces[currentWS]) {
        state.sessionState.workspaces[currentWS] = [];
    }
    const split = getSplitState(currentWS);

    if (split.enabled) {
        const targetIdx = (split.activePane === 'primary') ? split.secondaryIdx : split.primaryIdx;
        state.sessionState.workspaces[currentWS][targetIdx] = url;
        window.flyAPI.saveSession(state.sessionState);

        const wv = state.activeViewsCache[currentWS]?.[targetIdx];
        if (wv && typeof wv.setAttribute === 'function') {
            loadWebviewSource(wv, currentWS, url);
        } else {
            wakeTab(currentWS, targetIdx);
        }
        applySplitLayout();
        return;
    }

    const activeLi = document.querySelector('#TabList li.selected');
    const activeIdx = activeLi ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeLi) : 0;

    state.sessionState.workspaces[currentWS].push(url);
    window.flyAPI.saveSession(state.sessionState);

    const newTargetIdx = state.sessionState.workspaces[currentWS].length - 1;
    if (!state.tabSleepStates[currentWS]) state.tabSleepStates[currentWS] = [];
    state.tabSleepStates[currentWS][newTargetIdx] = null;
    if (!state.tabActivityTimestamps[currentWS]) state.tabActivityTimestamps[currentWS] = [];
    state.tabActivityTimestamps[currentWS][newTargetIdx] = Date.now();
    if (!state.tabMediaAudible[currentWS]) state.tabMediaAudible[currentWS] = [];
    state.tabMediaAudible[currentWS][newTargetIdx] = false;
    if (!state.tabPinned[currentWS]) state.tabPinned[currentWS] = [];
    state.tabPinned[currentWS][newTargetIdx] = false;

    split.enabled = true;
    split.mode = split.mode || 'vertical';
    split.primaryIdx = activeIdx;
    split.secondaryIdx = newTargetIdx;
    split.activePane = 'secondary';

    renderWorkspaceUI(newTargetIdx);
}
