#include "locate.hpp"

namespace sc64::setup {
namespace fs = std::filesystem;

std::vector<fs::path> resource_candidates(const fs::path& base) {
    return {base / "setup-resources", base / ".." / "Resources" / "setup-resources"};
}

std::vector<fs::path> recompiler_candidates(const fs::path& base, const HostInfo& host) {
    const std::string name = "N64Recomp" + host.exe_suffix;
    return {base / name, base / ".." / "MacOS" / name};
}

fs::path first_directory(const std::vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (fs::is_directory(candidate, error)) return candidate.lexically_normal();
    }
    return {};
}

fs::path first_file(const std::vector<fs::path>& candidates) {
    for (const auto& candidate : candidates) {
        std::error_code error;
        if (fs::is_regular_file(candidate, error)) return candidate.lexically_normal();
    }
    return {};
}
}
