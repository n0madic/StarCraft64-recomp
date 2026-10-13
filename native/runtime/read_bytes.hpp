#pragma once
// Whole-file read shared by the shell, the setup library and the tests.
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>

namespace sc64::runtime {
// The bytes of `path`, or nullopt when it cannot be opened or read.
inline std::optional<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) return std::nullopt;
    std::string bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (stream.bad()) return std::nullopt;
    return bytes;
}
}
