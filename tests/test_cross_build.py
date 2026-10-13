import importlib.util
import io
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
spec = importlib.util.spec_from_file_location('sc64_cross_build', ROOT / 'tools/desktop/cross_build.py')
cross = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = cross
spec.loader.exec_module(cross)
Host = cross.Host

MAC, LINUX, LINUX_ARM, WINDOWS = (Host('darwin', 'arm64'), Host('linux', 'x86_64'),
                                  Host('linux', 'arm64'), Host('windows', 'x86_64'))


class PlanTests(unittest.TestCase):
    def test_windows_always_uses_mingw(self):
        for host in (MAC, LINUX, WINDOWS):
            with self.subTest(host=host.label):
                plan = cross.plan_for('windows', host, docker=True)
                self.assertEqual(plan.mode, 'mingw')
                self.assertEqual(plan.strip, 'x86_64-w64-mingw32-strip')
                self.assertEqual(plan.toolchain.name, 'mingw-w64-x86_64.cmake')

    def test_linux_is_native_only_on_a_linux_x86_64_host(self):
        self.assertEqual(cross.plan_for('linux', LINUX).mode, 'native')
        for host in (MAC, LINUX_ARM, WINDOWS):
            with self.subTest(host=host.label):
                self.assertEqual(cross.plan_for('linux', host).mode, 'docker')

    def test_docker_can_be_forced_on_linux_x86_64(self):
        plan = cross.plan_for('linux', LINUX, docker=True)
        self.assertEqual((plan.mode, plan.strip), ('docker', 'x86_64-linux-gnu-strip'))

    def test_every_toolchain_file_exists(self):
        for toolchain, _ in cross.TOOLCHAINS.values():
            self.assertTrue((ROOT / toolchain).is_file(), toolchain)


class ContainerTests(unittest.TestCase):
    def test_posix_hosts_mount_the_repository_at_its_own_path(self):
        path = cross.ROOT / 'local/build/linux'
        for host in (MAC, LINUX):
            self.assertEqual(cross.container_path(path, host), str(path))
        command = cross.in_container('img', ['true'], LINUX_ARM)
        self.assertIn(f'{cross.ROOT}:{cross.ROOT}', command)
        self.assertEqual(command[command.index('--platform') + 1], 'linux/arm64')
        self.assertIn('--user', command)

    def test_windows_host_maps_into_work_and_has_no_uid(self):
        path = cross.ROOT / 'local/build/linux'
        self.assertEqual(cross.container_path(path, WINDOWS), '/work/local/build/linux')
        command = cross.in_container('img', ['true'], WINDOWS)
        self.assertNotIn('--user', command)
        self.assertEqual(command[command.index('--platform') + 1], 'linux/amd64')
        self.assertEqual(command[command.index('-w') + 1], '/work')

    def test_runner_translates_paths_only_inside_the_container(self):
        target = cross.ROOT / 'local/dist/x'
        direct = cross.Runner(WINDOWS)
        self.assertEqual(direct.command(['strip', target]), ['strip', str(target)])
        contained = cross.Runner(WINDOWS, 'img')
        self.assertEqual(contained.command(['strip', target])[-2:], ['strip', '/work/local/dist/x'])


class ModulePackagingTests(unittest.TestCase):
    def test_every_package_target_ships_its_game_module(self):
        self.assertEqual({name: target['module'] for name, target in cross.TARGETS.items()},
                         {'windows': 'sc64-game.dll', 'linux': 'sc64-game.so'})

    def test_module_names_match_the_loader(self):
        source = (ROOT / 'native/shell/game_module.cpp').read_text()
        for target in cross.TARGETS.values():
            self.assertIn(f'"{target["module"]}"', source)

    def test_module_is_a_data_file_in_the_archive(self):
        self.assertEqual(cross.tar_member(tarfile.TarInfo('StarCraft64-linux/sc64-game.so')).mode, 0o644)

    def test_audit_hashes_the_module_with_the_other_members(self):
        with tempfile.TemporaryDirectory() as directory:
            module = Path(directory) / 'sc64-game.so'
            module.write_bytes(b'\x7fELFmodule')
            self.assertEqual(set(cross.audit([module])), {'sc64-game.so'})


class ReceiptHelperTests(unittest.TestCase):
    def test_configured_compiler_reads_the_detection_file(self):
        with tempfile.TemporaryDirectory() as directory:
            build = Path(directory)
            self.assertIsNone(cross.configured_compiler(build))
            detected = build / 'CMakeFiles/3.31.6'
            detected.mkdir(parents=True)
            (detected / 'CMakeCXXCompiler.cmake').write_text(
                'set(CMAKE_CXX_COMPILER "/usr/bin/x86_64-linux-gnu-g++")\nset(CMAKE_CXX_COMPILER_ID "GNU")\n')
            self.assertEqual(cross.configured_compiler(build), '/usr/bin/x86_64-linux-gnu-g++')

    def test_tar_modes_make_the_binary_executable(self):
        buffer = io.BytesIO()
        with tempfile.TemporaryDirectory() as directory:
            for name in ('StarCraft64', 'DejaVuSansMono-Bold.ttf'):
                (Path(directory) / name).write_bytes(b'x')
                (Path(directory) / name).chmod(0o600)
            with tarfile.open(fileobj=buffer, mode='w') as archive:
                for name in ('StarCraft64', 'DejaVuSansMono-Bold.ttf'):
                    archive.add(Path(directory) / name, f'StarCraft64-linux/{name}', filter=cross.tar_member)
        buffer.seek(0)
        with tarfile.open(fileobj=buffer) as archive:
            modes = {m.name: m.mode for m in archive.getmembers()}
        self.assertEqual(modes, {'StarCraft64-linux/StarCraft64': 0o755,
                                 'StarCraft64-linux/DejaVuSansMono-Bold.ttf': 0o644})

    def test_audit_rejects_private_inputs(self):
        with tempfile.TemporaryDirectory() as directory:
            rom = Path(directory) / 'game.z64'
            rom.write_bytes(b'\x80\x37\x12\x40rest')
            with self.assertRaisesRegex(ValueError, 'Forbidden private input'):
                cross.audit([rom])


if __name__ == '__main__':
    unittest.main()
