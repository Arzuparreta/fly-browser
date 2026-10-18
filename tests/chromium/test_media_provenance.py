"""Exercise native media evidence boundaries without launching any browser."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / 'tests/chromium/control-smoke.js'


class MediaProvenanceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-media-provenance-')
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        # Data bytes only: this file is never executed and models no engine.
        self.executable = self.directory / 'synthetic-browser'
        self.executable.write_bytes(b'provenance fixture, not a browser')
        self.record = self.directory / 'native-build.json'
        self.work = self.directory / 'must-not-exist'
        self.data = {
            'stage': 'native-build',
            'lock_sha256': hashlib.sha256((ROOT / 'chromium/upstream.lock.json').read_bytes()).hexdigest(),
            'fly_commit': 'fixture', 'fly_dirty': False,
            'details': {'target': 'chrome', 'native_desktop_supported': True,
                        'fingerprint': 'a' * 64, 'binary': str(self.executable),
                        'sha256': hashlib.sha256(self.executable.read_bytes()).hexdigest()},
        }

    def reject(self, data, message):
        args = ['node', str(HARNESS), '--native', '--executable', str(self.executable),
                '--work-dir', str(self.work)]
        if data is not None:
            self.record.write_text(json.dumps(data))
            args += ['--native-record', str(self.record)]
        env = dict(os.environ, FLY_PLAYWRIGHT_MODULE='must-never-load-this-module')
        result = subprocess.run(args, capture_output=True, text=True, env=env)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(message, result.stderr)
        self.assertNotIn('Cannot find module', result.stderr)
        self.assertFalse(self.work.exists())

    def test_native_requires_build_record_before_any_side_effect(self):
        self.reject(None, 'Native fixtures require --native-record')

    def test_control_or_test_success_cannot_authorise_native_media(self):
        for stage in ('build', 'native-test', 'native-test-browser'):
            with self.subTest(stage=stage):
                data = copy.deepcopy(self.data)
                data['stage'] = stage
                self.reject(data, 'Require a completed native build record')

    def test_unsupported_desktop_cannot_claim_native_acceptance(self):
        self.data['details']['native_desktop_supported'] = False
        self.reject(self.data, 'Native desktop adapter is unsupported')

    def test_different_engine_or_overlay_record_is_rejected(self):
        for field, value, message in (
                ('lock_sha256', 'b' * 64, 'Native engine lock differs'),
                ('fingerprint', '', 'Require overlay fingerprint'),
                ('target', 'fly_unittests', 'Require the native browser build')):
            with self.subTest(field=field):
                data = copy.deepcopy(self.data)
                (data if field == 'lock_sha256' else data['details'])[field] = value
                self.reject(data, message)

    def test_different_or_replaced_executable_is_rejected(self):
        other = self.directory / 'another-browser'
        other.write_bytes(self.executable.read_bytes())
        self.data['details']['binary'] = str(other)
        self.reject(self.data, 'Native executable path differs')
        self.data['details']['binary'] = str(self.executable)
        self.executable.write_bytes(b'replaced')
        self.reject(self.data, 'Native executable hash differs')

    def test_matching_record_returns_auditable_provenance_without_launch(self):
        self.record.write_text(json.dumps(self.data))
        code = "const p=require(process.argv[1]); console.log(JSON.stringify(p.validateNativeBuild(process.argv[2],process.argv[3])));"
        result = subprocess.run(['node', '-e', code, str(HARNESS), str(self.executable), str(self.record)],
                                capture_output=True, text=True, check=True)
        provenance = json.loads(result.stdout)
        self.assertEqual(provenance['sha256'], hashlib.sha256(self.record.read_bytes()).hexdigest())
        self.assertEqual(provenance['overlayFingerprint'], 'a' * 64)
        self.assertEqual(provenance['flyCommit'], 'fixture')
        self.assertFalse(self.work.exists())
