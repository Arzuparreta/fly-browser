import { prepareOverlay } from './focus.js';
import { state } from './state.js';
import { escapeHtml, focusActiveWebview, getActiveWebview, isPdfUrl, getPdfViewerUrl } from './utils.js';
import { renderWorkspaceUI, switchTabFocus, spawnTabWithUrl, spawnNewBlankTab, handleTabRemoval, wakeTab } from './webview.js';
import { formatSearchUrl, resolveSearchAliasUrl, getEffectiveSearchAliases } from './overlays/searchEngine.js';
import { evaluateMathExpression } from './calculator.js';
import { setTargetUrl } from './statusBar.js';
import { loadWebviewSource } from './extensions.js';

export const POPULAR_BANGS = {
    '!w': 'Wikipedia',
    '!g': 'Google',
    '!gh': 'GitHub',
    '!yt': 'YouTube',
    '!a': 'ArchWiki',
    '!aw': 'ArchWiki',
    '!aur': 'Arch User Repository',
    '!r': 'Reddit',
    '!so': 'Stack Overflow',
    '!ddg': 'DuckDuckGo',
    '!d': 'DuckDuckGo',
    '!b': 'Brave Search',
    '!k': 'Kagi',
    '!sp': 'Startpage',
    '!m': 'MDN Web Docs',
    '!mdn': 'MDN Web Docs',
    '!wikt': 'Wiktionary',
    '!osm': 'OpenStreetMap',
    '!npm': 'npm Registry',
    '!p': 'Python Docs',
    '!py': 'Python Docs',
    '!rust': 'Rust Docs',
    '!cpp': 'C++ Reference',
    '!imdb': 'IMDb',
    '!tr': 'Google Translate',
    '!eb': 'eBay',
    '!am': 'Amazon',
    '!maps': 'Google Maps',
    '!tw': 'Twitter / X',
    '!x': 'Twitter / X',
    '!v': 'Vimeo',
    '!arch': 'Arch Linux Packages'
};

export function extractDdgBang(query) {
    if (!query || typeof query !== 'string') return null;
    const trimmed = query.trim();
    const match = trimmed.match(/(?:^|\s)!([a-zA-Z0-9]+)(?:\s|$)/);
    if (match) {
        const bang = '!' + match[1].toLowerCase();
        const cleanQuery = trimmed.replace(new RegExp('(?:^|\\s)!' + match[1] + '(?:\\s|$)', 'i'), ' ').trim();
        return {
            bang,
            cleanQuery,
            fullQuery: trimmed
        };
    }
    return null;
}

function applyTargetUrl(targetUrl) {
    const currentWS = state.sessionState.current_workspace;
    const activeListItem = document.querySelector('#TabList li.selected');
    if (!activeListItem) {
        hideSuggestions();
        spawnTabWithUrl(targetUrl);
        focusActiveWebview();
        return;
    }

    const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);

    state.sessionState.workspaces[currentWS][currentIdx] = targetUrl;
    window.flyAPI.saveSession(state.sessionState);

    const effectiveLoadUrl = isPdfUrl(targetUrl) ? getPdfViewerUrl(targetUrl) : targetUrl;

    if (state.activeViewsCache[currentWS] && state.activeViewsCache[currentWS][currentIdx]) {
        loadWebviewSource(state.activeViewsCache[currentWS][currentIdx], currentWS, effectiveLoadUrl);
    } else {
        wakeTab(currentWS, currentIdx);
    }

    hideSuggestions();
    const addressBar = document.getElementById('WideAddressBar');
    if (addressBar) addressBar.style.display = 'none';
    focusActiveWebview();
}

export function displayAddressOverlay() {
    const addressBar = document.getElementById('WideAddressBar');
    if (addressBar.style.display === 'block') {
        addressBar.style.display = 'none';
        hideSuggestions();
        focusActiveWebview();
    } else {
        const currentWS = state.sessionState.current_workspace;
        prepareOverlay('WideAddressBar');
        const activeListItem = document.querySelector('#TabList li.selected');
        if (activeListItem) {
            const currentIdx = Array.from(document.querySelectorAll('#TabList li')).indexOf(activeListItem);
            if (state.sessionState.workspaces[currentWS] && state.sessionState.workspaces[currentWS][currentIdx]) {
                addressBar.value = state.sessionState.workspaces[currentWS][currentIdx] === 'about:blank' ? '' : state.sessionState.workspaces[currentWS][currentIdx];
            }
        }
        
        addressBar.style.display = 'block';
        
        requestAnimationFrame(() => {
            if (addressBar.style.display !== 'block') return;
            addressBar.focus();
            addressBar.select();
        });
    }
}

export async function handleNavigation(input) {
    if (!input) return;
    
    const trimmedInput = input.trim();

    if (trimmedInput.toLowerCase().startsWith('ws:') || trimmedInput.toLowerCase().startsWith('ws ')) {
        const targetWs = trimmedInput.replace(/^ws[:\s]+/i, '').trim();
        const availableWorkspaces = Object.keys(state.sessionState.workspaces);
        const matchedWs = availableWorkspaces.find(ws => ws.toLowerCase() === targetWs.toLowerCase());

        if (matchedWs) {
            state.sessionState.current_workspace = matchedWs;
            window.flyAPI.saveSession(state.sessionState);
            renderWorkspaceUI(0);
        } else {
            alert(`Workspace "${targetWs}" does not exist.`);
        }

        hideSuggestions();
        document.getElementById('WideAddressBar').style.display = 'none';
        focusActiveWebview();
        return;
    }

    // Direct arithmetic evaluation
    const calcResult = evaluateMathExpression(trimmedInput);
    if (calcResult !== null) {
        if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
            navigator.clipboard.writeText(String(calcResult)).catch(() => {});
        }
        setTargetUrl(`Calculator: Copied ${calcResult} to clipboard`, 3500);
        const addressBar = document.getElementById('WideAddressBar');
        if (addressBar) {
            addressBar.value = String(calcResult);
            addressBar.style.display = 'none';
        }
        hideSuggestions();
        focusActiveWebview();
        return;
    }

    // DuckDuckGo bang redirection
    const bangMatch = extractDdgBang(trimmedInput);
    if (bangMatch) {
        const targetUrl = `https://duckduckgo.com/?q=${encodeURIComponent(trimmedInput)}`;
        applyTargetUrl(targetUrl);
        return;
    }

    const parts = trimmedInput.split(' ');
    const aliasCandidate = parts[0].toLowerCase();
    const aliasQuery = parts.slice(1).join(' ').trim();

    let targetUrl = null;
    if (aliasQuery) {
        targetUrl = resolveSearchAliasUrl(aliasCandidate, aliasQuery, state.customSearchAliases);
    }

    if (!targetUrl) {
        targetUrl = trimmedInput;
        if (trimmedInput.startsWith('/') || trimmedInput.startsWith('file://')) {
            targetUrl = trimmedInput.startsWith('/') ? 'file://' + trimmedInput : trimmedInput;
        } else if (!trimmedInput.startsWith('http://') && !trimmedInput.startsWith('https://')) {
            if (trimmedInput.includes('.') && !trimmedInput.includes(' ')) {
                targetUrl = `https://${trimmedInput}`;
            } else {
                let searchTemplate = 'https://duckduckgo.com/?q=%s';
                if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
                    try {
                        const cfg = await window.flyAPI.getBrowserSettings();
                        if (cfg && cfg.search_engine) {
                            searchTemplate = cfg.search_engine;
                        }
                    } catch (e) {}
                }
                targetUrl = formatSearchUrl(trimmedInput, searchTemplate);
            }
        }
    }

    applyTargetUrl(targetUrl);
}

export function setupAddressBarAutocomplete() {
    const addressBar = document.getElementById('WideAddressBar');
    const suggestionsList = document.getElementById('AddressSuggestions');

    if (!addressBar || !suggestionsList) return;

    // Presses on the list keep focus in the bar, so a later page click still closes it.
    suggestionsList.addEventListener('mousedown', e => e.preventDefault());

    addressBar.addEventListener('input', async (e) => {
        const query = e.target.value.trim();
        if (!query) {
            hideSuggestions();
            return;
        }

        if (query.toLowerCase().startsWith('ws ')) {
            const wsQuery = query.slice(3).toLowerCase();
            const matchingWorkspaces = Object.keys(state.sessionState.workspaces)
                .filter(ws => ws.toLowerCase().includes(wsQuery))
                .map(ws => ({ title: `Switch to Workspace: ${ws}`, value: `ws:${ws}`, type: 'Workspace' }));

            renderSuggestions(matchingWorkspaces);
            return;
        }

        const matches = [];

        // 1. Calculator evaluation
        const calcVal = evaluateMathExpression(query);
        if (calcVal !== null) {
            matches.push({
                title: `= ${calcVal}`,
                value: String(calcVal),
                type: 'Calculator',
                isCalc: true,
                expr: query
            });
            matches.push({
                title: `Search web for "${query}"`,
                value: query,
                type: 'Search',
                forceSearch: true
            });
        }

        // 2. DuckDuckGo Bang detection
        const bangInfo = extractDdgBang(query);
        if (bangInfo) {
            const bangDesc = POPULAR_BANGS[bangInfo.bang];
            let bangTitle = '';
            if (bangDesc) {
                bangTitle = bangInfo.cleanQuery 
                    ? `${bangDesc}: ${bangInfo.cleanQuery} (${bangInfo.bang})`
                    : `${bangDesc} (${bangInfo.bang})`;
            } else {
                bangTitle = `DuckDuckGo Bang: ${query}`;
            }

            matches.push({
                title: bangTitle,
                value: `https://duckduckgo.com/?q=${encodeURIComponent(query)}`,
                type: 'Bang',
                isBang: true
            });
        }

        // 3. Search Alias detection (built-in + custom)
        const queryParts = query.split(' ');
        const aliasCandidate = queryParts[0].toLowerCase();
        const aliasSearch = queryParts.slice(1).join(' ').trim();
        const effectiveAliases = getEffectiveSearchAliases(state.customSearchAliases);
        const aliasEntry = effectiveAliases[aliasCandidate];

        if (aliasEntry) {
            const aliasTitle = aliasSearch
                ? `Search ${aliasEntry.name}: "${aliasSearch}" (${aliasCandidate})`
                : `Search ${aliasEntry.name} (${aliasCandidate})`;
            const resolvedUrl = aliasSearch
                ? resolveSearchAliasUrl(aliasCandidate, aliasSearch, state.customSearchAliases)
                : (aliasEntry.url.includes('%s') ? aliasEntry.url.replace('%s', '') : aliasEntry.url);

            matches.push({
                title: aliasTitle,
                value: resolvedUrl || query,
                type: 'Search',
                isAlias: true
            });
        }

        Object.keys(state.sessionState.workspaces).forEach(wsName => {
            state.sessionState.workspaces[wsName].forEach((url, idx) => {
                const title = (state.activeTitlesCache[wsName] && state.activeTitlesCache[wsName][idx]) || url;
                if (title.toLowerCase().includes(query.toLowerCase()) || url.toLowerCase().includes(query.toLowerCase())) {
                    matches.push({ title: `${title} (${wsName})`, value: url, type: 'Tab' });
                }
            });
        });

        if (window.flyAPI && typeof window.flyAPI.searchHistory === 'function') {
            try {
                const historyMatches = await window.flyAPI.searchHistory(query);
                historyMatches.slice(0, 5).forEach(item => {
                    matches.push({ title: item.title, value: item.url, type: 'History' });
                });
            } catch (err) {}
        }

        state.bookmarks.forEach(bm => {
            if (bm.title.toLowerCase().includes(query.toLowerCase()) || bm.url.toLowerCase().includes(query.toLowerCase())) {
                matches.push({ title: bm.title, value: bm.url, type: 'Bookmark' });
            }
        });

        if (addressBar.style.display === 'block' && addressBar.value.trim() === query) renderSuggestions(matches);
    });

    addressBar.addEventListener('keydown', (e) => {
        if (suggestionsList.style.display === 'none') return;

        if (e.key === 'ArrowDown') {
            e.preventDefault();
            if (state.addressSuggestions.length > 0) {
                state.addressSelectionIdx = (state.addressSelectionIdx + 1) % state.addressSuggestions.length;
                updateSuggestionHighlight();
            }
        } else if (e.key === 'ArrowUp') {
            e.preventDefault();
            if (state.addressSuggestions.length > 0) {
                state.addressSelectionIdx = (state.addressSelectionIdx - 1 + state.addressSuggestions.length) % state.addressSuggestions.length;
                updateSuggestionHighlight();
            }
        } else if (e.key === 'Tab') {
            e.preventDefault();
            const idx = state.addressSelectionIdx >= 0 ? state.addressSelectionIdx : 0;
            if (state.addressSuggestions[idx]) {
                state.addressSelectionIdx = idx;
                updateSuggestionHighlight();
            }
        }
    });
}

export function renderSuggestions(items) {
    const suggestionsList = document.getElementById('AddressSuggestions');
    suggestionsList.innerHTML = '';
    state.addressSuggestions = items;
    state.addressSelectionIdx = -1;

    if (items.length === 0) {
        hideSuggestions();
        return;
    }

    items.forEach((item) => {
        const li = document.createElement('li');
        li.className = 'suggestion-item';

        let typeClass = 'suggestion-type';
        if (item.type === 'Calculator') typeClass += ' calculator';
        else if (item.type === 'Bang') typeClass += ' bang';

        const titleHtml = item.type === 'Calculator'
            ? `<strong>${escapeHtml(item.title)}</strong>`
            : escapeHtml(item.title);

        li.innerHTML = `<span>${titleHtml}</span><span class="${typeClass}">${escapeHtml(item.type)}</span>`;
        
        li.addEventListener('click', () => {
            selectSuggestion(item);
        });

        suggestionsList.appendChild(li);
    });

    suggestionsList.style.display = 'block';
}

export function updateSuggestionHighlight() {
    const items = document.querySelectorAll('#AddressSuggestions .suggestion-item');
    items.forEach((item, idx) => {
        if (idx === state.addressSelectionIdx) {
            item.classList.add('selected');
            item.scrollIntoView({ block: 'nearest' });
            if (state.addressSuggestions[idx]) {
                document.getElementById('WideAddressBar').value = state.addressSuggestions[idx].value;
            }
        } else {
            item.classList.remove('selected');
        }
    });
}

export async function selectSuggestion(item) {
    if (!item) return;

    if (item.value.startsWith('ws:')) {
        const targetWs = item.value.slice('ws:'.length);
        state.sessionState.current_workspace = targetWs;
        window.flyAPI.saveSession(state.sessionState);
        renderWorkspaceUI(0);
        hideSuggestions();
        const addressBar = document.getElementById('WideAddressBar');
        if (addressBar) addressBar.style.display = 'none';
        return;
    }

    if (item.isCalc || item.type === 'Calculator') {
        if (navigator.clipboard && typeof navigator.clipboard.writeText === 'function') {
            navigator.clipboard.writeText(String(item.value)).catch(() => {});
        }
        setTargetUrl(`Calculator: Copied ${item.value} to clipboard`, 3500);
        const addressBar = document.getElementById('WideAddressBar');
        if (addressBar) {
            addressBar.value = String(item.value);
            addressBar.style.display = 'none';
        }
        hideSuggestions();
        focusActiveWebview();
        return;
    }

    if (item.forceSearch) {
        let searchTemplate = 'https://duckduckgo.com/?q=%s';
        if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
            try {
                const cfg = await window.flyAPI.getBrowserSettings();
                if (cfg && cfg.search_engine) {
                    searchTemplate = cfg.search_engine;
                }
            } catch (e) {}
        }
        const targetUrl = formatSearchUrl(item.value, searchTemplate);
        applyTargetUrl(targetUrl);
        return;
    }

    handleNavigation(item.value);
    hideSuggestions();
}

// Hides the bar and its suggestions without moving focus, for callers that already own it.
// Reopening replaces the typed text with the tab address.
export function dismissAddressBar() {
    const addressBar = document.getElementById('WideAddressBar');
    if (addressBar) addressBar.style.display = 'none';
    hideSuggestions();
}

export function hideSuggestions() {
    const suggestionsList = document.getElementById('AddressSuggestions');
    if (suggestionsList) suggestionsList.style.display = 'none';
    state.addressSuggestions = [];
    state.addressSelectionIdx = -1;
}
