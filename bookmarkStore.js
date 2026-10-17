const fs = require('node:fs');
const path = require('node:path');

exports.createBookmarkStore = function (file, onChanged = () => {}) {
    let bookmarks;
    let origins = new Set();
    function updateOrigins(data) {
        const next = new Set();
        for (const bookmark of data) {
            try {
                const url = new URL(bookmark.url);
                if (['http:', 'https:'].includes(url.protocol)) next.add(url.origin);
            } catch { /* Ignore unsupported or malformed bookmarks. */ }
        }
        if (next.size !== origins.size || [...next].some(origin => !origins.has(origin))) origins = next;
    }
    function read() {
        if (bookmarks === undefined) {
            try { bookmarks = JSON.parse(fs.readFileSync(file, 'utf8')); } catch { bookmarks = []; }
            if (!Array.isArray(bookmarks)) bookmarks = [];
            updateOrigins(bookmarks);
        }
        return bookmarks;
    }
    return {
        read,
        origins() { read(); return origins; },
        save(data) {
            if (!Array.isArray(data)) return false;
            try {
                fs.mkdirSync(path.dirname(file), {recursive:true});
                fs.writeFileSync(file + '.tmp', JSON.stringify(data, null, 4), 'utf8');
                fs.renameSync(file + '.tmp', file);
            } catch { return false; }
            bookmarks = data;
            updateOrigins(data);
            onChanged(data);
            return true;
        }
    };
};
