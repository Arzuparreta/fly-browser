const { _electron } = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const root = path.resolve(__dirname, '..');
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-overlay-state-'));
    const entry = path.join(temporary, 'start.cjs');
    fs.writeFileSync(entry, `const {app}=require('electron'); app.setAppPath(${JSON.stringify(root)}); app.setPath('home',${JSON.stringify(temporary)}); app.setPath('userData',${JSON.stringify(path.join(temporary, 'data'))}); require(${JSON.stringify(path.join(root, 'main.js'))});`);
    const server = http.createServer((_request, response) => response.end('<!doctype html><title>Overlay fixture</title><input id="editable" style="margin-left:260px"><button>Hint target</button>'));
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    let application;
    try {
        application = await _electron.launch({executablePath: path.join(root, 'node_modules/electron/dist/electron'), args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.MISE_TEST_OZONE_PLATFORM || 'x11'}`, entry]});
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction);
        await page.waitForFunction(() => {try {return [...document.querySelectorAll('webview')].find(view => view.getClientRects().length)?.getWebContentsId() > 0;} catch {return false;}});
        await page.evaluate(url => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).loadURL(url), `http://127.0.0.1:${server.address().port}`);
        const surfaces = [
            ['Control+l', 'WideAddressBar'], ['Control+f', 'FindBarOverlay'],
            ['Control+Shift+w', 'DashboardOverlay'], ['Control+h', 'PreferencesOverlay'],
            ['Control+p', 'CommandPaletteOverlay'], ['Control+Shift+b', 'BookmarksOverlay'],
            ['Control+Shift+h', 'HistoryOverlay'], ['Control+n', 'NotesOverlay']
        ];
        const focused = id => page.waitForFunction(id => {const root = document.getElementById(id); return root === document.activeElement || root.contains(document.activeElement);}, id, {timeout: 5000}).catch(async error => {console.log('Focus failure', id, errors, await page.evaluate(() => ({focus: document.activeElement.id, visible: [...document.querySelectorAll('[id$=Overlay], #WideAddressBar')].filter(el => el.getClientRects().length).map(el => el.id)}))); throw error;});
        // Open from the sidebar, then cancel from the input and from another chrome control.
        await page.locator('#TabList li.selected').focus();
        await page.keyboard.press('Control+l'); await focused('WideAddressBar');
        await page.keyboard.press('Escape'); await page.locator('#WideAddressBar').waitFor({state: 'hidden'});
        await page.keyboard.press('Control+l'); await focused('WideAddressBar');
        await page.locator('#menu-btn').focus(); await page.keyboard.press('Escape');
        await page.locator('#WideAddressBar').waitFor({state: 'hidden'});
        for (const [key, id] of surfaces) {
            await page.keyboard.press(key); await page.locator('#' + id).waitFor({state: 'visible'}); await focused(id);
            await page.keyboard.press('Escape'); await page.locator('#' + id).waitFor({state: 'hidden'});
            await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
            await page.keyboard.press(key); await focused(id);
            await page.keyboard.press(key); await page.locator('#' + id).waitFor({state: 'hidden'});
        }
        // Every pair of overlays must leave exactly the destination visible.
        for (const [sourceKey, source] of surfaces) {
            for (const [destinationKey, destination] of surfaces) {
                if (source === destination) continue;
                await page.keyboard.press(sourceKey); await focused(source);
                await page.keyboard.press(destinationKey); await focused(destination);
                await page.locator('#' + source).waitFor({state: 'hidden'});
                await page.keyboard.press('Escape'); await page.locator('#' + destination).waitFor({state: 'hidden'});
            }
        }
        // Child cancellation leaves its dashboard open, including from the Save button.
        await page.evaluate(() => window.executeBrowserAction('new-workspace'));
        await focused('DashboardOverlay');
        await page.locator('#submit-ws-btn').focus(); await page.keyboard.press('Escape');
        assert(await page.locator('#DashboardOverlay').isVisible());
        assert(!(await page.locator('#WorkspaceInputBox').isVisible()));
        await page.keyboard.press('Escape');
        await page.keyboard.press('Control+Shift+w');
        await focused('DashboardOverlay');
        await page.locator('[title="Rename Workspace"]').first().click();
        await page.waitForFunction(() => document.activeElement.classList.contains('dashboard-rename-input'));
        await page.keyboard.press('Escape');
        assert(await page.locator('#DashboardOverlay').isVisible());
        await focused('DashboardOverlay');
        await page.keyboard.press('Escape');
        await page.keyboard.press('Control+n'); await focused('NotesOverlay');
        await page.locator('#ToggleEditNotesBtn').click(); await focused('NotesTextArea');
        await page.keyboard.type('Preserved note'); await page.keyboard.press('Escape');
        assert(!(await page.locator('#NotesOverlay').isVisible()));
        assert.equal(await page.evaluate(() => window.miseAPI.readNotes()), 'Preserved note');
        // Page text and modal keys still work after the overlay transitions.
        const point = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('(()=>{const r=document.getElementById("editable").getBoundingClientRect(); return {x:r.x+10,y:r.y+10};})()'));
        await page.locator('webview:visible').click({position: point});
        const guestState = () => page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).executeJavaScript('({focus:document.hasFocus(), active:document.activeElement.id, value:document.getElementById("editable").value, hints:!!document.getElementById("mise-hint-layer")})'));
        const waitGuest = async predicate => {
            for (let attempt = 0; attempt < 150; attempt++) {
                if (predicate(await guestState())) return;
                await page.waitForTimeout(20);
            }
            throw new Error('Guest state timeout: ' + JSON.stringify(await guestState()));
        };
        await waitGuest(state => state.focus && state.active === 'editable');
        await page.keyboard.type('ff');
        await waitGuest(state => state.value === 'ff');
        await page.keyboard.press('Escape'); await page.keyboard.press('f');
        await waitGuest(state => state.hints);
        await page.keyboard.press('Escape');
        // Diagnostics participates in transitions and can close while its data is loading.
        await page.evaluate(() => window.executeBrowserAction('open-diagnostics'));
        await focused('DiagnosticsOverlay'); await page.keyboard.press('Control+l');
        await focused('WideAddressBar'); assert(!(await page.locator('#DiagnosticsOverlay').isVisible()));
        await page.keyboard.press('Escape');
        // Sharing can be cancelled both from its controls and its embedded guest.
        const fixture = await page.evaluate(() => [...document.querySelectorAll('webview')].find(view => view.getClientRects().length).getURL());
        await page.evaluate(url => window.executeBrowserAction('open-transient-share', url), fixture);
        await focused('ShareModalOverlay'); await page.keyboard.press('Escape');
        await page.locator('#ShareModalOverlay').waitFor({state: 'hidden'});
        await page.evaluate(url => window.executeBrowserAction('open-transient-share', url), fixture);
        await page.locator('#ShareModalWebviewContainer webview').click({position: {x: 280, y: 20}});
        await page.keyboard.press('Escape'); await page.locator('#ShareModalOverlay').waitFor({state: 'hidden'});
        await page.waitForFunction(() => document.activeElement.tagName === 'WEBVIEW');
        assert.equal(await page.locator('#ShareModalWebviewContainer webview').count(), 0);
        // Content layers must not cover the expanded sidebar, including empty workspaces.
        const sidebarAboveContent = () => page.evaluate(() => document.elementFromPoint(150, 100)?.closest('#Sidebar')?.id === 'Sidebar');
        await page.mouse.move(10, 100);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        assert(await sidebarAboveContent(), 'The expanded sidebar must remain above the live webview');
        const checkPin = async pinned => {
            await page.waitForFunction(pinned => {
                const button = document.getElementById('pin-sidebar-btn');
                return document.body.classList.contains('sidebar-pinned') === pinned &&
                    button.classList.contains('pin-active') === pinned &&
                    button.getAttribute('aria-pressed') === String(pinned);
            }, pinned);
            await page.waitForFunction(async pinned => (await window.miseAPI.getBrowserSettings()).sidebar_auto_collapse === !pinned, pinned);
        };
        const moveToContent = async () => {
            // A host content target gives CDP a real pointer exit from the sidebar.
            const bottom = await page.evaluate(() => innerHeight - 10);
            await page.mouse.move(500, bottom);
        };
        const checkPinRoutes = async () => {
            await moveToContent();
            await page.keyboard.press('Control+Shift+z');
            await checkPin(true);
            await page.waitForFunction(() => document.getElementById('right-content').getBoundingClientRect().left >= 259);
            await page.locator('#pin-sidebar-btn').click();
            await checkPin(false);
            await page.waitForFunction(() => getComputedStyle(document.getElementById('pin-sidebar-btn')).backgroundColor === 'rgba(0, 0, 0, 0)');
            await moveToContent();
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
            await page.mouse.move(10, 100);
            await page.locator('#pin-sidebar-btn').click();
            await checkPin(true);
            await moveToContent();
            await page.keyboard.press('Control+Shift+z');
            await checkPin(false);
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
            // Temporary keyboard focus expansion must not survive an unpin.
            await page.keyboard.press('Control+s');
            await page.keyboard.press('Control+Shift+z'); await checkPin(true);
            await page.keyboard.press('Control+Shift+z'); await checkPin(false);
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
            await page.evaluate(() => window.executeBrowserAction('toggle-sidebar-pin'));
            await checkPin(true);
            await page.evaluate(() => window.toggleSidebarExpansion());
            await checkPin(false);
        };
        await checkPinRoutes();
        await page.mouse.move(10, 100);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        // Window exits must not collapse the sidebar, even at the outermost pixel.
        const sidebarWidth = () => page.locator('#Sidebar').evaluate(sidebar => sidebar.getBoundingClientRect().width);
        for (const x of [-1, 0, -1, 0, -1]) {
            await page.mouse.move(x, 100);
            await page.waitForTimeout(250);
            assert(await sidebarWidth() >= 259, 'Leaving the left window edge must retain expansion');
        }
        await page.mouse.move(500, 100);
        // Host CDP pointer injection does not route into Electron's guest surface.
        const guestId = await page.locator('webview:visible').evaluate(view => view.getWebContentsId());
        await application.evaluate(({webContents}, id) => {
            const guest = webContents.fromId(id);
            guest.sendInputEvent({type: 'mouseLeave', x: -1, y: 100});
            guest.sendInputEvent({type: 'mouseMove', x: 500, y: 100});
        }, guestId);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37, null, {timeout: 5000});
        await page.mouse.move(10, 100);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        const windowHeight = await page.evaluate(() => innerHeight);
        for (const point of [{x: 10, y: -1}, {x: 10, y: windowHeight}]) {
            await page.mouse.move(point.x, point.y);
            await page.waitForTimeout(250);
            assert(await sidebarWidth() >= 259, 'Leaving the top or bottom window edge must retain expansion');
            await page.mouse.move(10, 100);
        }
        await page.evaluate(() => window.executeBrowserAction('new-workspace'));
        await page.locator('#NewWorkspaceTitleInput').fill('Empty sidebar fixture');
        await page.locator('#NewWorkspaceTitleInput').press('Enter');
        await page.locator('#EmptyWorkspaceWelcome').waitFor({state: 'visible'});
        await page.mouse.move(10, 100);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        assert(await sidebarAboveContent(), 'The expanded sidebar must paint and receive clicks above the empty workspace');
        if (process.env.MISE_SIDEBAR_SCREENSHOT) await page.screenshot({path: process.env.MISE_SIDEBAR_SCREENSHOT});
        await page.mouse.move(-1, 100);
        await page.waitForTimeout(250);
        assert(await sidebarWidth() >= 259, 'The empty-workspace sidebar must also remain expanded outside the window');
        await page.mouse.move(500, 400);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
        for (const button of ['back-btn', 'forward-btn']) {
            await page.mouse.move(10, 100);
            await page.locator('#' + button).click();
            assert.equal(await page.locator('#' + button).evaluate(button => button.matches(':focus')), false);
            await page.mouse.move(500, 400);
            await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width <= 37);
        }
        await page.mouse.move(10, 100);
        await page.waitForFunction(() => document.getElementById('Sidebar').getBoundingClientRect().width >= 259);
        await page.locator('#WorkspaceLabel').click();
        await focused('DashboardOverlay'); await page.keyboard.press('Escape');
        await page.mouse.move(500, 400);
        await checkPinRoutes();
        assert.deepEqual(errors, []);
        console.log('Live overlay cancellation, focus, pairwise transitions, child dialog and editable page checks passed.');
    } finally {
        if (application) await application.close();
        server.close(); fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
