#!/usr/bin/env node
'use strict';
// Headful control/native media fixtures; not full native Fly acceptance.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn, spawnSync} = require('node:child_process');
const {parseArgs} = require('node:util');
const fixtures = require('./fixtures');

const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function validateNativeBuild(executable, recordFile) {
    assert(recordFile, 'Native fixtures require --native-record from native.py build');
    const record = JSON.parse(fs.readFileSync(recordFile, 'utf8'));
    assert.equal(record.stage, 'native-build', 'Require a completed native build record');
    assert.equal(record.details?.native_desktop_supported, true, 'Native desktop adapter is unsupported in this build');
    assert.equal(record.details.target, 'chrome', 'Require the native browser build');
    assert.match(record.details.fingerprint || '', /^[0-9a-f]{64}$/, 'Require overlay fingerprint');
    assert.equal(record.lock_sha256, sha256(path.resolve(__dirname, '../../chromium/upstream.lock.json')),
        'Native engine lock differs from the fixture checkout');
    assert.equal(fs.realpathSync(record.details.binary), fs.realpathSync(executable), 'Native executable path differs');
    assert.equal(record.details.sha256, sha256(executable), 'Native executable hash differs');
    assert(record.details.runtime, 'Require complete native runtime manifest');
    const runtime = spawnSync(process.env.FLY_PYTHON || (process.platform === 'win32' ? 'python' : 'python3'),
        [path.resolve(__dirname, '../../scripts/chromium/verify-runtime.py'),
            '--build-record', path.resolve(recordFile), '--executable', executable], {encoding: 'utf8'});
    assert.equal(runtime.status, 0, 'Native runtime verification failed: ' + (runtime.stderr || runtime.error || ''));
    const runtimeFiles = JSON.parse(runtime.stdout);
    return {file: path.resolve(recordFile), sha256: sha256(recordFile),
        overlayFingerprint: record.details.fingerprint, flyCommit: record.fly_commit,
        flyDirty: record.fly_dirty, lockSha256: record.lock_sha256,
        runtimeManifestSha256: record.details.runtime.sha256, runtimeEntries: runtimeFiles.entries};
}

const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, process, timeout = 30000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {
        if (process.exitCode !== null) throw new Error('Browser exited before debugger was ready: ' + process.exitCode);
        const value = await fn();
        if (value) return value;
        await pause(100);
    }
    throw new Error('Timed out waiting for Chromium debugger port');
}

async function main() {
    const {values} = parseArgs({options: {
        executable: {type: 'string'}, 'work-dir': {type: 'string'},
        extension: {type: 'boolean', default: false},
        video: {type: 'boolean', default: false},
        native: {type: 'boolean', default: false},
        'native-record': {type: 'string'},
        backend: {type: 'string', default: 'x11'},
    }});
    assert(values.executable && values['work-dir'], 'Provide --executable and a dedicated --work-dir');
    assert(['x11', 'wayland'].includes(values.backend), 'This desktop smoke requires X11 or Wayland');
    const executable = path.resolve(values.executable);
    assert(values.native || !values['native-record'], '--native-record requires --native');
    // Validate before creating any evidence/profile or starting a fixture server.
    // Unknown switches alone cannot prove this executable embeds Fly's desktop.
    const nativeBuild = values.native ? validateNativeBuild(executable, values['native-record']) : null;
    const {chromium} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
    const directory = path.resolve(values['work-dir']);
    fs.mkdirSync(directory, {recursive: true, mode: 0o700});
    const work = fs.mkdtempSync(path.join(directory, 'control-smoke-'));
    const profile = path.join(work, 'profile');
    const fixture = await fixtures.serve({video: values.video});
    const extension = values.extension ? fixtures.extension(path.join(work, 'extension')) : null;
    const args = ['--user-data-dir=' + profile, '--remote-debugging-port=0',
        '--remote-debugging-address=127.0.0.1', '--no-first-run', '--no-default-browser-check',
        '--ozone-platform=' + values.backend, 'about:blank'];
    if (extension) args.unshift('--load-extension=' + extension.directory);
    if (values.native) args.unshift('--fly-native');
    const output = fs.openSync(path.join(work, 'browser.log'), 'wx', 0o600);
    const child = spawn(executable, args, {stdio: ['ignore', output, output]});
    let spawnError;
    child.once('error', error => {spawnError = error;});
    fs.closeSync(output);
    let browser;
    let context;
    const evidence = {role: values.native ? 'native Fly single-profile media fixture' : 'control fixture validation; no native Fly acceptance',
        native: values.native, media: values.video ? 'H264/AAC MP4' : 'PCM WAV',
        mediaFixtureSha256: fixture.mediaSha256,
        executable, launcherSha256: sha256(executable), nativeBuild,
        args, sandboxDisabled: false, profile, backend: values.backend,
        extensionManifestSha256: extension?.manifestSha256 || null,
        checks: {}, limitations: [values.native ? 'Single profile fixture only; full workspace/split/desktop acceptance untested' : 'No native Fly shell/workspace integration',
            'Real Twitch and real Pie untested; synthetic loopback media only',
            'No minimise/restore, packaged application or platform-wide acceptance']};
    try {
        const activePort = path.join(profile, 'DevToolsActivePort');
        const port = await until(() => {
            if (spawnError) throw spawnError;
            if (!fs.existsSync(activePort)) return false;
            const lines = fs.readFileSync(activePort, 'utf8').split('\n');
            return /^\d+$/.test(lines[0]) && lines[1]?.startsWith('/devtools/browser/') && lines[0];
        }, child);
        browser = await chromium.connectOverCDP('http://127.0.0.1:' + port, {noDefaults: true});
        evidence.browserVersion = browser.version();
        context = browser.contexts()[0];
        const page = await until(() => context.pages().find(page => !page.isClosed()), child);
        const firstSession = await context.newCDPSession(page);
        // Keep native focus/visibility; also guard against older attach paths.
        await firstSession.send('Emulation.setFocusEmulationEnabled', {enabled: false});
        evidence.focusEmulationDisabled = true;
        page.setDefaultTimeout(15000);
        await page.goto(fixture.url);
        await page.waitForFunction(() => window.flyFixture);
        evidence.mediaCapabilities = await page.evaluate(() => {
            const media = document.querySelector('#media');
            return {type: media.tagName, mp4: media.canPlayType('video/mp4; codecs="avc1.42E01E,mp4a.40.2"')};
        });
        if (values.video) assert(evidence.mediaCapabilities.mp4, 'H264/AAC support must be built into the real engine');
        await page.waitForFunction(() => {
            const media = document.querySelector('#media');
            return media.readyState >= 2 || media.error;
        });
        assert.equal(await page.evaluate(() => document.querySelector('#media').error?.code || null), null, 'The engine must decode the synthetic media');
        await page.bringToFront();
        await page.waitForFunction(() => !document.hidden);
        const before = await page.evaluate(() => flyFixture.events.filter(event => event.type === 'visibilitychange').length);
        await page.locator('#editable').fill('normal editable input');
        assert.equal(await page.evaluate(() => flyFixture.events.filter(event => event.type === 'visibilitychange').length), before,
            'Changing focus inside a visible document must not hide it');
        evidence.checks.editableFocusDoesNotHide = 'pass';
        await page.locator('#play').click();
        await page.waitForFunction(() => !document.querySelector('#media').paused);
        if (values.video) {
            await page.waitForFunction(() => document.querySelector('#media').getVideoPlaybackQuality().totalVideoFrames > 0);
            evidence.videoFrames = await page.evaluate(() => document.querySelector('#media').getVideoPlaybackQuality().totalVideoFrames);
            evidence.checks.nativeVideoDecode = 'pass';
        }
        await page.locator('#pause').click();
        const cdp = await browser.newBrowserCDPSession();
        const pendingPage = context.waitForEvent('page');
        // Playwright's context.newPage may request a new desktop window. This
        // case requires two tabs in the same native browser window.
        await cdp.send('Target.createTarget', {url: fixture.url + '/second', newWindow: false});
        const second = await pendingPage;
        await second.waitForLoadState();
        const secondSession = await context.newCDPSession(second);
        await secondSession.send('Emulation.setFocusEmulationEnabled', {enabled: false});
        const firstWindow = await firstSession.send('Browser.getWindowForTarget');
        const secondWindow = await secondSession.send('Browser.getWindowForTarget');
        assert.equal(firstWindow.windowId, secondWindow.windowId, 'Visibility fixture must use tabs in one native window');
        evidence.checks.sameNativeWindow = 'pass';
        await second.bringToFront();
        // requestAnimationFrame is suspended in a hidden native tab. Poll with
        // timers so the harness observes that state without changing visibility.
        await page.waitForFunction(() => document.hidden, null, {polling: 100});
        assert(await page.evaluate(() => flyFixture.snapshot().paused), 'Explicit pause survives backgrounding');
        await page.bringToFront();
        await page.waitForFunction(() => !document.hidden);
        await pause(250);
        assert(await page.evaluate(() => flyFixture.snapshot().paused), 'Returning to a paused tab must not play it');
        evidence.checks.pausedAcrossTabSwitch = 'pass';
        await page.locator('#play').click();
        await page.waitForFunction(() => !document.querySelector('#media').paused);
        await second.bringToFront();
        await page.waitForFunction(() => document.hidden, null, {polling: 100});
        const time = await page.evaluate(() => flyFixture.snapshot().time);
        await page.waitForFunction(time => !document.querySelector('#media').paused && document.querySelector('#media').currentTime !== time, time, {polling: 100});
        evidence.checks.backgroundAudioContinues = 'pass';
        await page.bringToFront();
        await page.locator('#pause').click();
        if (extension) {
            await page.waitForFunction(() => document.documentElement.dataset.flyExtensionProbe);
            evidence.extensionProbe = await page.evaluate(() => JSON.parse(document.documentElement.dataset.flyExtensionProbe));
            assert.equal(typeof evidence.extensionProbe.tabId, 'number');
            assert.equal(typeof evidence.extensionProbe.windowId, 'number');
            assert.equal(typeof evidence.extensionProbe.state.identity, 'string');
            evidence.checks.extensionProbeLoaded = 'pass';
        }
        evidence.events = await page.evaluate(() => flyFixture.events);
        await page.screenshot({path: path.join(work, 'fixture.png')});
        evidence.result = 'pass';
    } catch (error) {
        evidence.result = 'fail'; evidence.error = error.message;
        throw error;
    } finally {
        if (browser) {
            await browser.close().catch(() => {});
        }
        if (child.exitCode === null) {
            child.kill('SIGTERM');
            await Promise.race([new Promise(resolve => child.once('exit', resolve)), pause(5000)]);
            if (child.exitCode === null) child.kill('SIGKILL');
        }
        await fixture.close();
        fs.writeFileSync(path.join(work, 'result.json'), JSON.stringify(evidence, null, 2) + '\n', {mode: 0o600});
        console.log('Control smoke evidence: ' + path.join(work, 'result.json'));
    }
}

if (require.main === module) main().catch(error => {console.error(error); process.exitCode = 1;});
module.exports = {validateNativeBuild};
