#!/usr/bin/env python3
"""Package native Fly for Linux from a release build.

  python3 scripts/chromium/package-linux.py --root /path/to/engine \\
      [--output out/FlyRelease] [--dist dist]

Produces dist/Fly-<version>-native-linux-x64.tar.gz (and .sha256) holding a
fly-browser-native/ directory: the browser and its runtime files, Fly's fonts,
What's New page, user content worker and bundled extensions, a launcher and an
install.sh that installs it for the current user as "Fly" next to an
existing Electron Fly. Native Fly keeps its data in
$XDG_CONFIG_HOME/fly-browser-native; the Electron profile in
~/.config/fly-browser is only read, once, to import it.
"""
import argparse
import hashlib
import json
import platform
import shutil
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

# Runtime files of a static (non-component) Chromium build.
RUNTIME_FILES = [
    'chrome', 'chrome_crashpad_handler', 'chrome_100_percent.pak',
    'chrome_200_percent.pak', 'resources.pak', 'icudtl.dat',
    'v8_context_snapshot.bin', 'snapshot_blob.bin', 'libEGL.so',
    'libGLESv2.so', 'libvk_swiftshader.so', 'vk_swiftshader_icd.json',
    'libvulkan.so.1', 'libqt5_shim.so', 'libqt6_shim.so',
]
RUNTIME_DIRS = ['locales', 'MEIPreload', 'PrivacySandboxAttestationsPreloaded',
                'fly_fonts', 'fly_resources']
REQUIRED = ['chrome', 'resources.pak', 'icudtl.dat', 'locales', 'fly_fonts',
            'fly_resources', 'chrome_sandbox']
WHATS_NEW = ['whats-new.html', 'whats-new-page.js', 'whats-new-releases.js']

LAUNCHER = '''#!/bin/sh
# Native Fly launcher. Data lives apart from Electron Fly's profile, which is
# only read once to import it.
HERE=$(dirname "$(readlink -f "$0")")
CONFIG="${XDG_CONFIG_HOME:-$HOME/.config}"
exec "$HERE/fly" --fly-native --class=fly-browser-native \\
  --user-data-dir="$CONFIG/fly-browser-native" "$@"
'''

INSTALL = r'''#!/bin/sh
# Installs native Fly for the current user, next to any Electron Fly.
#   ./install.sh            install or update
#   ./install.sh --remove   uninstall (keeps your data)
set -eu
HERE=$(dirname "$(readlink -f "$0")")
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET="$DATA/fly-browser-native"
BIN="$HOME/.local/bin"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor"
if [ "${1:-}" = "--remove" ]; then
  rm -rf "$TARGET" "$APPS/fly-browser-native.desktop"
  for link in "$BIN/fly-browser-native" "$BIN/fly-browser"; do
    if [ "$(readlink "$link" 2>/dev/null || true)" = "$TARGET/fly-browser-native" ]; then rm -f "$link"; fi
  done
  for size in 48 128 256; do
    rm -f "$ICONS/${size}x${size}/apps/fly-browser-native.png"
  done
  echo "Removed native Fly. Your data in ${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser-native is kept."
  exit 0
fi
STAGE="$TARGET.new"
rm -rf "$STAGE"
mkdir -p "$STAGE" "$BIN" "$APPS"
cp -a "$HERE/app/." "$STAGE/"
rm -rf "$TARGET.old"
[ -d "$TARGET" ] && mv "$TARGET" "$TARGET.old"
mv "$STAGE" "$TARGET"
rm -rf "$TARGET.old"
ln -sf "$TARGET/fly-browser-native" "$BIN/fly-browser-native"
ln -sf "$TARGET/fly-browser-native" "$BIN/fly-browser"
for size in 48 128 256; do
  mkdir -p "$ICONS/${size}x${size}/apps"
  cp "$TARGET/icons/fly-logo$size.png" "$ICONS/${size}x${size}/apps/fly-browser-native.png"
done
# Escape both the quoted Desktop Entry argument and the entry's string value.
escaped=$(printf '%s' "$TARGET/fly-browser-native" | sed 's/[\\"`$]/\\&/g; s/\\/\\\\/g; s/%/%%/g')
{
  sed '/^Exec=/d' "$TARGET/fly-browser-native.desktop"
  printf 'Exec="%s" %%U\n' "$escaped"
} > "$APPS/fly-browser-native.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" || true
echo "Installed native Fly in $TARGET."
echo "Start it from your applications menu (Fly) or with: fly-browser-native"
echo "Close Electron Fly before the first start so your workspaces are imported then."
'''

DESKTOP = '''[Desktop Entry]
Name=Fly
GenericName=Web Browser
Comment=Fly on the Chromium engine
Keywords=browser;web;internet;
Exec=@EXEC@ %U
Icon=fly-browser-native
Terminal=false
Type=Application
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
StartupWMClass=fly-browser-native
'''


def fail(message):
    print(f'package-linux: {message}', file=sys.stderr)
    raise SystemExit(1)


def normalize_package_permissions(root):
    """Public runtime assets must survive a restrictive build-host umask."""
    for file in [root, *root.rglob('*')]:
        if file.is_symlink():
            fail(f'Unexpected symlink in native package: {file}')
        file.chmod(0o755 if file.is_dir() or file.stat().st_mode & 0o111 else 0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--root', required=True, type=Path,
                        help='engine checkout root (contains src/)')
    parser.add_argument('--output', default='out/FlyRelease',
                        help='build directory below src/')
    parser.add_argument('--dist', type=Path, default=REPO / 'dist')
    parser.add_argument('--extensions-cache', type=Path,
                        default=Path(tempfile.gettempdir()) / 'fly-bundled-extensions')
    args = parser.parse_args()
    if sys.platform != 'linux' or platform.machine() != 'x86_64':
        fail('native release archives currently support Linux x86_64 only')
    build = args.root / 'src' / args.output
    for name in REQUIRED:
        if not (build / name).exists():
            fail(f'{build / name} is missing; build the chrome target first')
    args_gn = (build / 'args.gn').read_text(encoding='utf-8')
    if 'is_component_build = true' in args_gn:
        fail('this is a component build; package a release build '
             '(chromium/gn/release.gnargs)')
    version = json.loads((REPO / 'package.json').read_text())['version']
    engine = json.loads((REPO / 'chromium' / 'upstream.lock.json').read_text())['engine_version']
    name = f'Fly-{version}-native-linux-x64'
    args.dist.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        top = Path(tmp) / 'fly-browser-native'
        app = top / 'app'
        app.mkdir(parents=True)
        for file in RUNTIME_FILES:
            if (build / file).is_file():
                shutil.copy2(build / file, app / ('fly' if file == 'chrome' else file))
        if (build / 'chrome_sandbox').is_file():
            shutil.copy2(build / 'chrome_sandbox', app / 'chrome-sandbox')
        # Release builds keep the symbol table (about a third of the browser
        # binary); packages do not need it.
        strip = shutil.which('strip')
        if strip:
            for binary in ('fly', 'chrome_crashpad_handler', 'chrome-sandbox'):
                if (app / binary).is_file():
                    subprocess.run([strip, str(app / binary)], check=True)
        for directory in RUNTIME_DIRS:
            if (build / directory).is_dir():
                shutil.copytree(build / directory, app / directory, symlinks=False)
        for file in WHATS_NEW:
            shutil.copy2(REPO / 'assets' / file, app / 'fly_resources' / file)
        shutil.copytree(REPO / 'assets' / 'fonts', app / 'fly_resources' / 'fonts')
        shutil.copytree(REPO / 'assets' / 'icons', app / 'fly_resources' / 'icons')
        shutil.copytree(REPO / 'assets' / 'icons', app / 'icons')
        subprocess.run([sys.executable, str(REPO / 'scripts/chromium/bundled-extensions.py'),
                        '--out', str(app / 'fly_extensions'),
                        '--cache', str(args.extensions_cache)], check=True)
        shutil.copy2(REPO / 'LICENSE', app / 'LICENSE')
        provenance = {
            'product_version': version,
            'engine': json.loads((REPO / 'chromium/upstream.lock.json').read_text()),
            'repository_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
            'repository_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=REPO, text=True).strip()),
            'binary_sha256': hashlib.sha256((app / 'fly').read_bytes()).hexdigest(),
        }
        (app / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        launcher = app / 'fly-browser-native'
        launcher.write_text(LAUNCHER, encoding='utf-8')
        launcher.chmod(0o755)
        (app / 'fly-browser-native.desktop').write_text(DESKTOP, encoding='utf-8')
        (app / 'VERSION').write_text(f'Fly {version}\nEngine Chromium {engine}\n',
                                     encoding='utf-8')
        installer = top / 'install.sh'
        installer.write_text(INSTALL, encoding='utf-8')
        installer.chmod(0o755)
        normalize_package_permissions(top)
        archive = args.dist / f'{name}.tar.gz'
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(top, arcname='fly-browser-native')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (args.dist / f'{name}.tar.gz.sha256').write_text(f'{digest}  {archive.name}\n')
    print(f'{archive} ({archive.stat().st_size // (1024 * 1024)} MiB)\nsha256 {digest}')


if __name__ == '__main__':
    main()
