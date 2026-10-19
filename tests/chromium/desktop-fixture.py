#!/usr/bin/env python3
"""Run an explicit Linux desktop fixture on an owned Xvfb/i3 display."""
import argparse
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import tempfile
import time


def stop(child):
    if child and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--work-dir', required=True)
    parser.add_argument('--xvfb', default='Xvfb')
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command[:1] == ['--']:
        args.command.pop(0)
    if not args.command or not 1 <= args.timeout <= 900:
        parser.error('Provide a fixture command and a timeout between 1 and 900 seconds')
    for executable in (args.xvfb, 'i3', 'xauth'):
        if not shutil.which(executable):
            parser.error('Missing desktop prerequisite: ' + executable)
    directory = Path(args.work_dir).resolve()
    directory.mkdir(parents=True, exist_ok=True, mode=0o700)
    evidence = Path(tempfile.mkdtemp(prefix='desktop-', dir=directory))
    with tempfile.TemporaryDirectory(prefix='fly-xvfb-') as tmp:
        authority = Path(tmp) / 'auth'
        authority.touch(mode=0o600)
        cookie = os.urandom(16).hex()
        subprocess.run(['xauth', '-f', authority, 'add', ':99', '.', cookie], check=True)
        config = Path(tmp) / 'i3.conf'
        config.write_text('font pango:monospace 8\nfor_window [class=".*"] floating enable, '
                          'border none, move position 80 80, resize set 1200 800\n')
        server = wm = test = None
        try:
            with (evidence / 'xvfb.log').open('w') as server_log, (evidence / 'wm.log').open('w') as wm_log:
                server = subprocess.Popen([args.xvfb, '-displayfd', '1', '-screen', '0',
                                           '1280x1024x24', '-nolisten', 'tcp', '-auth', authority],
                                          stdout=subprocess.PIPE, stderr=server_log,
                                          text=True, start_new_session=True)
                if not select.select([server.stdout], [], [], 15)[0]:
                    raise RuntimeError('Disposable X11 display startup timed out')
                display = server.stdout.readline().strip()
                if not display.isdigit():
                    raise RuntimeError('Disposable X11 display did not start')
                subprocess.run(['xauth', '-f', authority, 'add', ':' + display, '.', cookie], check=True)
                env = dict(os.environ, DISPLAY=':' + display, XAUTHORITY=str(authority),
                           FLY_TEST_OZONE_PLATFORM='x11')
                wm = subprocess.Popen(['i3', '-c', config], env=env, stdout=wm_log,
                                      stderr=subprocess.STDOUT, start_new_session=True)
                for attempt in range(100):
                    ready = subprocess.run(['i3-msg', '-t', 'get_version'], env=env,
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                           timeout=2, check=False)
                    if ready.returncode == 0:
                        break
                    if wm.poll() is not None:
                        raise RuntimeError('Disposable window manager exited during startup')
                    time.sleep(.1)
                else:
                    raise RuntimeError('Disposable window manager startup timed out')
                with (evidence / 'fixture.log').open('w') as log:
                    test = subprocess.Popen(args.command, env=env, stdout=log,
                                            stderr=subprocess.STDOUT, start_new_session=True)
                    try:
                        code = test.wait(timeout=args.timeout)
                    except subprocess.TimeoutExpired:
                        stop(test)
                        log.write('\nTIMEOUT: stopped the owned fixture process group.\n')
                        code = 124
        finally:
            stop(test)
            stop(wm)
            stop(server)
        print('Desktop fixture evidence:', evidence, flush=True)
        return code


if __name__ == '__main__':
    raise SystemExit(main())
