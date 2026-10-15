const fs = require('node:fs');
const path = require('node:path');

function hostname(url) {
    try {
        const parsed = new URL(url);
        return /^https?:$/.test(parsed.protocol) ? parsed.hostname : '';
    } catch { return ''; }
}

function createSiteZoom(file, { isPrivate = () => false } = {}) {
    const privateLevels = Object.create(null);
    let levels = Object.create(null);
    try {
        const stored = JSON.parse(fs.readFileSync(file, 'utf8'));
        for (const [host, level] of Object.entries(stored)) {
            if (typeof level === 'number' && Number.isFinite(level) && level >= -5 && level <= 5 && level !== 0) levels[host] = level;
        }
    } catch (error) {
        if (error.code !== 'ENOENT') console.warn('Could not read site zoom preferences:', error.message);
    }
    const guests = new Set();
    const preferences = guest => isPrivate(guest) ? privateLevels : levels;
    const apply = guest => guest.setZoomLevel(preferences(guest)[hostname(guest.getURL())] || 0);
    function change(guest, action) {
        if (!['zoom-in', 'zoom-out', 'reset-zoom'].includes(action)) return;
        const host = hostname(guest.getURL());
        const level = action === 'reset-zoom' ? 0 : Math.max(-5, Math.min(5, guest.getZoomLevel() + (action === 'zoom-in' ? 0.5 : -0.5)));
        if (!host) { guest.setZoomLevel(level); return; }
        const privateGuest = isPrivate(guest);
        const next = privateGuest ? privateLevels : Object.assign(Object.create(null), levels);
        if (level === 0) delete next[host];
        else next[host] = level;
        if (!privateGuest) {
            try {
                fs.mkdirSync(path.dirname(file), { recursive: true });
                const temporary = `${file}.${process.pid}.tmp`;
                fs.writeFileSync(temporary, JSON.stringify(next, null, 2));
                fs.renameSync(temporary, file);
            } catch (error) {
                console.warn('Could not save site zoom preferences:', error.message);
            }
            levels = next;
        }
        for (const target of guests) {
            if (!target.isDestroyed() && isPrivate(target) === privateGuest && hostname(target.getURL()) === host) apply(target);
        }
    }
    function attach(guest) {
        guests.add(guest);
        // Manual mode prevents Chromium's per-origin zoom from competing with
        // Firefox-style hostname preferences (shared across schemes and ports).
        guest.setZoomMode('manual');
        guest.on('did-navigate', () => apply(guest));
        guest.on('dom-ready', () => apply(guest));
        guest.on('zoom-changed', (_event, direction) => change(guest, direction === 'in' ? 'zoom-in' : 'zoom-out'));
        guest.once('destroyed', () => guests.delete(guest));
    }
    return { attach, change };
}

module.exports = { createSiteZoom, hostname };
