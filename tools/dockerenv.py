"""Run Linux tools from any host in local Docker images (stdlib only).

Shared by the Linux package build and the libultra inventory chain. Images are
built locally from a Dockerfile in this repository and tagged by its hash; nothing
is pushed or built remotely. POSIX hosts mount the repository at its own absolute
path, so CMake caches and printed paths stay valid; a Windows host path cannot
exist in a Linux container, so the repository is mounted at ``CONTAINER_ROOT``.
"""

from __future__ import annotations

import hashlib
import os
import subprocess
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

import hostenv
from hostenv import Host

ROOT = Path(__file__).resolve().parents[1]
CONTAINER_ROOT = PurePosixPath('/work')


def container_path(path: Path, host: Host) -> str:
    """Path of ``path`` (inside the repository) as seen inside the container."""
    if host.system != 'windows':
        return str(path)
    return str(CONTAINER_ROOT / path.resolve().relative_to(ROOT).as_posix())


def base_image(dockerfile: Path) -> str:
    """The pinned base image named by the first FROM line of ``dockerfile``."""
    for line in dockerfile.read_text().splitlines():
        if line.startswith('FROM '):
            return line.split()[1]
    raise ValueError(f'No FROM line in {dockerfile}')


def ensure_image(repository: str, dockerfile: Path, host: Host | None = None, platform: str | None = None) -> str:
    """Build (or reuse) a local image of ``dockerfile``, tagged by the file's hash.

    The tag is unchanged for the host's own platform; another platform (to run a
    foreign-architecture binary) gets its architecture appended.
    """
    host = host or hostenv.current()
    platform = platform or host.docker_platform
    tag = f'{repository}:{hashlib.sha256(dockerfile.read_bytes()).hexdigest()[:16]}'
    if platform != host.docker_platform:
        tag += '-' + platform.split('/')[1]
    present = subprocess.run(['docker', 'image', 'inspect', tag], capture_output=True).returncode == 0
    if not present:
        subprocess.run(['docker', 'build', '--platform', platform, '-t', tag, '-f', str(dockerfile), str(dockerfile.parent)],
                       check=True)
    return tag


def in_container(image: str, command: list[str], host: Host | None = None, platform: str | None = None,
                 mounts: tuple[str, ...] = ()) -> list[str]:
    """``docker run`` line; ``mounts`` are extra ``source:target[:ro]`` volume specs."""
    host = host or hostenv.current()
    user = ['--user', f'{os.getuid()}:{os.getgid()}'] if host.system != 'windows' else []
    volumes = [item for mount in (f'{ROOT}:{container_path(ROOT, host)}', *mounts) for item in ('-v', mount)]
    return ['docker', 'run', '--rm', '--platform', platform or host.docker_platform, *user, *volumes,
            '-w', container_path(ROOT, host), image, *command]


@dataclass(frozen=True)
class Runner:
    """Runs tools on the host, or inside a local image when ``image`` is set."""
    host: Host
    image: str | None = None
    platform: str | None = None
    mounts: tuple[str, ...] = ()

    def path(self, path: Path) -> str:
        return container_path(path, self.host) if self.image else str(path)

    def command(self, args: list[str | Path]) -> list[str]:
        words = [self.path(item) if isinstance(item, Path) else str(item) for item in args]
        if self.image:
            return in_container(self.image, words, self.host, self.platform, self.mounts)
        return words

    def run(self, args: list[str | Path], **kwargs) -> None:
        subprocess.run(self.command(args), check=True, **kwargs)

    def output(self, args: list[str | Path]) -> str:
        return subprocess.run(self.command(args), check=True, capture_output=True, text=True).stdout
