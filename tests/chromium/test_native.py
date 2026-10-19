"""Native overlay transaction guards; only tiny disposable local Git trees."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/chromium'))
import native


class NativeOverlayTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-native-guard-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        self.source = self.root / 'src'
        self.source.mkdir()
        self.git('init', '--quiet')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@invalid.example')
        self.git('remote', 'add', 'origin', 'https://chromium.googlesource.com/chromium/src.git')
        (self.source / 'upstream.cc').write_text('original\n')
        self.git('add', 'upstream.cc')
        self.git('commit', '--quiet', '-m', 'upstream')
        (self.source / '.git/info/exclude').write_text('out/\n')
        self.lock = {'chromium': {'url': self.git('remote', 'get-url', 'origin'),
                                  'revision': self.git('rev-parse', 'HEAD')}}
        self.overlay = self.root / 'product/overlay'
        (self.overlay / 'fly').mkdir(parents=True)
        (self.overlay / 'fly/native.cc').write_text('native\n')
        self.patches = self.root / 'product/patches'
        self.patches.mkdir()
        self.series = self.patches / 'series'
        self.series.write_text('first.patch\n')
        (self.patches / 'first.patch').write_text(
            '--- a/upstream.cc\n+++ b/upstream.cc\n@@ -1 +1 @@\n-original\n+adapted\n')
        self.scope = patch.multiple(native, OVERLAY=self.overlay, SERIES=self.series)
        self.scope.start()
        self.addCleanup(self.scope.stop)
        self.stage_guard = patch.object(native.engine, 'require_stage')
        self.stage_guard.start()
        # Tiny build artefacts model integrity gates only; no engine is built.
        args = self.source / 'out/FlyControl/args.gn'
        args.parent.mkdir(parents=True)
        args.write_bytes(native.CONFIG.read_bytes())
        executable = native.engine.binary(self.root)
        executable.parent.mkdir(parents=True, exist_ok=True)
        executable.write_bytes(b'synthetic control fixture')
        native.engine.atomic_json(self.root / 'evidence/build.json', {
            'details': {'args_sha256': native.engine.digest(native.CONFIG),
                        'sha256': native.engine.digest(executable)}})
        self.addCleanup(self.stage_guard.stop)

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.source, check=True,
                              capture_output=True, text=True).stdout.strip()

    def test_full_overlay_requires_control_with_matching_built_arguments(self):
        marker = self.root / 'evidence/build.json'
        original = marker.read_text()
        data = json.loads(original)
        data['details']['args_sha256'] = '0' * 64
        marker.write_text(json.dumps(data))
        with self.assertRaisesRegex(RuntimeError, 'Control build arguments differ'):
            native.apply(self.root, self.lock)
        marker.write_text(original)
        (self.source / 'out/FlyControl/args.gn').write_text('different arguments')
        with self.assertRaisesRegex(RuntimeError, 'Control build arguments differ'):
            native.apply(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_full_overlay_rejects_missing_or_modified_control_executable(self):
        executable = native.engine.binary(self.root)
        executable.write_bytes(b'changed executable')
        with self.assertRaisesRegex(RuntimeError, 'Browser executable differs'):
            native.apply(self.root, self.lock)
        executable.unlink()
        with self.assertRaisesRegex(RuntimeError, 'Browser executable differs'):
            native.apply(self.root, self.lock)
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_apply_verify_and_revert_preserve_upstream_identity(self):
        native.apply(self.root, self.lock)
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'adapted\n')
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'native\n')
        native.verify_source(self.root, self.lock)
        native.apply(self.root, self.lock)  # idempotent
        self.assertEqual(self.git('rev-parse', 'HEAD'), self.lock['chromium']['revision'])
        native.revert(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'original\n')
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_refresh_preserves_upstream_and_unchanged_owned_file_timestamps(self):
        (self.overlay / 'fly/unchanged.h').write_text('unchanged\n')
        native.apply(self.root, self.lock)
        watched = [self.source / 'upstream.cc', self.source / 'fly/unchanged.h']
        before = {file: file.stat().st_mtime_ns for file in watched}
        (self.overlay / 'fly/native.cc').write_text('updated native\n')
        native.refresh_overlay(self.root, self.lock)
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'updated native\n')
        self.assertEqual(before, {file: file.stat().st_mtime_ns for file in watched})
        native.verify_source(self.root, self.lock)
        native.revert(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_refresh_cannot_change_upstream_patch_output_or_overwrite_owner_edits(self):
        native.apply(self.root, self.lock)
        patch_file = self.patches / 'first.patch'
        original = patch_file.read_text()
        patch_file.write_text(original.replace('+adapted', '+changed'))
        with self.assertRaisesRegex(RuntimeError, 'Upstream patch output changed'):
            native.refresh_overlay(self.root, self.lock)
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'adapted\n')
        patch_file.write_text(original)
        owned = self.source / 'fly/native.cc'
        owned.write_text('owner edits\n')
        with self.assertRaisesRegex(RuntimeError, 'Managed native source was edited'):
            native.refresh_overlay(self.root, self.lock)
        self.assertEqual(owned.read_text(), 'owner edits\n')

    def test_refresh_can_add_and_remove_only_managed_fly_files(self):
        native.apply(self.root, self.lock)
        (self.overlay / 'fly/native.cc').unlink()
        (self.overlay / 'fly/replacement.cc').write_text('replacement\n')
        native.refresh_overlay(self.root, self.lock)
        self.assertFalse((self.source / 'fly/native.cc').exists())
        self.assertEqual((self.source / 'fly/replacement.cc').read_text(), 'replacement\n')
        native.verify_source(self.root, self.lock)
        native.revert(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_interrupted_refresh_recovers_from_either_reviewed_overlay_version(self):
        for write_first in (False, True):
            with self.subTest(write_first=write_first):
                (self.overlay / 'fly/native.cc').write_text('native\n')
                native.apply(self.root, self.lock)
                (self.overlay / 'fly/native.cc').write_text('updated\n')
                destination = self.source / 'fly/native.cc'
                real_write = native.atomic_bytes
                def interrupted(file, data):
                    if file == destination:
                        if write_first:
                            real_write(file, data)
                        raise OSError('fixture interruption')
                    return real_write(file, data)
                with patch.object(native, 'atomic_bytes', side_effect=interrupted):
                    with self.assertRaisesRegex(OSError, 'fixture interruption'):
                        native.refresh_overlay(self.root, self.lock)
                self.assertEqual(json.loads((self.root / native.MANIFEST).read_text())['phase'], 'applying')
                native.revert(self.root, self.lock, recover=True)
                self.assertEqual(self.git('status', '--porcelain'), '')

    def test_refresh_recovery_preserves_unreviewed_edits(self):
        native.apply(self.root, self.lock)
        (self.overlay / 'fly/native.cc').write_text('updated\n')
        destination = self.source / 'fly/native.cc'
        real_write = native.atomic_bytes
        def interrupted(file, data):
            if file == destination:
                raise OSError('fixture interruption')
            return real_write(file, data)
        with patch.object(native, 'atomic_bytes', side_effect=interrupted):
            with self.assertRaises(OSError):
                native.refresh_overlay(self.root, self.lock)
        destination.write_text('owner edits after interruption\n')
        with self.assertRaisesRegex(RuntimeError, 'Edited transaction file must be preserved'):
            native.revert(self.root, self.lock, recover=True)
        self.assertEqual(destination.read_text(), 'owner edits after interruption\n')

    def test_core_unit_overlay_does_not_patch_the_browser_or_require_control_binary(self):
        native.apply_core(self.root, self.lock)
        manifest = native.verify_source(self.root, self.lock)
        self.assertEqual(manifest['kind'], 'core')
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'original\n')
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'native\n')
        native.apply_core(self.root, self.lock)
        with self.assertRaisesRegex(RuntimeError, 'Core-only overlay'):
            native.apply(self.root, self.lock)
        native.revert(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_core_unit_overlay_refuses_existing_unmanaged_sources(self):
        (self.source / 'fly').mkdir()
        (self.source / 'fly/native.cc').write_text('keep owner work\n')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            native.apply_core(self.root, self.lock)
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'keep owner work\n')
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_compile_only_overlay_cannot_authorise_full_browser_build(self):
        with patch.object(native.engine, 'require_control', side_effect=AssertionError('No control available')):
            native.apply(self.root, self.lock, objects_only=True)
        manifest = native.verify_source(self.root, self.lock)
        self.assertEqual(manifest['kind'], 'objects')
        with self.assertRaisesRegex(RuntimeError, 'Compile-only overlay'):
            native.apply(self.root, self.lock)
        native.revert(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_late_failed_patch_does_not_install_earlier_patch(self):
        self.series.write_text('first.patch\nsecond.patch\n')
        (self.patches / 'second.patch').write_text(
            '--- a/upstream.cc\n+++ b/upstream.cc\n@@ -1 +1 @@\n-missing\n+never\n')
        with self.assertRaises(subprocess.CalledProcessError):
            native.apply(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_dirty_source_is_never_overwritten(self):
        (self.source / 'upstream.cc').write_text('owner\n')
        with self.assertRaisesRegex(RuntimeError, 'Dirty checkout'):
            native.apply(self.root, self.lock)
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'owner\n')

    def test_edited_overlay_and_unmanaged_files_block_revert(self):
        native.apply(self.root, self.lock)
        (self.source / 'fly/native.cc').write_text('owner edit\n')
        with self.assertRaisesRegex(RuntimeError, 'edited'):
            native.revert(self.root, self.lock)
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'owner edit\n')
        (self.source / 'fly/native.cc').write_text('native\n')
        (self.source / 'keep.cc').write_text('owner file\n')
        with self.assertRaisesRegex(RuntimeError, 'Unmanaged'):
            native.revert(self.root, self.lock)
        self.assertEqual((self.source / 'keep.cc').read_text(), 'owner file\n')

    def test_updated_product_requires_explicit_reapply_but_can_revert_old_overlay(self):
        native.apply(self.root, self.lock)
        (self.overlay / 'fly/native.cc').write_text('new native\n')
        with self.assertRaisesRegex(RuntimeError, 'inputs changed'):
            native.verify_source(self.root, self.lock)
        native.revert(self.root, self.lock)
        native.apply(self.root, self.lock)
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'new native\n')

    def test_interrupted_install_can_recover_without_resetting_anything_else(self):
        original = native.atomic_bytes
        def interrupted(file, data):
            if file == self.source / 'upstream.cc':
                raise OSError('simulated power loss')
            original(file, data)
        with patch.object(native, 'atomic_bytes', side_effect=interrupted):
            with self.assertRaisesRegex(OSError, 'power loss'):
                native.apply(self.root, self.lock)
        with self.assertRaisesRegex(RuntimeError, 'Interrupted overlay'):
            native.verify_source(self.root, self.lock)
        self.assertEqual((self.source / 'fly/native.cc').read_text(), 'native\n')
        native.revert(self.root, self.lock, recover=True)
        self.assertEqual(self.git('status', '--porcelain'), '')
        self.assertFalse((self.root / native.MANIFEST).exists())

    def test_recovery_preserves_edits_after_interruption(self):
        native.apply(self.root, self.lock)
        file = self.root / native.MANIFEST
        manifest = json.loads(file.read_text())
        manifest['phase'] = 'applying'
        native.engine.atomic_json(file, manifest)
        (self.source / 'upstream.cc').write_text('owner after interruption\n')
        with self.assertRaisesRegex(RuntimeError, 'must be preserved'):
            native.revert(self.root, self.lock, recover=True)
        self.assertEqual((self.source / 'upstream.cc').read_text(), 'owner after interruption\n')

    def test_symlinks_and_path_traversal_are_rejected(self):
        for name in ('../keep', '/tmp/keep', 'fly/../../keep', 'C:keep', 'fly/.git/x', 'fly\\keep'):
            with self.assertRaisesRegex(RuntimeError, 'Unsafe overlay path'):
                native.relative_path(name)
        (self.overlay / 'fly/link').symlink_to(self.source / 'upstream.cc')
        with self.assertRaisesRegex(RuntimeError, 'Symlinked overlay'):
            native.apply(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')

    def test_staged_changes_block_verification(self):
        native.apply(self.root, self.lock)
        self.git('add', 'upstream.cc')
        with self.assertRaisesRegex(RuntimeError, 'Staged engine changes'):
            native.verify_source(self.root, self.lock)

    def test_overlay_requires_completed_control_build(self):
        (self.root / 'evidence/build.json').unlink()
        self.stage_guard.stop()
        with self.assertRaisesRegex(RuntimeError, 'build has not succeeded'):
            native.apply(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')



class NativeObjectPlanTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-object-plan-')
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name)
        self.ninja = self.output / 'build.ninja'

    def test_uses_real_compiler_edges_and_prefers_scoped_browser_runner(self):
        self.ninja.write_text(
            'build obj/chrome/test/browser_tests/test.o: cxx ../../fly/test/workspace_browser_test.cc | prerequisites\n'
            'build obj/fly/fly_browser_tests/test.o: cxx ../../fly/test/workspace_browser_test.cc | prerequisites\n'
            'build obj/ui/native.obj: cxx ../../fly/ui/native.cc | generated.h\n'
            'build obj/chrome/start.o: cxx ../../chrome/browser/start.cc\n'
            'build ignored: copy ../../fly/ui/header.h\n')
        with self.ninja.open('ab') as stream:
            stream.write(b'cflags = -Dthird_party_metadata=\xff\n')
        manifest = {'files': dict.fromkeys(['fly/test/workspace_browser_test.cc',
                                          'fly/ui/native.cc', 'chrome/browser/start.cc',
                                          'fly/ui/header.h'])}
        targets, skipped = native.object_targets(self.output, manifest, 'win32')
        self.assertEqual(len(targets), 3)
        self.assertEqual(targets['fly/test/workspace_browser_test.cc'], 'obj/fly/fly_browser_tests/test.o')
        self.assertEqual(targets['fly/ui/native.cc'], 'obj/ui/native.obj')
        self.assertEqual(skipped, [])

    def test_missing_compiler_edges_fail_instead_of_claiming_source_validation(self):
        self.ninja.write_text('')
        manifest = {'files': {'chrome/browser/start.cc': {}}}
        for platform in ('linux', 'win32', 'darwin'):
            with self.subTest(platform=platform):
                with self.assertRaisesRegex(RuntimeError, 'No GN compiler edge'):
                    native.object_targets(self.output, manifest, platform)

    def test_mac_desktop_exclusion_is_explicit_and_does_not_hide_shared_sources(self):
        self.ninja.write_text('build obj/fly/commands/config.o: cxx ../../fly/commands/config.cc\n')
        manifest = {'files': dict.fromkeys(['fly/ui/native.cc', 'fly/test/test.cc',
                                          'fly/commands/config.cc'])}
        targets, skipped = native.object_targets(self.output, manifest, 'darwin')
        self.assertEqual(list(targets), ['fly/commands/config.cc'])
        self.assertEqual(skipped, ['fly/test/test.cc', 'fly/ui/native.cc'])
        with self.assertRaisesRegex(RuntimeError, 'No GN compiler edge'):
            native.object_targets(self.output, manifest, 'linux')


class NativeProductCheckoutBytesTests(unittest.TestCase):
    def test_native_inputs_keep_exact_bytes_with_windows_autocrlf(self):
        with tempfile.TemporaryDirectory(prefix='fly-native-checkout-') as tmp:
            root = Path(tmp)
            def git(*args):
                return subprocess.run(['git', *args], cwd=root, check=True,
                                      capture_output=True)
            git('init', '--quiet')
            git('config', 'user.name', 'Fixture')
            git('config', 'user.email', 'fixture@invalid.example')
            git('config', 'core.autocrlf', 'true')
            (root / '.gitattributes').write_bytes((ROOT / '.gitattributes').read_bytes())
            patch_file = root / 'chromium/patches/native.patch'
            overlay = root / 'chromium/overlay/fly/native.cc'
            patch_file.parent.mkdir(parents=True)
            overlay.parent.mkdir(parents=True)
            patch_bytes = (b'--- a/upstream.cc\n+++ b/upstream.cc\n'
                           b'@@ -1 +1 @@\n-original\n+native\n')
            patch_file.write_bytes(patch_bytes)
            overlay.write_bytes(b'// Native source\n')
            git('add', '.gitattributes', 'chromium')
            git('commit', '--quiet', '-m', 'native inputs')
            patch_file.unlink()
            overlay.unlink()
            git('checkout', '--', 'chromium')
            self.assertEqual(patch_file.read_bytes(), patch_bytes)
            self.assertEqual(overlay.read_bytes(), b'// Native source\n')
            (root / 'upstream.cc').write_bytes(b'original\n')
            git('apply', '--check', '--whitespace=error-all', str(patch_file))


class BrowserResultCoverageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-browser-coverage-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root / 'browser.cc'
        self.summary = self.root / 'results.json'
        self.source.write_text(
            'IN_PROC_BROWSER_TEST_F(FlyWorkspaceBrowserTest, NativeIds) {}\n'
            'IN_PROC_BROWSER_TEST_F(FlySessionRestoreBrowserTest, PRE_Restart) {}\n'
            'IN_PROC_BROWSER_TEST_F(FlySessionRestoreBrowserTest, Restart) {}\n')
        self.passed = {'FlyWorkspaceBrowserTest.NativeIds': [{'status': 'SUCCESS'}],
                       'FlySessionRestoreBrowserTest.Restart': [{'status': 'SUCCESS'}]}

    def test_launcher_zero_cases_or_a_missing_fixture_cannot_pass(self):
        for iterations in ([], [{}], [dict(list(self.passed.items())[:1])]):
            with self.subTest(iterations=iterations):
                self.summary.write_text(json.dumps({'per_iteration_data': iterations}))
                with self.assertRaisesRegex(RuntimeError, 'no test iterations|missing or unsuccessful'):
                    native.verify_browser_results(self.summary, self.source)

    def test_failed_or_not_run_main_result_cannot_pass(self):
        for status in ('FAILURE', 'CRASH', 'NOTRUN', 'SKIPPED'):
            with self.subTest(status=status):
                data = dict(self.passed)
                data['FlySessionRestoreBrowserTest.Restart'] = [{'status': status}]
                self.summary.write_text(json.dumps({'per_iteration_data': [data]}))
                with self.assertRaisesRegex(RuntimeError, 'missing or unsuccessful'):
                    native.verify_browser_results(self.summary, self.source)

    def test_pre_success_is_folded_into_the_native_main_result(self):
        self.summary.write_text(json.dumps({'per_iteration_data': [self.passed]}))
        result = native.verify_browser_results(self.summary, self.source)
        self.assertEqual(result['main_cases_per_iteration'], 2)
        self.assertEqual(result['iterations'], 1)


if __name__ == '__main__':
    unittest.main()
