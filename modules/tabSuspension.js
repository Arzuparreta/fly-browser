import { state } from './state.js';

// Visibility is authoritative for background time, including workspaces and split panes.
export async function syncTabPolicies() {
    const requests = [];
    for (const [workspace, views] of Object.entries(state.activeViewsCache)) {
        views.forEach((view, index) => {
            if (!view?.isConnected) return;
            try {
                requests.push(window.flyAPI.setTabSuspensionPolicy(view.getWebContentsId(), {
                    visible: view.getClientRects().length > 0 && getComputedStyle(view).visibility !== 'hidden',
                    pinned: !!state.tabPinned?.[workspace]?.[index]
                }));
            } catch { /* A guest before dom-ready has no webContents ID yet. */ }
        });
    }
    await Promise.all(requests);
}
export async function resumeView(view) {
    try { await window.flyAPI.resumeTab(view.getWebContentsId()); } catch { /* Closed or not ready. */ }
}
export async function suspendInactiveTabs() {
    await syncTabPolicies();
    return window.flyAPI.suspendInactiveTabs();
}
export async function resumeWorkspaceTabs(workspace = state.sessionState.current_workspace) {
    await Promise.all((state.activeViewsCache[workspace] || []).filter(Boolean).map(resumeView));
}
export function initializeTabSuspension() {
    let scheduled = false;
    const schedule = () => {
        if (scheduled) return;
        scheduled = true;
        queueMicrotask(() => { scheduled = false; syncTabPolicies().catch(() => {}); });
    };
    new MutationObserver(schedule).observe(document.getElementById('webview-container'), {
        childList: true, subtree: true, attributes: true, attributeFilter: ['style', 'class']
    });
    new MutationObserver(schedule).observe(document.getElementById('TabList'), {
        childList: true, subtree: true, attributes: true, attributeFilter: ['class']
    });
    document.addEventListener('fly-guest-ready', schedule);
    schedule();
}
