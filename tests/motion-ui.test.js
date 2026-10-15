// Optional Electron regression and before/after layout benchmark; not a CI gate.
const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-motion-'));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', ...(process.env.FLY_MOTION_GPU ? [] : ['--disable-gpu']), '--ozone-platform=x11', createElectronFixture(root, temporary)]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.mouse.move(600, 400);
        await page.evaluate(async () => {
            window.applySidebarMode(true);
            document.activeElement.blur();
            const {state} = await import('./modules/state.js');
            const {renderWorkspaceUI} = await import('./modules/webview.js');
            state.sessionState.workspaces[state.sessionState.current_workspace] = Array.from({length: 60}, (_, i) => `https://example.invalid/${i}`);
            renderWorkspaceUI();
        });
        const cdp = await page.context().newCDPSession(page);
        await cdp.send('Performance.enable');
        console.log(JSON.stringify({gpu: await application.evaluate(({app}) => app.getGPUFeatureStatus())}));
        const css = fs.readFileSync(path.join(root, 'style.css'), 'utf8');
        const variants = process.env.FLY_MOTION_BASELINE ? [['before', fs.readFileSync(process.env.FLY_MOTION_BASELINE, 'utf8')], ['after', css]] : [['after', css]];
        for (const [name, content] of variants) {
            await page.evaluate(content => {
                document.querySelector('link[href="style.css"]').disabled = true;
                let style = document.getElementById('motion-test-style');
                if (!style) { style = document.createElement('style'); style.id = 'motion-test-style'; document.head.append(style); }
                style.textContent = content;
            }, content);
            for (const rate of [1, 6]) {
                await cdp.send('Emulation.setCPUThrottlingRate', {rate});
                await page.waitForTimeout(350);
                const metrics = async () => Object.fromEntries((await cdp.send('Performance.getMetrics')).metrics.map(m => [m.name, m.value]));
                const before = await metrics();
                const frames = await page.evaluate(async () => {
                    const sidebar = document.getElementById('Sidebar');
                    const frames = [];
                    for (let i = 0; i < 12; i++) {
                        sidebar.classList.toggle('expanded', i % 2 === 0);
                        const start = performance.now(); let previous = start;
                        await new Promise(resolve => {
                            const sample = now => { frames.push(now - previous); previous = now; if (now - start < 270) requestAnimationFrame(sample); else resolve(); };
                            requestAnimationFrame(sample);
                        });
                    }
                    return frames.filter(n => n > 0).sort((a,b) => a-b);
                });
                const after = await metrics();
                const result = {name, rate, layouts: after.LayoutCount-before.LayoutCount, layoutMs: (after.LayoutDuration-before.LayoutDuration)*1000, styleMs: (after.RecalcStyleDuration-before.RecalcStyleDuration)*1000, frameP95: frames[Math.floor(frames.length*.95)]};
                console.log(JSON.stringify(result));
                if (name === 'after') assert(result.layouts <= 30, 'Sidebar must not lay out on every frame');
            }
        }
        await cdp.send('Emulation.setCPUThrottlingRate', {rate: 1});
        // Interrupted transitions settle to the last state without timers or orphan layers.
        await page.evaluate(async () => {
            const sidebar = document.getElementById('Sidebar');
            for (let i=0;i<9;i++) { sidebar.classList.toggle('expanded', i%2===0); await new Promise(r=>setTimeout(r,25)); }
            sidebar.classList.remove('expanded');
        });
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => getComputedStyle(document.getElementById('Sidebar')).clipPath), 'inset(0px 224px 0px 0px)');
        assert.equal(await page.evaluate(() => document.getElementById('Sidebar').contains(document.elementFromPoint(100, 200))), false, 'Clipped surface must not intercept page input');
        await page.locator('#pin-sidebar-btn').focus();
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => getComputedStyle(document.getElementById('Sidebar')).clipPath), 'inset(0px -32px 0px 0px)');
        assert(await page.locator('#TabList li').evaluateAll(items => items.every(item => item.getBoundingClientRect().height >= 28)), 'Long tab lists scroll without crushing rows');
        if (process.env.FLY_MOTION_SCREENSHOT) await page.screenshot({path: process.env.FLY_MOTION_SCREENSHOT});
        await page.emulateMedia({reducedMotion: 'reduce'});
        await page.evaluate(() => {
            document.activeElement.blur();
            document.getElementById('Sidebar').classList.remove('pointer-inside', 'expanded');
            document.getElementById('update-indicator-btn').style.display = 'inline-flex';
        });
        assert.equal(await page.evaluate(() => document.getAnimations().length), 0, 'Reduced motion settles immediately');
        await page.emulateMedia({reducedMotion: 'no-preference'});
        await page.waitForTimeout(250);
        assert.equal(await page.evaluate(() => document.getAnimations().length), 0, 'No decorative animations at rest');
        // Progress updates preserve track geometry and interpolate only the fill.
        const progress = await page.evaluate(async () => {
            const {handleDownloadStarted, handleDownloadProgress, handleDownloadDone} = await import('./modules/downloads.js');
            const data = {id: 'motion-test', filename: 'fixture.txt', totalBytes: 100, receivedBytes: 50, percent: 50};
            handleDownloadStarted(data);
            const bar = document.querySelector('#dl-card-motion-test .download-card-progress-bar');
            const width = bar.offsetWidth;
            handleDownloadProgress(data);
            const halfway = bar.style.transform;
            handleDownloadProgress({...data, percent: 120});
            const bounded = bar.style.transform;
            const finalWidth = bar.offsetWidth;
            handleDownloadDone({...data, state: 'completed'});
            return {width, finalWidth, halfway, bounded, complete: bar.style.transform};
        });
        assert.equal(progress.width, progress.finalWidth);
        assert.equal(progress.halfway, 'scaleX(0.5)');
        assert.equal(progress.bounded, 'scaleX(1)');
        assert.equal(progress.complete, 'scaleX(1)');
        assert.deepEqual(errors, []);
        console.log('Motion: layout budget, rapid reversal, clipped hit testing, focus, reduced motion and idle passed.');
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive:true, force:true});
    }
})().catch(error => {console.error(error); process.exitCode=1;});
