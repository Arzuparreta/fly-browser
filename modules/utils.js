import { isOverlayTransition } from './focus.js';
import { state } from './state.js';

// Returns true if the browser interface is currently in dark mode
export const isDarkMode = () => document.body.classList.contains('dark-mode');

// Updates an element's background and text color based on the current theme
export function applyThemeToOverlayElement(element, darkBg, lightBg, darkText, lightText) {
    if (!element) return;
    const dark = isDarkMode();
    element.style.backgroundColor = dark ? darkBg : lightBg;
    element.style.color = dark ? darkText : lightText;
}

// Simple HTML escaping helper for safe rendering
export function escapeHtml(text) {
    if (!text) return "";
    return text
        .split("&").join("&amp;")
        .split("<").join("&lt;")
        .split(">").join("&gt;")
        .split('"').join("&quot;")
        .split("'").join("&#039;");
}

// Script extensions array for Oversight security check
export const SCRIPT_EXTENSIONS = [
    ".sh", ".bash", ".zsh", ".ksh", ".csh", ".fish", ".local",
    ".py", ".pyw", ".pl", ".rb", ".lua", ".tcl", ".pyi",
    ".c", ".cpp", ".cc", ".h", ".hpp", ".rs", ".go", ".js", ".ts"
];

export function isTargetScript(inputText) {
    if (!inputText) return false;
    const lowerInput = inputText.toLowerCase();
    return SCRIPT_EXTENSIONS.some(ext => lowerInput.endsWith(ext));
}

export function getActiveWebview() {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) return null;
    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
    return state.activeViewsCache[currentWS]?.[currentIdx] || null;
}

export function focusActiveWebview() {
    if (isOverlayTransition()) return;
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (activeListItem) {
        const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
        if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
            state.activeViewsCache[currentWS][currentIdx].focus();
        }
    }
}

// Removing a focused guest leaves Chromium routing keys to it, so later clicks and
// focus() on another webview never receive typing. Release focus first.
export function removeWebview(view) {
    if (!view) return;
    if (document.activeElement === view) view.blur();
    view.remove();
}

// Generates a filesystem-safe persistent container partition string for a workspace
export function getWorkspacePartition(workspaceName) {
    if (!workspaceName) return 'persist:default';
    const clean = String(workspaceName).trim().toLowerCase().replace(/[^a-z0-9_-]/gi, '-').replace(/-+/g, '-').replace(/^-|-$/g, '');
    return `persist:${clean || 'default'}`;
}

export function isPdfUrl(url) {
    if (!url || typeof url !== 'string') return false;
    try {
        const parsed = new URL(url);
        return parsed.pathname.toLowerCase().endsWith('.pdf');
    } catch (e) {
        const clean = url.split('?')[0].split('#')[0];
        return clean.toLowerCase().endsWith('.pdf');
    }
}

export function getPdfViewerUrl(targetUrl) {
    if (!targetUrl || typeof targetUrl !== 'string') return targetUrl;
    if (typeof window.flyAPI?.getPdfViewerUrl === 'function') {
        return window.flyAPI.getPdfViewerUrl(targetUrl);
    }
    if (typeof window.flyAPI?.getPdfViewerPath === 'function') {
        const viewerHtml = window.flyAPI.getPdfViewerPath();
        if (targetUrl.startsWith(viewerHtml)) return targetUrl;
        return `${viewerHtml}?file=${encodeURIComponent(targetUrl)}`;
    }
    return targetUrl;
}

export function extractOriginalUrl(url) {
    if (!url || typeof url !== 'string') return url || '';
    if (url.includes('/assets/pdfjs/viewer.html') && url.includes('file=')) {
        try {
            const parsed = new URL(url);
            const fileParam = parsed.searchParams.get('file');
            if (fileParam) return fileParam;
        } catch (e) {}
    }
    return url;
}

// Validates whether a given URL belongs to a target domain or its subdomains
export function isMatchingDomain(url, targetDomain) {
    if (!url || typeof url !== 'string' || !targetDomain) return false;
    try {
        const candidate = /^[a-zA-Z][a-zA-Z\d+\-.]*:\/\//.test(url) ? url : `https://${url}`;
        const host = new URL(candidate).hostname.toLowerCase();
        const target = targetDomain.toLowerCase();
        return host === target || host.endsWith(`.${target}`);
    } catch (e) {
        return false;
    }
}

// Checks whether a URL matches domains designated for isolated private sessions
export function isTargetPrivateDomain(url) {
    if (!url || typeof url !== 'string') return false;
    if (url.includes('fly_test_auth=1')) return true;
    return isMatchingDomain(url, 'ycombinator.com');
}

