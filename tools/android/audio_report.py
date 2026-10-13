#!/usr/bin/env python3
"""Collect bounded, app-scoped Android audio telemetry without restarting the game."""
from __future__ import annotations
import argparse
import hashlib
import json
import sys
import time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local

sys.path.insert(0, str(Path(__file__).resolve().parent))
from adb_client import PACKAGE, device_command, parse_pids, single_pid

ROOT = Path(__file__).resolve().parents[2]


def samples(log: str) -> list[dict]:
    result = []
    for line in log.splitlines():
        if 'SC64 {' not in line:
            continue
        try:
            # The logcat tag is also SC64; parse from the telemetry marker.
            result.append(json.loads(line[line.index('SC64 {') + 5:]))
        except json.JSONDecodeError:
            raise ValueError('Truncated/invalid SC64 telemetry; preserve the raw log')
    return result


def analyze(rows: list[dict]) -> dict:
    windows = []
    for a, b in zip(rows, rows[1:]):
        seconds = b['host_seconds'] - a['host_seconds']
        if seconds <= 0 or a.get('paused') or b.get('paused'):
            continue
        if not a.get('running') or not b.get('running'):
            continue
        def delta(key):
            if key not in a or key not in b or b[key] < a[key]:
                return None
            return b[key] - a[key]
        source = delta('audio_source_frames')
        source_nonzero = delta('audio_source_nonzero_frames')
        played = delta('audio_played_frames')
        nonzero = delta('audio_nonzero_frames')
        same_mode = a.get('ui_mode') == b.get('ui_mode')
        same_rate = a.get('audio_source_hz') == b.get('audio_source_hz')
        xruns = delta('audio_device_xruns') if a.get('audio_device_xruns', -1) >= 0 else None
        windows.append({
            'from_field': a['fields'], 'to_field': b['fields'], 'seconds': seconds,
            'mode': b.get('ui_mode'), 'same_mode': same_mode, 'same_rate': same_rate,
            'backend': b.get('audio_backend'), 'source_hz': b.get('audio_source_hz'),
            'source_frames_per_second': source / seconds if source is not None and same_rate else None,
            'source_silence_fraction': 1 - source_nonzero / source
                if source and source_nonzero is not None else None,
            'output_played_frames_per_second': played / seconds if played is not None else None,
            # Played frames exclude the inserted zeros of waiting/starved
            # callbacks; this ratio describes played PCM, not all device time.
            'played_output_silence_fraction': 1 - nonzero / played if played and nonzero is not None else None,
            'starvations': delta('audio_underruns'), 'discarded_frames': delta('audio_dropped_frames'),
            'hardware_xruns': xruns,
            'source_gap_ms': b.get('audio_source_gap_ms'),
            'callback_gap_ms': b.get('audio_callback_gap_ms'),
            'callback_work_ms': b.get('audio_callback_work_ms'),
            'target_ms': b.get('audio_target_ms'), 'buffer_ms': b.get('audio_buffer_ms'),
            'software_wall_ms': b.get('software_ms'),
            'snapshot_wall_ms': b.get('capture_snapshot_ms'),
            'interpret_wall_ms': b.get('capture_interpret_ms'),
        })
    if not windows:
        raise ValueError('At least two advancing, running/unpaused telemetry samples are required')
    return {'schema': 1, 'windows': windows,
            'scope': 'Observed host pipeline counters. Not hardware loopback or audible-quality acceptance; mixed UI/rate windows are marked.'}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument('--serial', help='Explicit connected adb device serial')
    source.add_argument('--log', type=Path, help='Analyze a previously collected app log')
    parser.add_argument('--adb', default='adb')
    parser.add_argument('--package', default=PACKAGE, help='Application ID of the running app (default: the personal APK)')
    parser.add_argument('--seconds', type=float, default=60)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = require_local(args.output, new=True, root=ROOT, what='Output')
    if not 20 <= args.seconds <= 300:
        raise ValueError('seconds must be in [20,300]')
    output.mkdir(parents=True)
    device = None
    if args.serial:
        adb = device_command(args.adb, args.serial)
        pid = single_pid(adb('shell', 'pidof', args.package), args.package)
        if pid is None:
            raise ValueError('Expected one running game process')
        device = {key: adb('shell', 'getprop', key) for key in (
            'ro.product.model', 'ro.build.version.release', 'ro.build.fingerprint', 'ro.product.cpu.abi')}
        device['page_size'] = int(adb('shell', 'getconf', 'PAGE_SIZE'))
        apk_path = adb('shell', 'pm', 'path', args.package)
        if not apk_path.startswith('package:') or '\n' in apk_path:
            raise ValueError('Expected one installed APK')
        device['installed_apk_sha256'] = adb('shell', 'sha256sum', apk_path[8:]).split()[0]
        before = adb('logcat', '-d', f'--pid={pid}')
        (output / 'engine-before.log').write_text(before + '\n')
        earlier = samples(before)
        first = earlier[-1] if earlier else None
        start = time.monotonic()
        while time.monotonic() - start < args.seconds:
            time.sleep(.25)
        final_pid = adb('shell', 'pidof', args.package)
        log = adb('logcat', '-d', f'--pid={pid}')
        (output / 'engine.log').write_text(log + '\n')
        if parse_pids(final_pid) != (pid,):
            raise ValueError('Game process changed during capture; preserve raw logs')
        rows = samples(log)
        if first:
            rows = [first] + [r for r in rows if r['host_seconds'] > first['host_seconds']]
        device.update(pid=pid, elapsed_seconds=time.monotonic() - start)
    else:
        log = args.log.read_text()
        (output / 'engine.log').write_text(log)
        rows = samples(log)
    report = analyze(rows)
    report['device'] = device
    report['log_sha256'] = hashlib.sha256((output / 'engine.log').read_bytes()).hexdigest()
    (output / 'audio.json').write_text(json.dumps(report, indent=2) + '\n')
    print(output / 'audio.json')


if __name__ == '__main__':
    main()
