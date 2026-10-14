import { state } from '../state.js';
import { escapeHtml } from '../utils.js';

export const DEFAULT_SEARCH_ENGINES = {
    duckduckgo: 'https://duckduckgo.com/?q=%s',
    google: 'https://www.google.com/search?q=%s',
    startpage: 'https://www.startpage.com/sp/search?query=%s',
    brave: 'https://search.brave.com/search?q=%s',
    kagi: 'https://kagi.com/search?q=%s'
};

export const DEFAULT_SEARCH_ALIASES = {
    'g': { name: 'Google', url: 'https://www.google.com/search?q=%s' },
    'ddg': { name: 'DuckDuckGo', url: 'https://duckduckgo.com/?q=%s' },
    'yt': { name: 'YouTube', url: 'https://www.youtube.com/results?search_query=%s' },
    'gh': { name: 'GitHub', url: 'https://github.com/search?q=%s' },
    'a': { name: 'ArchWiki', url: 'https://wiki.archlinux.org/index.php?search=%s' },
    'aur': { name: 'Arch User Repository', url: 'https://aur.archlinux.org/packages?K=%s' },
    'pkg': { name: 'Arch Packages', url: 'https://archlinux.org/packages/?q=%s' },
    'so': { name: 'Stack Overflow', url: 'https://stackoverflow.com/search?q=%s' },
    'r': { name: 'Reddit', url: 'https://www.reddit.com/search/?q=%s' },
    'sp': { name: 'Startpage', url: 'https://www.startpage.com/sp/search?query=%s' },
    'b': { name: 'Brave Search', url: 'https://search.brave.com/search?q=%s' },
    'k': { name: 'Kagi', url: 'https://kagi.com/search?q=%s' }
};

export function getEffectiveSearchAliases(customAliases = {}) {
    const effective = {};
    for (const [key, val] of Object.entries(DEFAULT_SEARCH_ALIASES)) {
        effective[key.toLowerCase()] = typeof val === 'string' ? { name: key.toUpperCase(), url: val } : { ...val };
    }
    if (customAliases && typeof customAliases === 'object') {
        for (const [key, val] of Object.entries(customAliases)) {
            const cleanKey = key.trim().toLowerCase();
            if (!cleanKey) continue;
            if (typeof val === 'string') {
                effective[cleanKey] = { name: cleanKey.toUpperCase(), url: val.trim() };
            } else if (val && typeof val === 'object' && val.url) {
                effective[cleanKey] = { name: val.name || cleanKey.toUpperCase(), url: val.url.trim() };
            }
        }
    }
    return effective;
}

export function resolveSearchAliasUrl(alias, query, customAliases = {}) {
    if (!alias || !query) return null;
    const effective = getEffectiveSearchAliases(customAliases);
    const entry = effective[alias.trim().toLowerCase()];
    if (!entry || !entry.url) return null;

    const encoded = encodeURIComponent(query.trim());
    if (entry.url.includes('%s')) {
        return entry.url.replace('%s', encoded);
    }
    return entry.url + encoded;
}

export function renderSearchAliasesUI() {
    if (typeof document === 'undefined') return;
    const container = document.getElementById('SearchAliasesList');
    if (!container) return;

    container.innerHTML = '';
    const customEntries = Object.entries(state.customSearchAliases || {});

    if (customEntries.length === 0) {
        const emptyHint = document.createElement('div');
        emptyHint.className = 'search-alias-empty-hint';
        emptyHint.textContent = 'No custom aliases configured. Built-in defaults (g, ddg, yt, gh, a, aur, pkg, so, r, sp, b, k) are active.';
        container.appendChild(emptyHint);
        return;
    }

    customEntries.sort(([a], [b]) => a.localeCompare(b)).forEach(([key, val]) => {
        const urlStr = typeof val === 'string' ? val : (val?.url || '');
        const item = document.createElement('div');
        item.className = 'search-alias-item';

        const badge = document.createElement('span');
        badge.className = 'search-alias-badge';
        badge.textContent = key;

        const urlSpan = document.createElement('span');
        urlSpan.className = 'search-alias-url';
        urlSpan.textContent = urlStr;
        urlSpan.title = urlStr;

        const delBtn = document.createElement('button');
        delBtn.type = 'button';
        delBtn.className = 'search-alias-delete-btn';
        delBtn.title = `Delete alias '${key}'`;
        delBtn.innerHTML = '<i class="fa-solid fa-trash-can"></i>';
        delBtn.addEventListener('click', async (e) => {
            e.preventDefault();
            e.stopPropagation();
            if (state.customSearchAliases && state.customSearchAliases[key]) {
                delete state.customSearchAliases[key];
                renderSearchAliasesUI();
                if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
                    try {
                        const currentCfg = await window.flyAPI.getBrowserSettings();
                        currentCfg.custom_search_aliases = { ...state.customSearchAliases };
                        await window.flyAPI.updateBrowserSettings(currentCfg);
                        if (typeof window.setTargetUrl === 'function') {
                            window.setTargetUrl(`Removed search alias: ${key}`, 2500);
                        }
                    } catch (err) {
                        console.error('Failed to save search aliases:', err);
                    }
                }
            }
        });

        item.appendChild(badge);
        item.appendChild(urlSpan);
        item.appendChild(delBtn);
        container.appendChild(item);
    });
}

export async function initSearchEnginePreference() {
    if (typeof document === 'undefined') return;
    const select = document.getElementById('SearchEngineSelect');
    const addBtn = document.getElementById('add-search-alias-btn');
    const keyInput = document.getElementById('new-alias-key');
    const urlInput = document.getElementById('new-alias-url');

    try {
        if (window.flyAPI && typeof window.flyAPI.getBrowserSettings === 'function') {
            const cfg = await window.flyAPI.getBrowserSettings();
            if (cfg) {
                if (select && cfg.search_engine) {
                    select.value = cfg.search_engine;
                }
                if (cfg.custom_search_aliases && typeof cfg.custom_search_aliases === 'object') {
                    state.customSearchAliases = { ...cfg.custom_search_aliases };
                }
            }
        }
    } catch (err) {
        if (select) select.value = DEFAULT_SEARCH_ENGINES.duckduckgo;
    }

    renderSearchAliasesUI();

    if (select) {
        select.addEventListener('change', async (e) => {
            try {
                const currentCfg = await window.flyAPI.getBrowserSettings();
                currentCfg.search_engine = e.target.value;
                await window.flyAPI.updateBrowserSettings(currentCfg);
            } catch (err) {
                console.error('Failed to update search engine preference:', err);
            }
        });
    }

    const handleAddAlias = async () => {
        if (!keyInput || !urlInput) return;
        const key = keyInput.value.trim().toLowerCase();
        let url = urlInput.value.trim();

        if (!key) {
            if (typeof window.setTargetUrl === 'function') {
                window.setTargetUrl('Please enter a short alias (e.g. aur or nix)', 2500);
            }
            keyInput.focus();
            return;
        }

        if (!url) {
            if (typeof window.setTargetUrl === 'function') {
                window.setTargetUrl('Please enter a search URL template with %s', 2500);
            }
            urlInput.focus();
            return;
        }

        if (!url.startsWith('http://') && !url.startsWith('https://')) {
            url = 'https://' + url;
        }

        if (!state.customSearchAliases) {
            state.customSearchAliases = {};
        }

        state.customSearchAliases[key] = url;
        renderSearchAliasesUI();

        keyInput.value = '';
        urlInput.value = '';

        if (window.flyAPI && typeof window.flyAPI.updateBrowserSettings === 'function') {
            try {
                const currentCfg = await window.flyAPI.getBrowserSettings();
                currentCfg.custom_search_aliases = { ...state.customSearchAliases };
                await window.flyAPI.updateBrowserSettings(currentCfg);
                if (typeof window.setTargetUrl === 'function') {
                    window.setTargetUrl(`Added search alias: ${key} -> ${url}`, 3000);
                }
            } catch (err) {
                console.error('Failed to save search aliases:', err);
            }
        }
    };

    if (addBtn) {
        addBtn.addEventListener('click', (e) => {
            e.preventDefault();
            handleAddAlias();
        });
    }

    if (keyInput) {
        keyInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                if (urlInput) urlInput.focus();
            }
        });
    }

    if (urlInput) {
        urlInput.addEventListener('keydown', (e) => {
            if (e.key === 'Enter') {
                e.preventDefault();
                handleAddAlias();
            }
        });
    }
}

export function formatSearchUrl(query, template) {
    const engineTemplate = template || DEFAULT_SEARCH_ENGINES.duckduckgo;
    return engineTemplate.replace('%s', encodeURIComponent(query));
}
