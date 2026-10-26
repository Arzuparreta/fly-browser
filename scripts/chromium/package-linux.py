#!/usr/bin/env python3
"""Package native Fly for Linux from a release build.

  python3 scripts/chromium/package-linux.py --root /path/to/engine \\
      [--output out/FlyRelease] [--dist dist]

Produces dist/Fly-<version>-native-linux-x64.tar.gz (and .sha256) holding a
fly-browser/ directory: the browser (app/fly-browser) and its runtime files,
Fly's fonts, What's New page, user content worker and bundled extensions, and
an install.sh that installs it for the current user as "Fly".
The release build starts Fly by itself (fly_native_by_default) and keeps its
data in $XDG_CONFIG_HOME/fly-browser (default ~/.config/fly-browser).
"""
import argparse
import hashlib
import json
import platform
import re
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
            'fly_resources/whats-new.html', 'fly_resources/icons/fly-logo48.png',
            'fly_resources/icons/fly-logo128.png',
            'fly_resources/icons/fly-logo256.png', 'chrome_sandbox']

# Build outputs that the package names after Fly. The engine looks for
# fly-crashpad next to the browser (0041-native-desktop-identity.patch).
PACKAGED_NAMES = {'chrome': 'fly-browser', 'chrome_crashpad_handler': 'fly-crashpad'}

# Earlier releases ran app/fly through a launcher script. Their in-app update
# refuses a package without app/fly, a browser of theirs restarts through that
# path after an update or a package upgrade, and launch.sh starts it.
COMPAT_FLY = '''#!/bin/sh
# Starts fly-browser where earlier releases started this file.
exec "$(dirname "$(readlink -f "$0")")/fly-browser" "$@"
'''

INSTALL = r'''#!/bin/sh
# Installs Fly for the current user.
#   ./install.sh            install or update
#   ./install.sh --remove   uninstall (keeps your data)
set -eu
HERE=$(dirname "$(readlink -f "$0")")
DATA="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET="$DATA/fly-browser"
BIN="$HOME/.local/bin"
APPS="$DATA/applications"
ICONS="$DATA/icons/hicolor"
if [ "${1:-}" = "--remove" ]; then
  rm -rf "$TARGET" "$APPS/fly-browser.desktop"
  for link in "$BIN/fly-browser"; do
    if [ "$(readlink "$link" 2>/dev/null || true)" = "$TARGET/fly-browser" ]; then rm -f "$link"; fi
  done
  for size in 48 128 256; do
    rm -f "$ICONS/${size}x${size}/apps/fly-browser.png"
  done
  echo "Removed native Fly. Your data in ${XDG_CONFIG_HOME:-$HOME/.config}/fly-browser is kept."
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
ln -sf "$TARGET/fly-browser" "$BIN/fly-browser"
for size in 48 128 256; do
  mkdir -p "$ICONS/${size}x${size}/apps"
  cp "$TARGET/icons/fly-logo$size.png" "$ICONS/${size}x${size}/apps/fly-browser.png"
done
# Escape both the quoted Desktop Entry argument and the entry's string value.
escaped=$(printf '%s' "$TARGET/fly-browser" | sed 's/[\\"`$]/\\&/g; s/\\/\\\\/g; s/%/%%/g')
{
  sed '/^Exec=/d' "$TARGET/fly-browser.desktop"
  printf 'Exec="%s" %%U\n' "$escaped"
} > "$APPS/fly-browser.desktop"
command -v update-desktop-database >/dev/null 2>&1 && update-desktop-database "$APPS" || true
echo "Installed native Fly in $TARGET."
echo "Start it from your applications menu (Fly) or with: fly-browser"
'''

DESKTOP = '''[Desktop Entry]
Name=Fly
GenericName=Web Browser
Comment=Fly on the Chromium engine
Keywords=browser;web;internet;
Exec=@EXEC@ %U
Icon=fly-browser
Terminal=false
Type=Application
Categories=Network;WebBrowser;
MimeType=text/html;text/xml;application/xhtml+xml;x-scheme-handler/http;x-scheme-handler/https;
StartupWMClass=Fly
'''


def fail(message):
    print(f'package-linux: {message}', file=sys.stderr)
    raise SystemExit(1)


def check_release_build(args_gn):
    """Packages hold a static build that starts Fly without a launcher."""
    if re.search(r'^\s*is_component_build\s*=\s*true\s*$', args_gn, re.M):
        fail('this is a component build; package a release build '
             '(chromium/gn/release.gnargs)')
    if not re.search(r'^\s*fly_native_by_default\s*=\s*true\s*$', args_gn, re.M):
        fail('this build does not start Fly by itself; package a release build '
             '(chromium/gn/release.gnargs)')


def normalize_package_permissions(root):
    """Public runtime assets must survive a restrictive build-host umask."""
    for file in [root, *root.rglob('*')]:
        if file.is_symlink():
            fail(f'Unexpected symlink in native package: {file}')
        file.chmod(0o755 if file.is_dir() or file.stat().st_mode & 0o111 else 0o644)


def read_build_optimizations(build):
    record = build / 'fly-build.json'
    if not record.exists():
        return None
    optimization = json.loads(record.read_text())
    digest = hashlib.sha256()
    with (build / 'chrome').open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    if optimization.get('binary_sha256') != digest.hexdigest():
        fail('build optimization record does not match this executable; rebuild it')
    if optimization.get('args_sha256') != hashlib.sha256(
            (build / 'args.gn').read_bytes()).hexdigest():
        fail('build optimization record does not match these arguments; rebuild it')
    return optimization


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
    check_release_build((build / 'args.gn').read_text(encoding='utf-8'))
    optimization = read_build_optimizations(build)
    version = json.loads((REPO / 'package.json').read_text())['version']
    engine = json.loads((REPO / 'chromium' / 'upstream.lock.json').read_text())['engine_version']
    name = f'Fly-{version}-native-linux-x64'
    args.dist.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        top = Path(tmp) / 'fly-browser'
        app = top / 'app'
        app.mkdir(parents=True)
        for file in RUNTIME_FILES:
            if (build / file).is_file():
                shutil.copy2(build / file, app / PACKAGED_NAMES.get(file, file))
        if (build / 'chrome_sandbox').is_file():
            shutil.copy2(build / 'chrome_sandbox', app / 'chrome-sandbox')
        # Release builds keep the symbol table (about a third of the browser
        # binary); packages do not need it.
        strip = shutil.which('strip')
        if strip:
            for binary in ('fly-browser', 'fly-crashpad', 'chrome-sandbox'):
                if (app / binary).is_file():
                    subprocess.run([strip, str(app / binary)], check=True)
        for directory in RUNTIME_DIRS:
            if (build / directory).is_dir():
                shutil.copytree(build / directory, app / directory, symlinks=False)
        # The desktop menu entry and the AUR package install these icons.
        shutil.copytree(app / 'fly_resources' / 'icons', app / 'icons')
        subprocess.run([sys.executable, str(REPO / 'scripts/chromium/bundled-extensions.py'),
                        '--out', str(app / 'fly_extensions'),
                        '--cache', str(args.extensions_cache)], check=True)
        shutil.copy2(REPO / 'LICENSE', app / 'LICENSE')
        provenance = {
            'product_version': version,
            'engine': json.loads((REPO / 'chromium/upstream.lock.json').read_text()),
            'repository_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=REPO, text=True).strip(),
            'repository_dirty': bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=no'], cwd=REPO, text=True).strip()),
            'binary_sha256': hashlib.sha256((app / 'fly-browser').read_bytes()).hexdigest(),
            'build_optimizations': optimization,
        }
        (app / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
        compat = app / 'fly'
        compat.write_text(COMPAT_FLY, encoding='utf-8')
        compat.chmod(0o755)
        (app / 'fly-browser.desktop').write_text(DESKTOP, encoding='utf-8')
        (app / 'VERSION').write_text(f'Fly {version}\nEngine Chromium {engine}\n',
                                     encoding='utf-8')
        installer = top / 'install.sh'
        installer.write_text(INSTALL, encoding='utf-8')
        installer.chmod(0o755)
        normalize_package_permissions(top)
        archive = args.dist / f'{name}.tar.gz'
        with tarfile.open(archive, 'w:gz') as bundle:
            bundle.add(top, arcname='fly-browser')
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (args.dist / f'{name}.tar.gz.sha256').write_text(f'{digest}  {archive.name}\n')
    print(f'{archive} ({archive.stat().st_size // (1024 * 1024)} MiB)\nsha256 {digest}')


if __name__ == '__main__':
    main()
