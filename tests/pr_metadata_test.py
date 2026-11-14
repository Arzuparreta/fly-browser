"""Execute the real metadata workflow against a disposable GitHub API fixture."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = (ROOT / '.github/workflows/pr-guardrails.yml').read_text()
PROGRAM = textwrap.dedent(WORKFLOW.split('        run: |\n', 1)[1])


def commit(subject, sha='a' * 40):
    return {'sha': sha, 'commit': {'message': subject}}


class PullMetadataTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-pr-metadata-')
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        gh = self.root / 'gh'
        gh.write_text('''#!/usr/bin/env python3
import os,sys
from pathlib import Path
endpoint=next(arg for arg in sys.argv[1:] if arg.startswith('repos/'))
if endpoint.endswith('/commits'):
    assert '--paginate' in sys.argv and '--slurp' in sys.argv
    if os.environ.get('FAIL_COMMITS'): sys.exit(4)
    name='commits.json'
else:
    name='pull.json'
print((Path(os.environ['FIXTURE'])/name).read_text())
''')
        gh.chmod(0o755)

    def check(self, title='fix(ci): validate metadata', labels=('impact:none',),
              pages=None, fail_commits=False, expected_count=None):
        if pages is None:
            pages = [[commit('fix(ci): validate metadata')]]
        pull = {'title': title, 'labels': [{'name': name} for name in labels],
                'commits': sum(map(len, pages)) if expected_count is None else expected_count}
        (self.root / 'pull.json').write_text(json.dumps(pull))
        (self.root / 'commits.json').write_text(json.dumps(pages))
        env = {**os.environ, 'PATH': str(self.root) + os.pathsep + os.environ['PATH'],
               'FIXTURE': str(self.root), 'GITHUB_REPOSITORY': 'fixture/Fly',
               'PR': '7', 'GH_TOKEN': 'disposable-fixture',
               'FAIL_COMMITS': '1' if fail_commits else ''}
        return subprocess.run(['bash', '-e', '-c', PROGRAM], env=env, text=True,
                              capture_output=True, timeout=10)

    def test_scopes_breaking_markers_bodies_and_all_documented_types(self):
        subjects = [kind + '(scope): descripción' for kind in
                    ('feat', 'fix', 'docs', 'style', 'refactor', 'perf', 'test',
                     'build', 'ci', 'chore', 'revert')]
        subjects += ['fix: repair it', 'feat(api)!: change contract\n\nBREAKING CHANGE: details',
                     'chore(merge): integrate origin/main']
        result = self.check(title='feat!: change contract',
                            labels=('reviewed', 'impact:major'),
                            pages=[[commit(subject) for subject in subjects]])
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_title_never_passes_with_valid_commits_and_label(self):
        for title in ('Fix validation', 'Fix(ci): wrong type', 'fix(): empty scope',
                      'fix(ci): ', 'fix(ci):missing space', 'feat(two words): scope',
                      'fix(ci): first\nsecond'):
            with self.subTest(title=title):
                result = self.check(title=title)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('PR title must use', result.stderr)

    def test_every_commit_is_checked_including_later_pages_and_merges(self):
        for subject in ('old style message', 'Merge branch main'):
            result = self.check(pages=[
                [commit('fix(ci): valid', str(i).zfill(40)) for i in range(100)],
                [commit(subject, 'b' * 40)]])
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Commit bbbbbbbbbbbb', result.stderr)

    def test_missing_unknown_and_conflicting_impact_labels_fail(self):
        for labels in ((), ('impact:typo',), ('impact:patch', 'impact:none'),
                       ('impact:patch', 'impact:typo')):
            with self.subTest(labels=labels):
                result = self.check(labels=labels)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Add exactly one', result.stderr)

    def test_empty_commit_results_or_api_failure_cannot_pass(self):
        self.assertNotEqual(self.check(pages=[]).returncode, 0)
        self.assertNotEqual(self.check(fail_commits=True).returncode, 0)

    def test_api_commit_limit_cannot_silently_skip_unseen_subjects(self):
        result = self.check(pages=[[commit('fix(ci): valid')] * 250], expected_count=251)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Incomplete PR commit list', result.stderr)

    def test_metadata_is_data_and_never_executed(self):
        marker = self.root / 'must-not-exist'
        result = self.check(title='fix(ci): $(touch ' + str(marker) + ')')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(marker.exists())

    def test_title_edits_are_metadata_events_not_verification_events(self):
        self.assertIn('synchronize, edited, labeled, unlabeled', WORKFLOW)
        self.assertNotIn('npm test', PROGRAM)


if __name__ == '__main__':
    unittest.main()
