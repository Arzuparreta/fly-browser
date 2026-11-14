"""Provider policy, connection preservation and shared isolated tool contract."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/chromium'))
import repair_agents
import repair_sandbox
import repair_tools


class PolicyTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.workspace = self.root / 'workspace'
        self.workspace.mkdir()
        self.schema, self.output, self.log = [self.root / name for name in ('schema', 'result.json', 'run.log')]
        self.schema.write_text('{}')
        self.probe = patch.object(repair_tools, 'verify')
        self.probe.start()
        self.addCleanup(self.probe.stop)

    def run_agent(self, writable=False):
        return repair_agents.run(self.workspace, [], 'task', self.schema, self.output,
                                 self.log, writable, 120)

    def test_exact_models_effort_and_claude_preference(self):
        self.assertEqual(repair_agents.POLICY, [('claude', 'claude-opus-5-5', 'high'),
                                              ('codex', 'gpt-6.1-sol', 'high')])
        with patch.object(repair_agents, 'claude', return_value={'approved': True}) as claude, \
                patch.object(repair_sandbox, 'codex') as codex:
            self.assertEqual(self.run_agent(), {'approved': True})
        claude.assert_called_once()
        codex.assert_not_called()

    def test_quota_fallback_discards_partial_proposal_and_preserves_modes(self):
        source = self.workspace / 'adapter.cc'
        source.write_text('original')
        source.chmod(0o755)
        extra = self.workspace / 'unapproved.cc'
        nested_git = self.workspace / 'chromium/overlay/fly/.git/config'
        def quota(*args):
            source.write_text('partial Claude proposal')
            extra.write_text('unapproved addition')
            args[5].write_text(json.dumps({'type': 'result', 'is_error': True,
                                         'result': "You've hit your limit"}))
            nested_git.parent.mkdir(parents=True)
            nested_git.write_text('unapproved nested repository')
            raise repair_agents.AgentUnavailable('Claude usage limit')
        def fallback(*args, **kwargs):
            self.assertEqual(source.read_text(), 'original')
            self.assertTrue(source.stat().st_mode & 0o111)
            self.assertFalse(extra.exists())
            self.assertFalse(nested_git.exists())
            self.assertEqual(kwargs['connection_options'], ['trusted-proxy'])
            return {'result': 'repaired'}
        with patch.object(repair_agents, 'claude', side_effect=quota), \
                patch.object(repair_agents, 'codex_connection', return_value=('codex', ['trusted-proxy'], {})), \
                patch.object(repair_sandbox, 'codex', side_effect=fallback):
            self.assertEqual(self.run_agent(writable=True), {'result': 'repaired'})
        records = json.loads(self.output.with_suffix('.agent.json').read_text())
        self.assertEqual([record['provider'] for record in records], ['claude', 'codex'])

    def test_rejected_review_never_asks_another_provider_for_approval(self):
        verdict = {'approved': False, 'findings': ['lost desktop identity']}
        with patch.object(repair_agents, 'claude', return_value=verdict), \
                patch.object(repair_sandbox, 'codex') as codex:
            self.assertEqual(self.run_agent(), verdict)
        codex.assert_not_called()

    def test_transport_or_schema_bug_does_not_silently_change_provider(self):
        with patch.object(repair_agents, 'claude', side_effect=RuntimeError('Unexpected exposed tool')), \
                patch.object(repair_sandbox, 'codex') as codex, \
                self.assertRaisesRegex(RuntimeError, 'Unexpected exposed tool'):
            self.run_agent()
        codex.assert_not_called()

    def test_tool_inventory_failure_takes_precedence_over_quota(self):
        self.log.write_text('\n'.join(map(json.dumps, [
            {'type': 'system', 'subtype': 'init', 'model': 'claude-opus-5-5',
             'tools': ['Bash', 'mcp__fly_repair__exec']},
            {'type': 'assistant', 'error': 'rate_limit'}])))
        with patch.object(repair_sandbox, 'run', side_effect=RuntimeError('client failed')), \
                self.assertRaisesRegex(RuntimeError, 'unexpected tools/model'):
            repair_agents.claude_call([], self.workspace, self.log, 60, {}, 'task')

    def test_claude_client_quota_is_classified_before_schema_validation(self):
        self.log.write_text(json.dumps({'type': 'assistant', 'error': 'rate_limit'}))
        with patch.object(repair_sandbox, 'run', side_effect=RuntimeError('client failed')), \
                self.assertRaises(repair_agents.AgentUnavailable):
            repair_agents.claude_call([], self.workspace, self.log, 60, {}, 'task')

    def test_reviewer_never_inherits_author_scratch_home(self):
        scratch = self.workspace / '.repair-runtime/.local/lib/python/site-packages'
        scratch.mkdir(parents=True)
        (scratch / 'usercustomize.py').write_text('raise SystemExit("author code")')
        with patch.object(repair_agents, 'claude', return_value={'approved': True}):
            self.run_agent()
        self.assertFalse(scratch.exists())

    def test_unavailable_clients_defer_without_a_third_provider(self):
        with patch.object(repair_agents, 'claude', side_effect=repair_agents.AgentUnavailable('No Claude quota')), \
                patch.object(repair_agents, 'codex_connection', side_effect=repair_agents.AgentUnavailable('No proxy')), \
                self.assertRaises(repair_agents.AgentUnavailable):
            self.run_agent()
        self.assertEqual(len(json.loads(self.log.read_text())['attempts']), 2)

    def test_explicitly_disabled_t3_provider_is_unavailable(self):
        settings = self.root / 'userdata/settings.json'
        settings.parent.mkdir()
        settings.write_text(json.dumps({'providerInstances': {
            'claudeAgent': {'enabled': False, 'config': {}}}}))
        with patch.dict(os.environ, {'T3CODE_HOME': str(self.root)}), \
                self.assertRaisesRegex(repair_agents.AgentUnavailable, 'disabled'):
            repair_agents.t3_provider('claudeAgent')

    def test_previous_result_and_client_failure_cannot_satisfy_a_new_run(self):
        self.output.write_text('{"approved":true}')
        self.output.with_suffix('.agent.json').write_text('[]')
        client_log = self.log.with_name('run-claude.log')
        client_log.write_text(json.dumps({'type': 'assistant', 'error': 'rate_limit'}))
        with patch.object(repair_agents, 'claude', side_effect=RuntimeError('new schema error')), \
                patch.object(repair_sandbox, 'codex') as codex, \
                self.assertRaisesRegex(RuntimeError, 'new schema error'):
            self.run_agent()
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.with_suffix('.agent.json').exists())
        codex.assert_not_called()

    def test_provider_text_is_not_a_quota_failure_event(self):
        for event in ({'type': 'assistant', 'message': 'usage limit'},
                      {'type': 'result', 'is_error': False, 'result': 'usage limit'},
                      {'type': 'item.completed', 'item': {'text': 'usage limit'}}):
            self.log.write_text(json.dumps(event))
            self.assertIsNone(repair_agents.unavailable(self.log, 'claude'))
        self.log.write_text(json.dumps({'type': 'assistant', 'error': 'rate_limit',
                                       'message': {'content': [{'text': 'Limit reached'}]}}))
        self.assertIn('rate_limit', repair_agents.unavailable(self.log, 'claude'))

    def test_existing_proxy_connection_is_preserved_without_loading_user_tools(self):
        (self.root / 'config.toml').write_text('''model_provider = "proxy"
[model_providers.proxy]
name = "Existing dual-account proxy"
base_url = "http://127.0.0.1:8317/v1"
wire_api = "responses"
requires_openai_auth = true
http_headers = { Authorization = "Bearer private-fixture" }
''')
        with patch.dict(os.environ, {'CODEX_HOME': str(self.root)}), \
                patch.object(repair_agents, 't3_provider', return_value={'binaryPath': '/installed/codex'}), \
                patch.object(repair_agents.shutil, 'which', return_value='/installed/codex'):
            binary, options, env = repair_agents.codex_connection()
        self.assertEqual(binary, '/installed/codex')
        self.assertIn('model_provider="proxy"', options)
        self.assertIn('model_providers.proxy.base_url="http://127.0.0.1:8317/v1"', options)
        self.assertNotIn('private-fixture', ' '.join(options))
        self.assertIn('model_providers.proxy.env_http_headers={"Authorization"="FLY_REPAIR_CLIENT_HEADER_0"}', options)
        self.assertEqual(env['FLY_REPAIR_CLIENT_HEADER_0'], 'Bearer private-fixture')
        self.assertEqual(env['CODEX_HOME'], str(self.root))
        with patch.object(repair_sandbox, 'codex_runtime', return_value=None), \
                patch.object(repair_sandbox, 'run') as run:
            self.output.write_text('{"approved":true}')
            repair_sandbox.codex(binary, self.workspace, [], 'task', self.schema, self.output,
                self.log, False, connection_options=options, client_environment=env)
        command = run.call_args.args[0]
        self.assertIn('--ignore-user-config', command)
        self.assertEqual(command[command.index('--model') + 1], 'gpt-6.1-sol')
        self.assertIn('model_reasoning_effort="high"', command)
        self.assertIn('model_provider="proxy"', command)


class SharedToolTests(unittest.TestCase):
    def test_new_author_tests_stay_editable_across_tool_calls(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(repair_tools.shutil, 'which', return_value='/usr/bin/bwrap'):
            root = Path(temporary)
            repo = root / 'workspace'
            tests = repo / 'chromium/overlay/fly/test'
            tests.mkdir(parents=True)
            baseline = tests / 'baseline_test.cc'
            baseline.write_text('baseline')
            config = repair_tools.configuration(repo, [], True)
            added = tests / 'compat_new_test.cc'
            added.write_text('new regression')
            args = repair_tools.command(repo, [], True, 'pwd', config['protected_tests'])
            self.assertIn(str(baseline), args)
            self.assertNotIn(str(added), args)
            self.assertIn('PYTHONNOUSERSITE', args)

    def test_tools_have_private_home_network_namespace_and_read_only_review(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(repair_tools.shutil, 'which', return_value='/usr/bin/bwrap'):
            root = Path(temporary)
            (root / 'repo.git').mkdir()
            repo = root / 'workspace'
            repo.mkdir()
            args = repair_tools.command(repo, [], False, 'pwd')
            self.assertIn('--unshare-all', args)
            self.assertIn('--clearenv', args)
            self.assertNotIn('--share-net', args)
            self.assertNotIn('--bind', args[:-args.index('--clearenv')][-2:])
            self.assertEqual(args[args.index('--setenv') + 1:args.index('--setenv') + 3], ['PATH', '/usr/bin:/bin'])

    def test_tool_output_cannot_be_redirected_by_a_proposal_symlink(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            workspace = root / 'workspace'
            workspace.mkdir()
            (root / 'repo.git').mkdir()
            (workspace / '.repair-runtime').mkdir()
            outside = root / 'protected'
            outside.write_text('unchanged')
            (workspace / '.repair-runtime/tool-output.log').symlink_to(outside)
            def run(args, directory, log, *rest):
                self.assertFalse(log.is_relative_to(workspace))
                log.write_text('safe output')
            with patch.object(repair_tools.repair_sandbox, 'run', side_effect=run), \
                    patch.object(repair_tools.shutil, 'which', return_value='/usr/bin/bwrap'):
                result = repair_tools.execute({'workspace': str(workspace), 'upstream': [], 'writable': False}, 'pwd')
            self.assertEqual(outside.read_text(), 'unchanged')
            self.assertFalse(result['isError'])

    def test_scratch_and_source_symlinks_cannot_mount_private_host_paths(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(repair_tools.shutil, 'which', return_value='/usr/bin/bwrap'):
            root = Path(temporary)
            repo = root / 'workspace'
            repo.mkdir()
            (root / 'repo.git').mkdir()
            (repo / '.repair-runtime').symlink_to(root / 'repo.git')
            with self.assertRaisesRegex(RuntimeError, 'scratch must not be a symlink'):
                repair_tools.command(repo, [], True, 'pwd')
            (repo / '.repair-runtime').unlink()
            (repo / 'leak.cc').symlink_to('/etc/passwd')
            with self.assertRaisesRegex(RuntimeError, 'Symlinks are forbidden'):
                repair_tools.command(repo, [], True, 'pwd')


if __name__ == '__main__':
    unittest.main()
