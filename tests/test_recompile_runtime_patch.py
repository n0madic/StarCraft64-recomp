from __future__ import annotations

import hashlib
import json
import importlib.util
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("sc64_runtime_patch", ROOT / "tools/recompile/patch_runtime.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class RuntimePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.original = ROOT / "third_party/N64ModernRuntime"
        if not all((cls.original / name).is_file() for name in MODULE.HASHES):
            raise unittest.SkipTest("pinned local runtime source is unavailable")

    def source_fixture(self, directory: Path) -> None:
        for name in MODULE.HASHES:
            target = directory / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes((self.original / name).read_bytes())
        (directory / "unrelated.txt").write_text("upstream file\n")

    def test_prepare_is_idempotent_and_upstream_unchanged(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, destination = root / "source", root / "copy"
            self.source_fixture(source)
            original = {name: (source / name).read_bytes() for name in MODULE.HASHES}
            with patch.object(MODULE.subprocess, "check_output", return_value=MODULE.PIN + "\n"):
                MODULE.prepare(source, destination)
                first = {name: (destination / name).read_bytes() for name in MODULE.HASHES}
                manifest = json.loads((destination / '.sc64-device-patch.json').read_text())
                for name, digest in manifest['patched_sha256'].items():
                    self.assertEqual(hashlib.sha256((destination / name).read_bytes()).hexdigest(), digest)
                (destination / "host-owned.txt").write_text("preserve\n")
                MODULE.prepare(source, destination)
            self.assertEqual(first, {name: (destination / name).read_bytes() for name in MODULE.HASHES})
            self.assertEqual(original, {name: (source / name).read_bytes() for name in MODULE.HASHES})
            self.assertEqual((destination / "host-owned.txt").read_text(), "preserve\n")
            self.assertIn(b"sc64_load_rom_dma", first["librecomp/src/pi.cpp"])
            self.assertIn(b"send_pi_message", first["ultramodern/src/events.cpp"])

    def test_changed_source_rejected_before_destination_creation(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, destination = root / "source", root / "copy"
            self.source_fixture(source)
            (source / "librecomp/src/pi.cpp").write_text("changed source\n")
            with patch.object(MODULE.subprocess, "check_output", return_value=MODULE.PIN + "\n"):
                with self.assertRaisesRegex(ValueError, "source changed"):
                    MODULE.prepare(source, destination)
            self.assertFalse(destination.exists())

    def test_wrong_pin_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, destination = root / "source", root / "copy"
            self.source_fixture(source)
            with patch.object(MODULE.subprocess, "check_output", return_value="wrong\n"):
                with self.assertRaisesRegex(ValueError, "revision must"):
                    MODULE.prepare(source, destination)
            self.assertFalse(destination.exists())

    def test_unowned_existing_destination_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, destination = root / "source", root / "copy"
            self.source_fixture(source)
            destination.mkdir()
            (destination / "preserve.txt").write_text("user content\n")
            with patch.object(MODULE.subprocess, "check_output", return_value=MODULE.PIN + "\n"):
                with self.assertRaisesRegex(ValueError, "not owned"):
                    MODULE.prepare(source, destination)
            self.assertEqual((destination / "preserve.txt").read_text(), "user content\n")

    def test_source_sha256_is_the_pinned_set(self) -> None:
        for name, expected in MODULE.HASHES.items():
            self.assertEqual(hashlib.sha256((self.original / name).read_bytes()).hexdigest(), expected, name)

    def test_overlay_replacement_uses_complete_rom_sections(self) -> None:
        compiler = shutil.which("clang++") or shutil.which("c++")
        if compiler is None:
            self.skipTest("C++20 compiler unavailable")
        fixture = r'''#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <stdexcept>
using Function = void (*)();
void resident() {} void menu() {} void game() {} void game_tail() {}
struct FuncEntry { Function func; uint32_t offset; };
struct Section { uint32_t rom_addr; uint32_t size; int32_t ram_addr; const FuncEntry* funcs; size_t num_funcs; size_t index; };
struct LoadedSection { int32_t loaded_ram_addr; size_t section_table_index; };
const FuncEntry resident_functions[] = {{resident, 0}};
const FuncEntry menu_functions[] = {{menu, 0}};
const FuncEntry game_functions[] = {{game, 0}, {game_tail, 0x40000}};
Section sections[] = {
    {0x1000, 0xd5640, static_cast<int32_t>(0x80000400u), resident_functions, 1, 0},
    {0xd6640, 0xd2a0, static_cast<int32_t>(0x8011fbf0u), menu_functions, 1, 1},
    {0xe38e0, 0x49130, static_cast<int32_t>(0x8011fbf0u), game_functions, 2, 2},
};
struct { Section* code_sections; size_t num_code_sections; } sections_info{sections, 3};
std::vector<LoadedSection> loaded_sections;
std::unordered_map<int32_t, Function> func_map;
int32_t section_addresses[3];
void load_overlay(size_t index, int32_t ram) {
    const auto& section = sections[index];
    for (size_t i = 0; i < section.num_funcs; ++i) func_map[static_cast<int32_t>(uint32_t(ram) + section.funcs[i].offset)] = section.funcs[i].func;
    loaded_sections.push_back({ram, index});
    section_addresses[index] = ram;
}
''' + MODULE.OVERLAY_DMA_HELPER + r'''
void require(bool ok) { if (!ok) throw std::runtime_error("overlay regression"); }
int main() {
    const int32_t destination = static_cast<int32_t>(0x8011fbf0u);
    const int32_t resident_address = static_cast<int32_t>(0x80000400u);
    const int32_t tail = static_cast<int32_t>(uint32_t(destination) + 0x40000);
    load_overlay(0, resident_address);
    sc64_load_rom_dma(0xd6640, destination, 0xd2a0);
    require(func_map.at(destination) == menu);
    sc64_load_rom_dma(0xe38e0, destination, 0x49130);
    require(func_map.at(destination) == game && func_map.at(tail) == game_tail);
    sc64_load_rom_dma(0xd6640, destination, 0xd2a0);
    require(func_map.at(destination) == menu && !func_map.contains(tail));
    require(loaded_sections.size() == 2 && func_map.at(resident_address) == resident);
    sc64_load_rom_dma(0xe38e0, destination, 0x100);
    require(func_map.at(destination) == menu && loaded_sections.size() == 2);
    sc64_load_rom_dma(0x1000000, resident_address + 0x200, 0x200);
    require(func_map.at(resident_address) == resident && loaded_sections.size() == 2);
    sc64_load_rom_dma(0xd6640, destination, 0xd2a0);
    require(loaded_sections.size() == 2);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            source, executable = Path(temp) / "fixture.cpp", Path(temp) / "fixture"
            source.write_text(fixture)
            subprocess.run([compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", str(source), "-o", str(executable)], check=True, capture_output=True)
            subprocess.run([str(executable)], check=True, capture_output=True)

    def test_duplicate_anchor_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "once"):
            MODULE.replace_once("anchor anchor", "anchor", "replacement")


if __name__ == "__main__":
    unittest.main()
