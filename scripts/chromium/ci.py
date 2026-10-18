#!/usr/bin/env python3
"""Manual hosted-runner validation; always retain the failing stage's evidence."""
import datetime
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import sys
import time

import engine


def run_logged(command, log, label):
    """Keep exact child bytes and bounded live progress if artifact upload is lost."""
    last_progress = None
    with log.open('wb') as output:
        with subprocess.Popen(command, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT) as process:
            for raw in process.stdout:
                output.write(raw)
                lines = raw.decode('utf-8', errors='replace').splitlines()
                line = lines[-1] if lines else ''
                now = time.monotonic()
                if (last_progress is None or now - last_progress >= 15 or
                        'error:' in line.lower() or 'FAILED' in line):
                    # Prefix every emitted line so source output is never a
                    # leading GitHub workflow command. Full logs remain exact.
                    print('[' + label + '] ' + line[-2000:], flush=True)
                    output.flush()
                    last_progress = now
            return process.wait()


def main():
    root = engine.engine_root(os.environ['FLY_CI_ENGINE_ROOT'])
    depth = os.environ['FLY_CI_DEPTH']
    if depth not in ('configure', 'units', 'objects', 'build'):
        engine.fail('Unknown CI depth')
    evidence = root / 'evidence'
    evidence.mkdir(exist_ok=True)
    record = {'schema': 1, 'platform': platform.platform(), 'depth': depth,
              'fly_commit': os.environ.get('GITHUB_SHA'),
              'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'disk': shutil.disk_usage(root)._asdict(), 'steps': [],
              'native_desktop_supported': sys.platform in ('linux', 'win32'),
              'acceptance': 'Individual tests are evidence; complete desktop/package acceptance is not implied.'}
    engine.atomic_json(evidence / 'ci.json', record)
    commands = []
    # Component/symbol-free local control output measures about 10 GiB. Keep
    # explicit phase-specific floors: reserve source plus both outputs initially,
    # then avoid demanding that entire initial budget again after sync/build.
    # Resource failures remain failures with actual free-space evidence.
    stages = ('doctor', 'bootstrap', 'sync', 'hooks')
    if depth not in ('units', 'objects'):
        stages += ('configure',)
    for stage in stages:
        minimum = ({'doctor': 80, 'bootstrap': 80, 'sync': 60}.get(stage, 40)
                   if depth == 'build' else 40)
        commands.append([sys.executable, str(Path(__file__).with_name('engine.py')),
                         stage, '--root', str(root), '--minimum-free-gib',
                         str(minimum)])
    if depth == 'units':
        commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                         'test-core', '--root', str(root), '--jobs', '2'])
    if depth == 'objects':
        commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                         'test-objects', '--root', str(root), '--jobs', '2',
                         '--minimum-free-gib', '40'])
    if depth == 'build':
        commands.append([sys.executable, str(Path(__file__).with_name('engine.py')),
                         'build', '--root', str(root), '--minimum-free-gib', '40',
                         '--jobs', '2'])
        for stage in ('apply', 'configure', 'test', 'build'):
            commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                             stage, '--root', str(root), '--minimum-free-gib', '40',
                             '--jobs', '2'])
        if sys.platform in ('linux', 'win32'):
            commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                             'test-browser', '--root', str(root), '--minimum-free-gib', '40',
                             '--jobs', '2'])
    for index, command in enumerate(commands):
        log = evidence / f'ci-{index:02d}-{command[2]}.log'
        if command[2] == 'test' and Path(command[1]).name == 'native.py':
            # Preserve the complete control runtime/binary/provenance for
            # comparison, freeing only its no-longer-needed compilation objects.
            objects = root / 'src/out/FlyControl/obj'
            if objects.exists():
                before = shutil.disk_usage(root).free
                removed = 0
                suffixes = {'.o', '.obj', '.a', '.lib', '.rlib', '.rmeta', '.pcm', '.pch'}
                for file in objects.rglob('*'):
                    if file.suffix in suffixes and file.is_file() and not file.is_symlink():
                        file.unlink()
                        removed += 1
                record['removed_control_build_objects'] = {
                    'path': str(objects), 'files': removed,
                    'freed_bytes': shutil.disk_usage(root).free - before}
                engine.atomic_json(evidence / 'ci.json', record)
        if command[2] == 'test-browser':
            if sys.platform == 'linux':
                command = ['xvfb-run', '--auto-servernum',
                           '--server-args=-screen 0 1280x1024x24', *command]
        print('+ ' + ' '.join(command), flush=True)
        record['active_step'] = {'command': command, 'log': log.name}
        engine.atomic_json(evidence / 'ci.json', record)
        exit_code = run_logged(command, log, log.stem)
        record['steps'].append({'command': command, 'exit_code': exit_code,
                                'log': log.name, 'log_sha256': engine.digest(log)})
        record.pop('active_step', None)
        engine.atomic_json(evidence / 'ci.json', record)
        if exit_code:
            # Keep the log in the artifact; bounded output avoids giant sync logs.
            for line in log.read_text(encoding='utf-8', errors='replace')[-16000:].splitlines():
                print('[' + log.stem + '] ' + line, flush=True)
            return exit_code
    record['completed_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    engine.atomic_json(evidence / 'ci.json', record)
    return 0


if __name__ == '__main__':
    sys.exit(main())
