"""Validate native icon containers using their on-disk format contracts."""
from pathlib import Path
import struct
import unittest
import zlib

ICONS = Path(__file__).resolve().parents[2] / 'chromium/overlay/fly/resources/icons'


def png_dimensions(data):
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Expected a PNG representation')
    position = 8
    while position < len(data):
        length, = struct.unpack_from('>I', data, position)
        chunk = data[position + 4:position + 8 + length]
        crc, = struct.unpack_from('>I', data, position + 8 + length)
        if zlib.crc32(chunk) != crc:
            raise ValueError('PNG chunk CRC mismatch')
        position += 12 + length
    if position != len(data) or data[-8:-4] != b'IEND':
        raise ValueError('Incomplete PNG representation')
    return struct.unpack_from('>II', data, 16)


class ApplicationIconsTest(unittest.TestCase):
    def test_windows_directory_offsets_sizes_and_rgba_payloads(self):
        data = (ICONS / 'fly.ico').read_bytes()
        self.assertEqual(struct.unpack_from('<HHH', data), (0, 1, 9))
        sizes = []
        previous_end = 6 + 9 * 16
        for index in range(9):
            width, height, colors, reserved, planes, bits, length, offset = (
                struct.unpack_from('<BBBBHHII', data, 6 + 16 * index))
            size = width or 256
            self.assertEqual(height or 256, size)
            self.assertEqual((colors, reserved, planes, bits), (0, 0, 1, 32))
            self.assertEqual(offset, previous_end)
            frame = data[offset:offset + length]
            self.assertEqual(png_dimensions(frame), (size, size))
            self.assertEqual(frame, (ICONS / f'fly-logo{size}.png').read_bytes())
            previous_end = offset + length
            sizes.append(size)
        self.assertEqual(previous_end, len(data))
        self.assertEqual(sizes, [16, 20, 24, 32, 40, 48, 64, 128, 256])

    def test_mac_standard_and_retina_representations_have_full_png_chunks(self):
        data = (ICONS / 'app.icns').read_bytes()
        self.assertEqual(data[:4], b'icns')
        self.assertEqual(struct.unpack_from('>I', data, 4)[0], len(data))
        expected = {b'icp4': 16, b'icp5': 32, b'icp6': 64, b'ic07': 128,
                    b'ic08': 256, b'ic09': 512, b'ic10': 1024, b'ic11': 32,
                    b'ic12': 64, b'ic13': 256, b'ic14': 512}
        position = 8
        seen = set()
        while position < len(data):
            kind, length = struct.unpack_from('>4sI', data, position)
            self.assertNotIn(kind, seen)
            self.assertGreater(length, 8)
            frame = data[position + 8:position + length]
            size = expected[kind]
            self.assertEqual(png_dimensions(frame), (size, size))
            self.assertEqual(frame, (ICONS / f'fly-logo{size}.png').read_bytes())
            seen.add(kind)
            position += length
        self.assertEqual(position, len(data))
        self.assertEqual(seen, expected.keys())

    def test_windows_start_tile_pngs_preserve_full_resolution(self):
        self.assertEqual(png_dimensions((ICONS / 'Logo.png').read_bytes()), (600, 600))
        self.assertEqual(png_dimensions((ICONS / 'SmallLogo.png').read_bytes()), (176, 176))


if __name__ == '__main__':
    unittest.main()
