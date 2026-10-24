// Optional live check that a native Fly build plays Widevine-protected video.
// Needs network access (Google's component updater, jsDelivr, Shaka's public
// demo stream and licence proxy) and an owned display:
// FLY_NATIVE_BINARY=... FLY_PLAYWRIGHT_MODULE=... DISPLAY=... node <this file>
// A fresh profile downloads the CDM, then a restart (Linux loads it only at
// startup) must decrypt and play the stream. FLY_WIDEVINE_FAST=1 shortens
// the component updater's one-minute start delay; FLY_WIDEVINE_EVIDENCE keeps
// the profile, logs and a screenshot there.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const http = require('node:http');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {chromium} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const binary = process.env.FLY_NATIVE_BINARY;
assert(binary, 'Set FLY_NATIVE_BINARY to a built native Fly browser');
const work = process.env.FLY_WIDEVINE_EVIDENCE || fs.mkdtempSync(path.join(os.tmpdir(), 'fly-widevine-'));
const profile = path.join(work, 'profile');
fs.mkdirSync(profile, {recursive: true});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

// EME needs a secure context; a localhost page is one.
const PAGE = `<!doctype html><meta charset="utf-8"><title>Widevine</title>
<video id="v" muted width="640" height="360"></video>
<script src="https://cdn.jsdelivr.net/npm/shaka-player@4.11.7/dist/shaka-player.compiled.js"></script>
<script>
window.probeKeySystem = async () => {
  try {
    const access = await navigator.requestMediaKeySystemAccess('com.widevine.alpha', [{
      initDataTypes: ['cenc'],
      videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.42E01E"'}],
      audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]}]);
    await access.createMediaKeys();
    return {ok: true};
  } catch (e) { return {ok: false, error: e.name + ': ' + e.message}; }
};
window.playProtected = async () => {
  const video = document.getElementById('v');
  shaka.polyfill.installAll();
  const player = new shaka.Player();
  await player.attach(video);
  player.configure({drm: {servers: {'com.widevine.alpha': 'https://cwip-shaka-proxy.appspot.com/no_auth'}}});
  try {
    await player.load('https://storage.googleapis.com/shaka-demo-assets/angel-one-widevine/dash.mpd');
  } catch (e) { return {loadError: e.code}; }
  await video.play();
  const start = performance.now();
  while (performance.now() - start < 30000 && video.currentTime < 8) await new Promise(r => setTimeout(r, 250));
  const quality = video.getVideoPlaybackQuality();
  return {currentTime: video.currentTime, frames: quality.totalVideoFrames,
    keySystem: player.drmInfo() && player.drmInfo().keySystem, mediaKeys: !!video.mediaKeys};
};
</script>`;

async function launch(server, tag) {
    const portFile = path.join(profile, 'DevToolsActivePort');
    fs.rmSync(portFile, {force: true});
    const log = fs.openSync(path.join(work, tag + '.log'), 'w');
    const child = spawn(binary, ['--fly-native', `--user-data-dir=${profile}`, '--no-first-run',
        '--ozone-platform=x11', '--password-store=basic', '--remote-debugging-port=0',
        ...(process.env.FLY_WIDEVINE_FAST === '1' ? ['--component-updater=fast-update'] : [])], {
        env: {...process.env, HOME: work, XDG_CONFIG_HOME: work, XDG_CACHE_HOME: work},
        stdio: ['ignore', log, log]});
    for (let i = 0; i < 1200 && !fs.existsSync(portFile); i++) {
        assert.equal(child.exitCode, null, 'Fly exited during startup');
        await wait(100);
    }
    assert(fs.existsSync(portFile), 'Native browser did not expose its debugging port within 120 seconds');
    const browser = await chromium.connectOverCDP({endpointURL: 'http://127.0.0.1:' +
        fs.readFileSync(portFile, 'utf8').split('\n')[0], noDefaults: true});
    const page = await browser.contexts()[0].newPage();
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    return {child, browser, page};
}

async function close({child, browser}) {
    await browser.close().catch(() => {});
    child.kill('SIGTERM');
    for (let i = 0; i < 100 && child.exitCode === null && child.signalCode === null; i++) await wait(100);
    if (child.exitCode === null && child.signalCode === null) child.kill('SIGKILL');
}

const cdm = () => {
    const dir = path.join(profile, 'WidevineCdm');
    return fs.existsSync(dir) && fs.readdirSync(dir, {recursive: true})
        .some(file => file.endsWith('libwidevinecdm.so'));
};

(async () => {
    const server = http.createServer((request, response) => {
        response.writeHead(200, {'content-type': 'text/html'});
        response.end(PAGE);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let session;
    try {
        session = await launch(server, 'first');
        for (let i = 0; i < 300 && !cdm(); i++) await wait(1000);
        assert(cdm(), 'The component updater did not download the Widevine CDM within five minutes');
        console.log('Widevine CDM downloaded:', fs.readdirSync(path.join(profile, 'WidevineCdm')).join(', '));
        await close(session);
        session = await launch(server, 'second');
        const keySystem = await session.page.evaluate(() => window.probeKeySystem());
        assert(keySystem.ok, 'com.widevine.alpha is unavailable after restart: ' + keySystem.error);
        const playback = await session.page.evaluate(() => window.playProtected());
        await session.page.screenshot({path: path.join(work, 'playback.png')});
        console.log('Playback:', JSON.stringify(playback));
        assert.equal(playback.keySystem, 'com.widevine.alpha');
        assert(playback.mediaKeys && playback.currentTime > 3 && playback.frames > 0,
            'The protected stream did not play');
        console.log('Widevine playback works. Evidence:', work);
    } finally {
        if (session) await close(session);
        server.close();
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
