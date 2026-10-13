#!/usr/bin/env python3
"""Expose verified lower-level PI calls to the Stage 1 host, without game stubs."""
from __future__ import annotations

import argparse
import subprocess
from pathlib import Path

PIN = "ffb39cdad1da5de07eaaa48bd1db4a89a7986771"
FUNCTIONS = ["osEPiWriteIo", "__osEPiRawReadIo", "__osEPiRawWriteIo",
             "__osPiRawStartDma", "osPiRawStartDma", "__osEPiRawStartDma", "osViGetCurrentLine"]
ANCHOR = 'const std::unordered_set<std::string> N64Recomp::reimplemented_funcs {\n'
BLOCK = "    // SC64 Stage 1 verified PI boundary: real host implementations, no stubs.\n" + "".join(
    f'    "{name}",\n' for name in FUNCTIONS)
TABLE_ANCHOR = """        // TODO this assumes that the jump table is in the same section as the function itself
        cur_jtbl.rom = cur_jtbl.vram + func.rom - func.vram;
        cur_jtbl.section_index = func.section_index;

        while (vram < end_address) {
            // Retrieve the current entry of the jump table
            // TODO same as above
            uint32_t rom_addr = vram + func.rom - func.vram;
            uint32_t jtbl_word = byteswap(*reinterpret_cast<const uint32_t*>(&context.rom[rom_addr]));
"""
TABLE_REPLACEMENT = """        // SC64: overlay code refers to switch tables in resident rodata.
        // Keep current-section precedence for overlapping overlay addresses;
        // otherwise require one unique containing section, with bounded reads.
        size_t table_section_index = func.section_index;
        auto contains_table = [&](size_t index) {
            const auto& candidate = context.sections[index];
            return cur_jtbl.vram >= candidate.ram_addr &&
                uint64_t(cur_jtbl.vram) + 4 <= uint64_t(candidate.ram_addr) + candidate.size;
        };
        if (!contains_table(table_section_index)) {
            table_section_index = context.sections.size();
            for (size_t index = 0; index < context.sections.size(); ++index) {
                if (contains_table(index)) {
                    if (table_section_index != context.sections.size()) {
                        fmt::print("Ambiguous jump table section at 0x{:08X}\\n", cur_jtbl.vram);
                        return false;
                    }
                    table_section_index = index;
                }
            }
        }
        if (table_section_index == context.sections.size()) {
            fmt::print("Missing jump table section at 0x{:08X}\\n", cur_jtbl.vram);
            return false;
        }
        const auto& table_section = context.sections[table_section_index];
        cur_jtbl.rom = cur_jtbl.vram - table_section.ram_addr + table_section.rom_addr;
        cur_jtbl.section_index = table_section_index;

        while (vram < end_address && uint64_t(vram) + 4 <= uint64_t(table_section.ram_addr) + table_section.size) {
            uint32_t rom_addr = vram - table_section.ram_addr + table_section.rom_addr;
            if (uint64_t(rom_addr) + 4 > context.rom.size()) {
                return false;
            }
            uint32_t jtbl_word = byteswap(*reinterpret_cast<const uint32_t*>(&context.rom[rom_addr]));
"""


# Generated C is compared by SHA-256 (ADR-084), so every host must write the same bytes:
# a text-mode ofstream would turn each LF into CRLF on Windows.
BINARY_OUTPUTS = (
    ('std::ofstream func_header_file{ config.output_func_path / "funcs.h" };',
     'std::ofstream func_header_file{ config.output_func_path / "funcs.h", std::ios::binary };'),
    ('current_output_file = std::ofstream{config.output_func_path / fmt::format("funcs_{}.c", output_file_count)};',
     'current_output_file = std::ofstream{config.output_func_path / fmt::format("funcs_{}.c", output_file_count), std::ios::binary};'),
    ('std::ofstream lookup_file{ config.output_func_path / "lookup.cpp" };',
     'std::ofstream lookup_file{ config.output_func_path / "lookup.cpp", std::ios::binary };'),
    ('std::ofstream overlay_file(config.output_func_path / "recomp_overlays.inl");',
     'std::ofstream overlay_file(config.output_func_path / "recomp_overlays.inl", std::ios::binary);'),
)


def apply_source_change(source: Path, relative: str, anchor: str, replacement: str,
                        previous_replacements: tuple[str, ...] = ()) -> None:
    path = source / relative
    original = subprocess.check_output(["git", "-C", str(source), "show", f"HEAD:{relative}"], text=True)
    if original.count(anchor) != 1:
        raise SystemExit(f"Recompiler source anchor changed: {relative}")
    patched = original.replace(anchor, replacement)
    previous = tuple(original.replace(anchor, old) for old in previous_replacements)
    if path.read_text() not in (original, patched, *previous):
        raise SystemExit(f"Refusing to overwrite unrelated recompiler source changes: {relative}")
    path.write_text(patched)


def apply_source_changes(source: Path, relative: str, changes: tuple[tuple[str, str], ...]) -> None:
    """Several independent anchor replacements in one file, all checked before writing."""
    path = source / relative
    original = subprocess.check_output(["git", "-C", str(source), "show", f"HEAD:{relative}"], text=True)
    patched = original
    for anchor, replacement in changes:
        if original.count(anchor) != 1:
            raise SystemExit(f"Recompiler source anchor changed: {relative}")
        patched = patched.replace(anchor, replacement)
    if path.read_text() not in (original, patched):
        raise SystemExit(f"Refusing to overwrite unrelated recompiler source changes: {relative}")
    path.write_text(patched)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip()
    if revision != PIN:
        raise SystemExit(f"Source revision mismatch: expected {PIN}, got {revision}")
    legacy_block = "    // SC64 Stage 1 verified PI boundary: real host implementations, no stubs.\n" + "".join(
        f'    "{name}",\n' for name in FUNCTIONS[:-1])
    apply_source_change(args.source, "src/symbol_lists.cpp", ANCHOR, ANCHOR + BLOCK, (ANCHOR + legacy_block,))
    apply_source_change(args.source, "src/analysis.cpp", TABLE_ANCHOR, TABLE_REPLACEMENT)
    apply_source_changes(args.source, "src/main.cpp", BINARY_OUTPUTS)


if __name__ == "__main__":
    main()
