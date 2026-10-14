// The AUR package is pushed by a workflow nobody watches, so a mistake here
// reaches Arch users as a package that fails to build or installs no launcher
// entry. Test what it promises before it leaves.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const aur = require('../scripts/aur');

const throws = (fn, pattern) => assert.throws(fn, error => error.releaseTooling && pattern.test(error.message));
const sha = 'a'.repeat(64);
const files = aur.render('1.2.3', sha);

assert.match(files.PKGBUILD, /^pkgver=1\.2\.3$/m);
assert.match(files.PKGBUILD, /^pkgrel=1$/m);
assert.match(files.PKGBUILD, new RegExp(`^sha256sums=\\('${sha}'\\)$`, 'm'));
assert.match(files.PKGBUILD, /releases\/download\/v\$\{pkgver\}\/Fly-\$\{pkgver\}-x64\.tar\.gz/);
assert.match(files['.SRCINFO'], /^\tsource = Fly-1\.2\.3-x64\.tar\.gz::https:\/\/github\.com\/Arzuparreta\/fly-browser\/releases\/download\/v1\.2\.3\/Fly-1\.2\.3-x64\.tar\.gz$/m);
assert.match(aur.render('1.2.3', sha, 2)['.SRCINFO'], /^\tpkgrel = 2$/m);

throws(() => aur.render('1.2.3-rc.1', sha), /pre-release/);
throws(() => aur.render('1.2.3', 'abc'), /not a SHA-256/);
throws(() => aur.render('1.2.3', sha, 0), /pkgrel/);
throws(() => aur.parseOptions(['--sums', 'x']), /Usage/);
throws(() => aur.parseOptions(['--sums']), /needs a value/);

// The checksum is the tarball's, not a neighbour's that shares a prefix.
const sums = `${'1'.repeat(64)}  Fly-1.2.3-x64.tar.gz.sig\n${'2'.repeat(64)}  Fly-1.2.3-x64.tar.gz\n${'3'.repeat(64)}  Fly-1.2.3-x86_64.AppImage\n`;
assert.equal(aur.checksumFrom(sums, aur.tarballName('1.2.3')), '2'.repeat(64));
throws(() => aur.checksumFrom(sums, 'Fly-9.9.9-x64.tar.gz'), /no entry/);

// The package installs the desktop entry and icons from the tarball's
// resources, so the build must keep putting them there.
const root = path.join(__dirname, '..');
const linux = JSON.parse(fs.readFileSync(path.join(root, 'package.json'), 'utf8')).build.linux;
const resources = Object.fromEntries(linux.extraResources.map(entry => [entry.to, entry.from]));
assert.equal(resources['fly-browser.desktop'], 'fly-browser.desktop');
assert.equal(resources.icons, 'build/icons');
for (const size of aur.ICON_SIZES) {
    assert.ok(fs.existsSync(path.join(root, 'build/icons', `${size}x${size}.png`)), `build/icons/${size}x${size}.png`);
}
const desktop = fs.readFileSync(path.join(root, 'fly-browser.desktop'), 'utf8');
assert.match(desktop, /^Exec=fly-browser %u$/m, 'The entry starts the /usr/bin launcher the package installs');
assert.match(desktop, /^Icon=fly-browser$/m);

// Where makepkg exists, the hand-written .SRCINFO must be the one it derives.
if (spawnSync('makepkg', ['--version']).status === 0) {
    const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-aur-'));
    try {
        fs.writeFileSync(path.join(temporary, 'PKGBUILD'), files.PKGBUILD);
        const result = spawnSync('makepkg', ['--printsrcinfo'], {cwd: temporary, encoding: 'utf8'});
        assert.equal(result.status, 0, result.stderr);
        assert.equal(files['.SRCINFO'], result.stdout);
    } finally { fs.rmSync(temporary, {recursive: true, force: true}); }
}

console.log('The AUR package names the released tarball, its checksum and the launcher files the tarball ships.');
