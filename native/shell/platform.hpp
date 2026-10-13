#pragma once
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

// Platform services used by the shell main thread. Implementations:
// platform_sdl.cpp (all platforms) and platform_android.cpp (Android JNI/assets).
enum class Sc64Pick { rom, game_module };
struct Sc64Selection {
    std::string path;
    std::string error;
    Sc64Pick kind = Sc64Pick::rom;
};
// Starts an asynchronous choice of a ROM dump or a game module. The selected file is copied into an owned
// `sc64-import-*.tmp` file in storage; its result arrives through
// sc64_poll_selection(). Returns false when a choice is already in progress.
bool sc64_choose_file(Sc64Pick kind);
// The kind of choice in progress (the picker is open or its file is being copied), if any.
std::optional<Sc64Pick> sc64_pending_pick();
std::string sc64_storage_path();
void sc64_platform_initialize(const std::string& storage);
void sc64_platform_finalize();
Sc64Selection sc64_poll_selection();
// Reads a packaged 32 MiB ROM (an Android asset or a file beside the executable)
// into owned memory; throws when it is missing or not exactly that size.
std::vector<std::uint8_t> sc64_read_packaged_rom(const std::string& path);
std::vector<std::uint8_t> sc64_read_bundled_rom();
// Vibrate for milliseconds at amplitude 1..255; milliseconds <= 0 stops.
// Hosts without a vibrator ignore it.
void sc64_platform_vibrate(int milliseconds, int amplitude);
