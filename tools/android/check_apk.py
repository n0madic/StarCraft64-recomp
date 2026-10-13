#!/usr/bin/env python3
"""Audit arm64 APK content and ELF/ZIP alignment without claiming device proof."""
from __future__ import annotations
import argparse
import hashlib
import json
import struct
from pathlib import Path
import sys
from zipfile import ZipFile, ZIP_STORED

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import PACKAGE_FORBIDDEN_SUFFIXES
from rom_info import MAGIC_FORMATS, NTSC_U_SHA1 as ROM_SHA1

ROM_MAGIC = frozenset(MAGIC_FORMATS)
ENGINE_MEMBER = 'lib/arm64-v8a/libmain.so'
ROM_MEMBER = 'assets/sc64/sc64-ntsc-u.z64'
ROM_SIZE = 0x2000000
# Names only the recompiled game defines (its entry point and the minimap setter, game_static.cpp). The engine is
# linked with --strip-debug, which keeps them in the symbol table of a build that contains the game.
GAME_CODE_MARKERS = (b'recomp_entrypoint', b'sc64_resident_8000B988')

def validate_bundled_rom(payload: bytes) -> str:
    if len(payload) != ROM_SIZE or payload[:4] != b'\x80\x37\x12\x40':
        raise ValueError('Bundled ROM must be a normalized 32 MiB z64 image')
    digest = hashlib.sha1(payload).hexdigest()
    if digest != ROM_SHA1:
        raise ValueError('Bundled ROM SHA-1 does not match the researched NTSC-U revision')
    return digest

def elf_segments(data: bytes) -> list[dict]:
    if len(data) < 64 or data[:6] != b'\x7fELF\x02\x01':
        raise ValueError('Expected a little-endian ELF64 library')
    if struct.unpack_from('<H', data, 18)[0] != 183:
        raise ValueError('ELF is not AArch64')
    offset = struct.unpack_from('<Q', data, 32)[0]
    size, count = struct.unpack_from('<HH', data, 54)
    if size != 56 or not count or offset + size * count > len(data):
        raise ValueError('Malformed ELF program headers')
    segments = []
    for i in range(count):
        kind, flags, start, address, _, file_size, memory_size, alignment = struct.unpack_from(
            '<IIQQQQQQ', data, offset + i * size)
        if kind == 1:
            if alignment < 16384 or alignment & (alignment - 1) or (address - start) % alignment:
                raise ValueError('ELF LOAD segment does not support 16 KiB alignment')
            if start + file_size > len(data): raise ValueError('Truncated ELF LOAD segment')
            segments.append({'kind': 'LOAD', 'offset': start, 'address': address,
                             'alignment': alignment, 'flags': flags})
        elif kind == 0x6474e552:
            if (address + memory_size) % 16384:
                raise ValueError('ELF RELRO end is not 16 KiB aligned')
            segments.append({'kind': 'RELRO', 'end': address + memory_size})
    if not any(item['kind'] == 'LOAD' for item in segments): raise ValueError('No ELF LOAD segments')
    return segments

def contains_game_code(payload: bytes) -> bool:
    return any(marker in payload for marker in GAME_CODE_MARKERS)

def audit(apk: Path, engine: Path | None = None, *, bundled_rom: bool = False, launcher: bool = False) -> dict:
    """`launcher` asserts the APK of ADR-090: no ROM and no recompiled game linked into its libraries."""
    if launcher and bundled_rom:
        raise ValueError('The launcher APK never contains a ROM')
    libraries = {}
    game_code = False
    with ZipFile(apk) as archive, apk.open('rb') as raw:
        if len(set(archive.namelist())) != len(archive.namelist()): raise ValueError('Duplicate APK members')
        for item in archive.infolist():
            name = item.filename
            payload = archive.read(item)
            if bundled_rom and name == ROM_MEMBER:
                validate_bundled_rom(payload)
                continue
            if Path(name).suffix.lower() in PACKAGE_FORBIDDEN_SUFFIXES or payload[:4] in ROM_MAGIC:
                raise ValueError(f'Forbidden private input in APK: {name}')
            if not name.endswith('.so'): continue
            if not name.startswith('lib/arm64-v8a/'): raise ValueError(f'Unexpected ABI: {name}')
            segments = elf_segments(payload)
            raw.seek(item.header_offset)
            header = raw.read(30)
            signature, = struct.unpack_from('<I', header)
            if signature != 0x04034b50: raise ValueError('Malformed ZIP local header')
            namesize, extrasize = struct.unpack_from('<HH', header, 26)
            data_offset = item.header_offset + 30 + namesize + extrasize
            if item.compress_type == ZIP_STORED and data_offset % 16384:
                raise ValueError(f'Uncompressed ELF is not 16 KiB ZIP aligned: {name}')
            if contains_game_code(payload):
                game_code = True
                if launcher: raise ValueError(f'The launcher APK links the recompiled game: {name}')
            libraries[name] = {'sha256': hashlib.sha256(payload).hexdigest(), 'segments': segments,
                               'compressed': item.compress_type != ZIP_STORED, 'data_offset': data_offset}
        if ENGINE_MEMBER not in libraries: raise ValueError(f'APK lacks the game engine: {ENGINE_MEMBER}')
        if 'classes.dex' not in archive.namelist(): raise ValueError('APK lacks the SDL activity layer')
        if bundled_rom and ROM_MEMBER not in archive.namelist():
            raise ValueError('APK lacks the requested bundled ROM')
    if engine and hashlib.sha256(engine.read_bytes()).hexdigest() != libraries[ENGINE_MEMBER]['sha256']:
        raise ValueError('APK engine does not match the local link')
    return {'schema': 2, 'apk_sha256': hashlib.sha256(apk.read_bytes()).hexdigest(),
            'libraries': libraries, 'alignment_16k': True, 'rom_included': bundled_rom,
            'game_code_linked': game_code, 'launcher': launcher,
            'rom_sha1': ROM_SHA1 if bundled_rom else None,
            'on_device_accepted': False}

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('apk', type=Path)
    parser.add_argument('--engine', type=Path)
    parser.add_argument('--bundled-rom', action='store_true', help='Require the single verified personal ROM asset')
    parser.add_argument('--launcher', action='store_true', help='Require an APK without ROM and without the recompiled game (ADR-090)')
    args = parser.parse_args()
    print(json.dumps(audit(args.apk, args.engine, bundled_rom=args.bundled_rom, launcher=args.launcher), indent=2))

if __name__ == '__main__': main()
