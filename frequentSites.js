const fs = require('node:fs');

// Derived from the bounded history cache; no extra visit log or background timer.
exports.createFrequentSites = function (hiddenPath, getHistory, now = Date.now) {
    let hidden;
    let cached;
    let cachedHistory;
    let cachedDay;
    function loadHidden() {
        if (hidden) return hidden;
        try {
            const stored = JSON.parse(fs.readFileSync(hiddenPath, 'utf8'));
            hidden = new Set(Array.isArray(stored) ? stored.filter(value => typeof value === 'string') : []);
        } catch { hidden = new Set(); }
        return hidden;
    }
    return {
        invalidate() { cached = null; },
        get() {
            const history = getHistory();
            const day = Math.floor(now() / 86400000);
            if (cached && cachedHistory === history && cachedDay === day) return cached;
            const sites = new Map();
            for (const entry of history) {
                try {
                    const url = new URL(entry.url);
                    if (!['https:', 'http:'].includes(url.protocol) || loadHidden().has(url.origin)) continue;
                    const timestamp = Number.isFinite(entry.timestamp) ? entry.timestamp : 0;
                    const site = sites.get(url.origin) || { url: url.origin, title: url.host, score: 0, lastVisit: 0 };
                    // Frequency with a seven-day half-life; future timestamps cannot inflate rank.
                    site.score += 2 ** (-Math.max(0, day - Math.floor(timestamp / 86400000)) / 7);
                    site.lastVisit = Math.max(site.lastVisit, timestamp);
                    sites.set(url.origin, site);
                } catch { /* Ignore malformed historic entries. */ }
            }
            cachedHistory = history;
            cachedDay = day;
            cached = [...sites.values()].sort((a, b) => b.score - a.score || b.lastVisit - a.lastVisit || a.url.localeCompare(b.url))
                .slice(0, 5).map(({url, title}) => ({url, title}));
            return cached;
        },
        hide(value) {
            try {
                const url = new URL(value);
                if (!['https:', 'http:'].includes(url.protocol)) return false;
                const next = new Set(loadHidden());
                next.add(url.origin);
                fs.mkdirSync(require('node:path').dirname(hiddenPath), {recursive:true});
                const temporary = hiddenPath + '.tmp';
                fs.writeFileSync(temporary, JSON.stringify([...next]), 'utf8');
                fs.renameSync(temporary, hiddenPath);
                hidden = next;
                cached = null;
                return true;
            } catch { return false; }
        }
    };
};
