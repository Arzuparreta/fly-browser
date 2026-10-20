#!/usr/bin/env python3
"""Download, verify and unpack the extensions bundled with native Fly.

Reads chromium/bundled-extensions.json and writes <out>/<id>/ for each entry,
with the Web Store public key added to manifest.json so the extension keeps the
same identity wherever Fly is installed. Downloads are cached in --cache and
must match the pinned SHA-256. Existing output is replaced atomically.
"""
import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / 'chromium' / 'bundled-extensions.json'


def fail(message):
    print(f'bundled-extensions: {message}', file=sys.stderr)
    raise SystemExit(1)


def extension_id(key_b64):
    import base64
    digest = hashlib.sha256(base64.b64decode(key_b64)).hexdigest()[:32]
    return ''.join(chr(97 + int(c, 16)) for c in digest)


def fetch(entry, cache):
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f"{entry['id']}-{entry['version']}.zip"
    if not archive.is_file():
        partial = archive.with_suffix('.partial')
        with urllib.request.urlopen(entry['url'], timeout=120) as response:
            partial.write_bytes(response.read())
        partial.replace(archive)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    if digest != entry['sha256']:
        archive.unlink()
        fail(f"{entry['name']}: SHA-256 {digest} does not match the lock")
    return archive


def unpack(entry, archive, out):
    target = out / entry['id']
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        stage = Path(tmp) / entry['id']
        stage.mkdir()
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                path = Path(member.filename)
                if path.is_absolute() or '..' in path.parts:
                    fail(f'unsafe path in archive: {member.filename}')
                if (member.external_attr >> 16) & 0o170000 == 0o120000:
                    fail(f'symbolic link in archive: {member.filename}')
            bundle.extractall(stage)
        manifest_path = stage / 'manifest.json'
        manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
        if manifest.get('version') != entry['version']:
            fail(f"{entry['name']}: manifest version {manifest.get('version')}")
        manifest['key'] = entry['key']
        manifest_path.write_text(json.dumps(manifest, indent=2), encoding='utf-8')
        if target.exists():
            shutil.rmtree(target)
        stage.replace(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--cache', type=Path,
                        default=Path(tempfile.gettempdir()) / 'fly-bundled-extensions')
    parser.add_argument('--check', action='store_true',
                        help='validate the lock only (no network)')
    args = parser.parse_args()
    lock = json.loads(LOCK.read_text(encoding='utf-8'))
    if lock.get('schema') != 1:
        fail('unknown lock schema')
    for entry in lock['extensions']:
        if extension_id(entry['key']) != entry['id']:
            fail(f"{entry['name']}: key does not produce {entry['id']}")
    if args.check:
        print(f"{len(lock['extensions'])} bundled extension(s) pinned")
        return
    if not args.out:
        fail('--out is required')
    args.out.mkdir(parents=True, exist_ok=True)
    for entry in lock['extensions']:
        unpack(entry, fetch(entry, args.cache), args.out)
        print(f"{entry['name']} {entry['version']} -> {args.out / entry['id']}")


if __name__ == '__main__':
    main()
