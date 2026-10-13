#!/usr/bin/env python3
"""Launch the personal SDL3 bundle and bind private evidence to its binary."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import time
from pathlib import Path
from typing import NamedTuple
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hostenv
from private_paths import require_local_or

ROOT = Path(__file__).resolve().parents[2]

class Launch(NamedTuple):
    """Where the host's own build lives and which receipt entries bind it."""
    binary: Path
    resources: Path
    receipt: Path
    schema: int
    resources_key: str

def locate(host: hostenv.Host, root: Path = ROOT) -> Launch:
    if host.system == 'darwin':
        app = root / 'local/dist/StarCraft64.app/Contents'
        return Launch(app / 'MacOS/SC64', app / 'Resources', root / 'local/reports/desktop/build.json',
                      2, 'resources_sha256')
    if host.arch == 'x86_64' and host.system in ('linux', 'windows'):
        package = root / f'local/dist/StarCraft64-{host.system}'
        return Launch(package / ('StarCraft64' + host.exe_suffix), package,
                      root / f'local/reports/{host.system}/build.json', 1, 'files_sha256')
    raise SystemExit(f'No desktop package runs natively on {host.label}')

def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def resource_hashes(directory: Path) -> dict[str, str]:
    return {path.relative_to(directory).as_posix(): sha256(path)
            for path in sorted(directory.rglob('*')) if path.is_file()}

def resources_match(directory: Path, expected: dict[str, str]) -> bool:
    # Check the complete set as well as contents: a removed/added lazy-loaded
    # resource can change execution even when the native executable is identical.
    try:
        return bool(expected) and resource_hashes(directory) == expected
    except OSError:
        return False

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', type=Path)
    parser.add_argument('--storage', type=Path, default=ROOT / 'local/private/desktop/default')
    parser.add_argument('--inputs', type=Path)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--output', type=Path, required=True, help='New ignored local/ report directory')
    parser.add_argument('--gpu-probe', type=Path)
    parser.add_argument('--trace-from',type=int,default=0)
    parser.add_argument('--trace-to',type=int,default=0)
    parser.add_argument('--audio-from',type=int,default=0)
    parser.add_argument('--audio-to',type=int,default=0)
    parser.add_argument('--debug-input', action='store_true')
    parser.add_argument('--background-input-probe', action='store_true',
                        help='Bounded recipe diagnostic only; never lifecycle acceptance')
    parser.add_argument('--pause-at', type=float, default=0, help='Optional script-time lifecycle probe')
    parser.add_argument('--resume-at', type=float, default=0)
    args = parser.parse_args()
    output = require_local_or(sys.exit, args.output, new=True, root=ROOT, what='Report directory')
    storage = require_local_or(sys.exit, args.storage, root=ROOT, what='Personal storage')
    if args.seconds < 0 or args.seconds > 3600:
        raise SystemExit('seconds must be between 0 (interactive) and 3600')
    if args.pause_at < 0 or args.resume_at < 0 or (args.resume_at > 0 and args.resume_at <= args.pause_at):
        raise SystemExit('resume-at must follow pause-at; times must be nonnegative')
    if args.trace_from or args.trace_to:
        if not args.rom or args.trace_from<=0 or args.trace_to<args.trace_from or args.trace_to-args.trace_from>600:
            raise SystemExit('Live frame trace needs a ROM and a positive range of at most 600 fields')
    if args.audio_from or args.audio_to:
        if not args.rom or args.audio_from<=0 or args.audio_to<args.audio_from or args.audio_to-args.audio_from>600:
            raise SystemExit('Audio trace needs a ROM and a positive range of at most 600 fields')
    if args.gpu_probe and (args.rom or args.inputs):
        raise SystemExit('GPU fixture probe and live guest execution are separate runs')
    if args.background_input_probe and (not args.inputs or args.seconds <= 0 or args.pause_at or args.resume_at):
        raise SystemExit('Background input probe requires a timed recipe without lifecycle probes')
    launch = locate(hostenv.current(), ROOT)
    binary = launch.binary
    if not launch.receipt.is_file() or not binary.is_file():
        raise SystemExit('No build for this host; run `python tools/sc64.py build` first')
    receipt = json.loads(launch.receipt.read_text())
    if receipt.get('schema') != launch.schema:
        raise SystemExit('Build receipt predates the SDL3 shell; rebuild first')
    if sha256(binary) != receipt['binary_sha256']:
        raise SystemExit('Bundle differs from the build receipt; rebuild first')
    expected_resources = receipt.get(launch.resources_key, {})
    if not resources_match(launch.resources, expected_resources):
        raise SystemExit('Bundled resources differ from the build receipt; rebuild first')
    command = [str(binary), f'--config=sc64.storage={storage}',
               f'--config=sc64.seconds={args.seconds}', f'--config=sc64.pause_at={args.pause_at}',
               f'--config=sc64.resume_at={args.resume_at}']
    inputs = {}
    for name, path in (('rom', args.rom), ('inputs', args.inputs), ('gpu_probe', args.gpu_probe)):
        if path:
            path = path.resolve(strict=True)
            inputs[name] = {'path': str(path), 'sha256': sha256(path)}
            command.append(f'--config=sc64.{name}={path}')
    if args.debug_input:
        command.append('--config=sc64.debug_input=1')
    if args.background_input_probe:
        command.append('--config=sc64.background_input_probe=1')
    if args.trace_from:
        command.extend([f'--config=sc64.trace_out={output / "frames"}',
            f'--config=sc64.trace_from={args.trace_from}',f'--config=sc64.trace_to={args.trace_to}'])
    if args.audio_from:
        command.extend([f'--config=sc64.audio_trace_out={output / "audio"}',
            f'--config=sc64.audio_trace_from={args.audio_from}',f'--config=sc64.audio_trace_to={args.audio_to}'])
    if args.gpu_probe:
        command.append(f'--config=sc64.probe_out={output / "gpu"}')
    output.mkdir(parents=True)
    report = {'schema': 3, 'build': receipt,
              'resume_requested': args.resume_at > 0,
              'background_input_probe': args.background_input_probe,
              'command': command, 'inputs': inputs, 'started_unix': time.time()}
    (output / 'run.json').write_text(json.dumps(report, indent=2) + '\n')
    started = time.monotonic()
    with (output / 'engine.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    try:
        binary_unchanged = sha256(binary) == receipt['binary_sha256']
    except OSError:
        binary_unchanged = False
    resources_unchanged = resources_match(launch.resources, expected_resources)
    report.update({'elapsed_seconds': time.monotonic() - started, 'exit_code': result.returncode,
                   'binary_unchanged': binary_unchanged,
                   'resources_unchanged': resources_unchanged,
                   'log_sha256': sha256(output / 'engine.log')})
    (output / 'run.json').write_text(json.dumps(report, indent=2) + '\n')
    print(output / 'run.json')
    if result.returncode == 0 and not (binary_unchanged and resources_unchanged):
        raise SystemExit('Bundle changed during execution; run is not valid acceptance evidence')
    raise SystemExit(result.returncode)

if __name__ == '__main__':
    main()
