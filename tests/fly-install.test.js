// scripts/fly-install against a fake GitHub, pacman and makepkg in a throwaway
// home: it finds every installation, installs and updates both kinds, adds
// and removes the menu entry exactly as Fly writes it, and refuses a download
// whose checksum does not match.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { createDesktopIntegration } = require('../desktopIntegration');

const root = path.join(__dirname, '..');
const script = path.join(root, 'scripts/fly-install');
const template = fs.readFileSync(path.join(root, 'fly-browser.desktop'), 'utf8');

// The fallback entry inside the script is the repository's.
const embedded = /^DESKTOP_TEMPLATE='([^']*)'$/m.exec(fs.readFileSync(script, 'utf8'));
assert.ok(embedded, 'DESKTOP_TEMPLATE is a single-quoted block');
assert.equal(embedded[1] + '\n', template, 'Update DESKTOP_TEMPLATE in scripts/fly-install to match fly-browser.desktop');

const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-install-'));
try {
    const home = path.join(temporary, 'my home');
    const bin = path.join(temporary, 'bin');
    const release = path.join(temporary, 'release');
    const fake = path.join(temporary, 'fake');
    for (const dir of [home, bin, release, fake]) fs.mkdirSync(dir, { recursive: true });
    const log = path.join(fake, 'log');
    const tool = (name, body) => fs.writeFileSync(path.join(bin, name), `#!/bin/sh\n${body}\n`, { mode: 0o755 });

    tool('df', `printf 'Filesystem 1024-blocks Used Available Capacity Mounted on\\nmock 9999999 0 %s 0%% /\\n' \"$(cat '${fake}/space' 2>/dev/null || echo 9999999)\"`);

    // GitHub: URLs map to files in the fake release directory.
    tool('curl', `out=; url=
while [ $# -gt 0 ]; do case $1 in -o) out=$2; shift;; -*) ;; *) url=$1;; esac; shift; done
echo "curl $url" >> "${log}"
file="${release}/$(basename "$url")"
[ -f "$file" ] || exit 22
if [ -n "$out" ]; then cp "$file" "$out"; else cat "$file"; fi`);
    // pacman and makepkg keep the installed package version in a state file.
    tool('pacman', `echo "pacman $*" >> "${log}"
case $1 in
  -Q) [ -f "${fake}/installed" ] && echo "$2 $(cat "${fake}/installed")-1" || exit 1 ;;
  -R) rm -f "${fake}/installed" ;;
esac`);
    tool('makepkg', `echo "makepkg $* in $PWD" >> "${log}"
[ "$BUILDDIR" = "$PWD" ] && [ "$TMPDIR" = "$PWD" ] || exit 90
[ ! -f "${fake}/fail-build" ] || exit 1
sed -n 's/^pkgver=//p' PKGBUILD > "${fake}/installed"`);
    tool('sudo', `echo "sudo $*" >> "${log}"; exec "$@"`);
    tool('vercmp', `[ "$1" = "$2" ] && echo 0 && exit; [ "$(printf '%s\\n%s\\n' "$1" "$2" | sort -V | head -n1)" = "$1" ] && echo -1 || echo 1`);
    tool('xdg-desktop-menu', `echo "xdg-desktop-menu $*" >> "${log}"`);

    const publish = version => {
        const name = `Fly-${version}-x86_64.AppImage`;
        fs.writeFileSync(path.join(release, 'latest'), JSON.stringify({ tag_name: `v${version}` }, null, 2));
        fs.writeFileSync(path.join(release, name), `appimage ${version}`);
        fs.writeFileSync(path.join(release, 'PKGBUILD'), `pkgname=fly-browser-bin\npkgver=${version}\n`);
        const sha = crypto.createHash('sha256').update(`appimage ${version}`).digest('hex');
        fs.writeFileSync(path.join(release, 'SHA256SUMS.txt'), `${'0'.repeat(64)}  Fly-${version}-x64.tar.gz\n${sha}  ${name}\n`);
    };
    const run = (...args) => {
        fs.writeFileSync(log, '');
        const result = spawnSync('bash', [script, ...args], {
            encoding: 'utf8',
            env: { PATH: `${bin}:/usr/bin:/bin`, HOME: home, XDG_DATA_DIRS: path.join(temporary, 'system') },
            input: ''
        });
        return { ...result, calls: fs.readFileSync(log, 'utf8') };
    };
    const ok = (...args) => {
        const result = run(...args);
        assert.equal(result.status, 0, `fly-install ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
        return result;
    };
    const data = path.join(home, '.local/share');
    const entry = path.join(data, 'applications/fly-browser.desktop');
    const link = path.join(data, 'fly-browser/fly-browser.AppImage');

    publish('0.18.0');

    // Nothing installed.
    assert.match(ok('status').stdout, /Latest release: 0\.18\.0[\s\S]*nothing found[\s\S]*fly-install install/);
    assert.match(ok('update').stdout, /Fly is not installed/);

    // AppImage: downloaded, verified, executable, and in the menu exactly as Fly writes it.
    let result = ok('install', '--appimage');
    const installed = path.join(home, 'Applications/Fly-0.18.0-x86_64.AppImage');
    assert.equal(fs.readFileSync(installed, 'utf8'), 'appimage 0.18.0');
    assert.equal(fs.statSync(installed).mode & 0o777, 0o755);
    assert.equal(fs.readlinkSync(link), installed);
    const flyWrites = createDesktopIntegration({ env: { HOME: home }, platform: 'linux', appImage: installed, template }).entryContent();
    assert.equal(fs.readFileSync(entry, 'utf8'), flyWrites, 'The entry is the one Fly itself writes, even with a space in the path');
    assert.match(fs.readFileSync(entry, 'utf8'), /^Exec=".*my home.*fly-browser\.AppImage" %u$/m);
    assert.match(result.calls, /xdg-desktop-menu forceupdate --mode user/);
    assert.match(ok('status').stdout, /AppImage {2}0\.18\.0 {2}.*Fly-0\.18\.0-x86_64\.AppImage {2}up to date {2}\[applications menu\][\s\S]*added by Fly/);
    assert.match(ok('install', '--appimage').stdout, /already here/);

    // A second, older AppImage in Downloads: found, flagged, and updated in place.
    const downloads = path.join(home, 'Downloads');
    fs.mkdirSync(downloads);
    fs.writeFileSync(path.join(downloads, 'Fly-0.16.1-x86_64.AppImage'), 'old', { mode: 0o755 });
    result = ok('status');
    assert.match(result.stdout, /0\.16\.1 .*update available \(0\.18\.0\)/);
    assert.match(result.stdout, /2 installations/);
    result = ok('update');
    assert.equal(fs.existsSync(path.join(downloads, 'Fly-0.16.1-x86_64.AppImage')), false);
    assert.equal(fs.readFileSync(path.join(downloads, 'Fly-0.18.0-x86_64.AppImage'), 'utf8'), 'appimage 0.18.0');
    assert.match(result.stdout, /Applications\/Fly-0\.18\.0-x86_64\.AppImage is up to date/);

    // A newer release: the menu's AppImage is replaced and the menu follows it.
    publish('0.19.0');
    ok('update');
    const newer = path.join(home, 'Applications/Fly-0.19.0-x86_64.AppImage');
    assert.equal(fs.existsSync(installed), false);
    assert.equal(fs.readlinkSync(link), newer);
    assert.equal(fs.readFileSync(entry, 'utf8'), flyWrites, 'The entry starts the link, so it does not change');
    assert.deepEqual(fs.readdirSync(downloads), ['Fly-0.19.0-x86_64.AppImage'], 'Every installation is updated');
    fs.rmSync(path.join(downloads, 'Fly-0.19.0-x86_64.AppImage'));

    // A tampered download changes nothing.
    fs.writeFileSync(path.join(release, 'Fly-0.19.0-x86_64.AppImage'), 'tampered');
    fs.writeFileSync(path.join(release, 'latest'), JSON.stringify({ tag_name: 'v0.19.0' }));
    fs.writeFileSync(path.join(downloads, 'Fly-0.18.0-x86_64.AppImage'), 'appimage 0.18.0');
    result = run('update');
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /does not match the release's SHA256SUMS\.txt/);
    assert.equal(fs.readFileSync(path.join(downloads, 'Fly-0.18.0-x86_64.AppImage'), 'utf8'), 'appimage 0.18.0');
    assert.deepEqual(fs.readdirSync(downloads), ['Fly-0.18.0-x86_64.AppImage'], 'No partial download is left');
    publish('0.19.0');

    // Uninstalling a file that is not the menu's leaves the menu alone.
    ok('uninstall', path.join(downloads, 'Fly-0.18.0-x86_64.AppImage'), '--yes');
    assert.equal(fs.existsSync(entry), true);
    // Uninstalling the menu's AppImage removes its entry, link and icons too.
    fs.mkdirSync(path.join(data, 'icons/hicolor/48x48/apps'), { recursive: true });
    fs.writeFileSync(path.join(data, 'icons/hicolor/48x48/apps/fly-browser.png'), 'icon');
    result = ok('uninstall', newer, '--yes');
    assert.match(result.stdout, /Removed the applications menu entry/);
    for (const file of [newer, entry, link, path.dirname(link), path.join(data, 'icons/hicolor/48x48/apps/fly-browser.png')]) assert.equal(fs.existsSync(file), false, file);
    assert.match(result.stdout, /profile in ~\/\.config\/fly-browser is kept/);

    // An entry Fly did not write is never replaced or removed.
    fs.mkdirSync(path.dirname(entry), { recursive: true });
    fs.writeFileSync(entry, template);
    result = ok('install', '--appimage');
    assert.match(result.stderr, /was not written by Fly/);
    assert.equal(fs.readFileSync(entry, 'utf8'), template);
    ok('uninstall', 'all', '--yes');
    assert.equal(fs.readFileSync(entry, 'utf8'), template);
    fs.rmSync(entry);

    // Package (the default where makepkg exists): built in a temporary directory that is removed.
    publish('0.18.0');
    fs.writeFileSync(path.join(fake, 'space'), '100');
    result = run('install');
    assert.notEqual(result.status, 0);
    assert.match(result.stderr, /not enough space.*at least 2 GiB/);
    assert.doesNotMatch(result.calls, /makepkg|PKGBUILD/);
    assert.deepEqual(fs.readdirSync(path.join(home, '.cache/fly-install')), []);
    fs.unlinkSync(path.join(fake, 'space'));
    fs.writeFileSync(path.join(fake, 'fail-build'), '');
    result = run('install');
    assert.notEqual(result.status, 0);
    assert.deepEqual(fs.readdirSync(path.join(home, '.cache/fly-install')), []);
    fs.unlinkSync(path.join(fake, 'fail-build'));
    result = ok('install');
    const build = /makepkg -si in (.+)/.exec(result.calls);
    assert.ok(build, result.calls);
    assert.ok(build[1].startsWith(path.join(home, '.cache/fly-install/build.')), build[1]);
    assert.equal(fs.existsSync(build[1]), false, 'The build directory is removed');
    assert.match(result.calls, /curl https:\/\/github\.com\/Arzuparreta\/fly-browser\/releases\/download\/v0\.18\.0\/PKGBUILD/);
    assert.match(ok('status').stdout, /package {3}fly-browser-bin 0\.18\.0 .*up to date/);
    assert.match(ok('install').stdout, /already installed/);
    result = ok('update');
    assert.doesNotMatch(result.calls, /makepkg/, 'An up-to-date package is not rebuilt');
    publish('0.19.0');
    result = ok('update');
    assert.match(result.calls, /makepkg -si/);
    assert.equal(fs.readFileSync(path.join(fake, 'installed'), 'utf8').trim(), '0.19.0');
    result = ok('uninstall', 'package');
    assert.match(result.calls, /sudo pacman -R fly-browser-bin/);
    assert.equal(fs.existsSync(path.join(fake, 'installed')), false);

    // Uninstalling needs a target, and nonsense is refused.
    assert.match(run('uninstall').stderr, /say what to uninstall/);
    assert.match(run('uninstall', path.join(home, 'nothing.AppImage')).stderr, /no such file/);
    assert.equal(run('frobnicate').status, 2);
} finally { fs.rmSync(temporary, { recursive: true, force: true }); }

console.log('fly-install finds, installs, updates and uninstalls Fly, menu entry included, and refuses tampered downloads.');
