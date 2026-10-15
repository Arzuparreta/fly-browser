// Optional Electron regression: run with the normal renderer sandbox enabled.
const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-blocking-startup-'));
    const entry = createElectronFixture(root, temporary);
    const configDir = path.join(temporary, '.config/fly-browser');
    fs.mkdirSync(configDir, {recursive: true});
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({last_seen_version: require('../package.json').version}));
    // A local PCM clip tests media loading without external codecs or downloads.
    const clip = Buffer.alloc(44 + 16000);
    clip.write('RIFF'); clip.writeUInt32LE(clip.length - 8, 4); clip.write('WAVEfmt ', 8);
    clip.writeUInt32LE(16, 16); clip.writeUInt16LE(1, 20); clip.writeUInt16LE(1, 22);
    clip.writeUInt32LE(8000, 24); clip.writeUInt32LE(16000, 28);
    clip.writeUInt16LE(2, 32); clip.writeUInt16LE(16, 34);
    clip.write('data', 36); clip.writeUInt32LE(16000, 40);
    let blockedHits = 0, mediaHits = 0;
    const server = http.createServer((req, res) => {
        if (req.url === '/clip.wav') {
            mediaHits++;
            res.writeHead(200, {'Content-Type': 'audio/wav', 'Content-Length': clip.length});
            res.end(clip);
        } else if (req.url === '/fly-block-me.js') {
            blockedHits++;
            res.writeHead(200, {'Content-Type': 'text/javascript'});
            res.end('window.unblocked = true');
        } else {
            res.writeHead(200, {'Content-Type': 'text/html'});
            res.end('<title>Startup media</title><input id="text"><video id="media" autoplay muted loop src="/clip.wav"></video><script src="/fly-block-me.js"></script>');
        }
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    const seedSession = () => fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Work', workspaces: {Work: [url]}}));
    const guest = (page, script) => page.evaluate(script => document.querySelector('#webview-container webview').executeJavaScript(script), script);
    let application;
    const launch = async () => {
        seedSession();
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--disable-gpu', '--ozone-platform=x11', entry]});
        const page = await application.firstWindow();
        await page.waitForFunction(() => { try { return document.querySelector('#webview-container webview')?.getURL().startsWith('http://127.0.0.1'); } catch { return false; } });
        await page.waitForFunction(() => { try { return document.querySelector('#webview-container webview').executeJavaScript('document.getElementById("media")?.currentTime > 0.1').catch(() => false); } catch { return false; } });
        return page;
    };
    const background = script => application.evaluate(({webContents}, script) => webContents.getAllWebContents()
        .find(w => w.getType() === 'backgroundPage').executeJavaScript(script), script);
    try {
        let page = await launch();
        assert.equal((await page.evaluate(() => window.flyAPI.getBrowserSettings())).extensions_enabled, true);
        assert.equal(await page.locator('.tab-shield-btn').count(), 0);
        assert(mediaHits > 0, 'Media restored on the first enabled launch must reach the server');
        assert.equal(await guest(page, 'document.getElementById("media").error'), null);
        const pid = await application.evaluate(({webContents}) => webContents.getAllWebContents().find(w => w.getType() === 'backgroundPage').getOSProcessId());
        assert(fs.readFileSync(`/proc/${pid}/cmdline`, 'utf8').includes('--enable-sandbox'), 'Extension renderer must enable the sandbox');
        assert.match(fs.readFileSync(`/proc/${pid}/status`, 'utf8'), /^Seccomp:\s+2$/m);
        assert.deepEqual(await guest(page, `(() => {
            const canvas = document.createElement('canvas'); canvas.width = canvas.height = 1;
            const ctx = canvas.getContext('2d'); ctx.fillStyle = 'rgb(100, 50, 25)'; ctx.fillRect(0, 0, 1, 1);
            const before = [...ctx.getImageData(0, 0, 1, 1).data];
            canvas.toDataURL(); canvas.toDataURL();
            return [before, [...ctx.getImageData(0, 0, 1, 1).data]];
        })()`), [[100, 50, 25, 255], [100, 50, 25, 255]], 'Canvas reads and exports must preserve pixels');
        await background("import('./js/background.js').then(({default: u}) => u.appendUserFilters('/fly-block-me.js$script'))");
        await application.close(); application = null;
        blockedHits = 0; mediaHits = 0;
        page = await launch();
        assert.equal(blockedHits, 0, 'Saved filters must apply on the first restored navigation, without a reload');
        assert(mediaHits > 0);
        assert.equal(await guest(page, '!!window.unblocked'), false);
        assert(!fs.existsSync(path.join(temporary, 'data/adblock_cache.bin')), 'No Ghostery cache is created');
        console.log('Default uBlock, cold/restored media, first-navigation filtering, unchanged Canvas and sandboxed extension renderer passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
