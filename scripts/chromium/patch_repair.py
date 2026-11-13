"""Reanchor integration patches without changing any of Fly's edits.

Only exact, unique matches are accepted. All revisions are staged in temporary
Git trees; no engine checkout is read or changed. A complete series must pass
before any patch is written. Semantic/API conflicts are deliberately refused.
"""
import dataclasses
import concurrent.futures
import re
from pathlib import Path
import subprocess
import tempfile

import engine
import native


@dataclasses.dataclass
class Hunk:
    header: str
    lines: list


@dataclasses.dataclass
class FilePatch:
    name: str
    hunks: list


def parse(data):
    """Accept the existing-files, textual unified format used by Fly."""
    lines = data.decode('utf-8').splitlines(keepends=True)
    files = []
    i = 0
    while i < len(lines):
        if not lines[i].startswith('--- a/'):
            raise ValueError('Unsupported patch header')
        name = lines[i][6:].rstrip('\n')
        native.relative_path(name)
        if (name.startswith('fly/') or i + 1 >= len(lines)
                or lines[i + 1] != '+++ b/' + name + '\n'):
            raise ValueError('Only existing upstream files can be repaired')
        file = FilePatch(name, [])
        files.append(file)
        i += 2
        while i < len(lines) and lines[i].startswith('@@ '):
            header = lines[i]
            match = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', header)
            if not match:
                raise ValueError('Unsupported hunk header')
            i += 1
            body = []
            while i < len(lines) and not lines[i].startswith(('@@ ', '--- a/')):
                line = lines[i]
                if not line.endswith('\n') or line[:1] not in (' ', '+', '-'):
                    raise ValueError('Unsupported hunk body or missing final newline')
                body.append(line)
                i += 1
            old_count = sum(line[0] != '+' for line in body)
            new_count = sum(line[0] != '-' for line in body)
            if (old_count != int(match[2] or 1) or new_count != int(match[4] or 1)
                    or not any(line[0] in '+-' for line in body)):
                raise ValueError('Malformed or empty hunk')
            file.hunks.append(Hunk(header, body))
        if not file.hunks:
            raise ValueError('Patch has no hunks')
    if not files:
        raise ValueError('Empty patch')
    return files


def edits(data):
    return [(file.name, [line for hunk in file.hunks for line in hunk.lines
                         if line[0] in '+-']) for file in parse(data)]


def positions(lines, needle):
    if not needle:
        return []
    return [i for i in range(len(lines) - len(needle) + 1)
            if lines[i:i + len(needle)] == needle]


def reanchor(data, reference, target):
    result = []
    for file in parse(data):
        before = reference[file.name].decode('utf-8').splitlines(keepends=True)
        other = target[file.name].decode('utf-8').splitlines(keepends=True)
        result += ['--- a/' + file.name + '\n', '+++ b/' + file.name + '\n']
        prior_end = other_end = 0
        delta = 0
        for hunk in file.hunks:
            old = [line[1:] for line in hunk.lines if line[0] != '+']
            locations = positions(before, old)
            if len(locations) != 1:
                raise ValueError(file.name + ': original hunk is not unique')
            left = next(i for i, line in enumerate(hunk.lines) if line[0] in '+-')
            right = len(hunk.lines)
            while hunk.lines[right - 1][0] == ' ':
                right -= 1
            core = hunk.lines[left:right]
            start = locations[0] + left
            end = start + sum(line[0] != '+' for line in core)
            candidates = []
            # Use at most the conventional three context lines on either
            # side. Context may come from just one side when upstream changed
            # the other. A nonblank anchor and uniqueness in BOTH trees are
            # mandatory; the removed code alone is not an anchor.
            for lead in range(min(3, start) + 1):
                for tail in range(min(3, len(before) - end) + 1):
                    prefix, suffix = before[start - lead:start], before[end:end + tail]
                    if not any(line.strip() for line in prefix + suffix):
                        continue
                    body = [' ' + line for line in prefix] + core + [' ' + line for line in suffix]
                    needle = [line[1:] for line in body if line[0] != '+']
                    first, second = positions(before, needle), positions(other, needle)
                    if len(first) != 1 or len(second) != 1:
                        continue
                    if first[0] < prior_end or second[0] < other_end:
                        continue
                    candidates.append((lead + tail, min(lead, tail), first[0], second[0], body))
            if not candidates:
                raise ValueError(file.name + ': no unique unchanged context for hunk')
            _, _, pos, target_pos, body = max(candidates, key=lambda item: item[:2])
            old_count = sum(line[0] != '+' for line in body)
            new_count = sum(line[0] != '-' for line in body)
            result.append(f'@@ -{pos + 1},{old_count} +{pos + 1 + delta},{new_count} @@\n')
            result.extend(body)
            prior_end, other_end = pos + old_count, target_pos + old_count
            delta += new_count - old_count
    repaired = ''.join(result).encode('utf-8')
    if edits(data) != edits(repaired):
        raise ValueError('Repair changed Fly edits')
    return repaired


def stage(directory, contents):
    directory.mkdir()
    engine.run(['git', 'init', '--quiet'], directory, capture=True)
    for name, data in contents.items():
        native.atomic_bytes(directory / name, data)


def contents(directory, names):
    return {name: (directory / name).read_bytes() for name in names}


def apply(directory, patch, check=False):
    result = subprocess.run(['git', 'apply', '--whitespace=error-all',
                             *(['--check'] if check else []), '-'], input=patch,
                            cwd=directory, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError(result.stderr.decode('utf-8', errors='replace').strip())


def repair(reference, target, fetch, log=print):
    """Resolve all context drift, or write nothing. Reference must already
    accept the original series. Its output is proved byte-identical after
    every repaired patch, including dependencies on earlier patches."""
    import engine_update
    for revision in (reference, target):
        if not re.fullmatch(r'[0-9a-f]{40}', revision):
            raise ValueError('Invalid Chromium revision')
    patches, _ = native.inputs()
    names = sorted(native.patch_paths(patches))
    originals = {patch: patch.read_bytes() for patch in patches}
    # Complete upstream downloads before attempting a repair. Transport and
    # decoding failures are never treated as patch conflicts.
    snapshots = {}
    for revision in dict.fromkeys((reference, target)):
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            snapshots[revision] = dict(zip(names, pool.map(
                lambda name: engine_update.gitiles_file(revision, name, fetch), names)))
    changed = {}
    with tempfile.TemporaryDirectory(prefix='fly-patch-repair-') as tmp:
        root = Path(tmp)
        ref, dest, proof = root / 'reference', root / 'target', root / 'proof'
        stage(ref, snapshots[reference])
        stage(dest, snapshots[target])
        stage(proof, snapshots[reference])
        for patch, original in originals.items():
            before = contents(ref, names)
            try:
                apply(ref, original)
            except ValueError as error:
                raise ValueError(patch.name + ': reference series does not apply: ' + str(error)) from error
            replacement = original
            try:
                apply(dest, original, check=True)
            except ValueError:
                try:
                    replacement = reanchor(original, before, contents(dest, names))
                except ValueError as error:
                    raise ValueError(patch.name + ': ' + str(error)) from error
                changed[patch] = replacement
                log('Reanchored ' + patch.name)
            apply(proof, replacement)
            if contents(ref, names) != contents(proof, names):
                raise ValueError(patch.name + ': reference output changed')
            apply(dest, replacement)
        # A second strict replay starts from pristine trees and includes all
        # patches. There is no fuzzy application or skipped failed patch.
        for index, revision in enumerate(dict.fromkeys((reference, target))):
            replay = root / f'replay-{index}'
            stage(replay, snapshots[revision])
            for patch, original in originals.items():
                apply(replay, changed.get(patch, original))
    try:
        for patch, data in changed.items():
            native.atomic_bytes(patch, data)
    except BaseException:
        for patch in changed:
            native.atomic_bytes(patch, originals[patch])
        raise
    log(f'{len(changed)} context-only patch repair(s); reference output is unchanged.')
    return [patch.name for patch in changed]


def verify_base(base, revision, fetch):
    """Prove that a repair PR only changes context and preserves the complete
    pinned Stable output relative to current main. Repeated before merge."""
    import engine_update
    repo = engine.REPO
    changed = engine.git(repo, 'diff', '--name-only', base, 'HEAD').splitlines()
    patches, _ = native.inputs()
    allowed = {patch.relative_to(repo).as_posix(): patch for patch in patches}
    if not changed or any(name not in allowed for name in changed):
        raise ValueError('A Beta repair may only change existing integration patches')
    old = {}
    for name in changed:
        old[name] = subprocess.run(['git', 'show', f'{base}:{name}'], cwd=repo,
                                   check=True, stdout=subprocess.PIPE).stdout
        if edits(old[name]) != edits(allowed[name].read_bytes()):
            raise ValueError('A Beta repair changed Fly edits: ' + name)
    names = sorted(native.patch_paths(patches))
    snapshot = {name: engine_update.gitiles_file(revision, name, fetch) for name in names}
    with tempfile.TemporaryDirectory(prefix='fly-patch-proof-') as tmp:
        root = Path(tmp)
        for kind in ('base', 'head'):
            directory = root / kind
            stage(directory, snapshot)
            for patch in patches:
                name = patch.relative_to(repo).as_posix()
                apply(directory, old.get(name, patch.read_bytes()) if kind == 'base' else patch.read_bytes())
        if contents(root / 'base', names) != contents(root / 'head', names):
            raise ValueError('The pinned Stable output differs from main')
