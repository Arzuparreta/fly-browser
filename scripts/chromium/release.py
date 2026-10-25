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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--dist', type=Path, default=engine.REPO / 'dist')
    parser.add_argument('--require-google-api-key', action='store_true',
                        help='fail unless FLY_GOOGLE_API_KEY is set (releases)')
    args = parser.parse_args()
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
        output = root / 'src/out/FlyRelease'
        output.mkdir(parents=True, exist_ok=True)
        config = (engine.REPO / 'chromium/gn/release.gnargs').read_text()
        if api_key:
            config += f'google_api_key = "{api_key}"\n'
        (output / 'args.gn').write_text(config)
        engine.run(['gn', 'gen', 'out/FlyRelease'], root / 'src', env)
        engine.run(['autoninja', '-C', 'out/FlyRelease', '-j', str(args.jobs),
                    'chrome', 'chrome_sandbox'], root / 'src', env)
        native.verify_source(root, lock)
        subprocess.run([sys.executable, str(engine.REPO / 'scripts/chromium/package-linux.py'),
                        '--root', str(root), '--dist', str(args.dist)], check=True)


if __name__ == '__main__':
    main()
