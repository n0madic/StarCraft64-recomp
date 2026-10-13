#!/usr/bin/env python3
"""Cross-platform entry point of the build pipeline (ADR-082).

Runs the same commands on macOS, Linux and Windows (``python tools/sc64.py
<command>``); the Makefile is a thin wrapper over it. ``build`` makes the package
for the machine it runs on, the ``build-*`` commands name a target explicitly.
Option defaults come from the environment variables the Makefile exports (ROM,
BUNDLE_ROM, JOBS, BUILD_TYPE, STORAGE, REPORT, RUN_SECONDS).
"""
from __future__ import annotations

import argparse
import os
import subprocess
import re
import sys
import time
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools'
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / 'desktop'))
sys.path.insert(0, str(TOOLS / 'android'))
import adb_client
import hostenv
import pins
from hostenv import Host

BUILD_TYPES = ('Release', 'RelWithDebInfo', 'Debug')
# CTest targets of the desktop tree (tests/native); `test-native` builds exactly these.
NATIVE_TEST_TARGETS = (
    'sc64_desktop_bridge_test', 'sc64_flash_test', 'sc64_vi_test', 'sc64_ui_observers_test',
    'sc64_guest_fpu_test', 'sc64_gfx_test', 'sc64_rom_import_test', 'sc64_sha_test',
    'sc64_desktop_input_test', 'sc64_touch_input_test', 'sc64_gestures_test', 'sc64_map_taps_test',
    'sc64_hud_input_test', 'sc64_hud_buttons_test', 'sc64_menu_input_test', 'sc64_haptics_test',
    'sc64_host_telemetry_test', 'sc64_loop_watch_test', 'sc64_game_module_test')

# Pins of the recompilation environment (the splat research workflow has its own, see
# docs/research-workflow.md): `recompile_all.py` needs spimdisasm and its disassembler;
# the libultra inventory step (`inventory`) reads reference objects with pyelftools.
VENV_PACKAGES = ('spimdisasm==1.42.4', 'rabbitizer==1.16.2', 'pyelftools==0.33')
RENDERER_BUILD = ROOT / 'local/build/renderer'
INVENTORY = ROOT / 'local/reports/libultra-resident/inventory.json'
RECOMPILER_TARGETS = ('macos-arm64', 'linux-x86_64', 'windows-x86_64')  # tools/recompile/build_recompiler.py
EMULATOR = ROOT / 'local/toolchains/ares-build/test-runner/ares-test'
RENDER_DRIVERS = ('vulkan', 'opengles2')  # tools/android/build.py RENDER_DRIVERS
EMULATOR_DRIVER_LINE = re.compile(rb'^sc64\.render_driver = opengles2$', re.MULTILINE)
ACTIVITY = adb_client.PACKAGE + '/com.nomadic.sc64.Sc64Activity'

Command = list[str]


class UsageError(SystemExit):
    """The request cannot be satisfied; the message says what to change."""


def script(path: str, *args: str | Path) -> Command:
    return [sys.executable, str(TOOLS / path), *map(str, args)]


def env_flag(name: str) -> bool:
    value = os.environ.get(name, '0')
    if value not in ('0', '1'):
        raise UsageError(f'{name} must be 0 or 1')
    return value == '1'


def require_rom(rom: Path | None, why: str) -> Path:
    if rom is None:
        raise UsageError(f'{why} requires --rom (or ROM=/path/game.z64)')
    return rom


def build_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    """``build``: the package that runs on this host."""
    common = ('--build-type', args.build_type)
    if host.system == 'darwin':
        return build_macos_commands(host, args)
    if host.system == 'windows':
        return target_commands('windows', host, args)
    if host.linux_x86_64:
        return target_commands('linux', host, args)
    raise UsageError(f'No native package profile for {host.label}. `build-linux` makes the x86_64 Linux '
                     'package in Docker, `build-windows` the Windows one with mingw-w64.')


def build_macos_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    """``build-macos``: the app bundle, which only a macOS arm64 host can make."""
    if (host.system, host.arch) != ('darwin', 'arm64'):
        raise UsageError(f'build-macos needs a macOS arm64 host (this is {host.label}); '
                         '`build` makes the package of this machine.')
    return [script('desktop/build.py', '--build-type', args.build_type, '--jobs', args.jobs)]


def target_commands(target: str, host: Host, args: argparse.Namespace) -> list[Command]:
    extra = ['--docker'] if getattr(args, 'docker', False) else []
    return [script('desktop/cross_build.py', target, '--jobs', args.jobs, '--build-type', args.build_type, *extra)]


def appimage_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    extra = ['--docker'] if args.docker else []
    commands = target_commands('linux', host, args)
    options = []
    if args.bundle_rom:
        options = ['--bundle-rom', require_rom(args.rom, 'BUNDLE_ROM=1')]
    return commands + [script('desktop/appimage.py', *options, *extra)]


def run_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    options = ['--rom', args.rom] if args.rom else []
    report = args.report or ROOT / 'local/reports' / time.strftime('make-desktop-%Y%m%d-%H%M%S')
    return [script('desktop/run.py', *options, '--storage', args.storage, '--seconds', args.seconds,
                   '--output', report)]


def test_native_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    build = ROOT / 'local/build/desktop'
    return [script('desktop/build.py', '--build-type', args.build_type, '--configure-only'),
            ['cmake', '--build', str(build), '--target', *NATIVE_TEST_TARGETS, '-j', str(args.jobs)],
            ['ctest', '--test-dir', str(build), '--output-on-failure']]


def apk_path(args: argparse.Namespace) -> Path:
    if args.apk:
        return args.apk
    return ROOT / 'local/dist' / f'StarCraft64-android{"-opengles2" if args.render_driver == "opengles2" else ""}.apk'


def require_serial(args: argparse.Namespace) -> str:
    if not args.serial:
        raise UsageError('Set --serial (or SERIAL=) to an explicit adb device ID; see `devices`.')
    return args.serial


def adb(args: argparse.Namespace, *words: str | Path) -> Command:
    return [args.adb, '-s', require_serial(args), *map(str, words)]


def build_android_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    options: list[str | Path] = []
    if args.sdk:
        options += ['--sdk', args.sdk]
    if args.bundle_rom:
        options += ['--bundle-rom', require_rom(args.rom, 'BUNDLE_ROM=1')]
    if args.render_driver:
        options += ['--render-driver', args.render_driver]
    if args.debuggable:
        options.append('--debuggable')
    return [script('android/build.py', *options, '--jobs', args.jobs, '--build-type', args.build_type)]


def configure_android_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    options = ['--sdk', args.sdk] if args.sdk else []
    return [script('android/build.py', *options, '--jobs', args.jobs, '--build-type', args.build_type, '--configure-only')]


def check_apk_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    return [script('android/check_apk.py', apk_path(args), *(['--bundled-rom'] if args.bundle_rom else []))]


def install_android(host: Host, args: argparse.Namespace) -> int:
    apk = apk_path(args)
    require_serial(args)
    if status := execute(check_apk_commands(host, args)):
        return status
    if args.serial.startswith('emulator-'):
        with ZipFile(apk) as package:
            config = package.read('assets/sc64/config.ini')
        if not EMULATOR_DRIVER_LINE.search(config):
            raise UsageError('The Vulkan APK crashes on emulators (D-053): `build-android-opengles`, then install '
                             'with --render-driver opengles2.')
    return execute([adb(args, 'install', '-r', apk)])


def run_android_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    options = []
    if args.render_driver:
        options = ['--esa', 'com.nomadic.sc64.ARGUMENTS', f'--config=sc64.render_driver={args.render_driver}']
    return [adb(args, 'shell', 'am', 'start', '-n', ACTIVITY, *options)]


def smoke_android_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    options: list[str | Path] = []
    if args.bundle_rom:
        options.append('--bundled-rom')
    if args.page_size:
        options += ['--required-page-size', args.page_size]
    if args.backend:
        options += ['--backend', args.backend]
    report = args.report or ROOT / 'local/reports' / time.strftime('make-android-%Y%m%d-%H%M%S')
    return [script('android/run.py', *options, '--adb', args.adb, '--serial', require_serial(args), '--apk', apk_path(args),
                   '--seconds', args.smoke_seconds, '--home', '--output', report)]


def audio_android_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    report = args.report or ROOT / 'local/reports' / time.strftime('make-audio-%Y%m%d-%H%M%S')
    return [script('android/audio_report.py', '--adb', args.adb, '--serial', require_serial(args), '--output', report)]


def recompile_python(host: Host, args: argparse.Namespace) -> Path:
    python = args.recomp_python or hostenv.venv_python(ROOT, host)
    if not python.is_file():
        raise UsageError(f'{python} not found: create the recompilation environment with '
                         '`python tools/sc64.py venv` (or pass --recomp-python)')
    return python


def recompile_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    rom = require_rom(args.rom, 'recompile')
    return [[str(recompile_python(host, args)), str(TOOLS / 'recompile/recompile_all.py'), str(rom), '--jobs', str(args.jobs)]]


def inventory_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    rom = require_rom(args.rom, 'inventory')
    dump = ['--dump', str(args.dump)] if args.dump else []
    return [[str(recompile_python(host, args)), str(TOOLS / 'research_libultra/ensure_inventory.py'), str(rom), *dump]]


def recipe_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    """`recipe-verify`: N64Recomp + the committed recipe, compared with its hashes (stdlib Python)."""
    return [script('recompile/recipe.py', 'check'), script('recompile/recipe.py', 'generate', require_rom(args.rom, 'recipe-verify'))]


def build_recompiler_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    target = ['--target', args.target] if args.target else []
    return [script('recompile/build_recompiler.py', 'build', *target, '--jobs', str(args.jobs), *(['--docker'] if args.docker else []))]


def verify_recompiler_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    target = ['--target', args.target] if args.target else []
    return [script('recompile/build_recompiler.py', 'verify', require_rom(args.rom, 'verify-recompiler'), *target)]


def recipe_export_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    if not args.analysis:
        raise UsageError('recipe-export needs the analysis directory of a `make recompile` run: '
                         '--analysis local/recompile/symbols-<timestamp> (or ANALYSIS=...)')
    return [script('recompile/recipe.py', 'export', '--analysis', args.analysis), script('recompile/recipe.py', 'check')]


def venv_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    python = hostenv.venv_python(ROOT, host)
    create = [] if python.is_file() else [[sys.executable, '-m', 'venv', str(ROOT / '.venv')]]
    return create + [[str(python), '-m', 'pip', 'install', *VENV_PACKAGES]]


def benchmark_commands(host: Host, args: argparse.Namespace) -> list[Command]:
    if args.baseline_replay is None:
        raise UsageError('benchmark-renderer requires --baseline-replay (or BASELINE_REPLAY=) '
                         'pointing at a preserved baseline replay executable')
    report = args.report or ROOT / 'local/reports' / time.strftime('make-renderer-benchmark-%Y%m%d-%H%M%S')
    candidate = RENDERER_BUILD / 'native/gfx_core' / ('sc64_gfx_replay' + host.exe_suffix)
    return [['cmake', '-S', str(ROOT), '-B', str(RENDERER_BUILD), '-G', 'Ninja', '-DSC64_BUILD_HOST=OFF',
             '-DCMAKE_BUILD_TYPE=RelWithDebInfo'],
            ['cmake', '--build', str(RENDERER_BUILD), '--target', 'sc64_gfx_replay', '-j', str(args.jobs)],
            script('renderer/benchmark.py', args.renderer_manifest, report, '--baseline', args.baseline_replay,
                   '--candidate', candidate)]


def recompile_requirements(host: Host) -> list[hostenv.Check]:
    python = hostenv.venv_python(ROOT, host)
    spim = None
    if python.is_file():
        probe = subprocess.run([str(python), '-c', 'import spimdisasm'], capture_output=True)
        spim = str(python) if probe.returncode == 0 else None
    source = ROOT / 'third_party/N64Recomp/CMakeLists.txt'
    elftools = None
    if python.is_file():
        elftools = str(python) if subprocess.run([str(python), '-c', 'import elftools'], capture_output=True).returncode == 0 else None
    references = ROOT / 'third_party/ultralib/build'
    return [hostenv.Check('project .venv with spimdisasm', spim, '`python tools/sc64.py venv`'),
            hostenv.Check('project .venv with pyelftools (libultra inventory)', elftools, '`python tools/sc64.py venv`'),
            hostenv.Check('third_party/N64Recomp checkout', str(source.parent) if source.is_file() else None,
                          'git submodule update --init --recursive third_party/N64Recomp'),
            hostenv.Check('libultra inventory (or what generates it, below)',
                          str(INVENTORY) if INVENTORY.is_file() else None, '`python tools/sc64.py inventory`'),
            hostenv.Check('  research emulator (ares-test)', str(EMULATOR) if EMULATOR.is_file() else None,
                          'build it per docs/reference-capture.md sections 1-2'),
            hostenv.Check('  reference libultra objects', str(references) if all(
                any((references / version).glob('lib*ultra*/src/**/*.o')) for version in 'IJKL') else None,
                          'build them per tools/research_libultra/README.md')]


def doctor(host: Host, args: argparse.Namespace) -> int:
    """Report which tools this host has for each target. Exit 1 if `build` cannot work."""
    groups = [('common', hostenv.common_requirements(host)),
              (f'build ({host.label} package)', hostenv.desktop_requirements(host))]
    if host.system != 'windows':
        groups.append(('build-windows (mingw-w64 cross compiler)', hostenv.windows_requirements(host)))
    groups.append(('build-linux', hostenv.linux_requirements(host)))
    groups.append(('recompile (ROM -> local/generated/game)', recompile_requirements(host)))
    groups.append(('build-android (arm64-v8a APK)', hostenv.android_requirements(host, pins.SDK_REVISION, pins.BUILD_TOOLS)))
    failed = False
    print(f'Host: {host.label}')
    for index, (title, checks) in enumerate(groups):
        print(f'\n{title}')
        for check in checks:
            print(f'  [{"ok" if check.ok else "--"}] {check.name}' + (f' -> {check.path}' if check.ok else f'   ({check.hint})'))
        if index < 2 and not all(check.ok for check in checks):
            failed = True
    print('\nOnly the common and build groups are required for `build`; the others matter for their own target.')
    return 1 if failed else 0


def execute(commands: list[Command]) -> int:
    for command in commands:
        status = subprocess.run([str(word) for word in command], cwd=ROOT).returncode
        if status:
            return status
    return 0


def parser() -> argparse.ArgumentParser:
    main = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = main.add_subparsers(dest='command', required=True, metavar='command')
    options = argparse.ArgumentParser(add_help=False)
    options.add_argument('--jobs', type=int, default=int(os.environ.get('JOBS', '8')))
    options.add_argument('--build-type', choices=BUILD_TYPES, default=os.environ.get('BUILD_TYPE', BUILD_TYPES[0]),
                         help='CMake build type (default: Release)')
    options.add_argument('--rom', type=Path, default=Path(os.environ['ROM']) if os.environ.get('ROM') else None,
                         help='your NTSC-U dump (default: $ROM)')
    options.add_argument('--docker', action='store_true',
                         help='Linux package: use the pinned Docker image even on a Linux x86_64 host')
    options.add_argument('--bundle-rom', action='store_true', default=env_flag('BUNDLE_ROM'),
                         help='pack --rom into the AppImage/APK (personal use only; default: $BUNDLE_ROM=1)')
    options.add_argument('--report', type=Path, default=Path(os.environ['REPORT']) if os.environ.get('REPORT') else None,
                         help='new report directory under local/ (default: timestamped; default: $REPORT)')
    options.add_argument('--dump', type=Path, default=None,
                         help='inventory: use this canonical 8 MiB RDRAM dump instead of capturing one')
    options.add_argument('--recomp-python', type=Path,
                         default=Path(os.environ['RECOMP_PYTHON']) if os.environ.get('RECOMP_PYTHON') else None,
                         help='Python with spimdisasm for `recompile` (default: the project .venv, $RECOMP_PYTHON)')
    options.add_argument('--target', choices=RECOMPILER_TARGETS, default=os.environ.get('RECOMPILER_TARGET') or None,
                         help='build-recompiler/verify-recompiler: release target (default: this host; $RECOMPILER_TARGET)')
    options.add_argument('--analysis', type=Path, default=Path(os.environ['ANALYSIS']) if os.environ.get('ANALYSIS') else None,
                         help='recipe-export: analysis directory of a `recompile` run (default: $ANALYSIS)')
    options.add_argument('--baseline-replay', type=Path,
                         default=Path(os.environ['BASELINE_REPLAY']) if os.environ.get('BASELINE_REPLAY') else None,
                         help='benchmark-renderer: preserved baseline replay executable (default: $BASELINE_REPLAY)')
    options.add_argument('--renderer-manifest', type=Path,
                         default=Path(os.environ.get('RENDERER_MANIFEST') or ROOT / 'local/inputs/renderer/manifest.json'))
    android = argparse.ArgumentParser(add_help=False)
    android.add_argument('--sdk', type=Path, default=Path(os.environ['ANDROID_SDK']) if os.environ.get('ANDROID_SDK') else None,
                         help='Android SDK root (default: $ANDROID_SDK, ANDROID_HOME, ANDROID_SDK_ROOT, IDE location)')
    android.add_argument('--serial', default=os.environ.get('SERIAL') or None, help='explicit adb device ID (default: $SERIAL)')
    android.add_argument('--adb', default=os.environ.get('ADB') or 'adb', help='adb executable (default: $ADB or PATH)')
    android.add_argument('--render-driver', choices=RENDER_DRIVERS, default=os.environ.get('ANDROID_RENDER_DRIVER') or None,
                         help='SDL renderer; opengles2 selects the emulator APK (D-053)')
    android.add_argument('--debuggable', action='store_true', default=env_flag('ANDROID_DEBUGGABLE'),
                         help='package android:debuggable (run-as, debugger, `adb install -r -d` rollback)')
    android.add_argument('--apk', type=Path, default=Path(os.environ['APK']) if os.environ.get('APK') else None,
                         help='APK path (default: local/dist/StarCraft64-android[-opengles2].apk)')
    android.add_argument('--page-size', type=int, choices=(0, 4096, 16384), default=int(os.environ.get('PAGE_SIZE') or 0),
                         help='smoke: required device page size (0 = record only)')
    android.add_argument('--backend', choices=('opengles', 'vulkan'), default=os.environ.get('ANDROID_BACKEND') or None,
                         help='smoke: verify this graphics backend')
    android.add_argument('--smoke-seconds', default=os.environ.get('SMOKE_SECONDS') or '30')

    def add(name: str, help_: str, handler, *extra: argparse.ArgumentParser) -> argparse.ArgumentParser:
        sub = commands.add_parser(name, help=help_, parents=[options, *extra])
        sub.set_defaults(handler=handler)
        return sub

    add('doctor', 'check the tools this host has for each target', lambda h, a: doctor(h, a))
    add('venv', 'create .venv with the pinned recompilation packages', venv_commands)
    add('inventory', 'generate local/reports/libultra-resident/inventory.json (also run by recompile when missing)',
        inventory_commands)
    add('recompile', 'ROM -> local/generated/game (N64Recomp, analysis, observers, validation)', recompile_commands)
    add('recipe-verify', 'check recipe/ntsc-u and regenerate the game C from it; compare with its hashes (ADR-084)',
        recipe_commands)
    add('build-recompiler', 'build the patched N64Recomp release binary into local/dist/recompiler (ADR-085)',
        build_recompiler_commands)
    add('verify-recompiler', 'run that binary on the recipe and compare every generated hash', verify_recompiler_commands)
    add('recipe-export', 'refresh recipe/ntsc-u from a recompile analysis (maintainers)', recipe_export_commands)
    add('benchmark-renderer', 'compare private renderer timings and exact output against a baseline',
        benchmark_commands)
    add('bootstrap', 'fetch pinned public dependencies into local/toolchains',
        lambda h, a: [script('desktop/bootstrap.py')])
    add('icon', 'convert the original title art into local/icons',
        lambda h, a: [script('desktop/icon.py', require_rom(a.rom, 'icon'))])
    add('build', 'build the package for this host (macOS app, Linux or Windows package)', build_commands)
    add('build-macos', 'macOS arm64 app bundle (macOS hosts only)', build_macos_commands)
    add('configure', 'configure the host-compiler desktop tree without building',
        lambda h, a: [script('desktop/build.py', '--build-type', a.build_type, '--configure-only')])
    add('build-windows', 'Windows x86_64 zip (mingw-w64)', lambda h, a: target_commands('windows', h, a))
    add('build-linux', 'Linux x86_64 tar.gz (host compiler on Linux x86_64, else Docker)',
        lambda h, a: target_commands('linux', h, a))
    appimage = add('build-appimage', 'Linux x86_64 AppImage from a fresh Linux build', appimage_commands)
    run = add('run', 'launch the existing desktop build for this host', run_commands)
    run.add_argument('--storage', type=Path, default=Path(os.environ.get('STORAGE', ROOT / 'local/private/desktop/default')))
    run.add_argument('--seconds', default=os.environ.get('RUN_SECONDS', '0'), help='0 = interactive')
    add('build-android', 'arm64-v8a APK (Vulkan default, bundled ROM with --bundle-rom)', build_android_commands, android)
    # Not set_defaults: parent parsers share their Action objects, so that would change every command.
    add('build-android-opengles', 'APK with OpenGL ES for emulators',
        lambda h, a: build_android_commands(h, argparse.Namespace(**{**vars(a), 'render_driver': 'opengles2'})), android)
    add('configure-android', 'configure the NDK tree without packaging', configure_android_commands, android)
    add('check-apk', 'audit APK alignment and the private-data boundary', check_apk_commands, android)
    add('devices', 'list adb devices', lambda h, a: [[a.adb, 'devices', '-l']], android)
    add('install-android', 'install the audited APK on --serial', install_android, android)
    add('run-android', 'open the installed Android app on --serial', run_android_commands, android)
    add('smoke-android', 'restart the app and probe Home/return', smoke_android_commands, android)
    add('audio-android', 'record 60 seconds of the running app audio counters', audio_android_commands, android)
    add('test-python', 'synthetic Python suite',
        lambda h, a: [[sys.executable, '-m', 'unittest', 'discover', '-s', 'tests']])
    add('test-native', 'configure the desktop tree, build the fixtures and run CTest', test_native_commands)
    return main


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    host = hostenv.current()
    result = args.handler(host, args)
    return result if isinstance(result, int) else execute(result)


if __name__ == '__main__':
    sys.exit(main())
