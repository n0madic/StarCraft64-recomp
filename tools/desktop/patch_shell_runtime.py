#!/usr/bin/env python3
"""Prepare a separate, guarded shell runtime from the Stage1 host patch."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import shutil
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MARKER = 'sc64-shell-patch.json'

def tree_hashes(directory: Path) -> dict[str, str]:
    result = {}
    for base, dirs, files in os.walk(directory):
        dirs[:] = sorted(name for name in dirs if name not in {'.git', 'build'})
        for name in dirs + files:
            if (Path(base) / name).is_symlink():
                raise ValueError(f'Runtime tree contains a symbolic link: {Path(base) / name}')
        for name in sorted(files):
            if name in {'.git', 'build', MARKER}: continue
            path = Path(base) / name
            result[path.relative_to(directory).as_posix()] = digest(path.read_bytes())
    return result

def prepare(source: Path, output: Path) -> Path:
    source_hashes = tree_hashes(source)
    host = json.loads((source / 'sc64-host-patch.json').read_text())
    baseline = host.get('patched_sha256', host)
    for name, expected in baseline.items():
        if source_hashes.get(name) != expected:
            raise ValueError(f'Stage1 source drift: {name}')
    names = ['ultramodern/src/events.cpp', 'ultramodern/src/timer.cpp', 'ultramodern/src/scheduling.cpp',
             'librecomp/src/recomp.cpp']
    changes = {}
    for name in names:
        data = (source / name).read_bytes()
        expected = baseline.get(name)
        if expected is None:
            expected = digest((ROOT / 'third_party/N64ModernRuntime' / name).read_bytes())
        if digest(data) != expected:
            raise ValueError(f'Stage1 source drift: {name}')
        changes[name] = transform(name, data.decode()).encode()
    desired = dict(source_hashes)
    desired.update({name: digest(data) for name, data in changes.items()})
    marker = output / MARKER
    if output.exists():
        old = json.loads(marker.read_text())
        current = tree_hashes(output)
        if old.get('schema') == 2:
            if current != old['tree_sha256']:
                raise ValueError('Refusing unrelated changes in the shell runtime tree')
        else:
            for name, expected in old['patched_sha256'].items():
                if current.get(name) != expected:
                    raise ValueError(f'Refusing unrelated shell changes: {name}')
        if current == desired and old.get('source_sha256') == source_hashes:
            return marker
    manifest = {'schema': 2, 'source': str(source), 'source_sha256': source_hashes,
                'tree_sha256': desired, 'patched_sha256': {name: digest(data) for name, data in changes.items()}}
    output.parent.mkdir(parents=True, exist_ok=True)
    # Publish a complete snapshot. Preserve the old tree, including files whose
    # ownership a legacy three-file marker could not establish, for recovery.
    with tempfile.TemporaryDirectory(prefix='.sc64-runtime-', dir=output.parent) as temporary:
        staged = Path(temporary) / 'runtime'
        shutil.copytree(source, staged, ignore=shutil.ignore_patterns('.git', 'build', MARKER))
        for name, data in changes.items(): (staged / name).write_bytes(data)
        if tree_hashes(source) != source_hashes or tree_hashes(staged) != desired:
            raise ValueError('Runtime source changed during preparation')
        (staged / MARKER).write_text(json.dumps(manifest, indent=2) + '\n')
        previous = output.parent / (Path(temporary).name + '-previous')
        if output.exists(): os.replace(output, previous)
        try: os.replace(staged, output)
        except BaseException:
            if previous.exists(): os.replace(previous, output)
            raise
    return marker

def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def once(text: str, before: str, after: str) -> str:
    if text.count(before) != 1:
        raise ValueError(f"Runtime source drift: {before!r}")
    return text.replace(before, after, 1)

def transform(name: str, text: str) -> str:
    if name == 'librecomp/src/recomp.cpp':
        return once(text, 'bool recomp::load_stored_rom(const std::u8string& game_id) {',
                    'extern bool sc64_rom_is_preloaded(const std::u8string&);\n'
                    'bool recomp::load_stored_rom(const std::u8string& game_id) {\n'
                    '    if (sc64_rom_is_preloaded(game_id)) return true;')
    text = '#include "clock_bridge.hpp"\n' + text
    if name.endswith("scheduling.cpp"):
        return once(text, 'void ultramodern::check_running_queue(RDRAM_ARG1) {',
                    'void ultramodern::check_running_queue(RDRAM_ARG1) {\n    sc64_pause_point();')
    if name.endswith("events.cpp"):
        text = once(text, '    while (!exited) {', '    while (!exited) {\n        sc64_pause_point();\n        if (exited) break;')
        # total_vis is unsigned. Without a signed duration, an overdue `next`
        # wraps in the upstream clock-jump check, which then skips every wait.
        text = once(text, '(total_vis * 1000000us) / (60 * ultramodern::get_speed_multiplier())',
                    '(static_cast<int64_t>(total_vis) * 1000000us) / '
                    '(60 * static_cast<int64_t>(ultramodern::get_speed_multiplier()))')
        text = once(text, '        ultramodern::sleep_until(next);',
                    '        auto deadline = std::chrono::high_resolution_clock::time_point(\n'
                    '            std::chrono::duration_cast<std::chrono::high_resolution_clock::duration>(next.time_since_epoch()));\n'
                    '        auto delay = deadline - sc64_runtime_now();\n'
                    '        while (delay.count() > 0) {\n'
                    '            std::this_thread::sleep_for(std::chrono::ceil<std::chrono::milliseconds>(delay));\n'
                    '            sc64_pause_point();\n'
                    '            if (exited) break;\n'
                    '            delay = deadline - sc64_runtime_now();\n'
                    '        }\n'
                    '        sc64_pause_point();\n        if (exited) break;')
        # Own CPU-written framebuffer bytes when the guest declares them complete.
        text = once(text, 'extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {\n'
                    '    std::lock_guard lock{ events_context.message_mutex };',
                    'extern "C" void sc64_vi_swap(uint8_t*, int32_t);\n\n'
                    'extern "C" void osViSwapBuffer(RDRAM_ARG PTR(void) frameBufPtr) {\n'
                    '    sc64_vi_swap(rdram, frameBufPtr);\n'
                    '    std::lock_guard lock{ events_context.message_mutex };')
        return text.replace('std::chrono::high_resolution_clock::now()', 'sc64_runtime_now()')
    if name.endswith("timer.cpp"):
        text = text.replace('std::chrono::high_resolution_clock::now()', 'sc64_runtime_now()')
        text = once(text, '    while (!sc64_timers_stopping.load()) {',
                    '    while (!sc64_timers_stopping.load()) {\n        sc64_pause_point();')
        text = once(text, '        else {\n            if (sc64_timers_stopping.load()) return;',
                    '        else {\n            sc64_pause_point();\n'
                    '            if (sc64_timers_stopping.load()) return;\n'
                    '            if (time_now() < cur_timer->timestamp) {\n'
                    '                active_timers.insert(cur_timer_);\n                continue;\n            }')
        return text
    raise ValueError(name)

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'local/deps/N64ModernRuntime')
    parser.add_argument('--output', type=Path, default=ROOT / 'local/deps/N64ModernRuntime-shell')
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if (not source.is_relative_to(ROOT / 'local') or not output.is_relative_to(ROOT / 'local')
            or source.is_relative_to(output) or output.is_relative_to(source)):
        raise SystemExit('Distinct private local/ dependency directories required')
    print(prepare(source, output))

if __name__ == '__main__':
    main()
