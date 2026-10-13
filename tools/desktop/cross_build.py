#!/usr/bin/env python3
"""Build the personal Windows or Linux x86_64 SDL3 shell package locally.

Windows uses mingw-w64 GCC: cross-compiled on macOS and Linux, native from an
MSYS2 MINGW64 shell on Windows. Linux is built with the host compiler on a Linux
x86_64 machine and in a local Docker image everywhere else (or with ``--docker``;
no registry push, no remote build). Generated game code never leaves this machine.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import tarfile
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from zipfile import ZipFile, ZIP_DEFLATED
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import bootstrap
import hostenv
import patch_shell_runtime
from hostenv import Host
from icon import installed as installed_icon
from game_validation import validate_ui_hooks
from private_paths import PACKAGE_FORBIDDEN_SUFFIXES
from rom_info import MAGIC_FORMATS

RUNTIME = ROOT / 'local/deps/N64ModernRuntime-shell'
DOCKERFILE = ROOT / 'tools/desktop/linux-cross.Dockerfile'
# Where the repository appears inside the container when the host path cannot be reused.
CONTAINER_ROOT = PurePosixPath('/work')
TARGETS = {
    'windows': {'binary': 'StarCraft64.exe', 'profile': 'x86_64-windows', 'archive': '.zip'},
    'linux': {'binary': 'StarCraft64', 'profile': 'x86_64-linux', 'archive': '.tar.gz'},
}
TOOLCHAINS = {
    'mingw': ('cmake/toolchains/mingw-w64-x86_64.cmake', hostenv.MINGW_PREFIX + 'strip'),
    'native': ('cmake/toolchains/linux-native-x86_64.cmake', 'strip'),
    'docker': ('cmake/toolchains/linux-x86_64.cmake', 'x86_64-linux-gnu-strip'),
}

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def run(args: list[str | Path], **kwargs) -> None:
    subprocess.run([str(item) for item in args], check=True, **kwargs)

def container_path(path: Path, host: Host) -> str:
    """Path of ``path`` inside the container.

    POSIX hosts mount the repository at its own absolute path, so CMake caches
    stay valid and nothing needs translating. A Windows host path cannot exist
    in a Linux container, so the repository is mounted at ``CONTAINER_ROOT``.
    """
    if host.system != 'windows':
        return str(path)
    return str(CONTAINER_ROOT / path.resolve().relative_to(ROOT).as_posix())

def docker_image(host: Host | None = None) -> str:
    """Build (or reuse) the local cross image, tagged by its Dockerfile hash."""
    host = host or hostenv.current()
    tag = 'sc64-linux-cross:' + sha256(DOCKERFILE)[:16]
    present = subprocess.run(['docker', 'image', 'inspect', tag], capture_output=True).returncode == 0
    if not present:
        run(['docker', 'build', '--platform', host.docker_platform, '-t', tag, '-f', DOCKERFILE, DOCKERFILE.parent])
    return tag

def in_container(image: str, command: list[str], host: Host | None = None) -> list[str]:
    host = host or hostenv.current()
    user = ['--user', f'{os.getuid()}:{os.getgid()}'] if host.system != 'windows' else []
    return ['docker', 'run', '--rm', '--platform', host.docker_platform, *user,
            '-v', f'{ROOT}:{container_path(ROOT, host)}', '-w', container_path(ROOT, host), image, *command]

@dataclass(frozen=True)
class Runner:
    """Runs tools on the host, or inside the Linux cross image when ``image`` is set."""
    host: Host
    image: str | None = None

    def path(self, path: Path) -> str:
        return container_path(path, self.host) if self.image else str(path)

    def command(self, args: list[str | Path]) -> list[str]:
        words = [self.path(item) if isinstance(item, Path) else str(item) for item in args]
        return in_container(self.image, words, self.host) if self.image else words

    def run(self, args: list[str | Path], **kwargs) -> None:
        run(self.command(args), **kwargs)

    def output(self, args: list[str | Path]) -> str:
        return subprocess.run(self.command(args), check=True, capture_output=True, text=True).stdout

def audit(files: list[Path]) -> dict[str, str]:
    """Package members are authored resources and the binary: no private inputs."""
    result = {}
    for path in files:
        data = path.read_bytes()
        if path.suffix.lower() in PACKAGE_FORBIDDEN_SUFFIXES or data[:4] in frozenset(MAGIC_FORMATS):
            raise ValueError(f'Forbidden private input in package: {path.name}')
        result[path.name] = hashlib.sha256(data).hexdigest()
    return result

@dataclass(frozen=True)
class Plan:
    target: str
    mode: str  # mingw | native | docker
    toolchain: Path
    strip: str

def plan_for(target: str, host: Host, docker: bool = False) -> Plan:
    if target == 'windows':
        mode = 'mingw'
    elif host.linux_x86_64 and not docker:
        mode = 'native'
    else:
        mode = 'docker'
    toolchain, strip = TOOLCHAINS[mode]
    return Plan(target, mode, ROOT / toolchain, strip)

def configured_compiler(build: Path) -> str | None:
    """The C++ compiler CMake settled on, from its per-build detection file.

    Toolchain files set the compiler as a plain variable, so it is not in
    CMakeCache.txt; the path (inside the container, for Docker builds) is
    recorded in CMakeFiles/<cmake version>/CMakeCXXCompiler.cmake.
    """
    for detected in sorted(build.glob('CMakeFiles/*/CMakeCXXCompiler.cmake')):
        for line in detected.read_text(errors='replace').splitlines():
            if line.startswith('set(CMAKE_CXX_COMPILER "') and line.endswith('")'):
                return line[len('set(CMAKE_CXX_COMPILER "'):-2]
    return None

def tar_member(info: tarfile.TarInfo) -> tarfile.TarInfo:
    """Fixed modes, so an archive made on a host without POSIX permissions still runs."""
    info.mode = 0o755 if info.name.rsplit('/', 1)[-1] == TARGETS['linux']['binary'] else 0o644
    return info

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=tuple(TARGETS))
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--build-type', choices=bootstrap.BUILD_TYPES, default=bootstrap.DEFAULT_BUILD_TYPE,
                        help='CMake build type (default: %(default)s)')
    parser.add_argument('--docker', action='store_true',
                        help='Build the Linux package in the pinned Docker image even on a Linux x86_64 host')
    args = parser.parse_args()
    host = hostenv.current()
    plan = plan_for(args.target, host, args.docker)
    target = TARGETS[args.target]
    validate_ui_hooks(ROOT / 'local/generated/game')
    bootstrap.main()
    patch_shell_runtime.prepare(ROOT / 'local/deps/N64ModernRuntime', RUNTIME)
    tools = Runner(host, docker_image(host) if plan.mode == 'docker' else None)
    # A cache configured for one compiler cannot be reused with another, so the
    # host-compiler Linux build keeps its own tree beside the Docker one.
    build = ROOT / f'local/build/{args.target}{"-native" if plan.mode == "native" else ""}'
    configure = ['cmake', '-S', ROOT, '-B', build, '-G', 'Ninja',
                 f'-DCMAKE_TOOLCHAIN_FILE={tools.path(plan.toolchain)}', '-DSC64_BUILD_SHELL=ON',
                 '-DSC64_PACKAGED_BUILD=ON',
                 f'-DSC64_RUNTIME_DIR={tools.path(RUNTIME)}', f'-DCMAKE_BUILD_TYPE={args.build_type}',
                 # Empty when no icon was generated, which also clears a stale CMake cache entry.
                 f'-DSC64_ICON_RC={tools.path(icon) if (icon := installed_icon("StarCraft64.rc")) else ""}']
    tools.run(configure)
    if args.configure_only:
        return
    tools.run(['cmake', '--build', build, '--target', 'sc64_app', '-j', str(args.jobs)])
    dist = ROOT / 'local/dist'
    dist.mkdir(parents=True, exist_ok=True)
    package = dist / f'StarCraft64-{args.target}'
    with tempfile.TemporaryDirectory(prefix='.sc64-cross-', dir=dist) as temporary:
        staged = Path(temporary) / package.name
        staged.mkdir()
        binary = staged / target['binary']
        shutil.copy2(build / target['binary'], binary)
        tools.run([plan.strip, '--strip-debug', binary])
        shutil.copy2(bootstrap.FONT, staged / bootstrap.FONT.name)
        shutil.copy2(bootstrap.FONT_LICENSE, staged / 'LICENSE-DejaVu.txt')
        # A Linux executable cannot embed an icon; ship the PNG for launchers and desktop entries.
        if args.target == 'linux' and (icon := installed_icon('StarCraft64.png')):
            shutil.copy2(icon, staged / icon.name)
        members = sorted(staged.iterdir())
        compiler = configured_compiler(build)
        if compiler is None:
            raise RuntimeError(f'CMake did not record a C++ compiler under {build}')
        receipt = {'schema': 1, 'profile': target['profile'], 'build_type': args.build_type, 'cloud_upload': False,
                   'host': host.label, 'build_mode': plan.mode,
                   'binary_sha256': sha256(binary), 'sdl3_version': bootstrap.SDL_VERSION,
                   'sdl3_sha256': sha256(bootstrap.SDL_ARCHIVE), 'files_sha256': audit(members),
                   'toolchain': tools.output([compiler, '--version']).splitlines()[0],
                   'user_acceptance': False}
        archive = Path(temporary) / (package.name + target['archive'])
        if args.target == 'windows':
            with ZipFile(archive, 'w', ZIP_DEFLATED) as output:
                for member in members: output.write(member, f'{package.name}/{member.name}')
        else:
            with tarfile.open(archive, 'w:gz') as output:
                for member in members: output.add(member, f'{package.name}/{member.name}', filter=tar_member)
        receipt['archive_sha256'] = sha256(archive)
        # Publish the directory and archive only once both are complete.
        if package.exists(): shutil.rmtree(package)
        os.replace(staged, package)
        os.replace(archive, dist / archive.name)
    report = ROOT / f'local/reports/{args.target}/build.json'
    report.parent.mkdir(parents=True, exist_ok=True)
    report.write_text(json.dumps(receipt, indent=2) + '\n')
    print(report)
    print(dist / archive.name)

if __name__ == '__main__':
    main()
