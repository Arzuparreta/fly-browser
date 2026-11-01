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
