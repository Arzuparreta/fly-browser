#!/usr/bin/env python3
"""One credential-free tool surface for engine repair agents (MCP stdio)."""
import argparse
import json
import os
from pathlib import Path
import shutil
import shlex
import socket
import subprocess
import sys
import tempfile

import repair_sandbox


def reset_scratch(workspace):
    scratch = Path(workspace) / '.repair-runtime'
    if scratch.is_symlink():
        raise RuntimeError('Repair scratch must not be a symlink')
    if scratch.exists():
        shutil.rmtree(scratch)
    scratch.mkdir()


def configuration(workspace, upstream, writable, editable_tests=()):
    workspace = Path(workspace)
    return {'workspace': str(workspace), 'upstream': list(map(str, upstream)),
            'writable': writable, 'protected_tests': sorted(
                path.relative_to(workspace).as_posix()
                for path in (workspace / 'chromium/overlay/fly').rglob('*')
                if path.is_file() and repair_sandbox.test_source(path.relative_to(workspace))
                and path.relative_to(workspace).as_posix() not in editable_tests)}


def command(workspace, upstream, writable, shell, protected_tests=()):
    workspace = Path(workspace).resolve()
    if not shutil.which('bwrap'):
        raise RuntimeError('bubblewrap is required for repair tools')
    scratch = workspace / '.repair-runtime'
    if scratch.is_symlink():
        raise RuntimeError('Repair scratch must not be a symlink')
    for path in workspace.rglob('*'):
        if path == workspace / '.git' or '.repair-runtime' in path.relative_to(workspace).parts:
            continue
        if path.is_symlink():
            raise RuntimeError('Symlinks are forbidden in repair sources')
    scratch.mkdir(exist_ok=True)
    args = ['bwrap', '--unshare-all', '--die-with-parent', '--ro-bind', '/usr', '/usr',
            '--proc', '/proc', '--dev', '/dev', '--tmpfs', '/tmp']
    for name in ('/bin', '/sbin', '/lib', '/lib64'):
        path = Path(name)
        if path.is_symlink():
            args += ['--symlink', os.readlink(path), name]
        elif path.exists():
            args += ['--ro-bind', name, name]
    for name in ('/etc/ld.so.cache', '/etc/passwd', '/etc/group'):
        if Path(name).exists():
            args += ['--ro-bind', name, name]
    args += ['--ro-bind', str(workspace), str(workspace),
             '--ro-bind', str(workspace.parent / 'repo.git'), str(workspace.parent / 'repo.git')]
    if writable:
        for name in ('chromium/patches', 'chromium/overlay/fly'):
            path = workspace / name
            args += ['--bind', str(path), str(path)]
        protected = [workspace / name for name in (
            'chromium/patches/series', 'chromium/overlay/fly/resources',
            'chromium/overlay/fly/commands', 'chromium/overlay/fly/fly_version.h')]
        protected += [workspace / name for name in protected_tests]
        for path in protected:
            if path.exists():
                args += ['--ro-bind', str(path), str(path)]
    for root in dict.fromkeys(map(lambda root: str(Path(root).resolve()), upstream)):
        source = Path(root) / 'src'
        args += ['--ro-bind', str(source), str(source)]
        if (source / 'out').exists():
            args += ['--tmpfs', str(source / 'out')]
        private = source / 'buildtools/reclient_cfgs/reproxy.cfg'
        if private.exists():
            args += ['--ro-bind', '/dev/null', str(private)]
    args += ['--bind', str(scratch), str(scratch), '--clearenv',
             '--setenv', 'PATH', '/usr/bin:/bin', '--setenv', 'HOME', str(scratch),
             '--setenv', 'LANG', 'C.UTF-8', '--setenv', 'GIT_CONFIG_GLOBAL', '/dev/null',
             '--setenv', 'GIT_CONFIG_NOSYSTEM', '1', '--setenv', 'GIT_OPTIONAL_LOCKS', '0',
             '--setenv', 'PYTHONDONTWRITEBYTECODE', '1', '--setenv', 'PYTHONNOUSERSITE', '1',
             '--chdir', str(workspace),
             '--', '/bin/sh', '-c', shell]
    return args


def execute(config, shell, timeout=120):
    if not isinstance(shell, str) or not shell or len(shell) > 65536:
        raise ValueError('A bounded shell command is required')
    if not isinstance(timeout, int) or not 1 <= timeout <= 120:
        raise ValueError('Tool timeout must be 1..120 seconds')
    workspace = Path(config['workspace'])
    args = command(workspace, config['upstream'], config['writable'], shell,
                   config.get('protected_tests', ()))
    # Tool output is controller-owned: an agent must not redirect the host
    # logger by replacing a scratch filename with a symlink.
    with tempfile.TemporaryDirectory(prefix='fly-repair-tool-') as temporary:
        log = Path(temporary) / 'output.log'
        try:
            repair_sandbox.run(args, workspace, log, timeout, repair_sandbox.clean_environment())
            failed = False
        except (RuntimeError, subprocess.TimeoutExpired):
            failed = True
        text = log.read_text(errors='replace')
    if len(text) > 64000:
        text = text[:16000] + '\n[output truncated]\n' + text[-48000:]
    return {'content': [{'type': 'text', 'text': text}], 'isError': failed}


def verify(config):
    workspace = Path(config['workspace'])
    outside = workspace.parent / '.repair-private-canary'
    protected = workspace / '.repair-readonly-probe'
    adapter = workspace / 'chromium/overlay/fly/.repair-write-probe.cc'
    outside.write_text('isolation canary')
    protected.write_text('readonly canary')
    program = '''import pathlib,socket,sys
outside,alias,protected,adapter,port,writable=sys.argv[1:7]
for name in (outside,alias,*sys.argv[7:]):
 try:pathlib.Path(name).read_bytes()
 except OSError:pass
 else:raise SystemExit("outside reads allowed")
try:pathlib.Path(protected).write_text("forbidden")
except OSError:pass
else:raise SystemExit("protected writes allowed")
try:
 s=socket.socket();s.settimeout(1);s.connect(("127.0.0.1",int(port)))
except OSError:pass
else:raise SystemExit("host loopback allowed")
try:pathlib.Path(adapter).write_text("scoped probe")
except OSError:
 if writable=="true":raise SystemExit("author writes unavailable")
else:
 if writable!="true":raise SystemExit("reviewer source writes allowed")
print("REPAIR_ISOLATION_OK")
'''
    try:
        with socket.socket() as listener:
            listener.bind(('127.0.0.1', 0))
            listener.listen(1)
            args = ['python3', '-c', program, str(outside),
                    f'/proc/{os.getpid()}/root{outside}', str(protected), str(adapter),
                    str(listener.getsockname()[1]), str(config['writable']).lower(),
                    f'/proc/{os.getpid()}/cmdline', f'/proc/{os.getpid()}/environ']
            result = execute(config, ' '.join(map(shlex.quote, args)))
            if result['isError'] or 'REPAIR_ISOLATION_OK' not in result['content'][0]['text']:
                raise RuntimeError('Shared repair tool isolation self-test failed: ' +
                                   result['content'][0]['text'])
    finally:
        for path in (outside, protected, adapter):
            path.unlink(missing_ok=True)


def serve(config):
    for line in sys.stdin:
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get('id')
            if request_id is None:
                continue
            method = request.get('method')
            if method == 'initialize':
                result = {'protocolVersion': '2024-11-05', 'capabilities': {'tools': {}},
                          'serverInfo': {'name': 'fly-repair', 'version': '1'}}
            elif method == 'ping':
                result = {}
            elif method == 'tools/list':
                result = {'tools': [{'name': 'exec', 'description':
                    'Run shell commands inside the isolated repair workspace. No host auth/network. '
                    'Use rg, git show, cat and Python to inspect/edit allowed sources. '
                    'All engine sources are read-only; never build here.', 'inputSchema': {
                        'type': 'object', 'properties': {'command': {'type': 'string'},
                        'timeout': {'type': 'integer', 'minimum': 1, 'maximum': 120}},
                        'required': ['command'], 'additionalProperties': False}}]}
            elif method == 'tools/call' and request.get('params', {}).get('name') == 'exec':
                arguments = request['params'].get('arguments', {})
                result = execute(config, arguments.get('command'), arguments.get('timeout', 120))
            else:
                raise ValueError('Unsupported repair tool request')
            response = {'jsonrpc': '2.0', 'id': request_id, 'result': result}
        except (ValueError, TypeError, KeyError, OSError, RuntimeError) as error:
            response = {'jsonrpc': '2.0', 'id': request_id,
                        'error': {'code': -32602, 'message': str(error)}}
        print(json.dumps(response), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, required=True)
    serve(json.loads(parser.parse_args().config.read_text()))
