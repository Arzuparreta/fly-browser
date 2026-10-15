// Electron check of the sidebar's motion contract: the rail and the expanded
// sidebar share one layout, only compositor properties animate, the panel and
// its content move in step, and nothing animates at rest.
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
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, createElectronFixture(root, temporary)]});
        const page = await application.firstWindow();
        page.setDefaultTimeout(10000);
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.mouse.move(600, 400);
        await page.evaluate(async () => {
            window.applySidebarMode(true);
            document.activeElement.blur();
            const {state} = await import('./modules/state.js');
            const {renderWorkspaceUI} = await import('./modules/webview.js');
            const workspace = state.sessionState.current_workspace;
            state.sessionState.workspaces[workspace] = Array.from({length: 30}, (_, i) => `https://example.invalid/${i}`);
            state.tabPinned = {...state.tabPinned, [workspace]: [false, false, true]};
            renderWorkspaceUI(1);
        });
        const settle = () => page.evaluate(() => Promise.all(document.getAnimations().map(animation => animation.finished)));
        const setExpanded = async expanded => {
            await page.evaluate(expanded => document.getElementById('Sidebar').classList.toggle('expanded', expanded), expanded);
            await settle();
        };
        const edge = () => page.evaluate(() => document.getElementById('Sidebar').getBoundingClientRect().right);
        const geometry = () => page.evaluate(() => {
            const rect = selector => document.querySelector(selector).getBoundingClientRect().toJSON();
            return {
                badge: rect('#WorkspaceBadge'), pin: rect('#pin-sidebar-btn'),
                favicon: rect('#TabList li.selected .tab-favicon-wrapper'), title: rect('#TabList li.selected .tab-title'),
                rows: [...document.querySelectorAll('#TabList li')].slice(0, 8).map(row => row.getBoundingClientRect().toJSON())
            };
        });

        // One layout: expanding moves only the edge, never an icon, row or text.
        await setExpanded(false);
        assert.equal(await edge(), 36);
        const rail = await geometry();
        assert.equal(rail.badge.x + rail.badge.width / 2, 18, 'The workspace badge is centred on the rail');
        assert.equal(rail.favicon.x + rail.favicon.width / 2, 18, 'Favicons are centred on the rail');
        assert.equal(rail.pin.x + rail.pin.width / 2, 18, 'The pin is centred on the rail');
        assert(rail.title.x >= 36, 'Tab text starts beyond the rail');
        await setExpanded(true);
        assert.equal(await edge(), 260);
        assert.deepEqual(await geometry(), rail, 'Expanding must not move or resize sidebar content');

        // The rail hides the rest of each row from the pointer, not just from view.
        await setExpanded(false);
        const hit = await page.evaluate(({x, y}) => ['rail', 'page'].map((_, i) => document.elementFromPoint(i ? 120 : x, y)?.closest('#Sidebar')?.id || null), {x: 18, y: rail.rows[3].y + 20});
        assert.deepEqual(hit, ['Sidebar', null], 'Only the 36px rail receives pointer input');

        // Only compositor properties animate, and the content cancels the panel's slide.
        const samples = await page.evaluate(async () => {
            const sidebar = document.getElementById('Sidebar');
            const content = sidebar.querySelector('.sidebar-content');
            const translate = element => new DOMMatrixReadOnly(getComputedStyle(element).transform).m41;
            sidebar.classList.add('expanded');
            const properties = new Set(document.getAnimations().map(animation => animation.transitionProperty));
            const offsets = [];
            for (let frame = 0; frame < 12; frame++) {
                await new Promise(requestAnimationFrame);
                offsets.push([translate(sidebar), translate(content)]);
            }
            await Promise.all(document.getAnimations().map(animation => animation.finished));
            return {properties: [...properties].sort(), offsets};
        });
        assert.deepEqual(samples.properties, ['opacity', 'transform']);
        assert(samples.offsets.some(([panel]) => panel > -224 && panel < 0), 'The panel slides through intermediate positions');
        for (const [panel, content] of samples.offsets) assert(Math.abs(panel + content) < 0.01, `Content must stay still: ${panel} + ${content}`);

        // Interrupted transitions settle on the last requested state.
        await page.evaluate(async () => {
            const sidebar = document.getElementById('Sidebar');
            for (let i = 0; i < 9; i++) { sidebar.classList.toggle('expanded', i % 2 === 0); await new Promise(resolve => setTimeout(resolve, 25)); }
            sidebar.classList.remove('expanded');
        });
        await settle();
        assert.equal(await edge(), 36);
        assert.deepEqual(await geometry(), rail);

        // Focus expands the rail; a guest pointer entry older than the host's does not collapse it.
        await page.locator('#back-btn').focus();
        await settle();
        assert.equal(await edge(), 260, 'Focusing a control in the rail expands the sidebar');
        await page.evaluate(() => document.activeElement.blur());
        const guestEntry = age => page.evaluate(age => {
            const row = document.querySelector('#TabList li');
            row.dispatchEvent(new MouseEvent('mouseover', {bubbles: true}));
            const event = new Event('ipc-message');
            event.channel = 'guest-pointer-enter';
            event.args = [performance.timeOrigin + performance.now() + age];
            [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).dispatchEvent(event);
            return document.getElementById('Sidebar').classList.contains('pointer-inside');
        }, age);
        assert.equal(await guestEntry(-1000), true, 'A stale guest entry must not reverse a newer sidebar hover');
        assert.equal(await guestEntry(5), false, 'A newer guest entry collapses the sidebar');
        await settle();

        // Pinned: the sidebar is part of the layout and does not transform.
        await page.evaluate(() => window.applySidebarMode(false));
        assert.equal(await page.evaluate(() => getComputedStyle(document.getElementById('Sidebar')).transform), 'none');
        assert.equal(await edge(), 260);
        await page.evaluate(() => { window.applySidebarMode(true); document.activeElement.blur(); });
        await settle();

        // Reduced motion applies immediately; at rest nothing animates, including the update notice.
        await page.emulateMedia({reducedMotion: 'reduce'});
        await page.evaluate(() => document.getElementById('Sidebar').classList.add('expanded'));
        assert.equal(await page.evaluate(() => document.getAnimations().length), 0, 'Reduced motion settles immediately');
        assert.equal(await edge(), 260);
        await page.emulateMedia({reducedMotion: 'no-preference'});
        await page.evaluate(async () => {
            document.getElementById('Sidebar').classList.remove('expanded');
            const {displayUpdateNotification} = await import('./renderer.js');
            displayUpdateNotification({updateAvailable: true, currentVersion: '0.15.0', latestVersion: '99.0.0', releaseUrl: 'https://example.invalid/release'});
        });
        await settle();
        await page.waitForTimeout(100);
        assert.equal(await page.evaluate(() => document.getAnimations().length), 0, 'No decorative animations at rest');

        // Progress updates keep the track's geometry and move only the fill.
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
        console.log('Motion: shared rail layout, compositor-only slide, still content, hit testing, reversal, stale guest entry, pinning, reduced motion, idle and downloads passed.');
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
