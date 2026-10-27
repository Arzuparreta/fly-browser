"""Release optimization inputs and stale-build protection; no network/build."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'scripts/chromium'))
import release

SPEC = importlib.util.spec_from_file_location(
    'optimization_package', ROOT / 'scripts/chromium/package-linux.py')
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class ReleaseOptimizationTest(unittest.TestCase):
    def v8_fixture(self, root):
        source = root / 'src'
        directory = source / 'v8/tools/builtins-pgo/profiles'
        directory.mkdir(parents=True)
        revision = 'a' * 40
        (source / 'DEPS').write_text("'v8_revision': '" + revision + "',\n")
        header = source / 'v8/include/v8-version.h'
        header.parent.mkdir()
        header.write_text('fixture selected by the pinned downloader\n')
        return directory, revision

    def test_v8_builtins_profiles_are_prepared_and_revision_checked(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory, revision = self.v8_fixture(root)
            def download(*args):
                (directory / 'x64.profile').write_bytes(b'V8 builtins profile')
                (directory / 'meta.json').write_text(json.dumps(
                    {'revision':revision, 'version':'fixture'}))
            with patch.object(release.engine, 'run', side_effect=download) as run:
                record = release.prepare_v8_pgo(root, {})
            self.assertIn('--check-v8-revision', run.call_args.args[0])
            self.assertIn(root / 'depot_tools', run.call_args.args[0])
            self.assertEqual(record['v8_revision'], revision)
            self.assertEqual(record['sha256'], hashlib.sha256(b'V8 builtins profile').hexdigest())

    def test_v8_missing_empty_mismatched_and_symlinked_inputs_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory, revision = self.v8_fixture(root)
            profile = directory / 'x64.profile'
            metadata = directory / 'meta.json'
            with patch.object(release.engine, 'run') as run:
                with self.assertRaisesRegex(RuntimeError, 'missing or empty'):
                    release.prepare_v8_pgo(root, {})
                profile.touch()
                metadata.write_text(json.dumps({'revision':revision, 'version':'fixture'}))
                with self.assertRaisesRegex(RuntimeError, 'missing or empty'):
                    release.prepare_v8_pgo(root, {})
                self.assertIn('--force', run.call_args.args[0])
                profile.write_bytes(b'profile')
                metadata.write_text(json.dumps({'revision':'b'*40, 'version':'fixture'}))
                with self.assertRaisesRegex(RuntimeError, 'pinned V8 revision'):
                    release.prepare_v8_pgo(root, {})
                if sys.platform != 'win32':
                    profile.unlink()
                    profile.symlink_to(metadata)
                    with self.assertRaisesRegex(RuntimeError, 'Symlinked'):
                        release.prepare_v8_pgo(root, {})

    def test_comparison_modes_keep_security_and_native_release_options(self):
        configs = {mode: release.release_config(optimization=mode)
                   for mode in ('none', 'pgo', 'pgo-thinlto')}
        for config in configs.values():
            for setting in ('is_debug = false', 'is_component_build = false',
                            'dcheck_always_on = false', 'enable_widevine = true',
                            'fly_native_by_default = true'):
                self.assertIn(setting, config)
            self.assertNotIn('is_official_build = true', config)
        self.assertIn('chrome_pgo_phase = 0', configs['none'])
        self.assertIn('use_thin_lto = false', configs['none'])
        self.assertIn('use_mold = true', configs['none'])
        self.assertIn('chrome_pgo_phase = 2', configs['pgo'])
        self.assertIn('use_thin_lto = false', configs['pgo'])
        self.assertIn('use_mold = true', configs['pgo'])
        self.assertIn('chrome_pgo_phase = 2', configs['pgo-thinlto'])
        self.assertIn('use_thin_lto = true', configs['pgo-thinlto'])
        self.assertIn('use_lld = true', configs['pgo-thinlto'])

    def test_uses_pinned_selector_and_records_actual_profile_content(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / 'src/chrome/build'
            directory.mkdir(parents=True)
            selector = directory / 'linux.pgo.txt'
            name = 'chrome-linux-fixture.profdata'
            selector.write_text(name + '\n')
            profile = directory / 'pgo_profiles' / name
            profile.parent.mkdir()

            def download(*args):
                profile.write_bytes(b'profile from the pinned updater')

            with patch.object(release.engine, 'run', side_effect=download) as run:
                record = release.prepare_pgo(root, {})
            command = run.call_args.args[0]
            self.assertIn('tools/update_pgo_profiles.py', command)
            self.assertNotIn('--override-filename', command)
            self.assertEqual(record['filename'], name)
            self.assertEqual(record['sha256'], hashlib.sha256(profile.read_bytes()).hexdigest())
            self.assertEqual(record['selector_sha256'], hashlib.sha256(selector.read_bytes()).hexdigest())
            self.assertEqual(record['size_bytes'], profile.stat().st_size)

    def test_missing_empty_or_symlinked_profile_never_silently_disables_pgo(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            directory = root / 'src/chrome/build'
            directory.mkdir(parents=True)
            selector = directory / 'linux.pgo.txt'
            selector.write_text('fixture.profdata\n')
            profile = directory / 'pgo_profiles/fixture.profdata'
            profile.parent.mkdir()
            with patch.object(release.engine, 'run'):
                with self.assertRaisesRegex(RuntimeError, 'missing or empty'):
                    release.prepare_pgo(root, {})
                profile.touch()
                with self.assertRaisesRegex(RuntimeError, 'missing or empty'):
                    release.prepare_pgo(root, {})
                if sys.platform != 'win32':
                    profile.unlink()
                    profile.symlink_to(selector)
                    with self.assertRaisesRegex(RuntimeError, 'Symlinked'):
                        release.prepare_pgo(root, {})
                    profile.unlink()
                selector.write_text('../unexpected.profdata')
                with self.assertRaisesRegex(RuntimeError, 'Invalid pinned'):
                    release.prepare_pgo(root, {})

    def test_package_refuses_an_optimization_record_from_another_binary(self):
        with tempfile.TemporaryDirectory() as tmp:
            build = Path(tmp)
            self.assertIsNone(package.read_build_optimizations(build))
            binary = build / 'chrome'
            binary.write_bytes(b'built browser')
            config = build / 'args.gn'
            config.write_text('chrome_pgo_phase = 2\n')
            record = {'optimization': 'pgo-thinlto',
                      'args_sha256': hashlib.sha256(config.read_bytes()).hexdigest(),
                      'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
            (build / 'fly-build.json').write_text(json.dumps(record))
            self.assertEqual(package.read_build_optimizations(build), record)
            binary.write_bytes(b'a later unrecorded build')
            with self.assertRaises(SystemExit):
                package.read_build_optimizations(build)
            binary.write_bytes(b'built browser')
            config.write_text('chrome_pgo_phase = 0\n')
            with self.assertRaises(SystemExit):
                package.read_build_optimizations(build)


if __name__ == '__main__':
    unittest.main()
