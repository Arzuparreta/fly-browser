#!/usr/bin/env python3
"""Build and package pinned native Fly on a provisioned Linux x64 engine host.

Prepare the upstream control and full overlay with engine.py/native.py first.
This command verifies the overlay belongs to this repository checkout before
building; it never packages a stale binary left by another commit.
"""
import argparse
import os
import platform
from pathlib import Path
import re
import subprocess
import sys

import engine
import native


def release_config(api_key='', optimization='pgo-thinlto'):
    config = (engine.REPO / 'chromium/gn/release.gnargs').read_text()
    if optimization != 'pgo-thinlto':
        config = config.replace('use_thin_lto = true', 'use_thin_lto = false')
        config = config.replace('thin_lto_enable_optimizations = true',
                                'thin_lto_enable_optimizations = false')
    if optimization == 'none':
        config = config.replace('chrome_pgo_phase = 2', 'chrome_pgo_phase = 0')
    if api_key:
        config += f'google_api_key = "{api_key}"\n'
    return config


def prepare_pgo(root, env):
    """Use the profile selected by this exact checkout, never a floating latest."""
    source = root / 'src'
    selector = source / 'chrome/build/linux.pgo.txt'
    name = selector.read_text().strip()
    if not re.fullmatch(r'[A-Za-z0-9_.-]+\.profdata', name):
        engine.fail('Invalid pinned Linux PGO profile filename')
    profile = source / 'chrome/build/pgo_profiles' / name
    if profile.is_symlink():
        engine.fail('Symlinked PGO profile; preserve and inspect it')
    engine.run([sys.executable, 'tools/update_pgo_profiles.py', '--target=linux',
                'update', '--gs-url-base=chromium-optimization-profiles/pgo_profiles'],
               source, env)
    if not profile.is_file() or not profile.stat().st_size:
        engine.fail('Pinned Linux PGO profile is missing or empty')
    return {'filename': name, 'sha256': engine.digest(profile),
            'size_bytes': profile.stat().st_size,
            'selector_sha256': engine.digest(selector),
            'source': 'https://storage.googleapis.com/chromium-optimization-profiles/pgo_profiles/' + name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--dist', type=Path, default=engine.REPO / 'dist')
    parser.add_argument('--output', default='out/FlyRelease',
                        help='build directory below src/out (separate A/B builds)')
    parser.add_argument('--optimization', choices=('none', 'pgo', 'pgo-thinlto'),
                        default='pgo-thinlto',
                        help='release optimizations; none/pgo are comparison builds')
    parser.add_argument('--require-google-api-key', action='store_true',
                        help='fail unless FLY_GOOGLE_API_KEY is set (releases)')
    args = parser.parse_args()
    output_name = native.relative_path(args.output)
    if len(output_name.parts) != 2 or output_name.parts[0] != 'out':
        parser.error('--output must name a directory directly below src/out')
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        parser.error('Native release packaging currently supports Linux x64 only')
    # Google API key for Safe Browsing, from the FLY_GOOGLE_API_KEY secret.
    # It is compiled into the browser; it never enters the repository.
    api_key = os.environ.get('FLY_GOOGLE_API_KEY', '').strip()
    if api_key and not re.fullmatch(r'[A-Za-z0-9_-]+', api_key):
        parser.error('FLY_GOOGLE_API_KEY is not a Google API key')
    if not api_key:
        if args.require_google_api_key:
            parser.error('FLY_GOOGLE_API_KEY is not set; a release without it has no Safe Browsing')
        print('FLY_GOOGLE_API_KEY is not set: this build has no Safe Browsing.', file=sys.stderr)
    root = engine.engine_root(str(args.root))
    lock = engine.read_lock()
    with engine.exclusive(root):
        engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
        native.verify_source(root, lock)
        engine.verify_dependencies(root, source_verifier=lambda: native.verify_source(root, lock))
        env = engine.environment(root)
        output = root / 'src' / str(output_name)
        output.mkdir(parents=True, exist_ok=True)
        profile = prepare_pgo(root, env) if args.optimization != 'none' else None
        config = release_config(api_key, args.optimization)
        (output / 'args.gn').write_text(config)
        engine.run(['gn', 'gen', str(output_name)], root / 'src', env)
        engine.run(['autoninja', '-C', str(output_name), '-j', str(args.jobs),
                    'chrome', 'chrome_sandbox'], root / 'src', env)
        native.verify_source(root, lock)
        if profile and engine.digest(root / 'src/chrome/build/pgo_profiles' /
                                     profile['filename']) != profile['sha256']:
            engine.fail('PGO profile changed during the build; preserve the evidence and rebuild')
        engine.atomic_json(output / 'fly-build.json', {
            'schema': 1, 'optimization': args.optimization,
            'pgo_profile': profile, 'overlay_fingerprint': native.fingerprint(),
            'binary_sha256': engine.digest(output / 'chrome'),
            'args_sha256': engine.digest(output / 'args.gn')})
        subprocess.run([sys.executable, str(engine.REPO / 'scripts/chromium/package-linux.py'),
                        '--root', str(root), '--output', str(output_name),
                        '--dist', str(args.dist)], check=True)


if __name__ == '__main__':
    main()
