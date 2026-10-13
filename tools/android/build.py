#!/usr/bin/env python3
"""Locally link and package a personal arm64 Android APK of the SDL3 shell."""
from __future__ import annotations
import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import bootstrap
import patch_shell_runtime
from game_validation import validate_ui_hooks
import check_apk
from icon import installed as installed_icon
sys.path.insert(0, str(ROOT / 'tools'))
import hostenv
from rom_info import normalize_rom
sys.path.insert(0, str(ROOT / 'tools/android'))
from pins import API, BUILD_TOOLS, SDK_REVISION, TARGET_API

# Above every installed build of the former engine package (versionCode 1).
VERSION_CODE = 2
VERSION_NAME = '2.0'
RUNTIME = ROOT / 'local/deps/N64ModernRuntime-shell'
SDL_JAVA = bootstrap.SDL_DIR / 'android-project/app/src/main/java'
# Vulkan is the packaged default (verified on the physical phone). SDL 3.4.18's
# Vulkan renderer crashes on surface loss after Home/return on the Android
# emulator (D-053), so emulator runs use the OpenGL ES variant: a separate file
# and receipt, same package and key.
DEFAULT_RENDER_DRIVER = 'vulkan'
RENDER_DRIVERS = (DEFAULT_RENDER_DRIVER, 'opengles2')
# SDL tries a comma-separated list in order, so a device without a working
# Vulkan driver falls back to OpenGL ES instead of failing at startup. Explicit
# requests (`run.py --backend`, intent arguments) name a single driver and stay strict.
RENDER_DRIVER_CHAINS = {'vulkan': 'vulkan,opengles2', 'opengles2': 'opengles2'}

def output_paths(render_driver: str, root: Path = ROOT) -> tuple[Path, Path]:
    """APK and receipt paths; a non-default driver never replaces the default artifacts."""
    suffix = '' if render_driver == DEFAULT_RENDER_DRIVER else f'-{render_driver}'
    return (root / f'local/dist/StarCraft64-android{suffix}.apk',
            root / f'local/reports/android/build{suffix}.json')

def config_text(render_driver: str, bundled_rom: bool) -> str:
    return ('sc64.compact_telemetry = 1\n'
            f'sc64.render_driver = {RENDER_DRIVER_CHAINS[render_driver]}\n'
            f'sc64.bundled_rom = {int(bundled_rom)}\n')

def run(args: list[str | Path], **kwargs) -> None:
    subprocess.run([str(item) for item in args], check=True, **kwargs)

def digest(path: Path) -> str:
    return bootstrap.digest(path)

def write_icon(path: Path) -> None:
    """Launcher icon: the generated original artwork (ADR-076), else the authored fallback
    with a navy plate, gold frame and the overlay font."""
    path.parent.mkdir(parents=True, exist_ok=True)
    if generated := installed_icon('icon-android.png'):
        shutil.copyfile(generated, path)
        return
    from PIL import Image, ImageDraw, ImageFont
    size = 192
    image = Image.new('RGBA', (size, size), (8, 10, 31, 255))
    draw = ImageDraw.Draw(image)
    draw.rectangle((8, 8, size - 9, size - 9), outline=(204, 168, 77, 255), width=6)
    font = ImageFont.truetype(str(bootstrap.FONT), 54)
    draw.text((size / 2, size / 2), 'SC64', font=font, fill=(209, 209, 255, 255), anchor='mm')
    image.save(path)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, help='Android SDK root (default: ANDROID_SDK, ANDROID_HOME, ANDROID_SDK_ROOT or the IDE location)')
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--build-type', choices=bootstrap.BUILD_TYPES, default=bootstrap.DEFAULT_BUILD_TYPE,
                        help='CMake build type (default: %(default)s)')
    parser.add_argument('--render-driver', choices=RENDER_DRIVERS, default=DEFAULT_RENDER_DRIVER,
                        help='SDL renderer written into assets/sc64/config.ini (non-default drivers get a separate APK)')
    parser.add_argument('--debuggable', action='store_true',
                        help='Package android:debuggable (adb run-as access to app data, debugger attach); off by default')
    parser.add_argument('--bundle-rom', type=Path, help='Include your verified ROM in this personal APK and autostart from assets')
    args = parser.parse_args()
    bundled_rom = None
    if args.bundle_rom:
        if args.bundle_rom.stat().st_size != check_apk.ROM_SIZE:
            raise ValueError('Expected a 32 MiB ROM dump')
        _, bundled_rom = normalize_rom(args.bundle_rom.read_bytes())
        check_apk.validate_bundled_rom(bundled_rom)
    validate_ui_hooks(ROOT / 'local/generated/game')
    host = hostenv.current()
    android_sdk = args.sdk.resolve() if args.sdk else hostenv.android_sdk(host)
    if android_sdk is None:
        raise SystemExit('Android SDK not found: pass --sdk or set ANDROID_SDK (see `sc64.py doctor`)')
    jdk = hostenv.jdk_home(host)
    if jdk is None:
        raise SystemExit(f'JDK {hostenv.MIN_JDK}+ not found: set JAVA_HOME (see `sc64.py doctor`)')
    ndk = android_sdk / 'ndk' / SDK_REVISION
    properties = ndk / 'source.properties'
    if not properties.is_file():
        raise SystemExit(f'NDK {SDK_REVISION} not found at {ndk}: sdkmanager "ndk;{SDK_REVISION}"')
    if f'Pkg.Revision = {SDK_REVISION}' not in properties.read_text():
        raise ValueError('NDK pin mismatch')
    ndk_bin = ndk / 'toolchains/llvm/prebuilt' / host.ndk_tag / 'bin'
    build_tools = android_sdk / 'build-tools' / BUILD_TOOLS
    if not build_tools.is_dir():
        raise SystemExit(f'Android build-tools {BUILD_TOOLS} not found: sdkmanager "build-tools;{BUILD_TOOLS}"')
    android_jar = android_sdk / f'platforms/android-{TARGET_API}/android.jar'
    if not android_jar.is_file():
        raise SystemExit(f'Android platform {TARGET_API} not found: sdkmanager "platforms;android-{TARGET_API}"')
    exe, bat = host.exe_suffix, host.script_suffix
    bootstrap.main()
    patch_shell_runtime.prepare(ROOT / 'local/deps/N64ModernRuntime', RUNTIME)
    build = ROOT / 'local/build/android'
    run(['cmake', '-S', ROOT / 'native/android', '-B', build, '-G', 'Ninja',
         f'-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake',
         '-DANDROID_ABI=arm64-v8a', f'-DANDROID_PLATFORM=android-{API}', '-DANDROID_STL=c++_static',
         f'-DCMAKE_BUILD_TYPE={args.build_type}'])
    if args.configure_only: return
    run(['cmake', '--build', build, '--target', 'sc64_app', '-j', str(args.jobs)])
    java_env = dict(os.environ, JAVA_HOME=str(jdk))
    target, receipt = output_paths(args.render_driver, ROOT)
    target.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.sc64-android-', dir=target.parent) as temporary:
        staging = Path(temporary)
        library = staging / 'libmain.so'
        shutil.copy2(build / 'libmain.so', library)
        run([ndk_bin / ('llvm-strip' + exe), '--strip-debug', library])
        # Manifest and launcher icon.
        write_icon(staging / 'res/mipmap-xxxhdpi/icon.png')
        run([build_tools / ('aapt2' + exe), 'compile', '--dir', staging / 'res', '-o', staging / 'res.zip'])
        base = staging / 'base.apk'
        run([build_tools / ('aapt2' + exe), 'link', '-o', base, '-I', android_jar,
             '--manifest', ROOT / 'native/android/AndroidManifest.xml',
             '--min-sdk-version', str(API), '--target-sdk-version', str(TARGET_API),
             '--version-code', str(VERSION_CODE), '--version-name', VERSION_NAME,
             # Release packaging by default: no run-as access to the ROM copy and
             # saves. --debuggable also permits `adb install -r -d` rollbacks.
             *(['--debug-mode'] if args.debuggable else []), staging / 'res.zip'])
        # SDL's Java activity layer from the same pinned release, plus ours.
        classes = staging / 'classes'; classes.mkdir()
        java_sources = sorted(SDL_JAVA.rglob('*.java')) + sorted((ROOT / 'native/android/java').rglob('*.java'))
        run([jdk / 'bin' / ('javac' + exe), '--release', '11', '-nowarn', '-classpath', android_jar, '-d', classes, *java_sources],
            env=java_env)
        jar = staging / 'shell.jar'
        run([jdk / 'bin' / ('jar' + exe), 'cf', jar, '-C', classes, '.'], env=java_env)
        dex = staging / 'dex'; dex.mkdir()
        run([build_tools / ('d8' + bat), '--min-api', str(API), '--lib', android_jar, '--output', dex, jar], env=java_env)
        unsigned = staging / 'unsigned.apk'
        with ZipFile(base) as src, ZipFile(unsigned, 'w') as dst:
            for item in src.infolist():
                dst.writestr(item, src.read(item))
            dst.writestr('classes.dex', (dex / 'classes.dex').read_bytes(), compress_type=ZIP_DEFLATED)
            # Uncompressed so the loader maps it in place (16 KiB aligned below).
            dst.writestr(check_apk.ENGINE_MEMBER, library.read_bytes(), compress_type=ZIP_STORED)
            dst.writestr('assets/DejaVuSansMono-Bold.ttf', bootstrap.FONT.read_bytes(), compress_type=ZIP_DEFLATED)
            dst.writestr('assets/LICENSE-DejaVu.txt', bootstrap.FONT_LICENSE.read_bytes(), compress_type=ZIP_DEFLATED)
            # Intent arguments still override the packaged driver.
            dst.writestr('assets/sc64/config.ini', config_text(args.render_driver, bundled_rom is not None),
                         compress_type=ZIP_DEFLATED)
            # Add private input last. It never enters a source tree or upload.
            if bundled_rom is not None:
                dst.writestr(check_apk.ROM_MEMBER, bundled_rom, compress_type=ZIP_DEFLATED)
        aligned = staging / 'aligned.apk'
        run([build_tools / ('zipalign' + exe), '-f', '-P', '16', '4', unsigned, aligned])
        # Personal builds use a private debug key, never a distribution key.
        # Keep the existing key: a new one would force uninstalling app data.
        key = ROOT / 'local/private/android/debug.keystore'
        key.parent.mkdir(parents=True, exist_ok=True)
        if not key.exists():
            run([jdk / 'bin' / ('keytool' + exe), '-genkeypair', '-keystore', key, '-storepass', 'android',
                 '-keypass', 'android', '-alias', 'androiddebugkey', '-dname', 'CN=SC64 Personal Debug',
                 '-keyalg', 'RSA', '-keysize', '2048', '-validity', '10000'], env=java_env)
        signed = staging / 'StarCraft64.apk'
        run([build_tools / ('apksigner' + bat), 'sign', '--ks', key, '--ks-pass', 'pass:android',
             '--out', signed, aligned], env=java_env)
        run([build_tools / ('apksigner' + bat), 'verify', signed], env=java_env)
        run([build_tools / ('zipalign' + exe), '-c', '-P', '16', '4', signed])
        report = {'schema': 2, 'profile': 'arm64-android', 'ndk': SDK_REVISION,
                  'api': API, 'target_api': TARGET_API, 'build_tools': BUILD_TOOLS,
                  'version_code': VERSION_CODE, 'render_driver': args.render_driver,
                  'render_driver_chain': RENDER_DRIVER_CHAINS[args.render_driver], 'sdl3_version': bootstrap.SDL_VERSION,
                  'sdl3_sha256': digest(bootstrap.SDL_ARCHIVE),
                  'cloud_upload': False, 'build_type': args.build_type, 'debuggable': args.debuggable, 'rom_included': bundled_rom is not None, 'generated_c_included': False,
                  'package': 'com.nomadic.sc64.personal',
                  'library_sha256': digest(library), 'apk_sha256': digest(signed),
                  'apk_bytes': signed.stat().st_size, 'on_device_accepted': False}
        report['content_audit'] = check_apk.audit(signed, library, bundled_rom=bundled_rom is not None)
        os.replace(signed, target)
    receipt.parent.mkdir(parents=True, exist_ok=True)
    receipt.write_text(json.dumps(report, indent=2) + '\n')
    print(receipt); print(target)

if __name__ == '__main__':
    main()
