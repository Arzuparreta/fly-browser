// Disposable trust root for exercising the real GnuPG verifier in CI.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
module.exports = function signing(directory) {
    const home = path.join(directory, 'signing');
    fs.mkdirSync(home, {mode:0o700});
    const run = args => {
        const result = spawnSync('gpg', ['--homedir', home, '--batch', '--yes', ...args], {encoding:null});
        assert.equal(result.status, 0, result.stderr?.toString());
        return result.stdout;
    };
    run(['--passphrase', '', '--quick-generate-key', 'Fly test key', 'ed25519', 'sign', '0']);
    const key = run(['--export']);
    const armored = run(['--armor', '--export']);
    return {
        key,
        sign(file) { run(['--armor', '--detach-sign', file]); },
        installer(source, destination) {
            const script = fs.readFileSync(source, 'utf8');
            const trusted = /^release_key='([^']+)'$/m.exec(script);
            assert.ok(trusted, 'Installer must pin a release key');
            fs.writeFileSync(destination, script.replace(trusted[0], `release_key='${key.toString('base64')}'`)
                .replace(/^release_key_armored='[^']+'$/m, `release_key_armored='${armored.toString('base64')}'`));
        }
    };
};
