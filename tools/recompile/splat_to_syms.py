#!/usr/bin/env python3
"""Convert section-qualified spimdisasm function CSVs to N64Recomp TOML.

The CSV is emitted by spimdisasm's --function-info option (also usable with
splat's spimdisasm context). Addresses are never merged across overlays.
Unknown-instruction data labels (T_*) fail closed rather than becoming code.
"""
from __future__ import annotations

import argparse
import csv
import json
import re
from pathlib import Path

HEADER = "# SC64 NTSC-U symbol table: names, addresses and sizes only; no game bytes (ADR-084)."


def read_functions(path: Path, section: dict, identities: dict[int, str]) -> list[dict]:
    functions = []
    with path.open(newline="") as source:
        for row in csv.DictReader(source):
            address, size, rom = (int(row[key], 0) for key in ("address", "length", "vrom"))
            if row["name"].startswith("T_"):
                raise ValueError(f"Unclassified text/data boundary in {path}: {row['name']}")
            if address & 3 or rom & 3 or size <= 0 or size & 3:
                raise ValueError(f"Invalid function alignment/size: {row}")
            if rom != section["rom"] + address - section["vram"]:
                raise ValueError(f"Function mapping differs from section: {row}")
            if not section["rom"] <= rom < rom + size <= section["rom"] + section["size"]:
                raise ValueError(f"Function escapes section: {row}")
            name = identities.get(address) if section["name"] == "resident" else None
            name = name or f"sc64_{section['name']}_{address:08X}"
            if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9]*", name):
                raise ValueError(f"Not a C identifier: {name}")
            functions.append({"name": name, "vram": address, "size": size})
    return functions


def render_sections(sections: list[dict]) -> str:
    seen_names = set()
    lines = [HEADER]
    for section in sections:
        lines += ["", "[[section]]", f"name = {json.dumps(section['name'])}",
                  f"rom = 0x{section['rom']:X}", f"vram = 0x{section['vram']:08X}",
                  f"size = 0x{section['size']:X}", "functions = ["]
        previous_end = section["vram"]
        for function in sorted(section["functions"], key=lambda item: item["vram"]):
            if function["name"] in seen_names:
                raise ValueError(f"Duplicate function name: {function['name']}")
            if function["vram"] < previous_end:
                raise ValueError(f"Overlapping function in section {section['name']}: {function}")
            seen_names.add(function["name"])
            previous_end = function["vram"] + function["size"]
            lines.append(f"  {{ name = {json.dumps(function['name'])}, vram = 0x{function['vram']:08X}, size = 0x{function['size']:X} }},")
        lines.append("]")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path, help="Private JSON: sections with CSV paths and identities")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text())
    identities = {int(address, 0): name for address, name in manifest.get("identities", {}).items()}
    sections = manifest["sections"]
    for section in sections:
        section["functions"] = []
        for csv_file in section["function_csvs"]:
            section["functions"] += read_functions(args.manifest.parent / csv_file, section, identities)
    result = render_sections(sections)
    with args.output.open("x") as output:
        output.write(result)


if __name__ == "__main__":
    main()
