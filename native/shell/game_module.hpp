#pragma once
// The recompiled game as the shell sees it (ADR-086): entry point, overlay section tables and
// the minimap setter. Desktop builds and the Android launcher load it from a module built out of the
// generated C; the personal Android APK links it statically (game_static.cpp) and load() only reports it.
#include "sc64_module.h"
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace sc64::desktop::game {
class ModuleError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// File name of the module on this platform, for example sc64-game.dylib.
std::string module_file_name();
// Where load() looks, in order: $SC64_GAME_MODULE, beside the application's resources,
// then <storage>/game. `override_path` and `base` may be empty.
std::vector<std::filesystem::path> candidates(const std::filesystem::path& storage,
                                              const std::filesystem::path& base, std::string_view override_path);
// Rejects a module whose ABI, struct layout or ROM does not match this host (pure; no loading).
void validate(const Sc64ModuleInfo& info, std::string_view expected_rom_sha1);
// The host side of the ABI: the real function behind every entry of sc64_imports.def.
Sc64HostApi host_api();
// Android launcher staging. Pure parts, so that they can be tested on any host:
// the folder a module is imported from, or empty below Android 11 (where other apps could write it) and
// without external storage.
std::filesystem::path staging_directory(int sdk_version, const char* external_files_dir);
// The "module not found" text: the places searched and what to do, which depends on the platform.
std::string missing_message(const std::vector<std::filesystem::path>& places, const std::filesystem::path& staged,
                            bool refused_by_version, std::string_view import_error);
// Makes the staging folder exist (the app creates it, so that it is readable by the app and `adb push` can fill it).
void prepare_staging();
// Android launcher: a module copied (adb push) into the app's external files directory cannot be loaded from
// there (the linker refuses it, ADR-089), so it is moved into <storage>/game first. Returns true when
// <staged>/<module file> replaced the module in <storage>/game; an identical staged copy is just removed.
// Throws ModuleError when the copy fails. A missing file is not an error.
bool install_staged(const std::filesystem::path& staged, const std::filesystem::path& storage);
// Loads and validates the module; later calls return the same one. Throws ModuleError.
const Sc64ModuleInfo& load(const std::filesystem::path& storage);
// Loads one specific module file (used by load() and by tests). Throws ModuleError.
const Sc64ModuleInfo& load_file(const std::filesystem::path& file, std::string_view expected_rom_sha1);
// The loaded game; throws ModuleError before load().
const Sc64ModuleInfo& info();
}
