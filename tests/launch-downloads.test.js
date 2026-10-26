// launch.sh without a local engine build: published release and pull request
// test builds, downloaded through a fake GitHub (PATH curl) into a disposable HOME.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawn, spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-launch-downloads-'));
(async () => {
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
out= format= fail=
while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift;;
    -w) format=$2; shift;;
    -*f*) fail=1;;
    -*) ;;
    *) url=$1;;
  esac
  shift
done
echo "$url" >> "${requests}"
code() { [ -z "$format" ] || printf %s "$1"; }
if [ -n "\${FLY_TEST_OFFLINE:-}" ]; then code 000; exit 6; fi
file="${github}/\${url#https://*/}"
status=\${FLY_TEST_HTTP:-}
if [ -z "$status" ]; then if [ -f "$file" ]; then status=200; else status=404; fi; fi
if [ "$status" != 200 ]; then
  [ -z "$fail" ] || exit 22
  [ -z "$out" ] || echo error > "$out"
  code "$status"; exit 0
fi
if [ -n "$out" ]; then cp "$file" "$out"; else cat "$file"; fi
code 200
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
    // The app/fly that newer packages carry (scripts/chromium/package-linux.py).
    const compatFly = /^COMPAT_FLY = '\'\'([\s\S]*?)'\'\'$/m.exec(
        fs.readFileSync(path.join(root, 'scripts/chromium/package-linux.py'), 'utf8'))[1];
    // Publishes a package whose browser prints its name and arguments: app/fly
    // as in older packages, or app/fly-browser started by app/fly.
    const publish = (tag, label, {corrupt = false, renamed = false} = {}) => {
        const stage = path.join(temp, 'stage', tag);
        fs.rmSync(stage, {recursive:true, force:true});
        fs.mkdirSync(path.join(stage, 'fly-browser/app'), {recursive:true});
        fs.writeFileSync(path.join(stage, renamed ? 'fly-browser/app/fly-browser' : 'fly-browser/app/fly'),
            `#!/bin/sh\necho "${label}"\nprintf "%s\\n" "$@"\nexit 23\n`, {mode:0o755});
        if (renamed) fs.writeFileSync(path.join(stage, 'fly-browser/app/fly'), compatFly, {mode:0o755});
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
    const flags = `--fly-native\n--class=fly-browser-dev\n--user-data-dir=${profile}\n`;

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
    assert.match(offline.stderr, /Could not download from GitHub \(HTTP 000\); starting the last downloaded copy of the test build of pull request #153/);
    // Rate limits and server errors are not "no test build": the cache stays.
    const limited = launch(['--pr', '153'], {FLY_TEST_HTTP:'403'});
    assert.equal(limited.status, 23, limited.stderr);
    assert.match(limited.stderr, /HTTP 403/);
    console.log('--pr N starts the newest test build of the pull request, also offline once downloaded.');

    // A build that is running is not deleted when a newer one arrives, and a
    // start of another build is refused while it holds the profile (Chromium
    // would only open a window in the running browser).
    const channel = path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-153');
    const [running] = fs.readdirSync(channel);
    const shell = path.join(channel, running, 'fly-browser/app/sh');
    fs.copyFileSync('/bin/sh', shell);
    fs.chmodSync(shell, 0o755);
    const profileDir = `${env.XDG_CONFIG_HOME}/fly-browser-dev`;
    const holder = spawn(shell, ['-c', 'sleep 30; :', 'sh', `--user-data-dir=${profileDir}`], {stdio:'ignore'});
    const stranger = spawn('/bin/sh', ['-c', 'sleep 30; :'], {stdio:'ignore'});
    try {
        fs.mkdirSync(profileDir, {recursive:true});
        // A stale lock naming an unrelated process does not block a start.
        fs.symlinkSync(`${os.hostname()}-${stranger.pid}`, path.join(profileDir, 'SingletonLock'));
        assert.equal(launch(['--pr', '153']).status, 23);
        fs.unlinkSync(path.join(profileDir, 'SingletonLock'));
        fs.symlinkSync(`${os.hostname()}-${holder.pid}`, path.join(profileDir, 'SingletonLock'));
        publish('test-pr-153', 'pr 153 third');
        const busy = launch(['--pr', '153']);
        assert.equal(busy.status, 1);
        assert.equal(busy.stdout, '');
        assert.match(busy.stderr, /A development Fly from another build is running/);
        // Chromium rewrites its command line as one space-separated string.
        const joined = spawn(shell, ['-c', 'sleep 30; :', `fly --fly-native --user-data-dir=${profileDir} --class=fly-browser-dev`], {stdio:'ignore'});
        try {
            fs.unlinkSync(path.join(profileDir, 'SingletonLock'));
            fs.symlinkSync(`${os.hostname()}-${joined.pid}`, path.join(profileDir, 'SingletonLock'));
            assert.match(launch(['--pr', '153']).stderr, /A development Fly from another build is running/);
        } finally { joined.kill(); }
        assert.equal(fs.readdirSync(channel).length, 2, 'the running build is kept beside the new one');
        fs.unlinkSync(path.join(profileDir, 'SingletonLock'));
    } finally { holder.kill(); stranger.kill(); }
    await new Promise(resolve => holder.once('exit', resolve));
    assert.match(launch(['--pr', '153']).stdout, /^pr 153 third\n/);
    assert.equal(fs.readdirSync(channel).length, 1, 'once closed, the older build goes');
    // A failed download falls back to the last download of the channel.
    publish('test-pr-153', 'pr 153 fourth');
    fs.rmSync(path.join(github, 'Arzuparreta/fly-browser/releases/download/test-pr-153/Fly-9.9.9-native-linux-x64.tar.gz'));
    const failed = launch(['--pr', '153']);
    assert.match(failed.stdout, /^pr 153 third\n/);
    assert.match(failed.stderr, /The download failed; starting the last downloaded copy/);
    // A closed pull request's test build is gone from GitHub and from the cache.
    fs.rmSync(path.join(github, 'Arzuparreta/fly-browser/releases/download/test-pr-153'), {recursive:true});
    const closed = launch(['--pr', '153']);
    assert.equal(closed.status, 1);
    assert.equal(closed.stdout, '');
    assert.match(closed.stderr, /There is no test build of pull request #153/);
    assert.equal(fs.existsSync(channel), false);
    console.log('Running builds are kept, other builds are refused while one holds the profile, failed downloads fall back and closed pull requests are cleaned up.');

    // Newer packages start app/fly-browser through app/fly. A Fly already
    // running from the same build does not block the start: Chromium hands it
    // the new window.
    publish('test-pr-160', 'pr 160 renamed', {renamed:true});
    const renamed = launch(['--pr', '160', '--new-window']);
    assert.equal(renamed.status, 23, renamed.stderr);
    assert.equal(renamed.stdout, `pr 160 renamed\n${flags}--new-window\n`);
    const [build160] = fs.readdirSync(path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-160'));
    const sameBuild = path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-160', build160, 'fly-browser/app/sh');
    fs.copyFileSync('/bin/sh', sameBuild);
    fs.chmodSync(sameBuild, 0o755);
    const sameHolder = spawn(sameBuild, ['-c', 'sleep 30; :', 'sh', `--user-data-dir=${profileDir}`], {stdio:'ignore'});
    try {
        fs.symlinkSync(`${os.hostname()}-${sameHolder.pid}`, path.join(profileDir, 'SingletonLock'));
        const joining = launch(['--pr', '160']);
        assert.equal(joining.status, 23, joining.stderr);
        assert.doesNotMatch(joining.stderr, /another build is running/);
    } finally {
        fs.unlinkSync(path.join(profileDir, 'SingletonLock'));
        sameHolder.kill();
    }
    console.log('Newer packages start app/fly-browser through app/fly, also beside a Fly of the same build.');

    // Missing builds, bad checksums and bad numbers start nothing.
    const missing = launch(['--pr', '7']);
    assert.equal(missing.status, 1);
    assert.equal(missing.stdout, '');
    assert.match(missing.stderr, /There is no test build of pull request #7\. The maintainer publishes test builds/);
    const unreachable = launch(['--pr', '7'], {FLY_TEST_OFFLINE:'1'});
    assert.equal(unreachable.status, 1);
    assert.match(unreachable.stderr, /Could not reach GitHub to download the test build of pull request #7/);
    const failing = launch(['--pr', '7'], {FLY_TEST_HTTP:'503'});
    assert.equal(failing.status, 1);
    assert.match(failing.stderr, /GitHub did not provide the test build of pull request #7 \(HTTP 503\)/);
    publish('test-pr-8', 'tampered', {corrupt:true});
    const tampered = launch(['--pr', '8']);
    assert.equal(tampered.status, 1);
    assert.equal(tampered.stdout, '');
    assert.match(tampered.stderr, /does not match its SHA-256 checksum/);
    assert.deepEqual(fs.readdirSync(path.join(env.XDG_CACHE_HOME, 'fly-browser-launch/pr-8')), []);
    assert.match(launch(['--pr', 'x']).stderr, /must be a number/);
    const help = spawnSync('bash', [path.relative(temp, path.join(checkout, 'launch.sh')), '--help'], {cwd:temp, env, encoding:'utf8'});
    assert.equal(help.status, 0, help.stderr);
    assert.match(help.stdout, /--pr N/);
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

    // A cache reached through a symbolic link still protects running builds.
    fs.rmSync(path.join(temp, 'projects/fly-chromium'), {recursive:true});
    const realCache = path.join(temp, 'disk/cache');
    fs.mkdirSync(realCache, {recursive:true});
    const linkedCache = path.join(temp, 'linked-cache');
    fs.symlinkSync(realCache, linkedCache);
    const linked = {XDG_CACHE_HOME:linkedCache};
    publish('test-pr-20', 'pr 20 first');
    assert.equal(launch(['--pr', '20'], linked).status, 23);
    const linkedChannel = path.join(realCache, 'fly-browser-launch/pr-20');
    const [linkedBuild] = fs.readdirSync(linkedChannel);
    const linkedShell = path.join(linkedChannel, linkedBuild, 'fly-browser/app/sh');
    fs.copyFileSync('/bin/sh', linkedShell);
    fs.chmodSync(linkedShell, 0o755);
    const linkedHolder = spawn(linkedShell, ['-c', 'sleep 30; :'], {stdio:'ignore'});
    try {
        publish('test-pr-20', 'pr 20 second');
        assert.match(launch(['--pr', '20'], linked).stdout, /^pr 20 second\n/);
        assert.ok(fs.existsSync(path.join(linkedChannel, linkedBuild)), 'the running build survives behind a symlink');
    } finally { linkedHolder.kill(); }
    console.log('Running builds are recognised through a symbolically linked cache.');
} finally { fs.rmSync(temp, {recursive:true, force:true}); }
})().catch(error => { console.error(error); process.exit(1); });
