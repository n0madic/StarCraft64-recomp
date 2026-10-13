#!/usr/bin/env python3
"""License texts, notice and source statement for the release launcher (ADR-088).

The launcher executables statically link N64ModernRuntime (GPL-3.0) beside permissively
licensed components, so a package carries every license text, a component list and a
statement of where the corresponding source is. Texts are copied from the pinned sources
on this machine, never written from memory. The output holds no game data.
"""
from __future__ import annotations

import glob
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Callable
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / 'third_party/N64ModernRuntime'
RECOMP = ROOT / 'third_party/N64Recomp'
TOOLCHAINS = ROOT / 'local/toolchains'
REPOSITORY_URL = 'https://github.com/n0madic/StarCraft64-recomp'
GPL_FILE = 'LICENSES/GPL-3.0.txt'


class NoticeError(SystemExit):
    """A license source is missing or no longer has the expected shape."""


def whole(path: Path) -> Callable[[], str]:
    def read() -> str:
        if not path.is_file():
            raise NoticeError(f'License source not found: {path}')
        return path.read_text(errors='replace').strip() + '\n'
    return read


def tail_from(path: Path, marker: str) -> Callable[[], str]:
    """Text from the line containing ``marker`` to the end of the file, without a closing comment mark."""
    def read() -> str:
        lines = path.read_text(errors='replace').splitlines()
        for index, line in enumerate(lines):
            if marker in line:
                body = lines[index:]
                while body and body[-1].strip() in ('', '*/'):
                    body.pop()
                return '\n'.join(body) + '\n'
        raise NoticeError(f'{path} has no license block starting with {marker!r}')
    return read


def slash_block(path: Path, first_line: str) -> Callable[[], str]:
    """A run of ``//`` comment lines starting at the line that contains ``first_line``."""
    def read() -> str:
        lines = path.read_text(errors='replace').splitlines()
        for index, line in enumerate(lines):
            if first_line in line:
                block = []
                for text in lines[index:]:
                    if not text.lstrip().startswith('//'):
                        break
                    block.append(re.sub(r'^\s*//\s?', '', text))
                return '\n'.join(block).strip() + '\n'
        raise NoticeError(f'{path} has no comment block starting with {first_line!r}')
    return read


def star_block(path: Path, contains: str) -> Callable[[], str]:
    """The first ``/* ... */`` comment of a header that contains ``contains``."""
    def read() -> str:
        text = path.read_text(errors='replace')
        for match in re.finditer(r'/\*.*?\*/', text, flags=re.S):
            if contains in match.group(0):
                return '\n'.join(re.sub(r'^\s*(/\*+|\*/|\*)\s?', '', line)
                                 for line in match.group(0).splitlines()).strip() + '\n'
        raise NoticeError(f'{path} has no license comment containing {contains!r}')
    return read


def first_of(label: str, *sources: Callable[[], str | None]) -> Callable[[], str]:
    """The first source that yields text; each source returns None when its file is not on this machine."""
    def read() -> str:
        for source in sources:
            text = source()
            if text:
                return text.strip() + '\n'
        raise NoticeError(f'No license text for {label} was found on this machine (see tools/desktop/notices.py for the places tried)')
    return read


def local_file(pattern: str) -> Callable[[], str | None]:
    """The first file matching an absolute glob pattern."""
    def read() -> str | None:
        for name in sorted(glob.glob(pattern)):
            if Path(name).is_file():
                return Path(name).read_text(errors='replace')
        return None
    return read


def linux_image_file(shell_glob: str) -> Callable[[], str | None]:
    """A file of the pinned Debian cross image that builds the Linux package (docker only; nothing is pushed)."""
    def read() -> str | None:
        if not shutil.which('docker'):
            return None
        sys.path.insert(0, str(ROOT / 'tools/desktop'))
        import cross_build
        import hostenv
        image = cross_build.docker_image(hostenv.current())
        done = subprocess.run(['docker', 'run', '--rm', image, 'sh', '-c', f'cat $(ls {shell_glob} | head -n 1)'],
                              capture_output=True, text=True)
        return done.stdout if done.returncode == 0 else None
    return read


def mit_with_copyright(header: Path, source_license: Path) -> Callable[[], str]:
    """The standard MIT text with the copyright line the header declares in SPDX form."""
    def read() -> str:
        spdx = re.search(r'SPDX-FileCopyrightText:\s*(.+)', header.read_text(errors='replace'))
        if not spdx:
            raise NoticeError(f'{header} declares no SPDX copyright')
        body = source_license.read_text(errors='replace').split('\n\n', 2)
        if len(body) < 3 or 'Permission is hereby granted' not in body[2]:
            raise NoticeError(f'{source_license} is not a standard MIT license')
        copyright_line = re.sub(r'<[^>]+>', '', spdx.group(1)).strip()
        return f'MIT License\n\nCopyright (c) {copyright_line}\n\n{body[2].strip()}\n'
    return read


@dataclass(frozen=True)
class Component:
    name: str
    license_id: str
    used_in: str
    file: str  # name under LICENSES/
    text: Callable[[], str] | None  # None: the text is shared (see GPL_FILE)


def runtime_components(target: str | None) -> list[Component]:
    """C runtime pieces linked statically into the binaries of one package target (none for macOS)."""
    gcc_runtime = first_of('the GCC Runtime Library Exception',
                           local_file('/opt/homebrew/Cellar/gcc/*/COPYING.RUNTIME'),
                           local_file('/usr/local/Cellar/gcc/*/COPYING.RUNTIME'),
                           local_file('/usr/share/doc/gcc-*-base/copyright'),
                           local_file('/usr/share/doc/gcc-mingw-w64-base/copyright'),
                           local_file('/mingw64/share/licenses/gcc-libs/RUNTIME.LIBRARY.EXCEPTION'),
                           linux_image_file('/usr/share/doc/gcc-*-base/copyright'))
    if target == 'linux':
        return [
            Component('GNU C Library (glibc), statically linked', 'LGPL-2.1+', 'N64Recomp (Linux)', 'glibc-LGPL-2.1.txt',
                      first_of('the LGPL-2.1', local_file('/usr/share/common-licenses/LGPL-2.1'),
                               linux_image_file('/usr/share/common-licenses/LGPL-2.1'))),
            Component('GNU C Library copyright notices', 'LGPL-2.1+', 'N64Recomp (Linux)', 'glibc-copyright.txt',
                      first_of('the glibc copyright notices', local_file('/usr/share/doc/libc6-dev/copyright'),
                               linux_image_file('/usr/share/doc/libc6-dev*/copyright'))),
            Component('libstdc++ and libgcc', 'GPL-3.0+ with the GCC Runtime Library Exception', 'StarCraft64, sc64_setup, N64Recomp (Linux)',
                      'GCC-runtime-exception.txt', gcc_runtime),
        ]
    if target == 'windows':
        return [
            Component('mingw-w64 runtime (CRT, winpthreads)', 'ZPL-2.1, MIT and others', 'every Windows executable',
                      'mingw-w64.txt', first_of('the mingw-w64 license summary',
                                                local_file('/opt/homebrew/Cellar/mingw-w64/*/COPYING'),
                                                local_file('/usr/local/Cellar/mingw-w64/*/COPYING'),
                                                local_file('/usr/share/doc/mingw-w64-x86-64-dev/copyright'),
                                                local_file('/mingw64/share/licenses/mingw-w64-crt/COPYING'))),
            Component('libstdc++ and libgcc', 'GPL-3.0+ with the GCC Runtime Library Exception', 'every Windows executable',
                      'GCC-runtime-exception.txt', gcc_runtime),
        ]
    return []


def components(root: Path = ROOT, target: str | None = None) -> list[Component]:
    runtime, recomp, tools = root / 'third_party/N64ModernRuntime', root / 'third_party/N64Recomp', root / 'local/toolchains'
    mit_source = recomp / 'LICENSE'
    return [
        Component('StarCraft 64 recomp (this project: shell, sc64_setup, recipe)', 'MIT', 'StarCraft64, sc64_setup',
                  'StarCraft64-recomp-MIT.txt', whole(root / 'LICENSE')),
        Component('N64ModernRuntime: librecomp, ultramodern', 'GPL-3.0', 'StarCraft64 (statically linked)',
                  'GPL-3.0.txt', whole(runtime / 'COPYING')),
        Component('N64Recomp', 'MIT', 'N64Recomp', 'N64Recomp-MIT.txt', whole(recomp / 'LICENSE')),
        Component('rabbitizer', 'MIT', 'N64Recomp', 'rabbitizer-MIT.txt', whole(recomp / 'lib/rabbitizer/LICENSE')),
        Component('{fmt}', 'MIT', 'N64Recomp', 'fmt-MIT.txt', whole(recomp / 'lib/fmt/LICENSE')),
        Component('toml++', 'MIT', 'N64Recomp', 'tomlplusplus-MIT.txt', whole(recomp / 'lib/tomlplusplus/LICENSE')),
        Component('ELFIO', 'MIT', 'N64Recomp', 'ELFIO-MIT.txt', whole(recomp / 'lib/ELFIO/LICENSE.txt')),
        Component('SDL 3', 'Zlib', 'StarCraft64, sc64_setup', 'SDL3-Zlib.txt',
                  whole(tools / 'sdl3/SDL3-3.4.18/LICENSE.txt')),
        Component('miniaudio', 'Unlicense OR MIT-0', 'StarCraft64', 'miniaudio.txt',
                  tail_from(tools / 'miniaudio.h', 'This software is available as a choice of the following licenses')),
        Component('stb_truetype', 'MIT OR Unlicense', 'StarCraft64', 'stb_truetype.txt',
                  tail_from(tools / 'stb/stb_truetype.h', 'This software is available under 2 licenses')),
        Component('JSON for Modern C++', 'MIT', 'StarCraft64, sc64_setup', 'nlohmann-json-MIT.txt',
                  mit_with_copyright(runtime / 'thirdparty/json/json.hpp', mit_source)),
        Component('xxHash', 'BSD-2-Clause', 'StarCraft64', 'xxHash-BSD-2-Clause.txt',
                  whole(runtime / 'thirdparty/xxHash/LICENSE')),
        Component('o1heap', 'MIT', 'StarCraft64', 'o1heap-MIT.txt', whole(runtime / 'thirdparty/o1heap/LICENSE')),
        Component('miniz', 'MIT', 'StarCraft64', 'miniz-MIT.txt', whole(runtime / 'thirdparty/miniz/LICENSE')),
        Component('moodycamel::ConcurrentQueue', 'BSD-2-Clause', 'StarCraft64', 'concurrentqueue-BSD-2-Clause.txt',
                  slash_block(runtime / 'thirdparty/concurrentqueue/concurrentqueue.h', 'Simplified BSD license')),
        Component('sse2neon', 'MIT', 'StarCraft64 (Apple silicon)', 'sse2neon-MIT.txt',
                  star_block(runtime / 'thirdparty/sse2neon/sse2neon.h', 'freely redistributable under the MIT License')),
        Component('DejaVu Sans Mono Bold', 'Bitstream Vera / DejaVu license', 'overlay font', 'DejaVu.txt',
                  whole(tools / 'dejavu/LICENSE')),
    ] + runtime_components(target)


def git(*words: str, cwd: Path = ROOT) -> str:
    return subprocess.run(['git', '-C', str(cwd), *words], capture_output=True, text=True).stdout.strip()


def public_url(remote: str) -> str:
    """An anonymous https URL for a git remote: no credentials, ssh forms rewritten, anything else rejected."""
    remote = remote.strip()
    scp_like = re.fullmatch(r'[\w.-]+@([\w.-]+):(?!//)(.+)', remote)  # git@github.com:owner/repo.git
    if scp_like:
        host, path = scp_like.groups()
    else:
        parts = urlsplit(remote)
        if parts.scheme not in ('https', 'http', 'ssh', 'git') or not parts.hostname:
            raise NoticeError(f'The origin remote {parts.scheme or "?"}://... is not a URL the source statement can publish')
        host, path = parts.hostname + (f':{parts.port}' if parts.port and parts.scheme.startswith('http') else ''), parts.path.lstrip('/')
    return 'https://' + host + '/' + path.removesuffix('.git')


def source_info(root: Path = ROOT) -> dict:
    """Where the corresponding source of this build can be fetched."""
    remote = git('remote', 'get-url', 'origin', cwd=root)
    url = public_url(remote) if remote else REPOSITORY_URL
    commit = git('rev-parse', 'HEAD', cwd=root)
    return {'url': url, 'commit': commit,
            'dirty': bool(git('status', '--porcelain', '--untracked-files=no', cwd=root)),
            'published': bool(git('branch', '-r', '--contains', commit, cwd=root)) if commit else False,
            'runtime_commit': git('rev-parse', 'HEAD', cwd=root / 'third_party/N64ModernRuntime'),
            'recompiler_commit': git('rev-parse', 'HEAD', cwd=root / 'third_party/N64Recomp')}


def render(info: dict, items: list[Component], texts: dict[str, str], target: str | None = None) -> dict[str, str]:
    """Package files: NOTICE.txt, SOURCE.txt and LICENSES/*."""
    rows = '\n'.join(f'  {item.name}\n      License: {item.license_id}; used in: {item.used_in}; text: LICENSES/{item.file}'
                     for item in items)
    notice = (
        'StarCraft 64 launcher\n=====================\n\n'
        'This package contains no game code, no ROM and no assets. It builds the game on your computer\n'
        'from a cartridge image of StarCraft 64 (NTSC-U) that you provide and own.\n\n'
        'The StarCraft64 executable statically links N64ModernRuntime (librecomp, ultramodern), which is\n'
        'licensed under the GNU General Public License version 3 (LICENSES/GPL-3.0.txt). The executables of\n'
        'this package are therefore distributed under the terms of that license; their source is described\n'
        'in SOURCE.txt. The licenses of the other components are listed below and apply to them.\n\n'
        f'Components\n----------\n{rows}\n')
    source = (
        'Corresponding source (GNU GPL version 3, section 6)\n'
        '===================================================\n\n'
        'The executables in this package (StarCraft64, sc64_setup, N64Recomp) were built from:\n\n'
        f'  StarCraft64-recomp   {info["url"]}\n'
        f'                       commit {info["commit"]}\n'
        f'  N64ModernRuntime     https://github.com/N64Recomp/N64ModernRuntime\n'
        f'                       commit {info["runtime_commit"]}\n'
        f'  N64Recomp            https://github.com/N64Recomp/N64Recomp\n'
        f'                       commit {info["recompiler_commit"]}\n\n'
        'Both dependencies are patched by scripts in StarCraft64-recomp (tools/recompile/patch_*.py,\n'
        'tools/desktop/patch_shell_runtime.py); the scripts, the build files and the list of every other\n'
        'pinned dependency are part of that repository (docs/toolchain-sources.md). To get the source:\n\n'
        f'  git clone {info["url"]}\n  git checkout {info["commit"]}\n\n'
        'It is available there at no charge, for as long as this package is offered. Build instructions\n'
        'are in docs/desktop.md ("make package").\n')
    if target == 'linux':
        source += ('\nN64Recomp in this package is linked statically against the GNU C Library (LGPL-2.1+, see\n'
                   'LICENSES/glibc-LGPL-2.1.txt). It is built from the source above with "make build-recompiler";\n'
                   'rebuilding it against another version of the library is the way to relink it.\n')
    files = {'NOTICE.txt': notice, 'SOURCE.txt': source}
    for item in items:
        files[f'LICENSES/{item.file}'] = texts[item.file]
    return files


def build(root: Path = ROOT, target: str | None = None) -> tuple[dict[str, str], dict]:
    items = components(root, target)
    texts = {item.file: item.text() for item in items if item.text}
    info = source_info(root)
    for key in ('commit', 'runtime_commit', 'recompiler_commit'):
        if not info[key]:
            raise NoticeError(f'SOURCE.txt needs the {key.replace("_", " ")} but git reports none; build from the repository checkouts')
    return render(info, items, texts, target), info


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='directory to write NOTICE.txt, SOURCE.txt and LICENSES/ into')
    parser.add_argument('--target', choices=('macos', 'linux', 'windows'), help='package target (adds its runtime licenses)')
    args = parser.parse_args()
    files, info = build(target=args.target)
    for name, text in files.items():
        path = args.output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
    print(f'{len(files)} files; commit {info["commit"][:12]}, published={info["published"]}, dirty={info["dirty"]}')


if __name__ == '__main__':
    main()
