import hashlib
import importlib.util
import json
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/recompile'))
spec = importlib.util.spec_from_file_location('sc64_recipe', ROOT / 'tools/recompile/recipe.py')
recipe = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = recipe
spec.loader.exec_module(recipe)

SYMS = '''# Private ROM-derived metadata. Do not publish this file.

[[section]]
name = "resident"
rom = 0x1000
vram = 0x80000400
size = 0x100
functions = [
  { name = "sc64_resident_80000400", vram = 0x80000400, size = 0x3C },
]
'''
CONFIG = '''[input]
entrypoint = 0x80000400
symbols_file_path = "sc64.syms.toml"
rom_file_path = "/some/dir/rom.z64"
output_func_path = "../../generated/game"
functions_per_output_file = 50
uses_mips3_float_mode = false

[patches]
stubs = []
ignored = []
renamed = []

[[patches.instruction]]
func = "sc64_resident_80000400"
vram = 0x80000404
value = 0

[[patches.hook]]
func = "sc64_resident_80000400"
before_vram = 0x80000408
text = "extern void sc64_ui_pick(uint8_t*, recomp_context*, unsigned); sc64_ui_pick(rdram, ctx, 3);"
'''
GENERATED = {
    'funcs.h': 'void f();\n',
    'funcs_0.c': 'RECOMP_FUNC void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx) {}\n',
    'lookup.cpp': '// lookup\n',
    'recomp_overlays.inl': 'static SectionTableEntry section_table[] = {};\n',
}


def write_generated(directory: Path) -> dict[str, str]:
    directory.mkdir(parents=True, exist_ok=True)
    for name, text in GENERATED.items():
        (directory / name).write_text(text)
    return recipe.hashes(directory)


class Workspace(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(shutil.rmtree, self.directory)

    def make_analysis(self, *, with_generation=True) -> tuple[Path, Path]:
        analysis = self.directory / 'analysis'
        shutil.rmtree(analysis, ignore_errors=True)
        analysis.mkdir()
        (analysis / 'sc64.syms.toml').write_text(SYMS)
        (analysis / 'sc64-ui.toml').write_text(CONFIG)
        generated = self.directory / 'generated'
        shutil.rmtree(generated, ignore_errors=True)
        files = write_generated(generated)
        if with_generation:
            scratch = analysis / 'recompile-abc'
            scratch.mkdir()
            (scratch / 'generation.json').write_text(json.dumps({
                'config_sha256': hashlib.sha256(CONFIG.encode()).hexdigest(), 'files': files}))
        return analysis, generated

    def make_recipe(self) -> Path:
        analysis, generated = self.make_analysis()
        shutil.rmtree(self.directory / 'recipe', ignore_errors=True)
        return recipe.export(analysis, generated, self.directory / 'recipe')


class CommittedRecipeTests(unittest.TestCase):
    def test_committed_recipe_is_whitelisted_metadata_with_matching_hashes(self):
        manifest = recipe.check()
        self.assertEqual(manifest['rom_sha1'], 'a36d91b5aac7bb95df0d7de5f9814f5270e25c2b')
        self.assertGreater(len(manifest['files']), 80)

    def test_committed_recipe_holds_no_instruction_words_or_byte_blobs(self):
        text = '\n'.join(path.read_text() for path in recipe.RECIPE.iterdir() if path.suffix == '.toml')
        self.assertNotRegex(text, r'original_word|body_sha256')
        # Every hex literal is an address, size or section offset: at most 8 digits.
        self.assertFalse(re.search(r'0x[0-9A-Fa-f]{9,}', text))


class WhitelistTests(Workspace):
    def edit(self, name: str, replace) -> Path:
        directory = self.make_recipe()
        path = directory / name
        path.write_text(replace(path.read_text()))
        return directory

    def test_exported_recipe_passes(self):
        self.assertEqual(set(recipe.check(self.make_recipe())['files']), set(GENERATED))

    def test_nonzero_instruction_patch_is_rejected(self):
        directory = self.edit('sc64.toml', lambda text: text.replace('value = 0', 'value = 3'))
        with self.assertRaisesRegex(SystemExit, 'whitelist'):
            recipe.check(directory)

    def test_arbitrary_hook_text_is_rejected(self):
        directory = self.edit('sc64.toml', lambda text: text.replace('sc64_ui_pick(rdram, ctx, 3);', 'system("x");'))
        with self.assertRaisesRegex(SystemExit, 'whitelist'):
            recipe.check(directory)

    def test_extra_comment_or_field_is_rejected(self):
        directory = self.edit('sc64.syms.toml', lambda text: text + '# 3C0A8000 27BDFFE0\n')
        with self.assertRaisesRegex(SystemExit, 'whitelist'):
            recipe.check(directory)
        directory = self.edit('sc64.syms.toml', lambda text: text.replace('size = 0x3C }', 'size = 0x3C, bytes = "00" }'))
        with self.assertRaisesRegex(SystemExit, 'whitelist'):
            recipe.check(directory)

    def test_extra_file_is_rejected(self):
        directory = self.make_recipe()
        (directory / 'rom.bin').write_bytes(b'\0')
        with self.assertRaisesRegex(SystemExit, 'must hold exactly'):
            recipe.check(directory)

    def test_stale_manifest_hash_is_rejected(self):
        directory = self.make_recipe()
        manifest = json.loads((directory / 'generated.json').read_text())
        manifest['symbols_sha256'] = '0' * 64
        (directory / 'generated.json').write_text(json.dumps(manifest))
        with self.assertRaisesRegex(SystemExit, 'does not match'):
            recipe.check(directory)

    def test_placeholder_must_appear_once(self):
        directory = self.edit('sc64.toml', lambda text: text.replace('[patches]\n', 'rom_file_path = "@ROM@"\n[patches]\n', 1))
        with self.assertRaisesRegex(SystemExit, 'exactly once'):
            recipe.check(directory)


class ConfigTests(unittest.TestCase):
    def test_templatize_replaces_machine_paths_only(self):
        template = recipe.templatize(CONFIG)
        self.assertIn('rom_file_path = "@ROM@"', template)
        self.assertIn('output_func_path = "@OUTPUT@"', template)
        self.assertNotIn('/some/dir', template)
        self.assertIn('entrypoint = 0x80000400', template)

    def test_templatize_requires_both_paths(self):
        with self.assertRaisesRegex(SystemExit, 'rom_file_path'):
            recipe.templatize('[input]\noutput_func_path = "x"\n')

    def test_render_quotes_windows_and_unicode_paths(self):
        template = recipe.templatize(CONFIG)
        rendered = recipe.render_config(template, Path('C:\\Users\\Игрок\\sc64 "x".z64'), Path('C:\\out'))
        self.assertIn('rom_file_path = "C:\\\\Users\\\\Игрок\\\\sc64 \\"x\\".z64"', rendered)
        self.assertIn('output_func_path = "C:\\\\out"', rendered)
        try:
            import tomllib
        except ImportError:  # Python 3.10 has no TOML parser; the string checks above still apply
            return
        self.assertEqual(tomllib.loads(rendered)['input']['rom_file_path'], 'C:\\Users\\Игрок\\sc64 "x".z64')


class ExportTests(Workspace):
    def test_export_writes_neutral_header_and_template(self):
        directory = self.make_recipe()
        self.assertEqual((directory / 'sc64.syms.toml').read_text().splitlines()[0], recipe.HEADER)
        self.assertNotIn('Private ROM-derived', (directory / 'sc64.syms.toml').read_text())
        self.assertIn('@ROM@', (directory / 'sc64.toml').read_text())

    def test_export_requires_a_generation_of_this_config(self):
        analysis, generated = self.make_analysis(with_generation=False)
        with self.assertRaisesRegex(SystemExit, 'No generation.json'):
            recipe.export(analysis, generated, self.directory / 'recipe')

    def test_export_rejects_generated_dir_that_differs(self):
        analysis, generated = self.make_analysis()
        (generated / 'funcs.h').write_text('changed\n')
        with self.assertRaisesRegex(SystemExit, 'differs from the generation'):
            recipe.export(analysis, generated, self.directory / 'recipe')
        self.assertFalse((self.directory / 'recipe').exists())

    def test_compare_reports_each_kind_of_difference(self):
        self.assertEqual(recipe.compare({'a': '1', 'b': '2'}, {'a': '1', 'b': '2'}), [])
        problems = recipe.compare({'a': '1', 'b': '2'}, {'a': '9', 'c': '3'})
        self.assertEqual(problems, ['missing b', 'unexpected c', 'hash differs: a'])


class GenerateTests(Workspace):
    def setUp(self):
        super().setUp()
        self.recipe_dir = self.make_recipe()
        self.rom = self.directory / 'rom.z64'
        self.rom.write_bytes(b'rom')
        self.scratch = self.directory / 'scratch'
        self.calls = []
        self.damage = None
        patcher = mock.patch.object(recipe, 'sha1_file', return_value=recipe.ROM_SHA1)
        patcher.start()
        self.addCleanup(patcher.stop)
        patcher = mock.patch.object(recipe.subprocess, 'run', side_effect=self.fake_recompiler)
        patcher.start()
        self.addCleanup(patcher.stop)

    def fake_recompiler(self, command, **kwargs):
        config = Path(command[1])
        text = config.read_text()
        self.calls.append(text)
        output = Path(re.search(r'output_func_path = "((?:[^"\\]|\\.)*)"', text).group(1).replace('\\\\', '\\'))
        write_generated(output)
        if self.damage:
            with (output / self.damage).open('a') as stream:
                stream.write('// tampered\n')
        return mock.Mock(returncode=0)

    def run_generate(self, output=None, **kwargs):
        return recipe.generate(self.rom, Path('N64Recomp'), output, recipe=self.recipe_dir, scratch=self.scratch, **kwargs)

    def test_verify_only_leaves_no_scratch(self):
        report = self.run_generate()
        self.assertEqual(set(report['files']), set(GENERATED))
        self.assertEqual(list(self.scratch.iterdir()), [])
        self.assertIn(str(self.rom.resolve()).replace('\\', '\\\\'), self.calls[0])
        self.assertNotIn('@ROM@', self.calls[0])

    def test_output_is_published(self):
        output = self.directory / 'game'
        self.run_generate(output)
        self.assertEqual(recipe.hashes(output), recipe.check(self.recipe_dir)['files'])

    def test_hash_mismatch_is_rejected_and_kept_for_diagnosis(self):
        self.damage = 'funcs_0.c'
        output = self.directory / 'game'
        with self.assertRaisesRegex(SystemExit, 'hash differs: funcs_0.c'):
            self.run_generate(output)
        self.assertFalse(output.exists())
        self.assertEqual(len(list(self.scratch.iterdir())), 1)

    def test_existing_output_needs_replace_and_keeps_previous(self):
        output = self.directory / 'game'
        output.mkdir()
        (output / 'old.c').write_text('old\n')
        with self.assertRaisesRegex(SystemExit, 'already exists'):
            self.run_generate(output)
        self.assertEqual(self.calls, [])
        self.run_generate(output, replace=True)
        self.assertFalse((output / 'old.c').exists())
        previous = next(self.scratch.glob('recipe-*/previous-output'))
        self.assertTrue((previous / 'old.c').is_file())

    def test_wrong_rom_is_rejected_before_running_the_recompiler(self):
        with mock.patch.object(recipe, 'sha1_file', return_value='0' * 40):
            with self.assertRaisesRegex(SystemExit, 'Unsupported ROM'):
                self.run_generate()
        self.assertEqual(self.calls, [])

    def test_recompiler_failure_is_reported(self):
        recipe.subprocess.run.side_effect = lambda command, **kwargs: mock.Mock(returncode=3)
        with self.assertRaisesRegex(SystemExit, 'exited 3'):
            self.run_generate()


if __name__ == '__main__':
    unittest.main()
