#!/usr/bin/env node
'use strict';
// Explicit, network-dependent desktop acceptance. Never part of ordinary npm test.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn, spawnSync} = require('node:child_process');
const {parseArgs} = require('node:util');
const {validateNativeBuild} = require('./control-smoke');
const {parseCrx, unpackPackage} = require('../../extensionPackages');
const PIE_ID = 'jpkfgepcmmchgfbjblnodjhldacghenp';
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));
const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');

function publicTwitchUrl(input) {
    const url = new URL(input);
    assert(url.protocol === 'https:' && url.hostname === 'www.twitch.tv' &&
        !url.username && !url.password && !url.port && !url.search && !url.hash &&
        /^\/(?:[a-zA-Z0-9_]{1,40})?\/?$/.test(url.pathname),
    'Use the public Twitch homepage or a channel URL without account/query data');
    return url.href;
}

function pausedState(state) {
    return state.videos.length > 0 && state.videos.every(video => video.paused && video.error === null);
}

function validateBuild(executable, file, native) {
    if (native) return validateNativeBuild(executable, file);
    const record = JSON.parse(fs.readFileSync(file));
    assert.equal(record.stage, 'build', 'Require an unmodified upstream control build');
    assert.equal(record.fly_dirty, false, 'Require clean build provenance');
    assert.equal(record.lock_sha256, sha256(path.resolve(__dirname, '../../chromium/upstream.lock.json')));
    assert.equal(fs.realpathSync(record.details.binary), fs.realpathSync(executable));
    assert.equal(record.details.sha256, sha256(executable));
    const verified = spawnSync(process.env.FLY_PYTHON || 'python3',
        [path.resolve(__dirname, '../../scripts/chromium/verify-runtime.py'),
            '--build-record', file, '--executable', executable], {encoding: 'utf8'});
    assert.equal(verified.status, 0, verified.stderr || String(verified.error || ''));
    return {file, sha256: sha256(file), flyCommit: record.fly_commit,
        lockSha256: record.lock_sha256, runtime: JSON.parse(verified.stdout), unmodifiedUpstream: true};
}

// Pie intentionally modifies main-world focus/visibility on Twitch. Read native
// values in a separate world, alongside the site's exposed values. This adds no
// listeners, properties, media operations or focus/visibility emulation.
function nativeSnapshot() {
    return {hidden: document.hidden, focused: document.hasFocus(), visibility: document.visibilityState,
        videos: [...document.querySelectorAll('video')].map(video => ({paused: video.paused,
            muted: video.muted, time: video.currentTime, ready: video.readyState,
            width: video.videoWidth, height: video.videoHeight, error: video.error?.code || null,
            frames: video.getVideoPlaybackQuality().totalVideoFrames})),
        playerText: document.querySelector('[data-a-target="video-player"]')?.innerText?.slice(0, 200)};
}

async function main() {
    const {values} = parseArgs({options: {
        executable: {type: 'string'}, 'build-record': {type: 'string'},
        'work-dir': {type: 'string'}, url: {type: 'string'},
        native: {type: 'boolean', default: false}, 'pie-crx': {type: 'string'},
    }});
    assert(values.executable && values['build-record'] && values['work-dir'] && values.url,
        'Provide --executable, --build-record, --work-dir and --url');
    assert.equal(process.platform, 'linux', 'This physical-input fixture covers Linux X11 only');
    assert.equal(process.env.FLY_TEST_OZONE_PLATFORM, 'x11', 'Use desktop-fixture.py');
    const url = publicTwitchUrl(values.url);
    const executable = path.resolve(values.executable);
    const build = validateBuild(executable, path.resolve(values['build-record']), values.native);
    let packageBytes;
    if (values['pie-crx']) {
        assert(fs.statSync(values['pie-crx']).size <= 64 * 1024 * 1024, 'Pie CRX exceeds 64 MiB');
        packageBytes = fs.readFileSync(values['pie-crx']);
        parseCrx(packageBytes, PIE_ID); // Verify the signed identity before any profile side effects.
    }
    const {chromium} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
    const directory = path.resolve(values['work-dir']);
    fs.mkdirSync(directory, {recursive: true, mode: 0o700});
    const work = fs.mkdtempSync(path.join(directory, 'live-twitch-'));
    const profile = path.join(work, 'profile');
    const extension = packageBytes ? unpackPackage(packageBytes, path.join(work, 'pie'), PIE_ID) : null;
    if (extension) assert.equal(extension.manifest.manifest_version, 3);
    const pie = extension ? {id: PIE_ID, version: extension.manifest.version,
        manifestVersion: extension.manifest.manifest_version,
        sha256: crypto.createHash('sha256').update(packageBytes).digest('hex'),
        bytes: packageBytes.length, signatureAndIdentityVerified: true,
        manifestPublicKeyAddedForStableNativeId: true} : null;
    const args = ['--user-data-dir=' + profile, '--remote-debugging-port=0',
        '--remote-debugging-address=127.0.0.1', '--no-first-run', '--no-default-browser-check',
        '--ozone-platform=x11', 'about:blank'];
    if (values.native) args.unshift('--fly-native');
    if (extension) args.unshift('--load-extension=' + extension.root);
    const result = {scope: 'Public live Twitch regression in a disposable build-tree Linux X11 browser',
        native: values.native, url, build, pie, args, sandboxDisabled: false,
        harnessSha256: sha256(__filename), inputSha256: sha256(path.join(__dirname, 'x11-input.py')),
        checks: {}, checkpoints: [], startedAt: new Date().toISOString(),
        limitations: ['Not packaged/platform acceptance', 'Unpacked signed Pie; not store-install/update acceptance',
            'Public service state is variable; failures are retained, never converted into passes']};
    const fd = fs.openSync(path.join(work, 'browser.log'), 'wx', 0o600);
    const child = spawn(executable, args, {stdio: ['ignore', fd, fd]});
    fs.closeSync(fd);
    let spawnError;
    child.once('error', error => {spawnError = error;});
    let browser, page, session, world;
    const input = (...args) => {
        const sent = spawnSync(process.env.FLY_PYTHON || 'python3',
            [path.join(__dirname, 'x11-input.py'), ...args], {encoding: 'utf8'});
        assert.equal(sent.status, 0, sent.stderr || String(sent.error || ''));
    };
    const snapshot = async () => {
        const raw = await session.send('Runtime.evaluate', {contextId: world, returnByValue: true,
            expression: '(' + nativeSnapshot.toString() + ')()'});
        assert(!raw.exceptionDetails, 'Native isolated observation failed');
        const exposed = await page.evaluate(() => ({hidden: document.hidden, focused: document.hasFocus(),
            visibility: document.visibilityState, ownFocus: Object.hasOwn(document, 'hasFocus'),
            ownHidden: Object.hasOwn(document, 'hidden'), ownVisibility: Object.hasOwn(document, 'visibilityState')}));
        return {...raw.result.value, exposed};
    };
    const until = async (predicate, timeout = 10000) => {
        const deadline = Date.now() + timeout;
        let state;
        while (Date.now() < deadline) {
            state = await snapshot();
            if (predicate(state)) return state;
            await pause(100);
        }
        throw new Error('Native observation timed out: ' + JSON.stringify(state));
    };
    const checkpoint = async name => {
        const state = await snapshot();
        result.checkpoints.push({name, at: new Date().toISOString(), ...state});
        console.log('Live Twitch checkpoint:', name);
        return state;
    };
    const paused = pausedState;
    const click = async locator => {
        await until(state => !state.hidden);
        await Promise.race([
            page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)))),
            pause(5000).then(() => {throw new Error('Native compositor frame timed out');}),
        ]);
        const box = await locator.boundingBox();
        assert(box, 'Missing physical click target');
        const bounds = await page.evaluate(() => ({x: screenX + outerWidth - innerWidth,
            y: screenY + outerHeight - innerHeight, width: innerWidth, height: innerHeight}));
        assert(bounds.width > 0 && bounds.height > 0, 'Native compositor/layout must be ready');
        // The current Fly desktop has a 29px footer below its native content.
        const x = Math.round(bounds.x + box.x + box.width / 2);
        const y = Math.round(bounds.y - (values.native ? 29 : 0) + box.y + box.height / 2);
        input('--click', String(x), String(y));
        await pause(500);
    };
    const screenshot = name => {
        const file = path.join(work, name + '.png');
        const capture = spawnSync('import', ['-window', 'root', file], {encoding: 'utf8'});
        assert.equal(capture.status, 0, capture.stderr || String(capture.error || ''));
        result.screenshots ??= [];
        result.screenshots.push({file, sha256: sha256(file)});
    };
    try {
        let port;
        for (let i = 0; i < 1200 && !port; i++) {
            if (spawnError) throw spawnError;
            assert.equal(child.exitCode, null, 'Browser exited during startup');
            const activePort = path.join(profile, 'DevToolsActivePort');
            if (fs.existsSync(activePort)) port = fs.readFileSync(activePort, 'utf8').split('\n')[0];
            if (!port) await pause(100);
        }
        assert(/^\d+$/.test(port), 'Browser debugger startup timed out');
        browser = await chromium.connectOverCDP('http://127.0.0.1:' + port, {noDefaults: true});
        const context = browser.contexts()[0];
        page = context.pages().find(candidate => candidate.url() === 'about:blank');
        assert(page, 'Missing initial native page');
        session = await context.newCDPSession(page);
        await session.send('Emulation.setFocusEmulationEnabled', {enabled: false});
        const cdp = await browser.newBrowserCDPSession();
        if (pie) {
            for (let i = 0; i < 300; i++) {
                const {targetInfos} = await cdp.send('Target.getTargets');
                if (targetInfos.some(target => target.url.startsWith('chrome-extension://' + PIE_ID + '/'))) break;
                await pause(100);
            }
        }
        result.extensionTargets = (await cdp.send('Target.getTargets')).targetInfos
            .filter(target => target.url.startsWith('chrome-extension://')).map(target => ({type: target.type,
                origin: new URL(target.url).protocol + '//' + new URL(target.url).host}));
        if (pie) assert(result.extensionTargets.some(target => target.origin === 'chrome-extension://' + PIE_ID),
            'The official Pie worker/page must actually load');
        const closeWelcome = async () => {
            if (!pie) return;
            // onInstalled opens an asynchronous welcome tab. Close only this
            // known disposable onboarding surface; never configure an account.
            for (let i = 0; i < 300; i++) {
                const welcome = context.pages().filter(candidate => {
                    try {
                        const target = new URL(candidate.url());
                        return ['pie.org', 'www.pie.org'].includes(target.hostname) &&
                            target.pathname.startsWith('/welcome/');
                    } catch {return false;}
                });
                if (welcome.length) {
                    for (const tab of welcome) await tab.close();
                    await pause(500);
                    return;
                }
                await pause(100);
            }
        };
        await closeWelcome();
        await page.goto(url, {waitUntil: 'domcontentloaded', timeout: 60000});
        await page.bringToFront();
        const tree = await session.send('Page.getFrameTree');
        world = (await session.send('Page.createIsolatedWorld', {frameId: tree.frameTree.frame.id,
            worldName: 'FlyReadOnlyAcceptance'})).executionContextId;
        await pause(5000);
        const reject = page.getByRole('button', {name: 'Reject', exact: true});
        if (await reject.count()) await click(reject);
        // A preroll ad has a different background-playback policy. Wait for an
        // actual live stream, then unmute through the site's real player UI.
        await until(state => state.playerText?.includes('LIVE') && state.videos.some(video =>
            video.ready >= 2 && video.width > 0 && !video.paused && video.frames > 0), 120000);
        const player = page.locator('button[data-a-target="player-play-pause-button"]:visible');
        if ((await snapshot()).videos.some(video => video.muted))
            await click(page.locator('button[data-a-target="player-mute-unmute-button"]:visible'));
        await pause(3000);
        await checkpoint('live-playing');
        screenshot('playing');
        await click(player);
        await until(paused);
        assert(paused(await checkpoint('user-paused')));
        result.checks.playerPause = 'pass';
        if (values.native) {
            input('--keys', 'Control_L', 'Shift_L', 'w');
            await until(state => !state.hidden && !state.focused);
            assert(paused(await checkpoint('workspace-overlay')));
            screenshot('overlay');
            input('--keys', 'Escape');
            await until(state => !state.hidden && state.focused);
            await pause(2000);
            assert(paused(await checkpoint('overlay-closed')));
            result.checks.pauseAcrossNativeOverlay = 'pass';
            input('--keys', 'Control_L', 'comma');
            await until(state => !state.hidden && !state.focused);
            assert(paused(await checkpoint('preferences-open')));
            input('--keys', 'Escape');
            await until(state => !state.hidden && state.focused);
            await pause(2000);
            assert(paused(await checkpoint('preferences-closed')));
            result.checks.pauseAcrossNativePreferences = 'pass';
        }
        const {targetId} = await cdp.send('Target.createTarget', {url: 'about:blank', newWindow: false});
        let second;
        for (let i = 0; i < 100 && !second; i++) {
            second = context.pages().find(candidate => candidate !== page && candidate.url() === 'about:blank');
            if (!second) await pause(100);
        }
        assert(second, 'Second native tab did not open');
        const secondSession = await context.newCDPSession(second);
        await secondSession.send('Emulation.setFocusEmulationEnabled', {enabled: false});
        const firstWindow = await session.send('Browser.getWindowForTarget');
        const secondWindow = await secondSession.send('Browser.getWindowForTarget');
        assert.equal(firstWindow.windowId, secondWindow.windowId, 'Use tabs in the same native browser window');
        result.secondTarget = targetId;
        await second.bringToFront();
        await until(state => state.hidden);
        await pause(2000);
        assert(paused(await checkpoint('paused-background')));
        await page.bringToFront();
        await until(state => !state.hidden);
        await pause(2000);
        assert(paused(await checkpoint('paused-return')));
        result.checks.pauseAcrossNativeTabSwitch = 'pass';
        if (values.native) {
            // The clean catalog contains one workspace. The next picker item
            // is its native New workspace action; no profile data is copied.
            input('--keys', 'Control_L', 'Shift_L', 'w');
            await until(state => !state.hidden && !state.focused);
            input('--keys', 'Down');
            input('--keys', 'Return');
            await until(state => state.hidden, 30000);
            await closeWelcome();
            await pause(2000);
            assert(paused(await checkpoint('other-workspace')));
            input('--keys', 'Control_L', 'Shift_L', 'w');
            await pause(500);
            input('--keys', 'Return');
            await until(state => !state.hidden && state.focused, 30000);
            await pause(2000);
            assert(paused(await checkpoint('workspace-return')));
            result.checks.pauseAcrossNativeWorkspaceSwitch = 'pass';
        }
        await click(player);
        const playing = await until(state => state.videos.some(video => !video.paused));
        await second.bringToFront();
        await until(state => state.hidden);
        await pause(4000);
        const background = await checkpoint('playing-background');
        assert(background.videos.some((video, index) => !video.paused && video.time > playing.videos[index]?.time),
            'The real live stream must continue in a hidden tab');
        result.checks.backgroundPlaybackContinues = 'pass';
        await page.bringToFront();
        await until(state => !state.hidden);
        await click(player);
        await until(paused);
        assert(paused(await checkpoint('final-paused')));
        screenshot('final');
        result.result = 'pass';
    } catch (error) {
        result.result = 'fail';
        result.error = error.message.slice(0, 2000);
        if (page) {
            await checkpoint('failure').catch(() => {});
            try {screenshot('failure');} catch {}
        }
        process.exitCode = 1;
    } finally {
        result.finishedAt = new Date().toISOString();
        try {
            fs.writeFileSync(path.join(work, 'result.json'), JSON.stringify(result, null, 2) + '\n', {mode: 0o600});
            console.log('Live Twitch evidence:', path.join(work, 'result.json'));
        } finally {
            if (browser) await browser.close().catch(() => {});
            if (child.exitCode === null) {
                child.kill('SIGTERM');
                await Promise.race([new Promise(resolve => child.once('exit', resolve)), pause(5000)]);
                if (child.exitCode === null) child.kill('SIGKILL');
            }
        }
    }
}

if (require.main === module) main().catch(error => {console.error(error); process.exitCode = 1;});
module.exports = {publicTwitchUrl, pausedState};
