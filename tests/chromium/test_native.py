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
        self.root = Path(self.tmp.name)
        self.source = self.root / 'src'
        self.source.mkdir()
        self.git('init', '--quiet')
        self.git('config', 'user.name', 'Fixture')
        self.git('config', 'user.email', 'fixture@invalid.example')
        self.git('remote', 'add', 'origin', 'https://chromium.googlesource.com/chromium/src.git')
        (self.source / 'upstream.cc').write_text('original\n')
        self.git('add', 'upstream.cc')
        self.git('commit', '--quiet', '-m', 'upstream')
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
        self.addCleanup(self.stage_guard.stop)

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.source, check=True,
                              capture_output=True, text=True).stdout.strip()

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
        self.stage_guard.stop()
        with self.assertRaisesRegex(RuntimeError, 'build has not succeeded'):
            native.apply(self.root, self.lock)
        self.assertEqual(self.git('status', '--porcelain'), '')


if __name__ == '__main__':
    unittest.main()
