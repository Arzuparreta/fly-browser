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
import urllib.error
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/chromium'))
import engine
import engine_update
import native
import patch_repair

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


class OfficialMetadataTests(unittest.TestCase):
    def test_throttled_metadata_retries_the_same_url_and_respects_bounded_retry_after(self):
        pin = engine_update.load_pin()
        error = urllib.error.HTTPError('https://official.invalid/metadata', 429, 'busy',
                                       {'Retry-After': '1000'}, None)
        self.addCleanup(error.close)
        with patch.object(pin.urllib.request, 'urlopen', side_effect=[error, io.BytesIO(b'valid')]) as request, \
                patch.object(pin.time, 'sleep') as sleep:
            self.assertEqual(pin.fetch(error.url), b'valid')
        self.assertEqual(request.call_count, 2)
        self.assertEqual(request.call_args_list[0], request.call_args_list[1])
        sleep.assert_called_once_with(60)

    def test_outage_retries_are_bounded_and_not_found_is_not_retried(self):
        pin = engine_update.load_pin()
        for code, attempts in ((503, 3), (404, 1)):
            error = urllib.error.HTTPError('https://official.invalid/metadata', code, 'failed', {}, None)
            self.addCleanup(error.close)
            with patch.object(pin.urllib.request, 'urlopen', side_effect=error) as request, \
                    patch.object(pin.time, 'sleep'), self.assertRaises(urllib.error.HTTPError):
                pin.fetch(error.url, attempts=3)
            self.assertEqual(request.call_count, attempts)

    def test_freshness_uses_only_the_validated_release_record(self):
        pin = engine_update.load_pin()
        release = {'channel': 'Beta', 'platform': 'Linux', 'hashes': {'chromium': 'a' * 40}}
        with patch.object(pin, 'fetch', return_value=json.dumps([release]).encode()) as fetch:
            self.assertEqual(pin.latest_release('Beta'), release)
        fetch.assert_called_once_with(pin.DASH.format(channel='Beta'))
        release['hashes']['chromium'] = 'invalid'
        with patch.object(pin, 'fetch', return_value=json.dumps([release]).encode()), \
                self.assertRaisesRegex(ValueError, 'Invalid revision'):
            pin.latest_release('Beta')


class DisplayTests(unittest.TestCase):
    def test_socket_existence_is_insufficient_without_a_successful_x11_setup(self):
        connection = Mock()
        connection.__enter__ = Mock(return_value=connection)
        connection.__exit__ = Mock(return_value=False)
        with patch.object(engine_update.socket, 'socket', return_value=connection):
            for reply, expected in ((b'', False), (b'\0rejected', False), (b'\1ready', True)):
                connection.recv.return_value = reply
                self.assertEqual(engine_update.display_ready(94), expected)
            connection.connect.side_effect = ConnectionRefusedError()
            self.assertFalse(engine_update.display_ready(94))

    def test_display_crashing_after_initial_readiness_never_starts_tests(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            executable = root / 'xvfb/usr/bin/Xvfb'
            executable.parent.mkdir(parents=True)
            executable.touch()
            process = Mock()
            # Xvfb passes the first poll/setup, then crashes before handoff.
            process.poll.side_effect = [None, 1, 1]
            with patch.object(engine_update.subprocess, 'Popen', return_value=process), \
                    patch.object(engine_update, 'display_ready', return_value=True), \
                    self.assertRaisesRegex(RuntimeError, 'display exited during startup'):
                with engine_update.virtual_display(root, root / 'logs'):
                    self.fail('Tests must not start on a dead display')


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


class PatchRepairTests(PatchCheckTests):
    def configure(self, first, second=None):
        self.patches = native.SERIES.parent
        (self.patches / 'first.patch').write_text(first)
        (self.patches / 'second.patch').write_text(second or (
            '--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,4 @@\n'
            ' top\n TWO\n+three\n bottom\n'))
        return {path.name: path.read_bytes() for path in self.patches.iterdir()}

    def run_repair(self, original, target):
        reference, destination = 'a' * 40, 'b' * 40
        def fetch(url):
            revision = url.split('/+/', 1)[1].split('/', 1)[0]
            return self.fetch(original if revision == reference else target)(url)
        return patch_repair.repair(reference, destination, fetch, log=lambda _: None)

    def test_reanchors_context_and_replays_dependent_patches(self):
        old = self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                             ' top\n-two\n+TWO\n bottom\n')
        changed = self.run_repair({'a.cc': 'top\ntwo\nbottom\n'},
                                  {'a.cc': 'new top\ntwo\nbottom\n'})
        self.assertEqual(changed, ['first.patch', 'second.patch'])
        for name in changed:
            self.assertEqual(patch_repair.edits(old[name]),
                             patch_repair.edits((self.patches / name).read_bytes()))
        self.assertEqual(engine_update.patch_check('b' * 40,
                         self.fetch({'a.cc': 'new top\ntwo\nbottom\n'}), log=lambda _: None), {})

    def test_late_semantic_conflict_writes_nothing_including_earlier_repairs(self):
        old = self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                             ' top\n-two\n+TWO\n bottom\n',
                             '--- a/b.cc\n+++ b/b.cc\n@@ -1,3 +1,3 @@\n'
                             ' head\n-old API\n+Fly API\n tail\n')
        with self.assertRaisesRegex(ValueError, 'second.patch:'):
            self.run_repair({'a.cc': 'top\ntwo\nbottom\n', 'b.cc': 'head\nold API\ntail\n'},
                            {'a.cc': 'new top\ntwo\nbottom\n', 'b.cc': 'head\nnew API\ntail\n'})
        self.assertEqual(old, {path.name: path.read_bytes() for path in self.patches.iterdir()})

    def test_ambiguous_anchor_is_refused_without_writes(self):
        old = self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,4 @@\n'
                             ' head\n+Fly\n anchor\n tail\n')
        with self.assertRaisesRegex(ValueError, 'no unique unchanged context'):
            self.run_repair({'a.cc': 'head\nanchor\ntail\n'},
                            {'a.cc': 'new\nanchor\ntail\nnew\nanchor\ntail\n'})
        self.assertEqual(old, {path.name: path.read_bytes() for path in self.patches.iterdir()})

    def test_repair_is_idempotent_and_does_not_weaken_working_patches(self):
        old = self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                             ' top\n-two\n+TWO\n bottom\n')
        self.assertEqual(self.run_repair({'a.cc': 'top\ntwo\nbottom\n'},
                                         {'a.cc': 'top\ntwo\nbottom\n'}), [])
        self.assertEqual(old, {path.name: path.read_bytes() for path in self.patches.iterdir()})

    def test_transport_failure_never_changes_patches(self):
        old = self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                             ' top\n-two\n+TWO\n bottom\n')
        with self.assertRaises(OSError):
            patch_repair.repair('a' * 40, 'b' * 40,
                                lambda _: (_ for _ in ()).throw(OSError('network')), log=lambda _: None)
        self.assertEqual(old, {path.name: path.read_bytes() for path in self.patches.iterdir()})

    def test_verify_base_rejects_code_changes_even_when_patches_apply(self):
        self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                       ' top\n-two\n+TWO\n bottom\n')
        repo = self.patches.parent
        git(repo, 'init', '--quiet')
        commit(repo, {}, 'base')
        base = git(repo, 'rev-parse', 'HEAD')
        # Both patches continue to apply, but a Fly edit was changed.
        first = self.patches / 'first.patch'
        first.write_text(first.read_text().replace('+TWO\n', '+OTHER\n'))
        commit(repo, {}, 'changed code')
        with patch.object(engine, 'REPO', repo), self.assertRaisesRegex(ValueError, 'changed Fly edits'):
            patch_repair.verify_base(base, 'a' * 40, self.fetch({'a.cc': 'top\ntwo\nbottom\n'}))

    def test_verify_base_compares_full_output_and_refuses_unrelated_files(self):
        self.configure('--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n'
                       ' top\n-two\n+TWO\n bottom\n')
        repo = self.patches.parent
        git(repo, 'init', '--quiet')
        commit(repo, {}, 'base')
        base = git(repo, 'rev-parse', 'HEAD')
        self.run_repair({'a.cc': 'top\ntwo\nbottom\n'}, {'a.cc': 'new top\ntwo\nbottom\n'})
        commit(repo, {}, 'context repair')
        with patch.object(engine, 'REPO', repo):
            patch_repair.verify_base(base, 'a' * 40, self.fetch({'a.cc': 'top\ntwo\nbottom\n'}))
            commit(repo, {'config': 'unexpected'}, 'unrelated')
            with self.assertRaisesRegex(ValueError, 'only change existing integration patches'):
                patch_repair.verify_base(base, 'a' * 40, self.fetch({}))

    def test_rejects_new_files_traversal_and_malformed_patches(self):
        for data in (b'--- /dev/null\n+++ b/file\n',
                     b'--- a/../escape\n+++ b/../escape\n',
                     b'--- a/a.cc\n+++ b/a.cc\n@@ -1,3 +1,3 @@\n-x\n+y\n'):
            with self.subTest(data=data), self.assertRaises((ValueError, RuntimeError)):
                patch_repair.parse(data)


class VendoredDependencyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-vendored-dependency-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'src'
        self.source.mkdir()
        git(self.source, 'init', '--quiet')
        git(self.source, 'remote', 'add', 'origin', SRC_URL)
        self.dependency = self.source / 'vendor'
        self.dependency.mkdir()
        git(self.dependency, 'init', '--quiet')
        git(self.dependency, 'remote', 'add', 'origin', 'https://example.invalid/managed.git')
        dep = commit(self.dependency, {'file.cc': 'managed dependency\n'}, 'dependency')
        old = commit(self.source, {'main.cc': 'main\n'}, 'old source')
        self.old = {'url': SRC_URL, 'revision': old}
        env = {**os.environ, 'GIT_INDEX_FILE': str(self.root / 'new.index')}
        def index(*args):
            return subprocess.run(['git', *args], cwd=self.source, env=env, check=True,
                                  capture_output=True, text=True).stdout.strip()
        self.index = index
        index('read-tree', old)
        index('update-index', '--force-remove', 'vendor')
        blob = subprocess.run(['git', 'hash-object', '-w', '--stdin'], cwd=self.source,
                               input='vendored upstream\n', check=True, capture_output=True, text=True).stdout.strip()
        index('update-index', '--add', '--cacheinfo', '100644,' + blob + ',vendor/file.cc')
        tree = index('write-tree')
        new = git(self.source, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                  'commit-tree', tree, '-p', old, '-m', 'vendored upstream')
        self.new = {'url': SRC_URL, 'revision': new}
        inventory = self.root / 'evidence/dependency-revisions.json'
        engine.atomic_json(inventory, {'src': {'url': SRC_URL, 'rev': old},
            'src/vendor': {'url': 'https://example.invalid/managed.git', 'rev': dep}})
        engine.atomic_json(self.root / 'evidence/sync.json', {
            'details': {'dependency_revisions_sha256': engine.digest(inventory)}})

    def transition(self):
        return engine_update.retire_vendored_dependencies(self.root, self.old, self.new)

    def test_gitlink_becomes_vendored_files_without_deleting_the_dependency(self):
        engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertEqual((self.dependency / 'file.cc').read_text(), 'vendored upstream\n')
        record = json.loads((self.root / 'evidence/engine-retired-dependencies-last.json').read_text())
        backup = self.root / record['entries'][0]['backup']
        self.assertEqual((backup / 'file.cc').read_text(), 'managed dependency\n')
        self.assertTrue((backup / '.git').is_dir())
        self.assertFalse((self.root / engine_update.RETIRED_STATE).exists())
        engine.verify_checkout(self.source, self.new)

    def test_dirty_dependency_is_preserved_and_blocks_the_transition(self):
        (self.dependency / 'owner.txt').write_text('owner work\n')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertEqual((self.dependency / 'owner.txt').read_text(), 'owner work\n')
        self.assertEqual(git(self.source, 'rev-parse', 'HEAD'), self.old['revision'])
        self.assertFalse((self.root / engine_update.RETIRED_STATE).exists())

    def removed_target(self):
        self.index('read-tree', self.old['revision'])
        self.index('update-index', '--force-remove', 'vendor')
        tree = self.index('write-tree')
        revision = git(self.source, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                       'commit-tree', tree, '-p', self.old['revision'], '-m', 'removed dependency')
        self.new = {'url': SRC_URL, 'revision': revision}

    def test_removed_gitlink_is_preserved_before_checkout(self):
        self.removed_target()
        engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertFalse(self.dependency.exists())
        record = json.loads((self.root / 'evidence/engine-retired-dependencies-last.json').read_text())
        self.assertEqual((self.root / record['entries'][0]['backup'] / 'file.cc').read_text(), 'managed dependency\n')
        engine.verify_checkout(self.source, self.new)

    def test_resume_new_head_preserves_obsolete_checkout_before_verifying_sources(self):
        self.removed_target()
        git(self.source, 'checkout', '--quiet', '--detach', self.new['revision'])
        self.assertTrue(self.dependency.exists())
        self.assertFalse(engine_update.move_checkout(self.source, self.old, self.new,
                         submodules=False, transition=self.transition))
        self.assertFalse(self.dependency.exists())
        engine.verify_checkout(self.source, self.new)

    def generated_target(self):
        script = 'buildtools/reclient_cfgs/configure_reclient_cfgs.py'
        ignore = 'buildtools/reclient_cfgs/.gitignore'
        old = commit(self.source, {script: "print('generate reproxy.cfg')\n", ignore: 'reproxy.cfg\n'}, 'old generator')
        self.old = {'url': SRC_URL, 'revision': old}
        self.generated = self.source / 'buildtools/reclient_cfgs/reproxy.cfg'
        self.generated.write_text('preserved configuration\n')
        self.index('read-tree', old)
        for name, value in ((script, "print('modern Siso')\n"), (ignore, '')):
            blob = subprocess.run(['git', 'hash-object', '-w', '--stdin'], cwd=self.source,
                                   input=value, check=True, capture_output=True, text=True).stdout.strip()
            self.index('update-index', '--add', '--cacheinfo', '100644,' + blob + ',' + name)
        revision = git(self.source, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                       'commit-tree', self.index('write-tree'), '-p', old, '-m', 'new generator')
        self.new = {'url': SRC_URL, 'revision': revision}

    def test_only_the_retired_upstream_generated_config_is_preserved(self):
        self.generated_target()
        engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertFalse(self.generated.exists())
        record = json.loads((self.root / 'evidence/engine-retired-dependencies-last.json').read_text())
        item = record['entries'][0]
        self.assertEqual(item['kind'], 'generated')
        self.assertEqual((self.root / item['backup']).read_text(), 'preserved configuration\n')
        engine.verify_checkout(self.source, self.new)

    def test_failed_checkout_restores_the_generated_config_bytes(self):
        self.generated_target()
        real = engine.run
        def run(args, *positional, **kwargs):
            if args[:4] == ['git', '-c', 'core.autocrlf=false', 'checkout']:
                raise subprocess.CalledProcessError(1, args)
            return real(args, *positional, **kwargs)
        with patch.object(engine, 'run', side_effect=run), self.assertRaises(subprocess.CalledProcessError):
            engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertEqual(self.generated.read_text(), 'preserved configuration\n')
        self.assertFalse((self.root / engine_update.RETIRED_STATE).exists())
        engine.verify_checkout(self.source, self.old)

    def test_failed_checkout_restores_the_managed_checkout_without_reset(self):
        real = engine.run
        def run(args, *positional, **kwargs):
            if args[:4] == ['git', '-c', 'core.autocrlf=false', 'checkout']:
                raise subprocess.CalledProcessError(1, args)
            return real(args, *positional, **kwargs)
        with patch.object(engine, 'run', side_effect=run), self.assertRaises(subprocess.CalledProcessError):
            engine_update.move_checkout(self.source, self.old, self.new, submodules=False, transition=self.transition)
        self.assertEqual((self.dependency / 'file.cc').read_text(), 'managed dependency\n')
        self.assertTrue((self.dependency / '.git').is_dir())
        self.assertFalse((self.root / engine_update.RETIRED_STATE).exists())
        engine.verify_checkout(self.source, self.old)

    def nested_case(self, keep_gitlink=False):
        child = self.dependency / 'child'
        child.mkdir()
        git(child, 'init', '--quiet')
        git(child, 'remote', 'add', 'origin', 'https://example.invalid/child.git')
        child_rev = commit(child, {'file.cc': 'nested managed source\n'}, 'child')
        parent_old = commit(self.dependency, {'DEPS': 'old nested dependencies\n'}, 'parent with child')
        source_old = commit(self.source, {}, 'record parent')
        env = {**os.environ, 'GIT_INDEX_FILE': str(self.root / 'parent.index')}
        def index(*args):
            return subprocess.run(['git', *args], cwd=self.dependency, env=env, check=True,
                                  capture_output=True, text=True).stdout.strip()
        index('read-tree', parent_old)
        if keep_gitlink:
            newer = git(child, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                        'commit-tree', git(child, 'write-tree'), '-p', child_rev, '-m', 'unmanaged pointer')
            index('update-index', '--cacheinfo', '160000,' + newer + ',child')
        else:
            index('update-index', '--force-remove', 'child')
        parent_new = git(self.dependency, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                         'commit-tree', index('write-tree'), '-p', parent_old, '-m', 'parent removes child')
        previous = {'src': {'url': SRC_URL, 'rev': source_old},
            'src/vendor': {'url': 'https://example.invalid/managed.git', 'rev': parent_old},
            'src/vendor/child': {'url': 'https://example.invalid/child.git', 'rev': child_rev}}
        current = {'src/vendor': {'url': 'https://example.invalid/managed.git', 'rev': parent_new}}
        return child, previous, current

    def test_nested_retirement_after_gclient_sync_preserves_the_old_child(self):
        child, previous, current = self.nested_case()
        git(self.dependency, 'checkout', '--quiet', '--detach', current['src/vendor']['rev'])
        engine_update.retire_nested_dependencies(self.root, previous, current)
        self.assertFalse(child.exists())
        backup = next((self.root / 'evidence/retired-dependencies').glob('*/src/vendor/child/file.cc'))
        self.assertEqual(backup.read_text(), 'nested managed source\n')
        engine.verify_checkout(self.dependency, {'url': current['src/vendor']['url'],
                                                 'revision': current['src/vendor']['rev']})

    def test_nested_retirement_refuses_owner_edits(self):
        child, previous, current = self.nested_case()
        (child / 'owner.txt').write_text('owner work\n')
        git(self.dependency, 'checkout', '--quiet', '--detach', current['src/vendor']['rev'])
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine_update.retire_nested_dependencies(self.root, previous, current)
        self.assertEqual((child / 'owner.txt').read_text(), 'owner work\n')

    def test_gitlink_no_longer_managed_by_deps_is_preserved_and_left_uninitialised(self):
        child, previous, current = self.nested_case(keep_gitlink=True)
        git(self.dependency, 'checkout', '--quiet', '--detach', current['src/vendor']['rev'])
        engine_update.retire_nested_dependencies(self.root, previous, current)
        self.assertTrue(child.is_dir())
        self.assertEqual(list(child.iterdir()), [])
        backup = next((self.root / 'evidence/retired-dependencies').glob('*/src/vendor/child/file.cc'))
        self.assertEqual(backup.read_text(), 'nested managed source\n')
        engine.verify_checkout(self.dependency, {'url': current['src/vendor']['url'],
                                                 'revision': current['src/vendor']['rev']})

    def test_nested_checkout_is_prepared_before_recursive_dependency_fetch(self):
        child, previous, current = self.nested_case()
        real = engine.run
        def run(args, cwd, env=None, capture=False, foreground=False):
            if args[:2] == ['gclient', 'revinfo']:
                Path(args[-1]).write_text(json.dumps(current))
                return ''
            return real(args, cwd, env, capture, foreground)
        with patch.object(engine, 'run', side_effect=run):
            engine_update.fetch_dependencies(self.root, None, previous=previous)
        self.assertFalse(child.exists())
        self.assertEqual(git(self.dependency, 'rev-parse', 'HEAD'), current['src/vendor']['rev'])

    def test_resume_after_checkout_finalises_the_recovery_journal(self):
        self.test_gitlink_becomes_vendored_files_without_deleting_the_dependency()
        last = self.root / 'evidence/engine-retired-dependencies-last.json'
        last.rename(self.root / engine_update.RETIRED_STATE)
        engine_update.restore_retired_dependencies(self.root, self.old, self.new)
        self.assertFalse((self.root / engine_update.RETIRED_STATE).exists())
        self.assertTrue(last.exists())


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

    def test_sync_retires_nested_checkouts_before_full_gitlink_verification(self):
        parent = self.src / 'vendor'
        child = parent / 'child'
        child.mkdir(parents=True)
        git(parent, 'init', '--quiet')
        git(parent, 'remote', 'add', 'origin', 'https://example.invalid/parent.git')
        git(child, 'init', '--quiet')
        git(child, 'remote', 'add', 'origin', 'https://example.invalid/child.git')
        child_rev = commit(child, {'child.cc': 'managed child\n'}, 'child')
        parent_old = commit(parent, {'parent.cc': 'parent\n'}, 'old parent')
        self.old_rev = commit(self.src, {}, 'record old parent')
        index = self.root / 'parent.index'
        env = {**os.environ, 'GIT_INDEX_FILE': str(index)}
        def parent_index(*args):
            return subprocess.run(['git', *args], cwd=parent, env=env, check=True,
                                  text=True, capture_output=True).stdout.strip()
        parent_index('read-tree', parent_old)
        child_new = git(child, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                        'commit-tree', git(child, 'write-tree'), '-p', child_rev, '-m', 'unmanaged pointer')
        parent_index('update-index', '--cacheinfo', '160000,' + child_new + ',child')
        parent_new = git(parent, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                         'commit-tree', parent_index('write-tree'), '-p', parent_old, '-m', 'retire child')
        # Only gclient sync moves this parent (it has no nested DEPS file).
        git(self.src, 'update-index', '--cacheinfo', '160000,' + parent_new + ',vendor')
        self.new_rev = git(self.src, '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                           'commit-tree', git(self.src, 'write-tree'), '-p', self.old_rev, '-m', 'new parent')
        git(self.src, 'read-tree', self.old_rev)
        self.old = lock_for(self.old_rev, 'old deps\n', self.tools_rev, '155.0.1.1')
        self.new = lock_for(self.new_rev, 'old deps\n', self.tools_rev, '156.0.1.1')
        inventory = {'src': {'url': SRC_URL, 'rev': self.old_rev},
            'src/vendor': {'url': 'https://example.invalid/parent.git', 'rev': parent_old},
            'src/vendor/child': {'url': 'https://example.invalid/child.git', 'rev': child_rev}}
        revisions = self.root / 'evidence/dependency-revisions.json'
        engine.atomic_json(revisions, inventory)
        self.lock_file.write_text(json.dumps(self.old))
        engine.evidence(self.root, 'sync', self.old, {'dependency_revisions_sha256': engine.digest(revisions)})
        self.lock_file.write_text(json.dumps(self.new))
        desired = {'src': {'url': SRC_URL, 'rev': self.new_rev},
                   'src/vendor': dict(inventory['src/vendor'], rev=parent_new)}
        real = self.run_command
        def sync(args, cwd, env=None, capture=False, foreground=False):
            if list(map(str, args[:2])) == ['gclient', 'sync']:
                git(parent, 'checkout', '--quiet', '--detach', parent_new)
            if list(map(str, args[:2])) == ['gclient', 'revinfo']:
                Path(args[-1]).write_text(json.dumps(desired))
                return ''
            return real(args, cwd, env, capture, foreground)
        engine.run.side_effect = sync
        self.assertTrue(engine_update.move(self.root, self.new, jobs=1))
        self.assertTrue(child.is_dir())
        self.assertEqual(list(child.iterdir()), [])
        saved = next((self.root / 'evidence/retired-dependencies').glob('*/src/vendor/child/child.cc'))
        self.assertEqual(saved.read_text(), 'managed child\n')
        engine.verify_dependencies(self.root)

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
            # A check that fails after the copy leaves the root not current,
            # so a rerun adopts again instead of reporting nothing to do.
            real = engine.verify_dependencies
            calls = []

            def fails_after_copy(*args, **kwargs):
                calls.append(1)
                if len(calls) == 2:
                    raise RuntimeError('git-lfs: command not found')
                return real(*args, **kwargs)
            with patch.object(engine, 'verify_dependencies', side_effect=fails_after_copy):
                with self.assertRaisesRegex(RuntimeError, 'git-lfs'):
                    engine_update.follow(self.root, self.new, jobs=1, candidate=candidate)
            self.assertFalse(engine_update.root_current(self.root, self.new))
            self.assertTrue(engine_update.follow(self.root, self.new, jobs=1, candidate=candidate))
            self.assertFalse((self.root / engine_update.ADOPT_STATE).exists())
            native.verify_source(self.root, self.new, require_current=False)
            self.assertFalse(engine_update.follow(self.root, self.new, jobs=1, candidate=candidate))
        self.assertEqual(git(self.src, 'rev-parse', 'HEAD'), self.new_rev)
        self.assertEqual((self.src / 'fly/native.cc').read_text(), 'native\n')
        self.assertEqual(engine_update.recorded_lock(self.root)['engine_version'], '156.0.1.1')

    def fake_siso(self, root):
        """A stand-in for Siso's `fs export`/`fs import`: the state file holds
        the exported JSON itself."""
        tool = engine_update.siso(root)
        tool.parent.mkdir(parents=True, exist_ok=True)
        tool.write_text('#!/bin/sh\ncd "$4" || exit 1\n'
                        'case $2 in export) cat .siso_fs_state ;; import) cat > .siso_fs_state ;; esac\n')
        tool.chmod(0o755)
        with (root / 'src/.git/info/exclude').open('a') as exclude:
            exclude.write('third_party/siso/\n')

    @unittest.skipUnless(shutil.which('rsync'), 'rsync is not installed')
    def test_slots_swap_to_a_validated_candidate(self):
        candidate = self.root.parent / 'engine-next'
        candidate.mkdir()
        engine_update.copy_root(self.root, candidate, touch=False)
        (self.root / 'test-tools/xvfb').mkdir(parents=True)
        for base in (self.root, candidate):
            self.fake_siso(base)
            output = base / 'src/out/FlyNative'
            output.mkdir(parents=True)
            (output / '.siso_fs_state').write_text(json.dumps({'entries': [
                {'name': str(base / 'src/fly/native.cc'), 'digest': {'hash': 'a'}},
                {'name': str(self.root.parent / 'elsewhere/kept.h')},
                {'name': str((candidate if base == self.root else self.root) / 'src/copied.o')}]}))
        engine.atomic_json(candidate / 'evidence/build.json',
                           {'details': {'binary': str(candidate / 'src/out/FlyControl/chrome'),
                                        'other': str(self.root) + '-unrelated'}})
        active_link, candidate_link = self.root, candidate
        engine_update.make_slots(active_link, candidate_link)
        slot_a, slot_b = self.root.parent / 'engine-a', self.root.parent / 'engine-b'
        self.assertEqual(active_link.resolve(), slot_a)
        self.assertEqual(candidate_link.resolve(), slot_b)
        self.assertFalse(os.path.isabs(os.readlink(active_link)))
        record = json.loads((slot_b / 'evidence/build.json').read_text())['details']
        self.assertEqual(record['binary'], str(slot_b / 'src/out/FlyControl/chrome'))
        self.assertEqual(record['other'], str(self.root) + '-unrelated')
        state = json.loads((slot_b / 'src/out/FlyNative/.siso_fs_state').read_text())
        self.assertEqual([entry['name'] for entry in state['entries']],
                         [str(slot_b / 'src/fly/native.cc'), str(self.root.parent / 'elsewhere/kept.h')])
        self.assertEqual(state['entries'][0]['digest'], {'hash': 'a'})
        self.assertTrue((slot_b / 'src/out/FlyNative/.siso_fs_state.before-slots').is_file())
        with self.assertRaisesRegex(RuntimeError, 'already a symlink'):
            engine_update.make_slots(active_link, candidate_link)

        self.assertTrue(engine_update.move(candidate_link, self.new, jobs=1))
        with tempfile.TemporaryDirectory() as tmp, self.overlay(tmp):
            native.apply_core(slot_b, self.new)
            with self.assertRaisesRegex(RuntimeError, 'has not passed validate'):
                engine_update.swap(active_link, candidate_link, self.new)
            engine.atomic_json(slot_b / engine_update.VALIDATION,
                               {'lock_sha256': engine.digest(engine.LOCK)})
            self.assertTrue(engine_update.follow(active_link, self.new, jobs=1,
                                                 candidate=candidate_link))
            self.assertEqual(active_link.resolve(), slot_b)
            self.assertEqual(candidate_link.resolve(), slot_a)
            self.assertTrue((slot_b / 'test-tools/xvfb').is_dir())
            self.assertFalse((slot_a / engine_update.SWAP_STATE).exists())
            self.assertFalse((slot_b / engine_update.SWAP_STATE).exists())
            self.assertEqual(json.loads((slot_b / 'evidence/engine-swap.json').read_text())
                             ['details']['from'], str(slot_a))
            # Nothing is copied: the previous active slot keeps its pin.
            self.assertEqual(engine_update.recorded_lock(slot_a)['engine_version'], '155.0.1.1')
            self.assertFalse(engine_update.follow(active_link, self.new, jobs=1,
                                                  candidate=candidate_link))
            self.assertEqual(active_link.resolve(), slot_b)

    def test_an_interrupted_swap_is_completed(self):
        slot_a, slot_b = self.root.parent / 'engine-a', self.root.parent / 'engine-b'
        os.rename(self.root, slot_a)
        slot_b.mkdir()
        active, candidate = self.root.parent / 'engine', self.root.parent / 'engine-next'
        engine_update.point(active, slot_a)
        engine_update.point(candidate, slot_a)
        record = {'active': str(slot_b), 'candidate': str(slot_a)}
        for slot in (slot_a, slot_b):
            engine.atomic_json(slot / engine_update.SWAP_STATE, record)
        self.assertEqual(engine_update.slot_roles(active, candidate), (active, candidate))
        self.assertEqual(active.resolve(), slot_b)
        self.assertEqual(candidate.resolve(), slot_a)
        self.assertFalse((slot_a / engine_update.SWAP_STATE).exists())
        # A link whose directory is reached through a symlink still points
        # to the slot it names.
        alias = self.root.parent / 'nested/alias'
        alias.parent.mkdir()
        alias.symlink_to(self.root.parent)
        engine_update.point(alias / 'engine-next', slot_b)
        self.assertEqual((alias / 'engine-next').resolve(), slot_b)
        with self.assertRaisesRegex(RuntimeError, 'point to the same slot'):
            engine_update.slot_roles(active, candidate)
        # One symlinked root is a plain root on another disk, not a slot.
        self.assertIsNone(engine_update.slot_roles(active, slot_a))

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
            # Same contents, different times: what an earlier adoption leaves.
            (source / 'src/retimed.cc').write_text('same bytes\n')
            (root / 'src/retimed.cc').write_text('same bytes\n')
            past = time.time() - 86400
            for file in (source / 'src/same.cc', root / 'src/same.cc', source / 'src/changed.cc',
                         source / 'src/retimed.cc'):
                os.utime(file, (past, past))
            os.utime(root / 'src/retimed.cc', (past - 3600, past - 3600))
            written = engine_update.copy_root(source, root, touch=True)
            self.assertEqual(written, 1)
            self.assertEqual((root / 'src/changed.cc').read_text(), 'new\n')
            self.assertGreater((root / 'src/changed.cc').stat().st_mtime, past + 3600)
            self.assertAlmostEqual((root / 'src/same.cc').stat().st_mtime, past, delta=1)
            # Not the source's older time: outputs here may be newer than it.
            self.assertAlmostEqual((root / 'src/retimed.cc').stat().st_mtime, past - 3600, delta=1)
            self.assertFalse((root / 'src/removed.cc').exists())
            self.assertEqual((root / 'src/out/Fly/chrome').read_text(), 'active build\n')


if __name__ == '__main__':
    unittest.main()
