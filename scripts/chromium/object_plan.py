"""Retain GN compiler recipes while omitting runtime-only generation prerequisites.

GN's inputdeps also includes data generators (snapshots, resource packs) whose
host tools can pull in most of Chromium. Compiler validation needs generated
headers and modules; it does not need to link the browser to create those data.
Ninja still builds each selected prerequisite through its original GN graph.
"""
from pathlib import Path
import re
import subprocess

import engine

HEADER_SUFFIXES = (b'.h', b'.hh', b'.hpp', b'.hxx', b'.inc', b'.inl', b'.def',
                   b'.tcc', b'.ipp', b'.modulemap', b'.pcm', b'.pch')


def recipe_files(root, relative, manifest, env):
    output = root / 'src' / relative
    sources = sorted(name for name in manifest['files']
                     if name.endswith(('.cc', '.mm', '.c', '.m')))
    response = output / 'fly-object-sources.rsp'
    response.write_text(''.join('//' + name + '\n' for name in sources), encoding='utf-8')
    result = subprocess.run(
        ['gn', 'refs', relative, '@' + str(response.resolve()), '--default-toolchain',
         '--relation=source', '--as=label', '-q',
         '--type=source_set,static_library,shared_library,loadable_module,executable'],
        cwd=root / 'src', env=env, check=True, capture_output=True, text=True)
    labels = sorted(set(result.stdout.splitlines()))
    files = []
    for label in labels:
        # GN qualifies non-default toolchains. The browser and its scoped test
        # runner must be validated in the default product toolchain.
        if '(' in label:
            continue
        match = re.fullmatch(r'//([^:]*):([A-Za-z0-9_+.-]+)', label)
        if not match or '..' in match[1].split('/'):
            engine.fail('Unrecognised GN compiler source owner: ' + label)
        file = output / 'obj' / match[1] / (match[2] + '.ninja')
        if not file.is_file():
            engine.fail('Missing GN compiler recipe for source owner: ' + label)
        files.append(file)
    if not files:
        engine.fail('No default-toolchain GN compiler source owners')
    return files, labels


def compiler_plan(output, targets):
    edges = {}
    selected = {}
    wanted = {value.encode(): name for name, value in targets.items()}
    # Read bytes: third-party GN flag metadata need not be UTF-8. Keep each
    # compiler's original flags, rule, response-file handling and edge variables.
    files = {output / 'build.ninja', output / 'toolchain.ninja'}
    for target in targets.values():
        path = Path(target)
        candidates = [path.with_suffix('.ninja')]
        candidates.extend(Path(str(parent) + '.ninja') for parent in path.parents
                          if str(parent) != '.')
        for candidate in candidates:
            file = output / candidate
            if file.is_file() and (b'build ' + target.encode() + b': ') in file.read_bytes():
                files.add(file)
                break
        else:
            engine.fail('Compiler plan is missing selected GN edges: ' + target)
    loaded = set()

    def load_file(file):
        if file in loaded:
            return
        loaded.add(file)
        data = file.read_bytes()
        lines = data.splitlines(keepends=True)
        for index, line in enumerate(lines):
            if not line.startswith(b'build '):
                continue
            try:
                outputs, body = line[6:].strip().split(b': ', 1)
            except ValueError:
                engine.fail('Unsupported GN build edge in ' + str(file))
            parts = body.split()
            rule, deps = parts[0], [x for x in parts[1:] if x not in (b'|', b'||')]
            for token in outputs.split():
                edges[token] = (rule, deps)
                if token not in wanted:
                    continue
                if rule not in (b'cxx', b'cc', b'objcxx', b'objc'):
                    engine.fail('Selected GN edge is not a compiler')
                if not token.startswith(b'obj/') or b'$' in token:
                    engine.fail('Only default-toolchain compiler objects are supported')
                selected[token] = (file, index, parts)
    for file in sorted(files):
        load_file(file)
    if set(selected) != set(wanted):
        engine.fail('Compiler plan is missing selected GN edges')

    cache = {}

    def leaves(token, visiting=frozenset()):
        if token in cache:
            return cache[token]
        if token in visiting:
            engine.fail('Cyclic GN generation prerequisite')
        edge = edges.get(token)
        if not edge and token.startswith(b'phony/'):
            name = token.removeprefix(b'phony/').decode()
            for suffix in ('.inputdeps', '.linkdeps', '.inputs'):
                if name.endswith(suffix):
                    name = name[:-len(suffix)]
                    break
            if '..' in name.split('/') or name.startswith('/') or '\\' in name:
                engine.fail('Unsafe GN generation prerequisite')
            recipe = output / 'obj' / (name + '.ninja')
            if recipe.is_file():
                load_file(recipe)
            edge = edges.get(token)
            if not edge:
                engine.fail('Missing GN generation prerequisite: ' + token.decode())
        if not edge:
            return {token}
        if edge[0] != b'phony':
            return {token}
        result = set()
        for dep in edge[1]:
            result.update(leaves(dep, visiting | {token}))
        cache[token] = result
        return result

    def replace_include(data, old, new):
        lines = data.splitlines(keepends=True)
        found = [i for i, line in enumerate(lines)
                 if line.rstrip(b'\r\n') == b'subninja ' + old]
        if len(found) != 1:
            engine.fail('Unrecognised GN default toolchain scope/include')
        index = found[0]
        ending = b'\r\n' if lines[index].endswith(b'\r\n') else b'\n'
        lines[index] = b'subninja ' + new + ending
        return b''.join(lines)

    main = replace_include((output / 'build.ninja').read_bytes(),
                           b'toolchain.ninja', b'fly-object-check/toolchain.ninja')
    folder = output / 'fly-object-check'
    folder.mkdir(exist_ok=True)
    objects = {}
    prerequisites = set()
    recipes = {}
    gn_recipes = {}
    replacements = {}
    for original, (file, index, parts) in sorted(selected.items()):
        generated = set()
        for dep in parts[2:]:
            if dep in (b'|', b'||'):
                continue
            if dep.startswith(b'phony/'):
                generated.update(x for x in leaves(dep)
                                 if x.startswith(b'../../') or x.endswith(HEADER_SUFFIXES))
            else:
                generated.add(dep)
        prerequisites.update(generated)
        # Preserve the declared output as well as the recipe: GN's compiler
        # rules may derive their output from target_out_dir/label_name rather
        # than $out. Only prerequisite edges may change.
        recipe = (b'build ' + original + b': ' + parts[0] + b' ' + parts[1] +
                  (b' | ' + b' '.join(sorted(generated)) if generated else b'') + b'\n')
        replacements.setdefault(file, {})[index] = recipe
        objects[wanted[original]] = original.decode()
    toolchain = folder / 'toolchain.ninja'
    toolchain_data = (output / 'toolchain.ninja').read_bytes()
    for index, (file, changed) in enumerate(sorted(replacements.items())):
        name = 'target-' + str(index) + '.ninja'
        path = folder / name
        lines = file.read_bytes().splitlines(keepends=True)
        for line_index, recipe in changed.items():
            lines[line_index] = recipe
        path.write_bytes(b''.join(lines))
        toolchain_data = replace_include(
            toolchain_data, file.relative_to(output).as_posix().encode(),
            b'fly-object-check/' + name.encode())
        recipes[name] = engine.digest(path)
        gn_recipes[file.relative_to(output).as_posix()] = engine.digest(file)
    toolchain.write_bytes(toolchain_data)
    manifest = folder / 'build.ninja'
    manifest.write_bytes(main +
                         b'\nbuild fly-object-check: phony ' +
                         b' '.join(value.encode() for value in objects.values()) + b'\n')
    return {'manifest': 'fly-object-check/build.ninja', 'target': 'fly-object-check',
            'compiler_objects': objects, 'prerequisite_files': len(prerequisites),
            'manifest_sha256': engine.digest(manifest),
            'toolchain_sha256': engine.digest(toolchain), 'recipe_sha256': recipes,
            'gn_recipe_sha256': gn_recipes,
            'scope': 'Original GN compiler rules/flags with header and module generation; runtime-only data generation excluded'}
