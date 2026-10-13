#!/usr/bin/env python3
"""Recheck the private Stage 0 corpus; this does not certify a native port."""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local
from rom_info import NTSC_U_SHA1 as ROM_SHA1, normalize_rom
from summarize_capture import summarize
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'research_libultra'))
import accepted

UCODE_SHA1 = '7d0f686719afde0703d377ccc7a492318c01eec0'
SCENES = {
    'reference-cold': [2, 5, 8],
    'mission-01': [0, 7],
    'mission-02': [3, 5],
    'game-1p-01': [3],
    'game-1p-02': [2, 4],
    'menu-2p-01': [4],
    'game-2p-01': [2],
    'intro-decode': [2],
    'reference-4mb': [8],
    'save-game-02': [1],
    'save-restart': [10],
    'save-loaded': [2],
    'reference-2p-fields': [0],
    'cold-saved-mission-02': [13],
}


def sha256(path: Path) -> str:
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256') if hasattr(hashlib, 'file_digest') else None
        if digest is None:
            digest = hashlib.sha256()
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
    return digest.hexdigest()


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def evaluate(root: Path, rom: bytes) -> dict:
    require(hashlib.sha1(rom).hexdigest() == ROM_SHA1, 'unexpected normalized ROM')
    captures, summaries, files, protocols = root / 'local/captures/research', {}, {}, {}
    recipes = json.loads(Path(__file__).with_name('recipes.json').read_text())['recipes']
    for name, steps in SCENES.items():
        directory = captures / name
        expected_ram = 0x400000 if name == 'reference-4mb' else 0x800000
        require((directory / 'end.rdram.bin').stat().st_size == expected_ram,
                f'{name}: missing full configured RAM snapshot')
        inputs = json.loads((directory / 'input.json').read_text())
        require(inputs == recipes[name]['actions'], f'{name}: controller recipe changed')
        with (directory / 'events.jsonl').open() as stream:
            session = json.loads(next(stream))
            require(session.get('homebrew') is False and session['ram_bytes'] == expected_ram,
                    f'{name}: wrong original-ROM/memory profile')
            commands = Counter(json.loads(line)['command'] >> 24 for line in stream
                               if '"event":"flash"' in line)
        protocols[name] = dict(sorted(commands.items()))
        summary = summarize(directory, rom)
        require(summary['rdp_incomplete_u64_words'] == 0, f'{name}: incomplete RDP stream')
        require(summary['sp_tasks'], f'{name}: no task descriptors')
        if name not in ('reference-4mb', 'save-game-02', 'save-restart'):
            require(summary['gbi_lists'], f'{name}: no bounded decoded task snapshots')
        require(all('error' not in row for row in summary['gbi_lists']), f'{name}: undecoded GBI')
        require(all(task['type'] == 1 and task['ucode_sha1'] == UCODE_SHA1
                    for task in summary['sp_tasks']), f'{name}: unexpected RSP workload')
        require(not any(key.startswith(('INVALID_', 'TRI_')) for key in summary['rdp_histogram']),
                f'{name}: new RDP command family requires review')
        require(set(summary['cycle_types']) <= {0, 2}, f'{name}: new cycle mode')
        selected = [directory / f'step-{step}.png' for step in steps]
        if name in ('reference-2p-fields', 'cold-saved-mission-02'):
            step = steps[0]
            selected += [directory / f'step-{step}-field-{i}.png' for i in range(24)]
            require(summary['event_counts'].get('vi_registers', 0) == summary['event_counts']['vi'],
                    f'{name}: missing full VI registers')
        selected += [directory / f for f in ('input.json', 'events.jsonl', 'end.rdram.bin',
                                             'end.flash.bin', 'audio.wav')]
        for file in selected:
            require(file.is_file() and file.stat().st_size > 0, f'missing evidence: {file}')
            if file.suffix == '.png':
                header = file.read_bytes()[:24]
                require(header[:8] == b'\x89PNG\r\n\x1a\n', f'bad PNG: {file}')
                require(all(struct.unpack('>II', header[16:24])), f'empty PNG: {file}')
            files[str(file.relative_to(root))] = dict(bytes=file.stat().st_size, sha256=sha256(file))
        summaries[name] = summary

    def has_background(name: str, width: int, height: int) -> bool:
        return any(obj['opcode'] == 'BG_COPY' and obj['format'] == 2 and obj['size'] == 1
                   and obj['image_width_10_2'] == width * 4
                   and obj['image_height_10_2'] == height * 4
                   for row in summaries[name]['gbi_lists'] for obj in row['objects'])

    require(has_background('cold-saved-mission-02', 320, 240), 'missing CPU CI8 1P copy')
    require(has_background('reference-2p-fields', 512, 384), 'missing CPU CI8 2P copy')
    for name, width, serrate in [('reference-cold', 640, 1),
                                 ('cold-saved-mission-02', 320, 0),
                                 ('reference-2p-fields', 512, 1)]:
        require(any(v['width'] == width and v['serrate'] == serrate
                    for v in summaries[name]['vi_modes']), f'{name}: missing VI mode')

    overlays = []
    for name, start, end in [('reference-cold', 0xD6640, 0xE38E0),
                             ('game-2p-01', 0xE38E0, 0x12CA10)]:
        ram = (captures / name / 'end.rdram.bin').read_bytes()
        payload = rom[start:end]
        require(payload == ram[0x11FBF0:0x11FBF0 + len(payload)],
                f'{name}: overlay no longer equals entire ROM payload')
        overlays.append(dict(rom_start=start, rom_end=end, physical_start=0x11FBF0,
                             bytes=len(payload), sha256=hashlib.sha256(payload).hexdigest()))

    require({0x4B, 0x78, 0xB4, 0xA5} <= set(protocols['save-game-02']),
            'original save lacks Flash erase/program protocol')
    require({0xE1, 0xF0, 0xD2} <= set(protocols['cold-saved-mission-02']),
            'cold restart lacks Flash status/read protocol')
    require(not ({0x4B, 0x78, 0xA5} & set(protocols['cold-saved-mission-02'])),
            'cold reload rewrote battery state unexpectedly')
    require(recipes['cold-saved-mission-02']['state'] == 'none', 'cold replay must not load a savestate')
    saved = captures / 'save-game-02/end.flash.bin'
    cold = captures / 'cold-saved-mission-02/end.flash.bin'
    require(saved.stat().st_size == 131072 and saved.read_bytes() == cold.read_bytes(),
            'cold restart did not preserve the exact 128 KiB saved Flash image')
    require(sha256(saved) == 'c4170148c8133ad5308d7404f47fd1d60d0317752bd9f21361945e346202f66e',
            'saved-mission Flash identity changed; original-UI review required')
    require(summaries['cold-saved-mission-02']['event_counts']['ai_dma'] > 0,
            'cold mission lacks audio DMA')
    # Negative feedback evidence is bounded by the recorded interpreter windows.
    for name in ('cold-saved-mission-02', 'reference-2p-fields'):
        require(summaries[name]['event_counts'].get('cpu_access', 0) > 0,
                f'{name}: no CPU attribution window')
        require(summaries[name]['event_counts'].get('cpu_rdp_read', 0) == 0,
                f'{name}: new CPU consumption of RDP-owned bytes needs analysis')

    inventory_file = root / 'local/reports/libultra-resident/inventory.json'
    inventory = json.loads(inventory_file.read_text())
    require(inventory['rom_sha1'] == ROM_SHA1, 'libultra ROM mismatch')
    require(inventory['mapping']['scan_end'] == accepted.RESIDENT_SCAN_END, 'libultra scan spills into overlay')
    require(len(inventory['reference_matches']) == accepted.ACCEPTED_REFERENCE_MATCHES, 'libultra reference count changed')
    require(all(row['ram_bytes_equal'] for row in inventory['reference_matches']),
            'libultra loaded body mismatch')
    require(len(inventory['masked_crc_hits']) == accepted.ACCEPTED_CRC_HITS and
            all(row['ram_bytes_equal'] for row in inventory['masked_crc_hits']), 'CRC inventory changed')
    require(len(accepted.replacement_names(inventory)) == accepted.ACCEPTED_REPLACEMENT_NAMES,
            'replacement inventory changed')
    bolt_file = root / 'local/reports/research/bolt-audit.json'
    bolt = json.loads(bolt_file.read_text())
    require(bolt['rom_sha1'] == ROM_SHA1 and bolt['directory_count'] == 23
            and bolt['leaf_count'] == 2111 and bolt['decoded_bytes'] == 61833578,
            'BOLT census changed')
    require(all(row['reference_equal'] for row in bolt['leaves']), 'BOLT comparison incomplete')
    for file in (inventory_file, bolt_file):
        files[str(file.relative_to(root))] = dict(bytes=file.stat().st_size, sha256=sha256(file))
    regressions = {}
    for mode, expected in [('fragment', 1), ('native-word', 1), ('byte-array', 0)]:
        result = subprocess.check_output([str(root / 'local/toolchains/rdp-adapter-probe'), mode], text=True)
        require(result.strip() == f'{mode} sync_full_interrupts={expected}', 'RDP adapter regression failed')
        regressions[mode] = result.strip()
    sources = {str(p.relative_to(root)): sha256(p)
               for p in sorted((root / 'tools/research').glob('*')) if p.is_file()}
    return dict(schema=1, status='PASS', scope='Original-ROM Stage 0 corpus; manual scene review in docs/research.md',
                rom_sha1=ROM_SHA1, ucode_sha1=UCODE_SHA1, overlays=overlays,
                reference_regressions=regressions, flash_command_counts=protocols,
                source_sha256=sources, evidence=files,
                scenes={name: {k: summary[k] for k in ('event_counts', 'rdp_histogram', 'cycle_types',
                                                       'vi_modes', 'window_cpu_access_bytes')}
                        for name, summary in summaries.items()},
                limitations=['No native recompiled game, shell, Android or complete-campaign proof.',
                             'PNG content was reviewed separately; existence/hashes are not visual parity.',
                             'No observed audio RSP or GPU feedback applies only to the sampled corpus.',
                             'Stage 2 must pair pre-task memory and post-task VI/reference fields explicitly.'])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rom', type=Path)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        output = require_local(args.output, new=True, root=root, what='Acceptance evidence')
        _, rom = normalize_rom(args.rom.read_bytes())
        result = evaluate(root, rom)
        with output.open('x') as stream:
            json.dump(result, stream, indent=2)
            stream.write('\n')
        print(f"PASS: {len(result['scenes'])} original-ROM captures; {len(result['evidence'])} evidence files")
        return 0
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f'Stage 0 acceptance failed: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
