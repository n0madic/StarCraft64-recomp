#!/usr/bin/env python3
"""Pack the staged Linux x86_64 package into a personal AppImage.

The input is the package that `cross_build.py linux` staged under local/dist and
its receipt, so the AppImage always wraps an audited binary. `--bundle-rom` adds
the verified NTSC-U ROM beside the executable; the app then starts from it
without creating another ROM copy. The squashfs image is built inside the local
Linux cross image (no registry push, no remote build) and joined with the pinned
type2 runtime, which is how `appimagetool` assembles an AppImage.
"""
from __future__ import annotations
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import bootstrap
from cross_build import audit, docker_image, in_container, run, sha256
from private_paths import require_local
from rom_info import NTSC_U_SHA1, load_ntsc_u

PACKAGE = ROOT / 'local/dist/StarCraft64-linux'
LINUX_RECEIPT = ROOT / 'local/reports/linux/build.json'
ROM_NAME = 'sc64-ntsc-u.z64'
BINARY = 'StarCraft64'
ICON = 'StarCraft64.png'
SQUASHFS_MAGIC = b'hsqs'
ELF_X86_64 = 62

# The packaged ROM is autostarted through the same config key as Android's
# bundled build; only the file beside the executable decides.
APP_RUN = f'''#!/bin/sh
# AppImage entry point. The ROM, when packaged, sits beside the executable.
HERE="$(dirname "$(readlink -f "$0")")"
BIN="$HERE/usr/bin"
if [ -f "$BIN/{ROM_NAME}" ]; then
    exec "$BIN/{BINARY}" --config=sc64.bundled_rom=1 "$@"
fi
exec "$BIN/{BINARY}" "$@"
'''

def desktop_entry(with_icon: bool) -> str:
    lines = ['[Desktop Entry]', 'Type=Application', 'Name=StarCraft 64',
             'Comment=Personal native port of StarCraft 64 (NTSC-U)', f'Exec={BINARY}']
    if with_icon:
        lines.append(f'Icon={Path(ICON).stem}')
    lines += ['Categories=Game;StrategyGame;', 'Terminal=false']
    return '\n'.join(lines) + '\n'

def check_package(package: Path, receipt: Path) -> dict[str, str]:
    """Require the staged package to be exactly what the Linux build receipt recorded."""
    if not receipt.is_file() or not package.is_dir():
        raise ValueError('Run `make build-linux` first: the Linux package or its receipt is missing')
    actual = audit(sorted(package.iterdir()))
    if actual != json.loads(receipt.read_text())['files_sha256']:
        raise ValueError('The Linux package differs from its build receipt; rebuild it with `make build-linux`')
    header = (package / BINARY).read_bytes()[:20]
    if header[:4] != b'\x7fELF' or struct.unpack_from('<H', header, 18)[0] != ELF_X86_64:
        raise ValueError(f'{BINARY} is not an x86_64 ELF executable')
    return actual

def stage_appdir(appdir: Path, package: Path, rom: bytes | None) -> None:
    """Lay out AppDir: resources beside the executable, launcher metadata at the root."""
    bin_dir = appdir / 'usr/bin'
    bin_dir.mkdir(parents=True)
    has_icon = (package / ICON).is_file()
    for member in sorted(package.iterdir()):
        if member.name != ICON:
            shutil.copy2(member, bin_dir / member.name)
    (bin_dir / BINARY).chmod(0o755)
    if has_icon:
        shutil.copy2(package / ICON, appdir / ICON)
        (appdir / '.DirIcon').symlink_to(ICON)
    (appdir / 'StarCraft64.desktop').write_text(desktop_entry(has_icon))
    (appdir / 'AppRun').write_text(APP_RUN)
    (appdir / 'AppRun').chmod(0o755)
    if rom is not None:
        (bin_dir / ROM_NAME).write_bytes(rom)
    # The image is root-owned: every user must be able to read and traverse it,
    # whatever the build host's umask or the staged files' modes were.
    for path in appdir.rglob('*'):
        if not path.is_symlink():
            path.chmod(0o755 if path.is_dir() or path.name in (BINARY, 'AppRun') else 0o644)

def audit_appdir(appdir: Path, rom: bytes | None) -> dict[str, str]:
    """Hash every member relative to AppDir; only the verified ROM may be a ROM."""
    members = {}
    for path in sorted(appdir.rglob('*')):
        if path.is_symlink() or path.is_dir():
            continue
        relative = path.relative_to(appdir).as_posix()
        if path.name == ROM_NAME:
            if rom is None or path.read_bytes() != rom:
                raise ValueError(f'Unexpected or altered ROM in AppImage: {relative}')
            continue
        members[relative] = audit([path])[path.name]
    if rom is not None and not (appdir / 'usr/bin' / ROM_NAME).is_file():
        raise ValueError('AppImage lacks the requested ROM')
    return members

def join_image(runtime: Path, squashfs: Path, output: Path) -> int:
    """Write runtime + squashfs as an executable AppImage; return the squashfs offset."""
    offset = runtime.stat().st_size
    with output.open('wb') as target:
        for part in (runtime, squashfs):
            with part.open('rb') as source:
                shutil.copyfileobj(source, target, 1024 * 1024)
    output.chmod(0o755)
    with output.open('rb') as image:
        image.seek(offset)
        if image.read(4) != SQUASHFS_MAGIC:
            raise ValueError('AppImage file system does not start at the runtime boundary')
    return offset

def mksquashfs(image: str, appdir: Path, output: Path) -> None:
    run(in_container(image, ['mksquashfs', str(appdir), str(output), '-noappend', '-root-owned', '-no-xattrs',
                              '-comp', 'zstd', '-Xcompression-level', '19', '-all-time', '0', '-mkfs-time', '0',
                              '-quiet']))

def extract_and_audit(image: str, appimage: Path, offset: int, destination: Path, rom: bytes | None) -> dict[str, str]:
    """Unpack the finished artifact and audit what a user would actually receive."""
    run(in_container(image, ['unsquashfs', '-no-progress', '-o', str(offset), '-d', str(destination), str(appimage)]))
    for required in ('AppRun', 'StarCraft64.desktop', f'usr/bin/{BINARY}'):
        if not (destination / required).is_file():
            raise ValueError(f'AppImage lacks {required}')
    return audit_appdir(destination, rom)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle-rom', type=Path, help='Pack this NTSC-U dump beside the executable (personal use only)')
    args = parser.parse_args()
    rom = None
    if args.bundle_rom:
        _, rom = load_ntsc_u(args.bundle_rom)
    files = check_package(PACKAGE, LINUX_RECEIPT)
    bootstrap.fetch(bootstrap.APPIMAGE_RUNTIME, bootstrap.APPIMAGE_RUNTIME_URL, bootstrap.APPIMAGE_RUNTIME_SHA256)
    dist = require_local(ROOT / 'local/dist')
    output = dist / ('StarCraft64-x86_64-bundled-rom.AppImage' if rom else 'StarCraft64-x86_64.AppImage')
    image = docker_image()
    with tempfile.TemporaryDirectory(prefix='.sc64-appimage-', dir=dist) as temporary:
        work = Path(temporary)
        appdir = work / 'AppDir'
        stage_appdir(appdir, PACKAGE, rom)
        staged = audit_appdir(appdir, rom)
        squashfs = work / 'AppDir.squashfs'
        mksquashfs(image, appdir, squashfs)
        built = work / output.name
        offset = join_image(bootstrap.APPIMAGE_RUNTIME, squashfs, built)
        if extract_and_audit(image, built, offset, work / 'extracted', rom) != staged:
            raise ValueError('The packed AppImage differs from the staged AppDir')
        receipt = {'schema': 1, 'profile': 'x86_64-linux-appimage', 'cloud_upload': False,
                   'linux_binary_sha256': files[BINARY], 'files_sha256': staged,
                   'runtime_tag': bootstrap.APPIMAGE_RUNTIME_TAG, 'runtime_sha256': sha256(bootstrap.APPIMAGE_RUNTIME),
                   'squashfs_offset': offset, 'rom_included': rom is not None,
                   'rom_sha1': NTSC_U_SHA1 if rom is not None else None,
                   'appimage_sha256': sha256(built), 'user_acceptance': False}
        # Publish only the complete artifact; a running AppImage is not truncated.
        os.replace(built, output)
    report = ROOT / f'local/reports/appimage/{output.stem}.json'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(receipt, indent=2) + '\n')
    print(report)
    print(output)

if __name__ == '__main__':
    main()
