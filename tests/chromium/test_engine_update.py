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
            self.write_inventory(self.new_rev, Path(cwd))
        return ''

    def test_moves_syncs_and_runs_hooks_once(self):
        self.assertTrue(engine_update.move(self.root, self.new, jobs=1))
        self.assertEqual(git(self.src, 'rev-parse', 'HEAD'), self.new_rev)
        self.assertEqual(self.commands, [['gclient', 'sync'], ['gclient', 'revinfo'],
                                         ['gclient', 'runhooks']])
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
