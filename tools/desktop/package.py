#!/usr/bin/env python3
"""Release launcher packages for macOS arm64, Linux x86_64 and Windows x86_64 (ADR-088).

A launcher is the shell, ``sc64_setup``, the prebuilt N64Recomp and the setup resources: no ROM, no
generated game code, no game module and no icon made from the game (those stay in personal builds);
it carries the neutral SC64 icon of neutral_icon.py.
The user supplies the ROM and the first start builds the game (ADR-087). Everything is built
locally; the layout is checked against an explicit list before the archive is written.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import plistlib
import shutil
import stat
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import bootstrap
import hostenv
import neutral_icon
import notices
import patch_shell_runtime
from cross_build import configured_compiler, plan_for
from dockerenv import Runner
from private_paths import PACKAGE_FORBIDDEN_SUFFIXES
from rom_info import MAGIC_FORMATS

RUNTIME = ROOT / 'local/deps/N64ModernRuntime-shell'
MACOS_DEPLOYMENT_TARGET = '13.0'
RECOMPILERS = ROOT / 'local/dist/recompiler'
RECIPE = ROOT / 'recipe/ntsc-u'
PATCH_TOOL = ROOT / 'tools/recompile/patch_recompiler.py'
DIST = ROOT / 'local/dist/launcher'
REPORTS = ROOT / 'local/reports/launcher'
TARGETS = {
    'macos': {'recompiler': 'macos-arm64', 'archive': '.zip', 'exe': ''},
    'linux': {'recompiler': 'linux-x86_64', 'archive': '.tar.gz', 'exe': ''},
    'windows': {'recompiler': 'windows-x86_64', 'archive': '.zip', 'exe': '.exe'},
}
SETUP_FILES = ('recipe/ntsc-u/sc64.syms.toml', 'recipe/ntsc-u/sc64.toml', 'recipe/ntsc-u/generated.json',
               'module/module_main.c', 'module/sc64_module.h', 'module/sc64_imports.def', 'module/recomp.h',
               'module/include/librecomp/sections.h')
# The only sources that may carry a game-code suffix: authored C that the user's compiler builds.
AUTHORED_C = {'setup-resources/module/module_main.c'}
EXECUTABLE_NAMES = {'StarCraft64', 'StarCraft64.exe', 'SC64', 'sc64_setup', 'sc64_setup.exe', 'N64Recomp', 'N64Recomp.exe'}


class PackageError(SystemExit):
    """The inputs are not fit to publish, or a package would carry something it must not."""


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def package_name(target: str) -> str:
    arch = 'arm64' if target == 'macos' else 'x86_64'
    return f'StarCraft64-launcher-{target}-{arch}'


def readme(target: str) -> str:
    first = {
        'macos': ('Open StarCraft64.app. macOS may refuse an app downloaded from the internet because it is not\n'
                  '   notarized: Control-click the app, choose Open, then Open again (once).'),
        'linux': 'Run ./StarCraft64 from this folder.',
        'windows': ('Run StarCraft64.exe. Windows SmartScreen may warn about an unrecognized app: choose More info,\n'
                    '   then Run anyway (once).'),
    }[target]
    return f'''StarCraft 64 launcher ({target})
=================================

This package does not contain the game. It builds a native version of StarCraft 64 on your computer
from a cartridge image of the NTSC-U (USA) release that you own. Nothing is uploaded.

First start
-----------
1. {first}
2. Choose your ROM dump (a .z64, .n64 or .v64 file of 32 MiB, NTSC-U, SHA-1
   a36d91b5aac7bb95df0d7de5f9814f5270e25c2b). Other versions are refused.
3. Wait while the game is built (about a minute). It needs a C compiler (gcc, clang or Apple's
   command line tools). If none is found, a small compiler (zig, 50-100 MB, checked against a fixed
   SHA-256) is downloaded once; this needs the internet and the system tools curl and tar.
4. The game starts. Later starts begin immediately.

Where things are kept
---------------------
The built game, your save and the logs are in the application's storage folder:
  macOS    ~/Library/Application Support/StarCraft64
  Linux    ~/.local/share/StarCraft64
  Windows  %APPDATA%\\StarCraft64
Delete that folder to remove everything the launcher created (this includes your save data).

Command line
------------
sc64_setup builds the game without opening the window: sc64_setup --rom PATH [--use-zig] [--force].

Licenses
--------
See NOTICE.txt, SOURCE.txt and the LICENSES folder. The program is free software under the terms
listed there; the game, its ROM and its trademarks are not covered by those licenses.
'''


def check_recompiler(target: str) -> Path:
    """The prebuilt N64Recomp, only if its receipt shows that it reproduced the recipe hashes."""
    directory = RECOMPILERS / TARGETS[target]['recompiler']
    binary = directory / ('N64Recomp' + TARGETS[target]['exe'])
    receipt = directory / 'receipt.json'
    if not binary.is_file() or not receipt.is_file():
        raise PackageError(f'{binary} not found; run `python tools/sc64.py build-recompiler --target {TARGETS[target]["recompiler"]}`')
    data = json.loads(receipt.read_text())
    if data.get('binary_sha256') != sha256(binary):
        raise PackageError(f'{receipt} describes another binary; rebuild it')
    command = f'`python tools/sc64.py verify-recompiler --target {TARGETS[target]["recompiler"]}`'
    verification = data.get('recipe_verification')
    if not verification:
        raise PackageError(f'N64Recomp for {target} was never checked against the recipe; run {command}')
    if verification.get('recipe_sha256') != sha256(RECIPE / 'generated.json'):
        raise PackageError(f'N64Recomp for {target} was checked against a different recipe than the one being shipped; run {command}')
    if data.get('source', {}).get('patch_tool_sha256') != sha256(PATCH_TOOL):
        raise PackageError(f'N64Recomp for {target} was built with an older patch tool; rebuild it with '
                           f'`python tools/sc64.py build-recompiler --target {TARGETS[target]["recompiler"]}` and verify it again')
    return binary


def setup_resource_sources() -> dict[str, Path]:
    """Files of setup-resources/ and where each comes from (the repository, plus recomp.h of the runtime)."""
    sources = {
        'recipe/ntsc-u/sc64.syms.toml': ROOT / 'recipe/ntsc-u/sc64.syms.toml',
        'recipe/ntsc-u/sc64.toml': ROOT / 'recipe/ntsc-u/sc64.toml',
        'recipe/ntsc-u/generated.json': ROOT / 'recipe/ntsc-u/generated.json',
        'module/module_main.c': ROOT / 'native/module/module_main.c',
        'module/sc64_module.h': ROOT / 'native/module/sc64_module.h',
        'module/sc64_imports.def': ROOT / 'native/module/sc64_imports.def',
        'module/recomp.h': RUNTIME / 'N64Recomp/include/recomp.h',
        'module/include/librecomp/sections.h': ROOT / 'native/module/include/librecomp/sections.h',
    }
    assert set(sources) == set(SETUP_FILES)
    return sources


Source = Path | str | bytes


def info_plist() -> bytes:
    return plistlib.dumps({'CFBundleExecutable': 'SC64', 'CFBundleIconFile': 'StarCraft64', 'CFBundleIdentifier': 'com.nomadic.sc64', 'CFBundleName': 'StarCraft64',
                           'CFBundleDisplayName': 'StarCraft 64', 'CFBundlePackageType': 'APPL', 'NSHighResolutionCapable': True,
                           'LSMinimumSystemVersion': MACOS_DEPLOYMENT_TARGET, 'NSPrincipalClass': 'NSApplication'})


def layout(target: str, binaries: dict[str, Path], notice_files: dict[str, str], icons: Path) -> dict[str, Source]:
    """Package relative path -> source (a file to copy, or generated text/bytes): exactly what ships.

    ``icons`` is the neutral SC64 icon set (neutral_icon.py); the personal, game-derived icon directory is never read.
    """
    suffix = TARGETS[target]['exe']
    files: dict[str, Source] = {}
    if target == 'macos':
        # Executables live in Contents/MacOS so that the bundle signature covers them.
        files['StarCraft64.app/Contents/Info.plist'] = info_plist()
        files['StarCraft64.app/Contents/MacOS/SC64'] = binaries['shell']
        files['StarCraft64.app/Contents/MacOS/sc64_setup'] = binaries['setup']
        files['StarCraft64.app/Contents/MacOS/N64Recomp'] = binaries['recompiler']
        resources = 'StarCraft64.app/Contents/Resources/'
        files[resources + 'StarCraft64.icns'] = icons / 'StarCraft64.icns'
    else:
        files[f'StarCraft64{suffix}'] = binaries['shell']
        files[f'sc64_setup{suffix}'] = binaries['setup']
        files[f'N64Recomp{suffix}'] = binaries['recompiler']
        resources = ''
        if target == 'linux':
            files['StarCraft64.png'] = icons / 'StarCraft64.png'  # the Windows exe embeds its icon instead
    files[resources + 'DejaVuSansMono-Bold.ttf'] = bootstrap.FONT
    files[resources + 'launcher-icon.png'] = icons / 'StarCraft64.png'  # shown on the start screen
    for relative, source in setup_resource_sources().items():
        files[resources + 'setup-resources/' + relative] = source
    for name, text in notice_files.items():  # NOTICE.txt, SOURCE.txt, LICENSES/*
        files[resources + name] = text
    files['README.txt'] = readme(target)
    return files


def audit(stage: Path, expected: dict[str, Source]) -> None:
    """The staged tree must hold exactly ``expected``, byte for byte, and nothing private or game-derived."""
    found = {}
    for path in sorted(stage.rglob('*')):
        if path.is_symlink():
            raise PackageError(f'Symbolic link in package: {path.relative_to(stage)}')
        if path.is_file():
            found[path.relative_to(stage).as_posix()] = path
    if set(found) != set(expected):
        extra, missing = sorted(set(found) - set(expected)), sorted(set(expected) - set(found))
        raise PackageError(f'Package differs from its layout (unexpected {extra[:5]}, missing {missing[:5]})')
    for name, path in found.items():
        data = path.read_bytes()
        authored = any(name.endswith(allowed) for allowed in AUTHORED_C)
        if (path.suffix.lower() in PACKAGE_FORBIDDEN_SUFFIXES and not authored) or path.name.startswith('sc64-game') \
                or data[:4] in frozenset(MAGIC_FORMATS):
            raise PackageError(f'Forbidden content in a launcher package: {name}')
        source = expected[name]
        want = source.read_bytes() if isinstance(source, Path) else source.encode() if isinstance(source, str) else source
        if data != want:
            raise PackageError(f'{name} differs from its source')


def stage_files(stage: Path, files: dict[str, Source]) -> None:
    for name, source in files.items():
        destination = stage / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        if isinstance(source, Path):
            shutil.copy2(source, destination)
        else:
            destination.write_bytes(source.encode() if isinstance(source, str) else source)
        destination.chmod(0o755 if is_executable(name) else 0o644)


def is_executable(name: str) -> bool:
    return Path(name).name in EXECUTABLE_NAMES


def archive_time(commit_time: int) -> tuple[int, int, int, int, int, int]:
    moment = time.gmtime(max(commit_time, 315532800))
    return moment.tm_year, moment.tm_mon, moment.tm_mday, moment.tm_hour, moment.tm_min, moment.tm_sec - moment.tm_sec % 2


def write_archive(stage: Path, root_name: str, archive: Path, commit_time: int) -> None:
    """Deterministic archive: sorted members, fixed modes, the commit time as modification time."""
    members = sorted(path for path in stage.rglob('*') if path.is_file())
    if archive.suffix == '.zip':
        with ZipFile(archive, 'w', ZIP_DEFLATED) as output:
            for path in members:
                relative = path.relative_to(stage).as_posix()
                info = ZipInfo(f'{root_name}/{relative}', archive_time(commit_time))
                info.compress_type = ZIP_DEFLATED
                info.external_attr = (stat.S_IFREG | (0o755 if is_executable(relative) else 0o644)) << 16
                output.writestr(info, path.read_bytes())
        return

    def normalize(info: tarfile.TarInfo) -> tarfile.TarInfo:
        info.mode = 0o755 if info.isdir() or is_executable(info.name) else 0o644
        info.uid = info.gid = 0
        info.uname = info.gname = ''
        info.mtime = commit_time
        return info

    entries = sorted({Path('.')} | {p for path in members for p in path.relative_to(stage).parents} |
                     {path.relative_to(stage) for path in members})
    # tarfile's own "w:gz" stamps the file name and the current time into the gzip header.
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode='w', format=tarfile.PAX_FORMAT) as output:
        for entry in entries:
            name = root_name if entry == Path('.') else f'{root_name}/{entry.as_posix()}'
            output.add(stage / entry, name, recursive=False, filter=normalize)
    with archive.open('wb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=commit_time, compresslevel=9) as zipped:
        zipped.write(buffer.getvalue())


def build_binaries(target: str, host: hostenv.Host, jobs: int, docker: bool, build_type: str,
                   icons: Path) -> tuple[dict[str, Path], dict]:
    """Configure and build the shell and sc64_setup without any game code; returns their paths and toolchain facts."""
    bootstrap.main()
    patch_shell_runtime.prepare(ROOT / 'local/deps/N64ModernRuntime', RUNTIME)
    prefix = f'-ffile-prefix-map={ROOT}=/sc64'
    configure = ['-DSC64_BUILD_SHELL=ON', '-DSC64_BUILD_GAME_MODULE=OFF', '-DSC64_PACKAGED_BUILD=ON',
                 f'-DSC64_ICON_RC={icons / "StarCraft64.rc" if target == "windows" else ""}',
                 f'-DSC64_RUNTIME_DIR={RUNTIME}', f'-DCMAKE_BUILD_TYPE={build_type}',
                 f'-DCMAKE_C_FLAGS={prefix}', f'-DCMAKE_CXX_FLAGS={prefix}']
    if target == 'macos':
        if (host.system, host.arch) != ('darwin', 'arm64'):
            raise PackageError('The macOS launcher is built on a macOS arm64 host')
        llvm = hostenv.llvm_bin()
        if llvm is None:
            raise PackageError('LLVM is required: set LLVM_BIN or run `brew install llvm`')
        configure += [f'-DCMAKE_CXX_COMPILER={llvm / "clang++"}', f'-DCMAKE_C_COMPILER={llvm / "clang"}',
                      f'-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_DEPLOYMENT_TARGET}']
        runner, tree, strip = Runner(host), ROOT / 'local/build/launcher-macos', None
    else:
        plan = plan_for(target, host, docker)
        from cross_build import docker_image
        runner = Runner(host, docker_image(host) if plan.mode == 'docker' else None)
        tree = ROOT / f'local/build/launcher-{target}{"-native" if plan.mode == "native" else ""}'
        configure.append(f'-DCMAKE_TOOLCHAIN_FILE={runner.path(plan.toolchain)}')
        strip = plan.strip
    runner.run(['cmake', '-S', ROOT, '-B', tree, '-G', 'Ninja', *configure])
    runner.run(['cmake', '--build', tree, '--target', 'sc64_app', 'sc64_setup', '-j', str(jobs)])
    suffix = TARGETS[target]['exe']
    shell_name = 'sc64_app' if target == 'macos' else 'StarCraft64'
    binaries = {'shell': tree / (shell_name + suffix), 'setup': tree / ('sc64_setup' + suffix)}
    for key in binaries:
        if not binaries[key].is_file():
            raise PackageError(f'Build did not produce {binaries[key]}')
    compiler = configured_compiler(tree)
    facts = {'tree': str(tree), 'compiler': runner.output([compiler, '--version']).splitlines()[0] if compiler else None}
    if strip:
        stripped = tree / 'stripped'
        stripped.mkdir(exist_ok=True)
        for key, path in list(binaries.items()):
            copy = stripped / path.name
            shutil.copy2(path, copy)
            runner.run([strip, '--strip-all', copy])
            binaries[key] = copy
    return binaries, facts


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=tuple(TARGETS))
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--build-type', choices=bootstrap.BUILD_TYPES, default=bootstrap.DEFAULT_BUILD_TYPE)
    parser.add_argument('--docker', action='store_true', help='Linux: use the pinned Docker image even on a Linux x86_64 host')
    parser.add_argument('--allow-dirty', action='store_true',
                        help='package a working tree with uncommitted changes (the source statement will not match it)')
    args = parser.parse_args()
    host = hostenv.current()
    recompiler = check_recompiler(args.target)
    files_text, info = notices.build(target=args.target)
    if info['dirty'] and not args.allow_dirty:
        raise PackageError('The working tree has uncommitted changes, so SOURCE.txt would not describe this build; '
                           'commit them or pass --allow-dirty')
    if not info['published']:
        print(f'WARNING: commit {info["commit"][:12]} is not on any remote branch; push it before publishing the package',
              file=sys.stderr)
    icons = ROOT / 'local/build/neutral-icon'
    if icons.exists():
        shutil.rmtree(icons)
    icon_receipt = neutral_icon.build(icons)
    binaries, facts = build_binaries(args.target, host, args.jobs, args.docker, args.build_type, icons)
    binaries['recompiler'] = recompiler
    expected = layout(args.target, binaries, files_text, icons)
    name = package_name(args.target)
    DIST.mkdir(parents=True, exist_ok=True)
    REPORTS.mkdir(parents=True, exist_ok=True)
    commit_time = int(notices.git('show', '-s', '--format=%ct', 'HEAD') or time.time())
    with tempfile.TemporaryDirectory(prefix='.sc64-launcher-', dir=DIST) as temporary:
        stage = Path(temporary) / name
        stage_files(stage, expected)
        audit(stage, expected)
        if args.target == 'macos' and shutil.which('codesign'):
            # Seals the bundle (ad hoc: there is no Developer ID); it changes the Mach-O files, so audit first.
            subprocess.run(['codesign', '--force', '--deep', '--sign', '-', str(stage / 'StarCraft64.app')], check=True,
                           capture_output=True)
        hashes = {path.relative_to(stage).as_posix(): sha256(path) for path in sorted(stage.rglob('*')) if path.is_file()}
        archive = Path(temporary) / (name + TARGETS[args.target]['archive'])
        write_archive(stage, name, archive, commit_time)
        final = DIST / archive.name
        os.replace(archive, final)
    receipt = {'schema': 1, 'target': args.target, 'host': host.label, 'cloud_upload': False, 'game_derived': False,
               'source': info, 'toolchain': facts, 'icon': icon_receipt, 'files_sha256': hashes, 'archive': final.name,
               'archive_sha256': sha256(final), 'recompiler_sha256': sha256(recompiler)}
    (REPORTS / f'{args.target}.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(REPORTS / f'{args.target}.json')
    print(final)


if __name__ == '__main__':
    main()
