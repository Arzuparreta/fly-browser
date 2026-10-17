export const NEW_TAB_URL = new URL('../assets/new-tab.html', import.meta.url).href;
export function isNewTab(url) { return url === 'about:blank' || url === NEW_TAB_URL; }

const refreshVersions = new WeakMap();
let bookmarkRevision = 0;
let latestLists;
function bookmarkLinks(saved) {
    return (Array.isArray(saved) ? saved : []).flatMap(bookmark => {
        try {
            const url = new URL(bookmark.url);
            if (!['http:', 'https:'].includes(url.protocol)) return [];
            const title = typeof bookmark.title === 'string' && bookmark.title.trim() ? bookmark.title : url.host;
            return [{url:url.href, title}];
        } catch { return []; }
    });
}

// A save pushes the already-cached lists; no diagnostics, settings reads or polling.
export async function updateNewTabLists(data) {
    bookmarkRevision++;
    latestLists = {bookmarks:bookmarkLinks(data.bookmarks), sites:data.sites};
    const script = `if (location.href === ${JSON.stringify(NEW_TAB_URL)}) window.updateFlyNewTabLists?.(${JSON.stringify(latestLists)})`;
    await Promise.all([...document.querySelectorAll('webview')].map(async view => {
        try {
            if (view.style.display !== 'none' && view.getAttribute('partition') !== 'FlyPrivateProfile' && view.getURL() === NEW_TAB_URL) await view.executeJavaScript(script);
        } catch { /* A guest may close or navigate during the update. */ }
    }));
}

// A snapshot on activation, never a background metrics loop.
export async function refreshNewTab(view) {
    try {
        if (!view || view.getURL() !== NEW_TAB_URL) return;
        const version = (refreshVersions.get(view) || 0) + 1;
        refreshVersions.set(view, version);
        const revision = bookmarkRevision;
        const [config, diagnostics, savedBookmarks] = await Promise.all([
            window.flyAPI.getBrowserSettings(), window.flyAPI.getSystemDiagnostics({}),
            view.getAttribute('partition') === 'FlyPrivateProfile' ? [] : window.flyAPI.readBookmarks()
        ]);
        const showSites = config?.show_frequent_sites !== false && view.getAttribute('partition') !== 'FlyPrivateProfile';
        const sites = showSites ? await window.flyAPI.getFrequentSites() : [];
        if (!view.isConnected || view.getURL() !== NEW_TAB_URL || refreshVersions.get(view) !== version) return;
        const style = getComputedStyle(document.body);
        const colours = Object.fromEntries(['--bg-main', '--text', '--accent', '--panel', '--cell', '--border', '--dim', '--link'].map(key => [key, style.getPropertyValue(key).trim()]));
        const data = {
            light: document.body.classList.contains('light-mode'), colours,
            sites, showSites,
            showBookmarks: view.getAttribute('partition') !== 'FlyPrivateProfile',
            bookmarks: bookmarkLinks(savedBookmarks),
            memory: Math.round((diagnostics?.processes || []).reduce((sum, process) => sum + (Number.isFinite(process.residentSetKB) ? process.residentSetKB : 0), 0) / 1024)
        };
        if (data.showBookmarks && revision !== bookmarkRevision) Object.assign(data, latestLists);
        await view.executeJavaScript(`if (location.href === ${JSON.stringify(NEW_TAB_URL)}) window.updateFlyNewTab?.(${JSON.stringify(data)})`);
    } catch { /* A tab may close or navigate while the snapshot is gathered. */ }
}

export function refreshNewTabs() {
    return Promise.all([...document.querySelectorAll('webview')]
        .filter(view => view.style.display !== 'none').map(refreshNewTab));
}
