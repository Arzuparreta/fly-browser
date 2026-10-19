'use strict';
// Synthetic loopback-only data for native/control acceptance. No web API shims.
const http = require('node:http');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

function wave() {
    const frames = 24000;
    const bytes = Buffer.alloc(44 + frames * 2);
    bytes.write('RIFF'); bytes.writeUInt32LE(bytes.length - 8, 4); bytes.write('WAVEfmt ', 8);
    bytes.writeUInt32LE(16, 16); bytes.writeUInt16LE(1, 20); bytes.writeUInt16LE(1, 22);
    bytes.writeUInt32LE(8000, 24); bytes.writeUInt32LE(16000, 28);
    bytes.writeUInt16LE(2, 32); bytes.writeUInt16LE(16, 34);
    bytes.write('data', 36); bytes.writeUInt32LE(frames * 2, 40);
    for (let n = 0; n < frames; n++) bytes.writeInt16LE(Math.round(256 * Math.sin(n * Math.PI * 440 / 4000)), 44 + n * 2);
    return bytes;
}

const page = `<!doctype html><html lang="en"><meta charset="utf-8">
<title>Fly native acceptance fixture</title>
<h1>Native visibility and media</h1>
<audio id="media" controls loop src="/media.wav"></audio>
<button id="play">Play fixture</button><button id="pause">Pause fixture</button>
<input id="editable" aria-label="Editable input">
<div contenteditable="true" aria-label="Editable content">Editable</div>
<iframe title="Editable frame" src="/frame"></iframe>
<button id="storage">Create synthetic workspace state</button>
<pre id="state"></pre>
<script src="/fixture.js"></script></html>`;

const script = `(() => {
    const media = document.getElementById('media');
    document.getElementById('play').onclick = () => media.play();
    document.getElementById('pause').onclick = () => media.pause();
    const events = [];
    const snapshot = () => ({hidden: document.hidden, visibility: document.visibilityState,
        focused: document.hasFocus(), paused: media.paused, muted: media.muted,
        time: media.currentTime, readyState: media.readyState});
    const record = event => {
        events.push({type: event.type, ...snapshot()});
        if (events.length > 200) events.shift();
        document.getElementById('state').textContent = JSON.stringify(snapshot());
    };
    for (const name of ['visibilitychange', 'focus', 'blur', 'pageshow', 'pagehide'])
        (name === 'visibilitychange' ? document : window).addEventListener(name, record);
    for (const name of ['play', 'pause', 'volumechange', 'loadeddata']) media.addEventListener(name, record);
    document.getElementById('storage').onclick = async () => {
        let identity = localStorage.getItem('fly-fixture-identity');
        if (!identity) {
            identity = crypto.randomUUID();
            localStorage.setItem('fly-fixture-identity', identity);
        }
        document.cookie = 'fly_fixture=' + identity + '; SameSite=Strict; Path=/';
        const db = indexedDB.open('fly-fixture', 1);
        db.onupgradeneeded = () => db.result.createObjectStore('identity');
        db.onsuccess = () => {
            const tx = db.result.transaction('identity', 'readwrite');
            tx.objectStore('identity').put(identity, 'value');
            tx.oncomplete = () => db.result.close();
        };
        await navigator.serviceWorker.register('/worker.js');
        document.getElementById('state').textContent = identity;
    };
    window.flyFixture = Object.freeze({snapshot, events});
})();`;

const worker = `self.addEventListener('install', event => event.waitUntil(self.skipWaiting()));
self.addEventListener('activate', event => event.waitUntil(self.clients.claim()));
self.addEventListener('message', event => { if (event.ports[0]) event.ports[0].postMessage('fly-fixture-worker'); });`;

async function serve({video = false} = {}) {
    const requests = [];
    const media = video ? fs.readFileSync(path.join(__dirname, 'media/h264-aac.mp4')) : wave();
    const html = video ? page.replace('<audio id="media" controls loop src="/media.wav"></audio>',
        '<video id="media" controls loop playsinline width="320" height="180" src="/media.mp4"></video>') : page;
    const server = http.createServer((request, response) => {
        const url = new URL(request.url, 'http://127.0.0.1');
        requests.push(url.pathname);
        if (requests.length > 500) requests.shift();
        response.setHeader('Cache-Control', 'no-store');
        const routes = {
            '/': ['text/html', html], '/second': ['text/html', html],
            '/frame': ['text/html', '<!doctype html><title>Frame</title><input aria-label="Frame input">'],
            '/fixture.js': ['text/javascript', script], '/worker.js': ['text/javascript', worker],
            '/blocked': ['text/plain', 'This request must not reach the server when blocking is enabled.'],
        };
        if (url.pathname === (video ? '/media.mp4' : '/media.wav')) {
            response.setHeader('Content-Type', video ? 'video/mp4' : 'audio/wav');
            response.setHeader('Accept-Ranges', 'bytes');
            const range = request.headers.range?.match(/^bytes=(\d+)-(\d*)$/);
            const start = range ? Number(range[1]) : 0;
            const end = range && range[2] ? Math.min(Number(range[2]), media.length - 1) : media.length - 1;
            if (start > end || start >= media.length) { response.writeHead(416); response.end(); return; }
            if (range) { response.statusCode = 206; response.setHeader('Content-Range', 'bytes ' + start + '-' + end + '/' + media.length); }
            response.setHeader('Content-Length', end - start + 1);
            response.end(media.subarray(start, end + 1)); return;
        }
        const route = routes[url.pathname];
        response.writeHead(route ? 200 : 404, {'Content-Type': route?.[0] || 'text/plain'});
        response.end(route?.[1] || 'Missing fixture');
    });
    await new Promise((resolve, reject) => {
        server.once('error', reject);
        server.listen(0, '127.0.0.1', resolve);
    });
    return {url: 'http://127.0.0.1:' + server.address().port,
        mediaSha256: crypto.createHash('sha256').update(media).digest('hex'),
        requests, close: () => new Promise(resolve => server.close(resolve))};
}

function extension(directory) {
    fs.mkdirSync(directory, {recursive: true, mode: 0o700});
    const manifest = {
        manifest_version: 3, name: 'Fly native topology probe',
        version: require('../../package.json').version.split('-')[0],
        permissions: ['tabs', 'storage'],
        background: {service_worker: 'background.js'},
        content_scripts: [{matches: ['http://127.0.0.1/*'], js: ['media.js'], run_at: 'document_idle'}],
    };
    fs.writeFileSync(path.join(directory, 'manifest.json'), JSON.stringify(manifest, null, 2));
    fs.writeFileSync(path.join(directory, 'background.js'), `
let tail = Promise.resolve();
function record(type, values = {}) {
    tail = tail.then(async () => {
        const state = await chrome.storage.local.get(['identity', 'events']);
        const events = [...(state.events || []), {type, ...values}].slice(-100);
        await chrome.storage.local.set({identity: state.identity || crypto.randomUUID(), events});
    });
}
chrome.runtime.onInstalled.addListener(event => record('installed', {reason: event.reason}));
chrome.tabs.onActivated.addListener(event => record('activated', event));
chrome.tabs.onRemoved.addListener((tabId, info) => record('removed', {tabId, ...info}));
chrome.windows.onFocusChanged.addListener(windowId => record('focus', {windowId}));
chrome.runtime.onMessage.addListener((message, sender, reply) => {
    if (message?.kind !== 'fly-probe' || !sender.tab || !sender.url?.startsWith('http://127.0.0.1:')) return;
    tail.then(async () => reply({state: await chrome.storage.local.get(['identity', 'events']),
        tabId: sender.tab.id, windowId: sender.tab.windowId,
        tabs: (await chrome.tabs.query({currentWindow: true})).map(({id, windowId, active}) => ({id, windowId, active}))}));
    return true;
});`);
    fs.writeFileSync(path.join(directory, 'media.js'), `
// A visibility-sensitive extension surrogate. It does not replace page APIs.
const remembered = new WeakMap();
document.addEventListener('visibilitychange', () => {
    for (const media of document.querySelectorAll('audio, video')) {
        if (document.hidden) remembered.set(media, !media.paused);
        else if (remembered.get(media)) media.play().catch(() => {});
    }
});
chrome.runtime.sendMessage({kind: 'fly-probe'}).then(result => {
    document.documentElement.dataset.flyExtensionProbe = JSON.stringify(result);
});`);
    return {directory, manifestSha256: crypto.createHash('sha256').update(fs.readFileSync(path.join(directory, 'manifest.json'))).digest('hex')};
}

module.exports = {serve, extension, wave};
