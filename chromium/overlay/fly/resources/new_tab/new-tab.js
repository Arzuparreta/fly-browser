// Fly's new tab page. The browser sends the theme, the lists and, once a
// second while the page shows, Fly's memory use (chrome.send is the WebUI
// bridge).
// Lists fill their allocated space. Extra entries remain reachable with page
// controls, so a large collection never adds a scrollbar or gets clipped.
const collections = new Map();
let layoutFrame;
function queueLayout() {
    if (!layoutFrame) layoutFrame = requestAnimationFrame(() => {
        layoutFrame = undefined;
        for (const state of collections.values()) layoutCollection(state);
    });
}
function collection(id, emptyId, name) {
    const list = document.getElementById(id);
    const paging = document.getElementById(`${id}-paging`);
    const previous = document.createElement('button');
    previous.type = 'button';
    previous.textContent = '←';
    previous.setAttribute('aria-label', `Previous ${name} page`);
    const status = document.createElement('span');
    status.setAttribute('aria-live', 'polite');
    const next = document.createElement('button');
    next.type = 'button';
    next.textContent = '→';
    next.setAttribute('aria-label', `Next ${name} page`);
    paging.append(previous, status, next);
    const state = {id, list, paging, previous, next, status,
        empty: document.getElementById(emptyId), items: [], page: 0, capacity: 0};
    previous.addEventListener('click', () => { state.page--; layoutCollection(state); });
    next.addEventListener('click', () => { state.page++; layoutCollection(state); });
    collections.set(id, state);
    return state;
}
const bookmarks = collection('bookmarks', 'bookmarks-empty', 'bookmarks');
const sites = collection('sites', 'empty', 'frequent sites');
const resize = new ResizeObserver(queueLayout);
for (const state of collections.values()) resize.observe(state.list);
function setItems(state, items) {
    const snapshot = JSON.stringify(items);
    if (state.snapshot !== snapshot) {
        state.snapshot = snapshot;
        state.items = items;
        state.rendered = undefined;
    }
    state.list.hidden = !items.length;
    state.empty.hidden = items.length > 0;
    if (!items.length) {
        state.list.replaceChildren();
        state.page = 0;
        state.paging.hidden = true;
    }
}
function layoutCollection(state) {
    if (!state.items.length || !state.list.getClientRects().length) return;
    const style = getComputedStyle(state.list);
    const gap = parseFloat(style.rowGap);
    const rowHeight = parseFloat(style.gridAutoRows);
    const columns = Math.max(1, Math.floor((state.list.clientWidth + gap) /
        (parseFloat(style.getPropertyValue('--entry-width')) + gap)));
    state.list.style.setProperty('--columns', columns);
    const rows = Math.max(1, Math.floor((state.list.clientHeight + gap) / (rowHeight + gap)));
    const capacity = rows * columns;
    const focused = state.list.contains(document.activeElement)
        ? document.activeElement.closest('[data-url]')?.dataset.url : undefined;
    if (capacity !== state.capacity) {
        const anchor = focused ? state.items.findIndex(item => item.url === focused)
            : state.page * state.capacity;
        state.page = Math.floor(Math.max(0, anchor) / capacity);
        state.capacity = capacity;
    }
    const pages = Math.ceil(state.items.length / capacity);
    state.page = Math.max(0, Math.min(state.page, pages - 1));
    state.paging.hidden = pages < 2;
    state.previous.disabled = state.page === 0;
    state.next.disabled = state.page === pages - 1;
    state.status.textContent = `${state.page + 1} of ${pages}`;
    const key = `${state.snapshot}:${state.page}:${capacity}`;
    if (state.rendered === key) return;
    state.rendered = key;
    const fragment = document.createDocumentFragment();
    for (const item of state.items.slice(state.page * capacity, (state.page + 1) * capacity)) {
        const link = document.createElement('a');
        link.href = item.url;
        link.dataset.url = item.url;
        link.title = `${item.title} — ${item.url}`;
        if (state.id === 'bookmarks') {
            const title = document.createElement('span');
            title.className = 'bookmark-title';
            title.textContent = item.title;
            link.append(title);
            fragment.append(link);
        } else {
            const row = document.createElement('div');
            row.className = 'site-row';
            link.textContent = item.title;
            const remove = document.createElement('button');
            remove.type = 'button';
            remove.textContent = '×';
            remove.dataset.hideSite = item.url;
            remove.dataset.url = item.url;
            remove.setAttribute('aria-label', `Remove ${item.title} from frequent sites`);
            remove.title = 'Remove from frequent sites';
            row.append(link, remove);
            fragment.append(row);
        }
    }
    state.list.replaceChildren(fragment);
    if (focused) {
        const target = [...state.list.querySelectorAll('[data-url]')]
            .find(element => element.dataset.url === focused);
        target?.focus({preventScroll: true});
    }
}
window.updateFlyNewTab = data => {
    document.body.classList.toggle('light', data.light);
    for (const [key, value] of Object.entries(data.colours)) document.documentElement.style.setProperty(key, value);
    document.documentElement.style.setProperty('--scale', data.scale || 1);
    document.documentElement.style.setProperty('--sidebar-overhang', `${data.sidebarOverhang || 0}px`);
    const image = document.getElementById('memory-fly');
    const artwork = `identity/memory-${data.light ? 'papel' : 'chasis'}@2x.png`;
    if (image.getAttribute('src') !== artwork) image.src = artwork;
    document.getElementById('saved-bookmarks').hidden = !data.showBookmarks;
    document.getElementById('frequent-sites').hidden = !data.showSites;
    document.querySelector('.navigation').hidden = !data.showBookmarks && !data.showSites;
    document.querySelector('main').classList.toggle('no-collections', !data.showBookmarks && !data.showSites);
    setItems(bookmarks, data.bookmarks || []);
    setItems(sites, data.sites || []);
    queueLayout();
};
window.updateFlyNewTabMemory = megabytes => {
    document.getElementById('memory').textContent = `${megabytes} MB`;
};
document.addEventListener('click', event => {
    const button = event.target.closest?.('button[data-hide-site]');
    if (button) chrome.send('hideSite', [button.dataset.hideSite]);
});
let clockTimer;
function updateClock() {
    clearTimeout(clockTimer);
    if (document.hidden) return;
    const now = new Date();
    document.getElementById('clock').textContent = now.toLocaleTimeString('en-GB', {hour:'2-digit', minute:'2-digit'});
    document.getElementById('date').textContent = now.toLocaleDateString('en-GB', {weekday:'long', day:'numeric', month:'long', year:'numeric'});
    clockTimer = setTimeout(updateClock, 60000 - Date.now() % 60000);
}
document.addEventListener('visibilitychange', updateClock);
updateClock();
window.updateFlyNewTab(JSON.parse(document.getElementById('initial-data').textContent));
// Lay out the initial snapshot in this task, before the first content paint.
for (const state of collections.values()) layoutCollection(state);
chrome.send('ready');
