"""Checkout/build guard tests, using disposable local repositories only."""

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('fly_engine', ROOT / 'scripts/chromium/engine.py')
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


class EngineTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='fly-engine-test-')
        self.root = Path(self.temporary.name).resolve()
        self.addCleanup(self.temporary.cleanup)

    def repository(self):
        directory = self.root / 'src'
        directory.mkdir()
        def git(*args):
            return subprocess.run(['git', *args], cwd=directory, check=True,
                                  capture_output=True, text=True).stdout.strip()
        git('init')
        git('config', 'user.email', 'fixture@invalid.example')
        git('config', 'user.name', 'Fixture')
        git('remote', 'add', 'origin', 'https://chromium.googlesource.com/chromium/src.git')
        (directory / 'DEPS').write_text('fixture\n')
        git('add', 'DEPS')
        git('commit', '-m', 'fixture')
        pin = {'url': git('remote', 'get-url', 'origin'), 'revision': git('rev-parse', 'HEAD')}
        return directory, pin, git

    def test_dirty_and_untracked_engine_work_is_never_overwritten(self):
        directory, pin, git = self.repository()
        engine.verify_checkout(directory, pin)
        (directory / 'DEPS').write_text('owner changes\n')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine.checkout(self.root, 'src', pin)
        self.assertEqual((directory / 'DEPS').read_text(), 'owner changes\n')
        git('add', 'DEPS')
        git('commit', '-m', 'owner changes')
        pin['revision'] = git('rev-parse', 'HEAD')
        (directory / 'owner.txt').write_text('keep')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine.checkout(self.root, 'src', pin)
        self.assertEqual((directory / 'owner.txt').read_text(), 'keep')

    def test_revision_and_remote_mismatches_do_not_reset(self):
        directory, pin, git = self.repository()
        original = git('rev-parse', 'HEAD')
        with self.assertRaisesRegex(RuntimeError, 'Unexpected source revision'):
            engine.checkout(self.root, 'src', {**pin, 'revision': '0' * 40})
        self.assertEqual(git('rev-parse', 'HEAD'), original)
        with self.assertRaisesRegex(RuntimeError, 'Unexpected origin'):
            engine.checkout(self.root, 'src', {**pin, 'url': 'https://invalid.example/src.git'})
        self.assertEqual(git('remote', 'get-url', 'origin'), pin['url'])

    def test_non_git_and_unborn_directory_contents_are_preserved(self):
        directory = self.root / 'src'
        directory.mkdir()
        (directory / 'keep').write_text('owner')
        with self.assertRaisesRegex(RuntimeError, 'nonempty non-Git'):
            engine.checkout(self.root, 'src', engine.read_lock()['chromium'])
        self.assertEqual((directory / 'keep').read_text(), 'owner')
        subprocess.run(['git', 'init', str(directory)], check=True, capture_output=True)
        subprocess.run(['git', '-C', str(directory), 'remote', 'add', 'origin',
                        engine.read_lock()['chromium']['url']], check=True, capture_output=True)
        with self.assertRaisesRegex(RuntimeError, 'unrelated files'):
            engine.checkout(self.root, 'src', engine.read_lock()['chromium'])

    def test_external_root_rejects_repository_and_symlink_aliases(self):
        for location in (ROOT, ROOT / 'chromium/src', ROOT.parent):
            with self.assertRaisesRegex(RuntimeError, 'dedicated external'):
                engine.engine_root(location)
        alias = self.root / 'alias'
        alias.symlink_to(ROOT, target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'dedicated external'):
            engine.engine_root(alias / 'engine')

    def test_operations_exclude_each_other_and_release_after_failure(self):
        with self.assertRaisesRegex(RuntimeError, 'simulated failure'):
            with engine.exclusive(self.root):
                with self.assertRaisesRegex(RuntimeError, 'Another engine command'):
                    with engine.exclusive(self.root):
                        self.fail('acquired a lease twice')
                raise RuntimeError('simulated failure')
        with engine.exclusive(self.root):
            self.assertEqual(json.loads((self.root / '.fly-engine-operation/owner.json').read_text())['pid'], os.getpid())

    def test_existing_gclient_configuration_is_not_replaced(self):
        file = self.root / '.gclient'
        file.write_text('owner configuration')
        with self.assertRaisesRegex(RuntimeError, 'refusing to overwrite'):
            engine.gclient_config(self.root, engine.read_lock())
        self.assertEqual(file.read_text(), 'owner configuration')

    def test_stale_success_records_cannot_unlock_a_build(self):
        lock = engine.read_lock()
        with self.assertRaisesRegex(RuntimeError, 'sync has not succeeded'):
            engine.require_stage(self.root, 'sync', lock)
        file = self.root / 'evidence/sync.json'
        engine.atomic_json(file, {'lock_sha256': '0' * 64})
        with self.assertRaisesRegex(RuntimeError, 'sync has not succeeded'):
            engine.require_stage(self.root, 'sync', lock)
        engine.atomic_json(file, {'lock_sha256': engine.digest(engine.LOCK)})
        engine.require_stage(self.root, 'sync', lock)
        if os.name != 'nt':
            self.assertEqual(file.stat().st_mode & 0o777, 0o600)

    def test_modified_dependency_trees_are_rejected(self):
        directory, pin, git = self.repository()
        inventory = self.root / 'evidence/dependency-revisions.json'
        engine.atomic_json(inventory, {'src': {'url': pin['url'], 'rev': pin['revision']}})
        engine.atomic_json(self.root / 'evidence/sync.json', {
            'details': {'dependency_revisions_sha256': engine.digest(inventory)}})
        engine.verify_dependencies(self.root)
        (directory / 'DEPS').write_text('preserve dependency edit')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            engine.verify_dependencies(self.root)
        self.assertEqual((directory / 'DEPS').read_text(), 'preserve dependency edit')

    def test_tampered_dependency_inventory_is_rejected(self):
        inventory = self.root / 'evidence/dependency-revisions.json'
        engine.atomic_json(inventory, {})
        engine.atomic_json(self.root / 'evidence/sync.json', {
            'details': {'dependency_revisions_sha256': '0' * 64}})
        with self.assertRaisesRegex(RuntimeError, 'inventory changed'):
            engine.verify_dependencies(self.root)

    def test_pinned_tools_cannot_silently_self_update(self):
        env = engine.environment(self.root)
        self.assertEqual(env['DEPOT_TOOLS_UPDATE'], '0')
        self.assertEqual(env['DEPOT_TOOLS_WIN_TOOLCHAIN'], '0')
        self.assertEqual(env['DEPOT_TOOLS_METRICS'], '0')
        self.assertEqual(env['PATH'].split(os.pathsep)[0], str(self.root / 'depot_tools'))

    def test_git_line_endings_are_pinned_without_overwriting_existing_environment_options(self):
        with patch.dict(os.environ, {'GIT_CONFIG_COUNT': '1',
                                     'GIT_CONFIG_KEY_0': 'fixture.setting',
                                     'GIT_CONFIG_VALUE_0': 'keep'}):
            env = engine.environment(self.root)
        self.assertEqual(env['GIT_CONFIG_COUNT'], '2')
        self.assertEqual(env['GIT_CONFIG_KEY_0'], 'fixture.setting')
        self.assertEqual(env['GIT_CONFIG_VALUE_0'], 'keep')
        self.assertEqual(env['GIT_CONFIG_KEY_1'], 'core.autocrlf')
        self.assertEqual(env['GIT_CONFIG_VALUE_1'], 'false')

    def test_space_floor_is_explicit_and_checked(self):
        with patch.object(engine.shutil, 'disk_usage') as usage:
            usage.return_value.free = 50 * 1024**3
            engine.doctor(self.root, minimum_free_gib=40)
            with self.assertRaisesRegex(RuntimeError, 'Less than 100 GiB'):
                engine.doctor(self.root)

    def test_command_arguments_are_never_shell_evaluated(self):
        with patch.object(engine.subprocess, 'run') as run:
            engine.run(['program', '$(not-a-command)', 'value; still one argument'], self.root)
        call = run.call_args
        self.assertEqual(call.args[0], ['program', '$(not-a-command)', 'value; still one argument'])
        self.assertNotIn('shell', call.kwargs)


if __name__ == '__main__':
    unittest.main()
