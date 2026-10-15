const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const http = require('node:http');
const assert = require('node:assert/strict');
const createElectronFixture = require('./electron-fixture');
const root = path.resolve(__dirname, '..');

// Workspaces whose names the old derivation merged ("Trabajo" and "trabajo") get separate
// partitions, keep them across renames and restarts, and new ones never reuse a folder.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-workspace-partitions-ui-'));
    const entry = createElectronFixture(root, temporary);
    const configDir = path.join(temporary, '.config', 'fly-browser');
    fs.mkdirSync(configDir, { recursive: true });
    // A folder a deleted workspace left behind
    fs.mkdirSync(path.join(temporary, 'data', 'Partitions', 'trabajo-2'), { recursive: true });
    const server = http.createServer((req, res) => {
        res.setHeader('Set-Cookie', `ws=${req.url.slice(1)}; Path=/; Max-Age=3600`);
        res.end(`<!doctype html><title>${req.url}</title>`);
    });
    await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
    const url = `http://127.0.0.1:${server.address().port}/`;
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({ current_workspace: 'Trabajo', workspaces: { Trabajo: [url + 'upper'], trabajo: [url + 'lower'] } }));
    let application;
    const pageErrors = [];
    const launch = async () => {
        application = await _electron.launch({ executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry] });
        const page = await application.firstWindow();
        page.on('pageerror', error => pageErrors.push(error.message));
        page.setDefaultTimeout(15000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    const partitions = page => page.evaluate(() => window.flyAPI.getSession().then(session => session.workspace_partitions));
    const cookies = partition => application.evaluate(({ session }, partition) => session.fromPartition(partition).cookies.get({}).then(list => list.map(cookie => cookie.value)), partition);
    const visiblePartition = async (page, title) => {
        await page.waitForFunction(title => [...document.querySelectorAll('webview')].some(view => {
            try { return getComputedStyle(view).display !== 'none' && view.getTitle() === title; } catch { return false; }
        }), title);
        return page.locator('webview:visible').getAttribute('partition');
    };
    const header = (page, partition) => page.locator('.workspace-tree-header').filter({ has: page.locator('.workspace-container-tag', { hasText: new RegExp(`^${partition}$`) }) });
    try {
        let page = await launch();
        const stored = JSON.parse(fs.readFileSync(path.join(configDir, 'session.json'), 'utf8')).workspace_partitions;
        assert.deepEqual(stored, { Trabajo: 'persist:trabajo', trabajo: 'persist:trabajo-3' }, 'The completed assignment must be written at once');
        assert.equal(await visiblePartition(page, '/upper'), 'persist:trabajo', 'The first workspace keeps its old partition');
        await page.evaluate(() => window.executeBrowserAction('next-workspace'));
        assert.equal(await visiblePartition(page, '/lower'), 'persist:trabajo-3');
        assert.deepEqual(await cookies('persist:trabajo'), ['upper']);
        assert.deepEqual(await cookies('persist:trabajo-3'), ['lower'], 'Workspaces whose names differ in case must not share cookies');

        // A new workspace skips the assigned partitions and the left-behind folder.
        await page.evaluate(() => window.executeBrowserAction('new-workspace'));
        await page.locator('#NewWorkspaceTitleInput').fill('TRABAJO');
        await page.locator('#NewWorkspaceTitleInput').press('Enter');
        await page.locator('#WorkspaceInputBox').waitFor({ state: 'hidden' });
        assert.equal((await partitions(page)).TRABAJO, 'persist:trabajo-4');

        // Renaming keeps the partition; deleting and recreating a name gives a new one.
        await page.evaluate(() => window.executeBrowserAction('toggle-dashboard'));
        await header(page, 'trabajo-3').getByTitle('Rename Workspace').click();
        await page.locator('.dashboard-rename-input').fill('Personal');
        await page.locator('.dashboard-rename-input').press('Enter');
        await header(page, 'trabajo-4').getByTitle('Delete Workspace').click();
        await page.waitForFunction(() => !document.querySelector('.dashboard-rename-input'));
        assert.deepEqual(await partitions(page), { Trabajo: 'persist:trabajo', Personal: 'persist:trabajo-3' });
        await page.evaluate(() => window.executeBrowserAction('new-workspace'));
        await page.locator('#NewWorkspaceTitleInput').fill('TRABAJO');
        await page.locator('#NewWorkspaceTitleInput').press('Enter');
        await page.locator('#WorkspaceInputBox').waitFor({ state: 'hidden' });
        assert.equal((await partitions(page)).TRABAJO, 'persist:trabajo-5');
        await page.evaluate(() => window.flyAPI.flushSessionStore({ partition: 'persist:trabajo-3' }));
        await application.close();

        // After a restart every workspace still has its partition and its logins.
        page = await launch();
        assert.deepEqual(await partitions(page), { Trabajo: 'persist:trabajo', Personal: 'persist:trabajo-3', TRABAJO: 'persist:trabajo-5' });
        assert.deepEqual(await cookies('persist:trabajo-3'), ['lower']);
        await page.evaluate(() => window.executeBrowserAction('previous-workspace'));
        assert.equal(await visiblePartition(page, '/lower'), 'persist:trabajo-3');
        assert.deepEqual(pageErrors, []);
        console.log('Workspace partitions stay separate across migration, creation, renames, deletion and restarts.');
    } finally {
        await application?.close().catch(() => {});
        server.close();
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exit(1); });
