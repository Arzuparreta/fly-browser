const { _electron } = require(process.env.FLY_PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const createElectronFixture = require('./electron-fixture');
const {readReleases} = require('../scripts/whats-new');
const root = path.resolve(__dirname, '..');

// After an upgrade Fly opens What's New with every release the profile missed.
(async () => {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-whats-new-'));
    const entry = createElectronFixture(root, temporary);
    const configPath = path.join(temporary, '.config/fly-browser/config.json');
    const releases = readReleases();
    let application;
    const launch = async () => {
        application = await _electron.launch({
            executablePath: path.join(root, 'node_modules/electron/dist/electron'),
            args: ['--no-sandbox', '--disable-gpu', `--ozone-platform=${process.env.FLY_TEST_OZONE_PLATFORM || 'x11'}`, entry]
        });
        const page = await application.firstWindow();
        page.setDefaultTimeout(15000);
        await page.waitForFunction(() => !!window.executeBrowserAction && document.getElementById('KeyboardStatus').textContent === 'All changes saved');
        return page;
    };
    const whatsNew = page => page.waitForFunction(() => [...document.querySelectorAll('webview')].find(v => { try { return v.getURL().includes('whats-new.html') && !v.isLoading(); } catch { return false; } })?.getURL());
    const read = (page, url) => page.evaluate(url => [...document.querySelectorAll('webview')].find(v => v.getURL() === url).executeJavaScript(`({
        summary: document.getElementById('ReleaseSummary').textContent,
        date: document.getElementById('ReleaseDate').textContent,
        headings: [...document.querySelectorAll('.release-heading')].map(node => node.textContent),
        titles: [...document.querySelectorAll('.feature-title')].map(node => node.textContent),
        actions: document.querySelectorAll('.action-badge').length,
        keys: document.querySelectorAll('.feature-points kbd').length
    })`), url);
    try {
        let page = await launch();
        await application.close(); application = null;
        const cfg = JSON.parse(fs.readFileSync(configPath));
        cfg.last_seen_version = '0.17.0';
        fs.writeFileSync(configPath, JSON.stringify(cfg));

        page = await launch();
        const url = await (await whatsNew(page)).jsonValue();
        assert.match(url, /whats-new\.html\?from=0\.17\.0$/);
        const after017 = version => { const [major, minor] = version.split('.').map(Number); return major > 0 || minor > 17; };
        const missed = releases.filter(release => after017(release.version));
        const upgraded = await read(page, url);
        assert.deepEqual(upgraded.headings.map(text => text.split(' · ')[0]), missed.map(release => `Fly ${release.version}`), 'Every missed release, newest first');
        assert.match(upgraded.summary, /since Fly 0\.17\.0/);
        assert.ok(upgraded.titles.includes('uBlock Origin blocks ads') && upgraded.titles.includes('fly-install') && upgraded.titles.includes('Fixes'));
        assert.ok(upgraded.actions >= 1 && /Action needed/.test(upgraded.summary), 'Changes that need action are marked');
        assert.ok(upgraded.keys > 0, 'Inline code renders as keys');
        assert.ok(upgraded.date.length > 0);

        await page.evaluate(() => window.openWhatsNewTab());
        const plain = await (await page.waitForFunction(() => [...document.querySelectorAll('webview')].find(v => { try { return v.getURL().endsWith('whats-new.html') && !v.isLoading(); } catch { return false; } })?.getURL())).jsonValue();
        const latest = await read(page, plain);
        assert.deepEqual(latest.headings, [], 'Opened by hand, it shows the latest release alone');
        assert.equal(latest.summary.startsWith(`Fly ${releases[0].version}.`), true);
        assert.equal(JSON.parse(fs.readFileSync(configPath)).last_seen_version, require('../package.json').version);
        console.log("What's New lists every missed release after an upgrade and the latest one on demand.");
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, { recursive: true, force: true });
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
