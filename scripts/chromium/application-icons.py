#!/usr/bin/env python3
"""Sync native application icons from the committed identity renders.

No image library is needed: ICO and ICNS embed the original PNGs losslessly.
Regenerate the identity renders with design/identity/generator/export.js first.
"""
import argparse
import json
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024)
DESTINATION = ROOT / 'chromium/overlay/fly/resources/icons'


def png(size):
    data = (ROOT / f'design/identity/assets/png/icon-{size}.png').read_bytes()
    if (data[:8] != b'\x89PNG\r\n\x1a\n' or
            struct.unpack_from('>II', data, 16) != (size, size)):
        raise ValueError(f'Invalid {size}px identity render')
    return data


def outputs():
    images = {size: png(size) for size in SIZES}
    result = {f'fly-logo{size}.png': data for size, data in images.items()}
    # Chromium's development wrapper expects this exact output filename.
    result['product_logo_48.png'] = images[48]
    result['fly-logo.svg'] = (ROOT / 'design/identity/assets/fly-icon.svg').read_bytes()
    result['fly-template22.png'] = (ROOT / 'design/identity/assets/png/icon-mono22.png').read_bytes()
    frames = [(size, data) for size, data in images.items() if size <= 256]
    offset = 6 + 16 * len(frames)
    entries = []
    for size, data in frames:
        entries.append(struct.pack('<BBBBHHII', size % 256, size % 256,
                                   0, 0, 1, 32, len(data), offset))
        offset += len(data)
    result['fly.ico'] = (struct.pack('<HHH', 0, 1, len(frames)) + b''.join(entries) +
                         b''.join(data for _, data in frames))
    # Standard and Retina representations, including 512pt @2x.
    types = {b'icp4': 16, b'icp5': 32, b'icp6': 64, b'ic07': 128,
             b'ic08': 256, b'ic09': 512, b'ic10': 1024, b'ic11': 32,
             b'ic12': 64, b'ic13': 256, b'ic14': 512}
    chunks = b''.join(kind + struct.pack('>I', 8 + len(images[size])) + images[size]
                      for kind, size in types.items())
    result['app.icns'] = b'icns' + struct.pack('>I', 8 + len(chunks)) + chunks
    result['Logo.png'] = png(600)
    result['SmallLogo.png'] = png(176)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    stale = []
    for name, data in outputs().items():
        path = DESTINATION / name
        if args.check:
            if not path.is_file() or path.read_bytes() != data:
                stale.append(str(path.relative_to(ROOT)))
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    config = json.loads((ROOT / 't3.json').read_text())
    if config.get('iconPath') != 'design/identity/assets/fly-icon.svg':
        stale.append('t3.json iconPath')
    if stale:
        print('Stale application icons; run scripts/chromium/application-icons.py:\n' +
              '\n'.join(stale), file=sys.stderr)
        return 1
    print('Application icons match the Fly identity renders.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
