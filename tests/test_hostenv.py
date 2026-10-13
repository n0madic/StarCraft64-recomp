import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import hostenv
from hostenv import Host


class HostTests(unittest.TestCase):
    def detect(self, system, machine):
        with mock.patch.object(hostenv.platform, 'system', return_value=system), \
                mock.patch.object(hostenv.platform, 'machine', return_value=machine):
            return hostenv.current()

    def test_supported_hosts_are_normalized(self):
        cases = {('Darwin', 'arm64'): Host('darwin', 'arm64'), ('Linux', 'x86_64'): Host('linux', 'x86_64'),
                 ('Linux', 'aarch64'): Host('linux', 'arm64'), ('Windows', 'AMD64'): Host('windows', 'x86_64'),
                 ('Windows', 'ARM64'): Host('windows', 'arm64')}
        for (system, machine), expected in cases.items():
            with self.subTest(system=system, machine=machine):
                self.assertEqual(self.detect(system, machine), expected)

    def test_unknown_hosts_are_rejected(self):
        for system, machine in (('FreeBSD', 'amd64'), ('Linux', 'riscv64'), ('MSYS_NT-10.0', 'x86_64')):
            with self.subTest(system=system, machine=machine):
                with self.assertRaisesRegex(SystemExit, 'Unsupported build host'):
                    self.detect(system, machine)

    def test_host_derived_values(self):
        windows, mac, linux = Host('windows', 'x86_64'), Host('darwin', 'arm64'), Host('linux', 'x86_64')
        self.assertEqual((windows.exe_suffix, windows.script_suffix), ('.exe', '.bat'))
        self.assertEqual((mac.exe_suffix, mac.script_suffix), ('', ''))
        self.assertEqual([h.ndk_tag for h in (mac, linux, windows)],
                         ['darwin-x86_64', 'linux-x86_64', 'windows-x86_64'])
        self.assertEqual(mac.docker_platform, 'linux/arm64')
        self.assertEqual(linux.docker_platform, 'linux/amd64')
        self.assertTrue(linux.linux_x86_64)
        self.assertFalse(Host('linux', 'arm64').linux_x86_64)
        self.assertEqual(linux.label, 'linux-x86_64')

    def test_venv_python_follows_the_host_layout(self):
        root = Path('/repo')
        self.assertEqual(hostenv.venv_python(root, Host('linux', 'x86_64')), root / '.venv/bin/python')
        self.assertEqual(hostenv.venv_python(root, Host('windows', 'x86_64')), root / '.venv/Scripts/python.exe')


class FindToolTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))

    def make(self, name):
        path = self.directory / name
        path.write_text('')
        return path

    def test_environment_override_wins_and_is_not_second_guessed(self):
        tool = self.make('custom')
        with mock.patch.dict(os.environ, {'SC64_TEST_TOOL': str(tool)}), \
                mock.patch.object(hostenv.shutil, 'which', return_value='/usr/bin/other'):
            self.assertEqual(hostenv.find_tool('other', env='SC64_TEST_TOOL'), tool)
        with mock.patch.dict(os.environ, {'SC64_TEST_TOOL': str(self.directory / 'missing')}):
            self.assertIsNone(hostenv.find_tool('other', env='SC64_TEST_TOOL'))

    def test_path_then_directories(self):
        self.make('tool.exe')
        with mock.patch.object(hostenv.shutil, 'which', return_value=None):
            self.assertEqual(hostenv.find_tool('tool', dirs=(self.directory,)), self.directory / 'tool.exe')
            self.assertIsNone(hostenv.find_tool('absent', dirs=(self.directory,)))
        with mock.patch.object(hostenv.shutil, 'which', side_effect=lambda n: '/bin/' + n):
            self.assertEqual(hostenv.find_tool('first', 'second'), Path('/bin/first'))

    def test_llvm_bin_override_requires_clang(self):
        (self.directory / 'clang++').write_text('')
        with mock.patch.dict(os.environ, {'LLVM_BIN': str(self.directory)}):
            self.assertEqual(hostenv.llvm_bin(), self.directory)
        with mock.patch.dict(os.environ, {'LLVM_BIN': str(self.directory / 'nope')}):
            self.assertIsNone(hostenv.llvm_bin())

    def test_llvm_bin_uses_homebrew_keg(self):
        keg = self.directory / 'llvm'
        (keg / 'bin').mkdir(parents=True)
        (keg / 'bin/clang++').write_text('')
        env = {k: v for k, v in os.environ.items() if k != 'LLVM_BIN'}
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(hostenv, 'BREW_OPT_DIRS', (self.directory,)):
            self.assertEqual(hostenv.llvm_bin(), keg / 'bin')


class AndroidToolTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))

    def test_sdk_environment_variables_take_precedence_in_order(self):
        first, second = self.directory / 'a', self.directory / 'b'
        first.mkdir(); second.mkdir()
        env = {'ANDROID_SDK': '', 'ANDROID_HOME': str(second), 'ANDROID_SDK_ROOT': str(first)}
        with mock.patch.dict(os.environ, env):
            self.assertEqual(hostenv.android_sdk(Host('linux', 'x86_64')), second)
        with mock.patch.dict(os.environ, {**env, 'ANDROID_SDK': str(first)}):
            self.assertEqual(hostenv.android_sdk(Host('linux', 'x86_64')), first)
        with mock.patch.dict(os.environ, {**env, 'ANDROID_SDK': str(self.directory / 'missing')}):
            self.assertIsNone(hostenv.android_sdk(Host('linux', 'x86_64')))

    def test_sdk_ide_default_per_host(self):
        for host, relative in ((Host('darwin', 'arm64'), 'Library/Android/sdk'), (Host('linux', 'x86_64'), 'Android/Sdk')):
            home = self.directory / host.system
            (home / relative).mkdir(parents=True)
            env = {k: v for k, v in os.environ.items() if k not in ('ANDROID_SDK', 'ANDROID_HOME', 'ANDROID_SDK_ROOT')}
            with self.subTest(host=host.label), mock.patch.dict(os.environ, env, clear=True), \
                    mock.patch.object(hostenv.Path, 'home', return_value=home):
                self.assertEqual(hostenv.android_sdk(host), home / relative)
        windows = self.directory / 'local'
        (windows / 'Android/Sdk').mkdir(parents=True)
        env = {k: v for k, v in os.environ.items() if k not in ('ANDROID_SDK', 'ANDROID_HOME', 'ANDROID_SDK_ROOT')}
        env['LOCALAPPDATA'] = str(windows)
        with mock.patch.dict(os.environ, env, clear=True):
            self.assertEqual(hostenv.android_sdk(Host('windows', 'x86_64')), windows / 'Android/Sdk')

    def make_jdk(self, name, version):
        home = self.directory / name
        (home / 'bin').mkdir(parents=True)
        (home / 'bin/javac').write_text('')
        return home, mock.Mock(stdout=f'javac {version}.0.1\n')

    def test_jdk_must_meet_the_minimum_version(self):
        old, old_output = self.make_jdk('jdk17', 17)
        new, new_output = self.make_jdk('jdk21', 21)
        host = Host('linux', 'x86_64')
        with mock.patch.dict(os.environ, {'JAVA_HOME': str(old)}), mock.patch.object(hostenv, 'brew_prefix', return_value=None), \
                mock.patch.object(hostenv, 'find_tool', return_value=None), \
                mock.patch.object(hostenv.subprocess, 'run', return_value=old_output):
            self.assertIsNone(hostenv.jdk_home(host))
        with mock.patch.dict(os.environ, {'JAVA_HOME': str(new)}), mock.patch.object(hostenv.subprocess, 'run', return_value=new_output):
            self.assertEqual(hostenv.jdk_home(host), new)

    def test_jdk_falls_back_to_javac_on_path(self):
        home, output = self.make_jdk('path-jdk', 21)
        env = {k: v for k, v in os.environ.items() if k != 'JAVA_HOME'}
        with mock.patch.dict(os.environ, env, clear=True), mock.patch.object(hostenv, 'brew_prefix', return_value=None), \
                mock.patch.object(hostenv, 'find_tool', return_value=home / 'bin/javac'), \
                mock.patch.object(hostenv.subprocess, 'run', return_value=output):
            self.assertEqual(hostenv.jdk_home(Host('linux', 'x86_64')), home.resolve())

    def test_android_requirements_name_each_missing_piece(self):
        with mock.patch.object(hostenv, 'android_sdk', return_value=None), mock.patch.object(hostenv, 'jdk_home', return_value=None):
            checks = hostenv.android_requirements(Host('linux', 'x86_64'), '1.2.3', '4.5.6')
        self.assertEqual(len(checks), 4)
        self.assertFalse(any(c.ok for c in checks))
        self.assertIn('1.2.3', checks[1].name)


class RequirementTests(unittest.TestCase):
    def test_checks_name_the_install_command_per_host(self):
        with mock.patch.object(hostenv, 'find_tool', return_value=None), mock.patch.object(hostenv, 'llvm_bin', return_value=None):
            for host, needle in ((Host('darwin', 'arm64'), 'brew install llvm'),
                                 (Host('windows', 'x86_64'), 'pacman -S mingw-w64-x86_64-gcc'),
                                 (Host('linux', 'x86_64'), 'apt install')):
                with self.subTest(host=host.label):
                    checks = hostenv.desktop_requirements(host)
                    self.assertTrue(checks and not any(c.ok for c in checks))
                    self.assertTrue(any(needle in c.hint for c in checks))

    def test_linux_package_needs_docker_unless_host_is_x86_64_linux(self):
        with mock.patch.object(hostenv, 'find_tool', return_value=Path('/usr/bin/x')):
            self.assertEqual([c.name for c in hostenv.linux_requirements(Host('darwin', 'arm64'))][0][:6], 'docker')
            self.assertNotIn('docker', ' '.join(c.name for c in hostenv.linux_requirements(Host('linux', 'x86_64'))))


if __name__ == '__main__':
    unittest.main()
