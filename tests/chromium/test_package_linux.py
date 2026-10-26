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
    def test_release_modes_do_not_depend_on_runner_umask(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'package'
            root.mkdir(mode=0o700)
            assets = root / 'resources'
            assets.mkdir(mode=0o700)
            (assets / 'data').write_text('public')
            (assets / 'data').chmod(0o600)
            binary = root / 'fly'
            binary.write_text('#!/bin/sh\nexit 0\n')
            binary.chmod(0o700)
            package.normalize_package_permissions(root)
            self.assertEqual(root.stat().st_mode & 0o7777, 0o755)
            self.assertEqual(assets.stat().st_mode & 0o7777, 0o755)
            self.assertEqual((assets / 'data').stat().st_mode & 0o7777, 0o644)
            self.assertEqual(binary.stat().st_mode & 0o7777, 0o755)
            package.normalize_package_permissions(root)
            self.assertEqual(binary.stat().st_mode & 0o7777, 0o755)

    def test_packages_only_release_builds_that_start_fly(self):
        release = (Path(__file__).resolve().parents[2] / 'chromium/gn/release.gnargs').read_text()
        package.check_release_build(release)
        with self.assertRaises(SystemExit):
            package.check_release_build(release.replace('fly_native_by_default = true', ''))
        with self.assertRaises(SystemExit):
            package.check_release_build(release.replace('is_component_build = false',
                                                        'is_component_build = true'))

    def test_browser_and_desktop_entry_carry_fly_names(self):
        self.assertEqual(package.PACKAGED_NAMES['chrome'], 'fly-browser')
        self.assertEqual(package.PACKAGED_NAMES['chrome_crashpad_handler'], 'fly-crashpad')
        # X11 window class; Wayland matches the app_id with the entry's name.
        self.assertIn('\nStartupWMClass=Fly\n', package.DESKTOP)
        self.assertIn('\nIcon=fly-browser\n', package.DESKTOP)

    def test_earlier_releases_start_fly_browser_through_app_fly(self):
        with tempfile.TemporaryDirectory() as tmp:
            app = Path(tmp) / 'app dir'
            app.mkdir()
            (app / 'fly-browser').write_text('#!/bin/sh\nprintf "%s|" "$0" "$@"\n')
            (app / 'fly-browser').chmod(0o755)
            (app / 'fly').write_text(package.COMPAT_FLY)
            (app / 'fly').chmod(0o755)
            # Through a symlink, as an earlier install's restart may be.
            link = Path(tmp) / 'link'
            link.symlink_to(app / 'fly')
            result = subprocess.run([str(link), '--fly-native', '--user-data-dir=/a b'],
                                    capture_output=True, text=True, check=True)
            self.assertEqual(result.stdout, f'{app}/fly-browser|--fly-native|--user-data-dir=/a b|')

    def test_install_update_remove_keeps_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / 'home with spaces'
            source = root / 'bundle'
            app = source / 'app'
            (app / 'icons').mkdir(parents=True)
            home.mkdir()
            for size in (48, 128, 256):
                (app / f'icons/fly-logo{size}.png').write_bytes(b'icon')
            (app / 'fly-browser').write_text('#!/bin/sh\n')
            (app / 'fly-browser.desktop').write_text(package.DESKTOP)
            installer = source / 'install.sh'
            installer.write_text(package.INSTALL)
            env = dict(os.environ, HOME=str(home), XDG_DATA_HOME=str(home / 'data'), XDG_CONFIG_HOME=str(home / 'config'))
            preferences = home / 'config/fly-browser/Default/Preferences'
            preferences.parent.mkdir(parents=True)
            preferences.write_text('{}')
            profile = home / 'config/fly-browser/Local State'
            profile.parent.mkdir(parents=True, exist_ok=True)
            profile.write_text('native')
            target = home / 'data/fly-browser'
            def run(*args):
                subprocess.run(['sh', str(installer), *args], env=env, check=True, capture_output=True)
            run()
            self.assertEqual((home / '.local/bin/fly-browser').resolve(), target / 'fly-browser')
            entry = (home / 'data/applications/fly-browser.desktop').read_text()
            self.assertIn(f'Exec="{target}/fly-browser" %U', entry)
            (app / 'updated').write_text('next build')
            run()
            self.assertTrue((target / 'updated').is_file())
            run('--remove')
            self.assertFalse(target.exists())
            self.assertFalse((home / '.local/bin/fly-browser').is_symlink())
            self.assertEqual(profile.read_text(), 'native')
            self.assertEqual(preferences.read_text(), '{}')


if __name__ == '__main__':
    unittest.main()
