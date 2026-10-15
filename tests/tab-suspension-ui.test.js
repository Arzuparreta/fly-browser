const launch = require('./electron-cdp-fixture');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const {execFileSync} = require('node:child_process');
function processSample(rootPid) {
    const entries = new Map();
    for (const pid of fs.readdirSync('/proc').filter(name => /^\d+$/.test(name))) {
        try { const stat = fs.readFileSync(`/proc/${pid}/stat`, 'utf8'); const fields = stat.slice(stat.lastIndexOf(')') + 2).split(' '); entries.set(Number(pid), {parent: Number(fields[1]), ticks: Number(fields[11]) + Number(fields[12])}); } catch {}
    }
    const pids = new Set([rootPid]);
    for (let changed = true; changed;) { changed = false; for (const [pid, entry] of entries) if (pids.has(entry.parent) && !pids.has(pid)) {pids.add(pid); changed = true;} }
    let ticks = 0, pssKiB = 0;
    for (const pid of pids) {
        ticks += entries.get(pid)?.ticks || 0;
        try { pssKiB += Number(fs.readFileSync(`/proc/${pid}/smaps_rollup`, 'utf8').match(/^Pss:\s+(\d+)/m)[1]); } catch {}
    }
    return {ticks, pssMiB: pssKiB / 1024, processes: pids.size};
}
const root = path.resolve(__dirname, '..');
(async () => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-suspension-'));
    let requests = 0;
    const server = http.createServer((req, res) => {
        requests++;
        if (req.url === '/stream') { res.writeHead(200, {'Content-Type': 'text/event-stream'}); res.write('data: alive\n\n'); return; }
        res.end('<!doctype html><title>Suspension fixture</title><input id="draft"><div style="height:5000px"></div><script>window.counter=0;window.freezeCount=0;document.addEventListener("freeze",()=>{window.freezeCount++;window.atFreeze=counter});document.addEventListener("resume",()=>window.atResume=counter);window.identity=Math.random();setInterval(()=>{window.counter++;const end=performance.now()+3;while(performance.now()<end){}},20)</script>');
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await launch(root, directory, {bootstrap: `
            // Electron builds differ in when DevTools replaces an attached debugger.
            // Exercise the documented detach path deterministically on every build.
            app.on('web-contents-created', (_event, guest) => {
                if (guest.getType() === 'webview') guest.on('devtools-opened', () => {
                    if (guest.debugger.isAttached()) guest.debugger.detach();
                });
            });
        `});
        const {page} = application;
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.evaluate(async url => (await import('./modules/navigation.js')).handleNavigation(url), `http://127.0.0.1:${server.address().port}/page`);
        await page.waitForFunction(() => {try {const v = [...document.querySelectorAll('webview')].find(v => v.getClientRects().length); return v && !v.isLoading() && v.getURL().includes('/page');} catch {return false;}});
        const original = await page.evaluate(async () => {
            const view = [...document.querySelectorAll('webview')].find(v => v.getClientRects().length);
            const value = await view.executeJavaScript(`document.querySelector('#draft').value='unsaved';window.scrollTo(0,450);history.pushState({},'', '#retained');sessionStorage.setItem('draft','session');({identity,history:history.length})`);
            window.suspensionTestView = view;
            return {id: view.getWebContentsId(), ...value};
        });
        await page.evaluate(() => window.executeBrowserAction('spawn-tab-with-url', 'about:blank'));
        await page.waitForTimeout(800);
        const suspend = () => page.evaluate(async () => (await import('./modules/tabSuspension.js')).suspendInactiveTabs());
        const beforeStart = processSample(application.pid);
        await page.waitForTimeout(1000);
        const before = processSample(application.pid);
        await suspend();
        const afterStart = processSample(application.pid);
        await page.waitForTimeout(1000);
        const after = processSample(application.pid);
        const ticksPerSecond = Number(execFileSync('getconf', ['CLK_TCK'], {encoding: 'utf8'}));
        const sample = {scenario: 'Local page: 3 ms work every 20 ms; one-second CPU windows; default extensions; GPU disabled',
            before: {...before, cpuMs: (before.ticks - beforeStart.ticks) * 1000 / ticksPerSecond},
            suspended: {...after, cpuMs: (after.ticks - afterStart.ticks) * 1000 / ticksPerSecond}};
        console.log('Lifecycle resource sample', JSON.stringify(sample));
        if (process.env.FLY_SUSPENSION_SAMPLE) fs.writeFileSync(process.env.FLY_SUSPENSION_SAMPLE, JSON.stringify(sample, null, 2) + '\n');
        const stats = () => page.evaluate(() => window.flyAPI.getSystemDiagnostics({}));
        const snapshot = await stats();
        console.log('Suspension diagnostics', JSON.stringify(snapshot.session));
        assert.equal(snapshot.session.frozenTabs, 1);
        assert.equal(await page.evaluate(() => document.querySelectorAll('.tab-flight-state,.tab-sleep-badge,.tab-sleep-dot').length), 0);
        const beforeRequests = requests;
        // Host CDP evaluates native state without resuming the frozen guest's tasks.
        const read = () => page.evaluate(() => window.suspensionTestView.executeJavaScript('({counter,freezeCount,atFreeze:window.atFreeze,atResume:window.atResume,visibility:document.visibilityState,identity,draft:document.querySelector("#draft").value,scroll:scrollY,history:history.length,session:sessionStorage.getItem("draft"),hash:location.hash})'));
        // executeJavaScript waits on frozen pages: resume through the actual tab selection path.
        await page.waitForTimeout(300);
        await page.evaluate(() => window.executeBrowserAction('previous-tab'));
        await page.waitForFunction(() => document.activeElement === window.suspensionTestView);
        const restored = await read();
        await page.waitForTimeout(150);
        assert((await read()).counter > restored.counter, 'Timers resume on selection');
        console.log('Lifecycle sample', restored);
        assert(restored.freezeCount > 0, 'The document receives Chromium freeze');
        assert.equal(restored.atResume, restored.atFreeze, 'No timer runs between freeze and resume');
        assert.equal(restored.identity, original.identity);
        assert.equal(restored.draft, 'unsaved');
        assert.equal(restored.scroll, 450);
        assert.equal(restored.history, original.history);
        assert.equal(restored.hash, '#retained');
        assert.equal(restored.session, 'session');
        assert.equal(await page.evaluate(() => window.suspensionTestView.getWebContentsId()), original.id);
        assert.equal(requests, beforeRequests, 'Resuming never requests the page again');
        // Pinned background pages must not freeze.
        await page.evaluate(() => window.executeBrowserAction('toggle-pin-tab'));
        await page.evaluate(() => window.executeBrowserAction('next-tab'));
        await suspend();
        assert.equal((await stats()).session.frozenTabs, 0);
        const selectOriginal = async () => {
            await page.evaluate(async () => {
                const {state} = await import('./modules/state.js');
                const {switchTabFocus} = await import('./modules/webview.js');
                switchTabFocus(state.activeViewsCache[state.sessionState.current_workspace].indexOf(window.suspensionTestView));
            });
            await page.waitForFunction(() => document.activeElement === window.suspensionTestView);
        };
        const background = async () => {
            await page.evaluate(() => window.executeBrowserAction('next-tab'));
            await page.waitForTimeout(150);
        };
        const script = expression => page.evaluate(expression => window.suspensionTestView.executeJavaScript(expression), expression);
        await selectOriginal();
        await page.evaluate(() => window.executeBrowserAction('toggle-pin-tab'));
        for (const [create, dispose] of [
            ['window.peer = new RTCPeerConnection()', 'peer.close()'],
            ['window.audio = new AudioContext()', 'audio.close()'],
            ['window.track = document.createElement("canvas").captureStream().getVideoTracks()[0]', 'track.stop()'],
            ['window.stream = new EventSource("/stream")', 'stream.close()']
        ]) {
            await script(create + ';true');
            await background(); await suspend();
            assert.equal((await stats()).session.frozenTabs, 0, create);
            await selectOriginal(); await script(dispose + ';true');
        }
        console.log('Disposed work', await script('[peer.connectionState, track.readyState, stream.readyState]'));
        await page.waitForTimeout(500);
        // Both visible split panes stay alive, including a previously frozen page.
        await background(); await suspend();
        assert.equal((await stats()).session.frozenTabs, 1);
        await page.evaluate(() => window.executeBrowserAction('toggle-split'));
        await page.waitForFunction(() => document.querySelectorAll('webview').length === [...document.querySelectorAll('webview')].filter(v=>v.getClientRects().length).length);
        await suspend();
        assert.equal((await stats()).session.frozenTabs, 0);
        await page.evaluate(() => window.executeBrowserAction('close-split-view'));
        // DevTools opens on a hidden frozen page without leaving it stuck.
        await selectOriginal();
        await background(); await suspend();
        await page.evaluate(() => window.suspensionTestView.openDevTools());
        await page.waitForTimeout(300);
        assert.equal((await stats()).session.frozenTabs, 0);
        await suspend(); assert.equal((await stats()).session.frozenTabs, 0);
        await page.evaluate(() => window.suspensionTestView.closeDevTools());
        await page.waitForTimeout(300);
        await suspend();
        assert.equal((await stats()).session.frozenTabs, 1, 'Closing DevTools restores suspension');
        // Repeat from an ordinary foreground page, not just an already frozen guest.
        await selectOriginal();
        await page.evaluate(() => window.suspensionTestView.openDevTools());
        await page.waitForTimeout(300);
        await page.evaluate(() => window.suspensionTestView.closeDevTools());
        await page.waitForTimeout(300);
        await background(); await suspend();
        assert.equal((await stats()).session.frozenTabs, 1, 'Foreground DevTools cycle preserves suspension');
        await selectOriginal();
        assert.equal((await read()).identity, original.identity, 'DevTools recovery preserves the live document');
        assert.deepEqual(page.errors, []);
        console.log('Electron suspension: same guest, document, form, JS, scroll, history and storage; no reload; pin protection and badge removal passed.');
    } finally {
        if (application) await application.close();
        server.closeAllConnections();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(directory, {recursive: true, force: true});
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
