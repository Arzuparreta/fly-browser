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

import engine


def main():
    root = engine.engine_root(os.environ['FLY_CI_ENGINE_ROOT'])
    depth = os.environ['FLY_CI_DEPTH']
    if depth not in ('configure', 'units', 'build'):
        engine.fail('Unknown CI depth')
    evidence = root / 'evidence'
    evidence.mkdir(exist_ok=True)
    record = {'schema': 1, 'platform': platform.platform(), 'depth': depth,
              'fly_commit': os.environ.get('GITHUB_SHA'),
              'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'disk': shutil.disk_usage(root)._asdict(), 'steps': [],
              'acceptance': 'No desktop or packaged acceptance is implied by this run.'}
    engine.atomic_json(evidence / 'ci.json', record)
    commands = []
    # Source/configuration needs less space than a complete control+native build.
    # Resource failures remain failures with actual free-space evidence.
    stages = ('doctor', 'bootstrap', 'sync', 'hooks')
    if depth != 'units':
        stages += ('configure',)
    for stage in stages:
        commands.append([sys.executable, str(Path(__file__).with_name('engine.py')),
                         stage, '--root', str(root), '--minimum-free-gib',
                         '100' if depth == 'build' else '40'])
    if depth == 'units':
        commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                         'test-core', '--root', str(root)])
    if depth == 'build':
        commands.append([sys.executable, str(Path(__file__).with_name('engine.py')),
                         'build', '--root', str(root)])
        for stage in ('apply', 'configure', 'test', 'build'):
            commands.append([sys.executable, str(Path(__file__).with_name('native.py')),
                             stage, '--root', str(root)])
    for index, command in enumerate(commands):
        log = evidence / f'ci-{index:02d}-{command[2]}.log'
        print('+ ' + ' '.join(command), flush=True)
        with log.open('w', encoding='utf-8') as output:
            result = subprocess.run(command, stdout=output, stderr=subprocess.STDOUT)
        record['steps'].append({'command': command, 'exit_code': result.returncode,
                                'log': log.name, 'log_sha256': engine.digest(log)})
        engine.atomic_json(evidence / 'ci.json', record)
        if result.returncode:
            # Keep the log in the artifact; bounded output avoids giant sync logs.
            print(log.read_text(encoding='utf-8', errors='replace')[-16000:], flush=True)
            return result.returncode
    record['completed_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    engine.atomic_json(evidence / 'ci.json', record)
    return 0


if __name__ == '__main__':
    sys.exit(main())
