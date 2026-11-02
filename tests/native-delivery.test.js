// Exercise the native entry points with a disposable HOME and fake release service.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const root = path.resolve(__dirname, '..');
const version = require('../package.json').version;
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-native-delivery-'));
const signing = require('./release-signing')(temp);
const installer = path.join(temp, 'fly-install');
signing.installer(path.join(root, 'scripts/fly-install'), installer);
const run = (file, args, env) => spawnSync('bash', [file === 'scripts/fly-install' ? installer : path.join(root, file), ...args], {env, encoding: 'utf8'});
try {
    const home = path.join(temp, 'home with spaces');
    const bin = path.join(temp, 'bin');
    const releases = path.join(temp, 'releases');
    const engine = path.join(temp, 'engine');
    for (const dir of [home, bin, releases, path.join(engine, 'src/out/FlyRelease')]) fs.mkdirSync(dir, {recursive:true});
    const env = {...process.env, HOME:home, XDG_CACHE_HOME:path.join(home, 'cache'), XDG_CONFIG_HOME:path.join(home, 'config'), XDG_DATA_HOME:path.join(home, 'data'), PATH:`${bin}:${process.env.PATH}`, FLY_ENGINE_ROOT:engine, DISPLAY:':0'};
    const executable = path.join(engine, 'src/out/FlyRelease/chrome');
    fs.writeFileSync(executable, '#!/bin/sh\nprintf "%s\\n" "$@"\nexit 17\n', {mode:0o755});
    const launch = run('launch.sh', ['https://example.org/a b'], env);
    assert.equal(launch.status, 17, launch.stderr);
    assert.equal(launch.stdout, `--fly-native\n--class=fly-browser-dev\n--user-data-dir=${env.XDG_CONFIG_HOME}/fly-browser-dev\nhttps://example.org/a b\n`);
    // Prefer the development build; preserve explicit output and caller arguments.
    const native = path.join(engine, 'src/out/FlyNative/chrome');
    fs.mkdirSync(path.dirname(native), {recursive:true});
    fs.writeFileSync(native, '#!/bin/sh\nprintf "native:%s\\n" "$@"\nexit 19\n', {mode:0o755});
    assert.equal(run('launch.sh', [], env).status, 19);
    assert.equal(run('launch.sh', [], {...env, FLY_ENGINE_OUTPUT:'out/FlyRelease'}).status, 17);
    const custom = run('launch.sh', ['--user-data-dir=/custom profile', '--class=custom'], env);
    assert.match(custom.stdout, /native:--user-data-dir=\/custom profile\nnative:--class=custom\n$/);
    // Model this machine's external engine layout without depending on its files.
    const checkout = path.join(temp, 'storage/projects/fly-browser');
    const nearbyEngine = path.join(temp, 'storage/fly-chromium');
    fs.mkdirSync(checkout, {recursive:true});
    fs.copyFileSync(path.join(root, 'launch.sh'), path.join(checkout, 'launch.sh'));
    fs.renameSync(engine, nearbyEngine);
    const automatic = {...env}; delete automatic.FLY_ENGINE_ROOT; delete automatic.FLY_ENGINE_OUTPUT;
    const auto = spawnSync('bash', [path.join(checkout, 'launch.sh')], {env:automatic, encoding:'utf8'});
    assert.equal(auto.status, 19, auto.stderr);
    const headless = {...automatic}; delete headless.DISPLAY; delete headless.WAYLAND_DISPLAY;
    assert.match(spawnSync('bash', [path.join(checkout, 'launch.sh')], {env:headless, encoding:'utf8'}).stderr, /needs a graphical session/);
    const cacheEngine = path.join(home, '.cache/fly-chromium');
    fs.mkdirSync(path.dirname(cacheEngine), {recursive:true});
    fs.renameSync(nearbyEngine, cacheEngine);
    assert.equal(spawnSync('bash', [path.join(checkout, 'launch.sh')], {env:automatic, encoding:'utf8'}).status, 19);
    fs.renameSync(cacheEngine, engine);
    fs.unlinkSync(native);
    fs.unlinkSync(executable);
    // Without a build the launcher falls back to the published release; here
    // the network is unavailable and nothing was downloaded before.
    fs.writeFileSync(path.join(bin, 'curl'), '#!/bin/sh\nexit 6\n', {mode:0o755});
    const offline = run('launch.sh', [], env);
    assert.equal(offline.status, 1);
    assert.match(offline.stderr, /Native Fly is not built on this computer/);
    assert.match(offline.stderr, /Could not reach GitHub to download the published release/);

    fs.writeFileSync(path.join(bin, 'curl'), `#!/bin/bash
set -eu
out=
while [ $# -gt 0 ]; do
 case $1 in -o) out=$2; shift;; -*) ;; *) url=$1;; esac
 shift
done
cp "${releases}/$(basename "$url")" "$out"
`, {mode:0o755});
    // Keep host package manager state out of this user-install test.
    fs.writeFileSync(path.join(bin, 'pacman'), '#!/bin/sh\nexit 1\n', {mode:0o755});
    fs.writeFileSync(path.join(releases, 'latest'), JSON.stringify({tag_name:`v${version}`}));
    const bundle = path.join(temp, 'bundle/fly-browser');
    fs.mkdirSync(bundle, {recursive:true});
    fs.writeFileSync(path.join(bundle, 'install.sh'), '#!/bin/sh\nset -eu\nmkdir -p "$XDG_DATA_HOME/fly-browser"\necho native > "$XDG_DATA_HOME/fly-browser/VERSION"\n', {mode:0o755});
    const name = `Fly-${version}-native-linux-x64.tar.gz`;
    const archive = path.join(releases, name);
    assert.equal(spawnSync('tar', ['-czf', archive, '-C', path.dirname(bundle), 'fly-browser']).status, 0);
    const digest = crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex');
    const sums = path.join(releases, 'SHA256SUMS.txt');
    fs.writeFileSync(sums, `${'0'.repeat(64)}  ${name}\n`);
    signing.sign(sums);
    assert.match(run('scripts/fly-install', ['install', '--native'], env).stderr, /checksum mismatch/);
    assert.equal(fs.existsSync(path.join(env.XDG_DATA_HOME, 'fly-browser')), false);
    fs.writeFileSync(sums, `${digest}  ${name}\n`);
    assert.match(run('scripts/fly-install', ['install', '--native'], env).stderr, /signature verification failed/);
    signing.sign(sums);
    assert.equal(run('scripts/fly-install', ['install', '--native'], env).status, 0);
    assert.match(run('scripts/fly-install', ['status'], env).stdout, /native/);
    fs.mkdirSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser'), {recursive:true});
    fs.writeFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser/kept'), 'profile');
    fs.mkdirSync(path.join(home, '.local/bin'), {recursive:true});
    fs.writeFileSync(path.join(home, '.local/bin/fly-browser'), 'unrelated launcher');
    assert.equal(run('scripts/fly-install', ['uninstall', '--yes'], env).status, 0);
    assert.equal(fs.readFileSync(path.join(home, '.local/bin/fly-browser'), 'utf8'), 'unrelated launcher');
    assert.equal(fs.existsSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser/kept')), true);
    // Arch uninstall must remove the system package, also when spelled
    // `uninstall all --yes`, without touching profile data.
    const calls = path.join(temp, 'package-calls');
    fs.writeFileSync(path.join(bin, 'pacman'), '#!/bin/sh\nif [ "$1" = -Q ]; then exit 0; fi\nprintf "%s\n" "$*" >> "$FLY_TEST_CALLS"\n', {mode:0o755});
    fs.writeFileSync(path.join(bin, 'sudo'), '#!/bin/sh\nexec "$@"\n', {mode:0o755});
    const packageEnv = {...env, FLY_TEST_CALLS:calls};
    assert.equal(run('scripts/fly-install', ['uninstall', 'all', '--yes'], packageEnv).status, 0);
    assert.equal(fs.readFileSync(calls, 'utf8'), '-R --noconfirm fly-browser-bin\n');
    assert.equal(fs.readFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser/kept'), 'utf8'), 'profile');
    fs.unlinkSync(calls);
    assert.equal(run('scripts/fly-install', ['uninstall', 'native', '--yes'], packageEnv).status, 0);
    assert.equal(fs.existsSync(calls), false);
    assert.match(run('scripts/fly-install', ['uninstall', '--invalid'], packageEnv).stderr, /Unknown uninstall option/);
    fs.writeFileSync(path.join(bin, 'pacman'), '#!/bin/sh\nexit 1\n', {mode:0o755});
    assert.match(run('scripts/fly-install', ['status'], env).stdout, /Retained configuration:/);
    // Purging also works when only configuration remains after uninstall.
    const profile = path.join(env.XDG_CONFIG_HOME, 'fly-browser');
    fs.mkdirSync(profile, {recursive:true});
    fs.writeFileSync(path.join(profile, 'settings'), 'settings');
    fs.writeFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-flags.conf'), '--ozone-platform=wayland');
    const unrelated = path.join(env.XDG_CONFIG_HOME, 'other-app');
    fs.mkdirSync(unrelated);
    fs.writeFileSync(path.join(unrelated, 'kept'), 'unrelated');
    assert.equal(run('scripts/fly-install', ['uninstall', '--purge', '--yes'], env).status, 0);
    assert.equal(fs.existsSync(profile), false);
    assert.equal(fs.existsSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-flags.conf')), false);
    assert.equal(fs.readFileSync(path.join(unrelated, 'kept'), 'utf8'), 'unrelated');
    assert.equal(run('scripts/fly-install', ['uninstall', '--purge', '--yes'], env).status, 0);
    // The native package route authenticates the same manifest before handing
    // the exact downloaded file to the system manager; no PKGBUILD is executed.
    const deb = `fly-browser-bin_${version}_amd64.deb`;
    fs.writeFileSync(path.join(releases, deb), 'disposable package transport fixture');
    const debDigest = crypto.createHash('sha256').update(fs.readFileSync(path.join(releases, deb))).digest('hex');
    fs.writeFileSync(sums, `${digest}  ${name}\n${debDigest}  ${deb}\n`);
    signing.sign(sums);
    fs.writeFileSync(path.join(bin, 'apt-get'), '#!/bin/sh\nprintf "%s\\n" "$@" >> "$FLY_TEST_CALLS"\n', {mode:0o755});
    fs.writeFileSync(path.join(bin, 'dpkg-query'), '#!/bin/sh\nexit 1\n', {mode:0o755});
    assert.equal(run('scripts/fly-install', ['install', '--package'], packageEnv).status, 0);
    assert.match(fs.readFileSync(calls, 'utf8'), new RegExp(`install\\n[^\\n]+/${deb.replaceAll('.', '\\.')}\\n`));
    fs.unlinkSync(calls);
    fs.writeFileSync(path.join(bin, 'dpkg-query'), '#!/bin/sh\nprintf "install ok installed"\n', {mode:0o755});
    assert.equal(run('scripts/fly-install', ['update'], packageEnv).status, 0);
    assert.equal(fs.readFileSync(calls, 'utf8'), 'update\ninstall\n--only-upgrade\nfly-browser-bin\n');
    fs.writeFileSync(path.join(bin, 'dpkg-query'), '#!/bin/sh\nexit 1\n', {mode:0o755});
    fs.unlinkSync(archive);
    assert.match(run('scripts/fly-install', ['install', '--native'], env).stderr, /no Linux package/);
    console.log('Native launcher and installer preserve arguments, verify releases, reject releases without a Linux package and retain profile/unrelated launchers.');
} finally { fs.rmSync(temp, {recursive:true, force:true}); }
