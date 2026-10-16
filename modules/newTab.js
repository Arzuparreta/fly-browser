import { state } from './state.js';

export const NEW_TAB_URL = new URL('../assets/new-tab.html', import.meta.url).href;
export function isNewTab(url) { return url === 'about:blank' || url === NEW_TAB_URL; }

const refreshVersions = new WeakMap();

// A snapshot on activation, never a background metrics loop.
export async function refreshNewTab(view) {
    try {
        if (!view || view.getURL() !== NEW_TAB_URL) return;
        const version = (refreshVersions.get(view) || 0) + 1;
        refreshVersions.set(view, version);
        const [config, diagnostics, history] = await Promise.all([
            window.flyAPI.getBrowserSettings(), window.flyAPI.getSystemDiagnostics({}),
            state.globalPrivateModeActive ? [] : window.flyAPI.searchHistory('')
        ]);
        if (!view.isConnected || view.getURL() !== NEW_TAB_URL || refreshVersions.get(view) !== version) return;
        const style = getComputedStyle(document.body);
        const colours = Object.fromEntries(['--bg-main', '--text', '--accent', '--panel', '--cell', '--border', '--dim', '--link'].map(key => [key, style.getPropertyValue(key).trim()]));
        const sites = new Map();
        for (const entry of history || []) {
            try {
                const url = new URL(entry.url);
                if (!['http:', 'https:'].includes(url.protocol)) continue;
                const site = sites.get(url.origin) || { url: url.origin, title: url.hostname, count: 0 };
                site.count++;
                sites.set(url.origin, site);
            } catch {}
        }
        const data = {
            light: document.body.classList.contains('light-mode'), colours,
            addressHint: window.describeKeyboardAction?.('toggle-address', 'Address bar') || '',
            search: config?.search_engine || 'https://duckduckgo.com/?q=%s',
            sites: [...sites.values()].sort((a, b) => b.count - a.count).slice(0, 5),
            memory: Math.round((diagnostics?.processes || []).reduce((sum, process) => sum + (Number.isFinite(process.residentSetKB) ? process.residentSetKB : 0), 0) / 1024)
        };
        await view.executeJavaScript(`if (location.href === ${JSON.stringify(NEW_TAB_URL)}) window.updateFlyNewTab?.(${JSON.stringify(data)})`);
    } catch { /* A tab may close or navigate while the snapshot is gathered. */ }
}

export function refreshNewTabs() {
    return Promise.all([...document.querySelectorAll('webview')]
        .filter(view => view.style.display !== 'none').map(refreshNewTab));
}
