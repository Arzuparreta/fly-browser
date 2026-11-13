"""Exercise automatic PR gates with local Git remotes; no GitHub or engines."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def git(repo, *args):
    return subprocess.run(['git', *args], cwd=repo, check=True, text=True,
                          capture_output=True).stdout.strip()


class EngineWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='fly-engine-workflow-test-')
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.repo = self.base / 'repo'
        self.repo.mkdir()
        self.bin = self.base / 'bin'
        self.bin.mkdir()
        self.remote = self.base / 'origin.git'
        git(self.base, 'init', '--quiet', '--bare', str(self.remote))
        git(self.repo, 'init', '--quiet', '-b', 'main')
        git(self.repo, 'config', 'user.name', 'Fixture')
        git(self.repo, 'config', 'user.email', 'fixture@invalid.example')
        target = self.repo / 'scripts/chromium'
        target.mkdir(parents=True)
        shutil.copy2(ROOT / 'scripts/chromium/engine-update-ci.sh', target)
        (target / 'engine_update.py').write_text('# fixture entry point\n')
        patches = self.repo / 'chromium/patches'
        patches.mkdir(parents=True)
        (patches / 'one.patch').write_text('original context\n')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '--quiet', '-m', 'fixture')
        git(self.repo, 'remote', 'add', 'origin', str(self.remote))
        # credentials() normalises origin to HTTPS. Keep ALL fixture Git
        # calls local, including helper calls outside the child's environment.
        git(self.repo, 'config', 'url.' + self.remote.as_uri() + '.insteadOf',
            'https://github.com/fixture/Fly.git')
        git(self.repo, 'push', '--quiet', 'origin', 'main')
        self.initial = git(self.repo, 'rev-parse', 'HEAD')
        config = self.base / 'gitconfig'
        git(self.base, 'config', '--file', str(config),
            'url.' + self.remote.as_uri() + '.insteadOf', 'https://github.com/fixture/Fly.git')
        self.env = {**os.environ, 'PATH': str(self.bin) + os.pathsep + os.environ['PATH'],
                    'GIT_CONFIG_GLOBAL': str(config), 'GIT_CONFIG_NOSYSTEM': '1',
                    'GITHUB_REPOSITORY': 'fixture/Fly', 'GITHUB_RUN_ID': '123',
                    'RUNNER_TEMP': str(self.base), 'FIXTURE': str(self.base),
                    'FIXTURE_REMOTE': str(self.remote)}
        for key in ('GH_TOKEN', 'GITHUB_TOKEN', 'GIT_CONFIG_COUNT'):
            self.env.pop(key, None)
        self.program('npm', 'echo npm-test >> "$FIXTURE/events"\n')
        self.program('sleep', 'exit 0\n')
        self.program('date', 'n=$(cat "$FIXTURE/clock" 2>/dev/null || echo 0)\n'
                     'n=$((n + 1000)); echo "$n" > "$FIXTURE/clock"; echo "$n"\n')
        self.program('python3', '''
echo "python $*" >> "$FIXTURE/events"
case "$*" in
  *patch-repair*)
    if [ "${SCENARIO:-}" = semantic ]; then echo 'semantic conflict'; exit 1; fi
    printf 'repaired context\\n' > chromium/patches/one.patch ;;
esac
''')
        self.program('gh', '''
echo "gh $*" >> "$FIXTURE/events"
case "$1 $2" in
  'pr list') [ ! -e "$FIXTURE/open" ] || echo 7 ;;
  'pr create') touch "$FIXTURE/open" ;;
  'pr checks')
    if [ -n "${MOVE_MAIN_TO:-}" ] && [ ! -e "$FIXTURE/moved" ]; then
      git --git-dir="$FIXTURE_REMOTE" update-ref refs/heads/main "$MOVE_MAIN_TO"
      touch "$FIXTURE/moved"
    fi
    if [ "${SCENARIO:-}" = missing ]; then
      echo '[{"name":"engine-validation","bucket":"pass"}]'
    else
      echo '[{"name":"engine-validation","bucket":"pass"},{"name":"verify","bucket":"pass"},{"name":"impact_label","bucket":"pass"}]'
    fi ;;
  'pr view') [ "${SCENARIO:-}" != review ] || echo CHANGES_REQUESTED ;;
  'pr merge')
    git push --quiet origin HEAD:refs/heads/main
    git push --quiet origin --delete engine/chromium-beta
    rm "$FIXTURE/open" ;;
  'issue list') echo 9 ;;
  'issue view') cat "$FIXTURE/comments" 2>/dev/null || true ;;
  'issue comment')
    while [ "$1" != --body-file ]; do shift; done
    cat "$2" >> "$FIXTURE/comments" ;;
esac
''')

    def program(self, name, body):
        file = self.bin / name
        file.write_text('#!/usr/bin/env bash\nset -euo pipefail\n' + body)
        file.chmod(0o755)

    def run_beta(self, **env):
        result = subprocess.run(['bash', 'scripts/chromium/engine-update-ci.sh', 'beta'],
                                cwd=self.repo, env={**self.env, **env}, text=True,
                                capture_output=True, timeout=20)
        self.assertEqual(git(self.repo, 'status', '--porcelain'), '')
        self.assertEqual(git(self.repo, 'rev-parse', 'HEAD'), self.initial)
        self.assertEqual(len(git(self.repo, 'worktree', 'list', '--porcelain').split('worktree ')) - 1, 1)
        return result, (self.base / 'events').read_text()

    def test_merges_only_after_proof_and_all_checks_then_rechecks_main_before_closing(self):
        result, events = self.run_beta()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('--label impact:none', events)
        merge = events.index('gh pr merge')
        self.assertLess(events.index('patch-verify'), merge)
        self.assertLess(events.index('gh pr checks'), merge)
        self.assertGreater(events.rindex('patch-check --channel Beta'), merge)
        self.assertGreater(events.index('gh issue close'), events.rindex('patch-check --channel Beta'))
        self.assertNotIn(' validate --root', events)
        self.assertNotIn('release.js', events)

    def test_missing_verify_check_never_merges_or_closes_warning(self):
        result, events = self.run_beta(SCENARIO='missing')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('gh pr merge', events)
        self.assertNotIn('gh issue close', events)

    def test_requested_changes_prevent_automatic_merge(self):
        result, events = self.run_beta(SCENARIO='review')
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('gh pr merge', events)
        self.assertNotIn('gh issue close', events)

    def test_semantic_conflict_is_reported_once_without_a_pr_or_engine_operation(self):
        for _ in range(2):
            result, events = self.run_beta(SCENARIO='semantic')
            self.assertNotEqual(result.returncode, 0)
        self.assertEqual(events.count('gh issue comment'), 1)
        self.assertNotIn('gh pr create', events)
        self.assertNotIn('gh issue close', events)
        self.assertNotIn(' --root ', events)

    def test_main_moving_while_checks_run_is_merged_and_revalidated(self):
        git(self.repo, 'switch', '--quiet', '-c', 'future')
        (self.repo / 'unrelated').write_text('new main\n')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '--quiet', '-m', 'main moved')
        future = git(self.repo, 'rev-parse', 'HEAD')
        git(self.repo, 'push', '--quiet', 'origin', 'future')
        git(self.repo, 'switch', '--quiet', 'main')
        result, events = self.run_beta(MOVE_MAIN_TO=future)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertGreaterEqual(events.count('patch-verify'), 3)
        self.assertEqual(git(self.repo, 'show', 'origin/main:unrelated'), 'new main')

    def test_retry_preserves_main_changes_and_reuses_the_existing_repair_pr(self):
        result, _ = self.run_beta(SCENARIO='review')
        self.assertNotEqual(result.returncode, 0)
        git(self.repo, 'switch', '--quiet', '-c', 'future')
        (self.repo / 'unrelated').write_text('new main\n')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '--quiet', '-m', 'main moved between runs')
        git(self.repo, 'push', '--quiet', 'origin', 'HEAD:main')
        git(self.repo, 'switch', '--quiet', 'main')
        result, events = self.run_beta()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(events.count('gh pr create'), 1)
        self.assertEqual(git(self.repo, 'show', 'origin/main:unrelated'), 'new main')

    def test_retry_refuses_branch_scripts_before_running_any_of_them(self):
        result, events = self.run_beta(SCENARIO='review')
        self.assertNotEqual(result.returncode, 0)
        git(self.repo, 'fetch', '--quiet', 'origin', 'engine/chromium-beta')
        git(self.repo, 'switch', '--quiet', '-c', 'modified', 'origin/engine/chromium-beta')
        (self.repo / 'scripts/chromium/engine_update.py').write_text('unexpected branch code\n')
        git(self.repo, 'add', '.')
        git(self.repo, 'commit', '--quiet', '-m', 'unexpected script')
        git(self.repo, 'push', '--quiet', 'origin', 'HEAD:engine/chromium-beta')
        git(self.repo, 'switch', '--quiet', 'main')
        result, new_events = self.run_beta()
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn('python ', new_events[len(events):])
        self.assertNotIn('gh pr merge', new_events)


if __name__ == '__main__':
    unittest.main()
