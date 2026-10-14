// Preferences index: one visible pane at a time, so hidden sections cost no layout.
// The last pane stays selected for the session; nothing is persisted.
let current = 'general';
let onShow = () => {};

const tabs = () => [...document.querySelectorAll('#SettingsIndex [role="tab"]')];

export const currentSettingsPane = () => current;

export function showSettingsPane(name, {focusTab = false} = {}) {
    const list = tabs();
    const target = list.find(tab => tab.dataset.pane === name) || list[0];
    if (!target) return;
    current = target.dataset.pane;
    for (const tab of list) {
        const selected = tab === target;
        tab.setAttribute('aria-selected', String(selected));
        tab.tabIndex = selected ? 0 : -1;
        document.getElementById(tab.getAttribute('aria-controls')).hidden = !selected;
    }
    document.getElementById('SettingsContent').scrollTop = 0;
    if (focusTab) target.focus();
    onShow(current);
}

export function initialiseSettingsNav(handlePaneShown) {
    const index = document.getElementById('SettingsIndex');
    if (!index) return;
    showSettingsPane(current);
    onShow = handlePaneShown || onShow;
    index.addEventListener('click', event => {
        const tab = event.target.closest('[role="tab"]');
        if (tab && tab.dataset.pane !== current) showSettingsPane(tab.dataset.pane);
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
}
