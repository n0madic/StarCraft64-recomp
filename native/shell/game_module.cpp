#include "game_module.hpp"
#include <SDL3/SDL_loadso.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_error.h>
#if defined(__ANDROID__)
#include <SDL3/SDL_system.h>
#endif
#include <algorithm>
#include <fstream>
#include <system_error>
#include <cstdlib>
#include <mutex>
#include "log.hpp"
#include "read_bytes.hpp"
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
    // SDL_GetBasePath is "./" on Android: a cwd-relative path is not a place to load code from.
    if (!base.empty() && base.is_absolute()) result.push_back(base / module_file_name());
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

namespace {
// The module's description, once `object` is loaded: initialized with the host API and validated.
const Sc64ModuleInfo& bind(SDL_SharedObject* object, const std::filesystem::path& file, std::string_view expected_rom_sha1) {
    const auto init = reinterpret_cast<Sc64ModuleInitFn>(SDL_LoadFunction(object, SC64_MODULE_INIT));
    if (!init) throw ModuleError(utf8(file) + " is not a game module (" SC64_MODULE_INIT " not found)");
    static const Sc64HostApi api = host_api();
    const Sc64ModuleInfo* info = init(&api);
    if (!info) throw ModuleError(utf8(file) + " refused this application (ABI mismatch); rebuild the game module");
    validate(*info, expected_rom_sha1);
    return *info;
}
}

const Sc64ModuleInfo& load_file(const std::filesystem::path& file, std::string_view expected_rom_sha1) {
    // The handle of a module that is accepted is deliberately never released: guest threads may still run its code at exit.
    SDL_SharedObject* object = SDL_LoadObject(utf8(file).c_str());
    if (!object) throw ModuleError("Cannot load game module " + utf8(file) + ": " + SDL_GetError());
    try {
        return bind(object, file, expected_rom_sha1);
    } catch (...) {
        // A refused module must not stay mapped: loading the path again (a corrected module installed over it)
        // would otherwise return this image, as the loader matches already loaded objects by name.
        SDL_UnloadObject(object);
        throw;
    }
}

namespace {
// Android 10 and older: shared storage is writable by other apps, so nothing is imported from it.
constexpr int kFirstPrivateExternalFolder = 30;

#if defined(__ANDROID__)
bool refused_by_version() { return SDL_GetAndroidSDKVersion() < kFirstPrivateExternalFolder; }
#else
bool refused_by_version() { return false; }
#endif

// The folder of the app's own files on shared storage, which `adb push` reaches.
std::filesystem::path staging_dir() {
#if defined(__ANDROID__)
    return staging_directory(SDL_GetAndroidSDKVersion(), SDL_GetAndroidExternalStoragePath());
#else
    return {};
#endif
}
}

std::filesystem::path staging_directory(int sdk_version, const char* external_files_dir) {
    // Code is only taken from there where no other app can write it: on Android 10 and older any app with the
    // storage permission could place a module in an app's folder on shared storage (Android 11 closed that).
    if (sdk_version < kFirstPrivateExternalFolder || !external_files_dir || !*external_files_dir) return {};
    // The folder itself, not a subfolder: one made by `adb push` belongs to the shell user and the app may not read it.
    return external_files_dir;
}

void prepare_staging() {
    // SDL_GetAndroidExternalStoragePath makes the app create its folder on shared storage.
    (void)staging_dir();
}

std::string missing_message(const std::vector<std::filesystem::path>& places, const std::filesystem::path& staged,
                            bool refused_by_version, std::string_view import_error) {
    std::string message = "The game module " + module_file_name() + " was not found.";
    if (!import_error.empty()) message += "\n" + std::string(import_error);
    // The Android text applies where a staging folder exists or the OS is too old for one (the arguments say so).
    if (!staged.empty() || refused_by_version) {
        // A release user has no repository and no make: the steps use only the desktop launcher package and this screen.
        message += "\nThe phone cannot build it. On a computer with the Android NDK run once"
                   "\n(sc64_setup is in the desktop launcher package):"
                   "\n  sc64_setup --rom your.z64 --target android-arm64"
                   "\nCopy the " + module_file_name() + " it names to this phone, press Select game module,"
                   "\npick that file, then press Select ROM.";
        if (!staged.empty()) message += "\nOr adb push it to " + utf8(staged) + " and start the application again.";
        else message += "\nCopying it to shared storage is not used on Android 10 or older.";
    } else {
        message += "\nBuild it from your ROM (docs/desktop.md) and place it beside the application or in the game folder below.";
    }
    message += "\nLooked in:";
    for (const auto& place : places) message += "\n  " + utf8(place);
    return message;
}

namespace {
// Loaded code is kept read-only (Android 14 refuses writable dynamically loaded files for dex; the same rule is cheap here).
void make_read_only(const std::filesystem::path& file) {
    std::error_code error;
    std::filesystem::permissions(file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec, error);
    if (error) shell::log::warning("The imported game module stays writable: %s", error.message().c_str());
}

// Removes a file that may be read-only (Windows refuses to delete one).
void discard(const std::filesystem::path& file) {
    std::error_code ignored;
    std::filesystem::permissions(file, std::filesystem::perms::owner_all, ignored);
    std::filesystem::remove(file, ignored);
}

// rename replaces the old module in one step: a failure leaves it in place (and removes `temporary`).
void replace_module(const std::filesystem::path& temporary, const std::filesystem::path& target) {
    namespace fs = std::filesystem;
    std::error_code error;
    fs::rename(temporary, target, error);
#if defined(_WIN32)
    if (error) {  // Windows cannot rename over an existing file
        discard(target);
        fs::rename(temporary, target, error);
    }
#endif
    if (error) { discard(temporary); throw ModuleError("Cannot install the game module as " + utf8(target)); }
}

std::filesystem::path import_name() { return ".import-" + module_file_name(); }

// SDL reports "./" as the base path on Android: not an application folder.
std::filesystem::path application_folder() {
    const char* base = SDL_GetBasePath();
    std::filesystem::path folder = base ? base : "";
    return folder.is_absolute() ? folder : std::filesystem::path();
}
std::string override_module() {
    const char* value = std::getenv("SC64_GAME_MODULE");
    return value ? value : "";
}
// Where load() looks for a module file.
std::vector<std::filesystem::path> search_places(const std::filesystem::path& storage) {
    return candidates(storage, application_folder(), override_module());
}

// Whether the file mentions the module's entry point: a cheap check that the picked file is meant to be a
// game module at all, before any of its code is run (it keeps an unrelated library from being loaded).
bool names_module_entry(const std::filesystem::path& file) {
    constexpr std::string_view marker = SC64_MODULE_INIT;
    std::ifstream in(file, std::ios::binary);
    std::string window;
    std::vector<char> chunk(1 << 16);
    while (in.read(chunk.data(), std::streamsize(chunk.size())) || in.gcount() > 0) {
        window.append(chunk.data(), std::size_t(in.gcount()));
        if (window.find(marker) != std::string::npos) return true;
        // Keep the tail, so that a name that straddles two chunks is found.
        if (window.size() >= marker.size()) window.erase(0, window.size() - (marker.size() - 1));
    }
    return false;
}
}

bool install_staged(const std::filesystem::path& staged, const std::filesystem::path& storage) {
    namespace fs = std::filesystem;
    const fs::path source = staged / module_file_name(), target = storage / "game" / module_file_name();
    std::error_code error;
    if (staged.empty()) return false;
    const auto status = fs::status(source, error);
    if (status.type() == fs::file_type::not_found) return false;  // some libc++ builds also set `error` for this
    if (error) throw ModuleError("Cannot access the staged game module " + utf8(source) + ": " + error.message());
    if (!fs::is_regular_file(status)) return false;
    const auto forget_source = [&] { std::error_code ignored; fs::remove(source, ignored); };
    const auto bytes = runtime::read_bytes(source);
    if (!bytes || bytes->empty()) throw ModuleError("The staged game module " + utf8(source) + " is empty or unreadable");
    if (fs::is_regular_file(target, error)) {
        const auto current = runtime::read_bytes(target);
        if (current && *current == *bytes) { forget_source(); return false; }
    }
    fs::create_directories(target.parent_path(), error);
    const fs::path temporary = target.parent_path() / import_name();
    discard(temporary);
    {
        std::ofstream out(temporary, std::ios::binary);
        out.write(bytes->data(), std::streamsize(bytes->size()));
        out.flush();
        if (!out) { discard(temporary); throw ModuleError("Cannot copy the game module to " + utf8(target)); }
    }
    make_read_only(temporary);
    replace_module(temporary, target);
    forget_source();
    return true;
}

bool importable() { return true; }

bool bundled() {
    // A module named by the environment, or one beside the application, is the application's own.
    if (!override_module().empty()) return true;
    std::error_code error;
    const auto folder = application_folder();
    return !folder.empty() && std::filesystem::is_regular_file(folder / module_file_name(), error);
}

bool installed(const std::filesystem::path& storage) {
    std::error_code error;
    for (const auto& place : search_places(storage))
        if (std::filesystem::is_regular_file(place, error)) return true;
    return false;
}

void import_module(const std::filesystem::path& source, const std::filesystem::path& storage) {
    namespace fs = std::filesystem;
    std::lock_guard lock(mutex);
    if (loaded) throw ModuleError("A game module is already in use; restart the application to use a different one");
    const fs::path directory = storage / "game", temporary = directory / import_name();
    std::error_code error;
    fs::create_directories(directory, error);
    discard(temporary);
    fs::rename(source, temporary, error);
    if (error) {  // another volume
        fs::copy_file(source, temporary, fs::copy_options::overwrite_existing, error);
        if (error) throw ModuleError("Cannot copy the game module to " + utf8(temporary) + ": " + error.message());
        fs::remove(source, error);
    }
    if (!names_module_entry(temporary)) {
        discard(temporary);
        throw ModuleError("The selected file is not a game module (" SC64_MODULE_INIT " not found in it)");
    }
    // The dynamic loader only accepts code from the application's own storage, so the check happens on the copy.
    // The handle is released again: this module is not the one in use, and the installed one is loaded by load().
    // The copy stays writable until it is accepted, so that a refused one can always be deleted.
    SDL_SharedObject* object = SDL_LoadObject(utf8(temporary).c_str());
    try {
        if (!object) throw ModuleError("The selected file is not a game module for this system: " + std::string(SDL_GetError()));
        bind(object, "The selected file", runtime::rom_sha1);
    } catch (...) {
        if (object) SDL_UnloadObject(object);
        discard(temporary);
        throw;
    }
    SDL_UnloadObject(object);
    make_read_only(temporary);
    replace_module(temporary, directory / module_file_name());
}

const Sc64ModuleInfo& load(const std::filesystem::path& storage) {
    std::lock_guard lock(mutex);
    if (loaded) return *loaded;
    const auto staged = staging_dir();
    std::string import_error;
    if (!staged.empty()) {
        try {
            install_staged(staged, storage);
        } catch (const ModuleError& error) {
            // An installed module still works when a newly staged one cannot be imported.
            import_error = error.what();
            shell::log::warning("%s", error.what());
        }
    }
    const auto places = search_places(storage);
    for (const auto& place : places) {
        std::error_code error;
        if (std::filesystem::is_regular_file(place, error)) return *(loaded = &load_file(place, runtime::rom_sha1));
    }
    throw ModuleError(missing_message(places, staged, refused_by_version(), import_error));
}

const Sc64ModuleInfo& info() {
    std::lock_guard lock(mutex);
    if (!loaded) throw ModuleError("Game module is not loaded");
    return *loaded;
}
}
