#!/usr/bin/env python3
"""Generate the resident libultra inventory that `make recompile` consumes.

Produces local/reports/libultra-resident/inventory.json from the user's ROM:
  1. n64sym signature candidates (built or run locally; Docker when the host cannot run it),
  2. a cold-boot RDRAM dump from the research emulator (`run_capture.py reference-cold`),
  3. tools/research_libultra/inventory.py over the resident section, checked against the
     accepted Stage 0 profile (accepted.py) before it is published.

Every step is skipped when its result exists, nothing existing is overwritten, and the
inventory appears only complete and valid. The research emulator (`ares-test`) and the
reference objects in third_party/ultralib/build are prepared inputs: a missing one is
reported with the documented command, not built here. All outputs stay under ignored local/.
Run with the Python that has pyelftools (the project .venv; `tools/sc64.py venv`).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(HERE))
import accepted
import dockerenv
import hostenv
from private_paths import require_local_or
from rom_info import NTSC_U_SHA1

N64SYM_DIR = ROOT / 'third_party/n64sym'
N64SYM_BINARY = N64SYM_DIR / 'bin/n64sym'
N64SYM_DOCKERFILE = HERE / 'n64sym.Dockerfile'
CANDIDATES = ROOT / 'local/reports/libultra/n64sym-rom.splat.txt'
CAPTURES = ROOT / 'local/captures/research'
RECIPE = 'reference-cold'
DUMP_BYTES = 8 * 1024 * 1024
RUNNER = ROOT / 'local/toolchains/ares-build/test-runner/ares-test'
REFERENCE_ROOT = ROOT / 'third_party/ultralib/build'
OUTPUT = ROOT / 'local/reports/libultra-resident'
# ELF e_machine -> Docker platform of a Linux binary.
ELF_PLATFORMS = {62: ('x86_64', 'linux/amd64'), 183: ('arm64', 'linux/arm64')}

log = logging.getLogger('ensure_inventory')


class PipelineError(SystemExit):
    """A prerequisite is missing or a result is wrong; the message says how to fix it."""


def stamp() -> str:
    """Unique per call, so repeated failures never collide on a diagnostics directory name."""
    return time.strftime('%Y%m%d-%H%M%S') + f'-{time.time_ns() % 1_000_000:06d}'


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b''):
            digest.update(chunk)
    return digest.hexdigest()


def elf_arch(path: Path) -> tuple[str, str]:
    """(architecture, Docker platform) of a little-endian Linux ELF executable."""
    header = path.read_bytes()[:20]
    if header[:4] != b'\x7fELF' or header[5] != 1:
        raise PipelineError(f'{path} is not a little-endian ELF executable; remove it so it can be rebuilt')
    machine = struct.unpack_from('<H', header, 18)[0]
    if machine not in ELF_PLATFORMS:
        raise PipelineError(f'{path} has unsupported ELF machine {machine}; remove it so it can be rebuilt')
    return ELF_PLATFORMS[machine]


def base_image() -> str:
    """The pinned base image named by the first FROM line of the n64sym Dockerfile."""
    for line in N64SYM_DOCKERFILE.read_text().splitlines():
        if line.startswith('FROM '):
            return line.split()[1]
    raise PipelineError(f'No FROM line in {N64SYM_DOCKERFILE}')


def build_n64sym(host: hostenv.Host) -> None:
    """Build third_party/n64sym in place (its `bin/`, `obj/` and `build/` are ignored build output)."""
    if not (N64SYM_DIR / 'Makefile').is_file():
        raise PipelineError(f'{N64SYM_DIR} is missing; check out the pinned n64sym into third_party/ '
                            '(revision in tools/research_libultra/README.md)')
    if host.system == 'linux' and hostenv.find_tool('make') and hostenv.find_tool('g++'):
        runner = dockerenv.Runner(host)
    else:
        runner = dockerenv.Runner(host, dockerenv.ensure_image('sc64-n64sym-build', N64SYM_DOCKERFILE, host))
    log.info('==> build n64sym (%s)', 'docker' if runner.image else 'local make')
    runner.run(['make', '-C', N64SYM_DIR, 'n64sym'])


def n64sym_command(host: hostenv.Host, rom: Path, output: Path) -> list[str]:
    """Command that runs the n64sym binary: natively when the host can, else in Docker."""
    arch, platform = elf_arch(N64SYM_BINARY)
    arguments = ['-s', '-f', 'splat', '-o']
    if host.system == 'linux' and host.arch == arch:
        return [str(N64SYM_BINARY), str(rom), *arguments, str(output)]
    runner = dockerenv.Runner(host, base_image(), platform, (f'{rom}:/input/rom.z64:ro',))
    return runner.command([N64SYM_BINARY, '/input/rom.z64', *arguments, output])


def ensure_candidates(rom: Path, host: hostenv.Host) -> None:
    if CANDIDATES.is_file() and CANDIDATES.stat().st_size:
        log.info('n64sym candidates present: %s', CANDIDATES)
        return
    if not N64SYM_BINARY.is_file():
        build_n64sym(host)
    log.info('==> scan the ROM with n64sym')
    CANDIDATES.parent.mkdir(parents=True, exist_ok=True)
    partial = CANDIDATES.with_name(CANDIDATES.name + f'.partial-{os.getpid()}')
    result = subprocess.run(n64sym_command(host, rom, partial), capture_output=True, text=True)
    if result.returncode or not partial.is_file() or not partial.stat().st_size:
        partial.unlink(missing_ok=True)
        tail = (result.stderr or result.stdout).replace('\r', '\n').strip().splitlines()[-5:]
        raise PipelineError('n64sym failed:\n' + '\n'.join(tail))
    os.replace(partial, CANDIDATES)


def ensure_dump(rom: Path) -> Path:
    dump = CAPTURES / RECIPE / 'end.rdram.bin'
    if dump.is_file():
        if dump.stat().st_size != DUMP_BYTES:
            raise PipelineError(f'{dump} is not an 8 MiB RDRAM dump; move it aside so it can be recaptured')
        log.info('RDRAM dump present: %s', dump)
        return dump
    if (CAPTURES / RECIPE).exists():
        raise PipelineError(f'{CAPTURES / RECIPE} exists without end.rdram.bin; move it aside so it can be recaptured')
    if not RUNNER.is_file():
        raise PipelineError(
            f'The research emulator is not built: {RUNNER} is missing. It is a prepared input; build it as '
            'described in docs/reference-capture.md (sections 1 and 2), then run this again. '
            'Alternatively pass a previously captured dump with --dump.')
    log.info('==> capture the cold-boot RDRAM with the research emulator (about two minutes)')
    # Capture beside the target and move the finished directory into place, so a failed
    # or interrupted run never leaves a half-written capture where a reuse check looks.
    pending = CAPTURES.parent / f'.research-pending-{os.getpid()}'
    result = subprocess.run([sys.executable, str(ROOT / 'tools/research/run_capture.py'), RECIPE,
                             '--rom', str(rom), '--captures', str(pending), '--runner', str(RUNNER)], cwd=ROOT)
    produced = pending / RECIPE / 'end.rdram.bin'
    if result.returncode or not produced.is_file() or produced.stat().st_size != DUMP_BYTES:
        failed = pending.with_name(f'.research-failed-{stamp()}')
        if pending.exists():
            pending.rename(failed)
        raise PipelineError(f'Capture {RECIPE} failed (exit {result.returncode}); diagnostics kept in {failed}')
    CAPTURES.mkdir(parents=True, exist_ok=True)
    os.replace(pending / RECIPE, CAPTURES / RECIPE)
    pending.rmdir()
    return dump


def check_references() -> None:
    missing = [version for version in accepted.REFERENCE_VERSIONS
               if not any((REFERENCE_ROOT / version).glob('lib*ultra*/src/**/*.o'))]
    if missing:
        raise PipelineError(
            f'Reference libultra objects are missing for versions {", ".join(missing)} in {REFERENCE_ROOT}. '
            'They are prepared inputs (GCC 2.7.2 and IDO builds of third_party/ultralib); build them as '
            'described in tools/research_libultra/README.md, then run this again.')


def check_python() -> None:
    try:
        import elftools  # noqa: F401
    except ImportError as error:
        raise PipelineError('pyelftools is not importable by this Python; run via the project environment '
                            '(`python tools/sc64.py venv` installs it)') from error


def generate(rom: Path, dump: Path, output: Path) -> None:
    """Run inventory.py into a scratch directory, validate it and publish it atomically."""
    scratch = output.parent / f'.libultra-resident-{os.getpid()}'
    shutil.rmtree(scratch, ignore_errors=True)
    command = [sys.executable, str(HERE / 'inventory.py'), str(rom),
               '--n64sym', 'third_party/n64sym', '--recompiler', 'third_party/N64Recomp',
               '--runtime', 'third_party/N64ModernRuntime', '--reference-root', str(REFERENCE_ROOT.relative_to(ROOT)),
               '--candidates', str(CANDIDATES.relative_to(ROOT)), '--candidate-displacement', hex(accepted.RESIDENT_DISPLACEMENT),
               '--scan-start', hex(accepted.RESIDENT_SCAN_START), '--scan-end', hex(accepted.RESIDENT_SCAN_END),
               '--displacement', hex(accepted.RESIDENT_DISPLACEMENT), '--dump', str(dump), '--output', str(scratch)]
    log.info('==> libultra inventory (resident section)')
    result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
    if result.returncode:
        raise PipelineError('inventory.py failed:\n' + (result.stderr or result.stdout).strip()[-1500:])
    try:
        accepted.validate_resident(json.loads((scratch / 'inventory.json').read_text()), NTSC_U_SHA1)
    except ValueError as error:
        rejected = scratch.with_name(f'.libultra-resident-rejected-{stamp()}')
        scratch.rename(rejected)
        raise PipelineError(f'The regenerated inventory differs from the accepted Stage 0 profile: {error}; '
                            f'kept for diagnosis in {rejected}') from error
    if output.exists():
        output.rename(output.with_name(f'{output.name}.incomplete-{stamp()}'))
    os.replace(scratch, output)
    log.info('PASS: %s', output / 'inventory.json')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('rom', type=Path, help='your NTSC-U dump in z64 byte order')
    parser.add_argument('--output', type=Path, default=OUTPUT, help='inventory directory under local/')
    parser.add_argument('--dump', type=Path, help='use this canonical big-endian 8 MiB RDRAM dump instead of capturing one')
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format='%(message)s')
    output = require_local_or(sys.exit, args.output, root=ROOT, what='Inventory output')
    rom = args.rom.resolve()
    if not rom.is_file():
        raise PipelineError(f'ROM not found: {rom}')
    if sha1_file(rom) != NTSC_U_SHA1:
        raise PipelineError(f'Unsupported ROM: expected the NTSC-U z64 (big-endian) profile, SHA-1 {NTSC_U_SHA1}')
    existing = output / 'inventory.json'
    if existing.is_file():
        try:
            accepted.validate_resident(json.loads(existing.read_text()), NTSC_U_SHA1)
        except (ValueError, KeyError) as error:
            raise PipelineError(f'{existing} exists but is not the accepted resident inventory ({error}); '
                                'move it aside to regenerate it') from error
        log.info('Inventory present and valid: %s', existing)
        return 0
    check_python()
    check_references()
    host = hostenv.current()
    ensure_candidates(rom, host)
    dump = args.dump.resolve(strict=True) if args.dump else ensure_dump(rom)
    if dump.stat().st_size != DUMP_BYTES:
        raise PipelineError(f'{dump} is not an 8 MiB RDRAM dump')
    generate(rom, dump, output)
    return 0


if __name__ == '__main__':
    sys.exit(main())
