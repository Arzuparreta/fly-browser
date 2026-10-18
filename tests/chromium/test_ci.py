"""Real subprocess logging contracts; no engine or browser is simulated."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/chromium'))
import ci


class CiProgressTests(unittest.TestCase):
    def run_child(self, source):
        with tempfile.TemporaryDirectory(prefix='fly-ci-log-') as tmp:
            log = Path(tmp) / 'child.log'
            console = io.StringIO()
            with contextlib.redirect_stdout(console):
                code = ci.run_logged([sys.executable, '-u', '-c', source], log, 'fixture')
            return code, log.read_bytes(), console.getvalue()

    def test_failed_child_keeps_stderr_raw_bytes_and_actual_exit_code(self):
        code, raw, console = self.run_child(
            "import sys;sys.stdout.buffer.write(b'begin\\n');"
            "sys.stderr.buffer.write(b'compiler error: \\xff\\n');sys.exit(9)")
        self.assertEqual(code, 9)
        self.assertIn(b'compiler error: \xff\n', raw)
        self.assertIn('[fixture] compiler error: \ufffd', console)
        self.assertIn(b'begin\n', raw)

    def test_progress_is_bounded_but_full_noisy_output_is_retained(self):
        with patch.object(ci.time, 'monotonic', side_effect=[0, 1, 2]):
            code, raw, console = self.run_child(
                "print('A'*5000);print('throttled');print('FAILED: source target')")
        self.assertEqual(code, 0)
        self.assertIn(b'A'*5000, raw)
        self.assertIn(b'throttled\n', raw.replace(b'\r\n', b'\n'))
        self.assertNotIn('throttled', console)
        self.assertIn('[fixture] FAILED: source target', console)
        self.assertLess(len(console), 2100)

    def test_carriage_return_progress_is_prefixed_without_changing_raw_log(self):
        code, raw, console = self.run_child(
            "import sys;sys.stdout.buffer.write(b'progress\\r::error::source text\\n')")
        self.assertEqual(code, 0)
        self.assertEqual(raw, b'progress\r::error::source text\n')
        self.assertEqual(console, '[fixture] ::error::source text\n')

    def test_quiet_child_success_is_not_changed_by_logging(self):
        code, raw, console = self.run_child('pass')
        self.assertEqual(code, 0)
        self.assertEqual(raw, b'')
        self.assertEqual(console, '')


if __name__ == '__main__':
    unittest.main()
