const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-tab-menu-'));
    const entry = createElectronFixture(root, temporary);
    const requests = [];
    const server = http.createServer((request, response) => {
        requests.push(request.url);
        response.end(`<!doctype html><title>${request.url}</title>`);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const base = `http://127.0.0.1:${server.address().port}`;
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        // Smoke-test a real native popup; invoke later commands through the same Menu items.
        // Avoid manipulating native popup lifetimes while invoking synthetic click callbacks.
        await application.evaluate(({Menu}) => {
            const popup = Menu.prototype.popup;
            global.showNativeTabMenu = true;
            Menu.prototype.popup = function(options) {
                global.tabMenu = this;
                global.tabMenuClosed = options.callback;
                if (global.showNativeTabMenu) return popup.call(this, options);
            };
        });
        const read = () => page.evaluate(async () => {
            const {state} = await import('./modules/state.js');
            const ws = state.sessionState.current_workspace;
            return {urls: [...state.sessionState.workspaces[ws]], pinned: [...(state.tabPinned[ws] || [])],
                selected: [...document.querySelectorAll('#TabList li')].findIndex(li => li.classList.contains('selected'))};
        });
        const reset = async () => {
            await page.evaluate(async () => {
                const {state} = await import('./modules/state.js');
                const {handleTabRemoval, clearClosedTabs} = await import('./modules/webview.js');
                state.tabPinned[state.sessionState.current_workspace] = [];
                while (document.querySelector('#TabList li')) handleTabRemoval(0);
                clearClosedTabs();
            });
            for (const name of ['one', 'two', 'three']) {
                await page.evaluate(url => window.executeBrowserAction('spawn-tab-with-url', url), `${base}/${name}`);
            }
            await page.waitForFunction(() => [...document.querySelectorAll('webview')].every(view => { try { return !view.isLoading(); } catch { return false; } }));
        };
        const open = async index => {
            await application.evaluate(() => { global.tabMenu = null; });
            // A DOM contextmenu also covers an unselected row without a preliminary left click.
            await page.locator('#TabList li').nth(index).dispatchEvent('contextmenu', {button: 2});
            for (let attempt = 0; !await application.evaluate(() => !!global.tabMenu); attempt++) {
                assert(attempt < 100, 'Native menu must open');
                await new Promise(resolve => setTimeout(resolve, 20));
            }
            return application.evaluate(() => global.tabMenu.items.filter(item => item.type !== 'separator').map(item => ({id: item.id, label: item.label, enabled: item.enabled})));
        };
        const choose = async id => {
            await application.evaluate((_, id) => {
                const item = global.tabMenu.getMenuItemById(id);
                if (!item?.enabled) throw new Error(`Command is unavailable: ${id}`);
                item.click();
                global.tabMenuClosed();
            }, id);
            await page.waitForTimeout(150);
        };
        await reset();
        await application.evaluate(() => { global.tabMenu = null; });
        await page.locator('#TabList li').first().click({button: 'right', position: {x: 14, y: 20}});
        for (let attempt = 0; !await application.evaluate(() => !!global.tabMenu); attempt++) {
            assert(attempt < 100, 'Physical right click must open a native menu');
            await new Promise(resolve => setTimeout(resolve, 20));
        }
        await application.evaluate(() => { global.tabMenu.closePopup(); global.showNativeTabMenu = false; });
        const menu = await open(0);
        assert.equal((await read()).selected, 2, 'Opening the menu must preserve selection');
        assert.equal(menu.find(item => item.id === 'reopen').enabled, false);
        await choose('duplicate');
        assert.deepEqual((await read()).urls, [`${base}/one`, `${base}/one`, `${base}/two`, `${base}/three`]);
        assert.equal((await read()).selected, 1);

        await reset();
        await open(0); await choose('pin');
        assert.equal((await read()).pinned[0], true);
        assert.equal((await read()).selected, 2, 'Pinning a background tab preserves selection');
        const pinnedMenu = await open(0);
        assert.equal(pinnedMenu.find(item => item.id === 'pin').label, 'Unpin');
        assert.equal(pinnedMenu.find(item => item.id === 'close').enabled, false);
        await choose('mute');
        assert.equal(await page.evaluate(() => document.querySelector('webview').isAudioMuted()), true);
        const mutedMenu = await open(0);
        assert.equal(mutedMenu.find(item => item.id === 'mute').label, 'Unmute tab');
        const loadsBeforeReload = requests.filter(url => url === '/one').length;
        await choose('reload');
        for (let attempt = 0; requests.filter(url => url === '/one').length <= loadsBeforeReload; attempt++) {
            assert(attempt < 100, 'Reload must request the clicked background page');
            await new Promise(resolve => setTimeout(resolve, 20));
        }
        await page.waitForFunction(() => {
            const view = document.querySelector('webview');
            return !view.isLoading() && view.isAudioMuted();
        });
        assert.equal((await read()).selected, 2);
        await open(1); await choose('close-others');
        assert.deepEqual((await read()).urls, [`${base}/one`, `${base}/two`], 'Bulk close protects pinned tabs');
        await open(1); await choose('reopen');
        assert((await read()).urls.includes(`${base}/three`));

        await reset();
        await open(0); await choose('close-right');
        assert.deepEqual((await read()).urls, [`${base}/one`]);
        await open(0); await choose('new-tab');
        assert.deepEqual((await read()).urls, [`${base}/one`, 'about:blank']);
        assert.equal(await page.locator('#WideAddressBar').isVisible(), true);
        await page.evaluate(async () => (await import('./modules/navigation.js')).dismissAddressBar());
        await open(0); await choose('close');
        assert.deepEqual((await read()).urls, ['about:blank']);

        // A restored tab can be muted and reloaded without activating it.
        await reset();
        await page.evaluate(async () => {
            const {state} = await import('./modules/state.js');
            const ws = state.sessionState.current_workspace;
            state.activeViewsCache[ws][0].remove();
            state.activeViewsCache[ws][0] = null;
        });
        await open(0); await choose('mute');
        await open(0); await choose('reload');
        await page.waitForFunction(() => { try { return document.querySelector('webview:last-child').isAudioMuted(); } catch { return false; } });
        assert.equal((await read()).selected, 2);

        // Rebuilding the list while the popup is open invalidates its target.
        await open(0);
        await page.evaluate(async () => (await import('./modules/webview.js')).renderWorkspaceUI(2));
        await choose('close');
        assert.equal((await read()).urls.length, 3);
        assert.deepEqual(errors, []);
        console.log('Native sidebar tab menu: commands, background targeting, pinned protection, restored tabs and stale targets passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
