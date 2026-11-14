"""Isolation for engine repair proposals and untrusted native validation.

The Codex client keeps its normal login, outside tool sandboxes. Its tools see
only the proposal and public upstream sources. Native validation has its own
filesystem/PID/network namespaces and an empty HOME, never the runner's login.
"""
import json
import os
from pathlib import Path
import re
import shutil
import signal
import socket
import subprocess


class CodexQuotaUnavailable(RuntimeError):
    pass


def quota_failure(log):
    """Read CLI failure events, never model/tool text containing these words."""
    for line in log.read_text(errors='replace').splitlines():
        try:
            event = json.loads(line)
        except ValueError:
            continue
        if not isinstance(event, dict):
            continue
        message = None
        if event.get('type') == 'error':
            message = event.get('message')
        elif event.get('type') == 'turn.failed':
            error = event.get('error')
            message = error.get('message') if isinstance(error, dict) else None
        if isinstance(message, str) and 'usage limit' in message.lower():
            return message[:2000]
    return None


def test_source(name):
    path = Path(name)
    return (any(part in ('test', 'tests', 'testing') for part in path.parts) or
            re.search(r'(^|_)(test|unittest|browsertest|testing)(_|$)', path.stem) is not None)


def clean_environment(home=None):
    env = {key: os.environ[key] for key in ('PATH', 'LANG', 'LC_ALL', 'TZ',
                                           'SSL_CERT_FILE', 'SSL_CERT_DIR') if key in os.environ}
    env.update({'HOME': str(home or Path.home()), 'GIT_CONFIG_NOSYSTEM': '1',
                'GIT_CONFIG_GLOBAL': '/dev/null', 'GIT_OPTIONAL_LOCKS': '0',
                'PYTHONDONTWRITEBYTECODE': '1'})
    return env


def failure_excerpt(log):
    text = log.read_text(errors='replace')
    # Native launchers end with long lists of test names. Keep the first
    # assertion/startup/compiler failure too, so a proposer gets its cause.
    match = re.search(r'(?im)^.*(?:Missing X server|FATAL:|fatal error:|: error:|'
                      r': Failure|\[\s*FAILED\s*\]|Fatal server error:).*$', text)
    if match and match.start() < len(text) - 8000:
        return text[match.start():match.start() + 4000] + '\n...\n' + text[-4000:]
    return text[-8000:]


def run(command, directory, log, timeout, env=None, input=None):
    """Bound the entire process group, including compilers/agent children."""
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open('wb') as output:
        process = subprocess.Popen([str(arg) for arg in command], cwd=directory,
                                   env=env, stdin=subprocess.PIPE if input else subprocess.DEVNULL,
                                   stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.communicate(input.encode() if input else None, timeout=timeout)
        except BaseException:
            # Python's engine leases and overlay journals need their finally
            # blocks. Give the entire group an interrupt before termination.
            try:
                os.killpg(process.pid, signal.SIGINT)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=30)
            except subprocess.TimeoutExpired:
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                process.wait()
            finally:
                # A leader can exit while a descendant ignores SIGINT.
                # Terminate the owned group even after wait() succeeded.
                try:
                    os.killpg(process.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            raise
    if process.returncode:
        tail = failure_excerpt(log)
        raise RuntimeError(f'Command failed ({process.returncode}); log: {log}\n{tail}')


def codex_options(workspace, upstream, writable, runtime_binary=None, editable_tests=()):
    workspace = Path(workspace).resolve()
    # A private HOME is passed to shell tools. CLI authentication is loaded by
    # the client before it invokes a sandbox and is never granted to tools.
    rules = {':root': 'deny', ':minimal': 'read', str(workspace): 'read',
             str(workspace.parent / 'repo.git'): 'read'}
    if writable:
        for name in ('chromium/patches', 'chromium/overlay/fly'):
            rules[str(workspace / name)] = 'write'
        for file in (workspace / 'chromium/overlay/fly').rglob('*'):
            name = file.relative_to(workspace).as_posix()
            if file.is_file() and test_source(name) and name not in editable_tests:
                rules[str(file)] = 'read'
    for name in ('chromium/overlay/fly/fly_version.h', 'chromium/overlay/fly/commands',
                 'chromium/overlay/fly/resources', 'chromium/patches/series'):
        rules[str(workspace / name)] = 'read'
    for root in upstream:
        source = Path(root).resolve() / 'src'
        rules[str(source)] = 'read'
        # Release args contain a Google API key. Only public sources belong
        # in a repair prompt, not any build outputs or runtime profiles.
        rules[str(source / 'out')] = 'deny'
        rules[str(source / 'buildtools/reclient_cfgs/reproxy.cfg')] = 'deny'
    home = workspace / '.repair-runtime'
    rules[str(home)] = 'write'
    if runtime_binary:
        rules[str(runtime_binary)] = 'read'
    return ['-c', 'default_permissions="fly-repair"',
            '-c', 'permissions.fly-repair.filesystem=' + json.dumps(rules).replace(': ', '='),
            '-c', 'permissions.fly-repair.network.enabled=false',
            '-c', 'shell_environment_policy.inherit="none"',
            '-c', 'shell_environment_policy.set=' + json.dumps({
                'PATH': '/usr/bin:/bin', 'HOME': str(home), 'LANG': 'C.UTF-8',
                'GIT_CONFIG_NOSYSTEM': '1', 'GIT_CONFIG_GLOBAL': '/dev/null',
                'GIT_OPTIONAL_LOCKS': '0'}).replace(': ', '=')]


def codex_runtime(codex, workspace, upstream, writable=True, editable_tests=()):
    """Probe the installed CLI's sandbox, including its native re-exec path.
    Do not weaken isolation if its version cannot enforce this profile."""
    options = codex_options(workspace, upstream, True, editable_tests=editable_tests)
    command = [codex, 'sandbox', '-P', 'fly-repair', '-C', str(workspace), *options,
               '--', '/usr/bin/true']
    result = subprocess.run(command, capture_output=True, text=True, timeout=30)
    binary = None
    if result.returncode:
        match = re.search(r'bwrap: execvp (/[^\n]+/codex): No such file or directory', result.stderr)
        if not match:
            raise RuntimeError('Installed Codex cannot enforce repair isolation: ' + result.stderr[-2000:])
        binary = Path(match[1])
        if not binary.is_file() or binary.open('rb').read(4) != b'\x7fELF':
            raise RuntimeError('Invalid Codex sandbox runtime')
        command = [codex, 'sandbox', '-P', 'fly-repair', '-C', str(workspace),
                   *codex_options(workspace, upstream, True, binary, editable_tests), '--', '/usr/bin/true']
        subprocess.run(command, capture_output=True, text=True, check=True, timeout=30)
    # Capability probing must prove denials, not just that the CLI accepts a
    # profile name (older CLIs could ignore unknown permission settings).
    canary = workspace.parent / '.host-sandbox-canary'
    protected = workspace / '.sandbox-readonly-probe'
    adapter = workspace / 'chromium/overlay/fly/.sandbox-write-probe.cc'
    canary.write_text('public isolation probe')
    protected.write_text('public isolation probe')
    with socket.socket() as listener:
        listener.bind(('127.0.0.1', 0))
        listener.listen(1)
        program = '''import pathlib,socket,sys
outside,alias,protected,adapter,port,writable=sys.argv[1:]
for name in (outside,alias):
 try:pathlib.Path(name).read_bytes()
 except OSError:pass
 else:raise SystemExit("host reads were allowed")
try:pathlib.Path(protected).write_text("changed")
except OSError:pass
else:raise SystemExit("protected writes were allowed")
try:
 sock=socket.socket();sock.settimeout(1);sock.connect(("127.0.0.1",int(port)))
except OSError:pass
else:raise SystemExit("host network was allowed")
if pathlib.Path(adapter).parent.is_dir():
 try:pathlib.Path(adapter).write_text("public write probe")
 except OSError:
  if writable == "true":raise SystemExit("scoped source writes unavailable")
 else:
  if writable != "true":raise SystemExit("reviewer could write source")
'''
        command = [codex, 'sandbox', '-P', 'fly-repair', '-C', str(workspace),
                   *codex_options(workspace, upstream, writable, binary, editable_tests), '--', '/usr/bin/python3',
                   '-c', program, str(canary), f'/proc/{os.getpid()}/root{canary}',
                   str(protected), str(adapter), str(listener.getsockname()[1]),
                   'true' if writable else 'false']
        try:
            result = subprocess.run(command, capture_output=True, text=True, timeout=30)
            if result.returncode:
                raise RuntimeError('Codex isolation self-test failed: ' + result.stderr[-2000:])
        finally:
            for file in (canary, protected, adapter):
                file.unlink(missing_ok=True)
    return binary


def codex(codex, workspace, upstream, prompt, schema, output, log, writable, timeout=1200, editable_tests=()):
    runtime = codex_runtime(codex, workspace, upstream, writable, editable_tests)
    (workspace / '.repair-runtime').mkdir(exist_ok=True)
    command = [codex, '--no-daemon', '-a', 'never', 'exec', '--ephemeral',
               '--ignore-user-config', '--ignore-rules', '--strict-config',
               '--disable', 'apps', '--disable', 'plugins', '--disable', 'multi_agent',
               '--disable', 'multi_agent_v2', '--disable', 'browser_use',
               '--disable', 'computer_use', '--disable', 'view_image',
               '-c', 'web_search="disabled"', '-c', 'plugins._default.enabled=false',
               *codex_options(workspace, upstream, writable, runtime, editable_tests),
               '-C', str(workspace), '--output-schema', str(schema),
               '--output-last-message', str(output), '--json', '-']
    # Keep the user's selected Codex home for client auth, not for tool shells.
    env = clean_environment()
    if os.environ.get('CODEX_HOME'):
        env['CODEX_HOME'] = os.environ['CODEX_HOME']
    try:
        run(command, workspace, log, timeout, env, prompt)
    except RuntimeError as error:
        quota = quota_failure(log)
        if quota:
            raise CodexQuotaUnavailable(quota) from error
        raise
    if not output.is_file() or output.stat().st_size > 128 * 1024:
        raise RuntimeError('Codex returned no bounded structured result')
    return json.loads(output.read_text())


def build_command(repo, root, tools, command, network=False, extra_read=()):
    """Only the selected non-production engine is writable. No host /home,
    GitHub/Codex auth, SSH socket, desktop display or user profiles are mounted."""
    if not shutil.which('bwrap'):
        raise RuntimeError('bubblewrap is required for automatic native validation')
    repo, root, tools = Path(repo).resolve(), Path(root).resolve(), Path(tools).resolve()
    home = root / '.repair-home'
    home.mkdir(exist_ok=True)
    arguments = ['bwrap', '--unshare-all', '--die-with-parent']
    if network:
        arguments += ['--share-net']
    arguments += ['--ro-bind', '/usr', '/usr', '--proc', '/proc', '--dev', '/dev',
                  '--tmpfs', '/tmp']
    for name in ('/bin', '/sbin', '/lib', '/lib64'):
        if Path(name).is_symlink():
            arguments += ['--symlink', os.readlink(name), name]
        elif Path(name).exists():
            arguments += ['--ro-bind', name, name]
    for name in ('/etc/ssl', '/etc/pki', '/etc/ca-certificates', '/etc/ld.so.cache', '/etc/os-release',
                 '/etc/resolv.conf', '/etc/hosts', '/etc/nsswitch.conf',
                 '/etc/passwd', '/etc/group', '/etc/fonts'):
        if Path(name).exists():
            arguments += ['--ro-bind', name, name]
    # /sys CPU topology is read by Chromium's local build scheduler.
    if Path('/sys').exists():
        arguments += ['--ro-bind', '/sys', '/sys']
    arguments += ['--ro-bind', str(repo), str(repo), '--bind', str(root), str(root),
                  '--ro-bind', str(tools), str(tools)]
    for path in extra_read:
        arguments += ['--ro-bind', str(path), str(path)]
    arguments += ['--clearenv']
    env = clean_environment(home)
    env.update({'PATH': '/usr/local/bin:/usr/bin:/bin', 'LANG': 'C.UTF-8',
                'XDG_CONFIG_HOME': str(home / 'config'), 'XDG_CACHE_HOME': str(home / 'cache'),
                'XDG_DATA_HOME': str(home / 'data')})
    # Chromium dependencies retain Git LFS pointer bytes. Seeded repositories
    # can also retain old LFS post-checkout hooks; do not execute them or need
    # the host's LFS installation/auth inside this namespace. No config edits.
    overrides = {'core.hooksPath': '/dev/null', 'filter.lfs.process': '',
                 'filter.lfs.clean': '', 'filter.lfs.smudge': '',
                 'filter.lfs.required': 'false'}
    env['GIT_CONFIG_COUNT'] = str(len(overrides))
    for index, (key, value) in enumerate(overrides.items()):
        env[f'GIT_CONFIG_KEY_{index}'] = key
        env[f'GIT_CONFIG_VALUE_{index}'] = value
    for key, value in env.items():
        arguments += ['--setenv', key, value]
    return arguments + ['--chdir', str(repo), '--', *map(str, command)]
