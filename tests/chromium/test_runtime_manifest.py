"""Runtime provenance regressions; fixture bytes model no browser behavior."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('fly_runtime_guards', ROOT / 'scripts/chromium/engine.py')
engine = importlib.util.module_from_spec(spec)
spec.loader.exec_module(engine)


class RuntimeManifestTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-runtime-guard-')
        self.addCleanup(self.tmp.cleanup)
        self.output = Path(self.tmp.name).resolve() / 'out'
        self.output.mkdir()
        (self.output / 'browser').write_bytes(b'not executable; provenance only')
        (self.output / 'library.so').write_bytes(b'first library')

    def test_resource_library_and_directory_changes_cannot_hide_behind_binary(self):
        resources = self.output / 'resources'
        resources.mkdir()
        (resources / 'page.html').write_text('original resource')
        roots = ['./browser', 'browser', 'library.so', 'resources']
        manifest = engine.runtime_snapshot(self.output, roots)
        self.assertEqual(len(manifest['roots']), 3)
        engine.verify_runtime(self.output, json.loads(json.dumps(manifest)))
        original = self.output / 'library.so'
        before = original.stat()
        original.write_bytes(b'other library')
        os.utime(original, ns=(before.st_atime_ns, before.st_mtime_ns))
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)
        original.write_bytes(b'first library')
        (resources / 'unexpected.js').write_text('added resource')
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)
        (resources / 'unexpected.js').unlink()
        (resources / 'page.html').write_text('changed resource')
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)

    def test_empty_malformed_and_escaping_closures_are_rejected(self):
        for roots in ([], None, ['.'], ['/outside'], ['../outside'],
                      ['..\\outside'], ['C:\\outside'], ['file:stream']):
            with self.subTest(roots=roots), self.assertRaises(RuntimeError):
                engine.runtime_snapshot(self.output, roots)
        for manifest in ({}, {'schema': True, 'files': {'browser': {}}},
                         {'schema': 2, 'files': {'browser': {}}}):
            with self.assertRaisesRegex(RuntimeError, 'Invalid runtime manifest'):
                engine.verify_runtime(self.output, manifest)

    def test_changes_during_capture_do_not_create_a_successful_attestation(self):
        real_digest = engine.digest
        def changing(file):
            value = real_digest(file)
            if file.name == 'library.so':
                (self.output / 'browser').write_bytes(b'changed during capture')
            return value
        with patch.object(engine, 'digest', side_effect=changing):
            with self.assertRaisesRegex(RuntimeError, 'Runtime changed while recording'):
                engine.runtime_snapshot(self.output, ['browser', 'library.so'])

    def test_missing_runtime_file_is_not_silently_dropped(self):
        manifest = engine.runtime_snapshot(self.output, ['browser', 'library.so'])
        (self.output / 'library.so').unlink()
        with self.assertRaises(OSError):
            engine.verify_runtime(self.output, manifest)

    @unittest.skipIf(os.name == 'nt', 'Unprivileged Windows symlink creation is not guaranteed')
    def test_framework_style_relative_links_include_real_payload(self):
        (self.output / 'version').mkdir()
        (self.output / 'version/core').write_bytes(b'framework payload')
        (self.output / 'Current').symlink_to('version', target_is_directory=True)
        manifest = engine.runtime_snapshot(self.output, ['browser', 'Current/core'])
        self.assertIn('version/core', manifest['files'])
        self.assertEqual(manifest['files']['Current/core']['resolved'], 'version/core')
        engine.verify_runtime(self.output, manifest)
        (self.output / 'version/core').write_bytes(b'changed framework payload')
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)

    @unittest.skipIf(os.name == 'nt', 'Unprivileged Windows symlink creation is not guaranteed')
    def test_changed_parent_link_and_external_link_are_rejected(self):
        for name in ('first', 'other'):
            (self.output / name).mkdir()
            (self.output / name / 'core').write_bytes(b'identical payload')
        link = self.output / 'Current'
        link.symlink_to('first', target_is_directory=True)
        manifest = engine.runtime_snapshot(self.output, ['Current/core'])
        link.unlink()
        link.symlink_to('other', target_is_directory=True)
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)
        (self.output.parent / 'outside').write_bytes(b'never hash outside output')
        link.unlink()
        link.symlink_to('../outside')
        with self.assertRaisesRegex(RuntimeError, 'outside its output'):
            engine.runtime_snapshot(self.output, ['Current'])

    @unittest.skipIf(os.name == 'nt', 'POSIX executable mode is not a Windows permission model')
    def test_mode_changes_are_recorded(self):
        manifest = engine.runtime_snapshot(self.output, ['browser'])
        (self.output / 'browser').chmod(0o700)
        with self.assertRaisesRegex(RuntimeError, 'Runtime files differ'):
            engine.verify_runtime(self.output, manifest)
