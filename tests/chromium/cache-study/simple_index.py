"""Parse a Chromium simple-cache the-real-index (format v9) into (hash, last_used_unix, size_bytes)."""
import struct, sys
MAGIC = 0x656e74657220796f
def parse(path):
    b = open(path, 'rb').read()
    o = 8  # pickle header: payload size + crc
    magic, ver = struct.unpack_from('<QI', b, o); o += 12
    assert magic == MAGIC, hex(magic)
    count, cache_size = struct.unpack_from('<QQ', b, o); o += 16
    o += 4  # write reason
    out = []
    for _ in range(count):
        h, t, packed = struct.unpack_from('<QqQ', b, o); o += 24
        out.append((h, t / 1e6 - 11644473600 if t else 0, (packed >> 8) << 8))
    modified, = struct.unpack_from('<q', b, o)
    return dict(version=ver, count=count, cache_size=cache_size, entries=out,
                modified=modified / 1e6 - 11644473600)
