#!/usr/bin/env python3
"""Build the Pages site, materializing signed RPM bodies outside Git.

APT/pacman use flat Releases repositories. libzypp cannot use an external
package URL in RPM metadata, so Pages deploys a standard RPM tree as an
artifact. Large binaries never enter the site's Git history.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import urllib.request

SOURCE = 'https://github.com/Arzuparreta/fly-browser/releases/download/'
API = 'https://api.github.com/repos/Arzuparreta/fly-browser/releases?per_page=15'
LIMIT = 900 * 1024 * 1024


def download(url, destination):
    if not url.startswith(SOURCE):
        raise ValueError('Package URL must belong to the official Fly releases')
    request = urllib.request.Request(url, headers={'User-Agent': 'Fly-packages'})
    with urllib.request.urlopen(request, timeout=120) as response, destination.open('wb') as out:
        shutil.copyfileobj(response, out)


def verify(file, signature, key):
    with tempfile.TemporaryDirectory() as home:
        subprocess.run(['gpgv', '--homedir', home, '--keyring', str(key.resolve()),
                        str(signature), str(file)], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path.cwd())
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--local-pool', type=Path, help='Offline rehearsal: use a local signed package pool, without release history')
    args = parser.parse_args()
    source, output = args.root.resolve(), args.output.resolve()
    if output == source or source.is_relative_to(output):
        raise ValueError('Output must be separate from the source tree')
    if output.exists():
        raise ValueError('Output already exists; choose a fresh build directory')
    shutil.copytree(source, output, ignore=shutil.ignore_patterns(
        '.git', '.github', 'README.md', output.name, '__pycache__'))
    metadata = source / 'packages/rpm/packages.json'
    if not metadata.exists():
        return  # The bootstrap page exists before the first signed release.
    key = source / 'packages/release-key.gpg'
    verify(metadata, metadata.with_suffix('.json.asc'), key)
    manifest = json.loads(metadata.read_text())
    desired = list(manifest['packages'])
    # Retain two previous releases for clients with a recently cached index.
    request = urllib.request.Request(API, headers={'User-Agent': 'Fly-packages'})
    if args.local_pool:
        releases = []
    else:
        with urllib.request.urlopen(request, timeout=30) as response:
            releases = json.load(response)
    retained = 0
    with tempfile.TemporaryDirectory() as tmp:
        temp = Path(tmp)
        for release in releases:
            tag = release['tag_name']
            if (release['draft'] or release['prerelease'] or tag == 'v' + manifest['version']
                    or not re.fullmatch(r'v\d+\.\d+\.\d+', tag)):
                continue
            names = {item['name'] for item in release['assets']}
            rpms = sorted(name for name in names if name.endswith('.rpm'))
            if not rpms or 'SHA256SUMS.txt.asc' not in names:
                continue
            sums, signature = temp / 'sums', temp / 'sums.asc'
            download(SOURCE + tag + '/SHA256SUMS.txt', sums)
            download(SOURCE + tag + '/SHA256SUMS.txt.asc', signature)
            verify(sums, signature, key)
            hashes = {line.split()[1]: line.split()[0] for line in sums.read_text().splitlines()}
            desired += [{'name': name, 'url': SOURCE + tag + '/' + name, 'sha256': hashes[name]}
                        for name in rpms]
            retained += 1
            if retained == 2:
                break
        for entry in desired:
            name = entry['name']
            if not re.fullmatch(r'fly-browser-bin-[A-Za-z0-9_.-]+\.rpm', name):
                raise ValueError('Invalid RPM filename')
            if not re.fullmatch(r'[a-f0-9]{64}', entry['sha256']):
                raise ValueError('Invalid package digest')
            target = output / 'packages/rpm' / name
            if args.local_pool:
                shutil.copyfile(args.local_pool / name, target)
            else:
                download(entry['url'], target)
            if hashlib.sha256(target.read_bytes()).hexdigest() != entry['sha256']:
                raise ValueError('Downloaded RPM does not match its signed digest')
            print(f'Verified {name}', flush=True)
    size = sum(file.stat().st_size for file in output.rglob('*') if file.is_file())
    if size > LIMIT:
        raise ValueError('RPM repository and landing exceed the Pages size budget; previous deployment is kept')
    print(f'Pages artifact: {size // (1024 * 1024)} MiB', flush=True)


if __name__ == '__main__':
    main()
