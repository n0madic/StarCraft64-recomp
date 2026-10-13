import importlib.util
import sys
import unittest
import hashlib
import io
import json
import subprocess
import tempfile
from contextlib import redirect_stdout
from pathlib import Path
from unittest.mock import patch as mock_patch
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
def module(name):
    spec = importlib.util.spec_from_file_location('sc64_' + name, ROOT / 'tools/desktop' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = result
    spec.loader.exec_module(result)
    return result
patch = module('patch_shell_runtime')
gpu = module('check_gpu')
live = module('check_live')
launcher = module('run')
sys.path.insert(0, str(ROOT / 'tools/desktop'))
builder = module('build')

class LiveGateTests(unittest.TestCase):
    def test_audio_trace_cli_rejects_unbounded_or_guestless_capture(self):
        for args in [('--audio-from','1','--audio-to','602','--rom','unused'),
                     ('--audio-from','-1','--audio-to','100','--rom','unused'),
                     ('--audio-from','1','--audio-to','100'),
                     ('--audio-from','100','--audio-to','1','--rom','unused')]:
            with mock_patch.object(sys,'argv',['run','--output',str(ROOT/'local/reports/nonexistent-audio-test'),*args]):
                with self.assertRaisesRegex(SystemExit,'Audio trace needs'):
                    launcher.main()

    def log(self, *, paused_fields=600, resume_fields=1200):
        import json
        rows = []
        for seconds, fields, paused in ((0, 0, False), (5, 300, False), (10, 600, True),
                                        (15, paused_fields, True), (20, 600, False), (30, resume_fields, False)):
            row = {name: fields for name in live.FROZEN}
            row.update(host_seconds=seconds, error='', running=True, paused=paused, audio_dropped_frames=0)
            rows.append('INFO:SC64: SC64 ' + json.dumps(row))
        return '\n'.join(rows)

    def test_freeze_and_resume(self):
        result = live.analyze(self.log())
        self.assertTrue(result['pause_accepted'])
        self.assertTrue(result['cadence_accepted'])
        self.assertTrue(result['resume_accepted'])

    def test_paused_progress_is_rejected(self):
        self.assertFalse(live.analyze(self.log(paused_fields=601))['pause_accepted'])

    def test_catchup_after_resume_is_rejected(self):
        self.assertFalse(live.analyze(self.log(resume_fields=1800))['cadence_accepted'])

    def test_engine_error_is_rejected(self):
        with self.assertRaises(ValueError): live.analyze(self.log() + '\nERROR:SC64: failed')

    def test_background_input_probe_is_not_lifecycle_evidence(self):
        rows=[]
        for line in self.log().splitlines():
            if 'SC64 {' in line:
                sample=json.loads(line.split('SC64 ',1)[1])
                sample['background_input_probe']=True
                line='INFO:SC64: SC64 '+json.dumps(sample)
            rows.append(line)
        code,report=self.check('\n'.join(rows))
        self.assertEqual(code,1)
        self.assertTrue(report['background_input_probe'])
        self.assertFalse(report['lifecycle_accepted'])

    def check(self, log, flags=(), resources=True, resume_requested=False):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            output = root / 'local/report'
            output.mkdir(parents=True)
            (output / 'engine.log').write_text(log)
            receipt = {'schema': 3, 'log_sha256': hashlib.sha256(log.encode()).hexdigest(), 'exit_code': 0,
                       'binary_unchanged': True, 'build': {'binary_sha256': 'test', 'profile': 'arm64-macos'},
                       'resume_requested': resume_requested}
            if resources is not None: receipt['resources_unchanged'] = resources
            (output / 'run.json').write_text(json.dumps(receipt))
            with mock_patch.object(live, 'ROOT', root), mock_patch.object(sys, 'argv', ['check_live', str(output), *flags]), redirect_stdout(io.StringIO()):
                try:
                    live.main()
                    code = 0
                except SystemExit as error: code = error.code
            return code, json.loads((output / 'lifecycle.json').read_text())

    def test_resume_cli_rejects_pause_until_exit(self):
        lines = self.log().splitlines()
        paused = json.loads(lines[3].split('SC64 ', 1)[1])
        paused['host_seconds'] = 20
        log = '\n'.join(lines[:4] + ['INFO:SC64: SC64 ' + json.dumps(paused)])
        code, report = self.check(log, ['--require-resume'])
        self.assertEqual(code, 1)
        self.assertFalse(report['lifecycle_accepted'])
        self.assertFalse(report['resume_accepted'])
        # The same run remains valid for the separate paused-stop mode.
        code, report = self.check(log, ['--require-paused-shutdown'])
        self.assertEqual(code, 0)
        self.assertTrue(report['lifecycle_accepted'])
        code, report = self.check(log, resume_requested=True)
        self.assertEqual(code, 1)
        self.assertTrue(report['require_resume'])

    def test_resume_cli_requires_measured_interval(self):
        code, report = self.check('\n'.join(self.log().splitlines()[:-1]), ['--require-resume'])
        self.assertEqual(code, 1)
        self.assertFalse(report['resume_accepted'])

    def test_resume_cli_accepts_measured_nominal_cadence(self):
        code, report = self.check(self.log(), ['--require-resume'])
        self.assertEqual(code, 0)
        self.assertTrue(report['resume_accepted'])

    def test_live_cli_rejects_engine_era_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            output = root / 'local/report'; output.mkdir(parents=True)
            (output / 'engine.log').write_text(self.log())
            (output / 'run.json').write_text(json.dumps({'schema': 2, 'engine_unchanged': True}))
            with mock_patch.object(live, 'ROOT', root), mock_patch.object(sys, 'argv', ['check_live', str(output)]):
                with self.assertRaisesRegex(ValueError, 'predates'): live.main()

    def test_live_cli_rejects_resource_drift_and_legacy_receipt(self):
        for value in (False, None):
            with self.subTest(resources=value):
                code, report = self.check(self.log(), resources=value)
                self.assertEqual(code, 1)
                self.assertFalse(report['lifecycle_accepted'])

class DesktopPatchTests(unittest.TestCase):
    def source(self, root):
        source = root / 'local/source'
        files = {
            'ultramodern/src/scheduling.cpp': 'void ultramodern::check_running_queue(RDRAM_ARG1) {\n}\n',
            'ultramodern/src/events.cpp': '    while (!exited) {\n        ultramodern::sleep_until(next);\n}\n'
                'extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {\n'
                '    std::lock_guard lock{ events_context.message_mutex };\n}\n',
            'ultramodern/src/timer.cpp': '    while (!sc64_timers_stopping.load()) {\n        else {\n            if (sc64_timers_stopping.load()) return;\n}\n',
            'librecomp/src/os.cpp': 'version A',
            'librecomp/src/recomp.cpp': 'bool recomp::load_stored_rom(const std::u8string& game_id) {\n}\n',
        }
        for name, content in files.items():
            file = source / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content)
        (source / 'sc64-host-patch.json').write_text(json.dumps({'patched_sha256': {
            name: hashlib.sha256(data.encode()).hexdigest() for name, data in files.items()
            if name.startswith('ultramodern/') or name == 'librecomp/src/recomp.cpp'}}))
        return source

    def test_full_source_update_addition_and_removal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, output = self.source(root), root / 'local/output'
            patch.prepare(source, output)
            changed = source / 'librecomp/src/os.cpp'
            changed.write_text('version B')
            added = source / 'new.h'
            added.write_text('new header')
            patch.prepare(source, output)
            self.assertEqual((output / 'librecomp/src/os.cpp').read_text(), 'version B')
            self.assertEqual((output / 'new.h').read_text(), 'new header')
            added.unlink()
            patch.prepare(source, output)
            self.assertFalse((output / 'new.h').exists())
            stamp = (output / 'librecomp/src/os.cpp').stat().st_mtime_ns
            patch.prepare(source, output)
            self.assertEqual((output / 'librecomp/src/os.cpp').stat().st_mtime_ns, stamp)

    def test_refuses_unrelated_edit_outside_three_patches(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, output = self.source(root), root / 'local/output'
            patch.prepare(source, output)
            file = output / 'librecomp/src/os.cpp'
            file.write_text('user edit')
            with self.assertRaises(ValueError): patch.prepare(source, output)
            self.assertEqual(file.read_text(), 'user edit')

    def test_failed_copy_preserves_previous_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, output = self.source(root), root / 'local/output'
            patch.prepare(source, output)
            before = patch.tree_hashes(output)
            (source / 'new.h').write_text('header')
            copytree = patch.shutil.copytree
            def corrupt_copy(src, dst, *args, **kwargs):
                copytree(src, dst, *args, **kwargs)
                if Path(dst).name == 'runtime':
                    (Path(dst) / 'librecomp/src/os.cpp').write_text('incomplete copy')
            with mock_patch.object(patch.shutil, 'copytree', corrupt_copy):
                with self.assertRaises(ValueError): patch.prepare(source, output)
            self.assertEqual(patch.tree_hashes(output), before)

    def test_failed_publish_rolls_back_previous_tree(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, output = self.source(root), root / 'local/output'
            patch.prepare(source, output)
            before = patch.tree_hashes(output)
            (source / 'new.h').write_text('header')
            replace = patch.os.replace
            def fail_publish(src, dst):
                if Path(src).name == 'runtime': raise OSError('simulated publish failure')
                replace(src, dst)
            with mock_patch.object(patch.os, 'replace', fail_publish):
                with self.assertRaises(OSError): patch.prepare(source, output)
            self.assertEqual(patch.tree_hashes(output), before)

    def test_legacy_upgrade_retains_untracked_files_in_backup(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            source, output = self.source(root), root / 'local/output'
            marker = patch.prepare(source, output)
            manifest = json.loads(marker.read_text())
            marker.write_text(json.dumps({'schema': 1, 'patched_sha256': manifest['patched_sha256']}))
            (output / 'personal-note.txt').write_text('keep me')
            (source / 'librecomp/src/os.cpp').write_text('version B')
            patch.prepare(source, output)
            self.assertEqual((output / 'librecomp/src/os.cpp').read_text(), 'version B')
            backups = list(output.parent.glob('.sc64-runtime-*-previous'))
            self.assertEqual(len(backups), 1)
            self.assertEqual((backups[0] / 'personal-note.txt').read_text(), 'keep me')

    def test_safe_points_and_drift(self):
        source = 'void ultramodern::check_running_queue(RDRAM_ARG1) {\n}\n'
        result = patch.transform('ultramodern/src/scheduling.cpp', source)
        self.assertIn('sc64_pause_point();', result)
        for invalid in ('', source + source):
            with self.assertRaises(ValueError):
                patch.transform('ultramodern/src/scheduling.cpp', invalid)

    def test_preloaded_rom_hook_and_drift(self):
        source = 'bool recomp::load_stored_rom(const std::u8string& game_id) {\n    read_file();\n}\n'
        result = patch.transform('librecomp/src/recomp.cpp', source)
        self.assertIn('if (sc64_rom_is_preloaded(game_id)) return true;', result)
        self.assertLess(result.index('sc64_rom_is_preloaded(game_id)'), result.index('read_file();'))
        for invalid in ('', source + source):
            with self.assertRaises(ValueError): patch.transform('librecomp/src/recomp.cpp', invalid)

    SWAP = ('extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {\n'
            '    std::lock_guard lock{ events_context.message_mutex };\n}\n')

    def test_vi_late_deadline_is_signed(self):
        source = '    while (!exited) {\n        ultramodern::sleep_until(next);\n}\n' + self.SWAP
        result = patch.transform('ultramodern/src/events.cpp', source)
        self.assertIn('duration_cast<std::chrono::high_resolution_clock::duration>', result)
        self.assertIn('auto delay = deadline - sc64_runtime_now();', result)
        self.assertEqual(result.count('sc64_pause_point();'), 2)

    def test_vi_swap_owns_cpu_framebuffer_before_the_swap(self):
        source = '    while (!exited) {\n        ultramodern::sleep_until(next);\n}\n' + self.SWAP
        result = patch.transform('ultramodern/src/events.cpp', source)
        self.assertLess(result.index('sc64_vi_swap(rdram, frameBufPtr);'),
                        result.index('std::lock_guard lock{ events_context.message_mutex };'))
        for invalid in (source.replace(self.SWAP, ''), source + self.SWAP):
            with self.assertRaises(ValueError):
                patch.transform('ultramodern/src/events.cpp', invalid)

class PresentationTests(unittest.TestCase):
    def test_gpu_cli_rejects_changed_resources_and_legacy_receipt(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            output = root / 'local/run/gpu'
            output.mkdir(parents=True)
            manifest = root / 'manifest.json'
            manifest.write_text('{"cases": []}')
            for value in (False, None):
                receipt = {'schema': 3, 'exit_code': 0, 'binary_unchanged': True}
                if value is not None: receipt['resources_unchanged'] = value
                (output.parent / 'run.json').write_text(json.dumps(receipt))
                with self.subTest(resources=value), mock_patch.object(gpu, 'ROOT', root), mock_patch.object(sys, 'argv', ['check_gpu', str(output), '--manifest', str(manifest)]):
                    with self.assertRaises(ValueError): gpu.main()

    def test_all_replicated_pixels_and_letterbox(self):
        source = np.array([[[255, 0, 0], [0, 255, 0]], [[0, 0, 255], [17, 29, 47]]], dtype=np.uint8)
        bgra = np.zeros((8, 10, 4), dtype=np.uint8)
        bgra[1:7, 2:8, :3] = np.repeat(np.repeat(source[:, :, ::-1], 3, axis=0), 3, axis=1)
        # Window 10x8 would choose scale4; explicitly use 8x6 for scale3.
        bgra = bgra[1:7, 1:9]
        metadata = {'window_width': 8, 'window_height': 6, 'native_width': 2, 'native_height': 2, 'origin': 'top-left'}
        actual = gpu.presented_rgb(bgra.tobytes(), metadata)
        self.assertTrue(np.all(actual == gpu.nearest_fit(source,actual.shape[:2])))
        bgra[2, 3, 1] ^= 1
        self.assertFalse(np.all(gpu.presented_rgb(bgra.tobytes(), metadata) == gpu.nearest_fit(source,actual.shape[:2])))

    def test_fractional_fit_checks_full_viewport_and_black_bars(self):
        source = np.arange(36, dtype=np.uint8).reshape(3, 4, 3)
        reference = gpu.nearest_fit(source, (7, 10))
        bgra = np.zeros((8, 10, 4), dtype=np.uint8)
        bgra[1:, :, :3] = reference[:, :, ::-1]
        metadata = {'window_width': 10, 'window_height': 8, 'native_width': 4,
                    'native_height': 3, 'origin': 'top-left'}
        self.assertTrue(np.array_equal(gpu.presented_rgb(bgra.tobytes(),metadata),reference))
        bgra[0, 1, 0] = 1
        with self.assertRaisesRegex(ValueError,'Letterbox'):
            gpu.presented_rgb(bgra.tobytes(),metadata)

    def test_invalid_readback_is_rejected(self):
        metadata = {'window_width': 8, 'window_height': 6, 'native_width': 2, 'native_height': 2, 'origin': 'top-left'}
        with self.assertRaises(ValueError): gpu.presented_rgb(b'', metadata)
        with self.assertRaises(ValueError): gpu.presented_rgb(bytes(192), {**metadata, 'origin': 'bottom-left'})

    def test_nearest_boundary_only_permits_adjacent_source_texels(self):
        source = np.arange(36, dtype=np.uint8).reshape(3, 4, 3)
        captured = gpu.nearest_fit(source, (7, 10)).copy()
        # Output column 2 maps exactly to the boundary between source 0/1.
        captured[0, 2] = source[0, 0]
        self.assertTrue(gpu.nearest_valid(source, captured))
        captured[0, 2] = source[0, 2]
        self.assertFalse(gpu.nearest_valid(source, captured))
        captured = gpu.nearest_fit(source, (7, 10)).copy()
        captured[0, 0] = source[0, 1]
        self.assertFalse(gpu.nearest_valid(source, captured))

class LaunchReceiptTests(unittest.TestCase):
    def launch(self, mutation=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            app = root / 'local/dist/StarCraft64.app/Contents'
            resources = app / 'Resources'
            resources.mkdir(parents=True)
            (app / 'MacOS').mkdir()
            binary = app / 'MacOS/SC64'
            binary.write_bytes(b'binary')
            (resources / 'DejaVuSansMono-Bold.ttf').write_bytes(b'font')
            (resources / 'LICENSE-DejaVu.txt').write_bytes(b'license')
            build = root / 'local/reports/desktop/build.json'
            build.parent.mkdir(parents=True)
            build.write_text(json.dumps({'schema': 2, 'binary_sha256': launcher.sha256(binary),
                                         'resources_sha256': launcher.resource_hashes(resources)}))
            output = root / 'local/run'
            def process(*args, **kwargs):
                if mutation: mutation(resources)
                return subprocess.CompletedProcess([], 0)
            host = launcher.hostenv.Host('darwin', 'arm64')
            with mock_patch.object(launcher, 'ROOT', root), mock_patch.object(launcher.hostenv, 'current', return_value=host), mock_patch.object(launcher.subprocess, 'run', process), mock_patch.object(sys, 'argv', ['run', '--output', str(output)]), redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as error: launcher.main()
            return error.exception.code, json.loads((output / 'run.json').read_text())

    def test_unchanged_bundle_receipt(self):
        code, report = self.launch()
        self.assertEqual(code, 0)
        self.assertTrue(report['binary_unchanged'])
        self.assertTrue(report['resources_unchanged'])

    def test_resource_edits_additions_and_removals_invalidate_receipt(self):
        mutations = [lambda root: (root / 'DejaVuSansMono-Bold.ttf').write_bytes(b'changed'),
                     lambda root: (root / 'DejaVuSansMono-Bold.ttf').unlink(),
                     lambda root: (root / 'added.ttf').write_bytes(b'added'),
                     lambda root: (root / 'LICENSE-DejaVu.txt').write_bytes(b'new license')]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                code, report = self.launch(mutation)
                self.assertNotEqual(code, 0)
                self.assertEqual(report['exit_code'], 0)
                self.assertTrue(report['binary_unchanged'])
                self.assertFalse(report['resources_unchanged'])

class PackageLaunchTests(unittest.TestCase):
    """Linux and Windows packages launch under the same receipt binding as the macOS bundle."""
    def launch(self, host, name, mutation=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            package = root / f'local/dist/StarCraft64-{host.system}'
            package.mkdir(parents=True)
            binary = package / name
            binary.write_bytes(b'binary')
            (package / 'DejaVuSansMono-Bold.ttf').write_bytes(b'font')
            files = launcher.resource_hashes(package)
            receipt = root / f'local/reports/{host.system}/build.json'
            receipt.parent.mkdir(parents=True)
            receipt.write_text(json.dumps({'schema': 1, 'binary_sha256': launcher.sha256(binary), 'files_sha256': files}))
            output = root / 'local/run'
            commands = []
            def process(command, *args, **kwargs):
                commands.append(command)
                if mutation: mutation(package)
                return subprocess.CompletedProcess([], 0)
            with mock_patch.object(launcher, 'ROOT', root), mock_patch.object(launcher.hostenv, 'current', return_value=host), mock_patch.object(launcher.subprocess, 'run', process), mock_patch.object(sys, 'argv', ['run', '--output', str(output)]), redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as error: launcher.main()
            return error.exception.code, commands, package

    def test_linux_and_windows_packages_run_their_own_binary(self):
        for host, name in ((launcher.hostenv.Host('linux', 'x86_64'), 'StarCraft64'),
                           (launcher.hostenv.Host('windows', 'x86_64'), 'StarCraft64.exe')):
            with self.subTest(host=host.label):
                code, commands, package = self.launch(host, name)
                self.assertEqual(code, 0)
                self.assertEqual(commands[0][0], str(package / name))

    def test_game_module_edit_is_rejected_before_launch(self):
        host = launcher.hostenv.Host('linux', 'x86_64')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            package = root / 'local/dist/StarCraft64-linux'
            package.mkdir(parents=True)
            (package / 'StarCraft64').write_bytes(b'binary')
            (package / 'sc64-game.so').write_bytes(b'module')
            receipt = root / 'local/reports/linux/build.json'
            receipt.parent.mkdir(parents=True)
            receipt.write_text(json.dumps({'schema': 1, 'binary_sha256': launcher.sha256(package / 'StarCraft64'),
                                           'files_sha256': launcher.resource_hashes(package)}))
            (package / 'sc64-game.so').write_bytes(b'another module')
            with mock_patch.object(launcher, 'ROOT', root), mock_patch.object(launcher.hostenv, 'current', return_value=host), \
                    mock_patch.object(sys, 'argv', ['run', '--output', str(root / 'local/run')]):
                with self.assertRaisesRegex(SystemExit, 'resources differ'): launcher.main()

    def test_package_edit_is_rejected_before_launch(self):
        host = launcher.hostenv.Host('linux', 'x86_64')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            package = root / 'local/dist/StarCraft64-linux'
            package.mkdir(parents=True)
            (package / 'StarCraft64').write_bytes(b'binary')
            receipt = root / 'local/reports/linux/build.json'
            receipt.parent.mkdir(parents=True)
            receipt.write_text(json.dumps({'schema': 1, 'binary_sha256': launcher.sha256(package / 'StarCraft64'),
                                           'files_sha256': {'StarCraft64': launcher.sha256(package / 'StarCraft64')}}))
            (package / 'extra.ttf').write_bytes(b'added')
            with mock_patch.object(launcher, 'ROOT', root), mock_patch.object(launcher.hostenv, 'current', return_value=host), \
                    mock_patch.object(sys, 'argv', ['run', '--output', str(root / 'local/run')]):
                with self.assertRaisesRegex(SystemExit, 'resources differ'): launcher.main()

    def test_missing_build_and_unsupported_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary).resolve()
            for host, message in ((launcher.hostenv.Host('linux', 'x86_64'), 'No build for this host'),
                                  (launcher.hostenv.Host('linux', 'arm64'), 'natively')):
                with self.subTest(host=host.label), mock_patch.object(launcher, 'ROOT', root), \
                        mock_patch.object(launcher.hostenv, 'current', return_value=host), \
                        mock_patch.object(sys, 'argv', ['run', '--output', str(root / 'local/run')]):
                    with self.assertRaisesRegex(SystemExit, message): launcher.main()

class PublishBundleTest(unittest.TestCase):
    def tree(self, root, marker):
        (root / 'Contents').mkdir(parents=True)
        (root / 'Contents/marker').write_text(marker)

    def test_success_removes_previous_and_stale_backups(self):
        with tempfile.TemporaryDirectory() as directory:
            dist = Path(directory)
            app, staged = dist / 'StarCraft64.app', dist / 'stage/StarCraft64.app'
            self.tree(app, 'old'); self.tree(staged, 'new')
            self.tree(dist / '.sc64-build-old1-previous.app', 'stale')
            self.tree(dist / 'keep.app', 'unrelated')
            builder.publish_bundle(staged, app, dist / '.sc64-build-now-previous.app')
            self.assertEqual((app / 'Contents/marker').read_text(), 'new')
            self.assertEqual(sorted(p.name for p in dist.iterdir()), ['StarCraft64.app', 'keep.app', 'stage'])

    def test_failed_publish_restores_previous_bundle(self):
        with tempfile.TemporaryDirectory() as directory:
            dist = Path(directory)
            app, previous = dist / 'StarCraft64.app', dist / '.sc64-build-now-previous.app'
            self.tree(app, 'old')
            with self.assertRaises(FileNotFoundError):
                builder.publish_bundle(dist / 'missing.app', app, previous)
            self.assertEqual((app / 'Contents/marker').read_text(), 'old')
            self.assertFalse(previous.exists())

if __name__ == '__main__': unittest.main()
