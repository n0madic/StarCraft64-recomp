#!/usr/bin/env python3
"""Validate libultra candidates using masked CRCs and full ELF .text bytes.

All generated reports belong in ignored local/. No ROM bytes are emitted.
The ROM/RAM displacement is an explicit assumption until a RAM dump confirms it.
Requires pyelftools only when --reference-root is supplied.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import zlib
from collections import Counter, defaultdict
from pathlib import Path


def number(value: str) -> int:
    return int(value, 0)


def mask_bytes(data: bytes, relocations: list[tuple[int, int]]) -> bytes:
    result = bytearray(data)
    for offset, kind in relocations:
        if offset + 4 > len(result):
            continue
        if kind == 4:
            result[offset] &= 0xFC
            result[offset + 1:offset + 4] = b"\0\0\0"
        elif kind in (5, 6):
            result[offset + 2:offset + 4] = b"\0\0"
        else:
            raise ValueError(f"Unsupported relocation type: {kind}")
    return bytes(result)


def strip_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", type=Path)
    parser.add_argument("--n64sym", type=Path, required=True)
    parser.add_argument("--recompiler", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--reference-root", type=Path)
    parser.add_argument("--dump", type=Path, help="Canonical big-endian RDRAM dump")
    parser.add_argument("--displacement", type=number, default=0x7FFFF400)
    parser.add_argument("--candidates", type=Path,
                        help="Existing n64sym splat output; defaults to output/n64sym-rom.splat.txt")
    parser.add_argument("--candidate-displacement", type=number,
                        help="Mapping used in the raw candidate file, if different from this section")
    parser.add_argument("--scan-start", type=number, default=0x1000)
    parser.add_argument("--scan-end", type=number, default=0xD6640)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rom = args.rom.read_bytes()
    if rom[:4] != bytes.fromhex("80371240"):
        raise ValueError("Provide a normalized z64 image")
    scan = rom[args.scan_start:args.scan_end]
    if not 0 <= args.scan_start < args.scan_end <= len(rom):
        parser.error("scan interval must be a nonempty bounded ROM section")
    ram = args.dump.read_bytes() if args.dump else None
    signatures = json.loads((args.n64sym / "web/signatures.json").read_text())
    candidate_file = args.candidates or args.output / "n64sym-rom.splat.txt"
    if not candidate_file.is_file():
        parser.error("run n64sym first, or pass its output with --candidates")
    candidate_displacement = args.candidate_displacement if args.candidate_displacement is not None else args.displacement
    candidates = set()
    for address in re.findall(r"= 0x([0-9A-Fa-f]+)", candidate_file.read_text()):
        address = int(address, 16)
        # The pinned ROM scanner incorrectly uses its displacement's high nibble
        # for R_MIPS_26. Actual MIPS J/JAL use the caller PC's high nibble.
        if 0x70000000 <= address < 0x70800000:
            address += 0x10000000
        offset = address - candidate_displacement
        if args.scan_start <= offset < args.scan_end:
            candidates.add(offset)
    kinds = {"targ26": 4, "hi16": 5, "lo16": 6}
    crc_hits = []
    relocation_hints = []
    for index, (name, size, crc_a, crc_b, groups) in enumerate(signatures):
        rels = sorted((offset, kinds[kind]) for kind, _, offsets in groups for offset in offsets)
        for offset in sorted(candidates):
            if offset + size > args.scan_end:
                continue
            prefix = mask_bytes(rom[offset:offset + min(size, 8)], rels)
            if zlib.crc32(prefix) != crc_a:
                continue
            body = rom[offset:offset + size]
            masked = mask_bytes(body, rels)
            if zlib.crc32(masked) != crc_b:
                continue
            address = offset + args.displacement
            ram_offset = address & 0x1FFFFFFF
            crc_hits.append({"name": name, "rom_offset": offset, "ram_address": address,
                             "size": size, "signature_index": index,
                             "masked_crc32": f"{crc_b:08x}",
                             "raw_sha256": hashlib.sha256(body).hexdigest(),
                             "ram_bytes_equal": ram is not None and ram[ram_offset:ram_offset + size] == body,
                             "evidence": "full masked CRC match, not reference-byte proof"})
            hi = {}
            for kind, relname, offsets in groups:
                for rel_offset in offsets:
                    word = struct.unpack_from(">I", body, rel_offset)[0]
                    if kind == "targ26":
                        target = ((address + rel_offset + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                    elif kind == "hi16":
                        hi[relname] = (word & 0xFFFF) << 16
                        continue
                    elif relname in hi:
                        low = word & 0xFFFF
                        target = (hi[relname] + (low if low < 0x8000 else low - 0x10000)) & 0xFFFFFFFF
                    else:
                        continue
                    relocation_hints.append({"name": relname, "ram_address": target,
                                             "parent_name": name, "parent_ram_address": address,
                                             "instruction_rom_offset": offset + rel_offset,
                                             "kind": kind, "evidence": "relocation-derived candidate"})

    references = []
    rejected_reference_relocations = []
    if args.reference_root:
        from elftools.elf.elffile import ELFFile
        for path in sorted(args.reference_root.glob("*/lib*ultra*/src/**/*.o")):
            with path.open("rb") as stream:
                elf = ELFFile(stream)
                text = elf.get_section_by_name(".text")
                if text is None or not text.data():
                    continue
                body = text.data()
                symtab = elf.get_section_by_name(".symtab")
                text_index = next(i for i, s in enumerate(elf.iter_sections()) if s.name == ".text")
                symbols = [{"name": s.name, "offset": s["st_value"], "size": s["st_size"]}
                           for s in symtab.iter_symbols()
                           if s["st_shndx"] == text_index and s.name and s["st_info"]["bind"] == "STB_GLOBAL"]
                # IDO emits a padding-only .text even for data-only objects.
                # Tiny empty stubs cannot identify a library member uniquely.
                if not symbols or len(body) <= 16:
                    continue
                rel_section = elf.get_section_by_name(".rel.text")
                rels = [(r["r_offset"], r["r_info_type"]) for r in rel_section.iter_relocations()] if rel_section else []
                rel_details = []
                if rel_section:
                    rel_symtab = elf.get_section(rel_section["sh_link"])
                    rel_details = [(r["r_offset"], r["r_info_type"], rel_symtab.get_symbol(r["r_info_sym"]).name)
                                   for r in rel_section.iter_relocations()]
                if any(kind not in (4, 5, 6) for _, kind in rels):
                    continue
                masked = mask_bytes(body, rels)
                forbidden = set()
                for offset, _ in rels:
                    forbidden.update(range(offset, offset + 4))
                spans = []
                start = 0
                for i in range(len(body) + 1):
                    if i == len(body) or i in forbidden:
                        if i > start:
                            spans.append((start, i))
                        start = i + 1
                if not spans:
                    continue
                start, end = max(spans, key=lambda pair: pair[1] - pair[0])
                if end - start < 8:
                    continue
                needle = body[start:end]
                found = scan.find(needle)
                while found >= 0:
                    rom_offset = args.scan_start + found - start
                    if rom_offset >= args.scan_start and rom_offset % 4 == 0:
                        actual = rom[rom_offset:rom_offset + len(body)]
                        if len(actual) == len(body) and mask_bytes(actual, rels) == masked:
                            ram_address = rom_offset + args.displacement
                            resolved = []
                            hi = {}
                            for rel_offset, kind, relname in rel_details:
                                word = struct.unpack_from(">I", actual, rel_offset)[0]
                                if kind == 4:
                                    target = ((ram_address + rel_offset + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
                                elif kind == 5:
                                    hi[relname] = (word & 0xFFFF) << 16
                                    continue
                                elif relname in hi:
                                    low = word & 0xFFFF
                                    target = (hi[relname] + (low if low < 0x8000 else low - 0x10000)) & 0xFFFFFFFF
                                else:
                                    continue
                                resolved.append({"name": relname, "target": target, "offset": rel_offset})
                            ram_offset = ram_address & 0x1FFFFFFF
                            ram_verified = ram is not None and ram[ram_offset:ram_offset + len(body)] == actual
                            references.append({"reference": str(path), "rom_offset": rom_offset,
                                               "ram_address": ram_address, "text_size": len(body),
                                               "relocation_count": len(rels), "symbols": symbols,
                                               "resolved_relocations": resolved,
                                               "ram_bytes_equal": ram_verified,
                                               "evidence": "full .text byte equality after masking documented MIPS relocation fields",
                                               "raw_sha256": hashlib.sha256(actual).hexdigest(),
                                               "masked_sha256": hashlib.sha256(masked).hexdigest()})
                    found = scan.find(needle, found + 1)

    # CRC signature names can themselves be aliases: PI and SI queue routines
    # have identical masked bodies. Establish target constraints exclusively
    # from reference objects whose matched body has one source-file family.
    groups = defaultdict(list)
    for hit in references:
        groups[(hit["rom_offset"], hit["masked_sha256"])].append(hit)
    known_targets = defaultdict(set)
    for group in groups.values():
        if len({Path(hit["reference"]).stem for hit in group}) != 1:
            continue
        for hit in group:
            for rel in hit["resolved_relocations"]:
                if rel["name"].startswith(("os", "__os")):
                    known_targets[rel["name"]].add(rel["target"])
    filtered = []
    for hit in references:
        conflicts = []
        for rel in hit["resolved_relocations"]:
            expected = known_targets.get(rel["name"], set())
            if len(expected) == 1 and rel["target"] not in expected:
                conflicts.append({"name": rel["name"], "actual": rel["target"], "known": sorted(expected)})
        # Symbol aliases must also agree with incoming calls from uniquely
        # recognized objects, not merely with address-stripped instruction bytes.
        for symbol in hit["symbols"]:
            expected = known_targets.get(symbol["name"], set())
            actual = hit["ram_address"] + symbol["offset"]
            if len(expected) == 1 and actual not in expected:
                conflicts.append({"name": symbol["name"], "actual": actual, "known": sorted(expected)})
        if conflicts:
            rejected_reference_relocations.append({"reference": hit["reference"],
                                                    "rom_offset": hit["rom_offset"], "conflicts": conflicts})
        else:
            filtered.append(hit)
    references = filtered

    # Read the committed (pinned) list, not the working tree: the Stage 1 patch
    # (tools/recompile/patch_recompiler.py) adds names to it, which must not
    # change what the pinned revision's replacement set contains.
    source = strip_comments(subprocess.check_output(
        ["git", "-C", str(args.recompiler), "show", "HEAD:src/symbol_lists.cpp"], text=True))
    reimplemented = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', source.split("reimplemented_funcs", 1)[1].split("};", 1)[0]))
    ignored = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', source.split("ignored_funcs", 1)[1].split("};", 1)[0]))
    wrappers = defaultdict(list)
    for path in sorted((args.runtime / "librecomp/src").glob("*.*")):
        text = strip_comments(path.read_text(errors="replace"))
        for match in re.finditer(r"\bvoid\s+([A-Za-z_][A-Za-z0-9_]*)_recomp\s*\([^;{}]*\)\s*\{", text):
            wrappers[match[1]].append(str(path))

    inventory = defaultdict(lambda: {"crc_bodies": [], "reference_bytes": [], "relocation_hints": []})
    for hit in crc_hits:
        inventory[(hit["name"], hit["ram_address"])]["crc_bodies"].append(hit)
    for hit in references:
        for symbol in hit["symbols"]:
            name, address = symbol["name"], hit["ram_address"] + symbol["offset"]
            inventory[(name, address)]["reference_bytes"].append({"reference": hit["reference"],
                                                                  "object_rom_offset": hit["rom_offset"],
                                                                  "text_size": hit["text_size"],
                                                                  "ram_bytes_equal": hit["ram_bytes_equal"]})
    for hit in relocation_hints:
        inventory[(hit["name"], hit["ram_address"])]["relocation_hints"].append(hit)
    rows = []
    for (name, address), evidence in sorted(inventory.items(), key=lambda pair: (pair[0][1], pair[0][0])):
        rows.append({"name": name, "ram_address": address,
                     "rom_offset": address - args.displacement if args.scan_start <= address - args.displacement < args.scan_end else None,
                     "reimplemented": name in reimplemented, "ignored_by_default": name in ignored,
                     "runtime_wrappers": wrappers[name],
                     "evidence_class": "REFERENCE-BYTES" if evidence["reference_bytes"] else
                     "MASKED-CRC" if evidence["crc_bodies"] else "RELOCATION-CANDIDATE", **evidence})
    def git_commit(path: Path) -> str:
        return subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()

    provenance = {"n64sym": git_commit(args.n64sym), "N64Recomp": git_commit(args.recompiler),
                  "N64ModernRuntime": git_commit(args.runtime)}
    if args.reference_root:
        provenance["ultralib"] = git_commit(args.reference_root.parent)
    report = {"rom_sha1": hashlib.sha1(rom).hexdigest(),
              "mapping": {"displacement": args.displacement, "scan_start": args.scan_start, "scan_end": args.scan_end,
                          "status": "explicit provisional resident mapping; per-object RAM equality reported independently"},
              "ram_sha256": hashlib.sha256(ram).hexdigest() if ram is not None else None,
              "source_revisions": provenance,
              "candidate_offset_count": len(candidates), "masked_crc_hits": crc_hits,
              "reference_matches": references, "inventory": rows,
              "rejected_reference_relocations": rejected_reference_relocations,
              "reimplemented_with_no_game_candidate": sorted(reimplemented - {row["name"] for row in rows}),
              "version_match_counts": dict(Counter(Path(hit["reference"]).relative_to(args.reference_root).parts[0] for hit in references)),
              "runtime_semantic_gaps": {
                  "osEPiWriteIo": "No runtime wrapper; ignored by default, custom save path must be intercepted above MMIO.",
                  "osEPiReadIo": "Runtime supports ROM PIO only; save-memory PIO path asserts.",
                  "osPiRawStartDma": "Runtime quick-exit diagnostic stub.",
                  "osEPiRawStartDma": "Runtime quick-exit diagnostic stub.",
                  "__osSpSetPc": "Runtime assert(false) wrapper.",
                  "osPfs*": "Runtime Pak wrappers return PFS_ERR_NOPACK; not working Pak persistence."},
              "limitations": ["Static matches do not prove execution.",
                              "Built-in signatures lack per-version provenance.",
                              "Reference code bodies may be identical between SDK versions.",
                              "No reference-byte match is a proof of absence; customized bodies can differ.",
                              "CRC-only and relocation-only candidates require separate validation."]}
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "inventory.json").write_text(json.dumps(report, indent=2) + "\n")
    header = "name\tram_address\trom_offset\tcrc_body_count\treference_byte_count\tref_version_families\trelocation_hint_count\treimplemented\truntime_wrappers\n"
    lines = [header]
    for row in rows:
        versions = sorted({Path(item["reference"]).relative_to(args.reference_root).parts[0] for item in row["reference_bytes"]})
        lines.append("\t".join([row["name"], f"0x{row['ram_address']:08X}",
                                f"0x{row['rom_offset']:08X}" if row["rom_offset"] is not None else "",
                                str(len(row["crc_bodies"])), str(len(row["reference_bytes"])),
                                ",".join(versions), str(len(row["relocation_hints"])),
                                str(row["reimplemented"]), ";".join(row["runtime_wrappers"])]) + "\n")
    (args.output / "inventory.tsv").write_text("".join(lines))
    accepted_names = {row["name"] for row in rows if row["reimplemented"] and
                      (row["reference_bytes"] or row["crc_bodies"])}
    accepted_lines = [header]
    for row, line in zip(rows, lines[1:]):
        if row["name"] in accepted_names and (row["reference_bytes"] or row["crc_bodies"]):
            accepted_lines.append(line)
    (args.output / "reimplemented-body-candidates.tsv").write_text("".join(accepted_lines))
    print(json.dumps({"rows": len(rows), "crc_hits": len(crc_hits), "reference_matches": len(references),
                      "reference_versions": report["version_match_counts"],
                      "reimplemented_candidates": sum(row["reimplemented"] for row in rows)}, indent=2))


if __name__ == "__main__":
    main()
