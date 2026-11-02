"""Security and ownership boundaries of the shared Linux payload."""
import hashlib
import importlib.util
import io
import json
import shutil
import subprocess
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('distribution', ROOT / 'scripts/distribution/linux.py')
delivery = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(delivery)


class DistributionTest(unittest.TestCase):
    def archive(self, root, *, member=None, bad_hash=False):
        archive = root / 'payload.tar.gz'
        binary = b'#!/bin/sh\nexit 0\n'
        files = {
            'fly-browser': binary,
            'chrome-sandbox': binary,
            'fly-browser.desktop': b'[Desktop Entry]\nExec=@EXEC@ %U\n',
            'provenance.json': json.dumps({
                'product_version': delivery.VERSION,
                'binary_sha256': '0' * 64 if bad_hash else hashlib.sha256(binary).hexdigest()
            }).encode(),
            **{f'icons/fly-logo{size}.png': b'icon' for size in [48, 128, 256]}
        }
        with tarfile.open(archive, 'w:gz') as tar:
            for name, data in files.items():
                entry = tarfile.TarInfo('fly-browser/app/' + name)
                entry.size = len(data)
                entry.mode = 0o700 if name in ['fly-browser', 'chrome-sandbox'] else 0o600
                tar.addfile(entry, io.BytesIO(data))
            if member:
                tar.addfile(member)
        return archive

    def test_rejects_archive_escapes_and_links_before_extraction(self):
        for name, kind in [('../escaped', tarfile.REGTYPE),
                           ('/escaped', tarfile.REGTYPE),
                           ('fly-browser/app/link', tarfile.SYMTYPE),
                           ('fly-browser/app/hard', tarfile.LNKTYPE)]:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                member = tarfile.TarInfo(name)
                member.type = kind
                member.linkname = '/outside'
                archive = self.archive(root, member=member)
                with self.assertRaisesRegex(ValueError, 'Unsafe package'):
                    delivery.extract(archive, root / 'extracted')
                self.assertFalse((root / 'extracted').exists())

    def test_rejects_payload_not_matching_its_provenance(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaisesRegex(ValueError, 'provenance'):
                delivery.extract(self.archive(root, bad_hash=True), root / 'source')

    def test_system_payload_has_explicit_owner_and_public_permissions(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = delivery.extract(self.archive(root), root / 'source')
            stage = root / 'system'
            delivery.system_tree(app, stage)
            target = stage / 'opt/fly-browser'
            self.assertEqual((target / 'INSTALLATION').read_text(), 'system\n')
            self.assertEqual((target / 'chrome-sandbox').stat().st_mode & 0o7777, 0o4755)
            self.assertEqual((target / 'fly-browser').stat().st_mode & 0o7777, 0o755)
            self.assertEqual((target / 'provenance.json').stat().st_mode & 0o7777, 0o644)
            self.assertEqual((target / 'release-key.gpg').read_bytes(), delivery.KEY.read_bytes())
            self.assertIn('Exec=/usr/bin/fly-browser %U',
                          (stage / 'usr/share/applications/fly-browser.desktop').read_text())
            self.assertFalse((stage / 'etc/apparmor.d/fly-browser').exists())

    def test_bootstrap_embeds_exactly_the_committed_trust_root(self):
        import base64
        import re
        script = (ROOT / 'scripts/fly-install').read_text()
        encoded = re.search(r"^release_key='([^']+)'$", script, re.M)[1]
        self.assertEqual(base64.b64decode(encoded), delivery.KEY.read_bytes())
        armored = re.search(r"^release_key_armored='([^']+)'$", script, re.M)[1]
        self.assertEqual(base64.b64decode(armored), (ROOT / 'distribution/release-key.asc').read_bytes())

    def test_legacy_recipe_authenticates_payload_without_a_user_keyring(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            home = root / 'signing'
            home.mkdir(mode=0o700)
            def gpg(*args):
                return subprocess.run(['gpg', '--homedir', str(home), '--batch', '--yes', *args],
                                      check=True, capture_output=True).stdout
            gpg('--passphrase', '', '--quick-generate-key', 'Fly compatibility test', 'ed25519', 'sign', '0')
            key = root / 'key.gpg'
            key.write_bytes(gpg('--export'))
            assets = root / 'assets'
            assets.mkdir()
            name = f"{delivery.CONFIG['package']}-{delivery.VERSION}-1-x86_64.pkg.tar.zst"
            payload = assets / name
            payload.write_bytes(b'authenticated native payload')
            gpg('--armor', '--detach-sign', str(payload))
            with mock.patch.object(delivery, 'KEY', key):
                delivery.legacy_pkgbuild(assets)
                recipe = (assets / 'PKGBUILD').read_text()
                self.assertIn(hashlib.sha256(payload.read_bytes()).hexdigest(), recipe)
                self.assertIn("options=('!strip' '!debug')", recipe)
                source = root / 'source'
                source.mkdir()
                shutil.copy2(payload, source / 'payload')
                shutil.copy2(str(payload) + '.asc', source / 'payload.signature')
                command = ['bash', '-c', 'set -e; startdir=$1; srcdir=$2; source "$3"; prepare',
                           'legacy-test', str(root), str(source), str(assets / 'PKGBUILD')]
                self.assertEqual(subprocess.run(command, capture_output=True).returncode, 0)
                (source / 'payload').write_bytes(b'tampered payload')
                self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
                payload.write_bytes(b'tampered published package')
                with self.assertRaises(subprocess.CalledProcessError):
                    delivery.legacy_pkgbuild(assets)


if __name__ == '__main__':
    unittest.main()
