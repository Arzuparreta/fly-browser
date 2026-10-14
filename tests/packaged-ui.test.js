const {_electron} = require(process.env.MISE_PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const assert = require('node:assert/strict');
const asar = require('@electron/asar');

(async () => {
    const executablePath = process.argv[2];
    if (!executablePath) throw new Error('Supply the packaged Linux executable path.');
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-packaged-'));
    const configDir = path.join(temporary, '.config/mise-browser');
    fs.mkdirSync(configDir, {recursive: true});
    const version = require('../package.json').version;
    fs.writeFileSync(path.join(configDir, 'config.json'), JSON.stringify({last_seen_version: version}));
    fs.writeFileSync(path.join(configDir, 'session.json'), JSON.stringify({current_workspace: 'Smoke', workspaces: {Smoke: ['about:blank']}}));
    let application;
    try {
        application = await _electron.launch({executablePath: path.resolve(executablePath),
            args: ['--no-sandbox', '--disable-gpu', '--ozone-platform=x11'],
            env: {...process.env, HOME: temporary, XDG_CONFIG_HOME: path.join(temporary, '.config'), XDG_CACHE_HOME: path.join(temporary, '.cache'), XDG_DATA_HOME: path.join(temporary, '.local/share')}});
        const paths = await application.evaluate(({app}) => ({home: app.getPath('home'), data: app.getPath('userData'), app: app.getAppPath()}));
        assert.equal(paths.home, temporary);
        assert(paths.data.startsWith(temporary + path.sep), 'Packaged smoke test must use a disposable profile');
        assert(paths.app.endsWith('app.asar'), 'Test must execute the real packaged application');
        assert.match(asar.extractFile(paths.app, 'extensions.js').toString(), /function initializeExtensions/, 'Required local extension runtime must be in the archive');
        for (const name of ['adblocker-build.js', 'adblocker-worker.js']) assert(asar.extractFile(paths.app, name).length > 0, `${name} must be packaged`);
        const cache = path.join(paths.data, 'adblock_cache.bin');
        for (let attempt = 0; !fs.existsSync(cache); attempt++) {
            if (attempt === 600) throw new Error('Packaged cold filter compiler did not create its cache');
            await new Promise(resolve => setTimeout(resolve, 50));
        }
        const page = await application.firstWindow();
        const errors = []; page.on('pageerror', error => errors.push(error.message));
        await page.waitForFunction(() => !!window.executeBrowserAction && document.querySelectorAll('#TabList li').length === 1);
        assert.equal((await page.evaluate(() => window.miseAPI.getAppVersion())).version, version);
        assert.equal((await page.evaluate(() => window.miseAPI.getBrowserSettings())).extensions_enabled, false);
        assert.deepEqual(errors, []);
        console.log('Packaged ASAR, cold filter worker/cache, application startup, version, disabled extensions and disposable paths passed.');
    } finally {
        if (application) await application.close();
        fs.rmSync(temporary, {recursive: true, force: true});
    }
})().catch(error => {console.error(error); process.exitCode = 1;});
