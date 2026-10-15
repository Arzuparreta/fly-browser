// Focused control for CNN's foreground/background CPU. Uses ordinary launches,
// disposable profiles and the same systemd accounting as the full comparison.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {execFileSync} = require('node:child_process');
const benchmark = require('./benchmark-vs-chromium');
const root = path.resolve(__dirname, '..');
const configurations = (process.env.FLY_CNN_CONFIGS || 'chromium,fly,fly-noblock').split(',');
const output = process.env.FLY_CNN_OUTPUT || '/tmp/fly-cnn-visibility.json';
const pause = ms => new Promise(resolve => setTimeout(resolve, ms));

(async () => {
    const server = await benchmark.fixtureServer();
    const base = `http://127.0.0.1:${server.address().port}/fixture`;
    const samples = [];
    try {
        for (const config of configurations) {
            const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-cnn-control-'));
            let instance;
            try {
                instance = await benchmark.launch(config, profile, base);
                const local = await benchmark.attachTo(instance, url => url.startsWith(base));
                await benchmark.waitFor(instance, local.sessionId, 'document.readyState === "complete"');
                await pause(15000);
                let target;
                if (instance.cfg.browser === 'chromium') {
                    const {targetId} = await instance.cdp.send('Target.createTarget', {url: 'about:blank'});
                    target = (await benchmark.targets(instance.cdp)).find(t => t.targetId === targetId);
                } else {
                    const before = new Set((await benchmark.targets(instance.cdp)).map(t => t.targetId));
                    const host = await benchmark.hostSession(instance);
                    await benchmark.evaluate(instance.cdp, host.sessionId,
                        "import('./modules/webview.js').then(m => { m.spawnTabWithUrl('about:blank'); return true; })");
                    for (let attempt = 0; attempt < 300; attempt++) {
                        target = (await benchmark.targets(instance.cdp)).find(t => t.type === 'webview' && !before.has(t.targetId));
                        if (target) break;
                        await pause(25);
                    }
                }
                if (!target) throw new Error('CNN tab was not created');
                const {sessionId} = await instance.cdp.send('Target.attachToTarget', {targetId: target.targetId, flatten: true});
                await instance.cdp.send('Network.enable', {}, sessionId);
                const urls = [];
                const stopCounting = instance.cdp.on(message => {
                    if (message.sessionId === sessionId && message.method === 'Network.requestWillBeSent') {
                        urls.push(message.params.request.url);
                    }
                });
                await instance.cdp.send('Page.navigate', {url: 'https://edition.cnn.com/'}, sessionId);
                await benchmark.waitFor(instance, sessionId, 'document.readyState === "complete"');
                await pause(15000);
                const foreground = await benchmark.cpuOver(instance.dir, 10000);
                const media = await benchmark.evaluate(instance.cdp, sessionId, `({
                    hidden: document.hidden,
                    videos: [...document.querySelectorAll('video')].map(v => ({
                        paused: v.paused, muted: v.muted, width: v.videoWidth, height: v.videoHeight,
                        time: v.currentTime, error: v.error && v.error.code, quality: v.getVideoPlaybackQuality()
                    }))
                })`);
                if (instance.cfg.browser === 'chromium') {
                    await instance.cdp.send('Target.activateTarget', {targetId: local.targetId});
                } else {
                    const host = await benchmark.hostSession(instance);
                    await benchmark.evaluate(instance.cdp, host.sessionId,
                        "import('./modules/webview.js').then(m => { m.switchTabFocus(0); return true; })");
                }
                await pause(3000);
                const background = await benchmark.cpuOver(instance.dir, 10000);
                const backgroundHidden = await benchmark.evaluate(instance.cdp, sessionId, 'document.hidden');
                stopCounting();
                const sample = {config, foreground, background, media, backgroundHidden,
                    rootRequests: urls.length,
                    brightlineRequests: urls.filter(url => url.includes('cdn-media.brightline.tv/config/')).length,
                    pss: benchmark.snapshot(instance.dir).pssMiB};
                samples.push(sample);
                console.log(JSON.stringify(sample));
                fs.writeFileSync(output, JSON.stringify({date: new Date().toISOString(),
                    source: execFileSync('git', ['rev-parse', 'HEAD'], {cwd: root, encoding: 'utf8'}).trim(),
                    method: 'One fresh profile per config; 15s local settling, CNN load plus 15s settling, 10s foreground CPU, switch to local tab plus 3s settling, 10s background CPU; root-frame network observation only',
                    samples}, null, 2) + '\n');
            } finally {
                if (instance) await benchmark.close(instance);
                fs.rmSync(profile, {recursive: true, force: true});
            }
        }
    } finally {
        await new Promise(resolve => server.close(resolve));
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
