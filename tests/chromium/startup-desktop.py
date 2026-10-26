#!/usr/bin/env python3
"""Run the opt-in startup study on a private Xvfb/Openbox desktop."""
import argparse
import ctypes as c
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import threading
import time


def stop(child):
    if child and child.poll() is None:
        os.killpg(child.pid, signal.SIGTERM)
        try:
            child.wait(timeout=10)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.wait(timeout=10)


def watch_windows(display, output, done):
    # Observe the owned WM's managed-client list without touching page focus.
    x = c.CDLL('libX11.so.6')
    pointer, ulong = c.c_void_p, c.c_ulong
    x.XOpenDisplay.argtypes = [c.c_char_p]
    x.XOpenDisplay.restype = pointer
    x.XCloseDisplay.argtypes = [pointer]
    x.XDefaultRootWindow.argtypes = [pointer]
    x.XDefaultRootWindow.restype = ulong
    x.XInternAtom.argtypes = [pointer, c.c_char_p, c.c_int]
    x.XInternAtom.restype = ulong
    x.XGetWindowProperty.argtypes = [pointer, ulong, ulong, c.c_long,
        c.c_long, c.c_int, ulong, c.POINTER(ulong), c.POINTER(c.c_int),
        c.POINTER(ulong), c.POINTER(ulong), c.POINTER(pointer)]
    x.XFree.argtypes = [pointer]
    # A client may close between the list and property reads. Xlib's default
    # BadWindow handler exits the process; a disappeared observed window is benign.
    error_handler_type = c.CFUNCTYPE(c.c_int, pointer, pointer)
    error_handler = error_handler_type(lambda _display, _event: 0)
    x.XSetErrorHandler.argtypes = [error_handler_type]
    x.XSetErrorHandler(error_handler)
    connection = x.XOpenDisplay(display.encode())
    if not connection:
        output.write(json.dumps({'error': 'Cannot monitor the owned display'}) + '\n')
        return
    root = x.XDefaultRootWindow(connection)
    atoms = {name: x.XInternAtom(connection, name.encode(), 0)
             for name in ('_NET_CLIENT_LIST', '_NET_WM_PID')}

    def prop(window, name):
        actual, count, left = ulong(), ulong(), ulong()
        fmt, data = c.c_int(), pointer()
        x.XGetWindowProperty(connection, window,
            atoms[name], 0, 1024, 0, 0,
            c.byref(actual), c.byref(fmt), c.byref(count), c.byref(left), c.byref(data))
        try:
            return list(c.cast(data, c.POINTER(ulong))[:count.value]) if data and fmt.value == 32 else []
        finally:
            if data:
                x.XFree(data)

    seen = set()
    try:
        while not done.is_set():
            for window in prop(root, '_NET_CLIENT_LIST'):
                pids = prop(window, '_NET_WM_PID')
                if not pids:
                    continue
                key = (window, tuple(pids))
                if key not in seen:
                    seen.add(key)
                    output.write(json.dumps({'epochMs': time.time() * 1000,
                        'window': window, 'pid': pids}) + '\n')
                    output.flush()
            done.wait(.005)
    finally:
        x.XCloseDisplay(connection)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--xvfb', default='Xvfb')
    parser.add_argument('--openbox', default='openbox')
    parser.add_argument('--work-dir', required=True)
    parser.add_argument('--timeout', type=int, default=1800)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command[:1] == ['--']:
        args.command.pop(0)
    if not args.command or not 1 <= args.timeout <= 7200:
        parser.error('Provide a command and a timeout of 1 to 7200 seconds')
    root = Path(args.work_dir).resolve()
    root.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='startup-desktop-', dir=root))
    server = wm = study = None
    done = threading.Event()
    watcher = None
    try:
        with (work / 'xvfb.log').open('w') as xlog, (work / 'openbox.log').open('w') as wlog, \
                (work / 'windows.jsonl').open('w') as windows:
            server = subprocess.Popen([args.xvfb, '-displayfd', '1', '-screen',
                '0', '1440x1000x24', '-nolisten', 'tcp'], stdout=subprocess.PIPE,
                stderr=xlog, text=True, start_new_session=True)
            if not select.select([server.stdout], [], [], 15)[0]:
                raise RuntimeError('Owned Xvfb startup timed out')
            number = server.stdout.readline().strip()
            if not number.isdigit():
                raise RuntimeError('Owned Xvfb failed to start')
            display = ':' + number
            env = dict(os.environ, DISPLAY=display, FLY_STARTUP_OWNED_DISPLAY='1')
            env.pop('WAYLAND_DISPLAY', None)
            wm = subprocess.Popen([args.openbox], env=env, stdout=wlog,
                stderr=subprocess.STDOUT, start_new_session=True)
            time.sleep(.5)
            if wm.poll() is not None:
                raise RuntimeError('Owned Openbox failed to start; inspect ' + str(work))
            watcher = threading.Thread(target=watch_windows,
                args=(display, windows, done), daemon=True)
            watcher.start()
            (work / 'desktop.json').write_text(json.dumps({'display': display,
                'xvfbPID': server.pid, 'openboxPID': wm.pid}, indent=2) + '\n')
            study = subprocess.Popen(args.command, env=env, start_new_session=True)
            try:
                code = study.wait(timeout=args.timeout)
            except subprocess.TimeoutExpired:
                stop(study)
                code = 124
            finally:
                done.set()
                watcher.join(timeout=5)
    finally:
        done.set()
        stop(study)
        stop(wm)
        stop(server)
    print('Owned desktop evidence:', work, flush=True)
    return code


if __name__ == '__main__':
    raise SystemExit(main())
