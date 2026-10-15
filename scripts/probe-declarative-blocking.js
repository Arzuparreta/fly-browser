// Optional capability probe, independent of Fly's native hooks and extension API
// adapter. Re-run when upgrading Electron before considering uBlock Origin Lite.
const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const http = require('node:http');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-dnr-probe-'));
    const extension = path.join(temporary, 'extension');
    fs.mkdirSync(extension);
    const version = require('../package.json').version;
    fs.writeFileSync(path.join(extension, 'manifest.json'), JSON.stringify({
        manifest_version: 3, name: 'Declarative blocking probe', version,
        permissions: ['declarativeNetRequest'], host_permissions: ['<all_urls>'],
        declarative_net_request: {rule_resources: [{id: 'probe', enabled: true, path: 'rules.json'}]}
    }));
    fs.writeFileSync(path.join(extension, 'rules.json'), JSON.stringify([{
        id: 1, priority: 1, action: {type: 'block'},
        condition: {urlFilter: '/must-be-blocked.js', resourceTypes: ['script']}
    }]));
    fs.writeFileSync(path.join(temporary, 'package.json'), JSON.stringify({name: 'fly-dnr-probe', version, main: 'main.js'}));
    fs.writeFileSync(path.join(temporary, 'main.js'), `
        const {app, BrowserWindow, session} = require('electron');
        app.enableSandbox(); app.setPath('userData', ${JSON.stringify(path.join(temporary, 'data'))});
        app.whenReady().then(async () => {
            await session.defaultSession.extensions.loadExtension(${JSON.stringify(extension)});
            const window = new BrowserWindow({webPreferences: {sandbox: true}});
            window.loadURL('about:blank');
        });
    `);
    let serverHits = 0, application;
    const server = http.createServer((req, res) => {
        if (req.url.startsWith('/must-be-blocked.js')) {
            serverHits++;
            res.writeHead(200, {'Content-Type': 'text/javascript'});
            res.end('window.ruleFailed = true');
        } else {
            res.writeHead(200, {'Content-Type': 'text/html'});
            res.end('<script src="/must-be-blocked.js"></script>');
        }
    });
    try {
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--ozone-platform=x11', temporary]});
        const page = await application.firstWindow();
        await page.goto(`http://127.0.0.1:${server.address().port}/`);
        const scriptExecuted = await page.evaluate(() => window.ruleFailed === true);
        const engine = await application.evaluate(() => ({electron: process.versions.electron, chromium: process.versions.chrome}));
        console.log(JSON.stringify({date: new Date().toISOString(), ...engine, serverHits, scriptExecuted,
            enforced: serverHits === 0 && !scriptExecuted}, null, 2));
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
