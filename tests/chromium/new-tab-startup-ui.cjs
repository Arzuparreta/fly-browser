// Optional native startup/first-frame acceptance and repeatable timing report.
// Use an owned display and a release binary for comparable before/after runs:
// FLY_NATIVE_BINARY=... FLY_PLAYWRIGHT_MODULE=... DISPLAY=... node <this file>
// FLY_NEW_TAB_EVIDENCE optionally retains screenshots and timings as JSON.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {chromium} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const binary = process.env.FLY_NATIVE_BINARY;
assert(binary, 'Set FLY_NATIVE_BINARY to a built native Fly browser');
const evidence = process.env.FLY_NEW_TAB_EVIDENCE;
if (evidence) fs.mkdirSync(evidence, {recursive:true});
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
const report = [];
(async () => {
    for (const mode of ['dark', 'light']) for (const scale of ['normal', 'large']) {
        const home = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-newtab-startup-'));
        const profile = path.join(home, 'profile');
        fs.mkdirSync(profile);
        fs.writeFileSync(path.join(profile, 'Local State'), JSON.stringify({fly:{
            preferences:{schema:1,values:{theme:mode,interface_scale:scale}}}}));
        let child, browser;
        try {
            const start = performance.now();
            child = spawn(binary, ['--fly-native', `--user-data-dir=${profile}`,
                '--no-first-run', '--test-type',
                '--ozone-platform=x11', '--password-store=basic', '--remote-debugging-port=0'], {
                env:{...process.env,HOME:home,XDG_CONFIG_HOME:home,XDG_CACHE_HOME:home},
                stdio:'ignore'});
            const portFile = path.join(profile, 'DevToolsActivePort');
            for (let i=0;i<1200 && !fs.existsSync(portFile);i++) {
                assert.equal(child.exitCode, null, 'Browser exited during startup');
                await wait(50);
            }
            assert(fs.existsSync(portFile), 'No debugging port within 60 seconds');
            browser = await chromium.connectOverCDP({
                endpointURL:'http://127.0.0.1:'+fs.readFileSync(portFile,'utf8').split('\n')[0],
                noDefaults:true});
            const context = browser.contexts()[0];
            const initial = context.pages().find(page => page.url().startsWith('chrome://fly-new-tab/'));
            assert(initial, 'Native startup opens Fly new tab');
            await initial.waitForFunction(() => document.getElementById('search')?.textContent);
            report.push({mode,scale,startupObservedMs:performance.now()-start});
            await context.addInitScript(() => {
                // Prove the page has its final presentation without relying on
                // the ready round trip. Normal live updates have browser tests.
                Object.defineProperty(window, 'updateFlyNewTab', {
                    configurable:true,
                    set(render) {
                        Object.defineProperty(window, 'updateFlyNewTab', {
                            value:render,writable:true,configurable:true});
                        const send = chrome.send.bind(chrome);
                        chrome.send = (message, args) => {
                            if (message === 'ready') { window.readySuppressed = true; return; }
                            send(message, args);
                        };
                    }
                });
                window.firstNewTabFrame = new Promise(resolve => {
                    const sample = () => {
                        if (!document.body) return requestAnimationFrame(sample);
                        resolve({background:getComputedStyle(document.body).backgroundColor,
                            light:document.body.classList.contains('light'),
                            scale:getComputedStyle(document.documentElement).zoom,
                            searchLeft:document.getElementById('search')?.getBoundingClientRect().left});
                    };
                    requestAnimationFrame(sample);
                });
            });
            const cdp = await browser.newBrowserCDPSession();
            for (let tab=0;tab<8;tab++) {
                const startTab = performance.now();
                const opened = context.waitForEvent('page');
                // Browser-level target creation inserts a real native tab and
                // avoids relying on CDP keyboard events reaching native Views.
                await cdp.send('Target.createTarget', {url:'chrome://fly-new-tab/'});
                const page = await opened;
                await page.waitForURL('chrome://fly-new-tab/');
                await page.waitForFunction(() => document.getElementById('search')?.textContent &&
                    performance.getEntriesByType('paint').length);
                const state = await page.evaluate(async () => ({
                    frame:await window.firstNewTabFrame,
                    readySuppressed:window.readySuppressed,
                    telemetryElements:document.querySelectorAll('#memory,#clock,#sites').length,
                    paints:performance.getEntriesByType('paint').map(p => ({name:p.name,ms:p.startTime})),
                    domMs:performance.getEntriesByType('navigation')[0].domContentLoadedEventEnd,
                    initial:JSON.parse(document.getElementById('initial-data').textContent)}));
                assert.equal(state.readySuppressed, true, 'The ready round trip was suppressed');
                assert.equal(state.telemetryElements, 0, 'Quiet has no telemetry or history surface');
                assert.equal(state.frame.light, mode==='light', 'First frame uses the saved theme');
                assert.equal(state.frame.scale, scale==='large' ? '1.25' : '1', 'First frame has final scale');
                assert.equal(state.frame.background, mode==='light' ? 'rgb(236, 230, 216)' : 'rgb(18, 18, 17)');
                assert(state.frame.searchLeft >= state.initial.sidebarOverhang-1, 'First frame reserves sidebar');
                report.push({mode,scale,tab,observedMs:performance.now()-startTab,
                    frame:state.frame,paints:state.paints,domMs:state.domMs});
                if (evidence && tab===0) {
                    await page.evaluate(() => document.fonts.ready);
                    await page.screenshot({path:path.join(evidence,`${mode}-${scale}.png`)});
                }
            }
        } finally {
            if (browser) await browser.close();
            if (child && child.exitCode===null) {
                child.kill('SIGTERM');
                for (let i=0;i<100 && child.exitCode===null;i++) await wait(50);
                if (child.exitCode===null) child.kill('SIGKILL');
            }
            fs.rmSync(home, {recursive:true,force:true});
        }
    }
    if (evidence) fs.writeFileSync(path.join(evidence,'timings.json'),JSON.stringify(report,null,2)+'\n');
    console.log(JSON.stringify(report,null,2));
})().catch(error => {console.error(error);process.exitCode=1;});
