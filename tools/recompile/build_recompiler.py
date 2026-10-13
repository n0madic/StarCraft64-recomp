#!/usr/bin/env python3
"""Build the patched N64Recomp for a release target and prove it against the recipe (ADR-085).

    build_recompiler.py build [--target T]       configure, build and publish local/dist/recompiler/T
    build_recompiler.py verify ROM [--target T]  run the binary on the recipe and require the recorded hashes

Targets: macos-arm64 (macOS arm64 host), linux-x86_64 (host compiler on Linux
x86_64, else the pinned local Docker image) and windows-x86_64 (mingw-w64, cross
or native from MSYS2). Linux and Windows binaries are fully static; the macOS
one links only system libraries. Nothing leaves this machine.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))
sys.path.insert(0, str(TOOLS / "desktop"))
import cross_build
import dockerenv
import hostenv
import patch_recompiler
import recipe
from hostenv import Host

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "third_party/N64Recomp"
DIST = ROOT / "local/dist/recompiler"
TARGETS = ("macos-arm64", "linux-x86_64", "windows-x86_64")
MACOS_DEPLOYMENT_TARGET = "13.0"
PATCHED_FILES = ("src/symbol_lists.cpp", "src/analysis.cpp", "src/main.cpp")
# The pinned Debian that runs the static Linux binary under emulation; no image build is needed.
LINUX_DOCKERFILE = ROOT / "tools/desktop/linux-cross.Dockerfile"
WINE_DOCKERFILE = ROOT / "tools/recompile/wine.Dockerfile"
# Wine refuses a prefix whose parent the (non-root) container user does not own, so the prefix
# lives in the ignored scratch tree, which is mounted at the host user's ownership.
WINE_PREFIX = ROOT / "local/recompile/wine-prefix"
WINE_ENV = (f"HOME={WINE_PREFIX}", f"WINEPREFIX={WINE_PREFIX}/prefix", "WINEDEBUG=-all", "XDG_RUNTIME_DIR=/tmp")
WINE_BINARY = "/usr/lib/wine/wine64"  # Debian's wine64 package ships no /usr/bin/wine


class RecompilerError(SystemExit):
    """The host cannot build or run this target, or the result is not acceptable."""


def host_target(host: Host) -> str:
    target = f"{host.system.replace('darwin', 'macos')}-{host.arch}"
    if target not in TARGETS:
        raise RecompilerError(f"No N64Recomp release target for {host.label}; pass an explicit target")
    return target


def binary_name(target: str) -> str:
    return "N64Recomp.exe" if target.startswith("windows") else "N64Recomp"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def build_dir(target: str, mode: str) -> Path:
    # macos-arm64 keeps the historical tree that `make recompile` already used.
    if target == "macos-arm64":
        return ROOT / "local/build/recompiler"
    return ROOT / f"local/build/recompiler-{target}{'-native' if mode == 'native' else ''}"


def binary_path(target: str, host: Host, docker: bool = False) -> Path:
    return build_dir(target, mode_for(target, host, docker)) / binary_name(target)


def mode_for(target: str, host: Host, docker: bool = False) -> str:
    if target == "macos-arm64":
        if (host.system, host.arch) != ("darwin", "arm64"):
            raise RecompilerError("macos-arm64 builds only on a macOS arm64 host")
        return "native"
    return cross_build.plan_for(target.split("-")[0], host, docker).mode


def compiler_flags(target: str, host: Host, mode: str) -> list[str]:
    flags = ["-DCMAKE_BUILD_TYPE=Release",
             # Pinned Rabbitizer omits malloc/free declarations; the forced include avoids editing it.
             "-DCMAKE_CXX_FLAGS=-include cstdlib"]
    if target == "macos-arm64":
        flags.append(f"-DCMAKE_OSX_DEPLOYMENT_TARGET={MACOS_DEPLOYMENT_TARGET}")
        if llvm := hostenv.llvm_bin():
            flags += [f"-DCMAKE_C_COMPILER={llvm / 'clang'}", f"-DCMAKE_CXX_COMPILER={llvm / 'clang++'}"]
    elif target == "linux-x86_64":
        flags.append("-DCMAKE_EXE_LINKER_FLAGS=-static")  # independent of the target's glibc
    return flags


def apply_patches(jobs_log=print) -> None:
    jobs_log("==> patch N64Recomp (idempotent)")
    subprocess.run([sys.executable, str(TOOLS / "recompile/patch_recompiler.py"), str(SOURCE)], check=True)


def tool_runner(host: Host, mode: str) -> dockerenv.Runner:
    """Build tools run on the host, or in the pinned local cross image for ``docker`` mode."""
    return dockerenv.Runner(host, cross_build.docker_image(host) if mode == "docker" else None)


def build(target: str, host: Host, jobs: int = 8, docker: bool = False) -> Path:
    """Build N64RecompCLI for ``target`` and return the binary inside its build tree."""
    if not (SOURCE / "CMakeLists.txt").is_file():
        raise RecompilerError(f"{SOURCE} is missing; check out the pinned N64Recomp "
                              "(`git -C third_party/N64Recomp submodule update --init --recursive`)")
    mode = mode_for(target, host, docker)
    apply_patches()
    tree = build_dir(target, mode)
    runner = tool_runner(host, mode)
    flags = compiler_flags(target, host, mode)
    if target != "macos-arm64":
        plan = cross_build.plan_for(target.split("-")[0], host, docker)
        flags.append(f"-DCMAKE_TOOLCHAIN_FILE={runner.path(plan.toolchain)}")
    if not (tree / "CMakeCache.txt").is_file():
        runner.run(["cmake", "-S", SOURCE, "-B", tree, "-G", "Ninja", *flags])
    runner.run(["cmake", "--build", tree, "--target", "N64RecompCLI", "-j", str(jobs)])
    binary = tree / binary_name(target)
    if not binary.is_file():
        raise RecompilerError(f"Recompiler build did not produce {binary}")
    return binary


def host_tree(host: Host) -> Path:
    return ROOT / f"local/build/recompiler-host-{host.label}"


def host_binary(host: Host) -> Path:
    """Where `make recompile` finds N64Recomp: the release target's tree, or a generic host build."""
    try:
        target = host_target(host)
    except RecompilerError:
        return host_tree(host) / ("N64Recomp" + host.exe_suffix)
    return binary_path(target, host)


def build_host(host: Host, jobs: int = 8) -> Path:
    """Build for the machine itself, whichever it is (hosts without a release target, e.g. Linux arm64)."""
    try:
        return build(host_target(host), host, jobs)
    except RecompilerError as error:
        if "No N64Recomp release target" not in str(error):
            raise
    apply_patches()
    tree = host_tree(host)
    if not (tree / "CMakeCache.txt").is_file():
        subprocess.run(["cmake", "-S", str(SOURCE), "-B", str(tree), "-G", "Ninja", *compiler_flags("host", host, "native")],
                       check=True)
    subprocess.run(["cmake", "--build", str(tree), "--target", "N64RecompCLI", "-j", str(jobs)], check=True)
    binary = host_binary(host)
    if not binary.is_file():
        raise RecompilerError(f"Recompiler build did not produce {binary}")
    return binary


def source_provenance() -> dict:
    def git(*words: str) -> str:
        return subprocess.check_output(["git", "-C", str(SOURCE), *words], text=True).strip()

    return {"commit": git("rev-parse", "HEAD"), "pin": patch_recompiler.PIN,
            "patch_tool_sha256": sha256(TOOLS / "recompile/patch_recompiler.py"),
            "patched_files_sha256": {name: sha256(SOURCE / name) for name in PATCHED_FILES},
            "submodules": [line.split()[0].lstrip("-+ U") + " " + line.split()[1]
                           for line in git("submodule", "status", "--recursive").splitlines() if line.strip()]}


def toolchain_version(target: str, host: Host, mode: str, tree: Path) -> str:
    compiler = cross_build.configured_compiler(tree)
    if compiler is None:
        raise RecompilerError(f"CMake did not record a C++ compiler under {tree}")
    return tool_runner(host, mode).output([compiler, "--version"]).splitlines()[0]


def publish(target: str, host: Host, binary: Path, mode: str) -> Path:
    """Copy the (stripped) binary and its receipt to ``local/dist/recompiler/TARGET``."""
    destination = DIST / target
    destination.mkdir(parents=True, exist_ok=True)
    staged = destination / (binary_name(target) + ".new")
    shutil.copy2(binary, staged)
    if target != "macos-arm64":
        plan = cross_build.plan_for(target.split("-")[0], host, mode == "docker")
        tool_runner(host, mode).run([plan.strip, "--strip-all", staged])
    os.replace(staged, destination / binary_name(target))
    receipt = {"schema": 1, "target": target, "host": host.label, "build_mode": mode,
               "binary_sha256": sha256(destination / binary_name(target)),
               "source": source_provenance(),
               "toolchain": toolchain_version(target, host, mode, binary.parent),
               "cloud_upload": False, "recipe_verification": None}
    write_receipt(destination, receipt)
    return destination


def write_receipt(directory: Path, receipt: dict) -> None:
    temporary = directory / "receipt.json.new"
    temporary.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    os.replace(temporary, directory / "receipt.json")


def init_wine_prefix(image: str, host: Host, mounts: tuple[str, ...]) -> None:
    """Create the prefix once, outside the run: Wine's first-start warnings must not reach the recompiler log."""
    if (WINE_PREFIX / "prefix").is_dir():
        return
    WINE_PREFIX.mkdir(parents=True, exist_ok=True)
    subprocess.run(dockerenv.in_container(image, ["env", *WINE_ENV, WINE_BINARY, "wineboot", "-u"], host,
                                          "linux/amd64", mounts), capture_output=True)


def launcher(target: str, host: Host, mounts: tuple[str, ...]):
    """Command line that runs ``(binary, config)`` for a target this host can execute."""
    if target == "macos-arm64" and (host.system, host.arch) == ("darwin", "arm64"):
        return lambda binary, config: [str(binary), str(config)], "native"
    if target == "linux-x86_64":
        if host.linux_x86_64:
            return lambda binary, config: [str(binary), str(config)], "native"
        if host.system == "windows":
            raise RecompilerError("verify of the Linux binary from a Windows host is not supported; run it on Linux or macOS")
        image = dockerenv.base_image(LINUX_DOCKERFILE)
        return (lambda binary, config: dockerenv.in_container(image, [str(binary), str(config)], host,
                                                              "linux/amd64", mounts), "docker linux/amd64")
    if target == "windows-x86_64":
        if host.system == "windows":
            return lambda binary, config: [str(binary), str(config)], "native"
        if host.system == "linux" or host.system == "darwin":
            image = dockerenv.ensure_image("sc64-wine", WINE_DOCKERFILE, host, "linux/amd64")
            init_wine_prefix(image, host, mounts)
            return (lambda binary, config: dockerenv.in_container(
                image, ["env", *WINE_ENV, WINE_BINARY, str(binary), str(config)], host, "linux/amd64", mounts),
                "wine in docker linux/amd64")
    raise RecompilerError(f"{host.label} cannot run the {target} binary")


def verify(target: str, rom: Path, host: Host, binary: Path | None = None) -> dict:
    """Generate the game from the recipe with the published binary and compare every hash."""
    binary = (binary or DIST / target / binary_name(target)).resolve()
    if not binary.is_file():
        raise RecompilerError(f"{binary} not found; run `build_recompiler.py build --target {target}` first")
    rom = rom.resolve()
    mounts = () if rom.is_relative_to(ROOT) else (f"{rom.parent}:{rom.parent}:ro",)
    launch, runner = launcher(target, host, mounts)
    report = recipe.generate(rom, binary, None, launch=launch)
    result = {"rom_sha1": recipe.ROM_SHA1, "generated_files": len(report["files"]), "runner": runner,
              "binary_sha256": sha256(binary)}
    receipt_path = binary.parent / "receipt.json"
    if receipt_path.is_file():
        receipt = json.loads(receipt_path.read_text())
        if receipt["binary_sha256"] != result["binary_sha256"]:
            raise RecompilerError("receipt.json describes a different binary; rebuild before verifying")
        receipt["recipe_verification"] = result
        write_receipt(binary.parent, receipt)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    build_parser = commands.add_parser("build")
    build_parser.add_argument("--target", choices=TARGETS, help="default: this host's own target")
    build_parser.add_argument("--jobs", type=int, default=int(os.environ.get("JOBS", "8")))
    build_parser.add_argument("--docker", action="store_true", help="Linux: use the pinned Docker image on a Linux x86_64 host")
    verify_parser = commands.add_parser("verify")
    verify_parser.add_argument("rom", type=Path)
    verify_parser.add_argument("--target", choices=TARGETS, help="default: this host's own target")
    args = parser.parse_args()
    host = hostenv.current()
    if args.command == "build":
        target = args.target or host_target(host)
        binary = build(target, host, args.jobs, args.docker)
        print(publish(target, host, binary, mode_for(target, host, args.docker)))
    else:
        result = verify(args.target or host_target(host), args.rom, host)
        print(f"PASS: {result['generated_files']} generated files match the recipe ({result['runner']})")


if __name__ == "__main__":
    main()
