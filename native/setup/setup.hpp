#pragma once
// ROM -> game module (ADR-087). Steps: check the ROM, find a compiler, generate the game C from the
// recipe with the prebuilt N64Recomp, compile it into sc64-game.<ext> and publish it with a receipt.
#include "process.hpp"
#include "recipe.hpp"
#include "toolchain.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace sc64::setup {
inline constexpr int kSteps = 5;

struct Options {
    std::filesystem::path rom;
    std::optional<std::string> rom_image;  // the ROM bytes (any byte order) instead of reading `rom`
    std::filesystem::path data_dir;     // module goes to <data>/game (<data>/game-<target> when cross-building),
                                        // logs to <data>/setup, zig to <data>/tools
    std::filesystem::path resources;    // recipe/ntsc-u and module/ (staged next to the executable)
    std::filesystem::path recompiler;   // the prebuilt, patched N64Recomp
    std::optional<std::filesystem::path> compiler;  // use this compiler instead of searching
    std::string cc_env;                 // $CC
    bool force = false;
    bool keep_work = false;
    bool allow_download = true;
    bool use_zig = false;               // skip system compilers and use the pinned zig
    // Cross-build the module for another platform (parse_target) instead of this host. Android needs the NDK
    // (`ndk` is its root, see ndk_candidates); the module is then built for the device, never loaded here.
    std::optional<Platform> target;
    std::optional<std::filesystem::path> ndk;
    unsigned jobs = 4;
    std::string rom_sha1{kRomSha1};     // the accepted ROM (changed only by tests)
};

struct Reporter {
    std::function<void(int step, int total, const std::string& text)> step;
    std::function<void(const std::string& text)> info;
};

struct Result {
    std::filesystem::path module;
    bool rebuilt = false;
    std::string compiler;
    std::filesystem::path logs;
};

// <data>/game/sc64-game<suffix>; a cross-built module has its own folder and receipt, <data>/game-<target>.
std::filesystem::path game_directory(const std::filesystem::path& data_dir, const Platform& platform, bool cross = false);
std::filesystem::path module_path(const std::filesystem::path& data_dir, const Platform& platform, bool cross = false);
// True when <data>/game holds a module whose receipt matches the ROM, recipe, recompiler, module sources
// and flags of `options` (the same test run_setup uses to do nothing). Never throws.
bool module_is_current(const Options& options);
// Throws SetupError with an actionable message. `run` runs every child process.
Result run_setup(const Options& options, const Runner& run, const Reporter& report);
}
