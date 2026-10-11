// modules/darkReader.js
// Minimal, zero-dependency Dark Reader engine for Mise Browser (KISS)

import { state } from './state.js';
import { getActiveWebview } from './utils.js';
import { isSplitActive, getSplitState } from './splitView.js';
import { setTargetUrl } from './statusBar.js';

export const DARK_READER_CSS = `
html {
    min-height: 100vh !important;
    background-color: #ffffff !important;
    filter: invert(90%) hue-rotate(180deg) contrast(88%) !important;
}

body {
    min-height: 100vh !important;
}

/* Re-invert media elements so photos, videos, canvases and charts preserve natural colours */
img, video, canvas:not(.pdf-page-canvas), svg:not(:root), embed, object, iframe {
    filter: invert(100%) hue-rotate(180deg) !important;
    mix-blend-mode: normal !important;
}

/* Reset mix-blend-mode and prevent double inversion on picture containers */
picture {
    filter: none !important;
    mix-blend-mode: normal !important;
}

/* Re-invert background image icons and leaf elements without inverting textual content */
[style*="background-image"]:empty {
    filter: invert(100%) hue-rotate(180deg) !important;
}

/* Avoid bright halo around transparent PNG images */
img {
    background-color: transparent !important;
}

/* Ensure fullscreen video players are never inverted */
:fullscreen, :fullscreen * {
    filter: none !important;
}

/* Dark mode scrollbars */
::-webkit-scrollbar {
    background-color: #16161e !important;
    width: 10px !important;
    height: 10px !important;
}
::-webkit-scrollbar-thumb {
    background-color: #3b4261 !important;
    border-radius: 4px !important;
}
::-webkit-scrollbar-thumb:hover {
    background-color: #7aa2f7 !important;
}
`;

export function isWebviewDarkReaderActive(wv) {
    return !!(wv && wv.__miseDarkReaderEnabled);
}

export async function setWebviewDarkReader(wv, enable) {
    if (!wv) return false;
    wv.__miseDarkReaderEnabled = !!enable;

    if (enable) {
        if (typeof wv.insertCSS === 'function') {
            try {
                if (wv.__miseDarkKey && typeof wv.removeInsertedCSS === 'function') {
                    try { await wv.removeInsertedCSS(wv.__miseDarkKey); } catch (e) {}
                    wv.__miseDarkKey = null;
                }
                const key = await wv.insertCSS(DARK_READER_CSS);
                wv.__miseDarkKey = key;
                return true;
            } catch (err) {
                console.error('Failed to apply Dark Reader CSS:', err);
                return false;
            }
        }
        return false;
    } else {
        if (wv.__miseDarkKey && typeof wv.removeInsertedCSS === 'function') {
            try {
                await wv.removeInsertedCSS(wv.__miseDarkKey);
            } catch (err) {}
            wv.__miseDarkKey = null;
        }
        return false;
    }
}

export async function reapplyWebviewDarkReader(wv) {
    if (!wv || !wv.__miseDarkReaderEnabled) return;
    if (typeof wv.insertCSS !== 'function') return;
    try {
        if (wv.__miseDarkKey && typeof wv.removeInsertedCSS === 'function') {
            try { await wv.removeInsertedCSS(wv.__miseDarkKey); } catch (e) {}
            wv.__miseDarkKey = null;
        }
        const key = await wv.insertCSS(DARK_READER_CSS);
        wv.__miseDarkKey = key;
    } catch (err) {
        console.error('Failed to reapply Dark Reader CSS on navigation:', err);
    }
}

export function updateDarkReaderButtonUI(enabled) {
    const btn = document.getElementById('dark-reader-btn');
    if (!btn) return;
    if (enabled) {
        btn.classList.add('active');
        btn.title = 'Toggle Dark Reader (Active - Ctrl+D / d)';
    } else {
        btn.classList.remove('active');
        btn.title = 'Toggle Dark Reader (Ctrl+D / d)';
    }
}

export async function toggleDarkReaderOnActiveTab() {
    const currentWS = state.sessionState.current_workspace;
    const activeWv = getActiveWebview();
    if (!activeWv) return;

    const willEnable = !isWebviewDarkReaderActive(activeWv);
    await setWebviewDarkReader(activeWv, willEnable);

    // Track tab Dark Reader state in central state store
    const activeListItem = document.querySelector('#TabList li.selected');
    const currentIdx = activeListItem ? Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem) : -1;
    if (currentIdx >= 0) {
        if (!state.tabDarkReaderStates) state.tabDarkReaderStates = {};
        if (!state.tabDarkReaderStates[currentWS]) state.tabDarkReaderStates[currentWS] = [];
        state.tabDarkReaderStates[currentWS][currentIdx] = willEnable;
    }

    // If split view is active, toggle the secondary pane as well
    if (isSplitActive(currentWS)) {
        const split = getSplitState(currentWS);
        const views = state.activeViewsCache[currentWS] || [];
        const otherIdx = (split.activePane === 'primary') ? split.secondaryIdx : split.primaryIdx;
        const otherWv = views[otherIdx];
        if (otherWv) {
            await setWebviewDarkReader(otherWv, willEnable);
            if (otherIdx >= 0) {
                state.tabDarkReaderStates[currentWS][otherIdx] = willEnable;
            }
        }
    }

    updateDarkReaderButtonUI(willEnable);
    setTargetUrl(willEnable ? 'Dark Reader: On' : 'Dark Reader: Off', 1500);
}
