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
      'headRefName': 'menu-system-redesign', 'url': 'https://example.test/pull/153'}


class FakeGitHub:
    def __init__(self, releases=(), states=None, head=PR['headRefOid'], dirty=''):
        self.releases = {tag: prerelease for tag, prerelease in releases}
        self.states = states or {}
        self.head = head
        self.dirty = dirty
        self.calls = []

    def gh_json(self, command):
        if command[:2] == ['pr', 'view']:
            if command[2].isdigit() and command[2] != '153':
                return {'state': self.states[command[2]]}
            return dict(PR)
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
                (FakeGitHub(head='b' * 40), 'check out and pull menu-system-redesign')):
            with self.subTest(message), mock.patch.object(build, 'fail', side_effect=SystemExit) as fail:
                with self.assertRaises(SystemExit):
                    github.main('--root', '/engine')
                self.assertIn(message, fail.call_args[0][0])
                self.assertEqual(github.calls, [])

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
