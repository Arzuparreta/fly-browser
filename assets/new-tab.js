let searchTemplate = 'https://duckduckgo.com/?q=%s';
window.updateFlyNewTab = data => {
    document.body.classList.toggle('light', data.light);
    for (const [key, value] of Object.entries(data.colours)) document.documentElement.style.setProperty(key, value);
    document.getElementById('memory-fly').src = `identity/memory-${data.light ? 'papel' : 'chasis'}@2x.png`;
    document.getElementById('memory').textContent = `${data.memory} MB`;
    document.getElementById('landed').textContent = String(data.landed);
    searchTemplate = data.search;
    document.getElementById('address-hint').textContent = data.addressHint;
    document.getElementById('links-hint').textContent = data.linksHint;
    const sites = document.getElementById('sites');
    sites.replaceChildren();
    for (const site of data.sites) {
        const link = document.createElement('a');
        link.href = site.url;
        link.textContent = site.title;
        sites.append(link);
    }
    document.getElementById('empty').hidden = data.sites.length > 0;
};
document.getElementById('search-form').addEventListener('submit', event => {
    event.preventDefault();
    const query = document.getElementById('search').value.trim();
    if (!query) return;
    let destination;
    if (/^https?:\/\//i.test(query)) destination = query;
    else if (/^[^\s/]+\.[^\s]+$/.test(query)) destination = `https://${query}`;
    else destination = searchTemplate.includes('%s') ? searchTemplate.replace(/%s/g, encodeURIComponent(query)) : searchTemplate + encodeURIComponent(query);
    try {
        const url = new URL(destination);
        if (['https:', 'http:'].includes(url.protocol)) location.href = url.href;
    } catch {}
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
