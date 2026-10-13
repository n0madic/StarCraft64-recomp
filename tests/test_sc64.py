import contextlib
import importlib.util
import io
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
spec = importlib.util.spec_from_file_location('sc64_driver', ROOT / 'tools/sc64.py')
driver = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = driver
spec.loader.exec_module(driver)
Host = driver.Host

MAC, LINUX, LINUX_ARM, WINDOWS = (Host('darwin', 'arm64'), Host('linux', 'x86_64'),
                                  Host('linux', 'arm64'), Host('windows', 'x86_64'))


def commands(host, *argv, env=None):
    """The command lines a driver invocation would run on ``host``."""
    with mock.patch.dict(os.environ, env or {}, clear=False):
        args = driver.parser().parse_args(argv)
        return args.handler(host, args)


def names(command_lines):
    return [Path(line[1]).name if line[0] == sys.executable else line[0] for line in command_lines]


class BuildDispatchTests(unittest.TestCase):
    def test_build_makes_the_package_of_the_host(self):
        self.assertEqual(names(commands(MAC, 'build')), ['build.py'])
        self.assertEqual(names(commands(LINUX, 'build')), ['cross_build.py'])
        self.assertEqual(commands(LINUX, 'build')[0][2], 'linux')
        self.assertEqual(commands(WINDOWS, 'build')[0][2], 'windows')

    def test_build_macos_is_explicit_and_macos_only(self):
        self.assertEqual(names(commands(MAC, 'build-macos')), ['build.py'])
        for host in (LINUX, LINUX_ARM, WINDOWS, Host('darwin', 'x86_64')):
            with self.subTest(host=host.label), self.assertRaisesRegex(SystemExit, 'needs a macOS arm64 host'):
                commands(host, 'build-macos')

    def test_build_refuses_a_host_without_a_package_profile(self):
        with self.assertRaisesRegex(SystemExit, 'No native package profile'):
            commands(LINUX_ARM, 'build')

    def test_explicit_targets_work_from_any_host(self):
        for host in (MAC, LINUX, LINUX_ARM, WINDOWS):
            with self.subTest(host=host.label):
                self.assertEqual(commands(host, 'build-windows')[0][2], 'windows')
                self.assertEqual(commands(host, 'build-linux')[0][2], 'linux')

    def test_options_reach_the_tool(self):
        line = commands(LINUX, 'build-linux', '--jobs', '3', '--build-type', 'Debug', '--docker')[0]
        self.assertEqual(line[3:], ['--jobs', '3', '--build-type', 'Debug', '--docker'])

    def test_environment_supplies_defaults(self):
        line = commands(MAC, 'build', env={'JOBS': '5', 'BUILD_TYPE': 'RelWithDebInfo'})[0]
        self.assertEqual(line[2:], ['--build-type', 'RelWithDebInfo', '--jobs', '5'])

    def test_invalid_build_type_is_rejected(self):
        with self.assertRaises(SystemExit), contextlib.redirect_stderr(io.StringIO()):
            commands(MAC, 'build', '--build-type', 'Fast')


class AppImageTests(unittest.TestCase):
    def test_appimage_rebuilds_linux_first(self):
        lines = commands(LINUX, 'build-appimage')
        self.assertEqual(names(lines), ['cross_build.py', 'appimage.py'])

    def test_bundled_rom_requires_the_dump(self):
        with self.assertRaisesRegex(SystemExit, 'requires --rom'):
            commands(LINUX, 'build-appimage', '--bundle-rom')
        with mock.patch.dict(os.environ, {'ROM': '/tmp/game.z64', 'BUNDLE_ROM': '1'}):
            lines = commands(LINUX, 'build-appimage')
        self.assertEqual(lines[1][2:], ['--bundle-rom', '/tmp/game.z64'])

    def test_bundle_rom_flag_must_be_boolean(self):
        with mock.patch.dict(os.environ, {'BUNDLE_ROM': 'yes'}):
            with self.assertRaisesRegex(SystemExit, 'must be 0 or 1'):
                driver.parser()


class RunAndTestTests(unittest.TestCase):
    def test_run_forwards_rom_storage_and_duration(self):
        line = commands(LINUX, 'run', '--rom', '/g.z64', '--seconds', '9', '--report', '/r/new')[0]
        self.assertEqual(line[1].split('/')[-1], 'run.py')
        self.assertEqual(line[2:4], ['--rom', '/g.z64'])
        self.assertIn('--seconds', line)
        self.assertEqual(line[line.index('--output') + 1], '/r/new')

    def test_run_without_rom_leaves_the_chooser_to_the_app(self):
        self.assertNotIn('--rom', commands(MAC, 'run')[0])

    def test_icon_requires_the_dump(self):
        with self.assertRaisesRegex(SystemExit, 'requires --rom'):
            commands(MAC, 'icon')

    def test_native_tests_configure_build_then_run_ctest(self):
        lines = commands(LINUX, 'test-native')
        self.assertEqual(names(lines), ['build.py', 'cmake', 'ctest'])
        self.assertIn('--configure-only', lines[0])
        self.assertEqual(lines[1][lines[1].index('--target') + 1:lines[1].index('-j')], list(driver.NATIVE_TEST_TARGETS))

    def test_native_test_list_matches_cmake(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        shell = (ROOT / 'cmake/shell.cmake').read_text()
        for target in driver.NATIVE_TEST_TARGETS:
            if target in ('sc64_game_module_test', 'sc64_setup_test', 'sc64_game_build_test'):
                self.assertIn(f'add_executable({target} ', shell + (ROOT / 'cmake/setup.cmake').read_text())
            elif target not in ('sc64_host_telemetry_test', 'sc64_loop_watch_test'):
                self.assertIn(target[len('sc64_'):-len('_test')], cmake, target)


def clean_env(**extra):
    """Environment without any variable the driver reads, so tests do not depend on the caller's shell."""
    names = ('ALLOW_DIRTY', 'DATA_DIR', 'FORCE', 'USE_ZIG', 'ROM', 'BUNDLE_ROM', 'RECOMP_PYTHON', 'BASELINE_REPLAY', 'RENDERER_MANIFEST', 'JOBS', 'BUILD_TYPE', 'STORAGE', 'REPORT', 'RUN_SECONDS', 'SERIAL', 'ADB', 'ANDROID_SDK',
             'APK', 'PAGE_SIZE', 'ANDROID_BACKEND', 'ANDROID_DEBUGGABLE', 'ANDROID_RENDER_DRIVER', 'SMOKE_SECONDS')
    env = {k: v for k, v in os.environ.items() if k not in names}
    env.update(extra)
    return mock.patch.dict(os.environ, env, clear=True)


class AndroidCommandTests(unittest.TestCase):
    def test_build_android_forwards_every_option(self):
        with clean_env():
            line = commands(MAC, 'build-android', '--sdk', '/sdk', '--rom', '/g.z64', '--bundle-rom',
                            '--render-driver', 'opengles2', '--debuggable')[0]
        self.assertEqual(Path(line[1]).name, 'build.py')
        for pair in (['--sdk', '/sdk'], ['--bundle-rom', '/g.z64'], ['--render-driver', 'opengles2']):
            self.assertIn(pair[0], line)
            self.assertEqual(line[line.index(pair[0]) + 1], pair[1])
        self.assertIn('--debuggable', line)

    def test_defaults_ask_for_nothing_extra(self):
        with clean_env():
            line = commands(WINDOWS, 'build-android')[0]
        self.assertEqual(line[2:], ['--jobs', '8', '--build-type', 'Release'])

    def test_launcher_variant_selects_its_apk_package_and_probes(self):
        with clean_env():
            build = commands(LINUX, 'build-android-launcher', '--sdk', '/sdk')[0]
            check = commands(LINUX, 'check-apk', '--launcher')[0]
            run = commands(LINUX, 'run-android', '--serial', 'S1', '--launcher')[0]
            smoke = commands(LINUX, 'smoke-android', '--serial', 'S1', '--launcher', '--report', '/r/new')[0]
            audio = commands(LINUX, 'audio-android', '--serial', 'S1', '--launcher', '--report', '/r/new')[0]
            personal = commands(LINUX, 'run-android', '--serial', 'S1')[0]
        self.assertIn('--launcher', build)
        self.assertTrue(check[2].endswith('StarCraft64-android-launcher.apk'))
        self.assertEqual(run[-1], 'com.nomadic.sc64.launcher/com.nomadic.sc64.Sc64Activity')
        self.assertEqual(personal[-1], driver.ACTIVITY)
        for line in (smoke, audio):
            self.assertEqual(line[line.index('--package') + 1], 'com.nomadic.sc64.launcher')
        self.assertTrue(smoke[smoke.index('--apk') + 1].endswith('StarCraft64-android-launcher.apk'))
        self.assertIn('--launcher', check)
        self.assertNotIn('--launcher', commands(LINUX, 'check-apk')[0])
        with clean_env(), self.assertRaisesRegex(SystemExit, 'never contains a ROM'):
            commands(LINUX, 'build-android-launcher', '--bundle-rom', '--rom', '/g.z64')

    def test_android_launcher_variable_does_not_change_what_build_android_builds(self):
        # $ANDROID_LAUNCHER selects the APK of install/run/smoke/check-apk; only build-android-launcher builds the launcher.
        with clean_env(ANDROID_LAUNCHER='1'):
            default = commands(LINUX, 'build-android')[0]
            emulator = commands(LINUX, 'build-android-opengles')[0]
            launcher = commands(LINUX, 'build-android-launcher')[0]
            check = commands(LINUX, 'check-apk')[0]
        self.assertNotIn('--launcher', default)
        self.assertNotIn('--launcher', emulator)
        self.assertIn('--launcher', launcher)
        self.assertTrue(check[2].endswith('StarCraft64-android-launcher.apk'))

    def test_push_module_stages_it_in_the_apps_own_folder(self):
        calls = []

        def fake_run(command, **kwargs):
            calls.append(command)
            return mock.Mock(returncode=0, stdout='present\n', stderr='')
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver.subprocess, 'run', side_effect=fake_run):
            args = driver.parser().parse_args(['push-android-module', '--serial', 'S1', '--data-dir', directory])
            with self.assertRaisesRegex(SystemExit, 'setup-game MODULE_TARGET=android-arm64'):
                driver.push_android_module(LINUX, args)
            module = Path(directory) / 'game-android-arm64/sc64-game.so'
            module.parent.mkdir()
            module.write_bytes(b'module')
            self.assertEqual(driver.push_android_module(LINUX, args), 0)
        folder = '/sdcard/Android/data/com.nomadic.sc64.launcher/files'
        self.assertEqual(calls[0][:4], ['adb', '-s', 'S1', 'shell'])
        self.assertIn(f'test -d {folder}', calls[0][4])
        # The file goes into the folder the app created, not into a subfolder made by adb (unreadable by the app).
        self.assertEqual([str(word) for word in calls[1]], ['adb', '-s', 'S1', 'push', str(module), folder + '/sc64-game.so'])

    def test_push_module_tells_a_missing_folder_from_an_adb_failure(self):
        for answer, message in ((mock.Mock(returncode=0, stdout='absent\n', stderr=''), 'start it once'),
                                (mock.Mock(returncode=1, stdout='', stderr='error: device offline'), 'device offline'),
                                (mock.Mock(returncode=0, stdout='', stderr=''), 'could not reach')):
            with self.subTest(message=message), tempfile.TemporaryDirectory() as directory, clean_env(), \
                    mock.patch.object(driver.subprocess, 'run', return_value=answer):
                module = Path(directory) / 'game-android-arm64/sc64-game.so'
                module.parent.mkdir()
                module.write_bytes(b'module')
                args = driver.parser().parse_args(['push-android-module', '--serial', 'S1', '--data-dir', directory])
                with self.assertRaisesRegex(SystemExit, message):
                    driver.push_android_module(LINUX, args)

    def test_opengles_target_selects_the_emulator_driver_and_apk(self):
        with clean_env():
            self.assertIn('opengles2', commands(LINUX, 'build-android-opengles')[0])
            check = commands(LINUX, 'check-apk', '--render-driver', 'opengles2')[0]
        self.assertTrue(check[2].endswith('StarCraft64-android-opengles2.apk'))

    def test_device_commands_need_an_explicit_serial(self):
        with clean_env():
            for name in ('run-android', 'smoke-android', 'audio-android', 'install-android'):
                with self.subTest(command=name), self.assertRaisesRegex(SystemExit, 'explicit adb device ID'):
                    args = driver.parser().parse_args([name])
                    driver.install_android(MAC, args) if name == 'install-android' else args.handler(MAC, args)

    def test_run_android_passes_the_renderer_as_activity_argument(self):
        with clean_env():
            plain = commands(MAC, 'run-android', '--serial', 'S1')[0]
            driver_line = commands(MAC, 'run-android', '--serial', 'S1', '--render-driver', 'opengles2')[0]
        self.assertEqual(plain, ['adb', '-s', 'S1', 'shell', 'am', 'start', '-n', driver.ACTIVITY])
        self.assertEqual(driver_line[-3:], ['--esa', 'com.nomadic.sc64.ARGUMENTS', '--config=sc64.render_driver=opengles2'])

    def test_smoke_forwards_probe_options_and_the_adb_executable(self):
        with clean_env(ADB='/opt/adb', PAGE_SIZE='16384', ANDROID_BACKEND='vulkan', SMOKE_SECONDS='12', BUNDLE_ROM='1'):
            line = commands(LINUX, 'smoke-android', '--serial', 'S1', '--report', '/r/new')[0]
        self.assertEqual(Path(line[1]).name, 'run.py')
        for pair in (['--required-page-size', '16384'], ['--backend', 'vulkan'], ['--adb', '/opt/adb'],
                     ['--seconds', '12'], ['--serial', 'S1'], ['--output', '/r/new']):
            self.assertEqual(line[line.index(pair[0]) + 1], pair[1], pair[0])
        self.assertIn('--bundled-rom', line)

    def test_emulator_install_requires_the_opengles_apk(self):
        with tempfile.TemporaryDirectory() as directory, clean_env():
            apk = Path(directory) / 'a.apk'
            for config, ok in ((b'sc64.render_driver = vulkan,opengles2\n', False), (b'sc64.render_driver = opengles2\n', True)):
                with ZipFile(apk, 'w') as package:
                    package.writestr('assets/sc64/config.ini', config)
                args = driver.parser().parse_args(['install-android', '--serial', 'emulator-5554', '--apk', str(apk)])
                calls = []
                with mock.patch.object(driver, 'execute', side_effect=lambda lines: calls.append(lines) or 0):
                    if ok:
                        self.assertEqual(driver.install_android(MAC, args), 0)
                        self.assertEqual(calls[-1][0][:5], ['adb', '-s', 'emulator-5554', 'install', '-r'])
                    else:
                        with self.assertRaisesRegex(SystemExit, 'crashes on emulators'):
                            driver.install_android(MAC, args)

    def test_physical_device_install_skips_the_emulator_guard(self):
        with clean_env():
            args = driver.parser().parse_args(['install-android', '--serial', 'R5CT1234', '--apk', '/missing.apk'])
            calls = []
            with mock.patch.object(driver, 'execute', side_effect=lambda lines: calls.append(lines) or 0):
                self.assertEqual(driver.install_android(MAC, args), 0)
        self.assertEqual(Path(calls[0][0][1]).name, 'check_apk.py')
        self.assertEqual(calls[1][0][:5], ['adb', '-s', 'R5CT1234', 'install', '-r'])


class RecompileCommandTests(unittest.TestCase):
    def test_recompile_uses_the_project_venv_python_per_host(self):
        with tempfile.TemporaryDirectory() as directory, clean_env():
            root = Path(directory)
            for host, relative in ((LINUX, '.venv/bin/python'), (WINDOWS, '.venv/Scripts/python.exe')):
                python = root / relative
                python.parent.mkdir(parents=True, exist_ok=True)
                python.write_text('')
                with self.subTest(host=host.label), mock.patch.object(driver, 'ROOT', root):
                    line = commands(host, 'recompile', '--rom', '/g.z64', '--jobs', '3')[0]
                    self.assertEqual(line[0], str(python))
                    self.assertEqual(Path(line[1]).name, 'recompile_all.py')
                    self.assertEqual(line[2:], ['/g.z64', '--jobs', '3'])

    def test_recompile_explains_how_to_create_the_environment(self):
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver, 'ROOT', Path(directory)):
            with self.assertRaisesRegex(SystemExit, 'sc64.py venv'):
                commands(MAC, 'recompile', '--rom', '/g.z64')
            with self.assertRaisesRegex(SystemExit, 'requires --rom'):
                commands(MAC, 'recompile', '--recomp-python', sys.executable)

    def test_explicit_python_overrides_the_venv(self):
        with clean_env():
            line = commands(MAC, 'recompile', '--rom', '/g.z64', '--recomp-python', sys.executable)[0]
        self.assertEqual(line[0], sys.executable)

    def test_venv_is_created_once_then_packages_are_installed(self):
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver, 'ROOT', Path(directory)):
            first = commands(LINUX, 'venv')
            self.assertEqual(first[0][1:3], ['-m', 'venv'])
            self.assertEqual(first[1][1:4], ['-m', 'pip', 'install'])
            self.assertEqual(first[1][4:], list(driver.VENV_PACKAGES))
            python = Path(directory) / '.venv/bin/python'
            python.parent.mkdir(parents=True)
            python.write_text('')
            self.assertEqual(len(commands(LINUX, 'venv')), 1)

    def test_pins_match_the_documented_environment(self):
        self.assertEqual(driver.VENV_PACKAGES, ('spimdisasm==1.42.4', 'rabbitizer==1.16.2', 'pyelftools==0.33'))


class RecipeCommandTests(unittest.TestCase):
    def test_verify_checks_the_recipe_then_regenerates_with_plain_python(self):
        with clean_env():
            check, generate = commands(LINUX, 'recipe-verify', '--rom', '/g.z64')
        self.assertEqual([Path(check[1]).name, check[2]], ['recipe.py', 'check'])
        self.assertEqual([Path(generate[1]).name, *generate[2:]], ['recipe.py', 'generate', '/g.z64'])
        self.assertEqual(check[0], sys.executable)

    def test_verify_requires_a_rom(self):
        with clean_env(), self.assertRaisesRegex(SystemExit, 'requires --rom'):
            commands(LINUX, 'recipe-verify')

    def test_export_needs_an_analysis_directory(self):
        with clean_env(), self.assertRaisesRegex(SystemExit, '--analysis'):
            commands(MAC, 'recipe-export')
        with clean_env():
            export, check = commands(MAC, 'recipe-export', '--analysis', 'local/recompile/symbols-1')
        self.assertEqual(export[2:], ['export', '--analysis', 'local/recompile/symbols-1'])
        self.assertEqual(check[2], 'check')


class PackageCommandTests(unittest.TestCase):
    def test_each_command_packages_its_target_with_the_shared_options(self):
        for command, host, target in (('package-macos', MAC, 'macos'), ('package-linux', LINUX, 'linux'),
                                      ('package-windows', LINUX, 'windows')):
            with self.subTest(command=command), clean_env():
                line = commands(host, command, '--jobs', '3', '--allow-dirty', '--docker')[0]
                self.assertEqual(Path(line[1]).name, 'package.py')
                self.assertEqual(line[2], target)
                self.assertEqual(line[line.index('--jobs') + 1], '3')
                self.assertIn('--allow-dirty', line)
                self.assertIn('--docker', line)

    def test_package_means_this_hosts_own_os(self):
        with clean_env():
            for host, target in ((MAC, 'macos'), (LINUX, 'linux'), (WINDOWS, 'windows')):
                self.assertEqual(commands(host, 'package')[0][2], target)
                self.assertNotIn('--allow-dirty', commands(host, 'package')[0])


class SetupGameTests(unittest.TestCase):
    def test_builds_the_tool_then_runs_it_with_the_host_recompiler(self):
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver, 'ROOT', Path(directory)):
            recompiler = Path(directory) / 'local/dist/recompiler/linux-x86_64/N64Recomp'
            recompiler.parent.mkdir(parents=True)
            recompiler.write_text('')
            lines = commands(LINUX, 'setup-game', '--rom', '/g.z64', '--data-dir', '/d', '--use-zig')
        self.assertEqual([Path(name).name for name in names(lines)], ['build.py', 'cmake', 'sc64_setup'])
        self.assertIn('--setup-only', lines[0])  # no generated game code is needed to build the tool
        self.assertIn('local/build/setup', lines[1][lines[1].index('-B') + 1] if '-B' in lines[1] else ' '.join(lines[1]))
        self.assertIn('sc64_setup', lines[1])
        run = lines[2]
        self.assertEqual(run[run.index('--rom') + 1], '/g.z64')
        self.assertEqual(run[run.index('--data') + 1], '/d')
        self.assertEqual(Path(run[run.index('--recompiler') + 1]), recompiler)
        self.assertIn('--use-zig', run)
        self.assertNotIn('--force', run)

    def test_android_module_target_passes_the_pinned_ndk(self):
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver, 'ROOT', Path(directory)):
            recompiler = Path(directory) / 'local/dist/recompiler/linux-x86_64/N64Recomp'
            recompiler.parent.mkdir(parents=True)
            recompiler.write_text('')
            sdk = Path(directory) / 'sdk'
            (sdk / 'ndk' / driver.pins.SDK_REVISION).mkdir(parents=True)
            lines = commands(LINUX, 'setup-game', '--rom', '/g.z64', '--module-target', 'android-arm64', '--sdk', str(sdk))
            run = lines[2]
            self.assertEqual(run[run.index('--target') + 1], 'android-arm64')
            self.assertEqual(Path(run[run.index('--ndk') + 1]), sdk / 'ndk' / driver.pins.SDK_REVISION)
            explicit = commands(LINUX, 'setup-game', '--rom', '/g.z64', '--module-target', 'android-arm64', '--ndk', '/n')[2]
            self.assertEqual(explicit[explicit.index('--ndk') + 1], '/n')
            # Without a pinned NDK sc64_setup looks for one itself ($ANDROID_NDK_ROOT).
            bare = commands(LINUX, 'setup-game', '--rom', '/g.z64', '--module-target', 'android-arm64', '--sdk', directory)[2]
            self.assertIn('--target', bare)
            self.assertNotIn('--ndk', bare)
            host = commands(LINUX, 'setup-game', '--rom', '/g.z64')[2]
            self.assertNotIn('--target', host)

    def test_a_missing_recompiler_names_the_command_that_builds_it(self):
        with tempfile.TemporaryDirectory() as directory, clean_env(), mock.patch.object(driver, 'ROOT', Path(directory)):
            with self.assertRaisesRegex(SystemExit, 'build-recompiler'):
                commands(MAC, 'setup-game', '--rom', '/g.z64')

    def test_requires_a_rom(self):
        with clean_env(), self.assertRaisesRegex(SystemExit, 'requires --rom'):
            commands(MAC, 'setup-game')


class InventoryCommandTests(unittest.TestCase):
    def test_inventory_runs_the_generator_with_the_venv_python(self):
        with clean_env(), tempfile.TemporaryDirectory() as directory, mock.patch.object(driver, 'ROOT', Path(directory)):
            python = Path(directory) / '.venv/bin/python'
            python.parent.mkdir(parents=True)
            python.write_text('')
            line = commands(LINUX, 'inventory', '--rom', '/g.z64')[0]
            self.assertEqual(line[0], str(python))
            self.assertEqual(Path(line[1]).name, 'ensure_inventory.py')
            self.assertEqual(line[2:], ['/g.z64'])
            self.assertEqual(commands(LINUX, 'inventory', '--rom', '/g.z64', '--dump', '/d.bin')[0][-2:], ['--dump', '/d.bin'])

    def test_inventory_needs_the_dump_of_the_game(self):
        with clean_env(), self.assertRaisesRegex(SystemExit, 'requires --rom'):
            commands(LINUX, 'inventory')


class BenchmarkCommandTests(unittest.TestCase):
    def test_baseline_is_required(self):
        with clean_env(), self.assertRaisesRegex(SystemExit, 'requires --baseline-replay'):
            commands(MAC, 'benchmark-renderer')

    def test_configure_build_then_compare(self):
        with clean_env():
            lines = commands(WINDOWS, 'benchmark-renderer', '--baseline-replay', '/base', '--report', '/r/new')
        self.assertEqual([line[0] for line in lines[:2]], ['cmake', 'cmake'])
        self.assertIn('-DSC64_BUILD_HOST=OFF', lines[0])
        compare = lines[2]
        self.assertEqual(Path(compare[1]).name, 'benchmark.py')
        self.assertEqual(compare[compare.index('--baseline') + 1], '/base')
        self.assertTrue(compare[compare.index('--candidate') + 1].endswith('sc64_gfx_replay.exe'))
        self.assertEqual(compare[3], '/r/new')


class ExecuteTests(unittest.TestCase):
    def test_every_word_reaches_subprocess_as_text_and_failure_stops_the_chain(self):
        calls = []
        def fake(command, cwd):
            calls.append(command)
            return mock.Mock(returncode=7 if len(calls) == 2 else 0)
        with mock.patch.object(driver.subprocess, 'run', side_effect=fake):
            status = driver.execute([['a', 1, Path('/p')], ['b', 2], ['never']])
        self.assertEqual(status, 7)
        self.assertEqual(calls, [['a', '1', '/p'], ['b', '2']])

    def test_every_real_command_is_text_only(self):
        with clean_env(SERIAL='S1', ROM='/g.z64', BASELINE_REPLAY='/b'), tempfile.TemporaryDirectory() as directory, \
                mock.patch.object(driver, 'ROOT', Path(directory)):
            python = Path(directory) / '.venv/bin/python'
            python.parent.mkdir(parents=True)
            python.write_text('')
            for name in ('build', 'build-linux', 'build-windows', 'run', 'test-native', 'recompile', 'venv', 'benchmark-renderer', 'build-android', 'configure-android', 'check-apk', 'run-android',
                         'smoke-android', 'audio-android'):
                lines = commands(LINUX, name)
                for line in lines:
                    for word in line:
                        self.assertIsInstance(word, (str, Path, int), (name, word))


class DoctorTests(unittest.TestCase):
    def test_doctor_fails_when_the_host_build_tools_are_missing(self):
        with mock.patch.object(driver.hostenv, 'find_tool', return_value=None), \
                mock.patch.object(driver.hostenv, 'llvm_bin', return_value=None), \
                mock.patch('builtins.print'):
            self.assertEqual(driver.doctor(MAC, None), 1)


if __name__ == '__main__':
    unittest.main()
