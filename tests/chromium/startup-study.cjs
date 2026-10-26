// Opt-in process-start/session-restore benchmark. Node >=22 (native WebSocket).
// Run on an owned X11 display; never pass an existing user profile.
// See docs/chromium/startup-study.md for methodology and limitations.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const {createHash} = require('node:crypto');
const {spawn} = require('node:child_process');
const assert = require('node:assert/strict');
const args = process.argv.slice(2);
const option = (name, fallback) => args.includes(name) ? args[args.indexOf(name) + 1] : fallback;
const binary = option('--binary', process.env.FLY_NATIVE_BINARY);
const comparisonBinary = option('--compare-binary');
let activeBinary = binary;
const output = path.resolve(option('--output', '/tmp/fly-startup-results'));
const root = option('--profile-root', os.tmpdir());
const repeats = Number(option('--repeat', '5'));
const settleMs = Number(option('--settle-ms', '2000'));
const extra = args.flatMap((arg, i) => arg === '--extra-arg' ? [args[i + 1]] : []);
const knownScenarios = ['fresh-ntp','fresh-url','reopen-ntp','restore-1','restore-12',
    'restore-60','slow-active','slow-background','crash-12','private','handoff-url',
    'restore-3-workspaces','restore-4-windows','cpu-background','dormant-ntp','dormant-url',
    'streaming-active','restore-12-visited','restore-60-visited'];
const scenarios = option('--scenarios', knownScenarios.join(',')).split(',');
assert(binary && fs.existsSync(binary), 'Pass --binary /path/to/fly-browser');
assert(!comparisonBinary || fs.existsSync(comparisonBinary), 'Comparison binary must exist');
assert(!comparisonBinary || !args.includes('--compare-unused-omnibox'), 'Choose one comparison');
assert(process.env.DISPLAY, 'Run on an owned X11 display');
assert.equal(process.env.FLY_STARTUP_OWNED_DISPLAY, '1', 'Use startup-desktop.py to own the display');
assert(Number.isInteger(repeats) && repeats > 0 && repeats <= 50);
assert(Number.isFinite(settleMs) && settleMs >= 500);
assert(scenarios.every(s=>knownScenarios.includes(s)), 'Unknown scenario');
fs.mkdirSync(output, {recursive:true});
fs.mkdirSync(root, {recursive:true});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
async function binaryHash(file = binary) {
    const hash=createHash('sha256');
    for await (const bytes of fs.createReadStream(file)) hash.update(bytes);
    return hash.digest('hex');
}
const version = require('../../package.json').version;
let requests = [];
let serverOrigin;
const server = http.createServer(async (req, res) => {
    const url = new URL(req.url, serverOrigin);
    if (!url.pathname.startsWith('/page/')) { res.writeHead(204).end(); return; }
    const record = {epochMs:Date.now(), url:url.pathname + url.search};
    requests.push(record);
    const delay = Number(url.searchParams.get('delay') || 0);
    if (delay) await wait(delay);
    res.writeHead(200, {'Content-Type':'text/html', 'Cache-Control':'no-store'});
    res.write(`<!doctype html><meta charset="utf-8"><title>${url.pathname}</title>
        <style>body{background:#eef1e5;color:#152815;font:24px sans-serif;padding:60px}</style>
        <h1>${url.pathname}</h1><button onclick="this.textContent='OK'">Ready</button>${' '.repeat(2048)}`);
    const tail=Number(url.searchParams.get('tail') || 0);
    if (tail) await wait(tail);
    res.end(`
        <script>const end=performance.now()+${Number(url.searchParams.get('cpu') || 0)};
        while(performance.now()<end){};</script>`);
    record.responseEpochMs = Date.now();
});
class CDP {
    constructor(ws) {
        this.ws = ws; this.next = 0; this.pending = new Map();
        ws.addEventListener('message', event => {
            const data = JSON.parse(event.data);
            if (!data.id) return;
            const promise = this.pending.get(data.id);
            if (!promise) return;
            this.pending.delete(data.id);
            data.error ? promise.reject(new Error(JSON.stringify(data.error))) : promise.resolve(data.result);
        });
        ws.addEventListener('close', () => {
            for (const promise of this.pending.values()) promise.reject(new Error('CDP closed'));
            this.pending.clear();
        });
    }
    static async connect(url) {
        const ws = new WebSocket(url);
        await new Promise((resolve,reject) => {ws.onopen=resolve;ws.onerror=reject;});
        return new CDP(ws);
    }
    send(method, params = {}, sessionId) {
        const id = ++this.next;
        return new Promise((resolve,reject) => {
            const timer=setTimeout(()=>{this.pending.delete(id);reject(new Error('CDP timeout: '+method));},30000);
            this.pending.set(id, {resolve:value=>{clearTimeout(timer);resolve(value);},
                reject:error=>{clearTimeout(timer);reject(error);}});
            this.ws.send(JSON.stringify({id,method,params,...(sessionId ? {sessionId} : {})}));
        });
    }
    async evaluate(target, expression) {
        const {sessionId} = await this.send('Target.attachToTarget', {targetId:target, flatten:true});
        try {
            const result = await this.send('Runtime.evaluate', {expression, returnByValue:true, awaitPromise:true}, sessionId);
            if (result.exceptionDetails) throw new Error(JSON.stringify(result.exceptionDetails));
            return result.result.value;
        } finally { await this.send('Target.detachFromTarget', {sessionId}); }
    }
}
async function until(fn, timeout = 30000) {
    const deadline = Date.now() + timeout;
    while (Date.now() < deadline) {const value = await fn(); if (value) return value; await wait(10);}
    throw new Error('Timed out');
}
const pageURL = (i, query='') => `${serverOrigin}/page/${i}${query}`;
async function launch(profile, suffix, urls=[], flags=[], trace=false) {
    fs.rmSync(path.join(profile, 'DevToolsActivePort'), {force:true});
    const log = fs.openSync(path.join(output, suffix + '.log'), 'w');
    const startEpochMs = Date.now();
    const hostStart={loadavg:os.loadavg(),cpuPressure:fs.existsSync('/proc/pressure/cpu') ?
        fs.readFileSync('/proc/pressure/cpu','utf8').trim() : null};
    const traceArgs = trace ? ['--trace-startup=startup,browser,toplevel,loading,blink.user_timing,devtools.timeline,extensions',
        '--trace-startup-duration=5', '--trace-startup-format=json',
        `--trace-startup-file=${path.join(output,suffix+'.trace.json')}`] : [];
    const child = spawn(activeBinary, ['--fly-native', `--user-data-dir=${profile}`,
        '--ozone-platform=x11', '--password-store=basic', '--remote-debugging-port=0',
        ...extra,...traceArgs,...flags,...urls], {
        env:{...process.env, HOME:path.dirname(profile), XDG_CONFIG_HOME:path.dirname(profile),
            XDG_CACHE_HOME:path.join(path.dirname(profile),'cache')}, stdio:['ignore',log,log]});
    fs.closeSync(log);
    try {
        const port = await until(() => {
            assert.equal(child.exitCode, null, 'Browser exited during startup');
            const file=path.join(profile,'DevToolsActivePort');
            if (!fs.existsSync(file)) return false;
            const [port,endpoint] = fs.readFileSync(file,'utf8').trim().split('\n');
            return endpoint && {port,endpoint};
        });
        const portMs = Date.now()-startEpochMs;
        const cdp = await CDP.connect(`ws://127.0.0.1:${port.port}${port.endpoint}`);
        return {child,cdp,startEpochMs,portMs,hostStart};
    } catch (error) {child.kill('SIGTERM'); throw error;}
}
async function stop(run, crash=false) {
    if (crash) run.child.kill('SIGKILL');
    else await run.cdp.send('Browser.close').catch(()=>{});
    if (run.child.exitCode === null && run.child.signalCode === null)
        await new Promise(resolve=>{
            const timer=setTimeout(()=>run.child.kill('SIGKILL'),10000);
            run.child.once('exit',()=>{clearTimeout(timer);resolve();});
        });
    run.cdp.ws.close();
}
async function pages(run) {
    return (await run.cdp.send('Target.getTargets')).targetInfos.filter(t=>t.type==='page');
}
const sameURL = (actual, expected) => actual===expected ||
    expected==='chrome://fly-new-tab/' && actual==='chrome://newtab/';
const readMetrics = `JSON.stringify({url:location.href,visibility:document.visibilityState,
    epochMs:performance.timeOrigin, now:performance.now(),readyState:document.readyState,
    navigation:performance.getEntriesByType('navigation').map(n=>n.toJSON()),
    paints:performance.getEntriesByType('paint').map(p=>p.toJSON())})`;
async function metrics(run,target) {return JSON.parse(await run.cdp.evaluate(target,readMetrics));}
async function waitLoaded(run,url) {
    return until(async()=>{
        const target=(await pages(run)).find(t=>t.url===url);
        if (!target) return false;
        const data=await metrics(run,target.targetId);
        return data.readyState==='complete' && data;
    });
}
async function seed(profile, scenario, suffix) {
    let count = scenario.startsWith('restore-60') ? 60 : /12|slow|cpu/.test(scenario) ? 12 : 1;
    if (scenario==='reopen-ntp' || scenario==='private' || scenario==='dormant-ntp') count=0;
    const urls=Array.from({length:count},(_,i)=>pageURL(i,
        scenario==='slow-active' && i===0 || scenario==='slow-background' && i>0 ? '?delay=1200' :
            scenario==='cpu-background' && i>0 ? '?cpu=250' :
                scenario==='streaming-active' && i===0 ? '?tail=1200' : ''));
    const run=await launch(profile,suffix+'-seed', urls.length ? urls : ['chrome://newtab/']);
    try {
        await until(async()=>(await pages(run)).length>=Math.max(1,count));
        if (count) {
            for (const url of urls) {
                await waitLoaded(run,url);
                if (scenario.endsWith('-visited')) {
                    const target=(await pages(run)).find(t=>t.url===url);
                    await run.cdp.send('Target.activateTarget',{targetId:target.targetId});
                    await until(async()=>(await metrics(run,target.targetId)).visibility==='visible');
                }
            }
            const first=(await pages(run)).find(t=>t.url===urls[0]);
            await run.cdp.send('Target.activateTarget',{targetId:first.targetId});
        }
        if (scenario==='restore-3-workspaces' || scenario==='restore-4-windows' || scenario.startsWith('dormant-')) {
            const windows=scenario==='restore-4-windows' ? 4 : 3;
            for(let i=1;i<windows;i++) {
                if (scenario!=='restore-4-windows') {
                    const directory='Profile '+i;
                    fs.mkdirSync(path.join(profile,directory),{recursive:true});
                    const child=spawn(activeBinary,['--fly-native',`--user-data-dir=${profile}`,
                        `--profile-directory=${directory}`,'--new-window',pageURL(i)],{stdio:'ignore'});
                    await new Promise((resolve,reject)=>{child.once('error',reject);child.once('exit',resolve);});
                } else await run.cdp.send('Target.createTarget',{url:pageURL(i),newWindow:true});
                await waitLoaded(run,pageURL(i));
            }
            const first=(await pages(run)).find(t=>sameURL(t.url,urls[0] || 'chrome://fly-new-tab/'));
            await run.cdp.send('Target.activateTarget',{targetId:first.targetId});
            if (scenario.startsWith('dormant-')) {
                for (const target of await pages(run)) {
                    if (target.targetId!==first.targetId)
                        await run.cdp.send('Target.closeTarget',{targetId:target.targetId});
                }
            }
        }
        // Allow Chromium's session command batching to flush before a crash.
        await wait(3500);
        fs.writeFileSync(path.join(output,suffix+'-seed.json'),JSON.stringify({urls,
            targets:await pages(run)},null,2)+'\n');
    } finally {await stop(run,scenario==='crash-12');}
    return urls[0];
}
async function measure(scenario,iteration,configuration) {
    const home=fs.mkdtempSync(path.join(root,'fly-startup-'));
    const profile=path.join(home,'profile'); fs.mkdirSync(profile);
    // No personal settings; do not open the package integration offer over a test page.
    fs.writeFileSync(path.join(profile,'Local State'),JSON.stringify({fly:{preferences:{schema:1,
        values:{desktop_integration_offered:true,last_seen_version:version}}}}));
    const suffix=`${configuration ? configuration+'-' : ''}${scenario}-${iteration}`;
    let active=['fresh-url','handoff-url'].includes(scenario) ? pageURL(0) : 'chrome://fly-new-tab/';
    let run;
    try {
        if (!scenario.startsWith('fresh-') && scenario!=='handoff-url')
            active=await seed(profile,scenario,suffix) || 'chrome://fly-new-tab/';
        const urls=scenario==='fresh-url' ? [active] : [];
        requests=[];
        run=await launch(profile,suffix,urls,scenario==='private' ? ['--incognito'] : [],args.includes('--trace'));
        let secondaryExitMs;
        if (scenario==='handoff-url') {
            await wait(settleMs);
            requests=[];run.startEpochMs=Date.now();run.portMs=null;
            const secondary=spawn(activeBinary,['--fly-native',`--user-data-dir=${profile}`,
                '--ozone-platform=x11','--password-store=basic',active],{stdio:'ignore'});
            await new Promise((resolve,reject)=>{secondary.once('error',reject);secondary.once('exit',resolve);});
            secondaryExitMs=Date.now()-run.startEpochMs;
        }
        await until(async()=>{
            const all=await pages(run);
            return all.some(t=>sameURL(t.url,active));
        });
        await wait(settleMs);
        const targets=await pages(run);
        const target=targets.find(t=>sameURL(t.url,active));
        assert(target,'Expected startup page');
        const data=await metrics(run,target.targetId);
        if (data.visibility==='visible') await until(async()=>{
            const m=await metrics(run,target.targetId);return m.paints.length && m.readyState==='complete';
        });
        const final=await metrics(run,target.targetId);
        assert.equal(final.visibility,'visible','The measured tab must be visible');
        const fcp=final.paints.find(p=>p.name==='first-contentful-paint');
        const nav=final.navigation[0];
        const expectedCount = scenario==='restore-3-workspaces' ? 3 : scenario==='restore-4-windows' ? 4 :
            scenario.startsWith('restore-60') ? 60 : /12|slow|cpu/.test(scenario) ? 12 :
            scenario==='handoff-url' ? 2 : 1;
        assert.equal(targets.length,expectedCount,'Restored page count must match the seeded session');
        const processInfo=(await run.cdp.send('SystemInfo.getProcessInfo')).processInfo;
        const processStats=processInfo.map(info=>{
            let pssKiB=null;
            try {pssKiB=Number(fs.readFileSync(`/proc/${info.id}/smaps_rollup`,'utf8').match(/^Pss:\s+(\d+)/m)?.[1]);} catch {}
            return {...info,pssKiB};
        });
        const windows=[];
        for(const page of targets) windows.push(await run.cdp.send('Browser.getWindowForTarget',{targetId:page.targetId}));
        const result={scenario,iteration,configuration,experimentFlags:[...extra],
            backgroundTabsVisited:scenario.endsWith('-visited'),binary:path.resolve(activeBinary),pid:run.child.pid,
            startEpochMs:run.startEpochMs,portMs:run.portMs,
            secondaryExitMs,processStats,windows,hostStart:run.hostStart,
            navigationStartMs:final.epochMs-run.startEpochMs,
            fcpMs:fcp ? final.epochMs+fcp.startTime-run.startEpochMs : null,
            domMs:nav?.domContentLoadedEventEnd ? final.epochMs+nav.domContentLoadedEventEnd-run.startEpochMs : null,
            loadMs:nav?.loadEventEnd ? final.epochMs+nav.loadEventEnd-run.startEpochMs : null,
            observationMs:Date.now()-run.startEpochMs, pageCount:targets.length,
            requestCount:requests.length,requests:requests.map(r=>({...r,startMs:r.epochMs-run.startEpochMs})),
            targetURLs:targets.map(t=>t.url),allTargets:(await run.cdp.send('Target.getTargets')).targetInfos,
            active:final};
        if (final.url.startsWith(serverOrigin)) {
            const {sessionId}=await run.cdp.send('Target.attachToTarget',{targetId:target.targetId,flatten:true});
            try {
                const point=await run.cdp.send('Runtime.evaluate',{expression:
                    'JSON.stringify((()=>{const r=document.querySelector("button").getBoundingClientRect();return {x:r.x+r.width/2,y:r.y+r.height/2}})())',
                    returnByValue:true},sessionId);
                const position=JSON.parse(point.result.value);
                await run.cdp.send('Input.dispatchMouseEvent',{type:'mousePressed',button:'left',clickCount:1,...position},sessionId);
                await run.cdp.send('Input.dispatchMouseEvent',{type:'mouseReleased',button:'left',clickCount:1,...position},sessionId);
                const clicked=await run.cdp.send('Runtime.evaluate',{expression:
                    'document.querySelector("button").textContent',returnByValue:true},sessionId);
                assert.equal(clicked.result.value,'OK','Fixture accepts page input');
                result.inputVerified=true;
            } finally {await run.cdp.send('Target.detachFromTarget',{sessionId});}
        }
        if (args.includes('--trace')) await wait(Math.max(0,5500-(Date.now()-run.startEpochMs)));
        await stop(run);run=null;
        fs.writeFileSync(path.join(output,suffix+'.json'),JSON.stringify(result,null,2)+'\n');
        console.log(JSON.stringify({scenario,iteration,configuration,fcpMs:result.fcpMs,loadMs:result.loadMs,
            portMs:result.portMs,pageCount:result.pageCount,requests:result.requestCount}));
        return result;
    } finally {
        if (run) await stop(run).catch(()=>run.child.kill('SIGTERM'));
        if (args.includes('--keep-profiles')) console.log('Disposable profile:',profile);
        else fs.rmSync(home,{recursive:true,force:true});
    }
}
(async()=>{
    await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
    serverOrigin=`http://127.0.0.1:${server.address().port}`;
    const results=[];
    try {
        // Round-robin reduces confounding by warm-up and changing host load.
        const configurations=comparisonBinary ? [
            {name:'baseline',flags:[],binary:comparisonBinary},
            {name:'optimized',flags:[],binary}] : args.includes('--compare-unused-omnibox') ? [
            {name:'standard',flags:[]}, {name:'without-unused-omnibox',flags:[
                '--disable-features=WebUIOmniboxPopup,WebUIOmniboxAimPopup,PreloadTopChromeWebUI']}]
            : [{name:undefined,flags:[]}];
        for(let i=0;i<repeats;i++) for(const scenario of scenarios) {
            const order=i%2 ? [...configurations].reverse() : configurations;
            for (const config of order) {
                const count=extra.length;
                try {
                    activeBinary=config.binary || binary;
                    extra.push(...config.flags);
                    results.push(await measure(scenario,i,config.name));
                } finally {extra.length=count;}
            }
        }
        fs.writeFileSync(path.join(output,'results.json'),JSON.stringify({
            created:new Date().toISOString(),platform:os.platform(),kernel:os.release(),
            cpu:os.cpus()[0].model,totalMemory:os.totalmem(),profileRoot:root,settleMs,
            binarySHA256:await binaryHash(),
            comparisonBinarySHA256:comparisonBinary ? await binaryHash(comparisonBinary) : undefined,
            extraArgs:extra,trace:args.includes('--trace'),results},null,2)+'\n');
    } finally {server.closeAllConnections();await new Promise(resolve=>server.close(resolve));}
})().catch(error=>{console.error(error);process.exitCode=1;});
