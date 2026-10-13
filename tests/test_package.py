import hashlib
import io
import json
import shutil
import stat
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import neutral_icon
import notices
import package


class Workspace(unittest.TestCase):
    def setUp(self):
        self.directory = Path(tempfile.mkdtemp()).resolve()
        self.addCleanup(__import__('shutil').rmtree, self.directory)
        self.binaries = {}
        for name in ('shell', 'setup', 'recompiler'):
            path = self.directory / f'bin-{name}'
            path.write_bytes(f'binary {name}'.encode())
            self.binaries[name] = path
        self.notice_files = {'NOTICE.txt': 'notice', 'SOURCE.txt': 'source', 'LICENSES/GPL-3.0.txt': 'gpl'}
        self.icons = self.directory / 'icons'
        self.icons.mkdir()
        for name in ('StarCraft64.icns', 'StarCraft64.png', 'StarCraft64.ico', 'StarCraft64.rc'):
            (self.icons / name).write_bytes(f'icon {name}'.encode())

    def staged(self, target):
        expected = package.layout(target, self.binaries, self.notice_files, self.icons)
        stage = self.directory / f'stage-{target}'
        package.stage_files(stage, expected)
        return stage, expected


class LayoutTests(Workspace):
    def test_every_target_has_the_same_pieces(self):
        for target, suffix in (('linux', ''), ('windows', '.exe'), ('macos', '')):
            with self.subTest(target=target):
                names = set(package.layout(target, self.binaries, self.notice_files, self.icons))
                resources = 'StarCraft64.app/Contents/Resources/' if target == 'macos' else ''
                for needed in ('NOTICE.txt', 'SOURCE.txt', 'LICENSES/GPL-3.0.txt', 'DejaVuSansMono-Bold.ttf',
                               'setup-resources/recipe/ntsc-u/generated.json', 'setup-resources/module/module_main.c',
                               'setup-resources/module/recomp.h'):
                    self.assertIn(resources + needed, names)
                self.assertIn('README.txt', names)
                self.assertFalse([name for name in names if 'sc64-game' in name or name.endswith(('.z64', '.v64', '.n64'))])
                if target == 'macos':
                    self.assertTrue({'StarCraft64.app/Contents/MacOS/SC64', 'StarCraft64.app/Contents/MacOS/sc64_setup',
                                     'StarCraft64.app/Contents/MacOS/N64Recomp', 'StarCraft64.app/Contents/Info.plist'} <= names)
                else:
                    self.assertTrue({f'StarCraft64{suffix}', f'sc64_setup{suffix}', f'N64Recomp{suffix}'} <= names)

    def test_each_platform_carries_the_neutral_icon_in_its_own_format(self):
        layouts = {target: package.layout(target, self.binaries, self.notice_files, self.icons) for target in package.TARGETS}
        mac = layouts['macos']['StarCraft64.app/Contents/Resources/StarCraft64.icns']
        self.assertEqual(mac, self.icons / 'StarCraft64.icns')
        self.assertEqual(layouts['linux']['StarCraft64.png'], self.icons / 'StarCraft64.png')
        self.assertFalse([name for name in layouts['windows'] if name.endswith(('.ico', '.png', '.rc'))])  # embedded in the exe
        self.assertIn(b'<key>CFBundleIconFile</key>', package.info_plist())

    def test_the_game_derived_icon_is_never_read(self):
        source = (ROOT / 'tools/desktop/package.py').read_text()
        self.assertNotIn('installed_icon', source)
        self.assertNotIn('local/icons', source)
        for sources in (package.layout(t, self.binaries, self.notice_files, self.icons) for t in package.TARGETS):
            for value in sources.values():
                if isinstance(value, Path):
                    self.assertNotIn('local/icons', value.as_posix())

    def test_setup_resources_match_what_the_setup_tool_reads(self):
        sources = package.setup_resource_sources()
        self.assertEqual(set(sources), set(package.SETUP_FILES))
        self.assertIn('module_main.c', ' '.join(package.SETUP_FILES))

    def test_readme_tells_each_platform_how_to_open_an_unsigned_app(self):
        self.assertIn('Control-click', package.readme('macos'))
        self.assertIn('SmartScreen', package.readme('windows'))
        for target in package.TARGETS:
            self.assertIn('a36d91b5aac7bb95df0d7de5f9814f5270e25c2b', package.readme(target))

    def test_names(self):
        self.assertEqual(package.package_name('macos'), 'StarCraft64-launcher-macos-arm64')
        self.assertEqual(package.package_name('windows'), 'StarCraft64-launcher-windows-x86_64')


class AuditTests(Workspace):
    def test_a_clean_stage_passes(self):
        for target in package.TARGETS:
            stage, expected = self.staged(target)
            package.audit(stage, expected)

    def test_an_unexpected_file_is_rejected(self):
        stage, expected = self.staged('linux')
        (stage / 'notes.txt').write_text('extra')
        with self.assertRaisesRegex(SystemExit, 'unexpected'):
            package.audit(stage, expected)

    def test_a_game_module_is_rejected_even_if_listed(self):
        stage, expected = self.staged('linux')
        (stage / 'sc64-game.so').write_bytes(b'module')
        expected['sc64-game.so'] = b'module'
        with self.assertRaisesRegex(SystemExit, 'Forbidden'):
            package.audit(stage, expected)

    def test_rom_data_is_rejected_under_any_name(self):
        for magic in (b'\x80\x37\x12\x40', b'\x37\x80\x40\x12', b'\x40\x12\x37\x80'):
            stage, expected = self.staged('linux')
            (stage / 'README.txt').write_bytes(magic + b'rest')
            expected['README.txt'] = magic + b'rest'
            with self.assertRaisesRegex(SystemExit, 'Forbidden'):
                package.audit(stage, expected)

    def test_generated_c_is_rejected_but_the_authored_module_source_is_not(self):
        stage, expected = self.staged('windows')
        package.audit(stage, expected)  # includes setup-resources/module/module_main.c
        (stage / 'funcs_0.c').write_text('int x;')
        expected['funcs_0.c'] = 'int x;'
        with self.assertRaisesRegex(SystemExit, 'Forbidden'):
            package.audit(stage, expected)

    def test_a_changed_or_missing_file_is_rejected(self):
        stage, expected = self.staged('linux')
        (stage / 'NOTICE.txt').write_text('edited')
        with self.assertRaisesRegex(SystemExit, 'differs from its source'):
            package.audit(stage, expected)
        stage, expected = self.staged('windows')
        (stage / 'README.txt').unlink()
        with self.assertRaisesRegex(SystemExit, 'missing'):
            package.audit(stage, expected)

    def test_symbolic_links_are_rejected(self):
        stage, expected = self.staged('linux')
        (stage / 'link').symlink_to(stage / 'README.txt')
        with self.assertRaisesRegex(SystemExit, 'Symbolic link'):
            package.audit(stage, expected)

    def test_modes_make_only_the_programs_executable(self):
        stage, _ = self.staged('linux')
        for name in ('StarCraft64', 'sc64_setup', 'N64Recomp'):
            self.assertTrue((stage / name).stat().st_mode & stat.S_IXUSR, name)
        for name in ('README.txt', 'NOTICE.txt', 'DejaVuSansMono-Bold.ttf'):
            self.assertFalse((stage / name).stat().st_mode & stat.S_IXUSR, name)


class ArchiveTests(Workspace):
    def test_archives_are_reproducible_and_keep_modes(self):
        for target in ('linux', 'windows'):
            stage, _ = self.staged(target)
            suffix = package.TARGETS[target]['archive']
            first, second = self.directory / f'{target}-1{suffix}', self.directory / f'{target}-2{suffix}'
            package.write_archive(stage, package.package_name(target), first, 1_700_000_000)
            for path in stage.rglob('*'):  # a different checkout time must not matter
                path.touch()
            package.write_archive(stage, package.package_name(target), second, 1_700_000_000)
            self.assertEqual(first.read_bytes(), second.read_bytes(), target)

    def test_tar_modes_and_ownership(self):
        stage, _ = self.staged('linux')
        archive = self.directory / 'a.tar.gz'
        package.write_archive(stage, 'root', archive, 1_700_000_000)
        with tarfile.open(archive) as tar:
            members = {member.name: member for member in tar.getmembers()}
        self.assertEqual(members['root/StarCraft64'].mode, 0o755)
        self.assertEqual(members['root/README.txt'].mode, 0o644)
        self.assertEqual(members['root'].mode, 0o755)
        self.assertTrue(all(m.uid == 0 and m.gid == 0 and m.mtime == 1_700_000_000 for m in members.values()))
        self.assertIn('root/setup-resources/module', members)

    def test_zip_modes(self):
        stage, _ = self.staged('windows')
        archive = self.directory / 'a.zip'
        package.write_archive(stage, 'root', archive, 1_700_000_000)
        with ZipFile(archive) as zipped:
            modes = {info.filename: info.external_attr >> 16 & 0o777 for info in zipped.infolist()}
        self.assertEqual(modes['root/StarCraft64.exe'], 0o755)
        self.assertEqual(modes['root/README.txt'], 0o644)
        self.assertEqual(modes['root/NOTICE.txt'], 0o644)


class RecompilerGateTests(Workspace):
    def setUp(self):
        super().setUp()
        (self.directory / 'recipe').mkdir()
        (self.directory / 'recipe/generated.json').write_text('{"recipe": 1}')
        (self.directory / 'patch_recompiler.py').write_text('# patch tool')
        self.recipe_hash = hashlib.sha256(b'{"recipe": 1}').hexdigest()
        self.patch_hash = hashlib.sha256(b'# patch tool').hexdigest()
        for name, value in (('RECOMPILERS', self.directory / 'recompilers'), ('RECIPE', self.directory / 'recipe'),
                            ('PATCH_TOOL', self.directory / 'patch_recompiler.py')):
            patcher = mock.patch.object(package, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def prepare(self, **receipt):
        directory = self.directory / 'recompilers' / 'linux-x86_64'
        directory.mkdir(parents=True)
        binary = directory / 'N64Recomp'
        binary.write_bytes(b'n64recomp')
        data = {'binary_sha256': hashlib.sha256(b'n64recomp').hexdigest(),
                'recipe_verification': {'generated_files': 86, 'recipe_sha256': self.recipe_hash},
                'source': {'patch_tool_sha256': self.patch_hash}}
        data.update(receipt)
        (directory / 'receipt.json').write_text(json.dumps(data))
        return binary

    def test_a_verified_binary_is_accepted(self):
        binary = self.prepare()
        self.assertEqual(package.check_recompiler('linux'), binary)

    def test_a_verification_of_another_recipe_is_refused(self):
        self.prepare(recipe_verification={'generated_files': 86, 'recipe_sha256': '0' * 64})
        with self.assertRaisesRegex(SystemExit, 'different recipe'):
            package.check_recompiler('linux')
        (self.directory / 'recipe/generated.json').write_text('{"recipe": 2}')  # the recipe changed after the check
        self.prepare_again()
        with self.assertRaisesRegex(SystemExit, 'different recipe'):
            package.check_recompiler('linux')

    def prepare_again(self):
        shutil.rmtree(self.directory / 'recompilers')
        self.prepare()

    def test_a_binary_built_with_an_older_patch_tool_is_refused(self):
        self.prepare()
        (self.directory / 'patch_recompiler.py').write_text('# newer patch tool')
        with self.assertRaisesRegex(SystemExit, 'older patch tool'):
            package.check_recompiler('linux')

    def test_unverified_or_foreign_binaries_are_refused(self):
        with self.assertRaisesRegex(SystemExit, 'build-recompiler'):
            package.check_recompiler('linux')
        self.prepare(recipe_verification=None)
        with self.assertRaisesRegex(SystemExit, 'verify-recompiler'):
            package.check_recompiler('linux')
        (self.directory / 'recompilers/linux-x86_64/receipt.json').write_text(json.dumps({'binary_sha256': '0' * 64, 'recipe_verification': {}}))
        with self.assertRaisesRegex(SystemExit, 'another binary'):
            package.check_recompiler('linux')


class NoticeTests(unittest.TestCase):
    def test_extractors_keep_only_the_license(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'tail.h').write_text('code\n/*\nnoise\nThis software is available under 2 licenses\nMIT text\n*/\n')
            self.assertEqual(notices.tail_from(root / 'tail.h', 'available under 2 licenses')(),
                             'This software is available under 2 licenses\nMIT text\n')
            (root / 'slash.h').write_text('// intro\n// Simplified BSD license:\n// Copyright (c) X\n//\n// Terms\ncode();\n// later\n')
            self.assertEqual(notices.slash_block(root / 'slash.h', 'Simplified BSD')(),
                             'Simplified BSD license:\nCopyright (c) X\n\nTerms\n')
            (root / 'star.h').write_text('#pragma once\n/*\n * other\n */\n/*\n * freely redistributable under the MIT License.\n *\n * Copyright (c) Y\n */\n')
            self.assertEqual(notices.star_block(root / 'star.h', 'freely redistributable')(),
                             'freely redistributable under the MIT License.\n\nCopyright (c) Y\n')
            (root / 'json.hpp').write_text('// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://x>\n')
            (root / 'LICENSE').write_text('The MIT License (MIT)\n\nCopyright (c) 2024 Someone\n\nPermission is hereby granted, free of charge\nthe rest\n')
            text = notices.mit_with_copyright(root / 'json.hpp', root / 'LICENSE')()
            self.assertIn('Copyright (c) 2013 - 2025 Niels Lohmann', text)
            self.assertIn('Permission is hereby granted', text)
            self.assertNotIn('Someone', text)
            with self.assertRaisesRegex(SystemExit, 'no license block'):
                notices.tail_from(root / 'tail.h', 'absent marker')()
            with self.assertRaisesRegex(SystemExit, 'not found'):
                notices.whole(root / 'missing')()

    def test_render_states_the_gpl_and_where_the_source_is(self):
        items = [notices.Component('N64ModernRuntime', 'GPL-3.0', 'StarCraft64', 'GPL-3.0.txt', None)]
        info = {'url': 'https://example.org/repo', 'commit': 'a' * 40, 'runtime_commit': 'b' * 40, 'recompiler_commit': 'c' * 40}
        files = notices.render(info, items, {'GPL-3.0.txt': 'license text'})
        self.assertEqual(set(files), {'NOTICE.txt', 'SOURCE.txt', 'LICENSES/GPL-3.0.txt'})
        self.assertIn('General Public License version 3', files['NOTICE.txt'])
        self.assertIn('no game code', files['NOTICE.txt'])
        for fragment in (info['url'], 'a' * 40, 'b' * 40, 'c' * 40, 'git checkout'):
            self.assertIn(fragment, files['SOURCE.txt'])

    def test_the_published_source_url_is_anonymous_and_clonable(self):
        for remote in ('git@github.com:n0madic/StarCraft64-recomp.git', 'ssh://git@github.com/n0madic/StarCraft64-recomp',
                       'https://user:ghp_secret@github.com/n0madic/StarCraft64-recomp.git', 'https://github.com/n0madic/StarCraft64-recomp'):
            with self.subTest(remote=remote):
                self.assertEqual(notices.public_url(remote), 'https://github.com/n0madic/StarCraft64-recomp')
        with self.assertRaisesRegex(SystemExit, 'not a URL'):
            notices.public_url('/Users/someone/repo')

    def test_a_missing_dependency_commit_stops_the_build(self):
        with tempfile.TemporaryDirectory() as directory, mock.patch.object(notices, 'components', return_value=[]):
            with self.assertRaisesRegex(SystemExit, 'git reports none'):
                notices.build(Path(directory))

    def test_static_runtime_licenses_belong_to_the_targets_that_link_them(self):
        names = {target: {item.file for item in notices.runtime_components(target)} for target in ('macos', 'linux', 'windows')}
        self.assertEqual(names['macos'], set())
        self.assertIn('glibc-LGPL-2.1.txt', names['linux'])
        self.assertIn('mingw-w64.txt', names['windows'])
        self.assertIn('GCC-runtime-exception.txt', names['linux'] & names['windows'])
        info = {'url': 'https://example.org/repo', 'commit': 'a' * 40, 'runtime_commit': 'b' * 40, 'recompiler_commit': 'c' * 40}
        self.assertIn('GNU C Library', notices.render(info, [], {}, 'linux')['SOURCE.txt'])
        self.assertNotIn('GNU C Library', notices.render(info, [], {}, 'windows')['SOURCE.txt'])

    def test_license_sources_are_tried_in_order_and_a_miss_is_an_error(self):
        with tempfile.TemporaryDirectory() as directory:
            (Path(directory) / 'found.txt').write_text('  license body\n\n')
            read = notices.first_of('x', notices.local_file(directory + '/absent*'), notices.local_file(directory + '/found*'))
            self.assertEqual(read(), 'license body\n')
            with self.assertRaisesRegex(SystemExit, 'No license text for y'):
                notices.first_of('y', notices.local_file(directory + '/absent*'))()

    @unittest.skipUnless((ROOT / 'third_party/N64ModernRuntime/COPYING').is_file() and (ROOT / 'local/toolchains/miniaudio.h').is_file(),
                         'needs the pinned dependency sources')
    def test_the_real_components_all_produce_a_license(self):
        files, info = notices.build()
        self.assertGreaterEqual(len(files), 19)
        self.assertIn('GNU GENERAL PUBLIC LICENSE', files['LICENSES/GPL-3.0.txt'])
        for name, text in files.items():
            self.assertTrue(text.strip(), name)
        self.assertEqual(len(info['commit']), 40)


class NeutralIconTests(unittest.TestCase):
    @unittest.skipUnless(neutral_icon.bootstrap.FONT.is_file(), 'needs the bootstrapped DejaVu font')
    def test_the_icon_is_deterministic_authored_and_not_the_game_art(self):
        with tempfile.TemporaryDirectory() as directory:
            first = neutral_icon.build(Path(directory) / 'a')
            second = neutral_icon.build(Path(directory) / 'b')
            self.assertEqual(first, second)
            self.assertFalse(first['game_derived'])
            self.assertEqual(first['label'], 'SC64')
            from PIL import Image
            art = Image.open(Path(directory) / 'a/icon-1024.png').convert('RGB')
            self.assertEqual(art.size, (1024, 1024))
            # A gradient, one lettering color and anti-aliasing: far fewer colors than a title-art crop.
            self.assertLess(len(art.getcolors(maxcolors=100000)), 5000)
            top, bottom = art.getpixel((512, 40)), art.getpixel((512, 990))
            self.assertTrue(all(abs(a - b) < 12 for a, b in zip(top, neutral_icon.BACKGROUND)), top)
            self.assertGreater(sum(bottom), sum(top))  # the gradient brightens downward
            icon = Image.open(Path(directory) / 'a/StarCraft64.ico')
            self.assertIn((256, 256), icon.info['sizes'])
            self.assertEqual((Path(directory) / 'a/StarCraft64.rc').read_text(), '1 ICON "StarCraft64.ico"\n')

    def test_the_label_is_the_requested_one(self):
        self.assertEqual(neutral_icon.LABEL, 'SC64')


class SourceTests(unittest.TestCase):
    def test_the_launcher_is_built_without_game_code(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertIn('option(SC64_BUILD_GAME_MODULE', cmake)
        source = (ROOT / 'tools/desktop/package.py').read_text()
        self.assertIn('-DSC64_BUILD_GAME_MODULE=OFF', source)
        self.assertNotIn('installed_icon', source)
        self.assertNotIn('generated/game', source)

    def test_android_selects_the_static_game_unless_the_launcher_is_requested(self):
        # The personal APK (the default) links the game; only `--launcher` turns that off (ADR-090).
        self.assertIn('option(SC64_STATIC_GAME "Link the generated game statically" ON)', (ROOT / 'native/android/CMakeLists.txt').read_text())
        build = (ROOT / 'tools/android/build.py').read_text()
        self.assertIn('-DSC64_STATIC_GAME={"OFF" if args.launcher else "ON"}', build)
        self.assertIn("if not args.launcher:\n        validate_ui_hooks", build)

    def test_build_paths_are_not_embedded(self):
        self.assertIn('-ffile-prefix-map=', (ROOT / 'tools/desktop/package.py').read_text())


if __name__ == '__main__':
    unittest.main()
