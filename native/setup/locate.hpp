#pragma once
// Where the setup resources and the prebuilt N64Recomp live relative to an executable (ADR-088).
// `base` is SDL_GetBasePath(): the executable's folder, or Contents/Resources inside a macOS app.
#include "toolchain.hpp"
#include <filesystem>
#include <vector>

namespace sc64::setup {
// setup-resources directories to try, in order.
std::vector<std::filesystem::path> resource_candidates(const std::filesystem::path& base);
// N64Recomp locations to try: beside the base folder, then Contents/MacOS next to Contents/Resources.
std::vector<std::filesystem::path> recompiler_candidates(const std::filesystem::path& base, const Platform& host);
// The first candidate that is a directory / regular file, or an empty path.
std::filesystem::path first_directory(const std::vector<std::filesystem::path>& candidates);
std::filesystem::path first_file(const std::vector<std::filesystem::path>& candidates);
}
