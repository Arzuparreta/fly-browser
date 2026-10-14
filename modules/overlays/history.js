import { prepareOverlay } from '../focus.js';
import { state } from '../state.js';
import { escapeHtml, focusActiveWebview } from '../utils.js';
import { renderWorkspaceUI, switchTabFocus, spawnTabWithUrl, spawnNewBlankTab, handleTabRemoval } from '../webview.js';

export function toggleHistoryOverlay() {
    const overlay = document.getElementById('HistoryOverlay');
    const input = document.getElementById('HistorySearchInput');
    
    state.historyActive = !state.historyActive;
    if (state.historyActive) {
        prepareOverlay('HistoryOverlay');
        overlay.style.display = 'flex';
        input.value = '';
        input.focus();
        filterHistoryItems('');
    } else {
        overlay.style.display = 'none';
        focusActiveWebview();
    }
}

export function filterHistoryItems(filterText) {
    const listContainer = document.getElementById('HistoryResultsList');
    listContainer.innerHTML = '';

    window.flyAPI.searchHistory(filterText).then((results) => {
        state.historyResults = results;

        if (state.historyResults.length === 0) {
            listContainer.innerHTML = '<div class="history-empty">No recent history items found.</div>';
            return;
        }

        state.historyResults.forEach((item) => {
            const itemEl = document.createElement('button');
            itemEl.type = 'button';
            itemEl.className = 'history-item';
            
            const safeTitle = escapeHtml(item.title);
            const safeUrl = escapeHtml(item.url);

            itemEl.innerHTML = `
                <span class="history-item-title">${safeTitle}</span>
                <span class="history-item-url">${safeUrl}</span>
            `;

            itemEl.addEventListener('click', () => {
                spawnTabWithUrl(item.url);
                toggleHistoryOverlay();
            });

            listContainer.appendChild(itemEl);
        });
    }).catch((err) => {
        console.error("Failed to load history:", err);
    });
}
