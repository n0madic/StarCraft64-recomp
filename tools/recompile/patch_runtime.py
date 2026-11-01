#!/usr/bin/env python3
"""Prepare a private, pinned N64ModernRuntime copy with SC64 PI/SP devices.

The upstream checkout is read-only. This tool verifies exact source hashes,
then regenerates only owned patched files from that source, making reapplication
idempotent while preserving a later host patch on unrelated files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

PIN = "cdf5abbd5026fef5c364c676e4667c45e42b6863"
HASHES = {
    "librecomp/src/overlays.cpp": "00707191aa9cb5c908c18992ad740d05c5ba5a9fd905a8d1594d79e93270019d",
    "librecomp/src/pi.cpp": "13b01743d2f179ea15880963a904a708aee60aaeba712d6375fa8d63eb4fdc17",
    "librecomp/src/sp.cpp": "498dff658ecae25f83b6e7e4b936f7688ea770fa0deef5b54abf562658f6e29a",
    "ultramodern/src/events.cpp": "6e97f155ae149a191128c9e941735a4e9948056787b39a8f67c26581866fac05",
    "ultramodern/include/ultramodern/ultramodern.hpp": "0942f7a52aa6345ecb6e5fd0b93ffacf05ecba46a6f6c4c0f06c6fd0c6bc30b8",
}


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError(f"source anchor must appear once: {old[:100]!r}")
    return text.replace(old, new, 1)


def patch_pi(text: str) -> str:
    text = replace_once(text, '#include "librecomp/files.hpp"', '#include "librecomp/files.hpp"\n#include "librecomp/overlays.hpp"\n#include "flash_device.hpp"\nextern "C" void sc64_load_rom_dma(uint32_t, int32_t, uint32_t);')
    text = replace_once(text, '    if (direction == 0) {\n        if (physical_addr >= recomp::rom_base) {', '''    auto completed = [&] {
        if (mq != NULLPTR) ultramodern::enqueue_external_message_src(mq, 0, false, ultramodern::EventMessageSource::Pi);
        else ultramodern::send_pi_message();
    };
    if (size == 0 || direction > 1) throw std::runtime_error("invalid PI DMA request");
    if (physical_addr >= recomp::sram_base && physical_addr < recomp::rom_base && recomp::flashram_allowed()) {
        sc64_flash_dma(rdram, static_cast<uint32_t>(rdram_address), physical_addr, size, direction);
        completed();
        return;
    }
    if (direction == 0) {
        if (physical_addr >= recomp::rom_base) {''')
    text = replace_once(text, '            recomp::do_rom_read(rdram, rdram_address, physical_addr, size);', '''            // A guest DMA can replace a shared-address executable overlay.
            // Do not run this from the host's initial 1MiB boot copy.
            recomp::do_rom_read(rdram, rdram_address, physical_addr, size);
            sc64_load_rom_dma(physical_addr - recomp::rom_base, static_cast<int32_t>(rdram_address), size);''')
    # Three queue completions in do_dma only (other runtime code is unchanged).
    begin = text.index('void do_dma(')
    end = text.index('extern "C" void osPiStartDma_recomp', begin)
    body = text[begin:end]
    original = 'ultramodern::enqueue_external_message_src(mq, 0, false, ultramodern::EventMessageSource::Pi);'
    if body.count(original) != 4:
        raise ValueError("PI completion anchors drifted")
    first = body.index(original)
    body = body[:first + len(original)] + body[first + len(original):].replace(original, 'completed();')
    text = text[:begin] + body + text[end:]
    text = replace_once(text, '''    if (physical_addr > recomp::rom_base) {
        // cart rom
        recomp::do_rom_pio(PASS_RDRAM dramAddr, physical_addr);
    } else {
        // sram
        assert(false && "SRAM ReadIo unimplemented");
    }''', '''    if (physical_addr >= recomp::rom_base) {
        recomp::do_rom_pio(PASS_RDRAM dramAddr, physical_addr);
    } else if (physical_addr >= recomp::sram_base && recomp::flashram_allowed()) {
        MEM_W(0, dramAddr) = sc64_flash_pio_read(physical_addr);
    } else {
        throw std::runtime_error("unsupported PI PIO read region");
    }''')
    # Replace raw-DMA diagnostic exits with the actual reference ABI and PI event.
    text = text[:text.index('extern "C" void osPiRawStartDma_recomp')]
    text += '''extern "C" void osEPiWriteIo_recomp(RDRAM_ARG recomp_context* ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    const uint32_t physical = k1_to_phys(handle->baseAddress | ctx->r5);
    if (!recomp::flashram_allowed()) throw std::runtime_error("PI write requires FlashRAM profile");
    sc64_flash_pio_write(physical, static_cast<uint32_t>(ctx->r6));
    ctx->r2 = 0;
}

extern "C" void osPiReadIo_recomp(RDRAM_ARG recomp_context* ctx) {
    const uint32_t physical = k1_to_phys(ctx->r4 | recomp::rom_base);
    recomp::do_rom_pio(PASS_RDRAM ctx->r5, physical);
    ctx->r2 = 0;
}

extern "C" void osPiWriteIo_recomp(RDRAM_ARG recomp_context* ctx) {
    // Match __osPiRawWriteIo's osRomBase OR address semantics.
    sc64_flash_pio_write(k1_to_phys(ctx->r4 | recomp::rom_base), static_cast<uint32_t>(ctx->r5));
    ctx->r2 = 0;
}

extern "C" void osPiRawStartDma_recomp(RDRAM_ARG recomp_context* ctx) {
    do_dma(PASS_RDRAM NULLPTR, ctx->r6, k1_to_phys(ctx->r5 | recomp::rom_base), ctx->r7, ctx->r4);
    ctx->r2 = 0;
}
extern "C" void osEPiRawStartDma_recomp(RDRAM_ARG recomp_context* ctx) {
    OSPiHandle* handle = TO_PTR(OSPiHandle, ctx->r4);
    do_dma(PASS_RDRAM NULLPTR, ctx->r7, k1_to_phys(handle->baseAddress | ctx->r6), MEM_W(0x10, ctx->r29), ctx->r5);
    ctx->r2 = 0;
}
extern "C" void __osPiRawStartDma_recomp(RDRAM_ARG recomp_context* ctx) { osPiRawStartDma_recomp(rdram, ctx); }
extern "C" void __osEPiRawStartDma_recomp(RDRAM_ARG recomp_context* ctx) { osEPiRawStartDma_recomp(rdram, ctx); }
extern "C" void __osEPiRawReadIo_recomp(RDRAM_ARG recomp_context* ctx) { osEPiReadIo_recomp(rdram, ctx); }
extern "C" void __osEPiRawWriteIo_recomp(RDRAM_ARG recomp_context* ctx) { osEPiWriteIo_recomp(rdram, ctx); }
'''
    return text


def patch_events(text: str) -> str:
    text = replace_once(text, '    } si;\n    // The same message queue', '    } si;\n    struct {\n        PTR(OSMesgQueue) mq = NULLPTR;\n        OSMesg msg = (OSMesg)0;\n    } pi;\n    // The same message queue')
    text = replace_once(text, '        case OS_EVENT_SP:\n', '        case OS_EVENT_PI:\n            events_context.pi.msg = msg;\n            events_context.pi.mq = mq_;\n            break;\n        case OS_EVENT_SP:\n')
    text = replace_once(text, 'void ultramodern::send_si_message() {', '''void ultramodern::send_pi_message() {
    std::lock_guard lock{events_context.message_mutex};
    if (events_context.pi.mq != NULLPTR) {
        ultramodern::enqueue_external_message_src(events_context.pi.mq, events_context.pi.msg, false, ultramodern::EventMessageSource::Pi);
    }
}

void ultramodern::send_si_message() {''')
    return text


def prepare(source: Path, destination: Path) -> None:
    source, destination = source.resolve(), destination.resolve()
    if source == destination or source in destination.parents:
        raise ValueError("runtime destination must be a separate checkout directory")
    head = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise ValueError(f"runtime revision must be {PIN}, got {head}")
    texts = {}
    for relative, expected in HASHES.items():
        payload = (source / relative).read_bytes()
        if hashlib.sha256(payload).hexdigest() != expected:
            raise ValueError(f"pinned runtime source changed: {relative}")
        texts[relative] = payload.decode()
    patched = {
        "librecomp/src/pi.cpp": patch_pi(texts["librecomp/src/pi.cpp"]),
        "librecomp/src/overlays.cpp": texts["librecomp/src/overlays.cpp"] + OVERLAY_DMA_HELPER,
        "librecomp/src/sp.cpp": replace_once(texts["librecomp/src/sp.cpp"], '''extern "C" void __osSpSetPc_recomp(uint8_t* rdram, recomp_context* ctx) {
    assert(false);
}''', '''extern "C" void __osSpSetPc_recomp(uint8_t* rdram, recomp_context* ctx) {
    // HLE submits the OSTask descriptor; no RSP PC is executed. Loading a task
    // therefore succeeds immediately while the emulated SP is idle.
    ctx->r2 = 0;
}'''),
        "ultramodern/src/events.cpp": patch_events(texts["ultramodern/src/events.cpp"]),
        "ultramodern/include/ultramodern/ultramodern.hpp": replace_once(texts["ultramodern/include/ultramodern/ultramodern.hpp"], 'void send_si_message();', 'void send_si_message();\nvoid send_pi_message();'),
    }
    marker = destination / ".sc64-device-patch.json"
    if destination.exists() and not marker.is_file():
        raise ValueError("existing runtime destination is not owned by this patcher")
    if not destination.exists():
        shutil.copytree(source, destination, ignore=shutil.ignore_patterns(".git", "build", "__pycache__"))
    for relative, content in patched.items():
        (destination / relative).write_bytes(content.encode('utf-8'))
    manifest = {"upstream_revision": PIN, "original_sha256": HASHES, "patched_sha256": {
        relative: hashlib.sha256(content.encode()).hexdigest() for relative, content in patched.items()
    }}
    marker.write_text(json.dumps(manifest, indent=2) + "\n")


OVERLAY_DMA_HELPER = r'''
// SC64 executable overlays share a destination but can have different sizes.
// Only complete registered executable ROM sections change function identities;
// asset DMA into resident data must not invalidate the resident section.
extern "C" void sc64_load_rom_dma(uint32_t rom, int32_t ram, uint32_t size) {
    const uint64_t rom_end = uint64_t(rom) + size;
    for (size_t index = 0; index < sections_info.num_code_sections; ++index) {
        const auto& section = sections_info.code_sections[index];
        if (section.rom_addr < rom || uint64_t(section.rom_addr) + section.size > rom_end) continue;
        const int32_t destination = static_cast<int32_t>(uint32_t(ram) + section.rom_addr - rom);
        const int64_t begin = destination;
        const int64_t end = begin + section.size;
        for (auto old = loaded_sections.begin(); old != loaded_sections.end();) {
            const auto& previous = sections_info.code_sections[old->section_table_index];
            const int64_t old_begin = old->loaded_ram_addr;
            const int64_t old_end = old_begin + previous.size;
            if (begin < old_end && old_begin < end) {
                for (size_t function_index = 0; function_index < previous.num_funcs; ++function_index) {
                    const auto& function = previous.funcs[function_index];
                    func_map.erase(static_cast<int32_t>(uint32_t(old->loaded_ram_addr) + function.offset));
                }
                section_addresses[previous.index] = previous.ram_addr;
                old = loaded_sections.erase(old);
            } else ++old;
        }
        load_overlay(index, destination);
    }
}
'''

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination)
    print(f"Prepared pinned SC64 runtime: {args.destination}")


if __name__ == "__main__":
    main()
