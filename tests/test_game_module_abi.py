import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MODULE = ROOT / 'native/module'
REAL_SECTIONS = ROOT / 'local/deps/N64ModernRuntime-shell/librecomp/include/librecomp/sections.h'
GENERATED = ROOT / 'local/generated/game'
ENTRY = re.compile(r'^SC64_(GUEST_CALL\((\w+)\)|IMPORT_[VR]\((?:[\w*]+, )?(\w+),)', re.M)


def imports() -> list[str]:
    return [match.group(2) or match.group(3) for match in ENTRY.finditer((MODULE / 'sc64_imports.def').read_text())]


def struct_bodies(text: str) -> dict[str, str]:
    """`typedef struct {...} Name;` and `typedef enum {...} Name;` bodies without comments or blank space."""
    text = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    return {name: re.sub(r'\s+', ' ', body).strip()
            for body, name in re.findall(r'typedef (?:struct|enum) \{(.*?)\} (\w+);', text, flags=re.S)}


class ImportListTests(unittest.TestCase):
    def test_names_are_unique_and_guest_calls_use_the_recomp_suffix(self):
        names = imports()
        self.assertGreater(len(names), 60)
        self.assertEqual(len(names), len(set(names)))
        text = (MODULE / 'sc64_imports.def').read_text()
        for match in re.finditer(r'^SC64_GUEST_CALL\((\w+)\)', text, re.M):
            self.assertTrue(match.group(1).endswith('_recomp'), match.group(1))

    def test_every_entry_is_used_by_the_header_trampolines_and_host(self):
        for name in ('module_main.c', 'sc64_module.h'):
            self.assertIn('sc64_imports.def', (MODULE / name).read_text())
        self.assertIn('sc64_imports.def', (ROOT / 'native/shell/game_module.cpp').read_text())

    @unittest.skipUnless(GENERATED.joinpath('funcs.h').is_file(), 'needs the private generated game')
    def test_the_generated_game_calls_only_imported_host_functions(self):
        wanted = set(re.findall(r'\b(\w+_recomp)\b', (GENERATED / 'funcs.h').read_text()))
        wanted |= set(re.findall(r'\b(\w+_recomp)\b', (GENERATED / 'recomp_overlays.inl').read_text()))
        for source in GENERATED.glob('funcs_*.c'):
            wanted |= set(re.findall(r'extern void (sc64_\w+)\(', source.read_text()))
        wanted -= {name for name in wanted if name.startswith(('sc64_resident_', 'sc64_menu_', 'sc64_game_'))}
        missing = sorted(wanted - set(imports()))
        self.assertEqual(missing, [], 'add these to native/module/sc64_imports.def and bump SC64_MODULE_ABI')


class SectionLayoutTests(unittest.TestCase):
    @unittest.skipUnless(REAL_SECTIONS.is_file(), 'needs the prepared runtime')
    def test_shim_matches_the_runtime_header(self):
        shim = struct_bodies((MODULE / 'include/librecomp/sections.h').read_text())
        real = struct_bodies(REAL_SECTIONS.read_text())
        for name in ('FuncEntry', 'RelocEntryType', 'RelocEntry', 'SectionTableEntry'):
            self.assertEqual(shim[name], real[name], name)


class BuildFlagTests(unittest.TestCase):
    """sc64_setup compiles the module with the flags cmake/game_module.cmake uses for the developer build."""
    def test_setup_and_cmake_agree_on_the_flags_that_change_the_generated_code(self):
        setup = (ROOT / 'native/setup/toolchain.cpp').read_text()
        cmake = (ROOT / 'cmake/game_module.cmake').read_text()
        for flag in ('-O2', '-fno-strict-aliasing', '-ffp-contract=off', '-fexceptions'):
            self.assertIn(f'"{flag}"', setup, flag)
            self.assertIn(flag, cmake, flag)
        self.assertIn('"-fvisibility=hidden"', setup)
        self.assertIn('C_VISIBILITY_PRESET hidden', cmake)

    def test_setup_stages_every_file_the_module_build_includes(self):
        staged = (ROOT / 'cmake/setup.cmake').read_text()
        for name in ('module_main.c', 'sc64_module.h', 'sc64_imports.def', 'recomp.h', 'sections.h'):
            self.assertIn(name, staged, name)
        self.assertIn('"-I" + sources.module_dir', (ROOT / 'native/setup/toolchain.cpp').read_text())


class ProbeTests(unittest.TestCase):
    """The compiler probe must build the shapes the recompiler emits, so a compiler that cannot is rejected up front."""
    def test_the_probe_contains_the_hook_blocks_of_the_recipe(self):
        probe = (ROOT / 'native/setup/toolchain.cpp').read_text()
        recipe = (ROOT / 'recipe/ntsc-u/sc64.toml').read_text()
        hooks = re.findall(r'text = "(\{ extern void [^"]+\})"', recipe)
        self.assertGreater(len(hooks), 20)
        # Every hook is a block (valid after a label in any C version); the probe has one of the same form.
        self.assertEqual(len(re.findall(r'^text = ', recipe, re.M)), len(hooks))
        self.assertIn('{ extern void sc64_guest_poll(uint8_t*); sc64_guest_poll(rdram); }', hooks)
        self.assertIn('{ extern void sc64_guest_poll(uint8_t*); sc64_guest_poll(rdram); }', probe)
        self.assertRegex(probe, r'L_80000420:\n    \{ extern')

    def test_the_probe_uses_the_macros_of_the_pinned_recomp_header(self):
        header = (ROOT / 'local/deps/N64ModernRuntime/N64Recomp/include/recomp.h')
        if not header.is_file():
            self.skipTest('needs the prepared runtime')
        text = header.read_text()
        probe = (ROOT / 'native/setup/toolchain.cpp').read_text()
        for macro in ('RECOMP_FUNC', 'S32', 'ADD32', 'MEM_W', 'CVT_S_W', 'LOOKUP_FUNC'):
            self.assertRegex(text, rf'define {macro}\b')
            self.assertIn(macro, probe)


class SourcesTests(unittest.TestCase):
    def test_module_is_plain_c_without_runtime_headers(self):
        source = (MODULE / 'module_main.c').read_text()
        self.assertNotIn('librecomp/', source.replace('"recomp_overlays.inl"', ''))
        for header in (MODULE / 'sc64_module.h', MODULE / 'include/librecomp/sections.h'):
            self.assertNotIn('ultramodern', header.read_text())

    def test_abi_version_is_declared_once(self):
        self.assertEqual(len(re.findall(r'#define SC64_MODULE_ABI \d+', (MODULE / 'sc64_module.h').read_text())), 1)


if __name__ == '__main__':
    unittest.main()
