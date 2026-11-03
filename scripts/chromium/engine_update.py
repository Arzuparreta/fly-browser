#!/usr/bin/env python3
"""Keep Fly on the latest official Chromium Stable release.

  status       compare the pinned engine with the latest official release
  pin          write the latest Stable pin into chromium/upstream.lock.json
  patch-check  replay the patch series on a release's upstream files, without
               a checkout (an early warning for the next major version)
  seed         create a second engine root by copying an existing one locally,
               without downloading anything (build outputs are not copied)
  adopt        give an engine root the exact sources of a validated candidate
               root, copied locally (no second download)
  follow       bring an engine root to this checkout's pin: adopt a candidate
               validated for it, or move and rebuild; nothing when current
  move         bring an existing engine root to this checkout's pin: revert
               the overlay, move the sources, sync dependencies, run hooks
  upgrade      move, then rebuild the unmodified control and apply the overlay
               (and build Fly with --through build); nothing when already current
  validate     upgrade, build Fly and run its unit and browser tests

The update process is in docs/chromium/engine-updates.md.
"""
import argparse
import base64
import concurrent.futures
import contextlib
import datetime
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.parse

import engine
import native

SCRIPTS = Path(__file__).resolve().parent
SOURCE = 'https://chromium.googlesource.com/chromium/src'
MOVE_STATE = 'evidence/engine-move.json'
VALIDATION = 'evidence/engine-validation.json'


def load_pin():
    spec = importlib.util.spec_from_file_location('pin_upstream', SCRIPTS / 'pin-upstream.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def version(text):
    if not isinstance(text, str) or not re.fullmatch(r'\d+\.\d+\.\d+\.\d+', text):
        engine.fail('Invalid Chromium version: ' + repr(text))
    return tuple(int(part) for part in text.split('.'))


def same_pin(first, second):
    return (first['chromium']['revision'] == second['chromium']['revision'] and
            first['depot_tools']['revision'] == second['depot_tools']['revision'])


# --- status and pin ---------------------------------------------------------

def status(lock, latest):
    pinned, current = lock['engine_version'], latest['engine_version']
    return {'pinned': pinned, 'latest': current,
            'major': version(current)[0],
            'newer': version(current) > version(pinned),
            'new_major': version(current)[0] > version(pinned)[0]}


def write_github_output(values):
    file = os.environ.get('GITHUB_OUTPUT')
    if not file:
        return
    with open(file, 'a') as stream:
        for key, value in values.items():
            if isinstance(value, bool):
                value = 'true' if value else 'false'
            stream.write(f'{key}={value}\n')


def write_lock(latest):
    engine.atomic_json(engine.LOCK, engine.validate_lock(latest))
    os.chmod(engine.LOCK, 0o644)


# --- patch-check --------------------------------------------------------------

def gitiles_file(revision, name, fetch, attempts=6, wait=2.0):
    """Upstream bytes of one file at an exact revision, from Gitiles. Gitiles
    answers bursts with 429/503, so those are retried with growing waits."""
    native.relative_path(name)
    url = SOURCE + '/+/' + revision + '/' + urllib.parse.quote(name) + '?format=TEXT'
    for attempt in range(attempts):
        try:
            return base64.b64decode(fetch(url), validate=True)
        except urllib.error.HTTPError as error:
            if error.code not in (429, 500, 502, 503, 504) or attempt == attempts - 1:
                raise ValueError(f'Gitiles {error.code} for {name}') from error
        time.sleep(wait * 2 ** attempt)


def patch_check(revision, fetch, log=print):
    """Apply the series in order to the given revision's files. Returns the
    failing patches as {name: reason}; a failed patch is skipped, so the
    patches after it are still tried against the files it would have changed."""
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        engine.fail('Invalid Chromium revision')
    patches, _ = native.inputs()
    touched = sorted(native.patch_paths(patches))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        contents = dict(zip(touched, pool.map(lambda name: gitiles_file(revision, name, fetch),
                                              touched)))
    failures = {}
    with tempfile.TemporaryDirectory(prefix='fly-patch-check-') as tmp:
        stage = Path(tmp)
        engine.run(['git', 'init', '--quiet'], stage, capture=True)
        for name, data in contents.items():
            native.atomic_bytes(stage / name, data)
        for patch in patches:
            result = subprocess.run(['git', 'apply', '--check', '--whitespace=error-all',
                                     str(patch)], cwd=stage, text=True,
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                failures[patch.name] = result.stdout.strip()
                log(f'FAIL {patch.name}\n{result.stdout.strip()}')
                continue
            engine.run(['git', 'apply', '--whitespace=error-all', patch], stage, capture=True)
            log(f'ok   {patch.name}')
    return failures


# --- move -------------------------------------------------------------------

def recorded_lock(root):
    """The pin a root was last synchronised to, from its own sync record."""
    file = root / 'evidence/sync.json'
    if not file.is_file():
        engine.fail('This root has no completed sync; prepare a new root with '
                    'engine.py bootstrap and sync instead')
    return engine.validate_lock(json.loads(file.read_text())['source'])


def stage_current(root, stage):
    file = root / 'evidence' / (stage + '.json')
    return (file.is_file() and
            json.loads(file.read_text()).get('lock_sha256') == engine.digest(engine.LOCK))


def head(directory):
    return engine.git(directory, 'rev-parse', 'HEAD')


def move_checkout(directory, old, new, submodules=True):
    """Detach a shallow official checkout at a new revision. Refuses anything
    but a clean tree at the old or the new pin; never resets. With
    `submodules=False` the tree at the new pin may still have its gitlinks at
    the old dependency revisions (gclient sync moves them next)."""
    current = head(directory)
    if current == new['revision']:
        engine.verify_checkout(directory, new, submodules)
        return False
    engine.verify_checkout(directory, old)
    present = subprocess.run(['git', 'cat-file', '-e', new['revision'] + '^{commit}'],
                             cwd=directory, stderr=subprocess.DEVNULL).returncode == 0
    if not present:
        engine.run(['git', 'fetch', '--depth=1', 'origin', new['revision']], directory)
    engine.run(['git', '-c', 'core.autocrlf=false', 'checkout', '--quiet', '--detach',
                new['revision']], directory)
    engine.verify_checkout(directory, new, submodules)
    return True


def revert_overlay(root, old, lock):
    """Remove the managed overlay before the sources move under it."""
    file = root / native.MANIFEST
    if not file.exists():
        return 'none'
    manifest = json.loads(file.read_text())
    upstream = manifest.get('upstream')
    if upstream != old['chromium']['revision']:
        if upstream == lock['chromium']['revision']:
            engine.fail('The overlay already targets the new pin but its sources were never '
                        'synchronised; inspect the root')
        engine.fail('The engine overlay belongs to neither the recorded nor the new pin; '
                    'preserve native-overlay.json and inspect it')
    if manifest.get('phase') != 'applied':
        engine.fail('Interrupted overlay transaction; run native.py recover from a '
                    'checkout of the previous pin first')
    native.revert(root, old)
    return 'reverted'


def move(root, lock, jobs, minimum_free_gib=100):
    """Bring an engine root to this checkout's pin. Resumable: rerun it after
    an interruption and it continues from the step that did not finish."""
    root = engine.engine_root(root)
    old = recorded_lock(root)
    state_file = root / MOVE_STATE
    state = json.loads(state_file.read_text()) if state_file.is_file() else None
    if state and state.get('to') != lock['chromium']['revision']:
        engine.fail('An interrupted move to another pin is recorded in ' + str(state_file) +
                    '; finish it from a checkout of that pin first')
    if not state and same_pin(old, lock) and stage_current(root, 'sync') and stage_current(root, 'hooks'):
        overlay = root / native.MANIFEST
        if (not overlay.exists() or
                json.loads(overlay.read_text()).get('upstream') == lock['chromium']['revision']):
            print(f'Engine root is already at Chromium {lock["engine_version"]}.', flush=True)
            return False
    print(f'Moving {root} from Chromium {old["engine_version"]} to {lock["engine_version"]}.',
          flush=True)
    engine.doctor(root, minimum_free_gib)
    if not state:
        # Everything other than src must still be exactly what the last sync
        # left; gclient would otherwise move or refuse unexplained changes.
        engine.verify_dependencies(root, source_verifier=lambda: None)
        state = {'from': old['chromium']['revision'], 'from_version': old['engine_version'],
                 'to': lock['chromium']['revision'], 'to_version': lock['engine_version'],
                 'started_at': datetime.datetime.now(datetime.timezone.utc).isoformat()}
        engine.atomic_json(state_file, state)
    revert_overlay(root, old, lock)
    env = engine.environment(root)
    if move_checkout(root / 'depot_tools', old['depot_tools'], lock['depot_tools']):
        if sys.platform == 'win32':
            engine.run([root / 'depot_tools/bootstrap/win_tools.bat'], root, env)
            engine.run([root / 'depot_tools/cipd_bin_setup.bat'], root, env)
        else:
            engine.run([root / 'depot_tools/ensure_bootstrap'], root, env)
        engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
    synced = stage_current(root, 'sync')
    move_checkout(root / 'src', old['chromium'], lock['chromium'], submodules=synced)
    env = engine.verify(root, lock, submodules=synced)
    if not synced:
        engine.gclient_config(root, lock)
        engine.run(['gclient', 'sync', '--nohooks', '--no-history',
                    '--revision', 'src@' + lock['chromium']['revision'],
                    '--jobs', str(jobs)], root, env)
        env = engine.verify(root, lock)
        revisions = root / 'evidence/dependency-revisions.json'
        engine.run(['gclient', 'revinfo', '--actual', '--output-json', revisions], root, env)
        engine.evidence(root, 'sync', lock,
                        {'dependency_revisions_sha256': engine.digest(revisions),
                         'moved_from': state['from_version']})
    engine.verify_dependencies(root)
    if not stage_current(root, 'hooks'):
        engine.run(['gclient', 'runhooks'], root, env)
        engine.verify(root, lock)
        engine.evidence(root, 'hooks', lock, {'moved_from': state['from_version']})
    state['completed_at'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    engine.atomic_json(root / 'evidence/engine-move-last.json', state)
    state_file.unlink()
    print(f'Engine root is now at Chromium {lock["engine_version"]}.', flush=True)
    return True


# --- seed -------------------------------------------------------------------

SEED_ENTRIES = ('.gclient', '.gclient_entries', '.gclient_previous_sync_commits',
                '.gcs_entries', '.cipd', 'depot_tools', 'src')
SEED_EVIDENCE = ('bootstrap.json', 'sync.json', 'dependency-revisions.json', 'hooks.json',
                 native.MANIFEST.removeprefix('evidence/'))


def copy_root(source, root, touch):
    """rsync an engine root's sources and dependency state (never build
    outputs). With `touch`, every file written gets the current time: ninja
    rebuilds by modification time, and a copied file keeps the source root's
    time, which can be older than outputs built later in this root."""
    entries = [name for name in SEED_ENTRIES if (source / name).exists()]
    # --modify-window=-1: compare times to the nanosecond, not the second.
    result = subprocess.run(['rsync', '-a', '--delete', '--modify-window=-1', '--exclude=/src/out/',
                             '--out-format=%i %n', *[str(source / name) for name in entries],
                             str(root) + '/'], cwd=root, check=True, text=True,
                            stdout=subprocess.PIPE)
    written = [line.split(' ', 1)[1] for line in result.stdout.splitlines()
               if line.startswith('>f')]
    if touch:
        now = time.time()
        for name in written:
            file = root / name
            if file.is_file() and not file.is_symlink():
                os.utime(file, (now, now))
    (root / 'evidence').mkdir(exist_ok=True)
    for name in SEED_EVIDENCE:
        file = source / 'evidence' / name
        if file.is_file():
            shutil.copy2(file, root / 'evidence' / name)
        else:
            (root / 'evidence' / name).unlink(missing_ok=True)
    return len(written)


def source_verifier(root, lock):
    if (root / native.MANIFEST).is_file():
        return lambda: native.verify_source(root, lock, require_current=False)
    return lambda: engine.verify_checkout(root / 'src', lock['chromium'])


def seed(source, root):
    """Copy an engine root's sources and dependency state to a new root. A
    local copy instead of a second download of the whole engine; the new root
    then moves on its own and downloads only what changed.

    The source keeps working meanwhile (no lease for a copy of tens of GB);
    the copy is verified afterwards, and a rerun copies only what differs."""
    source, root = engine.engine_root(source), engine.engine_root(root)
    if source == root or source in root.parents or root in source.parents:
        engine.fail('Seed a separate root, outside the source root')
    marker = root / '.fly-engine-seed'
    if any(root.iterdir()) and not marker.is_file():
        engine.fail(f'{root} is not empty; seed only creates new roots')
    if not shutil.which('rsync'):
        engine.fail('rsync is required to seed a root')
    marker.write_text(str(source) + '\n')
    lock = recorded_lock(source)
    print(f'Copying {source} to {root} (sources only, no build outputs).', flush=True)
    copy_root(source, root, touch=False)
    if recorded_lock(root) != lock:
        engine.fail('The source root synchronised while it was copied; run seed again')
    engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
    try:
        engine.verify_dependencies(root, source_verifier=source_verifier(root, lock))
    except RuntimeError as error:
        engine.fail(f'{error}. The source changed during the copy; run seed again '
                    'to copy only the difference')
    marker.unlink()
    print(f'Seeded {root} from {source} at Chromium {lock["engine_version"]}.', flush=True)


def validated(candidate, lock):
    """Whether a candidate root passed validate for exactly this pin."""
    file = candidate / VALIDATION
    if not file.is_file():
        return False
    record = json.loads(file.read_text())
    return (record.get('lock_sha256') == engine.digest(engine.LOCK) and
            same_pin(recorded_lock(candidate), lock) and
            stage_current(candidate, 'sync') and stage_current(candidate, 'hooks'))


def adopt(candidate, root, lock):
    """Give `root` the sources of a candidate validated for this pin. Only
    files that differ are copied; nothing is downloaded."""
    candidate, root = engine.engine_root(candidate), engine.engine_root(root)
    if candidate == root or candidate in root.parents or root in candidate.parents:
        engine.fail('The candidate must be a separate root')
    if not validated(candidate, lock):
        engine.fail(f'{candidate} has not passed validate for Chromium {lock["engine_version"]}')
    with engine.exclusive(candidate), engine.exclusive(root):
        old = recorded_lock(root)
        # rsync --delete must not remove anything the overlay tooling does not
        # own: the same guard refresh-overlay applies.
        engine.verify_checkout(root / 'depot_tools', old['depot_tools'])
        engine.verify_dependencies(root, source_verifier=source_verifier(root, old))
        native.verify_source(candidate, lock, require_current=False)
        print(f'Adopting Chromium {lock["engine_version"]} from {candidate} into {root} '
              f'(was {old["engine_version"]}).', flush=True)
        count = copy_root(candidate, root, touch=True)
        # The copied sources carry the candidate's applied overlay, so check
        # them through its manifest rather than as a clean checkout.
        engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
        if engine.digest(root / 'src/DEPS') != lock['chromium']['deps_sha256']:
            engine.fail('DEPS differs from the pinned upstream content after adopting')
        engine.verify_dependencies(root, source_verifier=source_verifier(root, lock))
        engine.evidence(root, 'engine-adopt', lock, {'from': str(candidate),
                                                     'moved_from': old['engine_version'],
                                                     'files_written': count})
    print(f'{root} is now at Chromium {lock["engine_version"]} ({count} files written).',
          flush=True)


def root_current(root, lock):
    overlay = root / native.MANIFEST
    return (same_pin(recorded_lock(root), lock) and stage_current(root, 'sync') and
            stage_current(root, 'hooks') and not (root / MOVE_STATE).exists() and
            (not overlay.exists() or
             json.loads(overlay.read_text()).get('upstream') == lock['chromium']['revision']))


def follow(root, lock, jobs, candidate=None):
    """What a release runs first: bring its engine root to the release's pin."""
    root = engine.engine_root(root)
    if root_current(root, lock):
        print(f'Engine root is already at Chromium {lock["engine_version"]}.', flush=True)
        return False
    if candidate and validated(engine.engine_root(candidate), lock):
        adopt(candidate, root, lock)
        return True
    print('No candidate root validated this pin; moving and rebuilding this root.', flush=True)
    upgrade(root, lock, jobs, 'apply')
    return True


# --- upgrade and validate -------------------------------------------------------

@contextlib.contextmanager
def virtual_display(tools, log_directory):
    """An owned Xvfb (and Openbox when present) for browser tests. Never the
    owner's display."""
    xvfb = tools / 'xvfb/usr/bin/Xvfb'
    if not xvfb.is_file():
        engine.fail('No Xvfb in ' + str(tools))
    number = next((n for n in range(94, 200)
                   if not Path(f'/tmp/.X11-unix/X{n}').exists() and
                   not Path(f'/tmp/.X{n}-lock').exists()), None)
    if number is None:
        engine.fail('No free X display number')
    display = f':{number}'
    log_directory.mkdir(parents=True, exist_ok=True)
    processes = []
    try:
        with (log_directory / 'xvfb.log').open('wb') as log:
            processes.append(subprocess.Popen(
                [str(xvfb), display, '-screen', '0', '1920x1080x24', '-nolisten', 'tcp'],
                stdout=log, stderr=subprocess.STDOUT, start_new_session=True))
        for _ in range(300):
            if Path(f'/tmp/.X11-unix/X{number}').exists():
                break
            if processes[0].poll() is not None:
                engine.fail('Xvfb exited; see ' + str(log_directory / 'xvfb.log'))
            time.sleep(0.1)
        else:
            engine.fail('Xvfb did not start')
        env = dict(os.environ, DISPLAY=display)
        openbox = tools / 'openbox/usr/bin/openbox'
        if openbox.is_file():
            base = (tools / 'openbox').resolve()
            wm = dict(env, LD_LIBRARY_PATH=str(base / 'usr/lib'),
                      XDG_CONFIG_DIRS=str(base / 'etc/xdg'),
                      XDG_DATA_DIRS=str(base / 'usr/share') + ':/usr/share')
            with (log_directory / 'openbox.log').open('wb') as log:
                processes.append(subprocess.Popen([str(openbox)], env=wm, stdout=log,
                                                  stderr=subprocess.STDOUT,
                                                  start_new_session=True))
            time.sleep(2)
        yield display
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=20)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def step(root, script, command, jobs, log_directory, display=None, extra=()):
    args = [sys.executable, str(SCRIPTS / script), command, '--root', str(root), *extra]
    if command in ('build', 'test', 'test-browser'):
        args += ['--jobs', str(jobs)]
    env = dict(os.environ)
    if display:
        env['DISPLAY'] = display
    log = log_directory / f'{script.removesuffix(".py")}-{command}.log'
    print('+ ' + ' '.join(args), flush=True)
    with log.open('wb') as output:
        process = subprocess.Popen(args, env=env, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT)
        for raw in process.stdout:
            output.write(raw)
            output.flush()
            sys.stdout.buffer.write(raw)
            sys.stdout.flush()
        code = process.wait()
    if code:
        engine.fail(f'{script} {command} failed with exit code {code}; log: {log}')
    return {'command': f'{script} {command}', 'log': str(log)}


def overlay_current(root, lock):
    file = root / native.MANIFEST
    if not file.is_file():
        return False
    manifest = json.loads(file.read_text())
    return (manifest.get('upstream') == lock['chromium']['revision'] and
            manifest.get('phase') == 'applied' and manifest.get('kind') is None)


def upgrade_current(root, lock, through):
    """Whether `upgrade --through <through>` has nothing to do: the overlay
    applied is this checkout's (same fingerprint), the control is built for
    this pin and, for `build`, out/FlyNative was built from this overlay."""
    if not (overlay_current(root, lock) and stage_current(root, 'build')):
        return False
    expected = native.fingerprint()
    if json.loads((root / native.MANIFEST).read_text()).get('fingerprint') != expected:
        return False
    if through == 'apply':
        return True
    record = root / 'evidence/native-build.json'
    return (stage_current(root, 'native-build') and
            json.loads(record.read_text())['details'].get('fingerprint') == expected)


def upgrade(root, lock, jobs, through, tools=None, force=False):
    """move, control build, overlay apply; then Fly's build and tests as far as
    `through` asks. Holds the engine lease for the whole sequence."""
    root = engine.engine_root(root)
    token = os.environ.get(engine.LEASE_TOKEN) or secrets.token_hex(16)
    os.environ[engine.LEASE_TOKEN] = token
    started = datetime.datetime.now(datetime.timezone.utc).isoformat()
    logs = root / 'evidence/engine-update-logs' / time.strftime('%Y%m%d-%H%M%S')
    logs.mkdir(parents=True, exist_ok=True)
    steps = []
    with engine.exclusive(root):
        moved = move(root, lock, jobs)
        if not moved and not force and upgrade_current(root, lock, through):
            print('Nothing to upgrade.', flush=True)
            return {'moved': False, 'steps': []}
        if moved or not stage_current(root, 'build'):
            # The control is built from untouched sources.
            if (root / native.MANIFEST).exists():
                native.revert(root, lock)
            steps.append(step(root, 'engine.py', 'configure', jobs, logs))
            steps.append(step(root, 'engine.py', 'build', jobs, logs))
        if not overlay_current(root, lock):
            steps.append(step(root, 'native.py', 'apply', jobs, logs))
        else:
            steps.append(step(root, 'native.py', 'refresh-overlay', jobs, logs,
                              extra=['--include-upstream']))
        if through in ('build', 'tests'):
            steps.append(step(root, 'native.py', 'configure', jobs, logs))
            steps.append(step(root, 'native.py', 'build', jobs, logs))
        if through == 'tests':
            steps.append(step(root, 'native.py', 'test', jobs, logs))
            if tools:
                with virtual_display(tools, logs) as display:
                    steps.append(step(root, 'native.py', 'test-browser', jobs, logs,
                                      display=display))
            elif os.environ.get('DISPLAY'):
                steps.append(step(root, 'native.py', 'test-browser', jobs, logs))
            else:
                engine.fail('Browser tests need --test-tools or a DISPLAY')
            engine.atomic_json(root / VALIDATION, {
                'schema': 1, 'engine_version': lock['engine_version'],
                'lock_sha256': engine.digest(engine.LOCK),
                'fly_commit': engine.git(engine.REPO, 'rev-parse', 'HEAD'),
                'fly_dirty': bool(engine.git(engine.REPO, 'status', '--porcelain')),
                'started_at': started,
                'completed_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
                'moved': moved, 'steps': steps})
    return {'moved': moved, 'steps': steps}


# --- command line -------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=['status', 'pin', 'patch-check', 'seed', 'adopt',
                                            'follow', 'move', 'upgrade', 'validate'])
    parser.add_argument('--root', help='engine root (every command but status, pin, patch-check)')
    parser.add_argument('--from', dest='source', help='seed: the existing engine root to copy')
    parser.add_argument('--candidate', help='adopt, follow: the root validate ran in')
    parser.add_argument('--jobs', type=int, default=os.cpu_count() or 1)
    parser.add_argument('--channel', choices=['Stable', 'Beta'],
                        help='patch-check: the latest release of this channel '
                             'instead of the pinned revision')
    parser.add_argument('--through', choices=['apply', 'build'], default='build',
                        help='upgrade: stop after applying the overlay, or after building Fly')
    parser.add_argument('--test-tools', type=Path,
                        help='validate: directory with xvfb/ and openbox/ for an owned display')
    parser.add_argument('--force', action='store_true',
                        help='upgrade: rebuild even when the root is already current')
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.command in ('seed', 'adopt', 'follow', 'move', 'upgrade', 'validate') and not args.root:
        parser.error('--root is required for ' + args.command)
    if (args.command == 'seed') != bool(args.source):
        parser.error('--from belongs to seed, and seed requires it')
    if args.command == 'adopt' and not args.candidate:
        parser.error('adopt requires --candidate')
    try:
        lock = engine.read_lock()
        if args.command in ('status', 'pin'):
            latest = load_pin().resolve('Stable')
            result = status(lock, latest)
            print(json.dumps(result, indent=2))
            write_github_output(result)
            if args.command == 'pin':
                if result['newer']:
                    write_lock(latest)
                    print(f'Pinned Chromium {latest["engine_version"]}.')
                else:
                    print('The pin is already the latest Stable release.')
        elif args.command == 'patch-check':
            pin = load_pin()
            if args.channel:
                target = pin.resolve(args.channel)
                revision, label = target['chromium']['revision'], f'{args.channel} {target["engine_version"]}'
            else:
                revision, label = lock['chromium']['revision'], f'pinned {lock["engine_version"]}'
            print(f'Replaying {native.SERIES.parent.name}/series on Chromium {label} ({revision}).')
            failures = patch_check(revision, pin.fetch)
            write_github_output({'version': label.split()[-1], 'failed': len(failures),
                                 'patches': ' '.join(sorted(failures))})
            if failures:
                print(f'{len(failures)} patch(es) do not apply: ' + ', '.join(sorted(failures)))
                return 1
            print('Every patch applies.')
        elif args.command == 'seed':
            seed(args.source, args.root)
        elif args.command == 'adopt':
            os.environ.setdefault(engine.LEASE_TOKEN, secrets.token_hex(16))
            adopt(args.candidate, args.root, lock)
        elif args.command == 'follow':
            os.environ.setdefault(engine.LEASE_TOKEN, secrets.token_hex(16))
            write_github_output({'moved': follow(args.root, lock, args.jobs, args.candidate)})
        elif args.command == 'move':
            os.environ.setdefault(engine.LEASE_TOKEN, secrets.token_hex(16))
            root = engine.engine_root(args.root)
            with engine.exclusive(root):
                move(root, lock, args.jobs)
        else:
            through = 'tests' if args.command == 'validate' else args.through
            if args.command == 'validate' and args.test_tools and not args.test_tools.is_dir():
                parser.error('--test-tools is not a directory')
            result = upgrade(args.root, lock, args.jobs, through,
                             tools=args.test_tools.resolve() if args.test_tools else None,
                             force=args.force or args.command == 'validate')
            write_github_output({'moved': result['moved']})
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        print('Engine update: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
