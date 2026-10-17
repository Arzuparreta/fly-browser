#!/usr/bin/env python3
"""Explicit, resumable Chromium control builds. Never invoked by npm test."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import contextlib
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]
LOCK = REPO / 'chromium/upstream.lock.json'
CONFIG = REPO / 'chromium/gn/control.gnargs'


def fail(message):
    raise RuntimeError(message)


def read_lock():
    lock = json.loads(LOCK.read_text())
    if lock.get('schema') != 1:
        fail('Unsupported upstream lock schema')
    expected = {
        'chromium': 'https://chromium.googlesource.com/chromium/src.git',
        'depot_tools': 'https://chromium.googlesource.com/chromium/tools/depot_tools.git',
    }
    for name, url in expected.items():
        if lock[name]['url'] != url or not re.fullmatch(r'[0-9a-f]{40}', lock[name]['revision']):
            fail(f'Invalid official source pin: {name}')
    if not re.fullmatch(r'[0-9a-f]{64}', lock['chromium']['deps_sha256']):
        fail('Invalid DEPS digest')
    return lock


def run(args, cwd, env=None, capture=False):
    if not capture:
        print('+ ' + ' '.join(str(arg) for arg in args), flush=True)
    command = [str(arg) for arg in args]
    if sys.platform == 'win32':
        resolved = shutil.which(command[0], path=(env or os.environ).get('PATH'))
        if resolved:
            command[0] = resolved
    result = subprocess.run(command, cwd=cwd, env=env,
                            check=True, text=True,
                            stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def git(directory, *args):
    return run(['git', *args], directory, capture=True)


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def atomic_json(file, value):
    file.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=file.parent, prefix=file.name + '.')
    try:
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, indent=2)
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, file)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def engine_root(raw):
    root = Path(raw).expanduser().resolve()
    if root == REPO or REPO in root.parents or root in REPO.parents:
        fail('Use a dedicated external checkout outside the Fly repository')
    if any(char.isspace() for char in str(root)):
        fail('Chromium checkout paths must not contain whitespace')
    root.mkdir(parents=True, exist_ok=True)
    return root


@contextlib.contextmanager
def exclusive(root):
    # Atomic mkdir works on the supported build hosts without an extra module.
    lease = root / '.fly-engine-operation'
    try:
        lease.mkdir()
    except FileExistsError:
        fail(f'Another engine command owns {lease}; inspect owner.json and its '
             'process before removing a stale lease. Never run sync during a build.')
    try:
        atomic_json(lease / 'owner.json', {'pid': os.getpid(), 'host': platform.node(),
                                         'command': sys.argv[1:]})
        yield
    finally:
        (lease / 'owner.json').unlink(missing_ok=True)
        lease.rmdir()


def environment(root):
    env = dict(os.environ)
    env['PATH'] = str(root / 'depot_tools') + os.pathsep + env.get('PATH', '')
    env['DEPOT_TOOLS_UPDATE'] = '0'
    env['DEPOT_TOOLS_METRICS'] = '0'
    env['DEPOT_TOOLS_WIN_TOOLCHAIN'] = '0'
    env['PYTHONUTF8'] = '1'
    # Preserve pinned source bytes on Windows without changing the owner's
    # global Git configuration. Dependencies inherit this runtime-only setting.
    count = int(env.get('GIT_CONFIG_COUNT', '0'))
    env[f'GIT_CONFIG_KEY_{count}'] = 'core.autocrlf'
    env[f'GIT_CONFIG_VALUE_{count}'] = 'false'
    env['GIT_CONFIG_COUNT'] = str(count + 1)
    return env


def doctor(root, minimum_free_gib=100):
    for tool in ('git', 'python3'):
        if not shutil.which(tool):
            fail(f'Missing required tool: {tool}')
    free = shutil.disk_usage(root).free
    ram = None
    if sys.platform == 'linux':
        ram = int(re.search(r'MemTotal:\s+(\d+)', Path('/proc/meminfo').read_text())[1]) * 1024
    result = {'platform': platform.platform(), 'machine': platform.machine(),
              'cpus': os.cpu_count(), 'free_bytes': free, 'ram_bytes': ram,
              'root': str(root), 'python': platform.python_version()}
    print(json.dumps(result, indent=2))
    if sys.version_info < (3, 9):
        fail('Chromium requires Python 3.9 or newer')
    if free < minimum_free_gib * 1024**3:
        fail(f'Less than {minimum_free_gib} GiB free: source sync/build requires another volume')
    if ram is not None and ram < 8 * 1024**3:
        fail('Less than 8 GiB RAM: below upstream Linux build minimum')
    return result


def verify_checkout(directory, pin):
    if not (directory / '.git').exists():
        fail(f'Missing Git checkout: {directory}; run bootstrap first')
    if git(directory, 'remote', 'get-url', 'origin') != pin['url']:
        fail(f'Unexpected origin in {directory}; no changes made')
    if git(directory, 'rev-parse', 'HEAD') != pin['revision']:
        fail(f'Unexpected source revision in {directory}; no reset is permitted')
    if git(directory, 'status', '--porcelain', '--untracked-files=normal'):
        fail(f'Dirty checkout: {directory}; preserve and inspect changes first')


def checkout(root, name, pin):
    directory = root / name
    if directory.exists() and not (directory / '.git').exists() and any(directory.iterdir()):
        fail(f'Refusing to use nonempty non-Git directory: {directory}')
    directory.mkdir(exist_ok=True)
    if not (directory / '.git').exists():
        run(['git', 'init'], directory)
        run(['git', 'remote', 'add', 'origin', pin['url']], directory)
    if git(directory, 'remote', 'get-url', 'origin') != pin['url']:
        fail(f'Unexpected origin: {directory}')
    # A cancelled initial fetch has no HEAD and can safely resume. Existing
    # checkouts at another revision require a deliberate upstream-update review.
    head = subprocess.run(['git', 'rev-parse', '--verify', 'HEAD'], cwd=directory,
                          text=True, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if head.returncode == 0:
        verify_checkout(directory, pin)
        return
    if any(item.name != '.git' for item in directory.iterdir()):
        fail(f'Unborn checkout has unrelated files: {directory}')
    run(['git', 'fetch', '--depth=1', 'origin', pin['revision']], directory)
    run(['git', '-c', 'core.autocrlf=false', 'checkout', '--detach', pin['revision']], directory)
    verify_checkout(directory, pin)


def verify(root, lock):
    verify_checkout(root / 'depot_tools', lock['depot_tools'])
    verify_checkout(root / 'src', lock['chromium'])
    if digest(root / 'src/DEPS') != lock['chromium']['deps_sha256']:
        fail('DEPS differs from the pinned upstream content')
    return environment(root)


def gclient_config(root, lock):
    expected = ('solutions = [\n  {"name": "src", "url": ' +
                repr(lock['chromium']['url']) +
                ', "managed": False, "custom_deps": {}, "custom_vars": {}},\n]\n')
    file = root / '.gclient'
    if file.exists() and file.read_text() != expected:
        fail('Existing .gclient differs; refusing to overwrite checkout configuration')
    if not file.exists():
        file.write_text(expected)


def verify_dependencies(root, source_verifier=None):
    file = root / 'evidence/dependency-revisions.json'
    record = root / 'evidence/sync.json'
    if not file.exists() or not record.exists():
        fail('No completed dependency inventory; run sync first')
    expected = json.loads(record.read_text())['details']['dependency_revisions_sha256']
    if digest(file) != expected:
        fail('Dependency revision inventory changed since sync')
    dependencies = json.loads(file.read_text())
    def check(item):
        relative, pin = item
        if pin.get('rev') is None:
            return  # CIPD/GCS objects are content-pinned in DEPS, not Git trees.
        if relative == 'src' and source_verifier is not None:
            source_verifier()
            return
        directory = (root / relative).resolve()
        if root not in directory.parents or ':' in relative or not re.fullmatch('[0-9a-f]{40}', pin['rev']):
            fail('Invalid dependency inventory path or revision')
        verify_checkout(directory, {'url': pin['url'], 'revision': pin['rev']})
    with ThreadPoolExecutor(max_workers=8) as pool:
        # Consume all futures; do not leave background Git operations running.
        results = list(pool.map(check, dependencies.items()))
    print(f'Checked {len(results)} dependency records (Git revisions and clean trees).', flush=True)


def require_stage(root, stage, lock):
    file = root / 'evidence' / (stage + '.json')
    if not file.exists() or json.loads(file.read_text())['lock_sha256'] != digest(LOCK):
        fail(f'{stage} has not succeeded for the current lock; run it first')


def evidence(root, stage, lock, details=None):
    value = {'stage': stage, 'completed_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
             'lock_sha256': digest(LOCK), 'source': lock,
             'fly_commit': git(REPO, 'rev-parse', 'HEAD'),
             'fly_dirty': bool(git(REPO, 'status', '--porcelain')),
             'details': details or {}}
    atomic_json(root / 'evidence' / (stage + '.json'), value)


def binary(root):
    output = root / 'src/out/FlyControl'
    if sys.platform == 'darwin':
        return output / 'Chromium.app/Contents/MacOS/Chromium'
    return output / ('chrome.exe' if sys.platform == 'win32' else 'chrome')


def execute(args):
    lock = read_lock()
    root = engine_root(args.root)
    with exclusive(root):
        if args.command == 'doctor':
            doctor(root, args.minimum_free_gib)
            return
        if args.command == 'bootstrap':
            doctor(root, args.minimum_free_gib)
            checkout(root, 'depot_tools', lock['depot_tools'])
            if sys.platform == 'win32':
                # Bootstrap pinned tools directly; update_depot_tools.bat would
                # move the checkout to a floating upstream revision.
                run([root / 'depot_tools/bootstrap/win_tools.bat'], root, environment(root))
                run([root / 'depot_tools/cipd_bin_setup.bat'], root, environment(root))
            else:
                run([root / 'depot_tools/ensure_bootstrap'], root, environment(root))
            verify_checkout(root / 'depot_tools', lock['depot_tools'])
            checkout(root, 'src', lock['chromium'])
            gclient_config(root, lock)
            evidence(root, 'bootstrap', lock)
            return
        env = verify(root, lock)
        if args.command == 'sync':
            if (root / 'evidence/sync.json').exists():
                verify_dependencies(root)
        else:
            require_stage(root, 'sync', lock)
            verify_dependencies(root)
        if args.command == 'sync':
            doctor(root, args.minimum_free_gib)
            gclient_config(root, lock)
            run(['gclient', 'sync', '--nohooks', '--no-history',
                 '--revision', 'src@' + lock['chromium']['revision'],
                 '--jobs', str(args.jobs)], root, env)
            verify(root, lock)
            revisions = root / 'evidence/dependency-revisions.json'
            revisions.parent.mkdir(exist_ok=True)
            run(['gclient', 'revinfo', '--actual', '--output-json', revisions], root, env)
            evidence(root, 'sync', lock, {'dependency_revisions_sha256': digest(revisions)})
            verify_dependencies(root)
        elif args.command == 'hooks':
            require_stage(root, 'sync', lock)
            run(['gclient', 'runhooks'], root, env)
            verify(root, lock)
            evidence(root, 'hooks', lock)
        elif args.command == 'configure':
            require_stage(root, 'hooks', lock)
            output = root / 'src/out/FlyControl'
            output.mkdir(parents=True, exist_ok=True)
            config = output / 'args.gn'
            if config.exists() and config.read_bytes() != CONFIG.read_bytes():
                fail('Existing control args differ; preserve output before reconfiguration')
            config.write_bytes(CONFIG.read_bytes())
            run(['gn', 'gen', 'out/FlyControl'], root / 'src', env)
            evidence(root, 'configure', lock, {'args_sha256': digest(config)})
        elif args.command == 'build':
            doctor(root, args.minimum_free_gib)
            require_stage(root, 'configure', lock)
            if digest(root / 'src/out/FlyControl/args.gn') != digest(CONFIG):
                fail('Control build arguments changed since configuration')
            run(['autoninja', '-C', 'out/FlyControl', '-j', str(args.jobs), 'chrome'], root / 'src', env)
            verify(root, lock)
            evidence(root, 'build', lock, {'binary': str(binary(root)), 'sha256': digest(binary(root))})
        elif args.command == 'run':
            require_stage(root, 'build', lock)
            if not re.fullmatch(r'https?://[^\s]+|about:blank', args.url):
                fail('Use an HTTP(S) fixture URL or about:blank')
            if digest(binary(root)) != json.loads((root / 'evidence/build.json').read_text())['details']['sha256']:
                fail('Browser executable differs from the successful build record')
            profiles = root / 'disposable-profiles'
            profiles.mkdir(exist_ok=True, mode=0o700)
            profile = Path(tempfile.mkdtemp(prefix='control-', dir=profiles))
            print(f'Disposable profile retained for inspection: {profile}', flush=True)
            run([binary(root), '--user-data-dir=' + str(profile), '--no-first-run',
                 '--no-default-browser-check', args.url], root / 'src', env)
            evidence(root, 'run', lock, {'profile': str(profile), 'result': 'process exited successfully; manual acceptance still required'})
        elif args.command == 'verify':
            print('Official pinned source and tools match, with clean working trees.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['doctor', 'bootstrap', 'sync', 'hooks',
                                          'configure', 'build', 'run', 'verify'])
    parser.add_argument('--root', required=True, help='dedicated external Chromium checkout root')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    parser.add_argument('--url', default='about:blank')
    parser.add_argument('--minimum-free-gib', type=int, default=100,
                        help='explicit space floor; configuration-only CI uses 40 GiB')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.minimum_free_gib < 40:
        parser.error('--minimum-free-gib must be at least 40')
    try:
        execute(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        print('Chromium: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
