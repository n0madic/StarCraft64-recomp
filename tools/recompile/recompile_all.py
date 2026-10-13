#!/usr/bin/env python3
"""One-command private recompilation: ROM -> local/generated/game ready for `make build`.

Chains the documented Stage 1 steps: patch and build N64Recomp, analyze the ROM
(prepare.py), add the guarded UI observers (add_ui_hooks.py), generate C
(run_recompiler.py --replace) and validate the observers the shell build
requires. The libultra inventory is a prepared input (see docs/libultra-inventory.md);
it is verified, never rebuilt here. Run with the Python that has spimdisasm
(`tools/sc64.py recompile` uses the project .venv; `sc64.py venv` creates it).
The recompiler is built with LLVM when found (macOS Homebrew or LLVM_BIN), else
with CC/CXX or the system compiler. All outputs stay under ignored local/.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import logging
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import hostenv
from private_paths import require_local_or
from rom_info import NTSC_U_SHA1 as ROM_SHA1

ROOT = Path(__file__).resolve().parents[2]
TOOLS = Path(__file__).resolve().parent
DEFAULT_INVENTORY = ROOT / "local/reports/libultra-resident/inventory.json"
RECOMPILER_SOURCE = ROOT / "third_party/N64Recomp"
RECOMPILER_BUILD = ROOT / "local/build/recompiler"
DEFAULT_OUTPUT = ROOT / "local/generated/game"

log = logging.getLogger("recompile_all")


class PipelineError(SystemExit):
    """A prerequisite or step failed; the message tells the user how to fix it."""


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_inputs(rom: Path, inventory: Path) -> None:
    """Fail before any long step if the ROM, inventory or Python environment is unusable."""
    if not rom.is_file():
        raise PipelineError(f"ROM not found: {rom}")
    if sha1_file(rom) != ROM_SHA1:
        raise PipelineError("Unsupported ROM: expected the NTSC-U z64 (big-endian) profile, "
                            f"SHA-1 {ROM_SHA1}; normalize byte order first")
    if not inventory.is_file():
        raise PipelineError(f"Libultra inventory is a prepared input but is missing: {inventory}\n"
                            "Build it once with tools/research_libultra/inventory.py "
                            "(see docs/libultra-inventory.md), or pass --inventory")
    if json.loads(inventory.read_text()).get("rom_sha1") != ROM_SHA1:
        raise PipelineError(f"Inventory {inventory} belongs to a different ROM")
    try:
        import spimdisasm  # noqa: F401
    except ImportError as error:
        raise PipelineError("spimdisasm is not importable by this Python; run via the project "
                            "environment (`tools/sc64.py recompile` uses the project .venv; `tools/sc64.py venv` creates it)") from error


def run(step: str, command: list[str | Path]) -> None:
    log.info("==> %s", step)
    result = subprocess.run([str(part) for part in command], cwd=ROOT)
    if result.returncode:
        raise PipelineError(f"Step failed ({step}), exit status {result.returncode}")


def recompiler_binary() -> Path:
    return RECOMPILER_BUILD / ("N64Recomp" + hostenv.current().exe_suffix)


def build_recompiler(jobs: int) -> None:
    if not (RECOMPILER_SOURCE / "CMakeLists.txt").is_file():
        raise PipelineError(f"{RECOMPILER_SOURCE} is missing; run: "
                            "git -C third_party/N64Recomp submodule update --init --recursive")
    run("patch N64Recomp (idempotent)", [sys.executable, TOOLS / "patch_recompiler.py", RECOMPILER_SOURCE])
    if not (RECOMPILER_BUILD / "CMakeCache.txt").is_file():
        flags = ["-DCMAKE_BUILD_TYPE=Release", "-DCMAKE_CXX_FLAGS=-include cstdlib"]
        # Pinned Rabbitizer omits malloc/free declarations; the forced include avoids editing it.
        if llvm := hostenv.llvm_bin():
            flags += [f"-DCMAKE_C_COMPILER={llvm / 'clang'}", f"-DCMAKE_CXX_COMPILER={llvm / 'clang++'}"]
        run("configure N64Recomp", ["cmake", "-S", RECOMPILER_SOURCE, "-B", RECOMPILER_BUILD, "-G", "Ninja", *flags])
    run("build N64Recomp", ["cmake", "--build", RECOMPILER_BUILD, "--target", "N64RecompCLI", "-j", str(jobs)])
    binary = recompiler_binary()
    if not binary.is_file():
        raise PipelineError(f"Recompiler build did not produce {binary}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--inventory", type=Path, default=DEFAULT_INVENTORY)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT,
                        help="Generated game directory (an existing one is replaced transactionally)")
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("JOBS", "8")))
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")

    rom = args.rom.resolve()
    output = require_local_or(sys.exit, args.output, root=ROOT, what="Generated output")
    check_inputs(rom, args.inventory.resolve())

    build_recompiler(args.jobs)

    analysis = ROOT / "local/recompile" / time.strftime("symbols-%Y%m%d-%H%M%S")
    run("analyze ROM", [sys.executable, TOOLS / "prepare.py", rom,
                        "--inventory", args.inventory.resolve(), "--output", analysis])
    run("add UI observers", [sys.executable, ROOT / "tools/input/add_ui_hooks.py",
                             "--rom", rom, "--config", analysis / "sc64.toml"])
    run("generate game C", [sys.executable, TOOLS / "run_recompiler.py", analysis / "sc64-ui.toml",
                            "--recompiler", recompiler_binary(), "--output", output, "--replace"])
    run("validate observers", [sys.executable, ROOT / "tools/desktop/game_validation.py", output])
    log.info("PASS: %s ready for `make build`; analysis kept in %s", output, analysis)


if __name__ == "__main__":
    main()
