#pragma once
#include <cstdint>
#include <span>

// NTSC-U guest addresses shared by the live-RAM observers and the canonical
// snapshot readers (research.md records how each was verified). Observers read
// live word-swapped RAM through the virtual address; snapshot readers index the
// canonical 8 MiB image with physical().
namespace sc64::runtime::guest {
constexpr std::uint32_t game_mode = 0x800a5ab4;     // s16: 3 mission, 4 frontend menus
constexpr std::uint32_t split_screen = 0x800afefc;  // u8: non-zero in split-screen
constexpr std::uint32_t physical(std::uint32_t virtual_address) { return virtual_address & 0x1fffffff; }
// The 1P mission fence of the guest setters (minimap, cursor): mission mode, no
// split screen, player byte 0x800A5B0E clear, no cursor lock 0x800F0C20
// (advisor/pause dialogs), build menu 0x800C6A64 or camera redraw 0x800FD038
// (research.md). `lane_xor` is 3 for live word-swapped RAM, 0 for snapshots.
inline bool one_player_mission(std::span<const std::uint8_t> ram, unsigned lane_xor) {
    if (ram.size() != 0x800000) return false;
    const auto byte = [&](std::uint32_t a) { return unsigned(ram[a ^ lane_xor]); };
    const auto half = [&](std::uint32_t a) { return (byte(a) << 8) | byte(a + 1); };
    return half(physical(game_mode)) == 3 && !byte(physical(split_screen)) && !byte(0xa5b0e)
        && !half(0xf0c20) && !byte(0xc6a64) && !byte(0xfd038);
}
}
