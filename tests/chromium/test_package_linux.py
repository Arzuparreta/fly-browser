"""Native user installer contract, using disposable package files and HOME."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('package_linux', Path(__file__).resolve().parents[2] / 'scripts/chromium/package-linux.py')
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class PackageInstallerTest(unittest.TestCase):
    def test_install_update_remove_keeps_profile_and_legacy(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / 'home with spaces'
            source = root / 'bundle'
            app = source / 'app'
            (app / 'icons').mkdir(parents=True)
            home.mkdir()
            for size in (48, 128, 256):
                (app / f'icons/fly-logo{size}.png').write_bytes(b'icon')
            (app / 'fly-browser-native').write_text('#!/bin/sh\n')
            (app / 'fly-browser-native.desktop').write_text(package.DESKTOP)
            installer = source / 'install.sh'
            installer.write_text(package.INSTALL)
            env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / 'data'), XDG_CONFIG_HOME=str(home / 'config'))
            legacy = home / 'config/fly-browser/session.json'
            legacy.parent.mkdir(parents=True)
            legacy.write_text('legacy')
            profile = home / 'config/fly-browser-native/Local State'
            profile.parent.mkdir(parents=True)
            profile.write_text('native')
            target = home / 'data/fly-browser-native'
            def run(*args):
                subprocess.run(['sh', str(installer), *args], env=env, check=True, capture_output=True)
            run()
            self.assertEqual((home / '.local/bin/fly-browser').resolve(), target / 'fly-browser-native')
            entry = (home / 'data/applications/fly-browser-native.desktop').read_text()
            self.assertIn(f'Exec="{target}/fly-browser-native" %U', entry)
            (app / 'updated').write_text('next build')
            run()
            self.assertTrue((target / 'updated').is_file())
            run('--remove')
            self.assertFalse(target.exists())
            self.assertFalse((home / '.local/bin/fly-browser').is_symlink())
            self.assertEqual(profile.read_text(), 'native')
            self.assertEqual(legacy.read_text(), 'legacy')


if __name__ == '__main__':
    unittest.main()
