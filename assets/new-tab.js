let bookmarkSnapshot;
window.updateFlyNewTab = data => {
    document.body.classList.toggle('light', data.light);
    for (const [key, value] of Object.entries(data.colours)) document.documentElement.style.setProperty(key, value);
    document.getElementById('memory-fly').src = `identity/memory-${data.light ? 'papel' : 'chasis'}@2x.png`;
    document.getElementById('memory').textContent = `${data.memory} MB`;
    document.getElementById('saved-bookmarks').hidden = !data.showBookmarks;
    document.getElementById('frequent-sites').hidden = !data.showSites;
    window.updateFlyNewTabLists(data);
};
window.updateFlyNewTabLists = data => {
    const bookmarkData = JSON.stringify(data.bookmarks || []);
    if (bookmarkSnapshot !== bookmarkData) {
        bookmarkSnapshot = bookmarkData;
        const bookmarks = document.getElementById('bookmarks');
        bookmarks.replaceChildren();
        for (const bookmark of data.bookmarks || []) {
            const link = document.createElement('a');
            link.href = bookmark.url;
            link.title = `${bookmark.title} — ${bookmark.url}`;
            const title = document.createElement('span');
            title.className = 'bookmark-title';
            title.textContent = bookmark.title;
            link.append(title);
            bookmarks.append(link);
        }
    }
    document.getElementById('bookmarks-empty').hidden = (data.bookmarks || []).length > 0;
    const sites = document.getElementById('sites');
    sites.replaceChildren();
    for (const site of data.sites) {
        const row = document.createElement('div');
        row.className = 'site-row';
        const link = document.createElement('a');
        link.href = site.url;
        link.textContent = site.title;
        const remove = document.createElement('button');
        remove.type = 'button';
        remove.textContent = '×';
        remove.dataset.hideSite = site.url;
        remove.setAttribute('aria-label', `Remove ${site.title} from frequent sites`);
        remove.title = 'Remove from frequent sites';
        row.append(link, remove);
        sites.append(row);
    }
    document.getElementById('empty').hidden = data.sites.length > 0;
};
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
