const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, '../security.js'), 'utf8');

(async () => {
    for (const scenario of ['cold', 'cached', 'corrupt', 'retry']) {
        const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mise-blocker-test-'));
        try {
            if (scenario === 'cached' || scenario === 'corrupt') fs.writeFileSync(path.join(temporary, 'adblock_cache.bin'), scenario);
            let builds = 0, deserialisations = 0, writes = 0, release;
            const sessions = new Set(), rules = [];
            const engine = {
                serialize: () => Buffer.from('cached'),
                updateFromDiff: diff => rules.push(diff),
                enableBlockingInSession: session => sessions.add(session),
                isBlockingEnabled: session => sessions.has(session),
                onBeforeRequest: (_details, callback) => callback({cancel: true}),
                onHeadersReceived: (details, callback) => callback({responseHeaders: details.responseHeaders})
            };
            const context = {
                module: {exports: {}}, __dirname: path.resolve(__dirname, '..'), console: {warn() {}, error() {}}, process,
                require: id => {
                    if (id === 'fs') return {...fs, writeFileSync: (...args) => {writes++; return fs.writeFileSync(...args);}};
                    if (id === 'electron') return {app: {getPath: () => temporary}, ipcMain: {removeHandler() {}}};
                    if (id === './adblocker-build') return () => {builds++; return new Promise((resolve, reject) => {release = ok => ok ? resolve(engine) : reject(new Error('Offline'));});};
                    if (id === '@ghostery/adblocker-electron') return {ElectronBlocker: {
                        deserialize: buffer => {deserialisations++; if (buffer.toString() === 'corrupt') throw new Error('Invalid cache'); return engine;},
                        fromPrebuiltAdsAndTracking: () => {builds++; return new Promise((resolve, reject) => {release = ok => ok ? resolve(engine) : reject(new Error('Offline'));});}
                    }};
                    return require(id);
                }
            };
            vm.createContext(context); vm.runInContext(source, context);
            context.module.exports.setTrustedDomains(['example.org']);
            const targets = Array.from({length: 3}, () => {
                const handlers = {};
                return {handlers, webRequest: {
                    onBeforeRequest: (_filter, handler) => {handlers.before = handler;},
                    onHeadersReceived: (_filter, handler) => {handlers.headers = handler;}
                }};
            });
            let pending = targets.map(target => context.initialiseAdblocker(target));
            assert.equal(builds, scenario === 'cached' ? 0 : 1, 'Concurrent sessions must share one pending build');
            if (scenario !== 'cached') release(scenario !== 'retry');
            await Promise.all(pending);
            if (scenario === 'retry') {
                assert.equal(sessions.size, 0, 'Failed build must not attach an absent engine');
                pending = targets.map(target => context.initialiseAdblocker(target));
                assert.equal(builds, 2, 'A later request must retry once for all waiting sessions');
                release(true); await Promise.all(pending);
            }
            assert.equal(sessions.size, 3, 'Every workspace session must receive the shared blocker');
            assert.equal(writes, scenario === 'cached' ? 0 : 1);
            assert.equal(deserialisations, ['cached', 'corrupt'].includes(scenario) ? 1 : 0);
            assert.equal(rules.filter(rule => rule.added.includes('youtube.com#@#+js()')).length, 1, 'Shared exceptions are installed once');
            assert(rules.some(rule => rule.added.some(filter => filter.includes('example.org'))), 'Trusted-domain exceptions must survive initialisation');
            for (const target of targets) {
                let result;
                target.handlers.before({url: 'https://tracker.invalid'}, value => {result = value;});
                assert.equal(result.cancel, true, 'Ordinary requests must still reach the blocker');
                target.handlers.before({url: 'https://accounts.google.com/?flowName=GlifWebSignIn'}, value => {result = value;});
                assert.match(result.redirectURL, /WebLiteSignIn/, 'Mise authentication rules must remain attached');
            }
        } finally { fs.rmSync(temporary, {recursive: true, force: true}); }
    }
    console.log('Shared adblocker startup, cached/corrupt builds, retry and per-session protection passed.');
})().catch(error => {console.error(error); process.exitCode = 1;});
