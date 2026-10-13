#include "zig.hpp"
#include "recipe.hpp"
#include <array>
#include <system_error>

namespace sc64::setup {
namespace {
// Release 0.16.0 (2026-04-13) from https://ziglang.org/download/index.json; the digests are the published ones.
constexpr std::string_view kVersion = "0.16.0";
constexpr std::array<ZigPin, 3> kPins{{
    ZigPin{"macos", "arm64", "https://ziglang.org/download/0.16.0/zig-aarch64-macos-0.16.0.tar.xz",
           "b23d70deaa879b5c2d486ed3316f7eaa53e84acf6fc9cc747de152450d401489",
           "zig-aarch64-macos-0.16.0"},
    ZigPin{"linux", "x86_64", "https://ziglang.org/download/0.16.0/zig-x86_64-linux-0.16.0.tar.xz",
           "70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00",
           "zig-x86_64-linux-0.16.0"},
    ZigPin{"windows", "x86_64", "https://ziglang.org/download/0.16.0/zig-x86_64-windows-0.16.0.zip",
           "68659eb5f1e4eb1437a722f1dd889c5a322c9954607f5edcf337bc3684a75a7e",
           "zig-x86_64-windows-0.16.0"},
}};
}

std::string_view zig_version() { return kVersion; }

std::optional<ZigPin> zig_pin(const HostInfo& host) {
    for (const auto& pin : kPins)
        if (pin.os == host.os && pin.arch == host.arch) return pin;
    return std::nullopt;
}

std::filesystem::path ensure_zig(const Runner& run, const HostInfo& host, const std::filesystem::path& tools_dir,
                                 const std::filesystem::path& log, const std::function<void(const std::string&)>& info) {
    const auto install = tools_dir / ("zig-" + std::string(kVersion));
    const auto binary = install / ("zig" + host.exe_suffix);
    std::error_code error;
    if (std::filesystem::is_regular_file(binary, error)) return binary;
    const auto pin = zig_pin(host);
    if (!pin) throw SetupError("No C compiler found, and zig " + std::string(kVersion) + " has no pinned download for " +
                               host.os + "-" + host.arch + ". Install gcc or clang and run the setup again.");
    const std::string hint = "\nInstall a C compiler (gcc or clang), or download " + std::string(pin->url) + " yourself, "
                             "unpack it so that " + path_utf8(binary) + " exists, and run the setup again.";
    std::filesystem::create_directories(tools_dir);
    const auto archive = tools_dir / (std::string(pin->url).substr(std::string(pin->url).rfind('/') + 1) + ".part");
    info("Downloading zig " + std::string(kVersion) + " (one time)");
    try {
        if (run({"curl", {"--fail", "--silent", "--show-error", "--location", "--retry", "3",
                          // A blocked or stalled network must end in an error, not hang the first run.
                          "--connect-timeout", "30", "--speed-limit", "1024", "--speed-time", "60", "--output", path_utf8(archive),
                          std::string(pin->url)}, log}) != 0)
            throw SetupError("Download of zig failed (see " + path_utf8(log) + ")" + hint);
    } catch (const SetupError&) {
        std::filesystem::remove(archive, error);
        throw;
    } catch (const std::exception& failure) {
        throw SetupError(std::string("Cannot run curl: ") + failure.what() + hint);
    }
    if (file_sha256(archive) != pin->sha256) {
        std::filesystem::remove(archive, error);
        throw SetupError("The downloaded zig archive does not match its pinned SHA-256; it was discarded." + hint);
    }
    info("Unpacking zig");
    const auto staging = tools_dir / (".unpack-" + std::string(kVersion));
    std::filesystem::remove_all(staging, error);
    std::filesystem::create_directories(staging);
    try {
        if (run({"tar", {"-xf", path_utf8(archive), "-C", path_utf8(staging)}, log}) != 0)
            throw SetupError("Unpacking zig failed (see " + path_utf8(log) + ")" + hint);
    } catch (const SetupError&) {
        std::filesystem::remove_all(staging, error);
        throw;
    } catch (const std::exception& failure) {
        std::filesystem::remove_all(staging, error);
        throw SetupError(std::string("Cannot run tar: ") + failure.what() + hint);
    }
    std::filesystem::remove_all(install, error);
    std::filesystem::rename(staging / std::string(pin->directory), install);
    std::filesystem::remove_all(staging, error);
    std::filesystem::remove(archive, error);
    if (!std::filesystem::is_regular_file(binary, error))
        throw SetupError("The unpacked zig archive has no " + path_utf8(binary.filename()) + hint);
    return binary;
}
}
