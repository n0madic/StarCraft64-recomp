#pragma once
// Pinned zig as the fallback C compiler (ADR-087). It is downloaded once with the system curl,
// verified against a pinned SHA-256 and unpacked with the system tar; nothing is installed system-wide.
#include "process.hpp"
#include "toolchain.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

namespace sc64::setup {
struct ZigPin {
    std::string_view os;
    std::string_view arch;
    std::string_view url;
    std::string_view sha256;
    std::string_view directory;  // top-level directory inside the archive
};

std::string_view zig_version();
std::optional<ZigPin> zig_pin(const Platform& host);
// The compiler under tools_dir, downloading it first when absent. `log` receives curl/tar output;
// `info` gets one line per phase. Throws SetupError with a manual-install hint on failure.
std::filesystem::path ensure_zig(const Runner& run, const Platform& host, const std::filesystem::path& tools_dir,
                                 const std::filesystem::path& log, const std::function<void(const std::string&)>& info);
}
