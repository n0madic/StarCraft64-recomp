import importlib.util
import struct
import tempfile
import unittest
import json
import hashlib
from unittest.mock import patch
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('check_apk', ROOT / 'tools/android/check_apk.py')
apk_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apk_check)
import sys
sys.modules['check_apk'] = apk_check
run_spec = importlib.util.spec_from_file_location('android_run', ROOT / 'tools/android/run.py')
android_run = importlib.util.module_from_spec(run_spec)
run_spec.loader.exec_module(android_run)

def elf(alignment=16384, relro_end=16384):
    data = bytearray(256)
    data[:6] = b'\x7fELF\x02\x01'
    struct.pack_into('<H', data, 18, 183)
    struct.pack_into('<Q', data, 32, 64)
    struct.pack_into('<HH', data, 54, 56, 2)
    struct.pack_into('<IIQQQQQQ', data, 64, 1, 5, 0, 0, 0, 256, 256, alignment)
    struct.pack_into('<IIQQQQQQ', data, 120, 0x6474e552, 4, 0, 0, 0, 0, relro_end, 1)
    return bytes(data)

class AndroidPackageTests(unittest.TestCase):
    def test_bad_bundle_input_cannot_replace_existing_apk(self):
        spec = importlib.util.spec_from_file_location('bundle_build', ROOT / 'tools/android/build.py')
        builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / 'local/dist/StarCraft64-android.apk'
            target.parent.mkdir(parents=True); target.write_bytes(b'previous complete APK')
            rom = root / 'wrong.z64'; rom.write_bytes(b'\x80\x37\x12\x40' + bytes(apk_check.ROM_SIZE - 4))
            with patch.object(builder, 'ROOT', root), patch.object(sys, 'argv', ['build', '--bundle-rom', str(rom)]), \
                 patch.object(builder.bootstrap, 'main') as prepare, patch.object(builder, 'run') as run:
                with self.assertRaisesRegex(ValueError, 'SHA-1'): builder.main()
                prepare.assert_not_called(); run.assert_not_called()
            self.assertEqual(target.read_bytes(), b'previous complete APK')

    def test_opengles_variant_has_separate_outputs_and_config(self):
        spec = importlib.util.spec_from_file_location('variant_build', ROOT / 'tools/android/build.py')
        builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
        root = Path('/repo')
        self.assertEqual(builder.DEFAULT_RENDER_DRIVER, 'vulkan')
        self.assertEqual(builder.output_paths('vulkan', root),
                         (root / 'local/dist/StarCraft64-android.apk', root / 'local/reports/android/build.json'))
        self.assertEqual(builder.output_paths('opengles2', root),
                         (root / 'local/dist/StarCraft64-android-opengles2.apk',
                          root / 'local/reports/android/build-opengles2.json'))
        self.assertIn('sc64.render_driver = vulkan,opengles2\n', builder.config_text(builder.DEFAULT_RENDER_DRIVER, True))
        self.assertIn('sc64.render_driver = opengles2\n', builder.config_text('opengles2', False))
        self.assertIn('sc64.bundled_rom = 1\n', builder.config_text('opengles2', True))
        self.assertIn('sc64.log_level = debug\n', builder.config_text('opengles2', False))  # the probes read DEBUG telemetry

    def test_launcher_variant_has_its_own_outputs_package_and_manifest(self):
        spec = importlib.util.spec_from_file_location('launcher_build', ROOT / 'tools/android/build.py')
        builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
        root = Path('/repo')
        self.assertEqual(builder.output_paths('vulkan', root, launcher=True),
                         (root / 'local/dist/StarCraft64-android-launcher.apk', root / 'local/reports/android/build-launcher.json'))
        self.assertEqual(builder.output_paths('opengles2', root, True)[0], root / 'local/dist/StarCraft64-android-launcher-opengles2.apk')
        self.assertEqual(builder.output_paths('vulkan', root)[0], root / 'local/dist/StarCraft64-android.apk')  # personal APK unchanged
        self.assertNotEqual(builder.LAUNCHER_PACKAGE, builder.PERSONAL_PACKAGE)
        source = (ROOT / 'native/android/AndroidManifest.xml').read_text()
        manifest = builder.launcher_manifest(source)
        self.assertIn(f'package="{builder.LAUNCHER_PACKAGE}"', manifest)
        # The home screen shows the activity's label, so both labels change.
        self.assertEqual(manifest.count(f'android:label="{builder.LAUNCHER_LABEL}"'), 2)
        self.assertNotIn('android:label="StarCraft 64"', manifest)
        self.assertNotIn(builder.PERSONAL_PACKAGE, manifest.split('<application')[0].split('<manifest')[1])
        self.assertEqual(manifest.count('android:name="com.nomadic.sc64.Sc64Activity"'), 1)  # same activity class
        with self.assertRaises(ValueError):
            builder.launcher_manifest(source.replace('android:label="StarCraft 64"', 'android:label="X"', 1))

    def test_launcher_audit_rejects_a_game_linked_library_and_a_rom(self):
        self.assertTrue(apk_check.contains_game_code(b'..recomp_entrypoint..'))
        self.assertTrue(apk_check.contains_game_code(b'sc64_resident_8000B988'))
        self.assertFalse(apk_check.contains_game_code(b'only the shell and SDL'))
        with tempfile.TemporaryDirectory() as directory:
            for game, launcher, ok in ((False, True, True), (True, True, False), (True, False, True)):
                apk = Path(directory) / f'{int(game)}{int(launcher)}.apk'
                payload = elf() + (b'\0recomp_entrypoint\0' if game else b'')
                with ZipFile(apk, 'w') as archive:
                    archive.writestr('lib/arm64-v8a/libmain.so', payload, compress_type=ZIP_DEFLATED)
                    archive.writestr('classes.dex', b'dex')
                if ok:
                    report = apk_check.audit(apk, launcher=launcher)
                    self.assertEqual((report['game_code_linked'], report['launcher']), (game, launcher))
                else:
                    with self.assertRaisesRegex(ValueError, 'links the recompiled game'):
                        apk_check.audit(apk, launcher=launcher)
            with self.assertRaisesRegex(ValueError, 'never contains a ROM'):
                apk_check.audit(Path(directory) / '00.apk', bundled_rom=True, launcher=True)

    def test_constants_shared_between_c_cmake_manifest_and_python_agree(self):
        import re
        sys.path.insert(0, str(ROOT / 'tools'))
        sys.path.insert(0, str(ROOT / 'tools/android'))
        import adb_client
        import pins
        spec = importlib.util.spec_from_file_location('sc64_driver_constants', ROOT / 'tools/sc64.py')
        driver = importlib.util.module_from_spec(spec); spec.loader.exec_module(driver)
        header = (ROOT / 'native/setup/toolchain.hpp').read_text()
        # The module is built for the API level of the app (sc64_setup --target android-arm64).
        self.assertEqual(int(re.search(r'kAndroidApi = (\d+);', header).group(1)), pins.API)
        names = re.search(r'target_names\(\) \{ return \{([^}]*)\}; \}', (ROOT / 'native/setup/toolchain.cpp').read_text()).group(1)
        self.assertEqual(tuple(re.findall(r'"([^"]+)"', names)), driver.MODULE_TARGETS)
        # The personal application ID is written in four places.
        self.assertIn(f'package="{adb_client.PACKAGE}"', (ROOT / 'native/android/AndroidManifest.xml').read_text())
        self.assertIn(f'set(SC64_ANDROID_PACKAGE "{adb_client.PACKAGE}"', (ROOT / 'native/android/CMakeLists.txt').read_text())
        self.assertIn(f'#define SC64_APP_ID "{adb_client.PACKAGE}"', (ROOT / 'native/shell/app.cpp').read_text())
        self.assertEqual(adb_client.ACTIVITY_CLASS, 'com.nomadic.sc64.Sc64Activity')
        self.assertIn(f'android:name="{adb_client.ACTIVITY_CLASS}"', (ROOT / 'native/android/AndroidManifest.xml').read_text())

    def test_aligned_elf(self):
        self.assertEqual(len(apk_check.elf_segments(elf())), 2)

    def test_rejects_small_page_and_relro(self):
        for data in (elf(4096), elf(relro_end=4096)):
            with self.assertRaises(ValueError): apk_check.elf_segments(data)

    def test_rejects_wrong_machine_and_truncated_headers(self):
        wrong = bytearray(elf()); struct.pack_into('<H', wrong, 18, 62)
        for data in (wrong, elf()[:100], b'not ELF'):
            with self.assertRaises(ValueError): apk_check.elf_segments(data)

    def package(self, root, extra=None, bridge=True, abi='arm64-v8a', engine_name='libmain.so', payload=None):
        apk = root / 'test.apk'
        with ZipFile(apk, 'w', compression=ZIP_DEFLATED) as archive:
            archive.writestr(f'lib/{abi}/{engine_name}', elf() if payload is None else payload)
            if bridge: archive.writestr('classes.dex', b'test dex')
            if extra: archive.writestr(*extra)
        return apk

    def test_package_keeps_device_gate_open(self):
        with tempfile.TemporaryDirectory() as temporary:
            report = apk_check.audit(self.package(Path(temporary)))
            self.assertTrue(report['alignment_16k'])
            self.assertFalse(report['on_device_accepted'])

    def test_rejects_rom_even_with_neutral_filename(self):
        with tempfile.TemporaryDirectory() as temporary:
            for extra in (('asset.z64', b'private'), ('assets/data', b'\x80\x37\x12\x40payload'), ('funcs.c', b'code')):
                with self.assertRaises(ValueError): apk_check.audit(self.package(Path(temporary), extra))

    def test_rejects_saves_and_signing_keys_but_keeps_package_content(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ('assets/slot.sav', 'assets/slot.sra', 'assets/slot.eep', 'assets/pak.mpk',
                         'assets/game.state', 'assets/flash.FLA', 'release.keystore', 'key.jks',
                         'key.p12', 'key.pk8', 'key.pem', 'native.o'):
                with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'Forbidden'):
                    apk_check.audit(self.package(root, (name, b'private')))
            # Members the shell APK carries must stay acceptable.
            for name in ('assets/DejaVuSansMono-Bold.ttf', 'assets/sc64/config.ini', 'res/icon.png', 'assets/data.bin',
                         'META-INF/CERT.RSA'):
                with self.subTest(name=name):
                    self.assertTrue(apk_check.audit(self.package(root, (name, b'content')))['alignment_16k'])

    def test_bundled_rom_requires_explicit_option_and_exact_identity(self):
        # Synthetic image identity is scoped to the fixture; retail data is never
        # checked in. The production constants remain the researched ROM hash.
        data = b'\x80\x37\x12\x40' + bytes(apk_check.ROM_SIZE - 4)
        with tempfile.TemporaryDirectory() as temporary, patch.object(apk_check, 'ROM_SHA1', hashlib.sha1(data).hexdigest()):
            apk = self.package(Path(temporary), (apk_check.ROM_MEMBER, data))
            with self.assertRaisesRegex(ValueError, 'Forbidden'): apk_check.audit(apk)
            self.assertTrue(apk_check.audit(apk, bundled_rom=True)['rom_included'])
            with ZipFile(apk, 'a') as archive: archive.writestr('assets/extra', data)
            with self.assertRaisesRegex(ValueError, 'Forbidden'): apk_check.audit(apk, bundled_rom=True)

    def test_bundled_option_rejects_missing_wrong_size_order_and_hash(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaisesRegex(ValueError, 'lacks the requested'): apk_check.audit(self.package(root), bundled_rom=True)
            for payload in (b'\x80\x37\x12\x40', b'\x37\x80\x40\x12' + bytes(apk_check.ROM_SIZE - 4),
                            b'\x80\x37\x12\x40' + bytes(apk_check.ROM_SIZE - 4)):
                with self.assertRaises(ValueError):
                    apk_check.audit(self.package(root, (apk_check.ROM_MEMBER, payload)), bundled_rom=True)

    def test_rejects_missing_bridge_and_other_abi(self):
        with tempfile.TemporaryDirectory() as temporary:
            for kw in ({'bridge': False}, {'abi': 'x86_64'}):
                with self.assertRaises(ValueError): apk_check.audit(self.package(Path(temporary), **kw))

    def test_rejects_engine_substitution(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); engine = root / 'engine.so'; engine.write_bytes(b'wrong engine')
            with self.assertRaises(ValueError): apk_check.audit(self.package(root), engine)
            engine.write_bytes(elf()); self.assertTrue(apk_check.audit(root / 'test.apk', engine)['alignment_16k'])

    def test_requires_game_engine_even_when_auxiliary_hash_matches(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); engine = root / 'engine.so'; engine.write_bytes(elf())
            apk = self.package(root, engine_name='libvkquality.so')
            for expected in (None, engine):
                with self.assertRaisesRegex(ValueError, 'lacks the game engine'): apk_check.audit(apk, expected)

    def test_hash_must_match_game_engine_not_auxiliary_library(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); engine = root / 'engine.so'; engine.write_bytes(elf())
            wrong = bytearray(elf()); wrong[-1] ^= 1
            apk = self.package(root, payload=wrong, extra=('lib/arm64-v8a/libvkquality.so', elf()))
            with self.assertRaisesRegex(ValueError, 'does not match'): apk_check.audit(apk, engine)

    def native_log(self, after=None):
        a = {'running': True, 'paused': False, 'error': '', 'fields': 100,
             'tasks': 2, 'samples': 40, 'host_seconds': 1, 'audio_nonzero_frames': 10,
             'presented': 2, 'render_driver': 'vulkan'}
        b = dict(a, fields=400, tasks=4, samples=80, host_seconds=6, audio_nonzero_frames=30, presented=4)
        rows = ['SC64 ' + json.dumps(s) for s in (a, b)]
        rows += ['SC64 Android suspended=1 fields=401 tasks=4 samples=80',
                 'SC64 Android suspended=0 fields=401 tasks=4 samples=80']
        if after is None:
            after = [dict(b, fields=460, tasks=5, samples=100, host_seconds=20, audio_nonzero_frames=40, presented=5),
                     dict(b, fields=760, tasks=8, samples=200, host_seconds=25, audio_nonzero_frames=80, presented=8)]
        return '\n'.join(rows + ['SC64 ' + json.dumps(s) for s in after])

    def receipt(self, log, same_process=True):
        return {'native': android_run.analyze(log), 'home_requested': True, 'process_unchanged': same_process,
                'device': {'ro.product.cpu.abi':'arm64-v8a','page_size':16384}}

    def test_native_receipt_does_not_certify_hardware(self):
        receipt = self.receipt(self.native_log())
        android_run.validate_receipt(receipt)
        report = receipt['native']
        self.assertTrue(report['guest_progress'] and report['suspension_freezes_guest'] and report['pcm_nonzero'])
        self.assertTrue(report['resume_accepted'])
        self.assertEqual(report['field_rates'][0],60)
        self.assertEqual(len(report['field_rates']),2) # Home time is not a cadence interval.
        self.assertFalse(report['physical_device_accepted'])

    def test_rejects_missing_or_unmeasured_resume(self):
        one = {'running': True, 'paused': False, 'error': '', 'fields': 500,
               'tasks': 6, 'samples': 100, 'host_seconds': 20, 'audio_nonzero_frames': 50,
               'presented': 6,'render_driver':'vulkan'}
        for after in ([], [one], [dict(one, paused=True), dict(one, paused=True, host_seconds=25)]):
            with self.subTest(after=after):
                receipt = self.receipt(self.native_log(after))
                self.assertTrue(receipt['native']['guest_progress']) # activity before Home is insufficient
                self.assertFalse(receipt['native']['resume_accepted'])
                with self.assertRaisesRegex(ValueError, 'freeze/resume'): android_run.validate_receipt(receipt)

    def test_rejects_stalled_guest_work_or_pcm_after_resume(self):
        good = self.native_log().splitlines()
        first, second = (json.loads(line[5:]) for line in good[-2:])
        for counter in ('fields', 'tasks', 'samples', 'audio_nonzero_frames','presented'):
            stalled = dict(second, **{counter: first[counter]})
            with self.subTest(counter=counter), self.assertRaisesRegex(ValueError, 'freeze/resume'):
                android_run.validate_receipt(self.receipt(self.native_log([first, stalled])))

    def test_rejects_exit_or_process_replacement(self):
        for final_pid in ('', 'replacement'):
            receipt = self.receipt(self.native_log(), same_process=False)
            receipt['final_pid'] = final_pid
            with self.assertRaisesRegex(ValueError, 'changed process'): android_run.validate_receipt(receipt)

    def test_rejects_pause_after_measured_resume(self):
        log = self.native_log() + '\nSC64 ' + json.dumps({'running': True, 'paused': True, 'fields': 760, 'error': ''})
        with self.assertRaisesRegex(ValueError, 'freeze/resume'): android_run.validate_receipt(self.receipt(log))

    def test_background_progress_and_crash_fail_native_check(self):
        log='SC64 Android suspended=1 fields=100 tasks=4 samples=80\nSC64 Android suspended=0 fields=300 tasks=8 samples=800\nFatal signal 11'
        report=android_run.analyze(log)
        self.assertFalse(report['guest_progress'] or report['suspension_freezes_guest'])
        self.assertTrue(report['android_crash'])

    def test_page_size_and_abi_are_runtime_requirements(self):
        receipt=self.receipt(self.native_log())
        receipt['required_page_size']=16384
        android_run.validate_receipt(receipt)
        for device in ({'ro.product.cpu.abi':'arm64-v8a','page_size':4096},
                       {'ro.product.cpu.abi':'x86_64','page_size':16384}):
            with self.subTest(device=device),self.assertRaisesRegex(ValueError,'page size|arm64'):
                android_run.validate_profile(device,16384)
        receipt['device']['page_size']=4096
        with self.assertRaisesRegex(ValueError,'page size'): android_run.validate_receipt(receipt)

    def test_backend_requires_observed_adapter(self):
        receipt=self.receipt(self.native_log());receipt['requested_backend']='vulkan'
        android_run.validate_receipt(receipt)
        for drivers in ([],['opengles2'],['opengles2','vulkan']):
            receipt['native']['render_drivers']=drivers
            with self.subTest(drivers=drivers),self.assertRaisesRegex(ValueError,'graphics adapter'):
                android_run.validate_receipt(receipt)

    def test_backend_uses_activity_argument_extra(self):
        plain=['shell','am','start','-n',android_run.ACTIVITY]
        self.assertEqual(android_run.activity_command(None),plain)
        for backend,driver in (('opengles','opengles2'),('vulkan','vulkan')):
            self.assertEqual(android_run.activity_command(backend),plain+[
                '--esa','com.nomadic.sc64.ARGUMENTS',f'--config=sc64.render_driver={driver}'])
        with self.assertRaises(ValueError): android_run.activity_command('guessed-backend')

    def test_live_guest_without_presented_frames_fails(self):
        receipt=self.receipt(self.native_log());receipt['home_requested']=False
        receipt['native']['presentation_progress']=False
        with self.assertRaisesRegex(ValueError,'presentation'): android_run.validate_receipt(receipt)

    def test_wrong_page_size_fails_before_install_or_restart(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();output=root/'local/probe'
            def response(command,**kwargs):
                if command[-2:] == ['getconf','PAGE_SIZE']: return '4096'
                if command[-2:] == ['getprop','ro.product.cpu.abi']: return 'arm64-v8a'
                if 'getprop' in command: return 'synthetic'
                self.fail(f'Unexpected device mutation: {command}')
            with patch.object(android_run,'ROOT',root),patch.object(apk_check,'audit',return_value={}), \
                 patch.object(android_run.subprocess,'check_output',side_effect=response), \
                 patch.object(sys,'argv',['probe','--serial','fixture','--output',str(output),
                    '--install','--required-page-size','16384']):
                with self.assertRaisesRegex(ValueError,'page size'): android_run.main()

    def test_pidof_output_with_several_processes_is_rejected(self):
        self.assertEqual(android_run.single_pid(''), None)
        self.assertEqual(android_run.single_pid('4242\n'), 4242)
        self.assertEqual(android_run.parse_pids('11 12'), (11, 12))
        with self.assertRaisesRegex(ValueError, 'Several'): android_run.single_pid('11 12')
        with self.assertRaisesRegex(ValueError, 'Unexpected pidof'): android_run.parse_pids('error: device')

    def test_smoke_stops_on_several_processes_and_uses_transfer_timeout(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary).resolve();output=root/'local/probe'
            apk=root/'fixture.apk';apk.write_bytes(b'synthetic apk')
            digest=hashlib.sha256(b'synthetic apk').hexdigest()
            timeouts={}
            def response(command,**kwargs):
                timeouts[command[3]]=kwargs['timeout']
                if command[-2:] == ['getconf','PAGE_SIZE']: return '4096'
                if command[-2:] == ['getprop','ro.product.cpu.abi']: return 'arm64-v8a'
                if 'getprop' in command: return 'synthetic'
                if command[3] == 'pm': return 'package:/data/app/base.apk'
                if command[3] == 'pull': Path(command[-1]).write_bytes(b'synthetic apk'); return ''
                if command[-2:] == ['pidof',android_run.PACKAGE]: return '101 102'
                return ''
            with patch.object(android_run,'ROOT',root),patch.object(apk_check,'audit',return_value={'apk_sha256':digest}), \
                 patch.object(android_run.subprocess,'check_output',side_effect=response), \
                 patch.object(sys,'argv',['probe','--serial','fixture','--output',str(output),
                    '--apk',str(apk),'--install']):
                with self.assertRaisesRegex(ValueError,'Several'): android_run.main()
            self.assertEqual(timeouts['install'],android_run.ADB_TRANSFER_TIMEOUT)
            self.assertEqual(timeouts['pull'],android_run.ADB_TRANSFER_TIMEOUT)
            self.assertEqual(timeouts['shell'],60)


sys.path.insert(0, str(ROOT / 'tools/desktop'))
from game_validation import validate_ui_hooks

class GameObserverValidationTests(unittest.TestCase):
    def source(self):
        # Authored fixture representing a complete set across generated files.
        return '\n'.join(['sc64_ui_font(rdram, ctx, 0);'] * 8 +
                         ['sc64_ui_briefing(rdram, ctx, 1);'] * 2 +
                         ['sc64_ui_blit(rdram, ctx);', '// 0x801296C8: nop', 'sc64_ui_pick(rdram, ctx, 0);', 'sc64_ui_pick(rdram, ctx, 1);'] +
                         [f'sc64_ui_hud_{kind}(rdram, ctx);' for kind in ('begin', 'end', 'present')] +
                         [f'sc64_ui_hud_font(rdram, ctx, {kind});' for kind in range(3)] +
                         [f'sc64_ui_main(rdram, ctx, {kind});' for kind in range(4)])

    def test_complete_and_empty_generated_trees(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            with self.assertRaises(ValueError): validate_ui_hooks(root)
            lines = self.source().splitlines()
            (root / 'funcs_0.c').write_text('\n'.join(lines[:7]))
            (root / 'funcs_1.c').write_text('\n'.join(lines[7:]))
            validate_ui_hooks(root)

    def test_missing_each_observer_or_wrap_and_duplicate_hook(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); file = root / 'funcs_0.c'
            lines = self.source().splitlines()
            for line in set(lines):
                broken = lines.copy(); broken.remove(line)
                file.write_text('\n'.join(broken))
                with self.subTest(line=line), self.assertRaises(ValueError): validate_ui_hooks(root)
            file.write_text(self.source() + '\nsc64_ui_main(rdram, ctx, 0);')
            with self.assertRaises(ValueError): validate_ui_hooks(root)

    def test_both_builders_reject_stale_output_before_configuring(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            generated = root / 'local/generated/game'; generated.mkdir(parents=True)
            (generated / 'recomp_overlays.inl').write_text('synthetic marker')
            (generated / 'funcs_0.c').write_text('void unhooked_game(void) {}')
            for stage in ('desktop', 'android'):
                spec = importlib.util.spec_from_file_location('review_build_' + stage, ROOT / 'tools' / stage / 'build.py')
                builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(builder)
                with patch.object(builder, 'ROOT', root), patch.object(sys, 'argv', ['build', '--configure-only']), \
                     patch('platform.system', return_value='Darwin'), patch('platform.machine', return_value='arm64'), \
                     patch.object(builder, 'run') as run:
                    with self.assertRaisesRegex(ValueError, 'observer/patch mismatch'): builder.main()
                    run.assert_not_called()

if __name__ == '__main__': unittest.main()
