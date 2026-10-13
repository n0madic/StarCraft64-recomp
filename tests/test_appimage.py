import hashlib
import importlib.util
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
spec = importlib.util.spec_from_file_location('sc64_appimage', ROOT / 'tools/desktop/appimage.py')
appimage = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = appimage
spec.loader.exec_module(appimage)


def elf(machine: int) -> bytes:
    header = bytearray(64)
    header[:4] = b'\x7fELF'
    struct.pack_into('<H', header, 18, machine)
    return bytes(header)


def make_package(root: Path, *, machine: int = 62, icon: bool = True) -> tuple[Path, Path]:
    """A synthetic stand-in for local/dist/StarCraft64-linux and its receipt."""
    package = root / 'StarCraft64-linux'
    package.mkdir()
    (package / 'StarCraft64').write_bytes(elf(machine))
    (package / 'DejaVuSansMono-Bold.ttf').write_bytes(b'font')
    (package / 'LICENSE-DejaVu.txt').write_text('license')
    if icon:
        (package / 'StarCraft64.png').write_bytes(b'\x89PNG')
    receipt = root / 'build.json'
    receipt.write_text(json.dumps({'files_sha256': {
        path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(package.iterdir())}}))
    return package, receipt


class PackageTests(unittest.TestCase):
    def test_package_must_match_its_receipt_and_be_x86_64(self):
        with tempfile.TemporaryDirectory() as directory:
            package, receipt = make_package(Path(directory))
            self.assertIn('StarCraft64', appimage.check_package(package, receipt))
            (package / 'DejaVuSansMono-Bold.ttf').write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError, 'differs from its build receipt'):
                appimage.check_package(package, receipt)
        with tempfile.TemporaryDirectory() as directory:
            package, receipt = make_package(Path(directory), machine=183)
            with self.assertRaisesRegex(ValueError, 'not an x86_64'):
                appimage.check_package(package, receipt)
        with self.assertRaisesRegex(ValueError, 'make build-linux'):
            appimage.check_package(Path('/nonexistent/package'), Path('/nonexistent/build.json'))

    def test_package_rejects_a_rom_in_the_staged_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            package, receipt = make_package(Path(directory))
            (package / 'game.z64').write_bytes(b'\x80\x37\x12\x40' + bytes(60))
            with self.assertRaisesRegex(ValueError, 'Forbidden private input'):
                appimage.check_package(package, receipt)


class AppDirTests(unittest.TestCase):
    def stage(self, root: Path, rom: bytes | None, **options) -> Path:
        package, _ = make_package(root, **options)
        appdir = root / 'AppDir'
        appimage.stage_appdir(appdir, package, rom)
        return appdir

    def test_layout_keeps_resources_beside_the_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            appdir = self.stage(Path(directory), None)
            for name in ('AppRun', 'StarCraft64.desktop', 'StarCraft64.png', 'usr/bin/StarCraft64',
                         'usr/bin/DejaVuSansMono-Bold.ttf', 'usr/bin/LICENSE-DejaVu.txt'):
                self.assertTrue((appdir / name).is_file(), name)
            self.assertEqual((appdir / '.DirIcon').readlink(), Path('StarCraft64.png'))
            self.assertFalse((appdir / 'usr/bin/StarCraft64.png').exists())
            self.assertFalse((appdir / 'usr/bin' / appimage.ROM_NAME).exists())
            self.assertTrue((appdir / 'AppRun').stat().st_mode & 0o111)
            self.assertIn('Icon=StarCraft64', (appdir / 'StarCraft64.desktop').read_text())

    def test_modes_are_readable_by_every_user(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            package, _ = make_package(root)
            (package / 'DejaVuSansMono-Bold.ttf').chmod(0o600)
            appdir = root / 'AppDir'
            appimage.stage_appdir(appdir, package, b'\x80\x37\x12\x40')
            modes = {path.relative_to(appdir).as_posix(): path.stat().st_mode & 0o777
                     for path in appdir.rglob('*') if not path.is_symlink()}
            self.assertEqual(modes['usr/bin/DejaVuSansMono-Bold.ttf'], 0o644)
            self.assertEqual(modes[f'usr/bin/{appimage.ROM_NAME}'], 0o644)
            self.assertEqual(modes['usr/bin/StarCraft64'], 0o755)
            self.assertEqual(modes['AppRun'], 0o755)
            self.assertEqual(modes['usr/bin'], 0o755)
            self.assertEqual(modes['StarCraft64.png'], 0o644)

    def test_missing_icon_is_omitted_from_the_desktop_entry(self):
        with tempfile.TemporaryDirectory() as directory:
            appdir = self.stage(Path(directory), None, icon=False)
            self.assertNotIn('Icon=', (appdir / 'StarCraft64.desktop').read_text())
            self.assertFalse((appdir / '.DirIcon').is_symlink())

    def test_launcher_autostarts_only_when_a_rom_is_packaged(self):
        self.assertIn('--config=sc64.bundled_rom=1', appimage.APP_RUN)
        self.assertIn(f'-f "$BIN/{appimage.ROM_NAME}"', appimage.APP_RUN)
        # The default path must forward user arguments without the autostart key.
        self.assertTrue(appimage.APP_RUN.rstrip().endswith('exec "$BIN/StarCraft64" "$@"'))

    def test_audit_accepts_only_the_requested_rom(self):
        rom = b'\x80\x37\x12\x40' + bytes(60)
        with tempfile.TemporaryDirectory() as directory:
            appdir = self.stage(Path(directory), rom)
            self.assertNotIn(f'usr/bin/{appimage.ROM_NAME}', appimage.audit_appdir(appdir, rom))
            with self.assertRaisesRegex(ValueError, 'Unexpected or altered ROM'):
                appimage.audit_appdir(appdir, None)
            with self.assertRaisesRegex(ValueError, 'Unexpected or altered ROM'):
                appimage.audit_appdir(appdir, rom + b'x')
        with tempfile.TemporaryDirectory() as directory:
            appdir = self.stage(Path(directory), None)
            with self.assertRaisesRegex(ValueError, 'lacks the requested ROM'):
                appimage.audit_appdir(appdir, rom)

    def test_audit_rejects_a_renamed_rom_and_a_save_file(self):
        with tempfile.TemporaryDirectory() as directory:
            appdir = self.stage(Path(directory), None)
            (appdir / 'usr/bin/data.bin').write_bytes(b'\x80\x37\x12\x40' + bytes(60))
            with self.assertRaisesRegex(ValueError, 'Forbidden private input'):
                appimage.audit_appdir(appdir, None)
            (appdir / 'usr/bin/data.bin').unlink()
            (appdir / 'usr/bin/game.sav').write_bytes(b'save')
            with self.assertRaisesRegex(ValueError, 'Forbidden private input'):
                appimage.audit_appdir(appdir, None)


class JoinTests(unittest.TestCase):
    def test_squashfs_follows_the_runtime_and_the_file_is_executable(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'runtime').write_bytes(b'\x7fELF' + bytes(100))
            (root / 'image.squashfs').write_bytes(b'hsqs' + bytes(50))
            offset = appimage.join_image(root / 'runtime', root / 'image.squashfs', root / 'out.AppImage')
            data = (root / 'out.AppImage').read_bytes()
            self.assertEqual(offset, 104)
            self.assertEqual(data[:104], (root / 'runtime').read_bytes())
            self.assertEqual(data[offset:offset + 4], b'hsqs')
            self.assertTrue((root / 'out.AppImage').stat().st_mode & 0o111)

    def test_a_file_system_not_at_the_boundary_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'runtime').write_bytes(b'\x7fELF' + bytes(100))
            (root / 'image.squashfs').write_bytes(b'junk' + bytes(50))
            with self.assertRaisesRegex(ValueError, 'runtime boundary'):
                appimage.join_image(root / 'runtime', root / 'image.squashfs', root / 'out.AppImage')


class RuntimePinTests(unittest.TestCase):
    def test_runtime_is_a_pinned_public_download(self):
        import bootstrap
        self.assertIn((bootstrap.APPIMAGE_RUNTIME, bootstrap.APPIMAGE_RUNTIME_URL, bootstrap.APPIMAGE_RUNTIME_SHA256),
                      bootstrap.FILES)
        self.assertIn(bootstrap.APPIMAGE_RUNTIME_TAG, bootstrap.APPIMAGE_RUNTIME_URL)
        self.assertEqual(len(bootstrap.APPIMAGE_RUNTIME_SHA256), 64)


if __name__ == '__main__':
    unittest.main()
