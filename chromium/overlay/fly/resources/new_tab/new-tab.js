// Both entry points open the same native surfaces as the browser shortcuts.
const keyNames = {ctrl:'Ctrl', alt:'Alt', shift:'Shift', meta:'Meta', escape:'Esc',
    arrowleft:'←', arrowright:'→', arrowup:'↑', arrowdown:'↓', pagedown:'Page Down', pageup:'Page Up'};
const keyLabel = key => keyNames[key.toLowerCase()] || (key.length === 1 ? key.toUpperCase() : key);
const chordKeys = binding => (binding || '').split('+').filter(Boolean).map(keyLabel);
const chordLabel = stroke => chordKeys(stroke).join(' + ');
// A NORMAL key is typed as shown: "F" is f, "Shift+H" is H.
const normalStroke = stroke => {
    const parts = stroke.split('+');
    const key = parts.at(-1);
    if (key.length !== 1) return chordLabel(stroke);
    if (parts.length === 1) return key.toLowerCase();
    if (parts.length === 2 && parts[0] === 'Shift') return key.toUpperCase();
    return chordLabel(stroke);
};
// Bindings may be sequences of strokes separated by spaces ("G G" is gg).
const sequence = (binding, format) => binding.split(' ').filter(Boolean).map(format).join(' ');
const fly = document.getElementById('memory-fly');
const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
let navigationLoading = false;
let motionTimer = 0;
// Four short movements, then 1.8 seconds at rest: a 2.4-second cycle.
// Only one timeout exists at a time, and none exists while idle or hidden.
const poses = [1, 2, 1, 2, 0];
function syncLoadingMotion() {
    clearTimeout(motionTimer);
    motionTimer = 0;
    delete fly.dataset.pose;
    if (!navigationLoading || document.hidden || reducedMotion.matches) return;
    let phase = 0;
    const step = () => {
        fly.dataset.pose = poses[phase];
        motionTimer = setTimeout(step, phase === 4 ? 1800 : 150);
        phase = (phase + 1) % poses.length;
    };
    // Avoid a flash of movement on fast navigations.
    motionTimer = setTimeout(step, 160);
}
window.updateFlyNewTabLoading = loading => {
    if (navigationLoading === loading) return;
    navigationLoading = loading;
    syncLoadingMotion();
};
document.addEventListener('visibilitychange', syncLoadingMotion);
reducedMotion.addEventListener('change', syncLoadingMotion);
window.addEventListener('pagehide', () => {
    navigationLoading = false;
    syncLoadingMotion();
});
let tipShown = false;
window.updateFlyNewTab = data => {
    document.body.classList.toggle('light', data.light);
    for (const [key, value] of Object.entries(data.colours))
        document.documentElement.style.setProperty(key, value);
    document.documentElement.style.setProperty('--scale', data.scale || 1);
    document.getElementById('memory-fly').src = `identity/memory-${data.light ? 'papel' : 'chasis'}@2x.png`;
    for (const [id, binding] of [['search', data.searchBinding], ['bookmarks', data.bookmarksBinding]]) {
        document.getElementById(`${id}-binding`).textContent = sequence(binding || '', chordLabel);
        const button = document.getElementById(id);
        if (binding) button.setAttribute('aria-keyshortcuts', (binding || '').replace(/\bCtrl\b/g, 'Control'));
        else button.removeAttribute('aria-keyshortcuts');
    }
    const tip = document.getElementById('tip');
    if (data.tip) {
        document.getElementById('tip-key').textContent = sequence(data.tip.key, data.tip.normal ? normalStroke : chordLabel);
        document.getElementById('tip-text').textContent = data.tip.text;
        tip.hidden = false;
        tip.classList.remove('gone');
        tipShown = true;
    } else if (tipShown) {
        // Keep the line's space so the fly does not move under the pointer.
        tip.classList.add('gone');
    } else {
        tip.hidden = true;
    }
};
document.getElementById('search').addEventListener('click', () => chrome.send('openSearch'));
document.getElementById('bookmarks').addEventListener('click', () => chrome.send('openBookmarks'));
document.getElementById('tip-dismiss').addEventListener('click', () => chrome.send('dismissTip'));
const initial = JSON.parse(document.getElementById('initial-data').textContent);
window.updateFlyNewTab(initial);
chrome.send('ready', [initial.tip ? initial.tip.action : '']);
