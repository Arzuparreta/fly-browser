// Optional Linux/Electron comparison. Normal launches load no instrumentation.
const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const { execFileSync } = require('node:child_process');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');

const roots = process.argv.slice(2).map(root => path.resolve(root));
if (roots.length < 2) throw new Error('Supply at least two source directories, each with node_modules/electron.');
const rounds = Number(process.env.MISE_BENCH_ROUNDS || 3);
const idleMs = Number(process.env.MISE_BENCH_IDLE_MS || 5000);
const output = process.env.MISE_BENCH_OUTPUT || '/tmp/mise-performance.json';
const samples = [];
const filterCacheSha256 = process.env.MISE_BENCH_CACHE ? crypto.createHash('sha256').update(fs.readFileSync(process.env.MISE_BENCH_CACHE)).digest('hex') : null;
const sources = roots.map(root => {
    const files = fs.readdirSync(root).filter(name => /\.(js|html|css)$/.test(name));
    const visit = directory => {for(const entry of fs.readdirSync(path.join(root, directory), {withFileTypes: true})) {const relative=path.join(directory, entry.name);if(entry.isDirectory()) visit(relative);else if(relative.endsWith('.js')) files.push(relative);}};
    visit('modules');
    const hash = crypto.createHash('sha256');
    for(const file of files.sort()) hash.update(file).update('\0').update(fs.readFileSync(path.join(root,file)));
    return {root, sourceSha256: hash.digest('hex'), electron: require(path.join(root, 'node_modules/electron/package.json')).version};
});
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

// Include zygotes and utility processes as well as Electron WebContents. PSS
// apportions shared pages; summing RSS would count those pages more than once.
function processes(rootPid) {
    const all = new Map();
    for (const name of fs.readdirSync('/proc').filter(name => /^\d+$/.test(name))) {
        try {
            const stat = fs.readFileSync(`/proc/${name}/stat`, 'utf8').split(') ')[1].split(' ');
            all.set(Number(name), {ppid: Number(stat[1]), ticks: Number(stat[11]) + Number(stat[12])});
        } catch (_) {}
    }
    const pids = new Set([rootPid]);
    let changed = true;
    while (changed) {
        changed = false;
        for (const [pid, stat] of all) if (!pids.has(pid) && pids.has(stat.ppid)) { pids.add(pid); changed = true; }
    }
    let pssKb = 0, ticks = 0;
    const members = [];
    for (const pid of pids) {
        ticks += all.get(pid)?.ticks || 0;
        try { const kb = Number(fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8').match(/^Pss:\s+(\d+)/m)[1]); pssKb += kb; const cmd = fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8'); members.push({type: cmd.match(/--type=([^ \0]+)/)?.[1] || 'browser', pssMiB: kb/1024}); } catch (_) {}
    }
    return {pssMiB: pssKb / 1024, ticks, processes: pids.size, members};
}

async function run(root, round, url) {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-perf-'));
    const configDir = path.join(temporary, '.config/mise-browser');
    fs.mkdirSync(configDir, {recursive: true});
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Bench', workspaces: {Bench: [url]}}));
    // All other preferences use the revision's clean defaults, including opaque colours.
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({last_seen_version: '0.15.0'}));
    if (process.env.MISE_BENCH_CACHE) {
        fs.mkdirSync(path.join(temporary, 'data'), {recursive: true});
        fs.copyFileSync(process.env.MISE_BENCH_CACHE, path.join(temporary, 'data/adblock_cache.bin'));
    }
    fs.writeFileSync(path.join(temporary, 'package.json'), JSON.stringify({name: 'mise-performance-fixture', version: require(path.join(root, 'package.json')).version, main: 'main.js'}));
    const entry = path.join(temporary, 'main.js');
    fs.writeFileSync(entry, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))}); app.commandLine.appendSwitch('proxy-server','http://127.0.0.1:9'); app.commandLine.appendSwitch('proxy-bypass-list','<-loopback>;127.0.0.1'); globalThis.__miseBenchFetches=0; const originalFetch=globalThis.fetch; globalThis.fetch=(...args)=>{globalThis.__miseBenchFetches++; return originalFetch(...args);}; globalThis.__miseBenchRequires=()=>Object.keys(require.cache); globalThis.__miseBenchMessages={}; app.on('web-contents-created',(_event,wc)=>wc.on('ipc-message',(_event,channel)=>{globalThis.__miseBenchMessages[channel]=(globalThis.__miseBenchMessages[channel]||0)+1;})); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    let application;
    try {
        const start = performance.now();
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', ...(process.env.MISE_BENCH_GPU === '1' ? [] : ['--disable-gpu']), '--ozone-platform=x11', temporary]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => typeof window.togglePreferencesView === 'function' && document.querySelectorAll('#TabList li').length === 1).catch(async error => {
            console.error({errors, windows: application.windows().map(window => window.url()), title: await page.title(), tabs: await page.locator('#TabList li').count()});
            throw error;
        });
        const guest = script => page.evaluate(script => document.querySelector('webview').executeJavaScript(script), script);
        for (let i = 0; ; i++) {
            if (await guest('!!document.getElementById("fixture")').catch(() => false)) break;
            if (i === 300) throw new Error('Fixture did not load');
            await pause(25);
        }
        const readyMs = performance.now() - start;
        const viewport = await page.evaluate(() => ({width: innerWidth, height: innerHeight, screenWidth: screen.width, screenHeight: screen.height}));
        for (let i = 0; !fs.existsSync(path.join(temporary, 'data/adblock_cache.bin')); i++) {
            if (i === 600) throw new Error('Filter cache did not initialise');
            await pause(50);
        }
        await pause(2000);
        const pid = application.process().pid;
        const cdp = await page.context().newCDPSession(page);
        await cdp.send('Performance.enable');
        const before = processes(pid);
        const idleStart = performance.now();
        await pause(idleMs);
        const after = processes(pid);
        assert.equal(await page.locator('#TabList li').count(), 1, 'The launcher must not introduce bootstrap or release-note tabs');
        assert.equal(await page.locator('webview').count(), 1);
        const idleCpuMs = (after.ticks - before.ticks) * 1000 / Number(execFileSync('getconf', ['CLK_TCK'], {encoding: 'utf8'}));
        const idleCpuPercent = idleCpuMs / (performance.now() - idleStart) * 100;
        const dom = await cdp.send('Memory.getDOMCounters');
        const memory = Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(m => [m.name, m.value]));
        const gpuFeatureStatus = await application.evaluate(({app}) => app.getGPUFeatureStatus());
        const gpuInfo = process.env.MISE_BENCH_GPU === '1' ? await application.evaluate(({app}) => app.getGPUInfo('basic')) : null;
        const filterFetches = await application.evaluate(() => globalThis.__miseBenchFetches);
        const heavyExtensionModule = await application.evaluate(() => globalThis.__miseBenchRequires().some(file => file.includes('/electron-chrome-extensions/')));
        const preferences = {};
        for (const rate of [1, 6]) {
            await cdp.send('Emulation.setCPUThrottlingRate', {rate});
            preferences[rate] = await page.evaluate(async () => {
                const timings = [];
                for (let i = 0; i < 12; i++) {
                    const start = performance.now();
                    const opened = window.togglePreferencesView();
                    document.getElementById('PreferencesOverlay').getBoundingClientRect();
                    timings.push(performance.now() - start);
                    await opened;
                    await new Promise(resolve => requestAnimationFrame(resolve));
                    await window.togglePreferencesView();
                    await new Promise(resolve => requestAnimationFrame(resolve));
                }
                return timings;
            });
        }
        await cdp.send('Emulation.setCPUThrottlingRate', {rate: 1});
        // Exercise page churn in separate tasks, so MutationObservers actually run.
        await page.evaluate(() => {window.__benchHintsMessages = 0; document.querySelector('webview').addEventListener('ipc-message', event => {if(event.channel === 'guest-hints-state') window.__benchHintsMessages++;});});
        const messagesBefore = await application.evaluate(() => ({...globalThis.__miseBenchMessages}));
        const mutationMs = await guest(`(async () => { const start = performance.now(); for(let i=0;i<150;i++){ const n=document.createElement('div');document.body.append(n);n.remove();await new Promise(r=>setTimeout(r,0)); } return performance.now()-start; })()`);
        const hintsMessages = await page.evaluate(() => window.__benchHintsMessages);
        await guest("{const layer=document.createElement('div');layer.id='mise-hint-layer';document.documentElement.append(layer);}");
        await pause(40);
        await guest("document.getElementById('mise-hint-layer').remove()");
        await pause(40);
        const hintsTransitions = await page.evaluate(() => window.__benchHintsMessages) - hintsMessages;
        const messagesAfter = await application.evaluate(() => ({...globalThis.__miseBenchMessages}));
        const tabs = await page.evaluate(async url => {
            const {spawnTabWithUrl, switchTabFocus, hibernateInactiveTabs} = await import('./modules/webview.js');
            const times = [];
            for (let i = 0; i < 5; i++) {
                const start = performance.now(); spawnTabWithUrl(url);
                document.getElementById('TabList').getBoundingClientRect();
                times.push(performance.now() - start);
                await new Promise(resolve => setTimeout(resolve, 150));
            }
            const switches = [];
            for (let i = 0; i < 20; i++) {
                const start = performance.now(); switchTabFocus(i % 6);
                document.getElementById('TabList').getBoundingClientRect();
                switches.push(performance.now() - start);
                await new Promise(resolve => setTimeout(resolve, 65));
            }
            return {createMs: times, switchMs: switches};
        }, url);
        await pause(1000);
        const live = processes(pid);
        await cdp.send('Emulation.setCPUThrottlingRate', {rate: 6});
        const slowSwitchMs = await page.evaluate(async () => {
            const {switchTabFocus} = await import('./modules/webview.js');
            const times = [];
            for(let i=0;i<20;i++) {const start=performance.now();switchTabFocus(i%6);document.getElementById('TabList').getBoundingClientRect();times.push(performance.now()-start);await new Promise(resolve=>setTimeout(resolve,65));}
            return times;
        });
        await cdp.send('Emulation.setCPUThrottlingRate', {rate: 1});
        await page.evaluate(async () => {const {hibernateInactiveTabs} = await import('./modules/webview.js'); await hibernateInactiveTabs();});
        await pause(1000);
        const sleeping = processes(pid);
        const views = await page.locator('webview').count();
        assert.equal(views, 1, 'Hibernation must leave only the selected view');
        assert.equal(heavyExtensionModule, false, 'Disabled extensions must not load the compatibility library');
        assert.deepEqual(errors, []);
        const result = {root, round, viewport, readyMs, idleCpuMs, idleCpuPercent, settled: before, idle: after, hostHeapMiB: memory.JSHeapUsedSize / 1048576, dom, preferences, mutationMs, hintsMessages, hintsTransitions, messagesBefore, messagesAfter, tabs, slowSwitchMs, live, sleeping, views, filterFetches, gpuFeatureStatus, gpuInfo, heavyExtensionModule};
        samples.push(result);
        fs.writeFileSync(output, JSON.stringify({sources, environment: {date: new Date().toISOString(), node: process.version, platform: os.release(), display: process.env.DISPLAY || null, displayDescription: process.env.MISE_BENCH_DISPLAY_DESCRIPTION || 'unspecified X11 display', cpu: os.cpus()[0].model, memoryGiB: os.totalmem()/2**30, idleMs, rounds, seededFilterCache: !!process.env.MISE_BENCH_CACHE, filterCacheSha256, gpu: process.env.MISE_BENCH_GPU === '1'}, samples}, null, 2) + '\n');
        console.log(JSON.stringify({root, round, readyMs, idleCpuPercent, idlePssMiB: after.pssMiB, hostHeapMiB: result.hostHeapMiB, nodes: dom.nodes, filterFetches, hintsMessages, hintsTransitions}));
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
}

(async () => {
    const server = http.createServer((_request, response) => {response.setHeader('Content-Type', 'text/html'); response.end('<!doctype html><title>Performance fixture</title><main id="fixture"><input style="margin-left:300px"><p>Local deterministic page</p></main><div style="height:4000px"></div>');});
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    try {
        for (let round = 0; round < rounds; round++) {
            // Reverse each round to reduce a systematic warm-cache/order advantage.
            for (const root of round % 2 ? [...roots].reverse() : roots) await run(root, round, `http://127.0.0.1:${server.address().port}`);
        }
    } finally { server.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
