#pragma once
// The committed recompilation recipe on the user's machine (ADR-084, ADR-087): it is rendered into a
// config for the prebuilt N64Recomp and its output is accepted only with the recorded SHA-256 hashes.
#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sc64::setup {
class SetupError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Manifest {
    std::string rom_sha1;
    std::string symbols_sha256;
    std::string config_sha256;
    std::map<std::string, std::string> files;  // generated file name -> SHA-256
};

inline constexpr std::string_view kSymbolsFile = "sc64.syms.toml";
inline constexpr std::string_view kConfigFile = "sc64.toml";
inline constexpr std::string_view kManifestFile = "generated.json";

// The only supported ROM: NTSC-U, normalized to z64 byte order.
inline constexpr std::string_view kRomSha1 = "a36d91b5aac7bb95df0d7de5f9814f5270e25c2b";
// Strict parser for generated.json (schema 1) of the recipe for `expected_rom_sha1`. Throws SetupError.
Manifest parse_manifest(std::string_view json, std::string_view expected_rom_sha1 = kRomSha1);
// A TOML basic string for a UTF-8 path.
std::string toml_string(const std::string& utf8);
// Replaces the "@ROM@" and "@OUTPUT@" placeholders by TOML strings. Throws unless each occurs once.
std::string render_config(std::string_view config_template, const std::filesystem::path& rom,
                          const std::filesystem::path& output);
// Checks the recipe directory against its manifest and returns the manifest.
Manifest load_recipe(const std::filesystem::path& directory, std::string_view expected_rom_sha1 = kRomSha1);
// SHA-256 of every regular file of a directory, by file name.
std::map<std::string, std::string> hash_directory(const std::filesystem::path& directory);
// "missing X", "unexpected X", "hash differs: X"; empty when equal.
std::vector<std::string> compare_hashes(const std::map<std::string, std::string>& expected,
                                        const std::map<std::string, std::string>& actual);
// Rejects recompiler diagnostics (the CLI can print a failure and still exit 0) and incomplete output.
void inspect_output(const std::filesystem::path& directory, std::string_view log);
std::string read_file(const std::filesystem::path& path);
// Writes bytes to a file, replacing it. Throws SetupError.
void write_file(const std::filesystem::path& path, std::string_view bytes);
// Copies by reading and writing: std::filesystem::copy_file fails with EACCES on some emulated or
// network file systems (copy_file_range / sendfile), and these files are small.
void copy_bytes(const std::filesystem::path& from, const std::filesystem::path& to);
std::string file_sha256(const std::filesystem::path& path);
}
