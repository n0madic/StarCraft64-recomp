import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/research_libultra'))
import accepted
import hostenv
spec = importlib.util.spec_from_file_location('sc64_ensure_inventory', ROOT / 'tools/research_libultra/ensure_inventory.py')
ensure = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = ensure
spec.loader.exec_module(ensure)
Host = hostenv.Host
ROM_SHA1 = ensure.NTSC_U_SHA1


def inventory(**changes):
    """A synthetic inventory that satisfies the accepted resident profile."""
    value = {
        'rom_sha1': ROM_SHA1,
        'mapping': {'scan_start': accepted.RESIDENT_SCAN_START, 'scan_end': accepted.RESIDENT_SCAN_END,
                    'displacement': accepted.RESIDENT_DISPLACEMENT},
        'reference_matches': [{'ram_bytes_equal': True}] * accepted.ACCEPTED_REFERENCE_MATCHES,
        'masked_crc_hits': [{'ram_bytes_equal': True}] * accepted.ACCEPTED_CRC_HITS,
        'inventory': [{'name': f'os{i}', 'reimplemented': True, 'crc_bodies': [1], 'reference_bytes': []}
                      for i in range(accepted.ACCEPTED_REPLACEMENT_NAMES)]
                     + [{'name': 'other', 'reimplemented': False, 'crc_bodies': [1], 'reference_bytes': []},
                        {'name': 'osNoBody', 'reimplemented': True, 'crc_bodies': [], 'reference_bytes': []}],
    }
    value.update(changes)
    return value


def elf_header(machine: int, data: int = 1) -> bytes:
    header = bytearray(64)
    header[:4] = b'\x7fELF'
    header[5] = data
    struct.pack_into('<H', header, 18, machine)
    return bytes(header)


class AcceptedProfileTests(unittest.TestCase):
    def test_accepted_inventory_passes(self):
        accepted.validate_resident(inventory(), ROM_SHA1)

    def test_each_deviation_is_rejected(self):
        broken = {
            'other ROM': inventory(rom_sha1='0' * 40),
            'overlay scan': inventory(mapping={'scan_start': 0x1000, 'scan_end': 0xE38E0,
                                               'displacement': accepted.RESIDENT_DISPLACEMENT}),
            'reference count': inventory(reference_matches=[{'ram_bytes_equal': True}] * 345),
            'crc count': inventory(masked_crc_hits=[{'ram_bytes_equal': True}] * 120),
            'no RAM proof': inventory(reference_matches=[{'ram_bytes_equal': False}] * accepted.ACCEPTED_REFERENCE_MATCHES),
            'name count': inventory(inventory=inventory()['inventory'][:-3]),
        }
        for label, value in broken.items():
            with self.subTest(label), self.assertRaises(ValueError):
                accepted.validate_resident(value, ROM_SHA1)

    def test_check_acceptance_uses_the_same_profile(self):
        text = (ROOT / 'tools/research/check_acceptance.py').read_text()
        for literal in ('== 346', '== 121', '== 51', "'scan_end'] == 0xD6640"):
            self.assertNotIn(literal, text)


class N64symTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))
        self.binary = self.directory / 'n64sym'

    def command(self, host, machine):
        self.binary.write_bytes(elf_header(machine))
        with mock.patch.object(ensure, 'N64SYM_BINARY', self.binary), \
                mock.patch.object(ensure.dockerenv, 'ROOT', self.directory), \
                mock.patch.object(ensure, 'base_image', return_value='debian@sha256:x'):
            return ensure.n64sym_command(host, Path('/roms/g.z64'), self.directory / 'out.txt')

    def test_elf_architecture_detection(self):
        for machine, expected in ((183, ('arm64', 'linux/arm64')), (62, ('x86_64', 'linux/amd64'))):
            self.binary.write_bytes(elf_header(machine))
            self.assertEqual(ensure.elf_arch(self.binary), expected)
        for data in (b'MZ' + bytes(60), elf_header(8), elf_header(62, data=2)):
            self.binary.write_bytes(data)
            with self.assertRaises(SystemExit):
                ensure.elf_arch(self.binary)

    def test_matching_linux_host_runs_the_binary_directly(self):
        command = self.command(Host('linux', 'arm64'), 183)
        self.assertEqual(command, [str(self.binary), '/roms/g.z64', '-s', '-f', 'splat', '-o', str(self.directory / 'out.txt')])

    def test_other_hosts_run_it_in_docker_on_its_own_platform(self):
        for host in (Host('darwin', 'arm64'), Host('linux', 'x86_64'), Host('windows', 'x86_64')):
            with self.subTest(host=host.label):
                command = self.command(host, 183)
                self.assertEqual(command[:2], ['docker', 'run'])
                self.assertEqual(command[command.index('--platform') + 1], 'linux/arm64')
                self.assertIn('/roms/g.z64:/input/rom.z64:ro', command)
                self.assertIn('/input/rom.z64', command)
                self.assertIn('debian@sha256:x', command)

    def test_base_image_comes_from_the_pinned_dockerfile(self):
        self.assertRegex(ensure.base_image(), r'^debian@sha256:[0-9a-f]{64}$')


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))
        self.captures = self.directory / 'captures/research'
        self.runner = self.directory / 'ares-test'
        patches = [mock.patch.object(ensure, 'CAPTURES', self.captures), mock.patch.object(ensure, 'RUNNER', self.runner),
                   mock.patch.object(ensure, 'DUMP_BYTES', 16)]
        for patch in patches:
            patch.start()
            self.addCleanup(patch.stop)

    def test_existing_dump_is_reused(self):
        dump = self.captures / 'reference-cold/end.rdram.bin'
        dump.parent.mkdir(parents=True)
        dump.write_bytes(bytes(16))
        with mock.patch.object(ensure.subprocess, 'run') as run:
            self.assertEqual(ensure.ensure_dump(Path('rom')), dump)
            run.assert_not_called()

    def test_wrong_size_or_partial_capture_is_never_overwritten(self):
        directory = self.captures / 'reference-cold'
        directory.mkdir(parents=True)
        with self.assertRaisesRegex(SystemExit, 'exists without end.rdram.bin'):
            ensure.ensure_dump(Path('rom'))
        (directory / 'end.rdram.bin').write_bytes(b'short')
        with self.assertRaisesRegex(SystemExit, 'not an 8 MiB'):
            ensure.ensure_dump(Path('rom'))

    def test_missing_emulator_is_a_prepared_input(self):
        with self.assertRaisesRegex(SystemExit, 'research emulator is not built.*reference-capture.md'):
            ensure.ensure_dump(Path('rom'))

    def fake_capture(self, size, code=0):
        def run(command, **kwargs):
            root = Path(command[command.index('--captures') + 1]) / 'reference-cold'
            root.mkdir(parents=True)
            (root / 'end.rdram.bin').write_bytes(bytes(size))
            return mock.Mock(returncode=code)
        return run

    def test_successful_capture_is_moved_into_place(self):
        self.runner.write_text('')
        with mock.patch.object(ensure.subprocess, 'run', side_effect=self.fake_capture(16)):
            dump = ensure.ensure_dump(Path('rom'))
        self.assertEqual(dump, self.captures / 'reference-cold/end.rdram.bin')
        self.assertTrue(dump.is_file())
        self.assertEqual([p.name for p in self.captures.parent.iterdir()], ['research'])

    def test_failed_capture_keeps_diagnostics_and_leaves_no_target(self):
        self.runner.write_text('')
        for size, code in ((16, 3), (4, 0)):
            with self.subTest(size=size, code=code), mock.patch.object(ensure.subprocess, 'run', side_effect=self.fake_capture(size, code)):
                with self.assertRaisesRegex(SystemExit, 'diagnostics kept'):
                    ensure.ensure_dump(Path('rom'))
                self.assertFalse((self.captures / 'reference-cold').exists())
        self.assertTrue(any(p.name.startswith('.research-failed-') for p in self.captures.parent.iterdir()))


class PublishTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))
        self.output = self.directory / 'reports/libultra-resident'
        self.output.parent.mkdir()
        for name, value in (('CANDIDATES', self.directory / 'c.txt'), ('REFERENCE_ROOT', self.directory / 'refs')):
            patch = mock.patch.object(ensure, name, value)
            patch.start()
            self.addCleanup(patch.stop)
        patch = mock.patch.object(ensure, 'ROOT', self.directory)
        patch.start()
        self.addCleanup(patch.stop)

    def run_inventory(self, value, code=0):
        def run(command, **kwargs):
            scratch = Path(command[command.index('--output') + 1])
            scratch.mkdir(parents=True)
            (scratch / 'inventory.json').write_text(json.dumps(value))
            return mock.Mock(returncode=code, stdout='', stderr='boom')
        return run

    def test_valid_inventory_is_published_and_incomplete_output_is_moved_aside(self):
        self.output.mkdir()
        (self.output / 'partial.txt').write_text('x')
        with mock.patch.object(ensure.subprocess, 'run', side_effect=self.run_inventory(inventory())):
            ensure.generate(Path('rom'), Path('dump'), self.output)
        self.assertTrue((self.output / 'inventory.json').is_file())
        aside = [p for p in self.output.parent.iterdir() if '.incomplete-' in p.name]
        self.assertEqual(len(aside), 1)
        self.assertTrue((aside[0] / 'partial.txt').is_file())

    def test_inventory_arguments_use_the_resident_profile_and_relative_paths(self):
        calls = []
        def run(command, **kwargs):
            calls.append(command)
            return self.run_inventory(inventory())(command)
        with mock.patch.object(ensure.subprocess, 'run', side_effect=run):
            ensure.generate(Path('rom'), Path('dump'), self.output)
        command = calls[0]
        def value(flag):
            return command[command.index(flag) + 1]
        self.assertEqual((value('--scan-start'), value('--scan-end'), value('--displacement')), ('0x1000', '0xd6640', '0x7ffff400'))
        self.assertEqual(value('--candidate-displacement'), '0x7ffff400')
        self.assertEqual(value('--reference-root'), 'refs')
        self.assertEqual(value('--candidates'), 'c.txt')

    def test_deviating_inventory_is_rejected_and_not_published(self):
        bad = inventory(reference_matches=[])
        with mock.patch.object(ensure.subprocess, 'run', side_effect=self.run_inventory(bad)):
            with self.assertRaisesRegex(SystemExit, 'differs from the accepted Stage 0 profile'):
                ensure.generate(Path('rom'), Path('dump'), self.output)
        self.assertFalse(self.output.exists())
        self.assertTrue(any('rejected' in p.name for p in self.output.parent.iterdir()))

    def test_generator_failure_is_reported(self):
        with mock.patch.object(ensure.subprocess, 'run', side_effect=self.run_inventory(inventory(), code=1)):
            with self.assertRaisesRegex(SystemExit, 'inventory.py failed'):
                ensure.generate(Path('rom'), Path('dump'), self.output)

    def test_missing_reference_versions_are_named(self):
        (self.directory / 'refs/J/libultra_rom/src').mkdir(parents=True)
        (self.directory / 'refs/J/libultra_rom/src/a.o').write_bytes(b'')
        with self.assertRaisesRegex(SystemExit, 'versions I, K, L'):
            ensure.check_references()


class MainTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))
        self.output = self.directory / 'local/reports/libultra-resident'
        patch = mock.patch.object(ensure, 'ROOT', self.directory)
        patch.start()
        self.addCleanup(patch.stop)
        self.rom = self.directory / 'game.z64'
        self.rom.write_bytes(b'rom')
        patch = mock.patch.object(ensure, 'sha1_file', return_value=ROM_SHA1)
        patch.start()
        self.addCleanup(patch.stop)

    def test_valid_existing_inventory_short_circuits_every_step(self):
        self.output.mkdir(parents=True)
        (self.output / 'inventory.json').write_text(json.dumps(inventory()))
        with mock.patch.object(ensure, 'check_python') as check, mock.patch.object(ensure, 'generate') as generate:
            self.assertEqual(ensure.main([str(self.rom), '--output', str(self.output)]), 0)
            check.assert_not_called()
            generate.assert_not_called()

    def test_foreign_existing_inventory_is_never_replaced(self):
        self.output.mkdir(parents=True)
        (self.output / 'inventory.json').write_text(json.dumps(inventory(reference_matches=[])))
        with self.assertRaisesRegex(SystemExit, 'move it aside'):
            ensure.main([str(self.rom), '--output', str(self.output)])

    def test_wrong_rom_stops_before_any_step(self):
        with mock.patch.object(ensure, 'sha1_file', return_value='0' * 40), mock.patch.object(ensure, 'check_python') as check:
            with self.assertRaisesRegex(SystemExit, 'Unsupported ROM'):
                ensure.main([str(self.rom), '--output', str(self.output)])
            check.assert_not_called()

    def test_output_must_stay_under_local(self):
        with self.assertRaises(SystemExit):
            ensure.main([str(self.rom), '--output', str(self.directory / 'elsewhere')])

    def test_full_chain_order_with_a_supplied_dump(self):
        dump = self.directory / 'dump.bin'
        dump.write_bytes(bytes(ensure.DUMP_BYTES))
        order = []
        with mock.patch.object(ensure, 'check_python', side_effect=lambda: order.append('python')), \
                mock.patch.object(ensure, 'check_references', side_effect=lambda: order.append('references')), \
                mock.patch.object(ensure, 'ensure_candidates', side_effect=lambda *a: order.append('candidates')), \
                mock.patch.object(ensure, 'ensure_dump') as capture, \
                mock.patch.object(ensure, 'generate', side_effect=lambda *a: order.append('generate')):
            ensure.main([str(self.rom), '--output', str(self.output), '--dump', str(dump)])
            capture.assert_not_called()
        self.assertEqual(order, ['python', 'references', 'candidates', 'generate'])


if __name__ == '__main__':
    unittest.main()
