#!/usr/bin/env python3
"""Build and package pinned native Fly on a provisioned Linux x64 engine host.

Prepare the upstream control and full overlay with engine.py/native.py first.
This command verifies the overlay belongs to this repository checkout before
building; it never packages a stale binary left by another commit.
"""
import argparse
import platform
from pathlib import Path
import subprocess
import sys

import engine
import native


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--dist', type=Path, default=engine.REPO / 'dist')
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        parser.error('Native release packaging currently supports Linux x64 only')
    root = engine.engine_root(str(args.root))
    lock = engine.read_lock()
    with engine.exclusive(root):
        engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
        native.verify_source(root, lock)
        engine.verify_dependencies(root, source_verifier=lambda: native.verify_source(root, lock))
        env = engine.environment(root)
        output = root / 'src/out/FlyRelease'
        output.mkdir(parents=True, exist_ok=True)
        (output / 'args.gn').write_bytes((engine.REPO / 'chromium/gn/release.gnargs').read_bytes())
        engine.run(['gn', 'gen', 'out/FlyRelease'], root / 'src', env)
        engine.run(['autoninja', '-C', 'out/FlyRelease', '-j', str(args.jobs),
                    'chrome', 'chrome_sandbox'], root / 'src', env)
        native.verify_source(root, lock)
        subprocess.run([sys.executable, str(engine.REPO / 'scripts/chromium/package-linux.py'),
                        '--root', str(root), '--dist', str(args.dist)], check=True)


if __name__ == '__main__':
    main()
