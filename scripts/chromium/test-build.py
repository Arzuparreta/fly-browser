#!/usr/bin/env python3
"""Build and publish a test build of a pull request for ./launch.sh --pr N.

Run it on the engine host from a clean checkout of the pull request's branch,
pushed and up to date:

  python3 scripts/chromium/test-build.py [--pr N] [--root /path/to/fly-chromium]

It applies this checkout's overlay, builds and packages native Fly exactly as
a release does (scripts/chromium/release.py), and publishes the package as the
GitHub pre-release `test-pr-N`, replacing an earlier test build of the same
pull request. Test builds are never releases: their tags do not start with
`v` (no release workflow or AUR publication runs), and Fly's update check and
fly-install read `releases/latest`, which never returns a pre-release. Each publication and each `--remove` also
delete the test builds of pull requests that are no longer open; `--remove`
deletes this pull request's test build.

The build runs the pull request's code on the engine host with the owner's
GitHub credentials, so pull requests from forks are refused unless
`--allow-fork` says their code has been read and is trusted.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

REPO = Path(__file__).resolve().parents[2]


def fail(message):
    print(f'test-build: {message}', file=sys.stderr)
    raise SystemExit(1)


def run(command, **kwargs):
    return subprocess.run(command, cwd=REPO, check=True, **kwargs)


def output(command):
    return subprocess.run(command, cwd=REPO, check=True, capture_output=True,
                          text=True).stdout.strip()


def gh_json(command):
    return json.loads(output(['gh', *command]))


def release_exists(tag):
    return subprocess.run(['gh', 'release', 'view', tag], cwd=REPO,
                          capture_output=True).returncode == 0


def delete_release(tag):
    if release_exists(tag):
        run(['gh', 'release', 'delete', tag, '--cleanup-tag', '--yes'])


def prune_closed():
    """Delete the test builds of pull requests that are no longer open."""
    releases = gh_json(['release', 'list', '--limit', '200',
                        '--json', 'tagName,isPrerelease'])
    for release in releases:
        tag = release['tagName']
        if not (release['isPrerelease'] and tag.startswith('test-pr-')):
            continue
        number = tag.removeprefix('test-pr-')
        if not number.isdigit():
            continue
        state = gh_json(['pr', 'view', number, '--json', 'state'])['state']
        if state != 'OPEN':
            print(f'Deleting {tag}: pull request #{number} is {state.lower()}.')
            delete_release(tag)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--pr', type=int,
                        help='pull request number (default: the current branch)')
    parser.add_argument('--root', type=Path, default=os.environ.get('FLY_ENGINE_ROOT'),
                        help='engine checkout (default: $FLY_ENGINE_ROOT)')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--remove', action='store_true',
                        help="delete this pull request's test build and stop")
    parser.add_argument('--allow-fork', action='store_true',
                        help='build a pull request from a fork (its code runs on this host)')
    args = parser.parse_args()

    pr = gh_json(['pr', 'view', *([str(args.pr)] if args.pr else []),
                  '--json', 'number,state,headRefOid,headRefName,url,isCrossRepository'])
    tag = f'test-pr-{pr["number"]}'
    if args.remove:
        delete_release(tag)
        prune_closed()
        print(f'Deleted the test build of pull request #{pr["number"]}.')
        return
    if pr['state'] != 'OPEN':
        fail(f'pull request #{pr["number"]} is {pr["state"].lower()}')
    if pr.get('isCrossRepository') and not args.allow_fork:
        fail(f'pull request #{pr["number"]} comes from a fork; read its code, then '
             'pass --allow-fork to build it on this host')
    if not args.root:
        fail('pass --root or set FLY_ENGINE_ROOT to the engine checkout')
    # Untracked files count too: the overlay copy and the package include them.
    if output(['git', 'status', '--porcelain']):
        fail('the checkout has uncommitted or untracked files; a test build must match a pushed commit')
    head = output(['git', 'rev-parse', 'HEAD'])
    if head != pr['headRefOid']:
        fail(f'this checkout is at {head[:12]}, but pull request #{pr["number"]} '
             f'is at {pr["headRefOid"][:12]}; check out and pull {pr["headRefName"]}')

    with tempfile.TemporaryDirectory(prefix='fly-test-build-') as dist:
        dist = Path(dist)
        run([sys.executable, str(REPO / 'scripts/chromium/native.py'), 'refresh-overlay',
             '--include-upstream', '--root', str(args.root)])
        run([sys.executable, str(REPO / 'scripts/chromium/release.py'), '--root',
             str(args.root), '--jobs', str(args.jobs), '--dist', str(dist)])
        packages = sorted(dist.glob('*-native-linux-x64.tar.gz'))
        if len(packages) != 1:
            fail(f'expected one native package in {dist}, found {len(packages)}')
        package = packages[0]
        digest = hashlib.sha256(package.read_bytes()).hexdigest()
        (dist / 'SHA256SUMS.txt').write_text(f'{digest}  {package.name}\n')
        notes = (
            f'Test build of {pr["url"]} at {head[:12]}. **Not a release.**\n\n'
            f'Start it from a checkout of this repository with '
            f'`./launch.sh --pr {pr["number"]}`. It uses the development '
            f'profile (`~/.config/fly-browser-dev`), not your Fly profile.\n\n'
            f'Fly never offers test builds as updates. This pre-release is '
            f'replaced by newer test builds of the pull request and deleted '
            f'once it is closed.\n')
        delete_release(tag)
        run(['gh', 'release', 'create', tag, '--prerelease', '--target', head,
             '--title', f'Test build: pull request #{pr["number"]} ({head[:7]})',
             '--notes', notes, str(package), str(dist / 'SHA256SUMS.txt')])
    prune_closed()
    print(f'Published {tag}. Start it anywhere with: ./launch.sh --pr {pr["number"]}')


if __name__ == '__main__':
    main()
