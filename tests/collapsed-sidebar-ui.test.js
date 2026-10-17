// Run directly with Electron/Playwright: geometry and live rail audio states.
const {_electron} = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-collapsed-sidebar-'));
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Audio fixture</title>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({
            executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, createElectronFixture(root, temporary)]
        });
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = [];
        page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.mouse.move(650, 400);
        await page.evaluate(async base => {
            const {state} = await import('./modules/state.js');
            const {renderWorkspaceUI} = await import('./modules/webview.js');
            const ws = state.sessionState.current_workspace;
            const names = ['GitHub · Fly', 'Design notes', 'Music', 'Muted video', 'Documentation', 'Reading list', 'New tab · Fly'];
            const colours = ['#b4ad9e', '#dd9b52', '#60a878', '#d0695e', '#699bd1', '#b28bbd', '#e0261c'];
            state.sessionState.workspaces[ws] = names.map((_, i) => i === 6 ? 'about:blank' : `${base}/${i}`);
            state.activeTitlesCache[ws] = names;
            state.tabFavicons[ws] = colours.map((colour, i) => 'data:image/svg+xml,' + encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16"><rect width="16" height="16" rx="3" fill="${colour}"/><text x="8" y="12" text-anchor="middle" font-family="sans-serif" font-size="12" font-weight="bold" fill="#111">${['G','N','♪','▶','D','R','F'][i]}</text></svg>`));
            state.tabPinned[ws] = [true];
            state.tabMediaAudible[ws] = [false, false, true];
            state.tabAudioMuted[ws] = [false, false, false, true];
            renderWorkspaceUI(6);
            window.applySidebarMode(true);
            document.activeElement.blur();
        }, `http://127.0.0.1:${server.address().port}`);
        const settle = () => page.evaluate(() => Promise.all(document.getAnimations().map(animation => animation.finished)));
        const expand = async value => {
            await page.evaluate(value => document.getElementById('Sidebar').classList.toggle('expanded', value), value);
            await settle();
        };
        const geometry = () => page.evaluate(() => [...document.querySelectorAll('#TabList li')].map(row => {
            const surface = getComputedStyle(row, '::after');
            const wrapper = row.querySelector('.tab-favicon-wrapper');
            const icon = wrapper.getBoundingClientRect();
            const bounds = row.getBoundingClientRect();
            const audio = wrapper.querySelector('.tab-audio-dot')?.getBoundingClientRect();
            if (audio) {
                if (audio.top < icon.bottom || audio.bottom > bounds.bottom - 1) throw new Error('Audio must fit below the favicon, inside the tab');
                if (Math.abs(audio.x + audio.width / 2 - 18) > 0.1) throw new Error('Audio must be centred on the rail');
            }
            return {row: bounds.toJSON(), icon: icon.toJSON(), width: surface.width, height: surface.height, opacity: surface.opacity};
        }));
        await expand(false);
        assert.equal(await page.locator('#Sidebar').evaluate(el => el.getBoundingClientRect().right), 36);
        const collapsed = await geometry();
        for (const [i, row] of collapsed.entries()) {
            assert.equal(row.row.height, 40);
            assert.equal(row.width, '28px');
            assert.equal(row.height, '40px');
            assert.equal(row.opacity, '1');
            if (i) assert.equal(row.row.y - collapsed[i - 1].row.bottom, 4);
        }
        assert.equal(await page.locator('.tab-audio-dot .fa-volume-high').count(), 1);
        assert.equal(await page.locator('.tab-audio-dot.is-muted .fa-volume-xmark').count(), 1);
        await page.screenshot({path: '/tmp/fly-rectangular-sidebar-dark.png', clip: {x: 0, y: 0, width: 320, height: 540}});
        await expand(true);
        const expanded = await geometry();
        expanded.forEach((row, i) => {
            assert.deepEqual(row.row, collapsed[i].row, 'Rows stay in place');
            assert.deepEqual(row.icon, collapsed[i].icon, 'Favicons stay in place');
            assert.equal(row.opacity, '0');
        });
        await page.screenshot({path: '/tmp/fly-rectangular-sidebar-expanded.png', clip: {x: 0, y: 0, width: 320, height: 540}});
        // Light mode uses the same theme operation as the interface.
        await page.evaluate(() => window.executeBrowserAction('toggle-theme'));
        await expand(false);
        await geometry();
        await page.screenshot({path: '/tmp/fly-rectangular-sidebar-light.png', clip: {x: 0, y: 0, width: 320, height: 540}});

        // A badge created during rendering must still toggle once per click
        // after a live update, rather than registering a second click listener.
        await page.evaluate(async () => {
            const {state} = await import('./modules/state.js');
            const {updateTabMediaIndicator} = await import('./modules/webview.js');
            updateTabMediaIndicator(state.sessionState.current_workspace, 2, true, false);
        });
        await page.locator('#TabList li').nth(2).locator('.tab-media-badge').dispatchEvent('click');
        assert.equal(await page.locator('#TabList li').nth(2).locator('.tab-audio-dot.is-muted').count(), 1);

        // Media events update the rail immediately, without rebuilding the tab list.
        await page.waitForFunction(() => {
            try { return [...document.querySelectorAll('webview')].find(v => v.style.display !== 'none')?.getWebContentsId() > 0; } catch { return false; }
        });
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.style.display !== 'none').loadURL(url), `http://127.0.0.1:${server.address().port}/audio`);
        await page.waitForFunction(() => {
            try { const view = [...document.querySelectorAll('webview')].find(v => v.style.display !== 'none'); return view && !view.isLoading() && view.getURL().endsWith('/audio'); } catch { return false; }
        });
        const last = page.locator('#TabList li').last();
        const mediaEvent = name => page.evaluate(name => [...document.querySelectorAll('webview')].find(v => v.style.display !== 'none').dispatchEvent(new Event(name)), name);
        await mediaEvent('media-started-playing');
        assert.equal(await last.locator('.tab-audio-dot .fa-volume-high').count(), 1);
        await page.evaluate(() => window.executeBrowserAction('toggle-tab-audio-mute'));
        assert.equal(await last.locator('.tab-audio-dot.is-muted .fa-volume-xmark').count(), 1);
        await mediaEvent('media-paused');
        assert.equal(await last.locator('.tab-audio-dot.is-muted').count(), 1, 'Muted remains visible after playback stops');
        await page.evaluate(() => window.executeBrowserAction('toggle-tab-audio-mute'));
        assert.equal(await last.locator('.tab-audio-dot').count(), 0);
        assert.equal(await last.locator('.tab-media-badge').count(), 0);
        await mediaEvent('media-started-playing');
        await mediaEvent('media-paused');
        assert.equal(await last.locator('.tab-audio-dot').count(), 0, 'Pausing removes the playing indicator');
        assert.deepEqual(errors, []);
        console.log('Collapsed sidebar: rectangular surfaces, spacing, stable icons, light/dark themes and live playing/muted/paused states passed.');
    } finally {
        if (application) await application.close();
        await new Promise(resolve => server.close(resolve));
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
