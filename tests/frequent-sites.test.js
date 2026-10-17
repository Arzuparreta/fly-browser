const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {createFrequentSites} = require('../frequentSites');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-sites-'));
try {
    const file = path.join(directory, 'hidden.json');
    const now = 1800000000000;
    let history = [
        ...Array.from({length: 10}, () => ({url:'https://old.test/path', timestamp:now - 90 * 86400000})),
        {url:'https://recent.test/a', timestamp:now},
        {url:'https://recent.test/b', timestamp:now},
        {url:'https://other.test', timestamp:now},
        {url:'file:///private'}, {url:'invalid'}, null
    ];
    const sites = createFrequentSites(file, () => history, () => now);
    assert.deepEqual(sites.get().map(site => site.url), ['https://recent.test', 'https://other.test', 'https://old.test']);
    assert.equal(sites.get(), sites.get(), 'repeated reads reuse the derived cache');
    assert.equal(sites.hide('javascript:alert(1)'), false);
    assert.equal(sites.hide('https://recent.test/path'), true);
    assert.equal(history.length, 16, 'hiding keeps history intact');
    assert(!sites.get().some(site => site.url === 'https://recent.test'));
    assert(!createFrequentSites(file, () => history, () => now).get().some(site => site.url === 'https://recent.test'));
    history.push({url:'https://new.test', timestamp:now});
    sites.invalidate();
    assert(sites.get().some(site => site.url === 'https://new.test'));
    history = [];
    assert.deepEqual(sites.get(), []);
    history = Array.from({length:20}, (_, index) => ({url:`https://site${index}.test`, timestamp:now}));
    assert.equal(sites.get().length, 5);
    console.log('Frequent sites: ranking, cache, persistence, filtering, removal and purge pass.');
} finally { fs.rmSync(directory, {recursive:true, force:true}); }
