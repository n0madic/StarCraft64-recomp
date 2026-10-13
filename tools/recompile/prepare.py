#!/usr/bin/env python3
"""Analyze the verified NTSC-U profile locally and prepare N64Recomp inputs.

Requires the pinned spimdisasm/splat environment. Output contains ROM-derived
disassembly and must remain in local/. This tool creates no game stubs.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
import sys
import struct
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local_or
from rom_info import NTSC_U_SHA1 as ROM_SHA1
from splat_to_syms import read_functions, render_sections
from discover_polls import find_memory_waits

SECTIONS = [
    {"name": "resident", "rom": 0x1000, "vram": 0x80000400, "size": 0xD5640,
     "code": [(0x1000, 0x1118), (0x5370, 0x8F8F0), (0x91290, 0xA2360)]},
    {"name": "menu", "rom": 0xD6640, "vram": 0x8011FBF0, "size": 0xD2A0,
     "code": [(0xD6640, 0xE38E0)]},
    {"name": "game", "rom": 0xE38E0, "vram": 0x8011FBF0, "size": 0x49130,
     "code": [(0xE38E0, 0x12CA10)]},
]


def reviewed_identities(inventory: dict) -> dict[int, str]:
    """Use body evidence, never relocation-only suggestions or duplicate aliases."""
    groups: dict[int, list[dict]] = {}
    for row in inventory["inventory"]:
        if (row["ignored_by_default"] or row["reimplemented"]) and (row["crc_bodies"] or row["reference_bytes"]):
            groups.setdefault(row["ram_address"], []).append(row)
    result = {}
    for address, rows in groups.items():
        # Full body matches take precedence over reference relocation labels;
        # aliases with no distinct bytes remain represented by one identity.
        rows.sort(key=lambda row: (-bool(row["reimplemented"]), -len(row["reference_bytes"]),
                                   -len(row["crc_bodies"]), row["name"]))
        result[address] = rows[0]["name"]
    # Generic eight-byte dummy bodies have many byte-identical occurrences.
    # They must retain unique section/address-derived names.
    seen = {}
    for address, name in result.items():
        seen.setdefault(name, []).append(address)
    return {address: name for address, name in result.items() if len(seen[name]) == 1}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--inventory", type=Path, default=Path("local/reports/libultra-resident/inventory.json"))
    parser.add_argument("--output", type=Path, default=Path("local/recompile/recompile"))
    parser.add_argument("--extra-functions", type=Path,
                        help="Private evidence JSON: rom_sha1, functions [{section,vram,reason}]")
    args = parser.parse_args()
    rom = args.rom.resolve()
    rom_bytes = rom.read_bytes()
    if hashlib.sha1(rom_bytes).hexdigest() != ROM_SHA1:
        raise SystemExit("Unsupported ROM: expected the verified NTSC-U z64 profile")
    root = Path(__file__).resolve().parents[2]
    output = require_local_or(sys.exit, args.output, root=root, what="ROM-derived output")
    output.mkdir(parents=True, exist_ok=False)
    inventory = json.loads(args.inventory.read_text())
    if inventory["rom_sha1"] != ROM_SHA1:
        raise SystemExit("Inventory belongs to a different ROM")
    counter_references = []
    for match in inventory["reference_matches"]:
        relocations = [item for item in match["resolved_relocations"] if item.get("name") == "__osViIntrCount"]
        if relocations:
            if not match["ram_bytes_equal"] or any(item["target"] != 0x80112100 for item in relocations):
                raise SystemExit("Libultra VI counter relocation proof differs from the verified profile")
            counter_references.append({key: match[key] for key in (
                "reference", "rom_offset", "ram_address", "text_size", "relocation_count", "ram_bytes_equal", "masked_sha256")}
                | {"resolved_relocations": relocations})
    if not any("vimgr.o" in item["reference"] for item in counter_references) or not any(
            "timerintr.o" in item["reference"] for item in counter_references):
        raise SystemExit("Missing independent VI counter reference objects")
    (output / "vi-counter-evidence.json").write_text(json.dumps({
        "rom_sha1": ROM_SHA1, "source_inventory_sha256": hashlib.sha256(args.inventory.read_bytes()).hexdigest(),
        "libultra_counter": {"name": "__osViIntrCount", "vram": 0x80112100, "evidence": counter_references},
        "game_counter": {"vram": 0x80111670, "writer_function": 0x800957CC,
                         "load_vram": 0x80095840, "increment_vram": 0x80095848, "store_vram": 0x8009584C,
                         "method": "original disassembly: LW/ADDIU/SW after the guest VI-event queue receive",
                         "status": "guest-owned; host polls schedule the original writer"}}, indent=2) + "\n")
    identities = reviewed_identities(inventory)
    # The J-family inventory omitted this four-instruction leaf. Verify its
    # complete body against the IO_READ(AI_STATUS_REG) implementation and the
    # register definition, rather than treating its native SIGBUS as a stub.
    ai_status_body = rom_bytes[0x9D290:0x9D2A0]
    if hashlib.sha256(ai_status_body).hexdigest() != "ca2590df63a0994d2295be9346906b5706c52e06e06a133776138a7b7929c473":
        raise SystemExit("Verified osAiGetStatus body changed")
    identities[0x8009C690] = "osAiGetStatus"
    vi_line_body = rom_bytes[0x99D00:0x99D10]
    if hashlib.sha256(vi_line_body).hexdigest() != "0af18846622724466d1caf495af57a8c9d41df8b856f03721d81fc5ee2785d5e":
        raise SystemExit("Verified osViGetCurrentLine body changed")
    identities[0x80099100] = "osViGetCurrentLine"
    # Analyze direct calls from every overlay before disassembling any one
    # section. Otherwise spimdisasm can merge a never-returning scheduler loop
    # with a following function referenced only by the other overlay.
    # osCreateThread receives 800049AC via an address pair; a direct-call-only
    # graph misses it because it follows the never-returning idle loop. This
    # entry was confirmed by the original setup and native lookup failure.
    extra_functions = [{"section": "resident", "vram": 0x800049AC,
                        "reason": "original osCreateThread entry argument; native boot lookup"},
                       {"section": "resident", "vram": 0x800958D8,
                        "reason": "original osCreateThread entry argument; native boot-03 lookup"}]
    if args.extra_functions:
        extra = json.loads(args.extra_functions.read_text())
        if extra["rom_sha1"] != ROM_SHA1:
            raise SystemExit("Extra function evidence belongs to a different ROM")
        extra_functions += extra["functions"]
    direct_targets = []
    for definition in SECTIONS:
        for start, end in definition["code"]:
            for offset in range(start, end, 4):
                word = struct.unpack_from(">I", rom_bytes, offset)[0]
                if word >> 26 == 3:
                    pc = definition["vram"] + offset - definition["rom"]
                    direct_targets.append((definition["name"], ((pc + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)))
    manifest = {"rom_sha1": ROM_SHA1, "identities": {hex(k): v for k, v in identities.items()}, "sections": []}
    sections = []
    for definition in SECTIONS:
        section = dict(definition, functions=[], function_csvs=[])
        section_targets = set()
        for caller_section, address in direct_targets:
            address_rom = definition["rom"] + address - definition["vram"]
            if any(start <= address_rom < end for start, end in definition["code"]):
                containing = sum(s["vram"] <= address < s["vram"] + s["size"] for s in SECTIONS)
                # A resident call into an overlapping overlay region must not
                # create a false entry in the other overlay's unrelated body.
                if caller_section == definition["name"] or containing == 1:
                    section_targets.add(address)
        labels = {address: f"sc64_{definition['name']}_{address:08X}" for address in section_targets}
        for extra in extra_functions:
            if extra["section"] == definition["name"]:
                address = extra["vram"]
                offset = definition["rom"] + address - definition["vram"]
                if address & 3 or not extra["reason"] or not any(start <= offset < end for start, end in definition["code"]):
                    raise SystemExit(f"Invalid extra function evidence: {extra}")
                labels[address] = f"sc64_{definition['name']}_{address:08X}"
        if definition["name"] == "resident":
            labels.update(identities)
        symbol_file = output / f"{definition['name']}.symbol_addrs.txt"
        symbol_file.write_text("".join(f"{name} = 0x{address:08X}; // type:func\n"
                                       for address, name in sorted(labels.items())))
        for index, (start, end) in enumerate(definition["code"]):
            stem = f"{definition['name']}-{index}"
            csv_path = output / f"{stem}.functions.csv"
            command = [sys.executable, "-m", "spimdisasm", "singleFileDisasm", str(rom), str(output / stem),
                       "--start", hex(start), "--end", hex(end),
                       "--vram", hex(definition["vram"] + start - definition["rom"]),
                       "--compiler", "GCC", "--arch-level", "MIPS3", "--function-info", str(csv_path),
                       "--save-context", str(output / f"{stem}.context.csv")]
            command += ["--symbol-addrs", str(symbol_file)]
            with (output / f"{stem}.log").open("w") as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            section["functions"] += read_functions(csv_path, definition, identities)
            section["function_csvs"].append(csv_path.name)
        manifest["sections"].append({k: v for k, v in section.items() if k != "functions"})
        sections.append(section)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "extra-functions.json").write_text(json.dumps({"rom_sha1": ROM_SHA1, "functions": extra_functions}, indent=2) + "\n")
    (output / "additional-identities.json").write_text(json.dumps([{
        "name": "osAiGetStatus", "vram": 0x8009C690, "rom": 0x9D290, "size": 16,
        "body_sha256": hashlib.sha256(ai_status_body).hexdigest(),
        "evidence": "complete leaf loads 0xA450000C; pinned ultralib aigetstat.c/PR/rcp.h; native boot-04 SIGBUS",
        "sources": ["third_party/ultralib/src/io/aigetstat.c", "third_party/ultralib/include/PR/rcp.h",
                    "third_party/N64ModernRuntime/librecomp/src/ai.cpp"]}, {
        "name": "osViGetCurrentLine", "vram": 0x80099100, "rom": 0x99D00, "size": 16,
        "body_sha256": hashlib.sha256(vi_line_body).hexdigest(),
        "evidence": "complete leaf loads VI_CURRENT at 0xA4400010; pinned ultralib vigetline.c/PR/rcp.h; CPU scanline-wait call sites",
        "sources": ["third_party/ultralib/src/io/vigetline.c", "third_party/ultralib/include/PR/rcp.h"]}], indent=2) + "\n")
    (output / "sc64.syms.toml").write_text(render_sections(sections))
    # Guest caches do not exist in librecomp's coherent host RDRAM model. The
    # custom startup cache walk contains CACHE instructions outside identified
    # libultra routines. Remove only these instruction effects, not their CPU
    # control flow. Every removed instruction is recorded for audit (D-021).
    instruction_patches = []
    ignored_body_names = {row["name"] for row in inventory["inventory"] if row["ignored_by_default"] or row["reimplemented"]}
    for section in sections:
        for function in section["functions"]:
            if function["name"] in ignored_body_names:
                continue
            start = section["rom"] + function["vram"] - section["vram"]
            for offset in range(0, function["size"], 4):
                word = struct.unpack_from(">I", rom_bytes, start + offset)[0]
                if word >> 26 == 0x2F:
                    instruction_patches.append({"func": function["name"], "vram": function["vram"] + offset,
                                                "rom": start + offset, "original_word": word,
                                                "value": 0, "reason": "coherent host RDRAM; guest CACHE maintenance only",
                                                "debt": "D-021"})
    config = "\n".join(["[input]", "entrypoint = 0x80000400", "symbols_file_path = \"sc64.syms.toml\"",
                        f"rom_file_path = {json.dumps(str(rom))}", "output_func_path = \"../../generated/game\"",
                        "functions_per_output_file = 50", "uses_mips3_float_mode = false", "",
                        "[patches]", "stubs = []", "ignored = []", "renamed = []", ""])
    for patch in instruction_patches:
        config += f"\n[[patches.instruction]]\nfunc = {json.dumps(patch['func'])}\nvram = 0x{patch['vram']:08X}\nvalue = 0\n"
    idle = next(function for section in sections if section["name"] == "resident"
                for function in section["functions"]
                if function["vram"] <= 0x80004994 < function["vram"] + function["size"])
    # The original idle loop expects hardware interrupts. ultramodern schedules
    # guest threads cooperatively, so the host must poll at this safe point.
    # This is a real host callback; it does not replace game logic (D-023).
    if struct.unpack_from(">I", rom_bytes, 0x5594)[0] != 0x5040FFFF:
        raise SystemExit("Idle loop anchor changed")
    config += ("\n[[patches.hook]]\n" + f"func = {json.dumps(idle['name'])}\n"
               "before_vram = 0x80004994\n"
               'text = "{ extern void sc64_idle_poll(uint8_t*); sc64_idle_poll(rdram); }"\n')
    polls = find_memory_waits(rom_bytes, sections, ignored_body_names)
    # This adjacent loop compares current/next VI framebuffer through two real
    # OS wrappers rather than an inline memory load. It likewise needs host
    # scheduling while waiting for the guest VI worker (D-025).
    framebuffer_wait = next(function for section in sections if section["name"] == "resident"
                            for function in section["functions"] if function["vram"] == 0x800925AC)
    if struct.unpack_from(">I", rom_bytes, 0x931B8)[0] != 0x0C026430:
        raise SystemExit("VI framebuffer wait-loop call anchor changed")
    polls.append({"func": framebuffer_wait["name"], "section": "resident", "vram": 0x800925B8,
                  "rom": 0x931B8, "branch_vram": 0x800925C8,
                  "body_sha256": hashlib.sha256(rom_bytes[0x931B8:0x931D0]).hexdigest(),
                  "reason": "wait for current/next VI framebuffer equality through OS getters", "debt": "D-025"})
    for poll in polls:
        config += ("\n[[patches.hook]]\n" + f"func = {json.dumps(poll['func'])}\n"
                   f"before_vram = 0x{poll['vram']:08X}\n"
                   'text = "{ extern void sc64_guest_poll(uint8_t*); sc64_guest_poll(rdram); }"\n')
    (output / "poll-hooks.json").write_text(json.dumps(polls, indent=2) + "\n")
    (output / "sc64.toml").write_text(config)
    (output / "instruction-patches.json").write_text(json.dumps(instruction_patches, indent=2) + "\n")
    (output / "summary.json").write_text(json.dumps({"rom_sha1": ROM_SHA1,
        "sections": [{"name": s["name"], "functions": len(s["functions"]), "rom": s["rom"],
                      "vram": s["vram"], "size": s["size"], "code": s["code"]} for s in sections],
        "identified_symbols": len(identities), "stubs": []}, indent=2) + "\n")
    print(output / "sc64.toml")


if __name__ == "__main__":
    main()
