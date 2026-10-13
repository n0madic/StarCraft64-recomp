import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('sc64_recompile_all', ROOT / 'tools/recompile/recompile_all.py')
pipeline = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = pipeline
spec.loader.exec_module(pipeline)


class InputCheckTests(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(self.directory))
        self.rom = self.directory / 'rom.z64'
        self.inventory = self.directory / 'inventory.json'

    def test_missing_rom_is_rejected(self):
        with self.assertRaisesRegex(SystemExit, 'ROM not found'):
            pipeline.check_inputs(self.rom, self.inventory)

    def test_wrong_rom_is_rejected_before_anything_else(self):
        self.rom.write_bytes(b'not the game')
        with self.assertRaisesRegex(SystemExit, 'Unsupported ROM'):
            pipeline.check_inputs(self.rom, self.inventory)

    def test_missing_and_foreign_inventory_are_rejected(self):
        self.rom.write_bytes(b'synthetic')
        original = pipeline.ROM_SHA1
        pipeline.ROM_SHA1 = pipeline.sha1_file(self.rom)
        self.addCleanup(setattr, pipeline, 'ROM_SHA1', original)
        with self.assertRaisesRegex(SystemExit, 'Libultra inventory is missing'):
            pipeline.check_inputs(self.rom, self.inventory)
        self.inventory.write_text(json.dumps({'rom_sha1': '0' * 40}))
        with self.assertRaisesRegex(SystemExit, 'different ROM'):
            pipeline.check_inputs(self.rom, self.inventory)

    def test_output_outside_local_is_rejected(self):
        sys.argv = ['recompile_all.py', str(self.rom), '--output', str(self.directory / 'game')]
        with self.assertRaisesRegex(SystemExit, 'ignored local/'):
            pipeline.main()

class InventoryStepTests(unittest.TestCase):
    """`recompile` generates the default inventory when it is missing, and only then."""
    def run_main(self, inventory_arg=None, existing=False):
        directory = Path(tempfile.mkdtemp())
        self.addCleanup(lambda: __import__('shutil').rmtree(directory))
        default = directory / 'inventory.json'
        if existing:
            default.write_text('{}')
        steps = []
        argv = ['recompile_all', str(directory / 'rom.z64')] + (['--inventory', str(inventory_arg)] if inventory_arg else [])
        with mock.patch.object(pipeline, 'DEFAULT_INVENTORY', default), \
                mock.patch.object(pipeline, 'run', side_effect=lambda step, command: steps.append((step, command))), \
                mock.patch.object(sys, 'argv', argv):
            with self.assertRaises(SystemExit):  # check_inputs: the synthetic ROM does not exist
                pipeline.main()
        return steps

    def test_missing_default_inventory_is_generated_first(self):
        steps = self.run_main()
        self.assertEqual([step for step, _ in steps], ['generate libultra inventory'])
        self.assertEqual(Path(steps[0][1][1]).name, 'ensure_inventory.py')

    def test_existing_default_inventory_is_not_regenerated(self):
        self.assertEqual(self.run_main(existing=True), [])

    def test_explicit_inventory_must_already_exist(self):
        self.assertEqual(self.run_main(inventory_arg=Path('/nonexistent/inventory.json')), [])


if __name__ == '__main__':
    unittest.main()
