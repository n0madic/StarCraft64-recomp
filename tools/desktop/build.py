#!/usr/bin/env python3
"""Build the personal arm64 macOS SDL3 application locally, without any upload.

On Linux and Windows hosts only ``--configure-only`` is available here: it
configures the host-compiler tree that the native tests use. Their packages come
from cross_build.py (``tools/sc64.py build`` picks the right tool).
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import plistlib
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
import bootstrap
import patch_shell_runtime
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hostenv
from bootstrap import FONT, FONT_LICENSE, MINIAUDIO, MINIAUDIO_VERSION, SDL_ARCHIVE, SDL_VERSION, STB_TRUETYPE
from game_validation import validate_ui_hooks
from icon import installed as installed_icon

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'local/deps/N64ModernRuntime-shell'
MACOS_DEPLOYMENT_TARGET = '13.0'

BACKUP_PREFIX = '.sc64-build-'
PREVIOUS_SUFFIX = '-previous.app'

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def resource_hashes(directory: Path) -> dict[str, str]:
    return {path.relative_to(directory).as_posix(): sha256(path)
            for path in sorted(directory.rglob('*')) if path.is_file()}

def publish_bundle(staged: Path, app: Path, previous: Path) -> None:
    """Replace app with staged; keep the old bundle only until publication succeeds."""
    if app.exists(): os.replace(app, previous)
    try: os.replace(staged, app)
    except BaseException:
        if previous.exists(): os.replace(previous, app)
        raise
    # The new bundle is in place, so rollback copies are no longer needed.
    # Also sweep copies retained by earlier builds of this tool.
    for stale in app.parent.glob(BACKUP_PREFIX + '*' + PREVIOUS_SUFFIX):
        if stale.is_dir() and not stale.is_symlink(): shutil.rmtree(stale)

def stage_resources(resources: Path, module: Path) -> None:
    """Shell resources: the overlay font, its license, the game module (ADR-086) and, when generated, the app icon."""
    resources.mkdir(parents=True, exist_ok=True)
    shutil.copy2(module, resources / module.name)
    shutil.copy2(FONT, resources / FONT.name)
    shutil.copy2(FONT_LICENSE, resources / 'LICENSE-DejaVu.txt')
    if icon := installed_icon('StarCraft64.icns'):
        shutil.copy2(icon, resources / icon.name)

def run(args: list[str], **kwargs) -> None:
    subprocess.run(args, check=True, **kwargs)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=8)
    parser.add_argument('--build-type', choices=bootstrap.BUILD_TYPES, default=bootstrap.DEFAULT_BUILD_TYPE,
                        help='CMake build type (default: %(default)s)')
    args = parser.parse_args()
    host = hostenv.current()
    if host.system == 'darwin' and host.arch != 'arm64':
        raise SystemExit('The macOS profile is arm64 only')
    if host.system != 'darwin' and not args.configure_only:
        raise SystemExit(f'No app bundle exists for {host.label}; build its package with '
                         '`python tools/sc64.py build` (Linux/Windows: tools/desktop/cross_build.py)')
    validate_ui_hooks(ROOT / 'local/generated/game')
    configure = ['-DSC64_BUILD_SHELL=ON', '-DSC64_RUNTIME_DIR=' + str(RUNTIME), f'-DCMAKE_BUILD_TYPE={args.build_type}']
    if host.system == 'darwin':
        llvm = hostenv.llvm_bin()
        if llvm is None:
            raise SystemExit('LLVM is required: set LLVM_BIN or install it (brew install llvm)')
        configure += [f'-DCMAKE_CXX_COMPILER={llvm / "clang++"}', f'-DCMAKE_C_COMPILER={llvm / "clang"}',
                      f'-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_DEPLOYMENT_TARGET}']
    # Other hosts compile with CC/CXX or the system compiler.
    bootstrap.main()
    patch_shell_runtime.prepare(ROOT / 'local/deps/N64ModernRuntime', RUNTIME)
    build = ROOT / 'local/build/desktop'
    run(['cmake', '-S', str(ROOT), '-B', str(build), '-G', 'Ninja', *configure])
    if args.configure_only:
        return
    # sc64_app depends on the game module, so one target builds both.
    run(['cmake', '--build', str(build), '--target', 'sc64_app', '-j', str(args.jobs)])
    app = ROOT / 'local/dist/StarCraft64.app'
    app.parent.mkdir(parents=True, exist_ok=True)
    receipt = ROOT / 'local/reports/desktop/build.json'
    receipt.parent.mkdir(parents=True, exist_ok=True)
    # Publish a complete new bundle instead of truncating a running executable
    # or leaving stale resources from a prior build.
    with tempfile.TemporaryDirectory(prefix=BACKUP_PREFIX, dir=app.parent) as temporary:
        staged = Path(temporary) / 'StarCraft64.app'
        (staged / 'Contents/MacOS').mkdir(parents=True)
        shutil.copy2(build / 'sc64_app', staged / 'Contents/MacOS/SC64')
        stage_resources(staged / 'Contents/Resources', build / 'sc64-game.dylib')
        plist = {
            'CFBundleExecutable': 'SC64', 'CFBundleIdentifier': 'com.nomadic.sc64.personal',
            'CFBundleName': 'StarCraft64', 'CFBundleDisplayName': 'StarCraft 64',
            'CFBundlePackageType': 'APPL', 'NSHighResolutionCapable': True,
            'LSMinimumSystemVersion': MACOS_DEPLOYMENT_TARGET, 'NSPrincipalClass': 'NSApplication'}
        if (staged / 'Contents/Resources/StarCraft64.icns').exists():
            plist['CFBundleIconFile'] = 'StarCraft64'
        (staged / 'Contents/Info.plist').write_bytes(plistlib.dumps(plist))
        build_receipt = {'schema': 2, 'profile': 'arm64-macos', 'host': host.label, 'build_type': args.build_type, 'cloud_upload': False,
            'binary_sha256': sha256(staged / 'Contents/MacOS/SC64'),
            'game_module_sha256': sha256(staged / 'Contents/Resources/sc64-game.dylib'),
            'sdl3_version': SDL_VERSION, 'sdl3_sha256': sha256(SDL_ARCHIVE),
            'miniaudio_version': MINIAUDIO_VERSION, 'miniaudio_sha256': sha256(MINIAUDIO),
            'stb_truetype_sha256': sha256(STB_TRUETYPE),
            'resources_sha256': resource_hashes(staged / 'Contents/Resources')}
        publish_bundle(staged, app, app.parent / (Path(temporary).name + PREVIOUS_SUFFIX))
    receipt.write_text(json.dumps(build_receipt, indent=2) + '\n')
    print(receipt)
    print(app)

if __name__ == '__main__':
    main()
