import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/recompile'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import build_recompiler as tool
from hostenv import Host

MAC = Host('darwin', 'arm64')
LINUX = Host('linux', 'x86_64')
LINUX_ARM = Host('linux', 'arm64')
WINDOWS = Host('windows', 'x86_64')


class TargetTests(unittest.TestCase):
    def test_host_targets(self):
        self.assertEqual(tool.host_target(MAC), 'macos-arm64')
        self.assertEqual(tool.host_target(LINUX), 'linux-x86_64')
        self.assertEqual(tool.host_target(WINDOWS), 'windows-x86_64')
        with self.assertRaisesRegex(SystemExit, 'No N64Recomp release target'):
            tool.host_target(LINUX_ARM)

    def test_binary_names(self):
        self.assertEqual(tool.binary_name('windows-x86_64'), 'N64Recomp.exe')
        self.assertEqual(tool.binary_name('linux-x86_64'), 'N64Recomp')

    def test_build_modes_per_host(self):
        self.assertEqual(tool.mode_for('macos-arm64', MAC), 'native')
        self.assertEqual(tool.mode_for('linux-x86_64', LINUX), 'native')
        self.assertEqual(tool.mode_for('linux-x86_64', LINUX, docker=True), 'docker')
        self.assertEqual(tool.mode_for('linux-x86_64', MAC), 'docker')
        self.assertEqual(tool.mode_for('windows-x86_64', MAC), 'mingw')
        with self.assertRaisesRegex(SystemExit, 'macOS arm64 host'):
            tool.mode_for('macos-arm64', LINUX)

    def test_build_trees_do_not_collide(self):
        trees = {tool.build_dir('macos-arm64', 'native'), tool.build_dir('linux-x86_64', 'docker'),
                 tool.build_dir('linux-x86_64', 'native'), tool.build_dir('windows-x86_64', 'mingw')}
        self.assertEqual(len(trees), 4)

    def test_release_flags_make_portable_binaries(self):
        with mock.patch.object(tool.hostenv, 'llvm_bin', return_value=None):
            mac = tool.compiler_flags('macos-arm64', MAC, 'native')
            linux = tool.compiler_flags('linux-x86_64', LINUX, 'native')
            windows = tool.compiler_flags('windows-x86_64', MAC, 'mingw')
        self.assertIn('-DCMAKE_OSX_DEPLOYMENT_TARGET=13.0', mac)
        self.assertIn('-DCMAKE_EXE_LINKER_FLAGS=-static', linux)
        self.assertNotIn('-DCMAKE_EXE_LINKER_FLAGS=-static', windows)  # the mingw toolchain file already links statically
        for flags in (mac, linux, windows):
            self.assertIn('-DCMAKE_BUILD_TYPE=Release', flags)
            self.assertIn('-DCMAKE_CXX_FLAGS=-include cstdlib', flags)

    def test_unsupported_hosts_get_a_generic_tree(self):
        self.assertEqual(tool.host_binary(LINUX_ARM), tool.host_tree(LINUX_ARM) / 'N64Recomp')
        self.assertEqual(tool.host_binary(MAC), tool.binary_path('macos-arm64', MAC))


class LauncherTests(unittest.TestCase):
    def test_native_where_the_host_can_execute_the_binary(self):
        for target, host in (('macos-arm64', MAC), ('linux-x86_64', LINUX), ('windows-x86_64', WINDOWS)):
            launch, runner = tool.launcher(target, host, ())
            self.assertEqual(runner, 'native')
            self.assertEqual(launch(Path('/b'), Path('/c')), ['/b', '/c'])

    def test_linux_binary_runs_in_an_amd64_container_elsewhere(self):
        launch, runner = tool.launcher('linux-x86_64', MAC, ('/roms:/roms:ro',))
        command = launch(Path('/repo/b'), Path('/repo/c'))
        self.assertEqual(runner, 'docker linux/amd64')
        self.assertEqual(command[:2], ['docker', 'run'])
        self.assertEqual(command[command.index('--platform') + 1], 'linux/amd64')
        self.assertIn('/roms:/roms:ro', command)
        self.assertEqual(command[-2:], ['/repo/b', '/repo/c'])

    def test_windows_binary_runs_under_wine_in_a_container_on_posix_hosts(self):
        with mock.patch.object(tool.dockerenv, 'ensure_image', return_value='sc64-wine:t') as image, \
                mock.patch.object(tool, 'init_wine_prefix'):
            launch, runner = tool.launcher('windows-x86_64', MAC, ())
        command = launch(Path('/repo/N64Recomp.exe'), Path('/repo/c'))
        self.assertEqual(runner, 'wine in docker linux/amd64')
        self.assertEqual(image.call_args.args[3], 'linux/amd64')
        self.assertEqual(command[command.index('sc64-wine:t') + 1:],
                         ['env', *tool.WINE_ENV, tool.WINE_BINARY, '/repo/N64Recomp.exe', '/repo/c'])

    def test_foreign_binaries_are_refused_without_a_runner(self):
        with self.assertRaisesRegex(SystemExit, 'cannot run'):
            tool.launcher('macos-arm64', LINUX, ())
        with self.assertRaisesRegex(SystemExit, 'Windows host'):
            tool.launcher('linux-x86_64', WINDOWS, ())


class VerifyTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(__import__('shutil').rmtree, self.directory)
        self.binary = self.directory / 'N64Recomp'
        self.binary.write_bytes(b'binary')
        self.rom = self.directory / 'rom.z64'
        self.rom.write_bytes(b'rom')

    def write_receipt(self, digest):
        (self.directory / 'receipt.json').write_text(json.dumps({'binary_sha256': digest, 'recipe_verification': None}))

    def test_success_is_recorded_in_the_receipt(self):
        self.write_receipt(tool.sha256(self.binary))
        with mock.patch.object(tool.recipe, 'generate', return_value={'files': {'a': '1', 'b': '2'}}) as generate:
            result = tool.verify('macos-arm64', self.rom, MAC, self.binary)
        self.assertEqual(result['generated_files'], 2)
        self.assertEqual(generate.call_args.args[0], self.rom)
        receipt = json.loads((self.directory / 'receipt.json').read_text())
        self.assertEqual(receipt['recipe_verification']['runner'], 'native')
        self.assertEqual(receipt['recipe_verification']['rom_sha1'], tool.recipe.ROM_SHA1)

    def test_a_receipt_for_another_binary_is_not_updated(self):
        self.write_receipt('0' * 64)
        with mock.patch.object(tool.recipe, 'generate', return_value={'files': {}}):
            with self.assertRaisesRegex(SystemExit, 'different binary'):
                tool.verify('macos-arm64', self.rom, MAC, self.binary)

    def test_hash_mismatch_propagates_and_leaves_the_receipt_unverified(self):
        self.write_receipt(tool.sha256(self.binary))
        with mock.patch.object(tool.recipe, 'generate', side_effect=tool.recipe.RecipeError('hash differs')):
            with self.assertRaisesRegex(SystemExit, 'hash differs'):
                tool.verify('macos-arm64', self.rom, MAC, self.binary)
        self.assertIsNone(json.loads((self.directory / 'receipt.json').read_text())['recipe_verification'])

    def test_missing_binary_names_the_build_command(self):
        with self.assertRaisesRegex(SystemExit, 'build --target linux-x86_64'):
            tool.verify('linux-x86_64', self.rom, LINUX, self.directory / 'absent')

    def test_rom_outside_the_repository_is_mounted_read_only_for_containers(self):
        self.write_receipt(tool.sha256(self.binary))
        with mock.patch.object(tool.recipe, 'generate', return_value={'files': {}}) as generate:
            tool.verify('linux-x86_64', self.rom, MAC, self.binary)
        command = generate.call_args.kwargs['launch'](self.binary, Path('/repo/config'))
        self.assertIn(f'{self.rom.parent}:{self.rom.parent}:ro', command)


if __name__ == '__main__':
    unittest.main()
