import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/recompile'))
import patch_recompiler as patcher

MAIN = 'int f() {\n' + '\n'.join(f'    {anchor}' for anchor, _ in patcher.BINARY_OUTPUTS) + '\n}\n'


def git(directory, *words):
    subprocess.run(['git', '-C', str(directory), '-c', 'user.name=t', '-c', 'user.email=t@example.com', *words],
                   check=True, capture_output=True)


class BinaryOutputPatchTests(unittest.TestCase):
    def setUp(self):
        self.source = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(__import__('shutil').rmtree, self.source)
        (self.source / 'src').mkdir()
        self.main = self.source / 'src/main.cpp'
        self.main.write_text(MAIN)
        git(self.source, 'init', '-q')
        git(self.source, 'add', '.')
        git(self.source, 'commit', '-q', '-m', 'pin')

    def apply(self):
        patcher.apply_source_changes(self.source, 'src/main.cpp', patcher.BINARY_OUTPUTS)

    def test_every_generated_output_opens_in_binary_mode(self):
        self.apply()
        text = self.main.read_text()
        self.assertEqual(text.count('std::ios::binary'), 4)
        for _, replacement in patcher.BINARY_OUTPUTS:
            self.assertIn(replacement, text)

    def test_patch_is_idempotent(self):
        self.apply()
        first = self.main.read_text()
        self.apply()
        self.assertEqual(self.main.read_text(), first)

    def test_unrelated_local_edits_are_refused(self):
        self.main.write_text(MAIN + '// local edit\n')
        with self.assertRaisesRegex(SystemExit, 'unrelated'):
            self.apply()
        self.assertTrue(self.main.read_text().endswith('// local edit\n'))

    def test_a_changed_anchor_fails_before_writing(self):
        self.main.write_text('int g() {}\n')
        git(self.source, 'commit', '-q', '-am', 'other source')
        with self.assertRaisesRegex(SystemExit, 'anchor changed'):
            self.apply()
        self.assertEqual(self.main.read_text(), 'int g() {}\n')


if __name__ == '__main__':
    unittest.main()
