#include "game_module.hpp"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_error.h>
#include <cstdlib>
#include <mutex>
#include "rom_image.hpp"

// Real host functions behind the module's imports; they are defined by librecomp,
// ultramodern and the shell itself.
extern "C" {
#define SC64_GUEST_CALL(name) void name(uint8_t* rdram, recomp_context* ctx);
#define SC64_IMPORT_V(name, params, args) void name params;
#define SC64_IMPORT_R(ret, name, params, args) ret name params;
#include "sc64_imports.def"
#undef SC64_GUEST_CALL
#undef SC64_IMPORT_V
#undef SC64_IMPORT_R
}

namespace sc64::desktop::game {
namespace {
std::mutex mutex;
const Sc64ModuleInfo* loaded = nullptr;

std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}
}

std::string module_file_name() {
#if defined(_WIN32)
    return "sc64-game.dll";
#elif defined(__APPLE__)
    return "sc64-game.dylib";
#else
    return "sc64-game.so";
#endif
}

std::vector<std::filesystem::path> candidates(const std::filesystem::path& storage,
                                              const std::filesystem::path& base, std::string_view override_path) {
    std::vector<std::filesystem::path> result;
    if (!override_path.empty()) result.emplace_back(override_path);
    if (!base.empty()) result.push_back(base / module_file_name());
    if (!storage.empty()) result.push_back(storage / "game" / module_file_name());
    return result;
}

void validate(const Sc64ModuleInfo& info, std::string_view expected_rom_sha1) {
    if (info.abi != SC64_MODULE_ABI)
        throw ModuleError("Game module ABI " + std::to_string(info.abi) + " does not match this application (ABI " +
                          std::to_string(SC64_MODULE_ABI) + "); rebuild the game module");
    if (info.size != sizeof(Sc64ModuleInfo) || info.section_entry_size != sizeof(SectionTableEntry) ||
        info.func_entry_size != sizeof(FuncEntry) || info.context_size != sizeof(recomp_context))
        throw ModuleError("Game module data layout does not match this application; rebuild the game module");
    if (!info.entrypoint || !info.sections || !info.num_sections || !info.minimap_set_position || !info.rom_sha1)
        throw ModuleError("Game module is incomplete");
    if (expected_rom_sha1 != info.rom_sha1)
        throw ModuleError("Game module was generated from a different ROM than this application supports");
}

Sc64HostApi host_api() {
    Sc64HostApi api{};
    api.abi = SC64_MODULE_ABI;
    api.size = sizeof(Sc64HostApi);
#define SC64_GUEST_CALL(name) api.name = name;
#define SC64_IMPORT_V(name, params, args) api.name = name;
#define SC64_IMPORT_R(ret, name, params, args) api.name = name;
#include "sc64_imports.def"
#undef SC64_GUEST_CALL
#undef SC64_IMPORT_V
#undef SC64_IMPORT_R
    return api;
}

const Sc64ModuleInfo& load_file(const std::filesystem::path& file, std::string_view expected_rom_sha1) {
    // The handle is deliberately never released: guest threads may still run module code at exit.
    SDL_SharedObject* object = SDL_LoadObject(utf8(file).c_str());
    if (!object) throw ModuleError("Cannot load game module " + utf8(file) + ": " + SDL_GetError());
    const auto init = reinterpret_cast<Sc64ModuleInitFn>(SDL_LoadFunction(object, SC64_MODULE_INIT));
    if (!init) throw ModuleError(utf8(file) + " is not a game module (" SC64_MODULE_INIT " not found)");
    static const Sc64HostApi api = host_api();
    const Sc64ModuleInfo* info = init(&api);
    if (!info) throw ModuleError(utf8(file) + " refused this application (ABI mismatch); rebuild the game module");
    validate(*info, expected_rom_sha1);
    return *info;
}

const Sc64ModuleInfo& load(const std::filesystem::path& storage) {
    std::lock_guard lock(mutex);
    if (loaded) return *loaded;
    const char* base = SDL_GetBasePath();
    const char* override_path = std::getenv("SC64_GAME_MODULE");
    const auto places = candidates(storage, base ? base : "", override_path ? override_path : "");
    for (const auto& place : places) {
        std::error_code error;
        if (std::filesystem::is_regular_file(place, error)) return *(loaded = &load_file(place, runtime::rom_sha1));
    }
    std::string message = "The game module " + module_file_name() + " was not found. Looked in:";
    for (const auto& place : places) message += "\n  " + utf8(place);
    message += "\nBuild it from your ROM (docs/desktop.md) and place it beside the application or in the game folder above.";
    throw ModuleError(message);
}

const Sc64ModuleInfo& info() {
    std::lock_guard lock(mutex);
    if (!loaded) throw ModuleError("Game module is not loaded");
    return *loaded;
}
}
