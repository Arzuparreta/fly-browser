"""Engine update tooling: pin comparison, patch replay, moving and copying
roots. Only tiny local Git trees; gclient is simulated and nothing is fetched."""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/chromium'))
import engine
import engine_update
import native

SRC_URL = 'https://chromium.googlesource.com/chromium/src.git'
TOOLS_URL = 'https://chromium.googlesource.com/chromium/tools/depot_tools.git'


def git(directory, *args):
    return subprocess.run(['git', *args], cwd=directory, check=True,
                          capture_output=True, text=True).stdout.strip()


def commit(directory, files, message):
    for name, text in files.items():
        file = directory / name
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(text)
    git(directory, 'add', '-A')
    git(directory, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
        'commit', '--quiet', '-m', message)
    return git(directory, 'rev-parse', 'HEAD')


def lock_for(source_rev, deps_text, tools_rev, version):
    import hashlib
    return {'schema': 1, 'engine_version': version,
            'chromium': {'url': SRC_URL, 'revision': source_rev,
                         'deps_sha256': hashlib.sha256(deps_text.encode()).hexdigest()},
            'depot_tools': {'url': TOOLS_URL, 'revision': tools_rev}}


class VersionTests(unittest.TestCase):
    def test_status_compares_numerically(self):
        result = engine_update.status({'engine_version': '155.0.8059.84'},
                                      {'engine_version': '155.0.8059.100'})
        self.assertTrue(result['newer'])
        self.assertFalse(result['new_major'])
        result = engine_update.status({'engine_version': '156.0.8078.79'},
                                      {'engine_version': '155.0.9999.999'})
        self.assertFalse(result['newer'])
        self.assertTrue(engine_update.status({'engine_version': '155.0.1.2'},
                                             {'engine_version': '156.0.0.1'})['new_major'])

    def test_rejects_malformed_versions_and_locks(self):
        with self.assertRaisesRegex(RuntimeError, 'Invalid Chromium version'):
            engine_update.version('156.0.8078')
        with self.assertRaisesRegex(RuntimeError, 'Unsupported upstream lock schema'):
            engine.validate_lock({'schema': 2})
        lock = lock_for('a' * 40, 'deps', 'b' * 40, '1.2.3.4')
        lock['chromium']['url'] = 'https://example.invalid/src.git'
        with self.assertRaisesRegex(RuntimeError, 'Invalid official source pin'):
            engine.validate_lock(lock)


class LeaseTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-lease-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.env = patch.dict(os.environ)
        self.env.start()
        self.addCleanup(self.env.stop)
        os.environ.pop(engine.LEASE_TOKEN, None)

    def test_children_with_the_token_share_the_lease_others_are_refused(self):
        os.environ[engine.LEASE_TOKEN] = 'sequence'
        with engine.exclusive(self.root):
            with engine.exclusive(self.root):
                pass
            self.assertTrue((self.root / '.fly-engine-operation').is_dir())
            os.environ[engine.LEASE_TOKEN] = 'someone-else'
            with self.assertRaisesRegex(RuntimeError, 'Another engine command'):
                with engine.exclusive(self.root):
                    pass
            del os.environ[engine.LEASE_TOKEN]
            with self.assertRaisesRegex(RuntimeError, 'Another engine command'):
                with engine.exclusive(self.root):
                    pass
            os.environ[engine.LEASE_TOKEN] = 'sequence'
        self.assertFalse((self.root / '.fly-engine-operation').exists())


class PatchCheckTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-patch-check-test-')
        self.addCleanup(tmp.cleanup)
        product = Path(tmp.name)
        (product / 'overlay/fly').mkdir(parents=True)
        (product / 'overlay/fly/native.cc').write_text('native\n')
        patches = product / 'patches'
        patches.mkdir()
        (patches / 'series').write_text('first.patch\nsecond.patch\n')
        (patches / 'first.patch').write_text(
            '--- a/a.cc\n+++ b/a.cc\n@@ -1,2 +1,2 @@\n one\n-two\n+TWO\n')
        (patches / 'second.patch').write_text(
            '--- a/b.cc\n+++ b/b.cc\n@@ -1 +1 @@\n-three\n+THREE\n')
        scope = patch.multiple(native, OVERLAY=product / 'overlay', SERIES=patches / 'series')
        scope.start()
        self.addCleanup(scope.stop)

    def fetch(self, files):
        import base64

        def fetch(url):
            name = url.split('/+/', 1)[1].split('/', 1)[1].removesuffix('?format=TEXT')
            return base64.b64encode(files[name].encode())
        return fetch

    def test_reports_each_failing_patch_and_tries_the_rest(self):
        failures = engine_update.patch_check(
            'c' * 40, self.fetch({'a.cc': 'one\nchanged\n', 'b.cc': 'three\n'}), log=lambda _: None)
        self.assertEqual(list(failures), ['first.patch'])
        self.assertEqual(engine_update.patch_check(
            'c' * 40, self.fetch({'a.cc': 'one\ntwo\n', 'b.cc': 'three\n'}), log=lambda _: None), {})

    def test_retries_gitiles_overload_then_gives_up(self):
        import urllib.error
        calls = []

        def busy(url):
            calls.append(url)
            raise urllib.error.HTTPError(url, 503, 'busy', {}, io.BytesIO())
        with patch.object(engine_update.time, 'sleep'):
            with self.assertRaisesRegex(ValueError, 'Gitiles 503'):
                engine_update.gitiles_file('c' * 40, 'a.cc', busy, attempts=3)
        self.assertEqual(len(calls), 3)


class FetchDependenciesTests(unittest.TestCase):
    """Only the missing new revision of each dependency is fetched, shallow,
    from a local origin; gclient revinfo is simulated."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-fetch-')
        self.addCleanup(tmp.cleanup)
        base = Path(tmp.name).resolve()
        self.origin = base / 'origin'
        self.origin.mkdir()
        git(self.origin, 'init', '--quiet')
        self.old_rev = commit(self.origin, {'file': 'old\n'}, 'old')
        self.root = base / 'engine'
        self.dependency = self.root / 'src/third_party/dependency'
        self.dependency.parent.mkdir(parents=True)
        git(base, 'clone', '--quiet', '--depth=1', self.origin.as_uri(), str(self.dependency))
        commit(self.origin, {'file': 'history\n'}, 'history')
        self.new_rev = commit(self.origin, {'file': 'new\n'}, 'new')
        self.inventory = {'src': {'url': SRC_URL, 'rev': 'a' * 40},
                          'src/third_party/dependency': {'url': self.origin.as_uri() + '.git',
                                                         'rev': self.new_rev},
                          'src/third_party/cipd': {'url': None, 'rev': None}}
        self.fetches = []
        real = engine.run

        def run(args, cwd, env=None, capture=False, foreground=False):
            args = [str(arg) for arg in args]
            if args[:2] == ['gclient', 'revinfo']:
                Path(args[-1]).write_text(json.dumps(self.inventory))
                return ''
            if args[:2] == ['git', 'fetch']:
                self.fetches.append(args)
                if len(self.fetches) == 1:
                    raise subprocess.CalledProcessError(128, args)  # HTTP 429
            return real(args, cwd, env, capture, foreground)
        scope = patch.object(engine, 'run', side_effect=run)
        scope.start()
        self.addCleanup(scope.stop)

    def test_fetches_only_the_missing_revision_shallow_and_retries(self):
        with patch.object(engine_update.time, 'sleep') as sleep:
            self.assertEqual(engine_update.fetch_dependencies(self.root, None), 1)
        sleep.assert_called_once()
        self.assertEqual(self.fetches[-1], ['git', 'fetch', '--depth=1', '--no-tags', 'origin',
                                            self.new_rev])
        git(self.dependency, 'cat-file', '-e', self.new_rev + '^{commit}')
        # Only the new commit came, not the history between the two pins.
        self.assertEqual(git(self.dependency, 'rev-list', '--count', self.new_rev), '1')
        self.fetches.clear()
        self.assertEqual(engine_update.fetch_dependencies(self.root, None), 0)
        self.assertEqual(self.fetches, [])


class NestedDependenciesTests(unittest.TestCase):
    """A nested dependency's revision is read from its parent's DEPS, as
    gclient does: the parent is checked out at its new revision first, then
    the child's new revision is fetched alone."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-nested-')
        self.addCleanup(tmp.cleanup)
        base = Path(tmp.name).resolve()
        self.root = base / 'engine'
        (self.root / 'src/third_party').mkdir(parents=True)
        self.child_origin = base / 'child'
        self.child_origin.mkdir()
        git(self.child_origin, 'init', '--quiet')
        child_old = commit(self.child_origin, {'file': 'old\n'}, 'old')
        self.parent_origin = base / 'parent'
        self.parent_origin.mkdir()
        git(self.parent_origin, 'init', '--quiet')
        commit(self.parent_origin, {'DEPS': child_old + '\n'}, 'old')
        self.parent = self.root / 'src/third_party/parent'
        self.child = self.parent / 'child'
        git(base, 'clone', '--quiet', '--depth=1', self.parent_origin.as_uri(), str(self.parent))
        git(base, 'clone', '--quiet', '--depth=1', self.child_origin.as_uri(), str(self.child))
        commit(self.child_origin, {'file': 'history\n'}, 'history')
        self.child_new = commit(self.child_origin, {'file': 'new\n'}, 'new')
        self.parent_new = commit(self.parent_origin, {'DEPS': self.child_new + '\n'}, 'new')
        real = engine.run

        def run(args, cwd, env=None, capture=False, foreground=False):
            args = [str(arg) for arg in args]
            if args[:2] == ['gclient', 'revinfo']:
                # gclient reads the child's pin from the parent's checkout.
                Path(args[-1]).write_text(json.dumps({
                    'src/third_party/parent': {'url': self.parent_origin.as_uri(),
                                               'rev': self.parent_new},
                    'src/third_party/parent/child': {
                        'url': self.child_origin.as_uri(),
                        'rev': (self.parent / 'DEPS').read_text().strip()}}))
                return ''
            return real(args, cwd, env, capture, foreground)
        scope = patch.object(engine, 'run', side_effect=run)
        scope.start()
        self.addCleanup(scope.stop)

    def test_fetches_the_child_revision_its_parent_moves_to(self):
        self.assertEqual(engine_update.fetch_dependencies(self.root, None), 2)
        self.assertEqual(git(self.parent, 'rev-parse', 'HEAD'), self.parent_new)
        git(self.child, 'cat-file', '-e', self.child_new + '^{commit}')
        self.assertEqual(git(self.child, 'rev-list', '--count', self.child_new), '1')


class MoveTests(unittest.TestCase):
    """A root at an old pin moves to a new one; the new revision is already
    in the object store, so nothing is fetched."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-move-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name).resolve() / 'engine'
        self.root.mkdir()
        tools = self.root / 'depot_tools'
        tools.mkdir()
        git(tools, 'init', '--quiet')
        git(tools, 'remote', 'add', 'origin', TOOLS_URL)
        self.tools_rev = commit(tools, {'gclient': 'tools\n'}, 'tools')
        self.src = self.root / 'src'
        self.src.mkdir()
        git(self.src, 'init', '--quiet')
        git(self.src, 'remote', 'add', 'origin', SRC_URL)
        (self.src / '.git/info/exclude').write_text('out/\n')
        self.old_rev = commit(self.src, {'DEPS': 'old deps\n', 'upstream.cc': 'old\n'}, 'old')
        self.new_rev = commit(self.src, {'DEPS': 'new deps\n', 'upstream.cc': 'new\n'}, 'new')
        git(self.src, 'checkout', '--quiet', '--detach', self.old_rev)
        self.old = lock_for(self.old_rev, 'old deps\n', self.tools_rev, '155.0.1.1')
        self.new = lock_for(self.new_rev, 'new deps\n', self.tools_rev, '156.0.1.1')
        self.lock_file = Path(tmp.name) / 'upstream.lock.json'
        self.lock_file.write_text(json.dumps(self.old))
        scopes = [patch.object(engine, 'LOCK', self.lock_file),
                  patch.object(engine, 'doctor'),
                  patch.object(engine, 'run', side_effect=self.run_command)]
        for scope in scopes:
            scope.start()
            self.addCleanup(scope.stop)
        self.commands = []
        self.write_inventory(self.old_rev)
        engine.evidence(self.root, 'sync', self.old, {
            'dependency_revisions_sha256': engine.digest(self.root / 'evidence/dependency-revisions.json')})
        engine.evidence(self.root, 'hooks', self.old)
        self.lock_file.write_text(json.dumps(self.new))

    def write_inventory(self, revision, root=None):
        file = (root or self.root) / 'evidence/dependency-revisions.json'
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(json.dumps({'src': {'url': SRC_URL, 'rev': revision}}))

    def run_command(self, args, cwd, env=None, capture=False, foreground=False):
        args = [str(arg) for arg in args]
        if args[0] == 'git':
            if args[1] == 'fetch':
                raise AssertionError('move fetched a revision that was already present')
            return subprocess.run(args, cwd=cwd, check=True, text=True,
                                  capture_output=True).stdout.strip()
        self.commands.append(args[:2])
        if args[:2] == ['gclient', 'revinfo']:
            Path(args[-1]).write_text(json.dumps({'src': {'url': SRC_URL, 'rev': self.new_rev}}))
        return ''

    def test_moves_syncs_and_runs_hooks_once(self):
        self.assertTrue(engine_update.move(self.root, self.new, jobs=1))
        self.assertEqual(git(self.src, 'rev-parse', 'HEAD'), self.new_rev)
        self.assertEqual(self.commands, [['gclient', 'revinfo'], ['gclient', 'sync'],
                                         ['gclient', 'revinfo'], ['gclient', 'runhooks']])
        self.assertEqual(engine_update.recorded_lock(self.root)['engine_version'], '156.0.1.1')
        self.assertFalse((self.root / engine_update.MOVE_STATE).exists())
        self.commands.clear()
        self.assertFalse(engine_update.move(self.root, self.new, jobs=1))
        self.assertEqual(self.commands, [])

    def test_resumes_after_an_interrupted_sync(self):
        failing = {'done': False}
        original = self.run_command

        def interrupted(args, cwd, env=None, capture=False, foreground=False):
            if str(args[0]) == 'gclient' and str(args[1]) == 'sync' and not failing['done']:
                failing['done'] = True
                raise subprocess.CalledProcessError(1, args)
            return original(args, cwd, env, capture, foreground)
        engine.run.side_effect = interrupted
        with self.assertRaises(subprocess.CalledProcessError):
            engine_update.move(self.root, self.new, jobs=1)
        self.assertTrue((self.root / engine_update.MOVE_STATE).exists())
        self.assertTrue(engine_update.move(self.root, self.new, jobs=1))
        self.assertFalse((self.root / engine_update.MOVE_STATE).exists())

    def test_refuses_unexplained_source_changes(self):
        (self.src / 'upstream.cc').write_text('local edit\n')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine_update.move(self.root, self.new, jobs=1)
        self.assertEqual(git(self.src, 'rev-parse', 'HEAD'), self.old_rev)

    def overlay(self, tmp):
        overlay = Path(tmp) / 'overlay'
        (overlay / 'fly').mkdir(parents=True)
        (overlay / 'fly/native.cc').write_text('native\n')
        series = Path(tmp) / 'patches/series'
        series.parent.mkdir()
        series.write_text('first.patch\n')
        (series.parent / 'first.patch').write_text(
            '--- a/upstream.cc\n+++ b/upstream.cc\n@@ -1 +1 @@\n-old\n+adapted\n')
        return patch.multiple(native, OVERLAY=overlay, SERIES=series)

    @unittest.skipUnless(shutil.which('rsync'), 'rsync is not installed')
    def test_adopts_a_candidate_validated_for_the_pin(self):
        candidate = self.root.parent / 'candidate'
        candidate.mkdir()
        engine_update.copy_root(self.root, candidate, touch=False)
        self.assertTrue(engine_update.move(candidate, self.new, jobs=1))
        with tempfile.TemporaryDirectory() as tmp, self.overlay(tmp):
            native.apply_core(candidate, self.new)
            with self.assertRaisesRegex(RuntimeError, 'has not passed validate'):
                engine_update.adopt(candidate, self.root, self.new)
            engine.atomic_json(candidate / engine_update.VALIDATION,
                               {'lock_sha256': engine.digest(engine.LOCK)})
            self.assertTrue(engine_update.follow(self.root, self.new, jobs=1, candidate=candidate))
            native.verify_source(self.root, self.new, require_current=False)
            self.assertFalse(engine_update.follow(self.root, self.new, jobs=1, candidate=candidate))
        self.assertEqual(git(self.src, 'rev-parse', 'HEAD'), self.new_rev)
        self.assertEqual((self.src / 'fly/native.cc').read_text(), 'native\n')
        self.assertEqual(engine_update.recorded_lock(self.root)['engine_version'], '156.0.1.1')

    def test_reverts_the_managed_overlay_before_moving(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.overlay(tmp):
                native.apply_core(self.root, self.old)
                self.assertTrue((self.src / 'fly/native.cc').exists())
                self.assertTrue(engine_update.move(self.root, self.new, jobs=1))
        self.assertFalse((self.src / 'fly').exists())
        self.assertFalse((self.root / native.MANIFEST).exists())
        self.assertEqual((self.src / 'upstream.cc').read_text(), 'new\n')


class UpgradeCurrentTests(unittest.TestCase):
    """upgrade only skips work when the applied overlay and the built browser
    both come from this checkout's inputs, not merely from the same pin."""

    def setUp(self):
        tmp = tempfile.TemporaryDirectory(prefix='fly-upgrade-current-')
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.lock = lock_for('a' * 40, 'deps', 'b' * 40, '156.0.1.1')
        lock_file = self.root / 'upstream.lock.json'
        lock_file.write_text(json.dumps(self.lock))
        for scope in (patch.object(engine, 'LOCK', lock_file),
                      patch.object(native, 'fingerprint', return_value='inputs')):
            scope.start()
            self.addCleanup(scope.stop)
        engine.evidence(self.root, 'build', self.lock)

    def record(self, applied, built):
        engine.atomic_json(self.root / native.MANIFEST, {
            'schema': 1, 'phase': 'applied', 'upstream': 'a' * 40,
            'fingerprint': applied, 'files': {}})
        engine.evidence(self.root, 'native-build', self.lock, {'fingerprint': built})

    def test_changed_overlay_inputs_are_not_current(self):
        self.record('inputs', 'inputs')
        self.assertTrue(engine_update.upgrade_current(self.root, self.lock, 'build'))
        self.record('older', 'older')
        self.assertFalse(engine_update.upgrade_current(self.root, self.lock, 'apply'))
        self.assertFalse(engine_update.upgrade_current(self.root, self.lock, 'build'))
        self.record('inputs', 'older')
        self.assertTrue(engine_update.upgrade_current(self.root, self.lock, 'apply'))
        self.assertFalse(engine_update.upgrade_current(self.root, self.lock, 'build'))


@unittest.skipUnless(shutil.which('rsync'), 'rsync is not installed')
class CopyTests(unittest.TestCase):
    def test_copies_sources_not_outputs_and_dates_written_files_now(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, root = Path(tmp) / 'candidate', Path(tmp) / 'active'
            for base in (source, root):
                (base / 'src/out/Fly').mkdir(parents=True)
                (base / 'depot_tools').mkdir()
                (base / 'src/same.cc').write_text('same\n')
            (source / 'src/changed.cc').write_text('new\n')
            (root / 'src/changed.cc').write_text('old\n')
            (root / 'src/removed.cc').write_text('gone\n')
            (root / 'src/out/Fly/chrome').write_text('active build\n')
            past = time.time() - 86400
            for file in (source / 'src/same.cc', root / 'src/same.cc', source / 'src/changed.cc'):
                os.utime(file, (past, past))
            written = engine_update.copy_root(source, root, touch=True)
            self.assertEqual(written, 1)
            self.assertEqual((root / 'src/changed.cc').read_text(), 'new\n')
            self.assertGreater((root / 'src/changed.cc').stat().st_mtime, past + 3600)
            self.assertAlmostEqual((root / 'src/same.cc').stat().st_mtime, past, delta=1)
            self.assertFalse((root / 'src/removed.cc').exists())
            self.assertEqual((root / 'src/out/Fly/chrome').read_text(), 'active build\n')


if __name__ == '__main__':
    unittest.main()
