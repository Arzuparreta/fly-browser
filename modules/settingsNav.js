import { keyboardMatchCount, filterKeyboardActions } from './keyboardSettings.js';

// Preferences index: one visible pane at a time, so hidden sections cost no layout.
// The last pane stays selected for the session; nothing is persisted.
let current = 'general';
let onShow = () => {};
let query = '';

const el = id => document.getElementById(id);
const tabs = () => [...document.querySelectorAll('#SettingsIndex [role="tab"]')];
const paneOf = tab => el(tab.getAttribute('aria-controls'));
const SEARCH_ITEMS = '.setting-item, .alias-card, .ext-card';
const KEYBOARD_TERMS = 'keyboard shortcuts bindings keybinds keys hotkeys';

export const currentSettingsPane = () => current;

export function showSettingsPane(name, {focusTab = false} = {}) {
    if (query) clearSettingsSearch();
    const list = tabs();
    const target = list.find(tab => tab.dataset.pane === name) || list[0];
    if (!target) return;
    current = target.dataset.pane;
    for (const tab of list) {
        const selected = tab === target;
        tab.setAttribute('aria-selected', String(selected));
        tab.tabIndex = selected ? 0 : -1;
        paneOf(tab).hidden = !selected;
    }
    el('SettingsContent').scrollTop = 0;
    if (focusTab) target.focus();
    onShow(current);
}

export function focusSettingsSearch() {
    const input = el('SettingsSearch');
    input.focus();
    input.select();
}

export function clearSettingsSearch() {
    const input = el('SettingsSearch');
    if (!input.value && !query) return;
    input.value = '';
    applySearch('');
}

// Highlights matched words without touching the DOM, so controls keep their state.
function highlight(items, terms) {
    if (!window.Highlight || !CSS.highlights) return;
    const ranges = [];
    for (const item of items) {
        const root = item.querySelector('.setting-info') || item;
        const walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
        for (let node = walker.nextNode(); node; node = walker.nextNode()) {
            const text = node.data.toLowerCase();
            for (const term of terms) {
                for (let at = text.indexOf(term); at >= 0; at = text.indexOf(term, at + term.length)) {
                    const range = new Range();
                    range.setStart(node, at); range.setEnd(node, at + term.length);
                    ranges.push(range);
                }
            }
        }
    }
    if (ranges.length) CSS.highlights.set('settings-search', new Highlight(...ranges));
    else CSS.highlights.delete('settings-search');
}

// Filters every section in place: matching rows stay live and editable where they are.
function applySearch(raw) {
    query = raw.trim().toLowerCase();
    const terms = query.split(/\s+/).filter(Boolean);
    const content = el('SettingsContent');
    content.classList.toggle('searching', terms.length > 0);
    el('SettingsIndex').classList.toggle('searching', terms.length > 0);
    if (!terms.length) {
        for (const node of content.querySelectorAll('.settings-group[hidden], .setting-item[hidden], .alias-card[hidden], .ext-card[hidden]')) node.hidden = false;
        for (const tab of tabs()) { tab.classList.remove('no-match'); delete tab.dataset.count; paneOf(tab).hidden = tab.dataset.pane !== current; }
        el('SettingsSearchStatus').textContent = '';
        highlight([], terms);
        return;
    }
    const found = [];
    let total = 0;
    for (const tab of tabs()) {
        const pane = paneOf(tab);
        const title = pane.querySelector('.settings-pane-header h2')?.textContent || '';
        let matches = 0;
        if (tab.dataset.pane === 'keyboard') {
            const all = terms.every(term => KEYBOARD_TERMS.includes(term));
            const count = keyboardMatchCount(query);
            matches = count || (all ? 1 : 0);
            pane.dataset.searchQuery = count ? query : '';
            el('SettingsKeyboardMatches').textContent = count ? `${count} matching ${count === 1 ? 'action' : 'actions'}` : 'All keyboard actions';
        } else {
            for (const group of pane.querySelectorAll('.settings-group')) {
                const heading = `${title} ${group.querySelector(':scope > h3')?.textContent || ''}`;
                const items = [...group.querySelectorAll(SEARCH_ITEMS)];
                let hits = 0;
                for (const item of items) {
                    const text = `${heading} ${item.textContent} ${item.dataset.keywords || ''}`.toLowerCase();
                    item.hidden = !terms.every(term => text.includes(term));
                    if (!item.hidden) { hits++; found.push(item); }
                }
                // Groups without rows, such as live extension usage, match on their headings.
                if (!items.length) {
                    const text = `${heading} ${group.querySelector('.settings-group-desc')?.textContent || ''}`.toLowerCase();
                    hits = terms.every(term => text.includes(term)) ? 1 : 0;
                }
                group.hidden = !hits;
                matches += hits;
            }
        }
        pane.hidden = !matches;
        tab.classList.toggle('no-match', !matches);
        if (matches) tab.dataset.count = tab.dataset.pane === 'keyboard' ? (keyboardMatchCount(query) || '') : matches; else delete tab.dataset.count;
        total += matches;
    }
    el('SettingsSearchStatus').textContent = total ? `Results for “${raw.trim()}”` : `No settings match “${raw.trim()}”.`;
    content.scrollTop = 0;
    highlight(found, terms);
}

export function initialiseSettingsNav(handlePaneShown) {
    const index = el('SettingsIndex');
    if (!index) return;
    showSettingsPane(current);
    onShow = handlePaneShown || onShow;
    index.addEventListener('click', event => {
        const tab = event.target.closest('[role="tab"]');
        if (tab && (tab.dataset.pane !== current || query)) showSettingsPane(tab.dataset.pane);
    });
    // Vertical tab list keys: arrows move and select, Home/End jump to the ends.
    index.addEventListener('keydown', event => {
        const list = tabs();
        const position = list.indexOf(document.activeElement);
        if (position < 0 || event.altKey || event.ctrlKey || event.metaKey) return;
        const next = {ArrowDown: position + 1, ArrowUp: position - 1, Home: 0, End: list.length - 1}[event.key];
        if (next === undefined) return;
        event.preventDefault();
        showSettingsPane(list[(next + list.length) % list.length].dataset.pane, {focusTab: true});
    });

    const input = el('SettingsSearch');
    input.addEventListener('input', () => applySearch(input.value));
    input.addEventListener('keydown', event => {
        if (event.key === 'Escape' && input.value) {
            // Clearing comes before closing Preferences.
            event.preventDefault(); event.stopPropagation();
            clearSettingsSearch();
        } else if (event.key === 'Enter' && query) {
            event.preventDefault();
            const first = [...el('SettingsContent').querySelectorAll('.settings-pane:not([hidden]) :is(button, input, select, textarea)')]
                .find(control => !control.disabled && control.getClientRects().length);
            first?.focus();
        }
    });
    el('PreferencesOverlay').addEventListener('keydown', event => {
        if (event.key !== '/' || event.ctrlKey || event.altKey || event.metaKey || event.target.closest('input, textarea, select, [contenteditable]')) return;
        event.preventDefault();
        focusSettingsSearch();
    });
    el('SettingsKeyboardShow').addEventListener('click', () => {
        const filter = el('SettingsPane-keyboard').dataset.searchQuery || '';
        showSettingsPane('keyboard');
        filterKeyboardActions(filter);
        el('KeyboardSearch').focus();
    });
}
