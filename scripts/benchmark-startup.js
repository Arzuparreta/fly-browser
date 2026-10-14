// Optional cold-start probe: distinguish a visible local page from a protected one.
const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const roots = process.argv.slice(2).map(root => path.resolve(root));
if (roots.length !== 2) throw new Error('Supply the upstream and fork source directories.');
const rounds = Number(process.env.FLY_BENCH_ROUNDS || 5);
const output = process.env.FLY_BENCH_OUTPUT || '/tmp/fly-startup.json';
const samples = [];
const sources = roots.map(root => {
    const files = fs.readdirSync(root).filter(name => /\.(js|html|css)$/.test(name));
    const visit = directory => { for (const entry of fs.readdirSync(path.join(root, directory), {withFileTypes: true})) { const relative = path.join(directory, entry.name); if (entry.isDirectory()) visit(relative); else if (relative.endsWith('.js')) files.push(relative); } };
    visit('modules');
    const hash = require('node:crypto').createHash('sha256');
    for (const file of files.sort()) hash.update(file).update('\0').update(fs.readFileSync(path.join(root, file)));
    return {root, sourceSha256: hash.digest('hex'), electron: require(path.join(root, 'node_modules/electron/package.json')).version};
});

function treePss(rootPid) {
    const all = new Map();
    for (const name of fs.readdirSync('/proc').filter(name => /^\d+$/.test(name))) {
        try {all.set(Number(name), Number(fs.readFileSync(`/proc/${name}/stat`, 'utf8').split(') ')[1].split(' ')[1]));} catch (_) {}
    }
    const pids = new Set([rootPid]);
    let changed = true;
    while (changed) {changed = false; for (const [pid, parent] of all) if (!pids.has(pid) && pids.has(parent)) {pids.add(pid); changed = true;}}
    let kb = 0;
    for (const pid of pids) {try {kb += Number(fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8').match(/^Pss:\s+(\d+)/m)[1]);} catch (_) {}}
    return kb / 1024;
}

(async () => {
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Startup fixture</title><main id="fixture">Local fixture</main>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    try {
        for (let round = 0; round < rounds; round++) for (const root of round % 2 ? [...roots].reverse() : roots) {
            const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-startup-'));
            const configDir = path.join(temporary, '.config/fly-browser');
            fs.mkdirSync(configDir, {recursive: true});
            const url = `http://127.0.0.1:${server.address().port}`;
            const version = require(path.join(root, 'package.json')).version;
            fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Bench', workspaces: {Bench: [url]}}));
            fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({last_seen_version: version}));
            fs.writeFileSync(path.join(temporary, 'package.json'), JSON.stringify({name: 'fly-startup-fixture', version, main: 'main.js'}));
            fs.writeFileSync(path.join(temporary, 'main.js'), `const {app}=require('electron');
app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))});
app.commandLine.appendSwitch('proxy-server','http://127.0.0.1:9'); app.commandLine.appendSwitch('proxy-bypass-list','<-loopback>;127.0.0.1');
globalThis.__benchProtected=new WeakSet(); globalThis.__benchFetches=0;
const originalFetch=globalThis.fetch; globalThis.fetch=(...args)=>{globalThis.__benchFetches++;return originalFetch(...args);};
const {ElectronBlocker}=require(${JSON.stringify(require.resolve('@ghostery/adblocker-electron'))});
const originalEnable=ElectronBlocker.prototype.enableBlockingInSession;
ElectronBlocker.prototype.enableBlockingInSession=function(session){const result=originalEnable.call(this,session);globalThis.__benchProtected.add(session);return result;};
require(${JSON.stringify(path.join(root, 'main.js'))});`);
            let application, memoryTimer, peakPssMiB = 0, memorySamples = 0;
            try {
                const start = performance.now();
                application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--ozone-platform=x11', temporary]});
                const sampleMemory = () => {peakPssMiB = Math.max(peakPssMiB, treePss(application.process().pid)); memorySamples++;};
                sampleMemory(); memoryTimer = setInterval(sampleMemory, 100);
                const page = await application.firstWindow();
                await page.waitForFunction(() => typeof window.togglePreferencesView === 'function' && document.querySelectorAll('#TabList li').length === 1);
                for (let attempt = 0; ; attempt++) {
                    const ready = await page.evaluate(() => document.querySelector('webview')?.executeJavaScript('!!document.getElementById("fixture")')).catch(() => false);
                    if (ready) break;
                    if (attempt === 500) throw new Error('Local page did not become ready');
                    await page.waitForTimeout(20);
                }
                const localReadyMs = performance.now() - start;
                for (let attempt = 0; ; attempt++) {
                    const protectedGuest = await application.evaluate(({webContents}) => webContents.getAllWebContents().some(contents => contents.getType() === 'webview' && globalThis.__benchProtected.has(contents.session)));
                    if (protectedGuest) break;
                    if (attempt === 1000) throw new Error('Guest filtering did not attach');
                    await page.waitForTimeout(20);
                }
                const protectedReadyMs = performance.now() - start;
                sampleMemory(); clearInterval(memoryTimer);
                const filterFetches = await application.evaluate(() => globalThis.__benchFetches);
                const sample = {root, round, localReadyMs, protectedReadyMs, filterFetches, peakPssMiB, memorySamples};
                samples.push(sample);
                fs.writeFileSync(output, JSON.stringify({date: new Date().toISOString(), rounds, sources, display: process.env.DISPLAY || null, displayDescription: process.env.FLY_BENCH_DISPLAY_DESCRIPTION || 'unspecified X11 display', gpu: true, emptyFilterCache: true, peakSampling: '100 ms from Electron connection until protected guest readiness; sampled PSS, not a guaranteed allocation peak', samples}, null, 2) + '\n');
                console.log(JSON.stringify(sample));
            } finally {
                clearInterval(memoryTimer);
                if (application) await application.close();
                fs.rmSync(temporary, {recursive: true, force: true});
            }
        }
    } finally { server.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
