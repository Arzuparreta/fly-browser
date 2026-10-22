"""Pull request test builds: what is published, refused and deleted (no GitHub, no engine)."""
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    'test_build', Path(__file__).resolve().parents[2] / 'scripts/chromium/test-build.py')
build = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build)

PR = {'number': 153, 'state': 'OPEN', 'headRefOid': 'a' * 40,
      'headRefName': 'menu-system-redesign', 'url': 'https://example.test/pull/153',
      'isCrossRepository': False}


class FakeGitHub:
    def __init__(self, releases=(), states=None, head=PR['headRefOid'], dirty='', fork=False):
        self.fork = fork
        self.releases = {tag: prerelease for tag, prerelease in releases}
        self.states = states or {}
        self.head = head
        self.dirty = dirty
        self.calls = []

    def gh_json(self, command):
        if command[:2] == ['pr', 'view']:
            if command[2].isdigit() and command[2] != '153':
                return {'state': self.states[command[2]]}
            return {**PR, 'isCrossRepository': self.fork}
        if command[:2] == ['release', 'list']:
            return [{'tagName': tag, 'isPrerelease': pre} for tag, pre in self.releases.items()]
        raise AssertionError(command)

    def output(self, command):
        if command[:2] == ['git', 'status']:
            return self.dirty
        if command[:2] == ['git', 'rev-parse']:
            return self.head
        raise AssertionError(command)

    def run(self, command, **kwargs):
        self.calls.append(command)
        # release.py writes the package into --dist.
        if any(str(part).endswith('release.py') for part in command):
            dist = Path(command[command.index('--dist') + 1])
            (dist / 'Fly-9.9.9-native-linux-x64.tar.gz').write_bytes(b'package')
        if command[:3] == ['gh', 'release', 'delete']:
            self.releases.pop(command[3])
        if command[:3] == ['gh', 'release', 'create']:
            self.releases[command[3]] = True

    def exists(self, tag):
        return tag in self.releases

    def main(self, *args):
        with mock.patch.object(build, 'gh_json', self.gh_json), \
             mock.patch.object(build, 'output', self.output), \
             mock.patch.object(build, 'run', self.run), \
             mock.patch.object(build, 'release_exists', self.exists), \
             mock.patch.object(sys, 'argv', ['test-build.py', *args]):
            build.main()


class TestBuildTest(unittest.TestCase):
    def test_refuses_builds_that_do_not_match_the_pushed_pull_request(self):
        for github, message in (
                (FakeGitHub(dirty=' M launch.sh'), 'uncommitted'),
                (FakeGitHub(dirty='?? chromium/overlay/fly/ui/new.cc'), 'untracked'),
                (FakeGitHub(head='b' * 40), 'check out and pull menu-system-redesign'),
                (FakeGitHub(fork=True), 'comes from a fork')):
            with self.subTest(message), mock.patch.object(build, 'fail', side_effect=SystemExit) as fail:
                with self.assertRaises(SystemExit):
                    github.main('--root', '/engine')
                self.assertIn(message, fail.call_args[0][0])
                self.assertEqual(github.calls, [])

    def test_publishes_a_pre_release_at_the_pull_requests_head(self):
        github = FakeGitHub(releases=[('test-pr-153', True), ('v0.29.3', False)])
        github.main('--root', '/engine')
        create = next(call for call in github.calls if call[:3] == ['gh', 'release', 'create'])
        self.assertEqual(create[3], 'test-pr-153')
        self.assertIn('--prerelease', create)
        self.assertEqual(create[create.index('--target') + 1], PR['headRefOid'])
        files = [Path(part).name for part in create if str(part).startswith('/')]
        self.assertEqual(files, ['Fly-9.9.9-native-linux-x64.tar.gz', 'SHA256SUMS.txt'])
        # The earlier test build is replaced, the release untouched.
        deletes = [call for call in github.calls if call[:3] == ['gh', 'release', 'delete']]
        self.assertEqual([call[3] for call in deletes], ['test-pr-153'])
        self.assertEqual(github.releases, {'test-pr-153': True, 'v0.29.3': False})
        self.assertLess(github.calls.index(deletes[0]), github.calls.index(create))
        # The overlay is applied before the build.
        steps = [next(str(part) for part in call if str(part).endswith('.py'))
                 for call in github.calls if call[0] == sys.executable]
        self.assertEqual([Path(step).name for step in steps], ['native.py', 'release.py'])

    def test_remove_deletes_only_this_pull_requests_test_build(self):
        github = FakeGitHub(releases=[('test-pr-153', True), ('v0.29.3', False)])
        github.main('--remove')
        self.assertEqual(github.releases, {'v0.29.3': False})

    def test_closed_pull_requests_lose_their_test_builds_and_releases_stay(self):
        github = FakeGitHub(
            releases=[('test-pr-1', True), ('test-pr-2', True), ('v0.29.3', False),
                      ('test-pr-9', False), ('v1.0.0-rc.1', True)],
            states={'1': 'MERGED', '2': 'OPEN', '9': 'CLOSED'})
        with mock.patch.object(build, 'gh_json', github.gh_json), \
             mock.patch.object(build, 'run', github.run), \
             mock.patch.object(build, 'release_exists', github.exists):
            build.prune_closed()
        # Only pre-releases named test-pr-N of pull requests no longer open.
        self.assertEqual(set(github.releases),
                         {'test-pr-2', 'v0.29.3', 'test-pr-9', 'v1.0.0-rc.1'})


if __name__ == '__main__':
    unittest.main()
