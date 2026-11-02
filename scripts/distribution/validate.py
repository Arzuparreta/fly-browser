#!/usr/bin/env python3
"""Exercise real signed package repositories in disposable distro containers.

First generate repositories with linux.py; the site is built offline from its signed pool.
No tests modify the host package manager, profile, running browser or keyring.
Containers get SYS_ADMIN and unconfined test policies solely so Chromium can
create its normal namespace sandbox inside Docker. --no-sandbox is never used.
"""
import argparse
import concurrent.futures
import functools
import http.server
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import threading
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]
MATRIX = {
    'debian12': ('debian:12', 'apt'),
    'ubuntu22': ('ubuntu:22.04', 'apt'),
    'ubuntu24': ('ubuntu:24.04', 'apt'),
    'fedora': ('fedora:44', 'dnf'),
    'opensuse': ('opensuse/tumbleweed:latest', 'zypper'),
    'arch': ('archlinux:latest', 'pacman'),
}
SMOKE = '''
test "$(stat -c %a /opt/fly-browser/chrome-sandbox)" = 4755
test "$(stat -c %u /opt/fly-browser/chrome-sandbox)" = 0
test "$(cat /opt/fly-browser/INSTALLATION)" = system
test -f /usr/share/applications/fly-browser.desktop
useradd -m flycheck
su -s /bin/sh flycheck -c 'dbus-run-session -- timeout 120s fly-browser --headless --disable-gpu --dump-dom "data:text/html,<title>Fly package smoke</title>"' > /tmp/fly-dom
grep -q 'Fly package smoke' /tmp/fly-dom
mkdir -p /home/flycheck/.config/fly-browser
echo retained > /home/flycheck/.config/fly-browser/acceptance-marker
'''


def script(manager, url):
    if manager == 'apt':
        install = f'''
export DEBIAN_FRONTEND=noninteractive
echo force-unsafe-io > /etc/dpkg/dpkg.cfg.d/fly-disposable-test
apt-get update -qq
apt-get install -y --no-install-recommends ca-certificates gpgv /delivery/flat/*.deb
sed -i 's|^URIs:.*|URIs: {url}/flat/|' /etc/apt/sources.list.d/fly-browser.sources
apt-get update
apt-cache policy fly-browser-bin | grep '700.*127.0.0.1'
apt-get install --reinstall -y fly-browser-bin
cp /etc/apt/sources.list.d/fly-browser.sources /tmp/fly.sources
sed -i 's|^URIs:.*|URIs: {url}/tampered-metadata/flat/|' /etc/apt/sources.list.d/fly-browser.sources
if apt-get update -o APT::Update::Error-Mode=any; then echo 'Accepted modified signed metadata'; exit 1; fi
sed -i 's|^URIs:.*|URIs: {url}/tampered-payload/flat/|' /etc/apt/sources.list.d/fly-browser.sources
apt-get update
apt-get clean
if apt-get install --reinstall -y fly-browser-bin; then echo 'Accepted modified package'; exit 1; fi
cp /tmp/fly.sources /etc/apt/sources.list.d/fly-browser.sources
'''
        remove = 'apt-get purge -y fly-browser-bin'
    elif manager == 'dnf':
        install = f'''
dnf install -y util-linux shadow-utils dbus-daemon dbus-tools
rpm --import /delivery/site/packages/release-key.asc
dnf install -y --setopt=localpkg_gpgcheck=1 /delivery/flat/*.rpm
sed -i 's|^baseurl=.*|baseurl={url}/site/packages/rpm/|' /etc/yum.repos.d/fly-browser.repo
dnf reinstall -y fly-browser-bin
'''
        remove = 'dnf remove -y fly-browser-bin'
    elif manager == 'zypper':
        install = f'''
zypper --non-interactive install util-linux shadow dbus-1-daemon
rpm --import /delivery/site/packages/release-key.asc
zypper --non-interactive install /delivery/flat/*.rpm
sed -i 's|^baseurl=.*|baseurl={url}/site/packages/rpm/|' /etc/zypp/repos.d/fly-browser.repo
zypper --non-interactive --gpg-auto-import-keys refresh fly-browser
zypper --non-interactive install --force --from fly-browser fly-browser-bin
'''
        remove = 'zypper --non-interactive remove fly-browser-bin'
    else:
        install = f'''
pacman-key --init
pacman-key --populate archlinux
pacman-key --add /delivery/site/packages/release-key.gpg
key=$(gpg --batch --with-colons --show-keys /delivery/site/packages/release-key.gpg | awk -F: '$1 == "fpr" {{print $10; exit}}')
pacman-key --lsign-key "$key"
pacman -Syu --noconfirm
pacman -U --noconfirm /delivery/flat/*.pkg.tar.zst
sed -i 's|^Server =.*|Server = {url}/flat|' /etc/pacman.d/fly-browser.conf
printf '\\nInclude = /etc/pacman.d/fly-browser.conf\\n' >> /etc/pacman.conf
pacman -Syu --noconfirm fly-browser-bin
'''
        remove = 'pacman -R --noconfirm fly-browser-bin\npacman -Q >/dev/null'
    return 'set -eux\n' + install + SMOKE + '\n' + remove + '''
test ! -e /opt/fly-browser/fly-browser
test "$(cat /home/flycheck/.config/fly-browser/acceptance-marker)" = retained
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repository', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--distros', nargs='+', choices=MATRIX, default=list(MATRIX))
    parser.add_argument('--port', type=int, default=18927)
    parser.add_argument('--jobs', type=int, default=2)
    args = parser.parse_args()
    repository = args.repository.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    # Exercise the same verified RPM tree that the Pages workflow deploys,
    # without modifying the supplied repository or relying on external URLs.
    served = args.output.resolve() / 'served'
    served.mkdir()
    (served / 'flat').mkdir()
    for file in (repository / 'flat').iterdir():
        if file.is_file():
            try:
                os.link(file, served / 'flat' / file.name)
            except PermissionError:
                shutil.copyfile(file, served / 'flat' / file.name)
    subprocess.run([sys.executable, str(ROOT / 'scripts/distribution/pages-build.py'),
        '--root', str(repository / 'site'), '--output', str(served / 'site'),
        '--local-pool', str(repository / 'flat')], check=True)
    repository = served
    class Handler(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):
            mode = None
            for prefix in ['tampered-metadata', 'tampered-payload']:
                if self.path.startswith('/' + prefix + '/'):
                    mode = prefix
                    self.path = self.path[len(prefix) + 1:]
                    break
            path = Path(self.translate_path(urllib.parse.urlsplit(self.path).path))
            if mode == 'tampered-metadata' and path.name == 'InRelease':
                body = path.read_bytes().replace(b'Origin: Fly Browser', b'Origin: altered')
            elif mode == 'tampered-payload' and path.suffix == '.deb':
                self.send_response(200)
                self.send_header('Content-Length', str(path.stat().st_size + 1))
                self.end_headers()
                try:
                    with path.open('rb') as file:
                        self.copyfile(file, self.wfile)
                    self.wfile.write(b'0')
                except (BrokenPipeError, ConnectionResetError):
                    pass  # Expected: APT aborts the altered-size download.
                return
            else:
                return super().do_GET()
            self.send_response(200)
            self.send_header('Content-Length', str(len(body)))
            self.end_headers()
            self.wfile.write(body)
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port),
        functools.partial(Handler, directory=str(repository)))
    threading.Thread(target=server.serve_forever, daemon=True).start()
    def test(name):
        image, manager = MATRIX[name]
        logfile = args.output / f'{name}.log'
        print(f'Starting {name}: {image}', flush=True)
        with logfile.open('w') as output:
            result = subprocess.run(['docker', 'run', '--rm', '--network', 'host',
                '--cap-add', 'SYS_ADMIN', '--security-opt', 'seccomp=unconfined',
                '--security-opt', 'apparmor=unconfined', '-v', f'{repository}:/delivery:ro',
                image, 'sh', '-c', script(manager, f'http://127.0.0.1:{args.port}')],
                stdout=output, stderr=subprocess.STDOUT)
        print(f'{name}: {"PASS" if result.returncode == 0 else "FAIL"} ({logfile})', flush=True)
        return name, {'passed': result.returncode == 0, 'image': image, 'log': str(logfile)}
    try:
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            results = dict(pool.map(test, args.distros))
        (args.output / 'summary.json').write_text(json.dumps(results, indent=2) + '\n')
        if not all(result['passed'] for result in results.values()):
            raise SystemExit(1)
    finally:
        server.shutdown()


if __name__ == '__main__':
    main()
