#!/usr/bin/env python3
"""The committed NTSC-U recompilation recipe (ADR-084).

Everything the analysis derives from the ROM before N64Recomp runs is metadata:
function names, addresses and sizes, section intervals, instruction-nop sites
and authored hook text. ``recipe/ntsc-u/`` keeps it in Git, with SHA-256 hashes
of the generated C files. A user then needs only the ROM (SHA-1 checked), the
patched N64Recomp and a C compiler:

    recipe.py check                       the recipe holds only whitelisted metadata
    recipe.py export --analysis DIR       refresh it from a ``make recompile`` analysis
    recipe.py generate ROM [--output D]   N64Recomp + recipe, compared with the hashes

The recipe never contains game bytes or generated code; ``check`` rejects any
line outside a strict whitelist. Stdlib only, so it also runs without ``.venv``.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local_or
from rom_info import NTSC_U_SHA1 as ROM_SHA1
from run_recompiler import inspect_output
from splat_to_syms import HEADER

ROOT = Path(__file__).resolve().parents[2]
RECIPE = ROOT / "recipe/ntsc-u"
SYMBOLS = "sc64.syms.toml"
CONFIG = "sc64.toml"
MANIFEST = "generated.json"
SCHEMA = 1
ROM_PLACEHOLDER = "@ROM@"
OUTPUT_PLACEHOLDER = "@OUTPUT@"
SCRATCH = ROOT / "local/recompile"

IDENT = r"[A-Za-z_][A-Za-z_0-9]*"
HEX = r"0x[0-9A-Fa-f]+"
SYMBOL_LINES = [re.compile(pattern) for pattern in (
    re.escape(HEADER), r"", r"\[\[section\]\]", rf'name = "{IDENT}"', rf"rom = {HEX}", rf"vram = {HEX}",
    rf"size = {HEX}", r"functions = \[", r"\]",
    rf'  \{{ name = "{IDENT}", vram = {HEX}, size = {HEX} \}},')]
# Hook text is authored C that calls a host callback; it never carries data.
HOOK_TEXT = (r'text = "\{ extern void sc64_[a-z_0-9]+\(uint8_t\*(?:, recomp_context\*)?(?:, unsigned)?\); '
             r'sc64_[a-z_0-9]+\(rdram(?:, ctx)?(?:, [0-9]+)?\); \}"')
CONFIG_LINES = [re.compile(pattern) for pattern in (
    r"", r"\[input\]", r"\[patches\]", r"\[\[patches\.instruction\]\]", r"\[\[patches\.hook\]\]",
    rf"entrypoint = {HEX}", rf'symbols_file_path = "{SYMBOLS}"', rf'rom_file_path = "{ROM_PLACEHOLDER}"',
    rf'output_func_path = "{OUTPUT_PLACEHOLDER}"', r"functions_per_output_file = [0-9]+",
    r"uses_mips3_float_mode = false", r"(?:stubs|ignored|renamed) = \[\]",
    rf'func = "{IDENT}"', rf"vram = {HEX}", r"value = 0", rf"before_vram = {HEX}", HOOK_TEXT)]
SHA256 = re.compile(r"[0-9a-f]{64}")
FILE_NAME = re.compile(r"[A-Za-z0-9_.]+")


class RecipeError(SystemExit):
    """The recipe, its inputs or a generated result is not acceptable."""


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sha1_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def reject_lines(text: str, patterns: list[re.Pattern], what: str) -> None:
    for number, line in enumerate(text.splitlines(), 1):
        if not any(pattern.fullmatch(line) for pattern in patterns):
            raise RecipeError(f"{what} line {number} is outside the metadata whitelist: {line[:80]!r}")


def validate_manifest(manifest: dict) -> None:
    if manifest.get("schema") != SCHEMA or manifest.get("rom_sha1") != ROM_SHA1:
        raise RecipeError(f"{MANIFEST}: unexpected schema or ROM")
    for key in ("symbols_sha256", "config_sha256"):
        if not SHA256.fullmatch(str(manifest.get(key, ""))):
            raise RecipeError(f"{MANIFEST}: {key} is not a SHA-256")
    files = manifest.get("files")
    if not isinstance(files, dict) or not files:
        raise RecipeError(f"{MANIFEST}: no generated file hashes")
    for name, digest in files.items():
        if not FILE_NAME.fullmatch(name) or not SHA256.fullmatch(str(digest)):
            raise RecipeError(f"{MANIFEST}: bad entry {name!r}")


def check(recipe: Path = RECIPE) -> dict:
    """Validate the recipe files and their recorded hashes; return the manifest."""
    expected = {SYMBOLS, CONFIG, MANIFEST}
    present = {path.name for path in recipe.iterdir()}
    if present != expected:
        raise RecipeError(f"{recipe} must hold exactly {sorted(expected)}, found {sorted(present)}")
    symbols = (recipe / SYMBOLS).read_text()
    reject_lines(symbols, SYMBOL_LINES, SYMBOLS)
    config = (recipe / CONFIG).read_text()
    reject_lines(config, CONFIG_LINES, CONFIG)
    for placeholder in (ROM_PLACEHOLDER, OUTPUT_PLACEHOLDER):
        if config.count(placeholder) != 1:
            raise RecipeError(f"{CONFIG} must contain {placeholder} exactly once")
    manifest = json.loads((recipe / MANIFEST).read_text())
    validate_manifest(manifest)
    if manifest["symbols_sha256"] != sha256_file(recipe / SYMBOLS) or manifest["config_sha256"] != sha256_file(recipe / CONFIG):
        raise RecipeError(f"{MANIFEST} does not match {SYMBOLS}/{CONFIG}; refresh with `recipe.py export`")
    return manifest


def toml_string(path: Path) -> str:
    return json.dumps(str(path), ensure_ascii=False)


def render_config(template: str, rom: Path, output: Path) -> str:
    """Fill the two placeholders; paths become TOML basic strings."""
    return template.replace(f'"{ROM_PLACEHOLDER}"', toml_string(rom)).replace(f'"{OUTPUT_PLACEHOLDER}"', toml_string(output))


def templatize(config: str) -> str:
    """Replace the machine-specific ROM and output paths of an analysis config."""
    for key, placeholder in (("rom_file_path", ROM_PLACEHOLDER), ("output_func_path", OUTPUT_PLACEHOLDER)):
        config, count = re.subn(rf"(?m)^{key}\s*=\s*[^\n]+$", f'{key} = "{placeholder}"', config)
        if count != 1:
            raise RecipeError(f"Expected one {key} in the analysis config")
    return config


def hashes(directory: Path) -> dict[str, str]:
    return {path.name: sha256_file(path) for path in sorted(directory.iterdir()) if path.is_file()}


def matching_generation(analysis: Path, config_sha256: str) -> dict:
    """The newest ``generation.json`` that recompiled exactly this config."""
    reports = sorted(analysis.glob("recompile-*/generation.json"), key=lambda path: path.stat().st_mtime, reverse=True)
    for report in reports:
        data = json.loads(report.read_text())
        if data.get("config_sha256") == config_sha256:
            return data
    raise RecipeError(f"No generation.json under {analysis} recompiled this config; run `make recompile` first")


def export(analysis: Path, generated: Path, recipe: Path = RECIPE) -> Path:
    config_path = analysis / "sc64-ui.toml"
    symbols_path = analysis / SYMBOLS
    for path in (config_path, symbols_path):
        if not path.is_file():
            raise RecipeError(f"{path} not found; --analysis must be a `make recompile` analysis directory")
    generation = matching_generation(analysis, sha256_file(config_path))
    if generation["files"] != hashes(generated):
        raise RecipeError(f"{generated} differs from the generation recorded for {config_path}")
    symbols = symbols_path.read_text().splitlines()
    if not symbols or not symbols[0].startswith("#"):
        raise RecipeError(f"{symbols_path} has no header line")
    texts = {SYMBOLS: "\n".join([HEADER, *symbols[1:]]) + "\n", CONFIG: templatize(config_path.read_text())}
    manifest = {"schema": SCHEMA, "rom_sha1": ROM_SHA1,
                "symbols_sha256": hashlib.sha256(texts[SYMBOLS].encode()).hexdigest(),
                "config_sha256": hashlib.sha256(texts[CONFIG].encode()).hexdigest(),
                "files": generation["files"]}
    texts[MANIFEST] = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
    staging = Path(tempfile.mkdtemp(prefix=".recipe-", dir=recipe.parent if recipe.parent.exists() else None))
    try:
        for name, text in texts.items():
            (staging / name).write_text(text)
        check(staging)
        recipe.mkdir(parents=True, exist_ok=True)
        for name in texts:
            (staging / name).replace(recipe / name)
    finally:
        shutil.rmtree(staging, ignore_errors=True)
    check(recipe)
    return recipe


def compare(expected: dict[str, str], actual: dict[str, str]) -> list[str]:
    problems = [f"missing {name}" for name in sorted(expected.keys() - actual.keys())]
    problems += [f"unexpected {name}" for name in sorted(actual.keys() - expected.keys())]
    problems += [f"hash differs: {name}" for name in sorted(expected.keys() & actual.keys()) if expected[name] != actual[name]]
    return problems


def generate(rom: Path, recompiler: Path, output: Path | None = None, *, replace: bool = False,
             recipe: Path = RECIPE, scratch: Path = SCRATCH,
             launch: Callable[[Path, Path], list[str]] | None = None) -> dict:
    """Run N64Recomp with the recipe and require the recorded hashes.

    ``launch(recompiler, config)`` returns the command line when the binary must run in a
    container or emulator; every path in it must resolve identically there.
    With ``output`` the verified result replaces it transactionally; without,
    only the comparison is made and the scratch directory is removed on success.
    """
    manifest = check(recipe)
    if not rom.is_file():
        raise RecipeError(f"ROM not found: {rom}")
    if sha1_file(rom) != ROM_SHA1:
        raise RecipeError("Unsupported ROM: expected the NTSC-U z64 (big-endian) profile, "
                          f"SHA-1 {ROM_SHA1}; normalize byte order first")
    if output is not None and output.exists() and not replace:
        raise RecipeError("Output already exists; pass --replace for a transactional successful replacement")
    scratch.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="recipe-", dir=scratch))
    produced = work / "generated"
    shutil.copy2(recipe / SYMBOLS, work / SYMBOLS)
    config = work / CONFIG
    config.write_text(render_config((recipe / CONFIG).read_text(), rom.resolve(), produced))
    with (work / "recompiler.log").open("w") as log_file:
        command = launch(recompiler.resolve(), config) if launch else [str(recompiler.resolve()), str(config)]
        result = subprocess.run(command, stdout=log_file, stderr=subprocess.STDOUT)
    log = (work / "recompiler.log").read_text()
    if result.returncode:
        raise RecipeError(f"Recompiler exited {result.returncode}; diagnostics: {work / 'recompiler.log'}")
    try:
        report = inspect_output(produced, log)
    except ValueError as error:
        raise RecipeError(f"{error}; diagnostics: {work / 'recompiler.log'}") from error
    if problems := compare(manifest["files"], report["files"]):
        raise RecipeError("Generated C does not match the recipe hashes (" + "; ".join(problems[:5]) +
                          f"); kept in {work}")
    if output is None:
        shutil.rmtree(work)
        return report
    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.rename(work / "previous-output")
    try:
        produced.rename(output)
    except BaseException:
        if (work / "previous-output").exists():
            (work / "previous-output").rename(output)
        raise
    return report


def recompiler_binary() -> Path:
    import build_recompiler
    import hostenv
    host = hostenv.current()
    if not build_recompiler.host_binary(host).is_file():
        build_recompiler.build_host(host)
    return build_recompiler.host_binary(host)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="validate the committed recipe (no ROM needed)")
    export_parser = commands.add_parser("export", help="refresh the recipe from a recompile analysis")
    export_parser.add_argument("--analysis", type=Path, required=True)
    export_parser.add_argument("--generated", type=Path, default=ROOT / "local/generated/game")
    generate_parser = commands.add_parser("generate", help="N64Recomp + recipe, compared with the recorded hashes")
    generate_parser.add_argument("rom", type=Path)
    generate_parser.add_argument("--output", type=Path, help="publish the verified result here (under local/)")
    generate_parser.add_argument("--replace", action="store_true")
    generate_parser.add_argument("--recompiler", type=Path)
    args = parser.parse_args()
    if args.command == "check":
        manifest = check()
        print(f"recipe OK: {len(manifest['files'])} generated file hashes")
    elif args.command == "export":
        print(export(args.analysis.resolve(), args.generated.resolve()))
    else:
        output = require_local_or(sys.exit, args.output, root=ROOT, what="Generated output") if args.output else None
        report = generate(args.rom.resolve(), args.recompiler or recompiler_binary(), output, replace=args.replace)
        print(f"PASS: {len(report['files'])} generated files match the recipe" + (f"; {output}" if output else ""))


if __name__ == "__main__":
    main()
