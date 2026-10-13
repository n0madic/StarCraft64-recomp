#include "recipe.hpp"
#include "read_bytes.hpp"
#include "process.hpp"
#include "sha.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <json/json.hpp>
#include <sstream>

namespace sc64::setup {
namespace {
bool is_hex(std::string_view text, std::size_t length) {
    return text.size() == length && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) || (c >= 'a' && c <= 'f');
    });
}
bool is_file_name(std::string_view name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_' || c == '.';
    });
}
std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return text;
}
}

std::string read_file(const std::filesystem::path& path) {
    auto bytes = sc64::runtime::read_bytes(path);
    if (!bytes) throw SetupError("Cannot read " + path_utf8(path));
    return std::move(*bytes);
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(bytes.data(), std::streamsize(bytes.size()));
    stream.close();
    if (!stream) throw SetupError("Cannot write " + path_utf8(path));
}

void copy_bytes(const std::filesystem::path& from, const std::filesystem::path& to) {
    write_file(to, read_file(from));
}

std::string file_sha256(const std::filesystem::path& path) {
    const std::string bytes = read_file(path);
    return sc64::runtime::sha256_hex({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

Manifest parse_manifest(std::string_view json, std::string_view expected_rom_sha1) {
    try {
        const auto document = nlohmann::json::parse(json);
        if (document.at("schema").get<int>() != 1) throw SetupError("generated.json: unsupported schema");
        Manifest manifest;
        manifest.rom_sha1 = document.at("rom_sha1").get<std::string>();
        manifest.symbols_sha256 = document.at("symbols_sha256").get<std::string>();
        manifest.config_sha256 = document.at("config_sha256").get<std::string>();
        for (const auto& [name, digest] : document.at("files").items()) manifest.files[name] = digest.get<std::string>();
        if (manifest.rom_sha1 != expected_rom_sha1) throw SetupError("generated.json: recipe is for a different ROM");
        if (!is_hex(manifest.symbols_sha256, 64) || !is_hex(manifest.config_sha256, 64) || manifest.files.empty())
            throw SetupError("generated.json: malformed hashes");
        for (const auto& [name, digest] : manifest.files)
            if (!is_file_name(name) || !is_hex(digest, 64)) throw SetupError("generated.json: bad entry " + name);
        return manifest;
    } catch (const nlohmann::json::exception& error) {
        throw SetupError(std::string("generated.json: ") + error.what());
    }
}

std::string toml_string(const std::string& utf8) {
    std::string result = "\"";
    for (unsigned char c : utf8) {
        switch (c) {
        case '"': result += "\\\""; break;
        case '\\': result += "\\\\"; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char escape[8];
                std::snprintf(escape, sizeof escape, "\\u%04X", c);
                result += escape;
            } else {
                result += char(c);
            }
        }
    }
    return result + '"';
}

std::string render_config(std::string_view config_template, const std::filesystem::path& rom,
                          const std::filesystem::path& output) {
    std::string text(config_template);
    const auto replace_once = [&](std::string_view placeholder, const std::string& value) {
        const std::string quoted = "\"" + std::string(placeholder) + "\"";
        const auto at = text.find(quoted);
        if (at == std::string::npos || text.find(quoted, at + 1) != std::string::npos)
            throw SetupError("Config template must contain " + quoted + " exactly once");
        text.replace(at, quoted.size(), toml_string(value));
    };
    replace_once("@ROM@", path_utf8(rom));
    replace_once("@OUTPUT@", path_utf8(output));
    return text;
}

Manifest load_recipe(const std::filesystem::path& directory, std::string_view expected_rom_sha1) {
    const Manifest manifest = parse_manifest(read_file(directory / std::string(kManifestFile)), expected_rom_sha1);
    if (file_sha256(directory / std::string(kSymbolsFile)) != manifest.symbols_sha256 ||
        file_sha256(directory / std::string(kConfigFile)) != manifest.config_sha256)
        throw SetupError("The recipe in " + path_utf8(directory) + " does not match its manifest; reinstall the application");
    return manifest;
}

std::map<std::string, std::string> hash_directory(const std::filesystem::path& directory) {
    std::map<std::string, std::string> result;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_regular_file()) result[path_utf8(entry.path().filename())] = file_sha256(entry.path());
    return result;
}

std::vector<std::string> compare_hashes(const std::map<std::string, std::string>& expected,
                                        const std::map<std::string, std::string>& actual) {
    std::vector<std::string> problems;
    for (const auto& [name, digest] : expected)
        if (!actual.contains(name)) problems.push_back("missing " + name);
    for (const auto& [name, digest] : actual)
        if (!expected.contains(name)) problems.push_back("unexpected " + name);
    for (const auto& [name, digest] : expected) {
        const auto found = actual.find(name);
        if (found != actual.end() && found->second != digest) problems.push_back("hash differs: " + name);
    }
    return problems;
}

void inspect_output(const std::filesystem::path& directory, std::string_view log) {
    std::istringstream lines{std::string(log)};
    std::string line;
    while (std::getline(lines, line)) {
        const std::string folded = lower(line);
        for (const char* marker : {"unhandled ", "error ", "failed ", "no function found"})
            if (folded.find(marker) != std::string::npos)
                throw SetupError("Recompiler diagnostic rejected: " + line);
    }
    std::size_t functions = 0;
    bool entrypoint = false;
    for (const char* required : {"funcs.h", "recomp_overlays.inl", "lookup.cpp"}) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(directory / required, error) || std::filesystem::file_size(directory / required) == 0)
            throw SetupError(std::string("Recompiler output is incomplete: ") + required);
    }
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        const std::string name = path_utf8(entry.path().filename());
        if (name.starts_with("funcs_") && name.ends_with(".c")) {
            ++functions;
            if (entry.file_size() == 0) throw SetupError("Recompiler output is incomplete: " + name);
            if (!entrypoint) entrypoint = read_file(entry.path()).find("RECOMP_FUNC void recomp_entrypoint(") != std::string::npos;
        }
    }
    if (!functions) throw SetupError("Recompiler output is incomplete: no funcs_*.c");
    if (!entrypoint) throw SetupError("Entrypoint was not emitted");
    if (read_file(directory / "recomp_overlays.inl").find("static SectionTableEntry section_table[]") == std::string::npos)
        throw SetupError("Section registration table was not emitted");
}
}
