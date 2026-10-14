// Optional cold-start probe: distinguish a visible local page from a protected one.
const {_electron} = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const roots = process.argv.slice(2).map(root => path.resolve(root));
if (roots.length !== 2) throw new Error('Supply the upstream and fork source directories.');
const rounds = Number(process.env.MISE_BENCH_ROUNDS || 5);
const output = process.env.MISE_BENCH_OUTPUT || '/tmp/mise-startup.json';
const samples = [];

(async () => {
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Startup fixture</title><main id="fixture">Local fixture</main>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    try {
        for (let round = 0; round < rounds; round++) for (const root of round % 2 ? [...roots].reverse() : roots) {
            const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-startup-'));
            const configDir = path.join(temporary, '.config/mise-browser');
            fs.mkdirSync(configDir, {recursive: true});
            const url = `http://127.0.0.1:${server.address().port}`;
            const version = require(path.join(root, 'package.json')).version;
            fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Bench', workspaces: {Bench: [url]}}));
            fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({last_seen_version: version}));
            fs.writeFileSync(path.join(temporary, 'package.json'), JSON.stringify({name: 'mise-startup-fixture', version, main: 'main.js'}));
            fs.writeFileSync(path.join(temporary, 'main.js'), `const {app}=require('electron');
app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))});
app.commandLine.appendSwitch('proxy-server','http://127.0.0.1:9'); app.commandLine.appendSwitch('proxy-bypass-list','<-loopback>;127.0.0.1');
globalThis.__benchProtected=new WeakSet(); globalThis.__benchFetches=0;
const originalFetch=globalThis.fetch; globalThis.fetch=(...args)=>{globalThis.__benchFetches++;return originalFetch(...args);};
const {ElectronBlocker}=require(${JSON.stringify(require.resolve('@ghostery/adblocker-electron'))});
const originalEnable=ElectronBlocker.prototype.enableBlockingInSession;
ElectronBlocker.prototype.enableBlockingInSession=function(session){const result=originalEnable.call(this,session);globalThis.__benchProtected.add(session);return result;};
require(${JSON.stringify(path.join(root, 'main.js'))});`);
            let application;
            try {
                const start = performance.now();
                application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--ozone-platform=x11', temporary]});
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
                const filterFetches = await application.evaluate(() => globalThis.__benchFetches);
                const sample = {root, round, localReadyMs, protectedReadyMs, filterFetches};
                samples.push(sample);
                fs.writeFileSync(output, JSON.stringify({date: new Date().toISOString(), rounds, gpu: true, emptyFilterCache: true, samples}, null, 2) + '\n');
                console.log(JSON.stringify(sample));
            } finally {
                if (application) await application.close();
                fs.rmSync(temporary, {recursive: true, force: true});
            }
        }
    } finally { server.close(); }
})().catch(error => {console.error(error); process.exitCode = 1;});
