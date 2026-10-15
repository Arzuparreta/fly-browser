const assert = require('node:assert/strict');
const { EventEmitter } = require('node:events');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { createSiteZoom, hostname } = require('../siteZoom');
const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-zoom-'));
const file = path.join(directory, 'zoom.json');
function guest(store, url) {
    const view = new EventEmitter();
    Object.assign(view, { url, level: 0, getURL() { return this.url; }, getZoomLevel() { return this.level; }, setZoomLevel(level) { this.level = level; }, setZoomMode() {}, isDestroyed() { return false; } });
    store.attach(view);
    view.emit('did-navigate');
    return view;
}
try {
    const store = createSiteZoom(file);
    const first = guest(store, 'https://example.com/a');
    const second = guest(store, 'http://example.com:8080/b');
    const subdomain = guest(store, 'https://sub.example.com/');
    store.change(first, 'zoom-in');
    assert.equal(first.level, 0.5);
    assert.equal(second.level, 0.5);
    assert.equal(subdomain.level, 0);
    assert.equal(guest(createSiteZoom(file), 'https://example.com/c').level, 0.5);
    first.url = 'https://other.example/'; first.emit('did-navigate');
    assert.equal(first.level, 0);
    first.url = 'https://example.com/back'; first.emit('did-navigate');
    assert.equal(first.level, 0.5);
    first.emit('zoom-changed', {}, 'in');
    assert.equal(second.level, 1);
    store.change(first, 'reset-zoom');
    assert.equal(second.level, 0);
    assert.deepEqual(JSON.parse(fs.readFileSync(file)), {});
    for (let i = 0; i < 30; i++) store.change(first, 'zoom-out');
    assert.equal(first.level, -5);
    assert.equal(hostname('file:///tmp/private.html'), '');
    assert.equal(hostname('https://EXAMPLE.COM:123/path'), 'example.com');
    fs.writeFileSync(file, '{');
    assert.equal(guest(createSiteZoom(file), 'https://example.com').level, 0);
    console.log('Site zoom tests passed');
} finally { fs.rmSync(directory, { recursive: true, force: true }); }
