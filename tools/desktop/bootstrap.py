#!/usr/bin/env python3
"""Fetch only pinned public shell dependencies into ignored local/toolchains."""
from __future__ import annotations
import hashlib
import logging
import os
import tarfile
import tempfile
import urllib.request
from pathlib import Path
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
TOOLCHAINS = ROOT / 'local/toolchains'
log = logging.getLogger('bootstrap')
SDL_VERSION = '3.4.18'
MINIAUDIO_VERSION = '0.11.23'
# Shipped builds are optimized release builds; the others are opt-in for debugging.
BUILD_TYPES = ('Release', 'RelWithDebInfo', 'Debug')
DEFAULT_BUILD_TYPE = BUILD_TYPES[0]
MINIAUDIO = TOOLCHAINS / 'miniaudio.h'
STB_TRUETYPE = TOOLCHAINS / 'stb/stb_truetype.h'
SDL_ARCHIVE = TOOLCHAINS / f'sdl3/SDL3-{SDL_VERSION}.tar.gz'
SDL_DIR = TOOLCHAINS / f'sdl3/SDL3-{SDL_VERSION}'
STB_COMMIT = '2c980bb59875b0d32144a71867fbdebb2f77cd20'
FONT_ARCHIVE = TOOLCHAINS / 'dejavu/dejavu-fonts-ttf-2.37.zip'
FONT = TOOLCHAINS / 'dejavu/DejaVuSansMono-Bold.ttf'
FONT_LICENSE = TOOLCHAINS / 'dejavu/LICENSE'
APPIMAGE_RUNTIME_TAG = '20251108'
APPIMAGE_RUNTIME = TOOLCHAINS / f'appimage/runtime-x86_64-{APPIMAGE_RUNTIME_TAG}'
APPIMAGE_RUNTIME_URL = f'https://github.com/AppImage/type2-runtime/releases/download/{APPIMAGE_RUNTIME_TAG}/runtime-x86_64'
APPIMAGE_RUNTIME_SHA256 = '2fca8b443c92510f1483a883f60061ad09b46b978b2631c807cd873a47ec260d'
FILES = (
    (SDL_ARCHIVE, f'https://github.com/libsdl-org/SDL/releases/download/release-{SDL_VERSION}/SDL3-{SDL_VERSION}.tar.gz',
     '9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3'),
    (STB_TRUETYPE,
     f'https://raw.githubusercontent.com/nothings/stb/{STB_COMMIT}/stb_truetype.h',
     'ecd30b05e0dd4fea3a13c26810dd9e1992dc379049482c393d5a19e6b5090aab'),
    (FONT_ARCHIVE,
     'https://github.com/dejavu-fonts/dejavu-fonts/releases/download/version_2_37/dejavu-fonts-ttf-2.37.zip',
     '7576310b219e04159d35ff61dd4a4ec4cdba4f35c00e002a136f00e96a908b0a'),
    (MINIAUDIO, f'https://raw.githubusercontent.com/mackron/miniaudio/{MINIAUDIO_VERSION}/miniaudio.h',
     '7e4f3f13c8fe66df2080ac3dd12a89193e3c2463cb7f067c798abd7331cd8ee6'),
    (APPIMAGE_RUNTIME, APPIMAGE_RUNTIME_URL, APPIMAGE_RUNTIME_SHA256),
)

def digest(path: Path) -> str:
    hash_ = hashlib.sha256()
    with path.open('rb') as file:
        for chunk in iter(lambda: file.read(1024 * 1024), b''):
            hash_.update(chunk)
    return hash_.hexdigest()

def fetch(path: Path, url: str, expected: str) -> None:
    if path.exists():
        if digest(path) != expected:
            raise ValueError(f'Existing public dependency differs from the pin: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.download-', dir=path.parent) as directory:
        temporary = Path(directory) / 'payload'
        log.info('Downloading %s', url)
        with urllib.request.urlopen(url, timeout=60) as response, temporary.open('wb') as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        if digest(temporary) != expected:
            raise ValueError(f'Public dependency hash mismatch: {url}')
        os.replace(temporary, path)

def write_atomic(target: Path, payload: bytes) -> None:
    """Publish ``payload`` at ``target`` only once complete; never leave a partial file."""
    target.parent.mkdir(parents=True, exist_ok=True)
    descriptor, name = tempfile.mkstemp(prefix=f'.{target.name}.', suffix='.partial', dir=target.parent)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'wb') as file:
            file.write(payload)
        os.replace(temporary, target)
    except BaseException:
        temporary.unlink(missing_ok=True)
        raise

def publish(target: Path, payload: bytes) -> None:
    if target.exists():
        if target.read_bytes() != payload:
            raise ValueError(f'Extracted dependency drift: {target}')
        return
    write_atomic(target, payload)

def extract_sdl(archive_path: Path, output: Path) -> None:
    """Extract regular files only; reject links and paths escaping the cache."""
    base = output.parent.resolve()
    with tarfile.open(archive_path) as archive:
        for member in archive.getmembers():
            if member.isdir():
                continue
            if not member.isfile():
                raise ValueError(f'SDL archive member is not a regular file: {member.name}')
            target = (base / member.name).resolve()
            if not target.is_relative_to(output.resolve()):
                raise ValueError(f'SDL archive member escapes its directory: {member.name}')
            source = archive.extractfile(member)
            assert source is not None
            publish(target, source.read())

def extract_font(archive_path: Path) -> None:
    with ZipFile(archive_path) as archive:
        publish(FONT, archive.read('dejavu-fonts-ttf-2.37/ttf/DejaVuSansMono-Bold.ttf'))
        publish(FONT_LICENSE, archive.read('dejavu-fonts-ttf-2.37/LICENSE'))

def main() -> None:
    if not logging.getLogger().handlers:
        logging.basicConfig(level=logging.INFO, format='%(message)s')
    for path, url, expected in FILES:
        fetch(path, url, expected)
    extract_sdl(SDL_ARCHIVE, SDL_DIR)
    extract_font(FONT_ARCHIVE)
    print(SDL_DIR)

if __name__ == '__main__':
    main()
