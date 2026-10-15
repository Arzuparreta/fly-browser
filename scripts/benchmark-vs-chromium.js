// Optional Linux comparison of Fly against a stock Chromium. Normal launches load no
// instrumentation. Both browsers are started as ordinary processes inside their own
// systemd user scope, so CPU time is exact (exited children included) and memory is
// the PSS of every process in the scope. Both are driven over the DevTools protocol.
//
//   node scripts/benchmark-vs-chromium.js
//   node scripts/benchmark-vs-chromium.js --report results.json   (Markdown tables)
//
// Results and analysis: docs/performance-vs-chromium.md
//
// FLY_VS_CONFIGS      comma list, default all (see CONFIGS below)
// FLY_VS_SCENARIOS    comma list of browse,video,speedometer (default all)
// FLY_VS_ROUNDS       rounds per scenario (default 3; speedometer uses FLY_VS_SPEEDOMETER_ROUNDS, default 2)
// FLY_VS_UBOL         unpacked uBlock Origin Lite for the chromium-ubol config
// FLY_VS_VIDEO        WebM file for the video scenario
// FLY_VS_OUTPUT       JSON output (default /tmp/fly-vs-chromium.json)
// FLY_VS_CHROMIUM     Chromium binary (default /usr/bin/chromium)
// FLY_VS_CFT          Chrome for Testing binary for the cft configs (an official,
//                     PGO-optimised Google build, unlike distribution Chromium packages)
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const net = require('node:net');
const { spawn, execFileSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const electronBinary = path.join(root, 'node_modules/electron/dist/electron');
const chromiumBinary = process.env.FLY_VS_CHROMIUM || '/usr/bin/chromium';
const cftBinary = process.env.FLY_VS_CFT || null;
const output = process.env.FLY_VS_OUTPUT || '/tmp/fly-vs-chromium.json';
const rounds = Number(process.env.FLY_VS_ROUNDS || 3);
const speedometerRounds = Number(process.env.FLY_VS_SPEEDOMETER_ROUNDS || 2);
const idleMs = Number(process.env.FLY_VS_IDLE_MS || 30000);
const ubol = process.env.FLY_VS_UBOL || null;
const video = process.env.FLY_VS_VIDEO || null;
const windowSize = [1600, 1040];
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

// A mix of light and advert-heavy pages. They change between runs, which is why every
// round visits all configurations back to back in alternating order.
const SITES = (process.env.FLY_VS_SITES || [
    'https://en.wikipedia.org/wiki/Web_browser',
    'https://www.theverge.com/',
    'https://edition.cnn.com/',
    'https://www.reddit.com/r/linux/',
    'https://www.youtube.com/',
    'https://github.com/electron/electron',
    'https://www.bbc.com/news',
    'https://www.marca.com/',
    'https://elpais.com/',
    'https://stackoverflow.com/questions'
].join(',')).split(',');

const CONFIGS = {
    // Stock Chromium, clean profile. --no-first-run skips the welcome tab and
    // --password-store=basic avoids a keyring prompt; nothing else is changed.
    'chromium': {browser: 'chromium', args: []},
    // Chromium with uBlock Origin Lite. Chromium 153 no longer runs Manifest V2,
    // so the full uBlock Origin Fly ships cannot be loaded there.
    'chromium-ubol': {browser: 'chromium', args: [], ubol: true},
    // Reference: Google's official Chrome for Testing build of the same engine, to
    // separate Fly's own cost from how the distribution compiled its Chromium.
    'cft': {browser: 'chromium', args: [], binary: 'cft'},
    'cft-ubol': {browser: 'chromium', args: [], binary: 'cft', ubol: true},
    // Diagnostic: Chromium with the renderer process cap Fly sets by default.
    'chromium-limit4': {browser: 'chromium', args: ['--renderer-process-limit=4']},
    // Current Fly defaults: the included uBlock Origin, extensions enabled.
    'fly': {browser: 'fly', config: {}},
    // Explicit extension-enabled configuration; retained for historical comparisons.
    'fly-ext': {browser: 'fly', config: {extensions_enabled: true}},
    // Fly with extensions disabled. There is no native blocker fallback.
    'fly-noblock': {browser: 'fly', config: {extensions_enabled: false}}
};
const configNames = (process.env.FLY_VS_CONFIGS || Object.keys(CONFIGS).filter(name => cftBinary || CONFIGS[name].binary !== 'cft').join(',')).split(',');
const scenarios = (process.env.FLY_VS_SCENARIOS || 'browse,video,speedometer').split(',');
for (const name of configNames) if (!CONFIGS[name]) throw new Error(`Unknown config ${name}`);
const reporting = process.argv[2] === '--report' || require.main !== module;
if (!reporting && configNames.some(name => CONFIGS[name].binary === 'cft') && !cftBinary) throw new Error('FLY_VS_CFT must point at a Chrome for Testing binary');
if (!reporting && configNames.some(name => CONFIGS[name].ubol) && !ubol) throw new Error('FLY_VS_UBOL must point at an unpacked uBlock Origin Lite');
if (!reporting && scenarios.includes('video') && !video) throw new Error('FLY_VS_VIDEO must point at a WebM file');

// MINIMAL DEVTOOLS CLIENT (one browser-level socket, flattened sessions)

class Cdp {
    static async connect(url) {
        const cdp = new Cdp();
        cdp.socket = new WebSocket(url);
        cdp.nextId = 1;
        cdp.pending = new Map();
        cdp.listeners = new Set();
        cdp.socket.addEventListener('message', event => {
            const message = JSON.parse(event.data);
            if (message.id && cdp.pending.has(message.id)) {
                const {resolve, reject} = cdp.pending.get(message.id);
                cdp.pending.delete(message.id);
                if (message.error) reject(new Error(`${message.error.message} ${message.error.data || ''}`));
                else resolve(message.result);
            } else if (message.method) {
                for (const listener of cdp.listeners) listener(message);
            }
        });
        cdp.socket.addEventListener('close', () => {
            for (const {reject} of cdp.pending.values()) reject(new Error('DevTools socket closed'));
            cdp.pending.clear();
        });
        await new Promise((resolve, reject) => {
            cdp.socket.addEventListener('open', resolve, {once: true});
            cdp.socket.addEventListener('error', reject, {once: true});
        });
        return cdp;
    }
    send(method, params = {}, sessionId, timeoutMs = 30000) {
        const id = this.nextId++;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {this.pending.delete(id); reject(new Error(`${method} timed out`));}, timeoutMs);
            this.pending.set(id, {resolve: v => {clearTimeout(timer); resolve(v);}, reject: e => {clearTimeout(timer); reject(e);}});
            this.socket.send(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}));
        });
    }
    on(listener) {this.listeners.add(listener); return () => this.listeners.delete(listener);}
    close() {try {this.socket.close();} catch (_) {}}
}

async function evaluate(cdp, sessionId, expression, timeoutMs) {
    const result = await cdp.send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true}, sessionId, timeoutMs);
    if (result.exceptionDetails) throw new Error(result.exceptionDetails.exception?.description || result.exceptionDetails.text);
    return result.result.value;
}

async function targets(cdp) {
    return (await cdp.send('Target.getTargets')).targetInfos;
}

// PROCESS ACCOUNTING

const clockTicks = Number(execFileSync('getconf', ['CLK_TCK'], {encoding: 'utf8'}));

function cgroupDir(pid) {
    const line = fs.readFileSync(`/proc/${pid}/cgroup`, 'utf8').trim().split('\n').find(l => l.startsWith('0::'));
    return path.join('/sys/fs/cgroup', line.slice(3));
}

function cpuUsec(dir) {
    return Number(fs.readFileSync(path.join(dir, 'cpu.stat'), 'utf8').match(/^usage_usec (\d+)/m)[1]);
}

function cgroupMemory(dir) {
    const stat = Object.fromEntries(fs.readFileSync(path.join(dir, 'memory.stat'), 'utf8').trim().split('\n').map(l => l.split(' ')).map(([k, v]) => [k, Number(v)]));
    const read = file => {try {return Number(fs.readFileSync(path.join(dir, file), 'utf8'));} catch (_) {return null;}};
    return {anonMiB: stat.anon / 1048576, fileMiB: stat.file / 1048576, currentMiB: read('memory.current') / 1048576, peakMiB: read('memory.peak') / 1048576};
}

function processType(cmdline) {
    const type = cmdline.match(/--type=([^\0 ]+)/)?.[1];
    if (!type) return cmdline.includes('crashpad') ? 'crashpad' : 'browser';
    if (type === 'utility') return `utility:${cmdline.match(/--utility-sub-type=([^\0 ]+)/)?.[1]?.split('.').slice(-1)[0] || '?'}`;
    if (type !== 'renderer') return type;
    if (cmdline.includes('--extension-process')) return 'renderer:extension';
    if (cmdline.includes('--top-chrome-webui')) return 'renderer:browser-ui';
    if (cmdline.includes('--app-path=') && !cmdline.includes('--enable-blink-features')) {
        // Both Fly's UI and extension backgrounds now run sandboxed. Their
        // command lines cannot reliably distinguish them; keep a combined bucket.
        return 'renderer:fly-ui-or-extension';
    }
    return 'renderer';
}

// PSS apportions shared pages, so the sum over processes does not double count them.
function pss(dir) {
    let totalKb = 0;
    const byType = {};
    const pids = fs.readFileSync(path.join(dir, 'cgroup.procs'), 'utf8').trim().split('\n').filter(Boolean);
    for (const pid of pids) {
        try {
            const kb = Number(fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8').match(/^Pss:\s+(\d+)/m)[1]);
            const type = processType(fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8'));
            totalKb += kb;
            byType[type] = byType[type] || {count: 0, pssMiB: 0};
            byType[type].count++;
            byType[type].pssMiB += kb / 1024;
        } catch (_) {}
    }
    return {pssMiB: totalKb / 1024, processes: pids.length, byType};
}

function snapshot(dir) {
    return {...pss(dir), cgroup: cgroupMemory(dir), cpuUsec: cpuUsec(dir)};
}

// Samples PSS in the background so a phase reports its peak, not just its end.
function peakSampler(dir, intervalMs = 500) {
    let peak = 0, stopped = false;
    const loop = (async () => {
        while (!stopped) {
            try {peak = Math.max(peak, pss(dir).pssMiB);} catch (_) {}
            await pause(intervalMs);
        }
    })();
    return async () => {stopped = true; await loop; return peak;};
}

// CPU ticks per process type, to attribute idle CPU. Processes that exit lose their
// ticks here; the cgroup total above is exact.
function ticksByType(dir) {
    const byType = {};
    for (const pid of fs.readFileSync(path.join(dir, 'cgroup.procs'), 'utf8').trim().split('\n').filter(Boolean)) {
        try {
            const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8').split(') ')[1].split(' ');
            const type = processType(fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8'));
            byType[type] = (byType[type] || 0) + Number(stat[11]) + Number(stat[12]);
        } catch (_) {}
    }
    return byType;
}

async function cpuOver(dir, ms) {
    const start = cpuUsec(dir), t = performance.now(), typesBefore = ticksByType(dir);
    await pause(ms);
    const used = cpuUsec(dir) - start, elapsed = performance.now() - t, typesAfter = ticksByType(dir);
    const byTypePercent = Object.fromEntries(Object.entries(typesAfter).map(([type, ticks]) => [type, (ticks - (typesBefore[type] || 0)) * 1000 / clockTicks / elapsed * 100]));
    return {cpuMs: used / 1000, cpuPercentOfOneCore: used / 1000 / elapsed * 100, byTypePercent};
}

function directorySize(dir) {
    try {return Number(execFileSync('du', ['-sb', dir], {encoding: 'utf8'}).split('\t')[0]) / 1048576;} catch (_) {return null;}
}

function freePort() {
    return new Promise(resolve => {
        const server = net.createServer().listen(0, '127.0.0.1', () => {const {port} = server.address(); server.close(() => resolve(port));});
    });
}

// LAUNCHING

let unitCounter = 0;

function flyWrapper(profile, cfg) {
    // Same isolation as benchmark-performance.js: the profile lives under a temporary
    // home without changing $HOME, so fontconfig and GTK caches stay shared with Chromium.
    const app = path.join(profile, 'app');
    fs.mkdirSync(app, {recursive: true});
    fs.writeFileSync(path.join(app, 'package.json'), JSON.stringify({name: 'fly-browser', version: require(path.join(root, 'package.json')).version, main: 'main.js'}));
    fs.writeFileSync(path.join(app, 'main.js'), `const {app}=require('electron');app.setAppPath(${JSON.stringify(root)});app.setPath('home',${JSON.stringify(profile)});app.setPath('userData',${JSON.stringify(path.join(profile, '.config/fly-browser'))});require(${JSON.stringify(path.join(root, 'main.js'))});`);
    return app;
}

async function launch(configName, profile, startUrl) {
    const cfg = CONFIGS[configName];
    const port = await freePort();
    const unit = `flybench-${process.pid}-${unitCounter++}`;
    let command, args;
    if (cfg.browser === 'chromium') {
        command = cfg.binary === 'cft' ? cftBinary : chromiumBinary;
        args = [`--user-data-dir=${path.join(profile, 'chromium')}`, '--no-first-run', '--no-default-browser-check', '--password-store=basic',
            `--remote-debugging-port=${port}`, `--window-size=${windowSize.join(',')}`, ...cfg.args];
        if (cfg.ubol) {
            const copy = path.join(profile, 'ubol');
            if (!fs.existsSync(copy)) fs.cpSync(ubol, copy, {recursive: true});
            args.push(`--load-extension=${copy}`);
        }
        args.push(startUrl);
    } else {
        const configDir = path.join(profile, '.config/fly-browser');
        fs.mkdirSync(configDir, {recursive: true});
        fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Bench', workspaces: {Bench: [startUrl]}}));
        const configPath = path.join(configDir, 'config.json');
        if (!fs.existsSync(configPath) && Object.keys(cfg.config).length) fs.writeFileSync(configPath, JSON.stringify(cfg.config));
        command = electronBinary;
        args = [flyWrapper(profile, cfg), `--remote-debugging-port=${port}`];
    }
    const started = performance.now();
    const child = spawn('systemd-run', ['--user', '--scope', '-q', `--unit=${unit}`, '--', command, ...args], {stdio: ['ignore', 'ignore', 'pipe']});
    let stderr = '';
    child.stderr.on('data', data => {stderr = (stderr + data).slice(-20000);});
    let dir;
    for (let i = 0; ; i++) {
        try {dir = cgroupDir(child.pid); if (dir.endsWith(`${unit}.scope`)) break;} catch (_) {}
        if (i > 200) throw new Error('Scope did not start');
        await pause(10);
    }
    let cdp;
    for (let i = 0; ; i++) {
        try {
            const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
            cdp = await Cdp.connect(version.webSocketDebuggerUrl);
            break;
        } catch (_) {
            if (i > 600 || child.exitCode !== null) throw new Error(`DevTools endpoint did not start\n${stderr}`);
            await pause(25);
        }
    }
    return {configName, cfg, child, unit, dir, cdp, started, stderr: () => stderr, profile};
}

async function close(instance) {
    const exited = new Promise(resolve => instance.child.once('exit', resolve));
    try {await instance.cdp.send('Browser.close', {}, undefined, 5000);} catch (_) {}
    instance.cdp.close();
    const timeout = pause(15000).then(() => 'timeout');
    if (await Promise.race([exited, timeout]) === 'timeout') {
        try {instance.child.kill('SIGTERM');} catch (_) {}
        if (await Promise.race([exited, pause(5000).then(() => 'timeout')]) === 'timeout') {
            try {execFileSync('systemctl', ['--user', 'kill', '-s', 'KILL', `${instance.unit}.scope`]);} catch (_) {}
            await exited;
        }
    }
    // Let the scope disappear before the next launch reuses CPU.
    await pause(1500);
}

// Attaches to the tab whose URL matches, returning its flattened session id.
async function attachTo(instance, predicate, timeoutMs = 60000) {
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
        const target = (await targets(instance.cdp)).find(t => (t.type === 'page' || t.type === 'webview') && predicate(t.url));
        if (target) {
            const {sessionId} = await instance.cdp.send('Target.attachToTarget', {targetId: target.targetId, flatten: true});
            return {sessionId, targetId: target.targetId};
        }
        await pause(25);
    }
    throw new Error(`No tab matched in ${timeoutMs} ms`);
}

async function waitFor(instance, sessionId, expression, timeoutMs = 60000, intervalMs = 25) {
    const deadline = performance.now() + timeoutMs;
    while (performance.now() < deadline) {
        try {const value = await evaluate(instance.cdp, sessionId, expression, 10000); if (value) return value;} catch (_) {}
        await pause(intervalMs);
    }
    return null;
}

async function hostSession(instance) {
    return attachTo(instance, url => url.startsWith('file://') && url.endsWith('/index.html'));
}

async function extensionTargets(instance) {
    // Fly's extension background pages are listed as plain pages.
    return (await targets(instance.cdp)).filter(t => t.url.startsWith('chrome-extension://') && /background|service_worker|^page$/.test(t.type)).map(t => ({type: t.type, url: t.url}));
}

async function gpuStatus(instance) {
    try {
        const info = await instance.cdp.send('SystemInfo.getInfo', {}, undefined, 10000);
        return {featureStatus: info.gpu.featureStatus, device: info.gpu.devices?.[0] && {vendor: info.gpu.devices[0].vendorString || info.gpu.devices[0].vendorId, device: info.gpu.devices[0].deviceString || info.gpu.devices[0].deviceId, driver: info.gpu.devices[0].driverVersion}};
    } catch (error) {return {error: error.message};}
}

// Opening a tab works the way a user would: a blank tab first, then a navigation. The
// session is attached before the navigation so every request is counted, including
// those in cross-site frames and requests an ad blocker cancels.
async function openTab(instance, host) {
    const before = new Set((await targets(instance.cdp)).map(t => t.targetId));
    if (instance.cfg.browser === 'chromium') {
        await instance.cdp.send('Target.createTarget', {url: 'about:blank'});
    } else {
        await evaluate(instance.cdp, host.sessionId, `import('./modules/webview.js').then(m => {m.spawnTabWithUrl('about:blank'); return true;})`);
    }
    for (let i = 0; i < 400; i++) {
        const target = (await targets(instance.cdp)).find(t => !before.has(t.targetId) && (t.type === 'page' || t.type === 'webview'));
        if (target) {
            const {sessionId} = await instance.cdp.send('Target.attachToTarget', {targetId: target.targetId, flatten: true});
            return {sessionId, targetId: target.targetId};
        }
        await pause(25);
    }
    throw new Error('New tab did not appear');
}

function networkCounter(instance) {
    const counts = new Map();
    const sessions = new Set();
    const off = instance.cdp.on(message => {
        if (!message.sessionId) return;
        if (message.method === 'Target.attachedToTarget') {
            const child = message.params.sessionId;
            sessions.add(child);
            counts.set(child, counts.get(message.sessionId));
            instance.cdp.send('Network.enable', {maxTotalBufferSize: 1, maxResourceBufferSize: 1}, child).catch(() => {});
            instance.cdp.send('Target.setAutoAttach', {autoAttach: true, waitForDebuggerOnStart: true, flatten: true}, child).catch(() => {});
            instance.cdp.send('Runtime.runIfWaitingForDebugger', {}, child).catch(() => {});
            return;
        }
        const bucket = counts.get(message.sessionId);
        if (!bucket) return;
        if (message.method === 'Network.requestWillBeSent') bucket.requests++;
        else if (message.method === 'Network.loadingFinished') {bucket.finished++; bucket.bytes += message.params.encodedDataLength || 0;}
        else if (message.method === 'Network.loadingFailed') {
            if (/BLOCKED_BY_CLIENT/.test(message.params.errorText) || message.params.blockedReason === 'inspector') bucket.blocked++;
            else bucket.failed++;
        }
    });
    return {
        async track(sessionId) {
            const bucket = {requests: 0, finished: 0, failed: 0, blocked: 0, bytes: 0};
            counts.set(sessionId, bucket);
            sessions.add(sessionId);
            await instance.cdp.send('Network.enable', {maxTotalBufferSize: 1, maxResourceBufferSize: 1}, sessionId);
            await instance.cdp.send('Target.setAutoAttach', {autoAttach: true, waitForDebuggerOnStart: true, flatten: true}, sessionId).catch(() => {});
            return bucket;
        },
        async stop() {
            off();
            await Promise.all([...sessions].map(sessionId => instance.cdp.send('Network.disable', {}, sessionId, 3000).catch(() => {})));
            await Promise.all([...sessions].map(sessionId => instance.cdp.send('Target.detachFromTarget', {sessionId}, undefined, 3000).catch(() => {})));
        }
    };
}

async function closeTabsExceptFirst(instance, host, tabTargetIds) {
    if (instance.cfg.browser === 'chromium') {
        for (const targetId of tabTargetIds) await instance.cdp.send('Target.closeTarget', {targetId}).catch(() => {});
    } else {
        await evaluate(instance.cdp, host.sessionId, `import('./modules/webview.js').then(async m => {
            const {state} = await import('./modules/state.js');
            const ws = state.sessionState.current_workspace;
            while (state.sessionState.workspaces[ws].length > 1) {
                m.switchTabFocus(state.sessionState.workspaces[ws].length - 1);
                m.handleTabRemoval(state.sessionState.workspaces[ws].length - 1);
                await new Promise(r => setTimeout(r, 50));
            }
            return state.sessionState.workspaces[ws].length;
        })`);
    }
}

// SCENARIOS

function fixtureServer() {
    const page = '<!doctype html><title>Benchmark fixture</title><main id="fixture"><h1>Local page</h1><p>Deterministic local content.</p><input></main><div style="height:4000px"></div>';
    const videoPage = '<!doctype html><title>Video</title><body style="margin:0;background:#000"><video id="v" src="/video.webm" autoplay muted loop playsinline style="width:100vw;height:100vh"></video>';
    const server = http.createServer((request, response) => {
        if (request.url === '/video.webm') {
            const stat = fs.statSync(video);
            const range = /bytes=(\d+)-(\d*)/.exec(request.headers.range || '');
            if (range) {
                const start = Number(range[1]), end = range[2] ? Number(range[2]) : stat.size - 1;
                response.writeHead(206, {'Content-Type': 'video/webm', 'Accept-Ranges': 'bytes', 'Content-Range': `bytes ${start}-${end}/${stat.size}`, 'Content-Length': end - start + 1});
                fs.createReadStream(video, {start, end}).pipe(response);
            } else {
                response.writeHead(200, {'Content-Type': 'video/webm', 'Accept-Ranges': 'bytes', 'Content-Length': stat.size});
                fs.createReadStream(video).pipe(response);
            }
            return;
        }
        response.setHeader('Content-Type', 'text/html');
        response.end(request.url.startsWith('/video') ? videoPage : page);
    });
    return new Promise(resolve => server.listen(0, '127.0.0.1', () => resolve(server)));
}

// Cold launch on an empty profile, then a warm relaunch of the same profile that goes
// on to browse real sites. Covers start-up, idle, browsing, many tabs and tab closing.
async function browseScenario(configName, round, base) {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-vs-'));
    const result = {scenario: 'browse', config: configName, round};
    const fixture = `${base}/fixture`;
    const ready = 'document.getElementById("fixture") && document.readyState === "complete"';
    try {
        for (const phase of ['cold', 'warm']) {
            const instance = await launch(configName, profile, fixture);
            const stopPeak = peakSampler(instance.dir, 250);
            const tab = await attachTo(instance, url => url.startsWith(fixture));
            if (!await waitFor(instance, tab.sessionId, ready)) throw new Error('Fixture did not load');
            const readyMs = performance.now() - instance.started;
            const cpuAtReady = cpuUsec(instance.dir);
            await pause(15000);
            const settled = snapshot(instance.dir);
            const startupPeakPssMiB = await stopPeak();
            const idle = await cpuOver(instance.dir, idleMs);
            const idleSnapshot = snapshot(instance.dir);
            const data = {readyMs, cpuToReadyMs: cpuAtReady / 1000, cpuFirst15sAfterReadyMs: (settled.cpuUsec - cpuAtReady) / 1000, startupPeakPssMiB, settled, idle, idleSnapshot,
                gpu: await gpuStatus(instance), extensions: await extensionTargets(instance),
                viewport: await evaluate(instance.cdp, tab.sessionId, '({width: innerWidth, height: innerHeight, dpr: devicePixelRatio})')};
            await instance.cdp.send('Target.detachFromTarget', {sessionId: tab.sessionId}).catch(() => {});
            if (phase === 'warm') {
                const host = instance.cfg.browser === 'fly' ? await hostSession(instance) : null;
                const counter = networkCounter(instance);
                const stopBrowsePeak = peakSampler(instance.dir, 500);
                const browseCpuStart = cpuUsec(instance.dir), browseStart = performance.now();
                const sites = [], tabIds = [];
                for (const site of SITES) {
                    const newTab = await openTab(instance, host);
                    tabIds.push(newTab.targetId);
                    const bucket = await counter.track(newTab.sessionId);
                    const loadStart = performance.now();
                    await instance.cdp.send('Page.enable', {}, newTab.sessionId).catch(() => {});
                    await instance.cdp.send('Page.navigate', {url: site}, newTab.sessionId).catch(() => {});
                    const loaded = await waitFor(instance, newTab.sessionId, `location.href.startsWith('http') && document.readyState === 'complete'`, 30000, 100);
                    const loadMs = loaded ? performance.now() - loadStart : null;
                    await pause(3000);
                    sites.push({site, loadMs, atLoadPlus3s: {...bucket}, bucket});
                }
                await pause(20000);
                const browseCpuMs = (cpuUsec(instance.dir) - browseCpuStart) / 1000;
                const browseWallMs = performance.now() - browseStart;
                const browsePeakPssMiB = await stopBrowsePeak();
                await counter.stop();
                const tabsSettled = snapshot(instance.dir);
                const tabsIdle = await cpuOver(instance.dir, idleMs);
                const tabsIdleSnapshot = snapshot(instance.dir);
                let suspended = null;
                if (host) {
                    await evaluate(instance.cdp, host.sessionId, `import('./modules/tabSuspension.js').then(async m => {await m.suspendInactiveTabs(); return true;})`);
                    await pause(10000);
                    suspended = snapshot(instance.dir);
                }
                await closeTabsExceptFirst(instance, host, tabIds);
                await pause(10000);
                const afterClose = snapshot(instance.dir);
                Object.assign(data, {sites: sites.map(({bucket, ...rest}) => ({...rest, total: {...bucket}})), browseCpuMs, browseWallMs, browsePeakPssMiB, tabsSettled, tabsIdle, tabsIdleSnapshot, suspended, afterClose});
            }
            result[phase] = data;
            await close(instance);
            result[`${phase}ProfileMiB`] = directorySize(profile);
            log(configName, round, phase, data);
        }
    } catch (error) {
        result.error = String(error.stack || error);
        console.error(configName, round, error);
        try {execFileSync('sh', ['-c', `systemctl --user kill -s KILL 'flybench-${process.pid}-*' 2>/dev/null; true`]);} catch (_) {}
    } finally {
        fs.rmSync(profile, {recursive: true, force: true});
    }
    return result;
}

async function videoScenario(configName, round, base) {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-vs-'));
    const result = {scenario: 'video', config: configName, round};
    try {
        // The video opens after start-up has settled, as a user would open one. A page
        // restored during start-up is a different case (see the report).
        const instance = await launch(configName, profile, `${base}/fixture`);
        try {
            const tab = await attachTo(instance, url => url.startsWith(`${base}/fixture`));
            await pause(15000);
            await evaluate(instance.cdp, tab.sessionId, `location.href = '${base}/video'`);
            if (!await waitFor(instance, tab.sessionId, 'document.getElementById("v")?.currentTime > 3', 60000, 100)) {
                const state = await evaluate(instance.cdp, tab.sessionId, `(() => {const v=document.getElementById('v');return v && {readyState:v.readyState,networkState:v.networkState,error:v.error&&v.error.code,paused:v.paused,time:v.currentTime,hidden:document.hidden};})()`).catch(error => error.message);
                throw new Error(`Video did not play: ${JSON.stringify(state)} ${JSON.stringify((await targets(instance.cdp)).map(t => [t.type, t.url]))}`);
            }
            await pause(5000);
            const quality = `(() => {const v=document.getElementById('v');const q=v.getVideoPlaybackQuality();return {total:q.totalVideoFrames,dropped:q.droppedVideoFrames,time:v.currentTime,w:v.videoWidth,h:v.videoHeight};})()`;
            const before = await evaluate(instance.cdp, tab.sessionId, quality);
            const cpu = await cpuOver(instance.dir, 20000);
            const after = await evaluate(instance.cdp, tab.sessionId, quality);
            Object.assign(result, {cpu, frames: after.total - before.total, droppedFrames: after.dropped - before.dropped, resolution: `${after.w}x${after.h}`, snapshot: snapshot(instance.dir), gpu: await gpuStatus(instance)});
            await instance.cdp.send('Target.detachFromTarget', {sessionId: tab.sessionId}).catch(() => {});
        } finally {await close(instance);}
        log(configName, round, 'video', result);
    } catch (error) {
        result.error = String(error.stack || error);
        console.error(configName, round, error);
    } finally {fs.rmSync(profile, {recursive: true, force: true});}
    return result;
}

async function speedometerScenario(configName, round) {
    const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-vs-'));
    const result = {scenario: 'speedometer', config: configName, round};
    const url = `https://browserbench.org/Speedometer3.1/?startAutomatically${process.env.FLY_VS_SPEEDOMETER_ITERATIONS ? `&iterationCount=${process.env.FLY_VS_SPEEDOMETER_ITERATIONS}` : ''}`;
    try {
        const instance = await launch(configName, profile, url);
        try {
            const tab = await attachTo(instance, u => u.startsWith('https://browserbench.org/Speedometer3.1'));
            const score = await waitFor(instance, tab.sessionId, `(() => {const n=document.querySelector('#result-number');return location.hash === '#summary' && n && n.textContent.trim() ? n.textContent.trim() : null;})()`, 15 * 60000, 2000);
            const detail = await evaluate(instance.cdp, tab.sessionId, `(() => ({score: document.querySelector('#result-number')?.textContent.trim(), error: document.querySelector('#confidence-number')?.textContent.trim(), hash: location.hash}))()`).catch(() => null);
            Object.assign(result, {score: score ? Number(score) : null, detail, viewport: await evaluate(instance.cdp, tab.sessionId, '({width: innerWidth, height: innerHeight})').catch(() => null)});
        } finally {await close(instance);}
        log(configName, round, 'speedometer', result);
    } catch (error) {
        result.error = String(error.stack || error);
        console.error(configName, round, error);
    } finally {fs.rmSync(profile, {recursive: true, force: true});}
    return result;
}

function log(configName, round, phase, data) {
    const brief = {config: configName, round, phase};
    if (data.readyMs) Object.assign(brief, {readyMs: Math.round(data.readyMs), pss: Math.round(data.settled.pssMiB), idleCpu: data.idle.cpuPercentOfOneCore.toFixed(2), procs: data.settled.processes});
    if (data.tabsSettled) Object.assign(brief, {tabsPss: Math.round(data.tabsSettled.pssMiB), tabsProcs: data.tabsSettled.processes, browseCpuS: (data.browseCpuMs / 1000).toFixed(1), tabsIdleCpu: data.tabsIdle.cpuPercentOfOneCore.toFixed(2), blocked: data.sites.reduce((s, x) => s + x.total.blocked, 0), requests: data.sites.reduce((s, x) => s + x.total.requests, 0)});
    if (data.cpu) Object.assign(brief, {videoCpu: data.cpu.cpuPercentOfOneCore.toFixed(1), dropped: data.droppedFrames, frames: data.frames});
    if (data.score !== undefined) Object.assign(brief, {score: data.score});
    console.log(JSON.stringify(brief));
}

function environment() {
    const chromiumVersion = execFileSync(chromiumBinary, ['--version'], {encoding: 'utf8'}).trim();
    const electron = require(path.join(root, 'node_modules/electron/package.json')).version;
    const electronChrome = execFileSync(electronBinary, ['-p', 'process.versions.chrome'], {encoding: 'utf8', env: {...process.env, ELECTRON_RUN_AS_NODE: '1'}}).trim();
    let gitHead = null;
    try {gitHead = execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8'}).trim();} catch (_) {}
    return {date: new Date().toISOString(), fly: require(path.join(root, 'package.json')).version, gitHead, electron, electronChrome, chromium: chromiumVersion,
        chromeForTesting: cftBinary ? execFileSync(cftBinary, ['--version'], {encoding: 'utf8'}).trim() : null,
        ubol: ubol ? JSON.parse(fs.readFileSync(path.join(ubol, 'manifest.json'), 'utf8')).version : null,
        kernel: os.release(), cpu: os.cpus()[0].model, cores: os.cpus().length, memoryGiB: os.totalmem() / 2 ** 30,
        display: process.env.DISPLAY || null, session: process.env.XDG_SESSION_TYPE || null, desktop: process.env.XDG_CURRENT_DESKTOP || null,
        windowSize, idleMs, rounds, speedometerRounds, sites: SITES, configs: configNames, scenarios};
}

// REPORT: node scripts/benchmark-vs-chromium.js --report results.json
// Prints Markdown tables of medians with [min–max] across rounds.
function report(file) {
    const data = JSON.parse(fs.readFileSync(file, 'utf8'));
    const names = data.environment.configs;
    const median = values => {const v = values.filter(x => x != null && Number.isFinite(x)).sort((a, b) => a - b); if (!v.length) return null; const m = v.length >> 1; return v.length % 2 ? v[m] : (v[m - 1] + v[m]) / 2;};
    const cell = (values, digits) => {
        const v = values.filter(x => x != null && Number.isFinite(x));
        if (!v.length) return '–';
        const f = x => x.toFixed(digits);
        return v.length === 1 ? f(v[0]) : `${f(median(v))} [${f(Math.min(...v))}–${f(Math.max(...v))}]`;
    };
    const table = (title, scenario, rows, configs = names) => {
        const samples = configs.map(name => data.samples.filter(s => s.scenario === scenario && s.config === name && !s.error));
        if (!samples.some(list => list.length)) return;
        console.log(`\n### ${title}\n`);
        console.log(`| Metric | ${configs.join(' | ')} |`);
        console.log(`| --- | ${configs.map(() => '---:').join(' | ')} |`);
        for (const [label, pick, digits = 1] of rows) {
            const cells = samples.map(list => cell(list.map(s => {try {return pick(s);} catch (_) {return null;}}), digits));
            if (cells.every(c => c === '–')) continue;
            console.log(`| ${label} | ${cells.join(' | ')} |`);
        }
    };
    const sum = (s, key) => s.warm.sites.reduce((total, site) => total + site.total[key], 0);
    const failures = data.samples.filter(s => s.error);
    console.log(`Samples: ${data.samples.length}, failed: ${failures.length}${failures.length ? ` (${failures.map(s => `${s.scenario}/${s.config}/r${s.round}`).join(', ')})` : ''}`);
    table('Cold start, empty profile, one local tab', 'browse', [
        ['Window to local page loaded, ms', s => s.cold.readyMs, 0],
        ['CPU used until loaded, s', s => s.cold.cpuToReadyMs / 1000, 2],
        ['CPU in the 15 s after loading, s', s => s.cold.cpuFirst15sAfterReadyMs / 1000, 2],
        ['Sampled peak PSS during start-up, MiB', s => s.cold.startupPeakPssMiB, 0],
        ['PSS 15 s after loading, MiB', s => s.cold.settled.pssMiB, 0],
        ['Processes', s => s.cold.settled.processes, 0],
        [`Idle CPU over ${data.environment.idleMs / 1000} s, % of one core`, s => s.cold.idle.cpuPercentOfOneCore, 2],
        ['Profile on disk after first run, MiB', s => s.coldProfileMiB, 1]
    ]);
    table('Warm start, same profile relaunched, one local tab', 'browse', [
        ['Window to local page loaded, ms', s => s.warm.readyMs, 0],
        ['CPU used until loaded, s', s => s.warm.cpuToReadyMs / 1000, 2],
        ['Sampled peak PSS during start-up, MiB', s => s.warm.startupPeakPssMiB, 0],
        ['PSS 15 s after loading, MiB', s => s.warm.settled.pssMiB, 0],
        ['Anonymous memory (cgroup), MiB', s => s.warm.settled.cgroup.anonMiB, 0],
        ['Processes', s => s.warm.settled.processes, 0],
        [`Idle CPU over ${data.environment.idleMs / 1000} s, % of one core`, s => s.warm.idle.cpuPercentOfOneCore, 2]
    ]);
    table(`Browsing ${data.environment.sites.length} real sites, one tab each`, 'browse', [
        ['CPU time for the whole browse, s', s => s.warm.browseCpuMs / 1000, 1],
        ['Median page load (navigate to load event), ms', s => median(s.warm.sites.map(x => x.loadMs)), 0],
        ['Pages that reached load within 30 s', s => s.warm.sites.filter(x => x.loadMs != null).length, 0],
        ['Requests started', s => sum(s, 'requests'), 0],
        ['Requests blocked by the client', s => sum(s, 'blocked'), 0],
        ['Other failed requests', s => sum(s, 'failed'), 0],
        ['Data received, MiB', s => sum(s, 'bytes') / 1048576, 1],
        ['Sampled peak PSS while browsing, MiB', s => s.warm.browsePeakPssMiB, 0],
        ['PSS with all tabs open (20 s settle), MiB', s => s.warm.tabsSettled.pssMiB, 0],
        ['Anonymous memory (cgroup), MiB', s => s.warm.tabsSettled.cgroup.anonMiB, 0],
        ['Processes', s => s.warm.tabsSettled.processes, 0],
        [`Idle CPU with all tabs open, % of one core`, s => s.warm.tabsIdle.cpuPercentOfOneCore, 2],
        ['PSS after Fly background-tab policy, MiB', s => (s.warm.suspended || s.warm.hibernated).pssMiB, 0],
        ['PSS 10 s after closing all but one tab, MiB', s => s.warm.afterClose.pssMiB, 0],
        ['Processes after closing', s => s.warm.afterClose.processes, 0],
        ['Profile on disk after browsing, MiB', s => s.warmProfileMiB, 1]
    ]);
    const types = [...new Set(data.samples.filter(s => s.scenario === 'browse' && !s.error).flatMap(s => Object.keys(s.warm.tabsSettled.byType)))].sort();
    table('PSS by process type with all tabs open, MiB', 'browse', types.map(type => [type, s => s.warm.tabsSettled.byType[type]?.pssMiB ?? 0, 0]));
    table('Process count by type with all tabs open', 'browse', types.map(type => [type, s => s.warm.tabsSettled.byType[type]?.count ?? 0, 0]));
    table('Local 1080p60 VP9 video, 20 s of playback', 'video', [
        ['CPU, % of one core', s => s.cpu.cpuPercentOfOneCore, 1],
        ['Frames presented', s => s.frames, 0],
        ['Frames dropped', s => s.droppedFrames, 0],
        ['PSS while playing, MiB', s => s.snapshot.pssMiB, 0]
    ]);
    table('Speedometer 3.1 (higher is better)', 'speedometer', [['Score', s => s.score, 2]]);
}

if (process.argv[2] === '--report' && require.main === module) {report(process.argv[3]); return;}
// Required as a module, it exposes the helpers for one-off investigations.
if (require.main !== module) {module.exports = {launch, close, attachTo, waitFor, evaluate, targets, hostSession, fixtureServer, snapshot, cpuOver}; return;}

(async () => {
    const env = environment();
    const samples = [];
    const save = () => fs.writeFileSync(output, JSON.stringify({environment: env, configs: Object.fromEntries(configNames.map(n => [n, CONFIGS[n]])), samples}, null, 2) + '\n');
    const server = await fixtureServer();
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
        const order = round => round % 2 ? [...configNames].reverse() : configNames;
        if (scenarios.includes('browse')) for (let round = 0; round < rounds; round++) for (const name of order(round)) {samples.push(await browseScenario(name, round, base)); save();}
        if (scenarios.includes('video')) for (let round = 0; round < rounds; round++) for (const name of order(round)) {samples.push(await videoScenario(name, round, base)); save();}
        if (scenarios.includes('speedometer')) for (let round = 0; round < speedometerRounds; round++) for (const name of order(round)) {samples.push(await speedometerScenario(name, round)); save();}
    } finally {server.close();}
    save();
})().catch(error => {console.error(error); process.exitCode = 1;});
