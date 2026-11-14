"""Scope, isolation, retry and validation gates; never contacts a model."""
import argparse
import contextlib
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import tomllib
import time
import unittest
import urllib.error
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/chromium'))
import codex_repair
import engine
import repair_sandbox

PATCH = b'--- a/chrome/file.cc\n+++ b/chrome/file.cc\n@@ -1,3 +1,3 @@\n top\n-old\n+Fly\n bottom\n'


class ScopeTests(unittest.TestCase):
    def test_adapters_existing_patches_and_new_compatibility_tests_are_allowed(self):
        codex_repair.guarded_change('chromium/patches/0001.patch', PATCH, PATCH.replace(b'+Fly', b'+FlyAPI'))
        codex_repair.guarded_change('chromium/overlay/fly/ui/adapter.cc', b'old', b'new')
        codex_repair.guarded_change('chromium/overlay/fly/test/compat_profile_test.cc', None, b'test')
        codex_repair.guarded_change('chromium/overlay/fly/integration.gni',
                                   b'  "//fly/old.cc",\n', b'  "//fly/old.cc",\n  "//fly/new.cc",\n')

    def test_model_cannot_change_authority_pin_versions_resources_or_existing_tests(self):
        names = ['scripts/chromium/native.py', '.github/workflows/engine-update.yml',
                 'AGENTS.md', 'chromium/upstream.lock.json', 'package.json',
                 'chromium/patches/series', 'chromium/overlay/fly/fly_version.h',
                 'chromium/overlay/fly/resources/file.cc',
                 'chromium/overlay/fly/test/workspace_browser_test.cc',
                 'chromium/overlay/fly/commands/actions.json']
        for name in names:
            with self.subTest(name=name), self.assertRaises(RuntimeError):
                codex_repair.guarded_change(name, b'old', b'new')

    def test_cannot_delete_sources_add_patches_or_inject_build_execution(self):
        for name, before, after in [
            ('chromium/overlay/fly/ui/adapter.cc', b'old', None),
            ('chromium/patches/new.patch', None, PATCH),
            ('chromium/overlay/fly/integration.gni', b'old\n', b'exec_script("steal.py")\n'),
            ('chromium/patches/0001.patch', PATCH, PATCH.replace(b'+Fly', b'+exec_script("steal.py")')),
        ]:
            with self.subTest(name=name), self.assertRaises((RuntimeError, ValueError)):
                codex_repair.guarded_change(name, before, after)

    def test_new_upstream_build_hooks_are_refused_but_context_changes_are_allowed(self):
        original = PATCH.replace(b'chrome/file.cc', b'chrome/BUILD.gn')
        codex_repair.guarded_change('chromium/patches/0001.patch', original,
                                   original.replace(b' top\n', b' new top\n'))
        with self.assertRaisesRegex(RuntimeError, 'integration.gni'):
            codex_repair.guarded_change('chromium/patches/0001.patch', original,
                                       original.replace(b'+Fly', b'+"//fly/new.cc",'))

    def test_symlinks_and_late_forbidden_changes_are_not_proposals(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'link').symlink_to('/etc/passwd')
            with self.assertRaisesRegex(RuntimeError, 'Symlink'):
                codex_repair.tree(root)
        with self.assertRaises(RuntimeError):
            codex_repair.changes({'chromium/overlay/fly/ui/a.cc': b'a', 'AGENTS.md': b'old'},
                                 {'chromium/overlay/fly/ui/a.cc': b'b', 'AGENTS.md': b'new'})


class SandboxTests(unittest.TestCase):
    def test_failure_feedback_keeps_the_first_assertion_before_a_long_summary(self):
        with tempfile.TemporaryDirectory() as tmp:
            log = Path(tmp) / 'native.log'
            log.write_text('build output\n' * 1000 + 'test.cc:42: Failure\nactual: wrong\n' +
                           'FailedTestName\n' * 2000 + 'final failure summary\n')
            excerpt = repair_sandbox.failure_excerpt(log)
            self.assertIn('test.cc:42: Failure\nactual: wrong', excerpt)
            self.assertTrue(excerpt.endswith('final failure summary\n'))
            self.assertLess(len(excerpt), 8100)

    @unittest.skipUnless(Path('/proc').exists(), 'Linux running-output guard')
    def test_a_running_output_is_deferred_without_interrupting_its_process(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            # Multicall coreutils can choose their applet using the executable
            # path, not argv[0]. The guard covers every binary under src/out.
            binary = root / 'src/out/FlyNative/sleep'
            binary.parent.mkdir(parents=True)
            shutil.copy2('/usr/bin/sleep', binary)
            process = subprocess.Popen([binary, '30'])
            try:
                self.assertIsNone(process.poll(), 'The fixture must still be running')
                self.assertEqual((Path('/proc') / str(process.pid) / 'exe').readlink(), binary)
                with self.assertRaisesRegex(codex_repair.DeferredRepair, 'preserve its session'):
                    codex_repair.idle_output(root)
                self.assertIsNone(process.poll())
            finally:
                process.terminate()
                process.wait()
    def test_export_preserves_executable_modes_and_has_an_independent_object_store(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo = Path(tmp)
            tool = repo / 'tool.sh'
            tool.write_text('#!/bin/sh\nexit 0\n')
            tool.chmod(0o755)
            for command in (['git', 'init', '--quiet'], ['git', 'add', '.'],
                            ['git', '-c', 'user.name=Fixture', '-c', 'user.email=fixture@invalid.example',
                             'commit', '--quiet', '-m', 'fixture']):
                subprocess.run(command, cwd=repo, check=True, capture_output=True)
            with codex_repair.proposal(repo) as workspace:
                self.assertTrue((workspace / 'tool.sh').stat().st_mode & 0o111)
                self.assertEqual(subprocess.run(['git', 'diff', '--summary', 'HEAD'], cwd=workspace,
                                               capture_output=True, text=True, check=True).stdout, '')
                self.assertFalse((workspace.parent / 'repo.git/objects/info/alternates').exists())

    @unittest.skipUnless(os.name == 'posix' and Path('/proc').exists(), 'Linux process group verification')
    def test_timeout_kills_descendants_even_when_the_leader_exits_on_interrupt(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            pidfile = root / 'child.pid'
            child = 'import signal,time;signal.signal(signal.SIGINT,signal.SIG_IGN);time.sleep(60)'
            parent = ('import subprocess,sys,time,pathlib;'
                      f'p=subprocess.Popen([sys.executable,"-c",{child!r}]);'
                      f'pathlib.Path({str(pidfile)!r}).write_text(str(p.pid));time.sleep(60)')
            with self.assertRaises(subprocess.TimeoutExpired):
                repair_sandbox.run([sys.executable, '-c', parent], root, root / 'log', 0.5,
                                   repair_sandbox.clean_environment(root))
            pid = int(pidfile.read_text())
            for _ in range(50):
                status = Path(f'/proc/{pid}/stat')
                if not status.exists() or status.read_text().split()[2] == 'Z':
                    break
                time.sleep(0.01)
            else:
                os.kill(pid, 9)
                self.fail('A repair descendant survived its timeout')

    def test_author_has_scoped_writes_reviewer_is_read_only_and_outputs_are_denied(self):
        root = Path('/tmp/repair-test/worktree')
        for writable in (False, True):
            arguments = repair_sandbox.codex_options(root, [Path('/engine')], writable)
            filesystem = next(item for item in arguments if item.startswith('permissions.fly-repair.filesystem='))
            rules = tomllib.loads('rules=' + filesystem.split('=', 1)[1])['rules']
            self.assertEqual(rules['/engine/src/out'], 'deny')
            self.assertEqual(rules[':root'], 'deny')
            self.assertEqual(rules.get('/tmp/repair-test/worktree/chromium/patches') == 'write', writable)
            self.assertEqual(rules['/tmp/repair-test/worktree/chromium/patches/series'], 'read')
            self.assertIn('shell_environment_policy.inherit="none"', arguments)

    def test_build_has_no_host_home_credentials_network_or_display(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(repair_sandbox.shutil, 'which', return_value='/usr/bin/bwrap'):
            root = Path(tmp) / 'engine'
            root.mkdir()
            command = repair_sandbox.build_command(Path(tmp) / 'repo', root, Path(tmp) / 'tools', ['python3', 'validate'])
            self.assertIn('--unshare-all', command)
            self.assertIn('--clearenv', command)
            self.assertNotIn('--share-net', command)
            self.assertNotIn(str(Path.home()), command)
            self.assertNotIn('DISPLAY', command)
            self.assertNotIn('GH_TOKEN', command)
            self.assertEqual(command[command.index('--bind') + 1:command.index('--bind') + 3], [str(root), str(root)])

    def test_environment_discards_tokens_agent_settings_sockets_and_desktop(self):
        with patch.dict(os.environ, {'GH_TOKEN': 'fake', 'OPENAI_API_KEY': 'fake',
                                    'SSH_AUTH_SOCK': '/fake', 'DISPLAY': ':0',
                                    'FLY_ENGINE_LEASE': 'fake', 'CODEX_HOME': '/private'}):
            env = repair_sandbox.clean_environment('/private-empty-home')
        self.assertFalse({'GH_TOKEN', 'OPENAI_API_KEY', 'SSH_AUTH_SOCK', 'DISPLAY',
                          'FLY_ENGINE_LEASE', 'CODEX_HOME'} & env.keys())
        self.assertEqual(env['HOME'], '/private-empty-home')


class BudgetAndReviewTests(unittest.TestCase):
    def test_upstream_outage_defers_without_running_a_proposer(self):
        with tempfile.TemporaryDirectory() as tmp, \
                patch.object(sys, 'argv', ['codex_repair.py', 'validate', '--logs', tmp]), \
                patch.object(codex_repair, 'target', side_effect=urllib.error.URLError('unavailable')), \
                patch.object(codex_repair, 'validate') as validate:
            self.assertEqual(codex_repair.main(), 2)
            validate.assert_not_called()

    def test_total_deadline_is_not_reset_between_repair_phases(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = argparse.Namespace(logs=Path(tmp))
            with patch.object(codex_repair.time, 'time', return_value=0):
                self.assertEqual(codex_repair.time_left(args, 1200), 1200)
            with patch.object(codex_repair.time, 'time', return_value=45 * 3600 - 10):
                self.assertEqual(codex_repair.time_left(args, 1200), 10)
            with patch.object(codex_repair.time, 'time', return_value=45 * 3600 + 1):
                with self.assertRaisesRegex(RuntimeError, 'time budget exhausted'):
                    codex_repair.time_left(args, 1200)
    def test_beta_resolution_timestamps_do_not_invalidate_unchanged_pins(self):
        first = dict(engine.read_lock(), channel='Beta', platform='Linux', resolved_at='first')
        second = dict(first, resolved_at='second')
        with tempfile.TemporaryDirectory() as tmp, patch.object(codex_repair.engine_update, 'load_pin') as pin:
            pin.return_value.resolve.side_effect = [first, second]
            args = argparse.Namespace(logs=Path(tmp), channel='Beta')
            original = codex_repair.target(args)
            (Path(tmp) / 'target.lock.json').unlink()
            self.assertEqual(codex_repair.target(args), original)
            self.assertNotIn('resolved_at', original)

    def test_validation_cache_is_bound_to_full_trees_target_source_and_validator(self):
        values = {'HEAD^{tree}': 'head', 'origin/main^{tree}': 'base', 'validator': 'v1'}
        with patch.object(engine, 'git', side_effect=lambda repo, *args: values[args[-1]]), \
                patch.object(engine, 'digest', side_effect=lambda file: values['validator'] + str(file)), \
                patch.object(codex_repair.native, 'fingerprint', return_value='source'):
            args = argparse.Namespace(base='origin/main')
            target = {'revision': 'one'}
            original = codex_repair.validation_key(args, target)
            for name in ('HEAD^{tree}', 'origin/main^{tree}', 'validator'):
                values[name] += '-changed'
                self.assertNotEqual(codex_repair.validation_key(args, target), original)
                values[name] = values[name].removesuffix('-changed')
            self.assertNotEqual(codex_repair.validation_key(args, {'revision': 'two'}), original)

    def test_rejected_review_never_creates_a_native_success_certificate(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            args = argparse.Namespace(logs=root, base='origin/main', channel='Beta')
            with patch.object(codex_repair, 'scope'), \
                    patch.object(codex_repair, 'roots', return_value=(root / 'active', root / 'candidate', root / 'beta')), \
                    patch.object(codex_repair, 'validation_key', return_value='key'), \
                    patch.object(codex_repair, 'state_directory', return_value=root), \
                    patch.object(codex_repair, 'prepare', return_value=root / 'beta'), \
                    patch.object(codex_repair, 'prepare_source'), \
                    patch.object(codex_repair, 'proposal', return_value=contextlib.nullcontext(root)), \
                    patch.object(codex_repair, 'replay'), \
                    patch.object(codex_repair, 'review', side_effect=RuntimeError('rejected')), \
                    patch.object(repair_sandbox, 'run') as native:
                with self.assertRaisesRegex(RuntimeError, 'rejected'):
                    codex_repair.validate(args, {})
                self.assertFalse((root / 'validated-key.json').exists())
                native.assert_not_called()

    def test_budget_is_bounded_for_unchanged_base_and_explicit_retry_resets_once(self):
        with tempfile.TemporaryDirectory() as tmp, patch.dict(os.environ,
                {'FLY_ENGINE_REPAIR_STATE': tmp, 'GITHUB_RUN_ID': 'run', 'FLY_ENGINE_REPAIR_RETRY': 'false'}), \
                patch.object(engine, 'git', return_value='base-chromium-tree'):
            args = argparse.Namespace(base='origin/main')
            frozen = {'chromium': {'revision': 'a' * 40}}
            self.assertEqual(codex_repair.reserve_attempt(args, frozen), 1)
            self.assertEqual(codex_repair.reserve_attempt(args, frozen), 2)
            with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
                codex_repair.reserve_attempt(args, frozen)
            os.environ['FLY_ENGINE_REPAIR_RETRY'] = 'true'
            self.assertEqual(codex_repair.reserve_attempt(args, frozen), 1)
            self.assertEqual(codex_repair.reserve_attempt(args, frozen), 2)
            with self.assertRaisesRegex(RuntimeError, 'budget exhausted'):
                codex_repair.reserve_attempt(args, frozen)

    def test_review_covers_committed_changes_and_requires_unambiguous_approval(self):
        with tempfile.TemporaryDirectory() as tmp:
            args = argparse.Namespace(logs=Path(tmp), base='origin/main')
            with patch.object(codex_repair, 'roots', return_value=(Path('/active'), Path('/candidate'), Path('/repair'))), \
                    patch.object(engine, 'git', return_value='trusted-base-sha'), \
                    patch.object(repair_sandbox, 'codex', return_value={'approved': False, 'findings': ['lost hook']}) as model:
                with self.assertRaisesRegex(RuntimeError, 'review rejected'):
                    codex_repair.review(Path(tmp), args, {}, Path('/repair'), Path(tmp) / 'review.json')
                self.assertIn('git diff trusted-base-sha', model.call_args.args[3])
                self.assertIn('committed AND uncommitted', model.call_args.args[3])
                self.assertIs(model.call_args.args[-2], False)
                self.assertLessEqual(model.call_args.args[-1], 1200)

    def test_root_aliasing_and_ancestor_paths_are_refused(self):
        args = argparse.Namespace(stable_root='/engine', candidate='/candidate', repair_root='/engine/subdir')
        with self.assertRaisesRegex(RuntimeError, 'separate'):
            codex_repair.roots(args)


if __name__ == '__main__':
    unittest.main()
