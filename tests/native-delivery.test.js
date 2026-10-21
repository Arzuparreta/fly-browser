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
const run = (file, args, env) => spawnSync('bash', [path.join(root, file), ...args], {env, encoding: 'utf8'});
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
    assert.equal(launch.stdout, `--fly-native\n--class=fly-browser-native\n--user-data-dir=${env.XDG_CONFIG_HOME}/fly-browser-native\nhttps://example.org/a b\n`);
    fs.unlinkSync(executable);
    assert.match(run('launch.sh', [], env).stderr, /Native Fly is not built/);

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
    const bundle = path.join(temp, 'bundle/fly-browser-native');
    fs.mkdirSync(bundle, {recursive:true});
    fs.writeFileSync(path.join(bundle, 'install.sh'), '#!/bin/sh\nset -eu\nmkdir -p "$XDG_DATA_HOME/fly-browser-native"\necho native > "$XDG_DATA_HOME/fly-browser-native/VERSION"\n', {mode:0o755});
    const name = `Fly-${version}-native-linux-x64.tar.gz`;
    const archive = path.join(releases, name);
    assert.equal(spawnSync('tar', ['-czf', archive, '-C', path.dirname(bundle), 'fly-browser-native']).status, 0);
    const digest = crypto.createHash('sha256').update(fs.readFileSync(archive)).digest('hex');
    const sums = path.join(releases, 'SHA256SUMS.txt');
    fs.writeFileSync(sums, `${'0'.repeat(64)}  ${name}\n`);
    assert.match(run('scripts/fly-install', ['install', '--native'], env).stderr, /checksum mismatch/);
    assert.equal(fs.existsSync(path.join(env.XDG_DATA_HOME, 'fly-browser-native')), false);
    fs.writeFileSync(sums, `${digest}  ${name}\n`);
    assert.equal(run('scripts/fly-install', ['install', '--native'], env).status, 0);
    assert.match(run('scripts/fly-install', ['status'], env).stdout, /native/);
    fs.mkdirSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-native'), {recursive:true});
    fs.writeFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-native/kept'), 'profile');
    fs.mkdirSync(path.join(home, '.local/bin'), {recursive:true});
    fs.writeFileSync(path.join(home, '.local/bin/fly-browser'), 'unrelated launcher');
    assert.equal(run('scripts/fly-install', ['uninstall', '--yes'], env).status, 0);
    assert.equal(fs.readFileSync(path.join(home, '.local/bin/fly-browser'), 'utf8'), 'unrelated launcher');
    assert.equal(fs.existsSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-native/kept')), true);
    // Arch uninstall must remove the system package, including the legacy
    // `uninstall all --yes` spelling, without touching profile data.
    const calls = path.join(temp, 'package-calls');
    fs.writeFileSync(path.join(bin, 'pacman'), '#!/bin/sh\nif [ "$1" = -Q ]; then exit 0; fi\nprintf "%s\n" "$*" >> "$FLY_TEST_CALLS"\n', {mode:0o755});
    fs.writeFileSync(path.join(bin, 'sudo'), '#!/bin/sh\nexec "$@"\n', {mode:0o755});
    const packageEnv = {...env, FLY_TEST_CALLS:calls};
    assert.equal(run('scripts/fly-install', ['uninstall', 'all', '--yes'], packageEnv).status, 0);
    assert.equal(fs.readFileSync(calls, 'utf8'), '-R --noconfirm fly-browser-bin\n');
    assert.equal(fs.readFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-native/kept'), 'utf8'), 'profile');
    fs.unlinkSync(calls);
    assert.equal(run('scripts/fly-install', ['uninstall', 'native', '--yes'], packageEnv).status, 0);
    assert.equal(fs.existsSync(calls), false);
    assert.match(run('scripts/fly-install', ['uninstall', '--invalid'], packageEnv).stderr, /Unknown uninstall option/);
    fs.writeFileSync(path.join(bin, 'pacman'), '#!/bin/sh\nexit 1\n', {mode:0o755});
    assert.match(run('scripts/fly-install', ['status'], env).stdout, /Retained configuration:/);
    // Purging also works when only configuration remains after uninstall.
    const electron = path.join(env.XDG_CONFIG_HOME, 'fly-browser');
    fs.mkdirSync(electron, {recursive:true});
    fs.writeFileSync(path.join(electron, 'settings'), 'legacy settings');
    fs.writeFileSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-flags.conf'), '--ozone-platform=wayland');
    const unrelated = path.join(env.XDG_CONFIG_HOME, 'other-app');
    fs.mkdirSync(unrelated);
    fs.writeFileSync(path.join(unrelated, 'kept'), 'unrelated');
    assert.equal(run('scripts/fly-install', ['uninstall', '--purge', '--yes'], env).status, 0);
    assert.equal(fs.existsSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-native')), false);
    assert.equal(fs.existsSync(electron), false);
    assert.equal(fs.existsSync(path.join(env.XDG_CONFIG_HOME, 'fly-browser-flags.conf')), false);
    assert.equal(fs.readFileSync(path.join(unrelated, 'kept'), 'utf8'), 'unrelated');
    assert.equal(run('scripts/fly-install', ['uninstall', '--purge', '--yes'], env).status, 0);
    fs.unlinkSync(archive);
    assert.match(run('scripts/fly-install', ['install', '--native'], env).stderr, /no native Chromium package/);
    console.log('Native launcher and installer preserve arguments, verify releases, reject legacy-only releases and retain profile/unrelated launchers.');
} finally { fs.rmSync(temp, {recursive:true, force:true}); }
