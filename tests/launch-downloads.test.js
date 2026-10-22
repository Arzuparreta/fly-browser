// launch.sh without a local engine build: published release and pull request
// test builds, downloaded through a fake GitHub (PATH curl) into a disposable HOME.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-launch-downloads-'));
try {
    // A checkout with no engine beside it.
    const checkout = path.join(temp, 'projects/fly-browser');
    fs.mkdirSync(checkout, {recursive:true});
    fs.copyFileSync(path.join(root, 'launch.sh'), path.join(checkout, 'launch.sh'));
    const home = path.join(temp, 'home with spaces');
    const bin = path.join(temp, 'bin');
    const github = path.join(temp, 'github');
    const requests = path.join(temp, 'requests');
    for (const dir of [home, bin, github]) fs.mkdirSync(dir, {recursive:true});
    // curl serves https://<host>/<path> from github/<path>; the request log
    // shows what was downloaded.
    fs.writeFileSync(path.join(bin, 'curl'), `#!/bin/bash
out=
while [ $# -gt 0 ]; do
  case $1 in -o) out=$2; shift;; -*) ;; *) url=$1;; esac
  shift
done
echo "$url" >> "${requests}"
[ -n "\${FLY_TEST_OFFLINE:-}" ] && exit 6
file="${github}/\${url#https://*/}"
[ -f "$file" ] || exit 22
if [ -n "$out" ]; then cp "$file" "$out"; else cat "$file"; fi
`, {mode:0o755});
    const env = {...process.env, HOME:home, XDG_CACHE_HOME:path.join(home, 'cache'),
        XDG_CONFIG_HOME:path.join(home, 'config'), PATH:`${bin}:${process.env.PATH}`, DISPLAY:':0'};
    delete env.FLY_ENGINE_ROOT; delete env.FLY_ENGINE_OUTPUT;
    const launch = (args, extra = {}) => {
        fs.rmSync(requests, {force:true});
        const result = spawnSync('bash', [path.join(checkout, 'launch.sh'), ...args],
            {env:{...env, ...extra}, encoding:'utf8'});
        result.requests = fs.existsSync(requests) ? fs.readFileSync(requests, 'utf8') : '';
        return result;
    };
    // Publishes a package whose browser prints its name and arguments.
    const publish = (tag, label, {corrupt = false} = {}) => {
        const stage = path.join(temp, 'stage', tag);
        fs.rmSync(stage, {recursive:true, force:true});
        fs.mkdirSync(path.join(stage, 'fly-browser/app'), {recursive:true});
        fs.writeFileSync(path.join(stage, 'fly-browser/app/fly'),
            `#!/bin/sh\necho "${label}"\nprintf "%s\\n" "$@"\nexit 23\n`, {mode:0o755});
        const dir = path.join(github, 'Arzuparreta/fly-browser/releases/download', tag);
        fs.mkdirSync(dir, {recursive:true});
        const name = 'Fly-9.9.9-native-linux-x64.tar.gz';
        assert.equal(spawnSync('tar', ['-czf', path.join(dir, name), '-C', stage, 'fly-browser']).status, 0);
        const digest = corrupt ? '0'.repeat(64)
            : crypto.createHash('sha256').update(fs.readFileSync(path.join(dir, name))).digest('hex');
        fs.writeFileSync(path.join(dir, 'SHA256SUMS.txt'), `${digest}  ${name}\n`);
    };
    const api = path.join(github, 'repos/Arzuparreta/fly-browser/releases');
    fs.mkdirSync(api, {recursive:true});
    fs.writeFileSync(path.join(api, 'latest'), JSON.stringify({tag_name:'v9.9.9'}, null, 2));
    publish('v9.9.9', 'release 9.9.9');
    const profile = `${env.XDG_CONFIG_HOME}/fly-browser-dev`;
    const flags = `--fly-native\n--class=fly-browser-dev\n--user-data-dir=${profile}\n--fly-import-from=${profile}/empty-import\n`;

    // No local build: the published release, checked and started with the
    // development profile and the caller's arguments.
    const first = launch(['https://example.org/a b']);
    assert.equal(first.status, 23, first.stderr);
    assert.equal(first.stdout, `release 9.9.9\n${flags}https://example.org/a b\n`);
    assert.match(first.stderr, /not built on this computer; using the published release/);
    assert.match(first.requests, /Fly-9\.9\.9-native-linux-x64\.tar\.gz/);
    // The next start reuses the download.
    const again = launch([]);
    assert.equal(again.status, 23);
    assert.doesNotMatch(again.requests, /\.tar\.gz/);
    console.log('Without a local build the launcher starts the verified published release and caches it.');

    // A pull request's test build, replaced when a newer one is published.
    publish('test-pr-153', 'pr 153 first');
    const pr = launch(['--pr', '153', '--new-window']);
    assert.equal(pr.status, 23, pr.stderr);
    assert.equal(pr.stdout, `pr 153 first\n${flags}--new-window\n`);
    assert.match(pr.requests, /releases\/download\/test-pr-153\/SHA256SUMS\.txt/);
    assert.doesNotMatch(pr.requests, /releases\/latest/);
    publish('test-pr-153', 'pr 153 second');
    assert.match(launch(['--pr=153']).stdout, /^pr 153 second\n/);
    const cached = fs.readdirSync(path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-153'));
    assert.equal(cached.length, 1, 'only the newest test build of a pull request is kept');
    // Offline, the last download of that pull request still starts.
    const offline = launch(['--pr', '153'], {FLY_TEST_OFFLINE:'1'});
    assert.equal(offline.status, 23);
    assert.match(offline.stdout, /^pr 153 second\n/);
    assert.match(offline.stderr, /starting the last downloaded test build of pull request #153/);
    console.log('--pr N starts the newest test build of the pull request, also offline once downloaded.');

    // Missing builds, bad checksums and bad numbers start nothing.
    const missing = launch(['--pr', '7']);
    assert.equal(missing.status, 1);
    assert.equal(missing.stdout, '');
    assert.match(missing.stderr, /no test build of pull request #7.*test-build\.py --pr 7/);
    publish('test-pr-8', 'tampered', {corrupt:true});
    const tampered = launch(['--pr', '8']);
    assert.equal(tampered.status, 1);
    assert.equal(tampered.stdout, '');
    assert.match(tampered.stderr, /does not match its SHA-256 checksum/);
    assert.deepEqual(fs.readdirSync(path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-8')), []);
    assert.match(launch(['--pr', 'x']).stderr, /must be a number/);
    console.log('Missing or tampered test builds start nothing and leave nothing behind.');

    // A local build still wins without options; --release overrides it.
    const engine = path.join(temp, 'projects/fly-chromium/src/out/FlyNative');
    fs.mkdirSync(engine, {recursive:true});
    fs.writeFileSync(path.join(engine, 'chrome'), '#!/bin/sh\necho local\nexit 19\n', {mode:0o755});
    const local = launch([]);
    assert.equal(local.status, 19);
    assert.equal(local.requests, '', 'a local build needs no network');
    assert.equal(launch(['--release']).status, 23);
    console.log('A local build is used without network; --release and --pr still download.');
} finally { fs.rmSync(temp, {recursive:true, force:true}); }
