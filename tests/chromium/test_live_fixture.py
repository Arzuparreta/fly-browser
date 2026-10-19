"""Network-free input/evidence guards; live playback requires the explicit desktop run."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class LiveFixtureGuards(unittest.TestCase):
    def node(self, source):
        subprocess.run(['node', '-e', source], cwd=ROOT, check=True,
                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def test_only_public_homepage_or_channel_urls_are_accepted(self):
        self.node("""
            const assert = require('node:assert/strict');
            const {publicTwitchUrl: url} = require('./tests/chromium/live-twitch');
            assert.equal(url('https://www.twitch.tv'), 'https://www.twitch.tv/');
            assert.equal(url('https://www.twitch.tv/public_channel'), 'https://www.twitch.tv/public_channel');
            for (const value of ['http://www.twitch.tv/test', 'https://evil.test/test',
                'https://www.twitch.tv.evil.test/test', 'https://user:secret@www.twitch.tv/test',
                'https://www.twitch.tv/test?token=private', 'https://www.twitch.tv/test#private',
                'https://www.twitch.tv:8443/test', 'https://www.twitch.tv/settings/security',
                'https://www.twitch.tv/directory/category/music', 'file:///tmp/profile'])
                assert.throws(() => url(value));
        """)

    def test_missing_media_or_decoder_errors_cannot_count_as_preserved_pause(self):
        self.node("""
            const assert = require('node:assert/strict');
            const {pausedState: paused} = require('./tests/chromium/live-twitch');
            assert.equal(paused({videos: []}), false);
            assert.equal(paused({videos: [{paused: true, error: 3}]}), false);
            assert.equal(paused({videos: [{paused: false, error: null}]}), false);
            assert.equal(paused({videos: [{paused: true, error: null}, {paused: false, error: null}]}), false);
            assert.equal(paused({videos: [{paused: true, error: null}]}), true);
        """)

    def test_physical_input_refuses_the_owners_display_before_loading_x11(self):
        with tempfile.TemporaryDirectory(prefix='fly-owner-display-guard-') as tmp:
            authority = Path(tmp) / 'auth'
            authority.touch()
            result = subprocess.run([sys.executable, ROOT / 'tests/chromium/x11-input.py',
                                     '--keys', 'a'], cwd=ROOT, text=True, capture_output=True,
                                    env=dict(os.environ, XAUTHORITY=str(authority), DISPLAY=':0',
                                             FLY_TEST_OZONE_PLATFORM='x11'))
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('owned disposable X11 display', result.stderr)


if __name__ == '__main__':
    unittest.main()
