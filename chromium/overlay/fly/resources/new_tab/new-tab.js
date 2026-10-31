// Both entry points open the same native surfaces as the browser shortcuts.
window.updateFlyNewTab = data => {
    document.body.classList.toggle('light', data.light);
    for (const [key, value] of Object.entries(data.colours))
        document.documentElement.style.setProperty(key, value);
    document.documentElement.style.setProperty('--scale', data.scale || 1);
    document.documentElement.style.setProperty('--sidebar-overhang', `${data.sidebarOverhang || 0}px`);
    document.getElementById('memory-fly').src = `identity/memory-${data.light ? 'papel' : 'chasis'}@2x.png`;
    for (const [id, binding] of [['search', data.searchBinding], ['bookmarks', data.bookmarksBinding]]) {
        const keys = (binding || '').split('+').filter(Boolean).map(key => ({
            ctrl:'Ctrl', alt:'Alt', shift:'Shift', meta:'Meta'
        })[key.toLowerCase()] || key.toUpperCase());
        document.getElementById(`${id}-binding`).textContent = keys.join(' + ');
        const button = document.getElementById(id);
        if (keys.length) button.setAttribute('aria-keyshortcuts', keys.map(key => key === 'Ctrl' ? 'Control' : key).join('+'));
        else button.removeAttribute('aria-keyshortcuts');
    }
};
document.getElementById('search').addEventListener('click', () => chrome.send('openSearch'));
document.getElementById('bookmarks').addEventListener('click', () => chrome.send('openBookmarks'));
window.updateFlyNewTab(JSON.parse(document.getElementById('initial-data').textContent));
chrome.send('ready');
