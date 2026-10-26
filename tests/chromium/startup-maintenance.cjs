// Opt-in packaged-browser acceptance on startup-desktop.py's owned display.
// Uses fresh disposable profiles; never changes desktop integration/defaults.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {spawn, spawnSync} = require('node:child_process');
const {createHash} = require('node:crypto');
const args = process.argv.slice(2);
const option = name => args[args.indexOf(name) + 1];
assert(args.includes('--binary') && args.includes('--output'));
assert.equal(process.env.FLY_STARTUP_OWNED_DISPLAY, '1');
const binary = path.resolve(option('--binary'));
const output = path.resolve(option('--output'));
const knownCases = ['fast','streaming','deadline','upgrade','integration','multiple-windows','early-quit'];
const cases = args.includes('--cases') ? option('--cases').split(',') : knownCases;
assert(cases.length && cases.every(name=>knownCases.includes(name)), 'Unknown startup acceptance case');
const version = require('../../package.json').version;
const previous = version.split('-')[0].split('.').map(Number);
for (let i = previous.length - 1; i >= 0; i--) {
    if (previous[i] > 0) { previous[i]--; break; }
}
assert.notEqual(previous.join('.'), version, 'Need a previous version for the upgrade fixture');
fs.mkdirSync(output, {recursive:true});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(fn, timeout = 20000) {
    const end = Date.now() + timeout;
    while (Date.now() < end) { const value = await fn(); if (value) return value; await wait(20); }
    throw new Error('Acceptance wait timed out');
}
class CDP {
    constructor(ws) {
        this.ws = ws; this.id = 0; this.pending = new Map();
        ws.addEventListener('message', event => {
            const data = JSON.parse(event.data), pending = this.pending.get(data.id);
            if (!pending) return;
            this.pending.delete(data.id);
            data.error ? pending.reject(new Error(JSON.stringify(data.error))) : pending.resolve(data.result);
        });
    }
    send(method, params = {}, sessionId) {
        const id = ++this.id;
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {this.pending.delete(id); reject(new Error(method + ' timed out'));}, 15000);
            this.pending.set(id, {resolve:v=>{clearTimeout(timer);resolve(v);}, reject:e=>{clearTimeout(timer);reject(e);}});
            this.ws.send(JSON.stringify({id, method, params, ...(sessionId ? {sessionId} : {})}));
        });
    }
    async evaluate(targetId, expression) {
        const {sessionId} = await this.send('Target.attachToTarget', {targetId, flatten:true});
        try {
            const result = await this.send('Runtime.evaluate', {expression, returnByValue:true}, sessionId);
            assert(!result.exceptionDetails, JSON.stringify(result.exceptionDetails));
            return result.result.value;
        } finally {await this.send('Target.detachFromTarget', {sessionId});}
    }
}
const server = http.createServer(async (req, res) => {
    const name = req.url.split('/').pop();
    if (['deadline', 'early-quit', 'multiple-windows'].includes(name)) await wait(7000);
    if (res.destroyed) return;
    res.writeHead(200, {'Content-Type':'text/html', 'Cache-Control':'no-store'});
    res.write('<!doctype html><title>Startup acceptance</title><h1>Visible page</h1><button>Ready</button>' + ' '.repeat(4096));
    if (name === 'streaming') await wait(7000);
    if (!res.destroyed) res.end();
});
const results = [];
async function runCase(name) {
    const home = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-maintenance-'));
    const profile = path.join(home, 'profile');
    fs.mkdirSync(profile);
    const state = path.join(profile, 'Local State');
    fs.writeFileSync(state, JSON.stringify({fly:{preferences:{schema:1, values:{
        desktop_integration_offered:name !== 'integration',
        last_seen_version:name === 'upgrade' ? previous.join('.') : version}}}}));
    const trace = path.join(output, name + '.trace.json');
    const log = fs.openSync(path.join(output, name + '.log'), 'w');
    const url = `http://127.0.0.1:${server.address().port}/${name}`;
    const started = Date.now();
    const child = spawn(binary, ['--fly-native', `--user-data-dir=${profile}`, '--ozone-platform=x11',
        '--password-store=basic', '--remote-debugging-port=0',
        '--trace-startup=browser,loading,devtools.timeline', '--trace-startup-format=json',
        '--trace-startup-duration=9', `--trace-startup-file=${trace}`, url],
        {env:{...process.env, HOME:home, XDG_CONFIG_HOME:home,
            XDG_CACHE_HOME:path.join(home, 'cache'), XDG_DATA_HOME:path.join(home, 'data'),
            XDG_DATA_DIRS:path.join(home, 'system-data')}, stdio:['ignore',log,log]});
    fs.closeSync(log);
    let cdp;
    try {
        const port = await until(() => {
            assert.equal(child.exitCode, null, 'Browser exited at startup');
            const file = path.join(profile, 'DevToolsActivePort');
            if (!fs.existsSync(file)) return false;
            const lines = fs.readFileSync(file, 'utf8').trim().split('\n');
            return lines.length === 2 && /^\d+$/.test(lines[0]) && lines;
        });
        const ws = new WebSocket(`ws://127.0.0.1:${port[0]}${port[1]}`);
        await new Promise((resolve,reject)=>{ws.onopen=resolve;ws.onerror=reject;});
        cdp = new CDP(ws);
        if (name === 'multiple-windows') {
            await cdp.send('Target.createTarget', {url:url.replace(/[^/]+$/, 'second-window'), newWindow:true});
        }
        if (name !== 'early-quit') {
            await wait(Math.max(0, 9500 - (Date.now() - started)));
            const pages = (await cdp.send('Target.getTargets')).targetInfos.filter(t=>t.type === 'page');
            const original = pages.find(t=>t.url === url);
            assert(original, 'The original startup page must survive');
            const measured = name === 'multiple-windows' ? pages.find(p=>p.url.endsWith('/second-window')) : original;
            assert(measured, 'The selected startup page must survive');
            const metrics = await cdp.evaluate(measured.targetId, `({visibility:document.visibilityState,
                paints:performance.getEntriesByType('paint').map(p=>p.toJSON()),readyState:document.readyState})`);
            assert(metrics.paints.some(p=>p.name === 'first-contentful-paint'));
            if (name === 'upgrade') assert(pages.some(p=>/whats.new/i.test(p.url)), 'Upgrade still shows What’s New');
            else assert.equal(pages.length, name === 'multiple-windows' ? 2 : 1);
            if (name === 'integration') {
                await until(() => JSON.parse(fs.readFileSync(state, 'utf8')).fly.preferences.values.desktop_integration_offered);
                assert(!fs.existsSync(path.join(home, 'data/applications/fly-browser.desktop')), 'Offer does not install anything');
            }
            // Optional full-window evidence also captures native notices, which
            // Page.captureScreenshot cannot include. Only the owned display.
            const desktopImage = path.join(output, name + '-desktop.png');
            const captured = spawnSync('import', ['-window', 'root', desktopImage], {timeout:10000});
            let visible = original;
            for (const page of pages) {
                if (await cdp.evaluate(page.targetId, 'document.visibilityState') === 'visible') {
                    visible = page; break;
                }
            }
            const {sessionId} = await cdp.send('Target.attachToTarget', {targetId:visible.targetId,flatten:true});
            const screenshot = await cdp.send('Page.captureScreenshot', {}, sessionId);
            fs.writeFileSync(path.join(output, name + '.png'), Buffer.from(screenshot.data, 'base64'));
            await cdp.send('Target.detachFromTarget', {sessionId});
            results.push({name, metrics, pages:pages.map(p=>p.url),
                desktopScreenshot:captured.status === 0 ? desktopImage : null});
        }
        await cdp.send('Browser.close');
        await until(() => child.exitCode !== null || child.signalCode !== null);
        ws.close(); cdp = null;
        await until(() => fs.existsSync(trace));
        const events = JSON.parse(fs.readFileSync(trace, 'utf8')).traceEvents;
        const selected = events.filter(e=>/^Fly\.(StartupMaintenance\.|FirstWindowTasks)/.test(e.name));
        const ready = selected.filter(e=>e.name === 'Fly.StartupMaintenance.Ready');
        const tasks = selected.filter(e=>e.name === 'Fly.FirstWindowTasks' && e.ph !== 'E');
        assert.equal(tasks.length, name === 'early-quit' ? 0 : 1, 'Maintenance runs once or is cancelled on quit');
        assert.equal(ready.length, tasks.length);
        if (ready.length) {
            assert.equal(ready[0].args.reason, name === 'deadline' ? 'deadline' : 'presented');
            assert(tasks[0].ts >= ready[0].ts);
            if (name === 'deadline') {
                const scheduled = selected.find(e=>e.name === 'Fly.StartupMaintenance.Scheduled');
                assert(ready[0].ts - scheduled.ts >= 4900000, 'Fallback waits for its deadline');
            } else {
                assert(selected.some(e=>e.name === 'Fly.StartupMaintenance.PagePainted' && e.ts <= ready[0].ts));
            }
        }
        if (name === 'early-quit') results.push({name});
        results.at(-1).events = selected;
        console.log(JSON.stringify({name, reason:ready[0]?.args.reason, maintenanceRuns:tasks.length}));
    } finally {
        cdp?.ws.close();
        if (child.exitCode === null && child.signalCode === null) {
            child.kill('SIGTERM');
            await until(()=>child.exitCode !== null || child.signalCode !== null).catch(()=>child.kill('SIGKILL'));
        }
        fs.rmSync(home, {recursive:true, force:true});
    }
}
(async()=>{
    await new Promise(resolve=>server.listen(0, '127.0.0.1', resolve));
    try {
        for (const name of cases) await runCase(name);
        const hash = createHash('sha256');
        for await (const chunk of fs.createReadStream(binary)) hash.update(chunk);
        fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify({
            binary, binarySHA256:hash.digest('hex'), created:new Date().toISOString(), results}, null, 2) + '\n');
    } finally {server.closeAllConnections(); server.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
