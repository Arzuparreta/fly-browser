#!/usr/bin/env python3
"""Official Linux delivery: one payload, native packages, signed static repositories.

Run in scripts/distribution/Dockerfile. No distro build service or store is used.
Private keys are supplied through GNUPGHOME; they never enter the payload/site.
"""
import argparse
import datetime as dt
import email.utils
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CONFIG = json.loads((ROOT / 'distribution/linux.json').read_text())
VERSION = json.loads((ROOT / 'package.json').read_text())['version']
KEY = ROOT / 'distribution/release-key.gpg'
DESCRIPTION = 'Fly Browser - native Chromium with keyboard-first workspaces'

APT_SOURCE = f'''Types: deb
URIs: {CONFIG['repository']}/
Suites: ./
Architectures: amd64
Signed-By: /etc/apt/keyrings/fly-browser.gpg
'''
RPM_REPO = f'''[fly-browser]
name=Fly Browser
baseurl={CONFIG['site']}/packages/rpm/
enabled=1
autorefresh=1
metadata_expire=1h
gpgcheck=1
repo_gpgcheck=1
includepkgs=fly-browser-bin
gpgkey=file:///usr/share/fly-browser/release-key.asc
'''
PACMAN_CONF = f'''[fly-browser]
SigLevel = Required DatabaseRequired
Server = {CONFIG['repository']}
'''


def run(argv, **kwargs):
    return subprocess.run([str(arg) for arg in argv], check=True, **kwargs)


def write(path, content, mode=0o644):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    path.chmod(mode)


def fingerprint():
    result = run(['gpg', '--batch', '--with-colons', '--show-keys', KEY],
                 capture_output=True, text=True)
    return next(line.split(':')[9] for line in result.stdout.splitlines()
                if line.startswith('fpr:'))


def gpg_args():
    args = ['gpg', '--batch', '--yes', '--local-user', fingerprint()]
    if password := os.environ.get('FLY_SIGNING_PASSPHRASE_FILE'):
        args += ['--pinentry-mode', 'loopback', '--passphrase-file', password]
    return args


def sign(path, suffix='.asc', clearsign=False):
    args = gpg_args()
    if suffix == '.asc' or clearsign:
        args += ['--armor']
    output = path.parent / ('InRelease' if clearsign else path.name + suffix)
    run([*args, '--output', output,
         '--clearsign' if clearsign else '--detach-sign', path])
    return output


def verify(path, signature):
    with tempfile.TemporaryDirectory() as home:
        run(['gpgv', '--homedir', home, '--keyring', KEY, signature, path])


def extract(archive, destination):
    with tarfile.open(archive) as bundle:
        # The release packager writes regular files/directories, never links.
        for entry in bundle.getmembers():
            parts = Path(entry.name).parts
            if (not parts or parts[0] != 'fly-browser' or '..' in parts
                    or Path(entry.name).is_absolute()
                    or not (entry.isfile() or entry.isdir())):
                raise ValueError(f'Unsafe package member: {entry.name}')
        # Debian's baseline Python predates tarfile's filter= argument. The
        # complete member validation above rejects every link and escape.
        bundle.extractall(destination)
    app = destination / 'fly-browser/app'
    provenance = json.loads((app / 'provenance.json').read_text())
    if provenance['product_version'] != VERSION:
        raise ValueError('Archive version does not match package.json')
    if hashlib.sha256((app / 'fly-browser').read_bytes()).hexdigest() != provenance['binary_sha256']:
        raise ValueError('Browser does not match its build provenance')
    for name in ['fly-browser', 'chrome-sandbox', 'fly-browser.desktop']:
        if not (app / name).is_file():
            raise ValueError(f'Incomplete payload: {name}')
    return app


def system_tree(app, stage):
    target = stage / 'opt/fly-browser'
    shutil.copytree(app, target)
    for file in [stage, *stage.rglob('*')]:
        file.chmod(0o755 if file.is_dir() or file.stat().st_mode & 0o111 else 0o644)
    (target / 'chrome-sandbox').chmod(0o4755)
    write(target / 'INSTALLATION', 'system\n')
    shutil.copy2(KEY, target / 'release-key.gpg')
    launcher = '''#!/bin/sh
set -f
flags="${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser-flags.conf"
if [ -r "$flags" ]; then
  set -- $(sed 's/#.*//' "$flags") "$@"
fi
exec /opt/fly-browser/fly-browser "$@"
'''
    write(stage / 'usr/bin/fly-browser', launcher, 0o755)
    desktop = (app / 'fly-browser.desktop').read_text().replace('@EXEC@', '/usr/bin/fly-browser')
    write(stage / 'usr/share/applications/fly-browser.desktop', desktop)
    for size in (48, 128, 256):
        icon = stage / f'usr/share/icons/hicolor/{size}x{size}/apps/fly-browser.png'
        icon.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(app / f'icons/fly-logo{size}.png', icon)
    # Ubuntu restricts unprivileged user namespaces using AppArmor. This only
    # grants Chromium its namespace sandbox; it does not disable that sandbox.
    write(stage / 'usr/share/fly-browser/apparmor', '''abi <abi/4.0>,
include <tunables/global>
/opt/fly-browser/fly-browser flags=(unconfined) {
  userns,
  include if exists <local/fly-browser>
}
''')
    shutil.copy2(ROOT / 'scripts/distribution/integration.sh',
                 stage / 'usr/share/fly-browser/integration.sh')
    shutil.copy2(ROOT / 'distribution/release-key.asc',
                 stage / 'usr/share/fly-browser/release-key.asc')


def packages(archive, output):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-rc\.\d+)?', VERSION):
        raise ValueError('Invalid declared version')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        temp = Path(tmp)
        app = extract(archive, temp / 'source')
        stage = temp / 'root'
        system_tree(app, stage)
        public = stage / 'etc/apt/keyrings/fly-browser.gpg'
        public.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(KEY, public)
        sources = stage / 'etc/apt/sources.list.d/fly-browser.sources'
        write(sources, APT_SOURCE)
        write(stage / 'etc/apt/preferences.d/fly-browser', '''Package: fly-browser-bin
Pin: release o=Fly Browser
Pin-Priority: 700

Package: *
Pin: release o=Fly Browser
Pin-Priority: -1
''')
        # Debian handles the old and t64 runtime names through alternatives.
        control = stage / 'DEBIAN'
        write(control / 'control', f'''Package: {CONFIG['package']}
Version: {VERSION.replace('-rc.', '~rc.')}-1
Architecture: amd64
Maintainer: Fly Browser contributors
Installed-Size: {sum(p.stat().st_size for p in stage.rglob('*') if p.is_file()) // 1024}
Depends: {', '.join(CONFIG['dependencies']['deb'])}
Provides: fly-browser
Conflicts: fly-browser
Section: web
Priority: optional
Homepage: {CONFIG['homepage']}
Description: {DESCRIPTION}
''')
        write(control / 'postinst', '''#!/bin/sh
set -e
if [ "$1" = configure ]; then sh /usr/share/fly-browser/integration.sh install; fi
''', 0o755)
        write(control / 'conffiles', '/etc/apt/sources.list.d/fly-browser.sources\n/etc/apt/keyrings/fly-browser.gpg\n/etc/apt/preferences.d/fly-browser\n')
        write(control / 'prerm', '''#!/bin/sh
set -e
if [ "$1" = remove ]; then sh /usr/share/fly-browser/integration.sh remove; fi
''', 0o755)
        run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z3', '--build', stage,
             output / f"{CONFIG['package']}_{VERSION}_amd64.deb"])
        shutil.rmtree(control)
        sources.unlink()
        shutil.rmtree(stage / 'etc')
        repo_config = RPM_REPO
        for directory in ['yum.repos.d', 'zypp/repos.d']:
            write(stage / f'etc/{directory}/fly-browser.repo', repo_config)
        # RPM dependencies use ABI capabilities, shared by Fedora/openSUSE.
        rpm = temp / 'rpm'
        rpm.mkdir()
        dependencies = CONFIG['dependencies']['rpm']
        spec = temp / 'fly.spec'
        filelist = '\n'.join('/' + str(p.relative_to(stage)) for p in stage.rglob('*') if p.is_file())
        # Preserve the sandbox helper's root ownership and setuid permission.
        filelist = filelist.replace('/opt/fly-browser/chrome-sandbox',
                                    '%attr(4755,root,root) /opt/fly-browser/chrome-sandbox')
        filelist = filelist.replace('/etc/', '%config(noreplace) /etc/')
        filelist += '\n' + '\n'.join('%dir /' + str(p.relative_to(stage))
            for p in stage.rglob('*') if p.is_dir() and
            (str(p.relative_to(stage)).startswith('opt/fly-browser') or
             str(p.relative_to(stage)) == 'usr/share/fly-browser'))
        write(spec, f'''Name: {CONFIG['package']}
Version: {VERSION.split('-')[0]}
Release: {'0.' + VERSION.split('-')[1].replace('-', '.') if '-' in VERSION else '1'}
Summary: {DESCRIPTION}
License: GPL-3.0-or-later
URL: {CONFIG['homepage']}
BuildArch: x86_64
AutoReqProv: no
Requires: {', '.join(dependencies)}
Provides: fly-browser
Conflicts: fly-browser
%description
{DESCRIPTION}
%prep
%build
%install
mkdir -p "%{{buildroot}}"
cp -a "{stage}/." "%{{buildroot}}/"
%post
sh /usr/share/fly-browser/integration.sh install
%preun
if [ "$1" = 0 ]; then sh /usr/share/fly-browser/integration.sh remove; fi
%files
{filelist}
''')
        run(['rpmbuild', '-bb', '--define', f'_topdir {rpm}',
             '--define', '_build_id_links none', '--define', '__os_install_post %{nil}', spec])
        for package in rpm.rglob('*.rpm'):
            shutil.copy2(package, output / package.name)
        shutil.rmtree(stage / 'etc')
        write(stage / 'etc/pacman.d/fly-browser.conf', PACMAN_CONF)
        # Arch's binary package format; no PKGBUILD execution or AUR required.
        info = [f"pkgname = {CONFIG['package']}", f'pkgbase = {CONFIG["package"]}',
                f'pkgver = {VERSION}-1', f'pkgdesc = {DESCRIPTION}',
                f'url = {CONFIG["homepage"]}', 'arch = x86_64',
                'license = GPL-3.0-or-later', 'packager = Fly Browser',
                f'builddate = {int(dt.datetime.now(dt.timezone.utc).timestamp())}',
                f'size = {sum(p.stat().st_size for p in stage.rglob("*") if p.is_file())}',
                'provides = fly-browser', 'conflict = fly-browser',
                'backup = etc/pacman.d/fly-browser.conf']
        info += ['depend = ' + dep for dep in CONFIG['dependencies']['pacman']]
        write(stage / '.PKGINFO', '\n'.join(info) + '\n')
        write(stage / '.INSTALL', '''post_install() { sh /usr/share/fly-browser/integration.sh install; }
post_upgrade() { post_install; }
pre_remove() { sh /usr/share/fly-browser/integration.sh remove; }
''')
        # libalpm uses the mtree for integrity checks and file metadata.
        mtree = run(['bsdtar', '--format=mtree', '--options=!all,use-set,type,uid,gid,mode,time,size,sha256',
                     '--uid', '0', '--gid', '0', '-cf', '-', '.'], cwd=stage, capture_output=True).stdout
        (stage / '.MTREE').write_bytes(gzip.compress(mtree, mtime=0))
        run(['bsdtar', '--zstd', '--uid', '0', '--gid', '0', '-cf',
             output / f"{CONFIG['package']}-{VERSION}-1-x86_64.pkg.tar.zst",
             '.PKGINFO', '.MTREE', '.INSTALL', 'opt', 'usr', 'etc'], cwd=stage)


def repositories(assets, output):
    """Flat APT/pacman live on Releases; Pages deploys a conventional RPM tree.

    APT joins its base URL to Filename, so it cannot serve package bodies from
    an unrelated host. Flat APT repositories on a release avoid Pages' size cap.
    RPM bodies are downloaded into the Pages artifact, outside its Git history.
    """
    if '-rc.' in VERSION:
        raise ValueError('Release candidates must not replace stable repositories')
    output.mkdir(parents=True, exist_ok=True)
    flat = output / 'flat'
    flat.mkdir()
    site = output / 'site/packages'
    site.mkdir(parents=True)
    for glob in ['*.deb', '*.rpm', '*.pkg.tar.zst']:
        for file in assets.glob(glob):
            shutil.copy2(file, flat / file.name)
    for rpm in flat.glob('*.rpm'):
        args = ['rpmsign', '--define', f'_gpg_name {fingerprint()}',
                '--define', '_gpg_digest_algo sha256', '--define', '__gpg /usr/bin/gpg']
        if password := os.environ.get('FLY_SIGNING_PASSPHRASE_FILE'):
            args += ['--define', f'_gpg_sign_cmd_extra_args --pinentry-mode loopback --passphrase-file {password}']
        run([*args, '--addsign', rpm])
    # Package signatures exist independently of repository metadata signatures.
    for file in sorted(flat.iterdir()):
        sign(file, '.sig')
    packages_index = run(['dpkg-scanpackages', '--multiversion', '.', '/dev/null'],
                         cwd=flat, capture_output=True).stdout
    (flat / 'Packages').write_bytes(packages_index)
    (flat / 'Packages.gz').write_bytes(gzip.compress(packages_index, mtime=0))
    now = dt.datetime.now(dt.timezone.utc)
    release = ['Origin: Fly Browser', 'Label: Fly Browser', 'Suite: stable',
               'Codename: fly', 'Architectures: amd64',
               'Date: ' + email.utils.format_datetime(now, usegmt=True),
               'Valid-Until: ' + email.utils.format_datetime(now + dt.timedelta(days=30), usegmt=True),
               'Description: Official Fly Browser packages', 'SHA256:']
    for name in ['Packages', 'Packages.gz']:
        data = (flat / name).read_bytes()
        release.append(f' {hashlib.sha256(data).hexdigest()} {len(data)} {name}')
    write(flat / 'Release', '\n'.join(release) + '\n')
    sign(flat / 'Release', '.gpg')
    sign(flat / 'Release', clearsign=True)
    arch = sorted(flat.glob('*.pkg.tar.zst'))
    run(['repo-add', flat / 'fly-browser.db.tar.gz', *arch])
    # GitHub assets are regular files, not filesystem symlinks.
    for name in ['fly-browser.db', 'fly-browser.files']:
        link = flat / name
        body = link.read_bytes()
        link.unlink()
        link.write_bytes(body)
        sign(link, '.sig')
    rpm_repo = site / 'rpm'
    rpm_repo.mkdir()
    # Only RPM files are indexed. The Pages builder materializes their bodies.
    with tempfile.TemporaryDirectory() as tmp:
        rpm_input = Path(tmp)
        for file in flat.glob('*.rpm'):
            shutil.copy2(file, rpm_input / file.name)
        # Conventional relative locations work with both DNF and libzypp.
        run(['createrepo_c', '--no-database',
             '--outputdir', rpm_repo, rpm_input])
    sign(rpm_repo / 'repodata/repomd.xml')
    manifest = {'version': VERSION, 'packages': [
        {'name': file.name, 'url': CONFIG['repository'] + '/' + file.name,
         'sha256': hashlib.sha256(file.read_bytes()).hexdigest(), 'size': file.stat().st_size}
        for file in sorted(flat.glob('*.rpm'))]}
    write(rpm_repo / 'packages.json', json.dumps(manifest, indent=2) + '\n')
    sign(rpm_repo / 'packages.json')
    for name in ['release-key.gpg', 'release-key.asc']:
        shutil.copy2(ROOT / 'distribution' / name, site / name)
    write(site / 'fly.sources', APT_SOURCE)
    write(site / 'fly.repo', RPM_REPO)
    write(site / 'pacman.conf', PACMAN_CONF)
    shutil.copy2(ROOT / 'scripts/fly-install', site / 'fly-install')
    write(site / 'index.html', f'''<!doctype html><html lang="en"><meta charset="utf-8">
<meta name="viewport" content="width=device-width"><title>Official Fly packages</title>
<h1>Official Fly Browser packages</h1><p>Linux x86_64. Published by Fly, signed by Fly.</p>
<p>Signing key fingerprint: <code>{fingerprint()}</code></p>
<p><a href="{CONFIG['homepage']}/blob/main/docs/guide/installation.md">Installation instructions</a>
 · <a href="release-key.asc">Public signing key</a></p></html>''')
    # No silent success without native packages or signature verification.
    if not all(list(flat.glob(pattern)) for pattern in ['*.deb', '*.rpm', '*.pkg.tar.zst']):
        raise ValueError('Missing a native package format')
    verify(flat / 'Release', flat / 'Release.gpg')
    verify(rpm_repo / 'repodata/repomd.xml', rpm_repo / 'repodata/repomd.xml.asc')


def sign_assets(assets):
    # RPM signing changes its bytes; call after repositories() copies the signed
    # RPM back, before constructing the release checksums.
    for file in sorted(assets.iterdir()):
        if file.is_file() and not file.name.endswith(('.asc', '.sig')) and file.name != 'SHA256SUMS.txt':
            sign(file)
    files = sorted(p for p in assets.iterdir() if p.is_file() and p.name != 'SHA256SUMS.txt'
                   and not p.name.endswith(('.asc', '.sig')))
    write(assets / 'SHA256SUMS.txt', ''.join(
        f'{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n' for p in files))
    sign(assets / 'SHA256SUMS.txt')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    build = sub.add_parser('packages')
    build.add_argument('--archive', required=True, type=Path)
    build.add_argument('--output', required=True, type=Path)
    repo = sub.add_parser('repositories')
    repo.add_argument('--assets', required=True, type=Path)
    repo.add_argument('--output', required=True, type=Path)
    signing = sub.add_parser('sign')
    signing.add_argument('--assets', required=True, type=Path)
    args = parser.parse_args()
    if args.command == 'packages':
        packages(args.archive.resolve(), args.output.resolve())
    elif args.command == 'repositories':
        repositories(args.assets.resolve(), args.output.resolve())
    else:
        sign_assets(args.assets.resolve())


if __name__ == '__main__':
    main()
