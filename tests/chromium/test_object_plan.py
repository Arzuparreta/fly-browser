"""Exercise compiler prerequisite isolation with real Ninja scheduling."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'scripts/chromium'))
import object_plan


class CompilerPrerequisiteTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fly-compiler-plan-')
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        (self.output / 'obj').mkdir()
        (self.output / 'build.ninja').write_text('subninja toolchain.ninja\n')
        python = sys.executable.replace('$', '$$').replace(':', '$:').replace(' ', '$ ')
        # Ninja retains response-file expansion and rule scope. These Python
        # fixture actions verify scheduling without needing a host C++ SDK.
        (self.output / 'toolchain.ninja').write_text(
            'python = ' + python + '\n'
            'rule tool\n  command = "$python" fixture.py tool $out\n'
            'rule generate\n  command = "$python" fixture.py generate $out $in\n'
            'rule runtime\n  command = "$python" fixture.py runtime $out\n'
            'rule cxx\n  command = "$python" fixture.py compile $in $out @$out.rsp\n'
            '  rspfile = $out.rsp\n  rspfile_content = $defines $special\n'
            'build obj/generator-tool: tool\n'
            'build gen/value.h: generate obj/generator-tool\n'
            'build gen/module.pcm: generate obj/generator-tool\n'
            'build gen/blob.bin: runtime\n'
            'build phony/native.inputdeps: phony gen/value.h gen/module.pcm gen/blob.bin\n'
            'subninja obj/native.ninja\n')
        (self.output / 'obj/native.ninja').write_text(
            'defines = --scoped-flag\n'
            'build obj/native.o: cxx source.cc | phony/native.inputdeps\n'
            '  special = --edge-flag\n')
        (self.output / 'source.cc').write_text('fixture source')
        (self.output / 'fixture.py').write_text('''import sys
from pathlib import Path
mode = sys.argv[1]
if mode == 'tool':
    Path(sys.argv[2]).write_text('built tool')
elif mode == 'generate':
    assert Path(sys.argv[3]).read_text() == 'built tool'
    Path(sys.argv[2]).write_text('generated input')
elif mode == 'runtime':
    Path('runtime-was-invoked').write_text('unexpected')
    raise SystemExit(99)
elif mode == 'compile':
    assert Path('gen/value.h').read_text() == 'generated input'
    assert Path('gen/module.pcm').read_text() == 'generated input'
    assert Path(sys.argv[2]).read_text() == 'fixture source'
    flags = Path(sys.argv[4][1:]).read_text()
    assert flags == '--scoped-flag --edge-flag', flags
    Path(sys.argv[3]).write_text(flags)
''')
        self.targets = {'fly/native.cc': 'obj/native.o'}

    def test_real_ninja_builds_generator_tool_headers_modules_and_original_rsp_recipe(self):
        ninja = os.environ.get('FLY_TEST_NINJA') or shutil.which('ninja')
        if not ninja:
            self.skipTest('Ninja is unavailable on this host')
        original = (self.output / 'obj/native.ninja').read_bytes()
        plan = object_plan.compiler_plan(self.output, self.targets)
        self.assertEqual((self.output / 'obj/native.ninja').read_bytes(), original)
        result = subprocess.run([ninja, '-C', str(self.output), '-f', plan['manifest'],
                                 plan['target']], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        generated = self.output / plan['compiler_objects']['fly/native.cc']
        self.assertEqual(generated.read_text(), '--scoped-flag --edge-flag')
        self.assertFalse((self.output / 'runtime-was-invoked').exists())
        self.assertEqual(generated, self.output / 'obj/native.o')
        self.assertTrue((self.output / 'obj/generator-tool').exists())

    def test_gn_rules_derive_the_same_declared_output_from_target_variables(self):
        ninja = os.environ.get('FLY_TEST_NINJA') or shutil.which('ninja')
        if not ninja:
            self.skipTest('Ninja is unavailable on this host')
        file = self.output / 'toolchain.ninja'
        file.write_text(file.read_text().replace(
            'fixture.py compile $in $out @$out.rsp',
            'fixture.py compile $in ${target_out_dir}/${source_name_part}.o '
            '@${target_out_dir}/${source_name_part}.o.rsp'))
        file = self.output / 'obj/native.ninja'
        file.write_text('target_out_dir = obj\n' + file.read_text() +
                        '  source_name_part = native\n')
        plan = object_plan.compiler_plan(self.output, self.targets)
        subprocess.run([ninja, '-C', str(self.output), '-f', plan['manifest'],
                        plan['target']], check=True, capture_output=True)
        self.assertEqual(plan['compiler_objects'], self.targets)
        self.assertEqual((self.output / 'obj/native.o').read_text(),
                         '--scoped-flag --edge-flag')

    def test_generated_header_changes_invalidate_the_compiler_object(self):
        ninja = os.environ.get('FLY_TEST_NINJA') or shutil.which('ninja')
        if not ninja:
            self.skipTest('Ninja is unavailable on this host')
        plan = object_plan.compiler_plan(self.output, self.targets)
        command = [ninja, '-C', str(self.output), '-f', plan['manifest'], plan['target']]
        subprocess.run(command, check=True, capture_output=True)
        header = self.output / 'gen/value.h'
        header.write_text('broken generated header')
        obj = self.output / plan['compiler_objects']['fly/native.cc']
        os.utime(header, ns=(obj.stat().st_mtime_ns + 2_000_000_000,) * 2)
        failed = subprocess.run(command, capture_output=True)
        self.assertNotEqual(failed.returncode, 0)

    def test_preserves_non_utf8_gn_metadata_and_records_recipe_digest(self):
        file = self.output / 'obj/native.ninja'
        file.write_bytes(b'foreign_metadata = \xff\n' + file.read_bytes())
        plan = object_plan.compiler_plan(self.output, self.targets)
        recipe = self.output / 'fly-object-check/target-0.ninja'
        self.assertTrue(recipe.read_bytes().startswith(b'foreign_metadata = \xff\n'))
        self.assertEqual(len(plan['recipe_sha256']['target-0.ninja']), 64)
        self.assertEqual(plan['prerequisite_files'], 2)

    def test_repeated_plans_do_not_parse_their_own_generated_edges(self):
        first = object_plan.compiler_plan(self.output, self.targets)
        second = object_plan.compiler_plan(self.output, self.targets)
        self.assertEqual(first, second)

    def test_transitive_header_only_aliases_load_their_gn_target_recipe(self):
        file = self.output / 'obj/exports/header_files.ninja'
        file.parent.mkdir()
        file.write_text('build phony/exports/header_files: phony gen/value.h\n')
        toolchain = self.output / 'toolchain.ninja'
        text = toolchain.read_text().replace(
            'phony gen/value.h gen/module.pcm gen/blob.bin',
            'phony phony/exports/header_files gen/module.pcm gen/blob.bin')
        toolchain.write_text(text + 'subninja obj/exports/header_files.ninja\n')
        object_plan.compiler_plan(self.output, self.targets)
        recipe = (self.output / 'fly-object-check/target-0.ninja').read_bytes()
        self.assertIn(b'gen/value.h', recipe)
        self.assertNotIn(b'phony/native.inputdeps', recipe)
        self.assertNotIn(b'gen/blob.bin', recipe)

    def test_windows_line_endings_keep_toolchain_scope_and_compiler_variables(self):
        for file in self.output.rglob('*.ninja'):
            file.write_bytes(file.read_bytes().replace(b'\r\n', b'\n').replace(b'\n', b'\r\n'))
        plan = object_plan.compiler_plan(self.output, self.targets)
        recipe = (self.output / 'fly-object-check/target-0.ninja').read_bytes()
        self.assertIn(b'defines = --scoped-flag\r\n', recipe)
        self.assertIn(b'  special = --edge-flag\r\n', recipe)
        self.assertEqual(plan['compiler_objects'], self.targets)

    def test_missing_objects_and_unknown_rule_scopes_fail(self):
        with self.assertRaisesRegex(RuntimeError, 'missing selected GN edges'):
            object_plan.compiler_plan(self.output, {'missing': 'obj/missing.o'})
        (self.output / 'build.ninja').write_text('subninja renamed-toolchain.ninja\n')
        with self.assertRaisesRegex(RuntimeError, 'default toolchain scope'):
            object_plan.compiler_plan(self.output, self.targets)

    def test_cyclic_generator_dependencies_fail(self):
        file = self.output / 'toolchain.ninja'
        file.write_text(file.read_text().replace(
            'build phony/native.inputdeps: phony gen/value.h gen/module.pcm gen/blob.bin',
            'build phony/native.inputdeps: phony phony/cycle\n'
            'build phony/cycle: phony phony/native.inputdeps'))
        with self.assertRaisesRegex(RuntimeError, 'Cyclic GN generation prerequisite'):
            object_plan.compiler_plan(self.output, self.targets)


class GNSourceOwnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='fly-gn-owners-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root / 'src/out/Objects'
        self.file = self.output / 'obj/fly/commands.ninja'
        self.file.parent.mkdir(parents=True)
        self.file.write_text('build obj/fly/config.o: cxx ../../fly/config.cc\n')
        self.manifest = {'files': dict.fromkeys(['fly/config.cc', 'fly/config.h'])}

    def test_gn_source_owners_bound_recipe_reads_and_use_a_response_file(self):
        result = subprocess.CompletedProcess([], 0, stdout=(
            '//fly:commands\n//fly:commands(//build/toolchain:host)\n'))
        with mock.patch.object(object_plan.subprocess, 'run', return_value=result) as run:
            files, labels = object_plan.recipe_files(self.root, 'out/Objects',
                                                     self.manifest, {})
        self.assertEqual(files, [self.file])
        self.assertEqual(len(labels), 2)
        args = run.call_args.args[0]
        self.assertIn('--relation=source', args)
        self.assertIn('--default-toolchain', args)
        response = Path(next(arg[1:] for arg in args if arg.startswith('@')))
        self.assertEqual(response.read_text(), '//fly/config.cc\n')

    def test_missing_or_invalid_owners_fail_instead_of_scanning_another_toolchain(self):
        for stdout in ('//fly:missing\n', '//../outside:target\n', '',
                       '//fly:commands(//build/toolchain:host)\n'):
            with self.subTest(stdout=stdout):
                result = subprocess.CompletedProcess([], 0, stdout=stdout)
                with mock.patch.object(object_plan.subprocess, 'run', return_value=result):
                    with self.assertRaises(RuntimeError):
                        object_plan.recipe_files(self.root, 'out/Objects', self.manifest, {})


if __name__ == '__main__':
    unittest.main()
