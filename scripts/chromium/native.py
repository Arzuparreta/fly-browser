#!/usr/bin/env python3
"""Apply and build Fly's pinned native overlay in a dedicated engine checkout.

No source reset, dependency fetch, owner profile access, or production cutover.
The unmodified control must have built successfully before the first apply.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path, PurePosixPath
import subprocess
import sys
import tempfile
import time

import engine

OVERLAY = engine.REPO / 'chromium/overlay'
SERIES = engine.REPO / 'chromium/patches/series'
MANIFEST = 'evidence/native-overlay.json'
OUTPUT = 'out/FlyNative'
CONFIG = engine.REPO / 'chromium/gn/control.gnargs'


def relative_path(raw):
    path = PurePosixPath(raw)
    if (not raw or path.is_absolute() or '..' in path.parts or
            str(path) != raw or '\\' in raw or ':' in raw or
            any(part.startswith('.git') for part in path.parts)):
        engine.fail(f'Unsafe overlay path: {raw}')
    return path


def inputs():
    patches = []
    for line in SERIES.read_text().splitlines():
        name = line.strip()
        if not name or name.startswith('#'):
            continue
        relative_path(name)
        file = SERIES.parent / name
        if file.is_symlink() or not file.is_file():
            engine.fail(f'Missing or symlinked patch: {file}')
        if file in patches:
            engine.fail(f'Duplicate patch: {name}')
        patches.append(file)
    files = {}
    for file in sorted(OVERLAY.rglob('*')):
        if file.is_symlink():
            engine.fail(f'Symlinked overlay input: {file}')
        if file.is_file():
            name = file.relative_to(OVERLAY).as_posix()
            relative_path(name)
            if not name.startswith('fly/'):
                engine.fail('New overlay sources must be under //fly')
            files[name] = file
    if not patches or not files:
        engine.fail('Native source overlay/patch series is empty')
    return patches, files


def fingerprint():
    patches, files = inputs()
    records = {'lock': engine.digest(engine.LOCK), 'series': engine.digest(SERIES),
               'patches': {p.name: engine.digest(p) for p in patches},
               'overlay': {name: engine.digest(p) for name, p in files.items()}}
    return hashlib.sha256(json.dumps(records, sort_keys=True).encode()).hexdigest()


def checked_path(source, name):
    relative_path(name)
    file = source / name
    if file.resolve() != file.absolute():
        engine.fail(f'Symlinked engine source path: {name}')
    return file


def upstream_file(source, revision, name):
    return subprocess.run(['git', 'show', revision + ':' + name], cwd=source,
                          check=True, stdout=subprocess.PIPE).stdout


def bytes_digest(data):
    return hashlib.sha256(data).hexdigest()


def atomic_bytes(file, data):
    file.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(dir=file.parent, prefix=file.name + '.')
    try:
        with os.fdopen(fd, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, 0o644)
        os.replace(temporary, file)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def changed_paths(source):
    # Reject staging: this tooling never changes the source index.
    if engine.git(source, 'diff', '--cached', '--name-only'):
        engine.fail('Staged engine changes must be preserved and inspected')
    tracked = engine.git(source, 'diff', '--name-only', '-z', 'HEAD')
    untracked = engine.git(source, 'ls-files', '--others', '--exclude-standard', '-z')
    return set(filter(None, (tracked + '\0' + untracked).split('\0')))


def read_manifest(root, lock, allow_pending=False):
    file = root / MANIFEST
    if not file.is_file():
        engine.fail('No managed native overlay; run apply first')
    manifest = json.loads(file.read_text())
    if manifest.get('schema') != 1 or manifest.get('upstream') != lock['chromium']['revision']:
        engine.fail('Native manifest schema/upstream differs; preserve it for recovery')
    if manifest.get('phase') not in ('applied', 'applying', 'reverting'):
        engine.fail('Unknown native overlay transaction phase')
    if not allow_pending and manifest.get('phase') != 'applied':
        engine.fail('Interrupted overlay operation: inspect native-overlay.json before recovery')
    for name, record in manifest['files'].items():
        checked_path(root / 'src', name)
        if record['before'] is None:
            if not name.startswith('fly/'):
                engine.fail('New native source outside //fly')
            tracked = engine.git(root / 'src', 'ls-files', '--', name)
            if tracked:
                engine.fail('Overlay manifest would remove an upstream tracked file')
        elif bytes_digest(upstream_file(root / 'src', manifest['upstream'], name)) != record['before']:
            engine.fail('Overlay manifest original differs from upstream: ' + name)
    return manifest


def verify_source(root, lock, require_current=True):
    source = root / 'src'
    manifest = read_manifest(root, lock)
    if engine.git(source, 'remote', 'get-url', 'origin') != lock['chromium']['url']:
        engine.fail('Native engine origin differs')
    if engine.git(source, 'rev-parse', 'HEAD') != lock['chromium']['revision']:
        engine.fail('Native engine HEAD differs; no reset is permitted')
    if require_current and manifest['fingerprint'] != fingerprint():
        engine.fail('Fly overlay inputs changed; revert the managed overlay, then reapply')
    expected_changed = set()
    for name, record in manifest['files'].items():
        file = checked_path(source, name)
        if not file.is_file() or engine.digest(file) != record['after']:
            engine.fail(f'Managed native source was edited: {name}; preserve edits')
        if record['before'] != record['after']:
            expected_changed.add(name)
    actual = changed_paths(source)
    if actual != expected_changed:
        engine.fail('Unmanaged engine changes: ' + repr(sorted(actual ^ expected_changed)))
    return manifest


def apply_core(root, lock):
    """Install only new //fly sources for service tests, without browser hooks."""
    if (root / MANIFEST).exists():
        manifest = verify_source(root, lock)
        if manifest.get('kind') != 'core':
            engine.fail('Full overlay present; use the normal native test command')
        return
    engine.verify_checkout(root / 'src', lock['chromium'])
    _, overlay = inputs()
    manifest = {'schema': 1, 'phase': 'applying', 'kind': 'core',
                'upstream': lock['chromium']['revision'],
                'fingerprint': fingerprint(), 'files': {}}
    for name, file in overlay.items():
        if checked_path(root / 'src', name).exists():
            engine.fail('Core overlay destination exists: ' + name)
        manifest['files'][name] = {'before': None, 'after': engine.digest(file)}
    engine.atomic_json(root / MANIFEST, manifest)
    for name, file in overlay.items():
        atomic_bytes(root / 'src' / name, file.read_bytes())
    manifest['phase'] = 'applied'
    engine.atomic_json(root / MANIFEST, manifest)
    verify_source(root, lock)


def apply(root, lock, objects_only=False):
    if (root / MANIFEST).exists():
        existing = verify_source(root, lock)
        if existing.get('kind') == 'core':
            engine.fail('Core-only overlay present; revert before applying browser hooks')
        if existing.get('kind') == 'objects' and not objects_only:
            engine.fail('Compile-only overlay present; revert and apply after the control build')
        print('Native overlay already matches the reviewed inputs.')
        return
    engine.verify_checkout(root / 'src', lock['chromium'])
    if objects_only:
        engine.require_stage(root, 'hooks', lock)
    else:
        engine.require_control(root, lock, CONFIG)
    patches, overlay = inputs()
    touched = set()
    for patch in patches:
        old = []
        new = []
        for line in patch.read_text().splitlines():
            if line.startswith('--- '):
                old.append(line.removeprefix('--- a/'))
            elif line.startswith('+++ '):
                new.append(line.removeprefix('+++ b/'))
        if not old or old != new:
            engine.fail('Integration patches must modify existing upstream files only')
        for name in old:
            relative_path(name)
            if name.startswith('fly/'):
                engine.fail('//fly sources belong to the overlay, not upstream patches')
            touched.add(name)
    manifest = {'schema': 1, 'phase': 'applying',
                'upstream': lock['chromium']['revision'],
                'fingerprint': fingerprint(), 'files': {}}
    if objects_only:
        manifest['kind'] = 'objects'
    # Exercise the entire ordered stack before touching the real source. This
    # catches a late failed hunk without leaving an earlier patch installed.
    with tempfile.TemporaryDirectory(prefix='fly-overlay-', dir=root) as tmp:
        stage = Path(tmp)
        engine.run(['git', 'init', '--quiet'], stage)
        for name in sorted(touched):
            original = upstream_file(root / 'src', manifest['upstream'], name)
            atomic_bytes(stage / name, original)
            manifest['files'][name] = {'before': bytes_digest(original)}
        for patch in patches:
            engine.run(['git', 'apply', '--check', '--whitespace=error-all', patch], stage)
            engine.run(['git', 'apply', '--whitespace=error-all', patch], stage)
        for name, file in overlay.items():
            if checked_path(root / 'src', name).exists():
                engine.fail(f'Overlay destination already exists: {name}')
            atomic_bytes(stage / name, file.read_bytes())
            manifest['files'][name] = {'before': None}
        for name, record in manifest['files'].items():
            checked_path(root / 'src', name)
            record['after'] = engine.digest(stage / name)
        engine.atomic_json(root / MANIFEST, manifest)
        for name in sorted(manifest['files']):
            atomic_bytes(root / 'src' / name, (stage / name).read_bytes())
    manifest['phase'] = 'applied'
    engine.atomic_json(root / MANIFEST, manifest)
    verify_source(root, lock)


def revert(root, lock, recover=False):
    if recover:
        manifest = read_manifest(root, lock, allow_pending=True)
        source = root / 'src'
        if (engine.git(source, 'rev-parse', 'HEAD') != lock['chromium']['revision'] or
                engine.git(source, 'remote', 'get-url', 'origin') != lock['chromium']['url']):
            engine.fail('Engine identity changed during overlay transaction')
        if not changed_paths(source).issubset(manifest['files']):
            engine.fail('Unmanaged source changes during overlay transaction')
        for name, record in manifest['files'].items():
            file = checked_path(source, name)
            current = engine.digest(file) if file.is_file() else None
            if current not in (record['before'], record['after']):
                engine.fail('Edited transaction file must be preserved: ' + name)
    else:
        manifest = verify_source(root, lock, require_current=False)
    # Verify every original before making the first write.
    originals = {}
    for name, record in manifest['files'].items():
        if record['before'] is not None:
            originals[name] = upstream_file(root / 'src', manifest['upstream'], name)
            if bytes_digest(originals[name]) != record['before']:
                engine.fail(f'Original content does not match manifest: {name}')
    manifest['phase'] = 'reverting'
    engine.atomic_json(root / MANIFEST, manifest)
    for name, record in manifest['files'].items():
        file = checked_path(root / 'src', name)
        if name in originals:
            atomic_bytes(file, originals[name])
        else:
            file.unlink(missing_ok=True)
            parent = file.parent
            while parent.exists() and parent != root / 'src' and not any(parent.iterdir()):
                parent.rmdir()
                parent = parent.parent
    engine.verify_checkout(root / 'src', lock['chromium'])
    (root / MANIFEST).unlink()


def object_targets(output, manifest, platform):
    """Resolve actual GN compiler edges, retaining GN-generated prerequisites."""
    wanted = {name for name in manifest['files']
              if name.endswith(('.cc', '.mm', '.c', '.m'))}
    found = {name: [] for name in wanted}
    source_names = {name.encode('utf-8'): name for name in wanted}
    for file in output.rglob('*.ninja'):
        # GN variables may carry non-UTF8 bytes from third-party metadata.
        # Decode only matching path tokens, not unrelated command variables.
        for line in file.read_bytes().splitlines():
            edge = re.match(rb'^build ([^ ]+): (?:cxx|cc|objcxx|objc) ../../([^ ]+)(?: |$)', line)
            if edge and edge[2] in source_names:
                target = edge[1].decode('utf-8')
                relative_path(target)
                found[source_names[edge[2]]].append(target)
    missing = sorted(name for name, targets in found.items() if not targets)
    skipped = [name for name in missing if platform == 'darwin' and
               name.startswith(('fly/ui/', 'fly/test/'))]
    unexpected = set(missing) - set(skipped)
    if unexpected:
        engine.fail('No GN compiler edge for native sources: ' + repr(sorted(unexpected)))
    targets = {}
    for name, candidates in found.items():
        if candidates:
            # The scoped runner and upstream browser_tests share the test TU.
            # Prefer the scoped runner; never build the upstream test catalogue.
            targets[name] = min(candidates, key=lambda target:
                                (not target.startswith('obj/fly/'), len(target), target))
    if not targets:
        engine.fail('No native compiler targets selected')
    return targets, skipped


def compile_objects(root, lock, manifest, env, jobs):
    relative = 'out/FlyObjects'
    output = root / 'src' / relative
    output.mkdir(parents=True, exist_ok=True)
    config = output / 'args.gn'
    if config.exists() and config.read_bytes() != CONFIG.read_bytes():
        engine.fail('Object build arguments differ; preserve existing output')
    config.write_bytes(CONFIG.read_bytes())
    engine.run(['gn', 'gen', relative], root / 'src', env)
    engine.run(['gn', 'check', relative, '//fly:*'], root / 'src', env)
    engine.run(['gn', 'check', relative, '//chrome/browser/ui:ui'], root / 'src', env)
    targets, skipped = object_targets(output, manifest, sys.platform)
    details = {'fingerprint': manifest['fingerprint'], 'objects': targets,
               'platform_excluded_sources': skipped,
               'native_desktop_supported': sys.platform in ('linux', 'win32'),
               'acceptance': 'Object compilation only; no linked browser or runtime acceptance'}
    engine.atomic_json(root / 'evidence/native-object-plan.json', details)
    engine.run(['autoninja', '-C', relative, '-j', str(jobs), *sorted(targets.values())],
               root / 'src', env)
    verify_source(root, lock)
    engine.evidence(root, 'native-test-objects', lock, details)


def verify_browser_results(summary, source):
    # PRE success is folded into its matching main result by Chromium's test
    # launcher. A failed PRE propagates to that main result. See upstream
    # base/test/launcher/test_results_tracker.cc::AddTestResult.
    expected = {fixture + '.' + name for fixture, name in re.findall(
        r'IN_PROC_BROWSER_TEST_F\(\s*(Fly\w*BrowserTest),\s*(\w+)\s*\)',
        source.read_text()) if not name.startswith('PRE_')}
    if not expected:
        engine.fail('No native browser cases declared in the reviewed source')
    report = json.loads(summary.read_text())
    iterations = report.get('per_iteration_data', [])
    if not iterations:
        engine.fail('Native browser summary contains no test iterations')
    for iteration in iterations:
        for name in sorted(expected):
            attempts = iteration.get(name, [])
            if not attempts or attempts[-1].get('status') != 'SUCCESS':
                engine.fail('Native browser case was missing or unsuccessful: ' + name)
    return {'main_cases_per_iteration': len(expected),
            'iterations': len(iterations), 'summary_sha256': engine.digest(summary)}


def execute(args):
    lock = engine.read_lock()
    root = engine.engine_root(args.root)
    with engine.exclusive(root):
        engine.verify_checkout(root / 'depot_tools', lock['depot_tools'])
        if args.command == 'test-core':
            engine.require_stage(root, 'hooks', lock)
            apply_core(root, lock)
        if args.command == 'test-objects':
            engine.doctor(root, args.minimum_free_gib)
            apply(root, lock, objects_only=True)
        if args.command == 'apply':
            # The control build verifies all dependency revisions. This command
            # only touches source files after its successful completion.
            apply(root, lock)
            return
        if args.command in ('revert', 'recover'):
            revert(root, lock, recover=args.command == 'recover')
            return
        manifest = verify_source(root, lock)
        engine.verify_dependencies(root, source_verifier=lambda: verify_source(root, lock))
        env = engine.environment(root)
        output = root / 'src' / OUTPUT
        if args.command in ('configure', 'build', 'test', 'test-browser'):
            if manifest.get('kind') in ('core', 'objects'):
                engine.fail('A full overlay after a matching control build is required')
            engine.require_control(root, lock, CONFIG)
        if args.command == 'test-objects':
            compile_objects(root, lock, manifest, env, args.jobs)
        elif args.command == 'test-core':
            # Service tests need no patched browser or claim of a complete
            # control build. Root-target keeps the real Chromium dependency
            # graph, toolchain, test launcher and platform implementations.
            unit_output = root / 'src/out/FlyUnit'
            unit_output.mkdir(parents=True, exist_ok=True)
            config = unit_output / 'args.gn'
            if config.exists() and config.read_bytes() != CONFIG.read_bytes():
                engine.fail('Core test build arguments differ; preserve existing output')
            config.write_bytes(CONFIG.read_bytes())
            engine.run(['gn', 'gen', 'out/FlyUnit', '--root-target=//fly'], root / 'src', env)
            engine.run(['autoninja', '-C', 'out/FlyUnit', '-j', str(args.jobs), 'fly_unittests'], root / 'src', env)
            executable = unit_output / ('fly_unittests.exe' if sys.platform == 'win32' else 'fly_unittests')
            engine.run([executable, '--test-launcher-jobs=2',
                        '--test-launcher-summary-output=' + str(root / 'evidence/native-core-tests.json')], root / 'src', env, foreground=True)
            verify_source(root, lock)
            engine.evidence(root, 'native-test-core', lock, {'fingerprint': manifest['fingerprint']})
        elif args.command == 'verify':
            print('Native source, overlay fingerprint and pinned dependencies match.')
        elif args.command == 'configure':
            engine.require_stage(root, 'hooks', lock)
            output.mkdir(parents=True, exist_ok=True)
            config = output / 'args.gn'
            if config.exists() and config.read_bytes() != CONFIG.read_bytes():
                engine.fail('Native build arguments differ; preserve the existing output')
            config.write_bytes(CONFIG.read_bytes())
            engine.run(['gn', 'gen', OUTPUT], root / 'src', env)
            engine.evidence(root, 'native-configure', lock,
                            {'fingerprint': manifest['fingerprint'], 'args_sha256': engine.digest(config)})
        elif args.command in ('build', 'test', 'test-browser'):
            engine.doctor(root, args.minimum_free_gib)
            engine.require_stage(root, 'native-configure', lock)
            configured = json.loads((root / 'evidence/native-configure.json').read_text())['details']
            if configured['fingerprint'] != manifest['fingerprint'] or engine.digest(output / 'args.gn') != configured['args_sha256']:
                engine.fail('Native inputs differ from configuration; configure again')
            target = {'build': 'chrome', 'test': 'fly_unittests',
                      'test-browser': args.browser_target}[args.command]
            engine.run(['autoninja', '-C', OUTPUT, '-j', str(args.jobs), target], root / 'src', env)
            if args.command == 'test':
                engine.run([output / ('fly_unittests.exe' if sys.platform == 'win32' else 'fly_unittests')], root / 'src', env, foreground=True)
            if args.command == 'test-browser':
                executable = output / (target + ('.exe' if sys.platform == 'win32' else ''))
                summary = root / 'evidence/native-browser-tests.json'
                if summary.exists():
                    summary.rename(summary.with_name(
                        'native-browser-tests.previous-' + str(time.time_ns()) + '.json'))
                engine.run([executable, '--gtest_filter=Fly*BrowserTest.*',
                            '--test-launcher-jobs=1',
                            '--test-launcher-summary-output=' + str(summary)], root / 'src', env, foreground=True)
                browser_results = verify_browser_results(
                    summary, OVERLAY / 'fly/test/workspace_browser_test.cc')
            verify_source(root, lock)
            details = {'fingerprint': manifest['fingerprint'], 'target': target}
            if args.command == 'test-browser':
                details.update(browser_results)
            if args.command == 'build':
                executable = (output / 'Chromium.app/Contents/MacOS/Chromium'
                              if sys.platform == 'darwin' else
                              output / ('chrome.exe' if sys.platform == 'win32' else 'chrome'))
                details.update({'binary': str(executable.resolve()),
                                'sha256': engine.digest(executable),
                                'args_sha256': configured['args_sha256'],
                                'native_desktop_supported': sys.platform in ('linux', 'win32')})
            engine.evidence(root, 'native-' + args.command, lock, details)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['apply', 'verify', 'configure', 'build', 'test', 'test-browser', 'test-core', 'test-objects', 'revert', 'recover'])
    parser.add_argument('--root', required=True)
    parser.add_argument('--browser-target', choices=['fly_browser_tests', 'browser_tests'],
                        default='fly_browser_tests',
                        help='Scoped Fly acceptance runner or the complete upstream test binary')
    parser.add_argument('--minimum-free-gib', type=int, default=100,
                        help='explicit free-space floor for native compilation')
    parser.add_argument('--jobs', type=int, default=min(8, os.cpu_count() or 1))
    args = parser.parse_args()
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    if args.minimum_free_gib < 40:
        parser.error('--minimum-free-gib must be at least 40')
    try:
        execute(args)
    except (RuntimeError, OSError, subprocess.CalledProcessError, ValueError, KeyError) as error:
        print('Fly native: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
