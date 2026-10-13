"""Host detection and tool discovery shared by the build pipeline (ADR-082).

Build tools ask this module what machine they run on and where its tools live,
instead of hard-coding macOS paths. Discovery order for every tool: an explicit
environment override, then ``PATH``, then well-known install locations.
"""

from __future__ import annotations

import os
import platform
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

SYSTEMS = {'Darwin': 'darwin', 'Linux': 'linux', 'Windows': 'windows'}
ARCHES = {'arm64': 'arm64', 'aarch64': 'arm64', 'x86_64': 'x86_64', 'amd64': 'x86_64', 'x64': 'x86_64'}
NDK_TAGS = {'darwin': 'darwin-x86_64', 'linux': 'linux-x86_64', 'windows': 'windows-x86_64'}
MIN_PYTHON = (3, 10)
MIN_JDK = 21
MINGW_PREFIX = 'x86_64-w64-mingw32-'
# Homebrew keg-only formulae live under one of these prefixes (Apple Silicon, Intel).
BREW_OPT_DIRS = (Path('/opt/homebrew/opt'), Path('/usr/local/opt'))


class UnsupportedHost(SystemExit):
    """The host OS or architecture has no build profile."""


@dataclass(frozen=True)
class Host:
    system: str  # darwin | linux | windows
    arch: str  # arm64 | x86_64

    @property
    def label(self) -> str:
        return f'{self.system}-{self.arch}'

    @property
    def exe_suffix(self) -> str:
        return '.exe' if self.system == 'windows' else ''

    @property
    def script_suffix(self) -> str:
        """Suffix of Android build-tools launcher scripts (``d8``, ``apksigner``)."""
        return '.bat' if self.system == 'windows' else ''

    @property
    def ndk_tag(self) -> str:
        return NDK_TAGS[self.system]

    @property
    def docker_platform(self) -> str:
        """Image platform that runs natively on this host's Docker engine."""
        return 'linux/arm64' if self.arch == 'arm64' else 'linux/amd64'

    @property
    def linux_x86_64(self) -> bool:
        return self.system == 'linux' and self.arch == 'x86_64'


def current() -> Host:
    system = SYSTEMS.get(platform.system())
    arch = ARCHES.get(platform.machine().lower())
    if system is None or arch is None:
        raise UnsupportedHost(f'Unsupported build host: {platform.system()} {platform.machine()}. '
                              'Supported: macOS, Linux and Windows on arm64 or x86_64 '
                              '(on Windows use a native or MSYS2 MINGW64 Python, not MSYS/Cygwin).')
    return Host(system, arch)


def find_tool(*names: str, env: str | None = None, dirs: tuple[Path, ...] = ()) -> Path | None:
    """Locate an executable: ``$env`` override, ``PATH``, then ``dirs``. ``None`` when absent."""
    if env and os.environ.get(env):
        override = Path(os.environ[env])
        return override if override.is_file() else None
    for name in names:
        if found := shutil.which(name):
            return Path(found)
    for directory in dirs:
        for name in names:
            for candidate in (directory / name, directory / (name + '.exe')):
                if candidate.is_file():
                    return candidate
    return None


def brew_prefix(formula: str) -> Path | None:
    for opt in BREW_OPT_DIRS:
        if (opt / formula).is_dir():
            return opt / formula
    return None


def llvm_bin() -> Path | None:
    """Directory of the LLVM toolchain the macOS profile builds with.

    ``LLVM_BIN`` overrides; otherwise Homebrew's ``llvm`` formula. Other hosts
    compile with ``CC``/``CXX`` or the system compiler and do not need this.
    """
    if os.environ.get('LLVM_BIN'):
        override = Path(os.environ['LLVM_BIN'])
        return override if (override / 'clang++').is_file() else None
    prefix = brew_prefix('llvm')
    return prefix / 'bin' if prefix and (prefix / 'bin/clang++').is_file() else None


def android_sdk(host: Host | None = None) -> Path | None:
    """Android SDK root: ``ANDROID_SDK``, ``ANDROID_HOME``, ``ANDROID_SDK_ROOT``, then the IDE default."""
    host = host or current()
    for variable in ('ANDROID_SDK', 'ANDROID_HOME', 'ANDROID_SDK_ROOT'):
        if os.environ.get(variable):
            candidate = Path(os.environ[variable])
            return candidate if candidate.is_dir() else None
    defaults = {'darwin': Path.home() / 'Library/Android/sdk', 'linux': Path.home() / 'Android/Sdk',
                'windows': Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local')) / 'Android/Sdk'}
    return defaults[host.system] if defaults[host.system].is_dir() else None


def _jdk_major(home: Path, host: Host) -> int | None:
    try:
        text = subprocess.run([str(home / 'bin' / ('javac' + host.exe_suffix)), '-version'],
                              capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    if match := re.search(r'javac (\d+)', text):
        return int(match.group(1))
    return None


def jdk_home(host: Host | None = None) -> Path | None:
    """A JDK (``MIN_JDK`` or newer): ``JAVA_HOME``, Homebrew's openjdk, then ``javac`` on ``PATH``."""
    host = host or current()
    candidates = []
    if os.environ.get('JAVA_HOME'):
        candidates.append(Path(os.environ['JAVA_HOME']))
    for formula in ('openjdk@21', 'openjdk'):
        if keg := brew_prefix(formula):
            candidates.append(keg)
    if javac := find_tool('javac'):
        candidates.append(javac.resolve().parent.parent)
    for home in candidates:
        if (home / 'bin' / ('javac' + host.exe_suffix)).is_file() and (_jdk_major(home, host) or 0) >= MIN_JDK:
            return home
    return None


def venv_python(root: Path, host: Host | None = None) -> Path:
    host = host or current()
    return root / '.venv' / ('Scripts/python.exe' if host.system == 'windows' else 'bin/python')


def mingw_tool(name: str) -> Path | None:
    """A mingw-w64 x86_64 tool such as ``gcc``, ``g++``, ``windres`` or ``strip``."""
    return find_tool(MINGW_PREFIX + name)


def system_compiler() -> Path | None:
    """The C++ compiler a native (non-cross) build would pick: ``CXX``, else the usual names."""
    if os.environ.get('CXX'):
        return find_tool(os.environ['CXX'].split()[0])
    return find_tool('c++', 'g++', 'clang++')


@dataclass(frozen=True)
class Check:
    name: str
    path: str | None  # what was found; None when missing
    hint: str  # how to install it on this host

    @property
    def ok(self) -> bool:
        return self.path is not None


INSTALL = {
    'cmake': {'darwin': 'brew install cmake', 'linux': 'apt install cmake', 'windows': 'pacman -S mingw-w64-x86_64-cmake'},
    'ninja': {'darwin': 'brew install ninja', 'linux': 'apt install ninja-build', 'windows': 'pacman -S mingw-w64-x86_64-ninja'},
    'llvm': {'darwin': 'brew install llvm', 'linux': 'apt install clang', 'windows': 'pacman -S mingw-w64-x86_64-clang'},
    'compiler': {'darwin': 'xcode-select --install', 'linux': 'apt install g++ pkg-config',
                 'windows': 'pacman -S mingw-w64-x86_64-gcc'},
    'mingw': {'darwin': 'brew install mingw-w64', 'linux': 'apt install mingw-w64',
              'windows': 'pacman -S mingw-w64-x86_64-gcc'},
    'docker': {'darwin': 'install Docker Desktop', 'linux': 'apt install docker.io', 'windows': 'install Docker Desktop'},
    'numpy': {'darwin': 'pip install numpy', 'linux': 'pip install numpy', 'windows': 'pip install numpy'},
    'pillow': {'darwin': 'pip install pillow', 'linux': 'pip install pillow', 'windows': 'pip install pillow'},
}


JDK_HINT = {'darwin': 'brew install openjdk@21', 'linux': 'apt install openjdk-21-jdk-headless',
            'windows': 'pacman -S mingw-w64-x86_64-openjdk or install a JDK 21 and set JAVA_HOME'}


def _check(host: Host, key: str, name: str, found: Path | str | None) -> Check:
    return Check(name, str(found) if found else None, INSTALL[key][host.system])


def _module(name: str) -> str | None:
    import importlib.util
    return name if importlib.util.find_spec(name) else None


def common_requirements(host: Host) -> list[Check]:
    python_ok = sys.version_info >= MIN_PYTHON
    return [
        Check('python >= %d.%d' % MIN_PYTHON, sys.executable if python_ok else None, 'install Python 3.10 or newer'),
        _check(host, 'cmake', 'cmake', find_tool('cmake')),
        _check(host, 'ninja', 'ninja', find_tool('ninja')),
        _check(host, 'numpy', 'python numpy', _module('numpy')),
        _check(host, 'pillow', 'python pillow', _module('PIL')),
    ]


def android_requirements(host: Host, ndk_revision: str, build_tools: str) -> list[Check]:
    sdk = android_sdk(host)
    jdk = jdk_home(host)
    ndk = sdk / 'ndk' / ndk_revision if sdk else None
    tools = sdk / 'build-tools' / build_tools if sdk else None
    return [
        Check('Android SDK (ANDROID_SDK / ANDROID_HOME)', str(sdk) if sdk else None,
              'install Android Studio or the command-line tools and set ANDROID_SDK'),
        Check(f'NDK {ndk_revision}', str(ndk) if ndk and ndk.is_dir() else None,
              f'sdkmanager "ndk;{ndk_revision}"'),
        Check(f'build-tools {build_tools}', str(tools) if tools and tools.is_dir() else None,
              f'sdkmanager "build-tools;{build_tools}"'),
        Check(f'JDK {MIN_JDK}+ (JAVA_HOME)', str(jdk) if jdk else None, JDK_HINT[host.system]),
    ]


def desktop_requirements(host: Host) -> list[Check]:
    """Tools for the package that matches this host (what ``sc64.py build`` makes)."""
    if host.system == 'darwin':
        return [_check(host, 'llvm', 'Homebrew LLVM clang++ (or LLVM_BIN)', llvm_bin())]
    if host.system == 'windows':
        return windows_requirements(host)
    return [_check(host, 'compiler', 'C++ compiler (CXX, c++, g++ or clang++)', system_compiler())]


def windows_requirements(host: Host) -> list[Check]:
    return [_check(host, 'mingw', MINGW_PREFIX + tool, mingw_tool(tool)) for tool in ('g++', 'windres', 'strip')]


def linux_requirements(host: Host) -> list[Check]:
    if host.linux_x86_64:
        return [_check(host, 'compiler', 'C++ compiler (CXX, c++, g++ or clang++)', system_compiler()),
                _check(host, 'compiler', 'strip', find_tool('strip'))]
    return [_check(host, 'docker', 'docker (builds the x86_64 Linux package in a local image)', find_tool('docker'))]
