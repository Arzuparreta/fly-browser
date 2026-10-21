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
assert.match(files.PKGBUILD, /releases\/download\/v\$\{pkgver\}\/Fly-\$\{pkgver\}-native-linux-x64\.tar\.gz/);
assert.match(files['.SRCINFO'], /^\tsource = Fly-1\.2\.3-native-linux-x64\.tar\.gz::https:\/\/github\.com\/Arzuparreta\/fly-browser\/releases\/download\/v1\.2\.3\/Fly-1\.2\.3-native-linux-x64\.tar\.gz$/m);
assert.match(aur.render('1.2.3', sha, 2)['.SRCINFO'], /^\tpkgrel = 2$/m);

throws(() => aur.render('1.2.3-rc.1', sha), /pre-release/);
throws(() => aur.render('1.2.3', 'abc'), /not a SHA-256/);
throws(() => aur.render('1.2.3', sha, 0), /pkgrel/);
throws(() => aur.parseOptions(['--sums', 'x']), /Usage/);
throws(() => aur.parseOptions(['--sums']), /needs a value/);

// The checksum is the tarball's, not a neighbour's that shares a prefix.
const sums = `${'1'.repeat(64)}  Fly-1.2.3-native-linux-x64.tar.gz.sig\n${'2'.repeat(64)}  Fly-1.2.3-native-linux-x64.tar.gz\n${'3'.repeat(64)}  Fly-1.2.3-x86_64.AppImage\n`;
assert.equal(aur.checksumFrom(sums, aur.tarballName('1.2.3')), '2'.repeat(64));
throws(() => aur.checksumFrom(sums, 'Fly-9.9.9-native-linux-x64.tar.gz'), /no entry/);

// Repackage the native archive layout into a disposable Arch package root.
const root = path.join(__dirname, '..');
const staging = fs.mkdtempSync(path.join(os.tmpdir(), 'fly-native-aur-'));
try {
    const app = path.join(staging, 'fly-browser-native/app');
    const pkgdir = path.join(staging, 'pkg');
    fs.mkdirSync(path.join(app, 'icons'), {recursive:true});
    fs.writeFileSync(path.join(app, 'fly-browser-native'), '#!/bin/sh\n');
    fs.writeFileSync(path.join(app, 'chrome-sandbox'), 'sandbox');
    fs.writeFileSync(path.join(app, 'fly-browser-native.desktop'), '[Desktop Entry]\nName=Fly\nExec=@EXEC@ %U\nIcon=fly-browser-native\n');
    for (const size of aur.ICON_SIZES) fs.writeFileSync(path.join(app, 'icons', `fly-logo${size}.png`), 'icon');
    const pkgbuild = path.join(staging, 'PKGBUILD');
    fs.writeFileSync(pkgbuild, files.PKGBUILD);
    const packaged = spawnSync('bash', ['-e', '-c', 'source "$1"; package', 'package-test', pkgbuild], {
        cwd:staging, env:{...process.env, pkgdir}, encoding:'utf8'
    });
    assert.equal(packaged.status, 0, packaged.stderr);
    assert.match(fs.readFileSync(path.join(pkgdir, 'usr/bin/fly-browser'), 'utf8'), /exec \/opt\/fly-browser\/fly-browser-native/);
    assert.match(fs.readFileSync(path.join(pkgdir, 'usr/share/applications/fly-browser-native.desktop'), 'utf8'), /Exec=\/usr\/bin\/fly-browser %U/);
    assert.ok(fs.existsSync(path.join(pkgdir, 'usr/share/icons/hicolor/128x128/apps/fly-browser-native.png')));
    assert.equal(fs.statSync(path.join(pkgdir, 'opt/fly-browser/chrome-sandbox')).mode & 0o7777, 0o4755);
} finally { fs.rmSync(staging, {recursive:true, force:true}); }

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
