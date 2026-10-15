const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const launch = require('./electron-cdp-fixture');
const root = path.resolve(__dirname, '..');
(async () => {
    const requests = [];
    const server = http.createServer((req, res) => { requests.push(req.url); res.end('<!doctype html><title>Restored page</title><div style="height:5000px"></div>'); });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}`;
    try {
        for (const cleanExit of [true, false]) {
            requests.length = 0;
            const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-restoration-'));
            const config = path.join(directory, '.config/fly-browser');
            fs.mkdirSync(config, {recursive: true});
            fs.writeFileSync(path.join(config, 'config.json'), JSON.stringify({extensions_enabled: false, tab_sleep_timeout_minutes: 0, last_seen_version: require('../package.json').version}));
            fs.writeFileSync(path.join(config, 'session_sentinel.json'), JSON.stringify({clean_exit: cleanExit}));
            fs.writeFileSync(path.join(config, 'session.json'), JSON.stringify({current_workspace: 'Work', workspaces: {Work: [`${base}/active`, `${base}/old`], Other: [`${base}/other`]}, tab_titles: {Work: ['Active', 'Saved draft']}, tab_sleep_states: {Work: [null, {url: `${base}/restored#part`, title: 'Saved draft', scrollY: 840, sessionStorage: '{"draft":"legacy value"}'}]}}));
            let application;
            try {
                application = await launch(root, directory);
                const {page} = application;
                await page.waitForFunction(() => !!window.executeBrowserAction);
                await page.waitForFunction(() => {try {return document.querySelector('webview') && !document.querySelector('webview').isLoading();} catch {return false;}});
                assert.equal(await page.evaluate(() => document.querySelectorAll('webview').length), 1);
                assert.equal(requests.filter(url=>url.startsWith('/restored') || url.startsWith('/other') || url.startsWith('/old')).length, 0);
                const settings = await page.evaluate(() => window.flyAPI.getBrowserSettings());
                assert.equal(settings.tab_suspend_timeout_minutes, 0);
                assert(!Object.hasOwn(settings, 'tab_sleep_timeout_minutes'));
                await page.evaluate(async () => (await import('./modules/webview.js')).switchTabFocus(1));
                await page.waitForFunction(() => {try {const view = [...document.querySelectorAll('webview')].find(v=>v.getClientRects().length); return view && !view.isLoading() && view.getURL().endsWith('/restored#part');} catch{return false;}});
                await page.waitForFunction(async () => {
                    const view = [...document.querySelectorAll('webview')].find(v=>v.getClientRects().length);
                    return await view.executeJavaScript('scrollY === 840 && sessionStorage.getItem("draft") === "legacy value"');
                });
                const session = await page.evaluate(async () => (await import('./modules/state.js')).state.sessionState);
                assert(!Object.hasOwn(session, 'tab_sleep_states'));
                assert.equal(session.workspaces.Work[1], `${base}/restored#part`);
                assert.deepEqual(page.errors, []);
            } finally {
                if (application) await application.close();
                fs.rmSync(directory, {recursive: true, force: true});
            }
        }
        console.log('Legacy settings and sessions migrate with Never preserved; clean/crash startup loads only the selected tab; deferred URL, scroll and storage restore on demand.');
    } finally {server.closeAllConnections(); await new Promise(resolve => server.close(resolve));}
})().catch(error => {console.error(error); process.exitCode = 1;});
