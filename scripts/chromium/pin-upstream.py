#!/usr/bin/env python3
"""Resolve official Stable/Linux source pins; does not update a checkout."""

import argparse
import base64
import datetime
import hashlib
import json
from pathlib import Path
import re
import urllib.request

DASH = 'https://chromiumdash.appspot.com/fetch_releases?channel=Stable&platform=Linux&num=1'
SOURCE = 'https://chromium.googlesource.com/chromium/src'
TOOLS = 'https://chromium.googlesource.com/chromium/tools/depot_tools'


def fetch(url):
    with urllib.request.urlopen(url, timeout=60) as response:
        data = response.read(8 * 1024 * 1024 + 1)
        if len(data) > 8 * 1024 * 1024:
            raise ValueError('Official response exceeds expected size')
        return data


def gitiles_json(url):
    raw = fetch(url)
    if not raw.startswith(b")]}'\n"):
        raise ValueError('Invalid Gitiles JSON response')
    return json.loads(raw[5:])


def resolve():
    release = json.loads(fetch(DASH))[0]
    if release['channel'] != 'Stable' or release['platform'] != 'Linux':
        raise ValueError('Release endpoint did not return Stable/Linux')
    chromium = release['hashes']['chromium']
    tools = gitiles_json(TOOLS + '/+/HEAD?format=JSON')['commit']
    for sha in (chromium, tools):
        if not re.fullmatch('[0-9a-f]{40}', sha):
            raise ValueError('Invalid revision from official endpoint')
    deps = base64.b64decode(fetch(SOURCE + '/+/' + chromium + '/DEPS?format=TEXT'), validate=True)
    version = base64.b64decode(fetch(SOURCE + '/+/' + chromium + '/chrome/VERSION?format=TEXT'), validate=True)
    fields = dict(line.split('=', 1) for line in version.decode().strip().splitlines())
    actual = '.'.join(fields[key] for key in ('MAJOR', 'MINOR', 'BUILD', 'PATCH'))
    if actual != release['version']:
        raise ValueError('Pinned source VERSION differs from the Stable release record')
    return {
        'schema': 1,
        'resolved_at': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'release_source': DASH, 'channel': release['channel'], 'platform': release['platform'],
        'release_time': release['time'], 'engine_version': actual,
        'chromium': {'url': SOURCE + '.git', 'revision': chromium,
                     'deps_sha256': hashlib.sha256(deps).hexdigest()},
        'depot_tools': {'url': TOOLS + '.git', 'revision': tools},
    }


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True,
                        help='new lock path (must not already exist); review before replacing the current lock')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists; refusing to replace an upstream pin implicitly')
    result = resolve()
    with args.output.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print('Resolved official source and tools to ' + str(args.output))
