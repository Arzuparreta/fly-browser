#!/usr/bin/env python3
"""Send physical input only to the private display owned by desktop-fixture.py."""
import argparse
import ctypes
import os
from pathlib import Path
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--click', nargs=2, type=int, metavar=('X', 'Y'))
    group.add_argument('--keys', nargs='+', metavar='KEYSYM')
    args = parser.parse_args()
    authority = Path(os.environ.get('XAUTHORITY', '')).resolve(strict=True)
    if (not authority.parent.name.startswith('fly-xvfb-') or
            os.environ.get('FLY_TEST_OZONE_PLATFORM') != 'x11' or
            not os.environ.get('DISPLAY', '').startswith(':')):
        parser.error('Physical input requires the owned disposable X11 display')
    x = ctypes.CDLL('libX11.so.6')
    xt = ctypes.CDLL('libXtst.so.6')
    x.XOpenDisplay.argtypes = [ctypes.c_char_p]
    x.XOpenDisplay.restype = ctypes.c_void_p
    x.XCloseDisplay.argtypes = [ctypes.c_void_p]
    x.XSync.argtypes = [ctypes.c_void_p, ctypes.c_int]
    x.XStringToKeysym.argtypes = [ctypes.c_char_p]
    x.XStringToKeysym.restype = ctypes.c_ulong
    x.XKeysymToKeycode.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    x.XKeysymToKeycode.restype = ctypes.c_uint
    xt.XTestFakeMotionEvent.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                      ctypes.c_int, ctypes.c_int, ctypes.c_ulong]
    xt.XTestFakeButtonEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                      ctypes.c_int, ctypes.c_ulong]
    xt.XTestFakeKeyEvent.argtypes = [ctypes.c_void_p, ctypes.c_uint,
                                   ctypes.c_int, ctypes.c_ulong]
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('Cannot open the disposable display')
    held = []
    pointer_held = False
    try:
        if args.click:
            left, top = args.click
            if not (0 <= left < 1280 and 0 <= top < 1024):
                parser.error('Click is outside the disposable desktop')
            if not xt.XTestFakeMotionEvent(display, -1, left, top, 0):
                raise RuntimeError('Pointer motion failed')
            x.XSync(display, 0)
            time.sleep(.15)
            if not xt.XTestFakeButtonEvent(display, 1, 1, 0):
                raise RuntimeError('Pointer press failed')
            pointer_held = True
            x.XSync(display, 0)
            time.sleep(.1)
            if not xt.XTestFakeButtonEvent(display, 1, 0, 0):
                raise RuntimeError('Pointer release failed')
            pointer_held = False
        else:
            codes = [x.XKeysymToKeycode(display, x.XStringToKeysym(key.encode('ascii')))
                     for key in args.keys]
            if not all(codes):
                parser.error('Unknown X11 keysym')
            for code in codes:
                if not xt.XTestFakeKeyEvent(display, code, 1, 0):
                    raise RuntimeError('Key press failed')
                held.append(code)
    finally:
        if pointer_held:
            xt.XTestFakeButtonEvent(display, 1, 0, 0)
        for code in reversed(held):
            xt.XTestFakeKeyEvent(display, code, 0, 0)
        x.XSync(display, 0)
        x.XCloseDisplay(display)


if __name__ == '__main__':
    main()
