// SDL platform services shared by every shell target: asynchronous ROM import,
// system cursor hiding and, outside Android, storage and stub peripherals.
#include "platform.hpp"
#include "platform_sdl.hpp"
#include "mouse_cursor.hpp"
#include "rom_import.hpp"
#include "log.hpp"
#include "../runtime/rom_image.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {
SDL_Window* window = nullptr;
bool cursor_hidden = false;
std::mutex import_mutex;
sc64::desktop::RomImportState imports;
std::filesystem::path storage_root;
std::thread import_worker;
std::atomic<unsigned> import_counter{0};

// Copy a chosen path or Android content URI into an owned temporary file. The
// session reads it immediately and persists its own atomic copy, so no fsync.
Sc64RomSelection copy_rom(const std::string& source, const std::filesystem::path& root) {
    Sc64RomSelection result;
    const auto name = "sc64-import-" + std::to_string(SDL_GetTicksNS()) + "-"
        + std::to_string(import_counter.fetch_add(1)) + ".tmp";
    const auto target = root / name;
    SDL_IOStream* input = SDL_IOFromFile(source.c_str(), "rb");
    if (!input) { result.error = std::string("Cannot open the selected ROM: ") + SDL_GetError(); return result; }
    SDL_IOStream* output = SDL_IOFromFile(target.string().c_str(), "wb");
    if (!output) {
        SDL_CloseIO(input);
        result.error = std::string("Cannot create the ROM import file: ") + SDL_GetError();
        return result;
    }
    std::vector<std::uint8_t> buffer(1 << 16);
    std::size_t total = 0;
    bool ok = true;
    for (;;) {
        const std::size_t read = SDL_ReadIO(input, buffer.data(), buffer.size());
        if (read == 0) { ok = SDL_GetIOStatus(input) == SDL_IO_STATUS_EOF; break; }
        total += read;
        if (total > sc64::runtime::rom_size || SDL_WriteIO(output, buffer.data(), read) != read) { ok = false; break; }
    }
    SDL_CloseIO(input);
    ok = SDL_CloseIO(output) && ok;
    if (!ok || total != sc64::runtime::rom_size) {
        std::error_code ignored;
        std::filesystem::remove(target, ignored);
        result.error = total > sc64::runtime::rom_size || (ok && total != sc64::runtime::rom_size)
            ? "Expected a 32 MiB ROM dump" : "ROM import copy failed";
        return result;
    }
    result.path = target.string();
    return result;
}

void SDLCALL chosen(void*, const char* const* files, int) {
    if (!files || !files[0]) {
        std::lock_guard lock(import_mutex);
        if (!files) imports.complete({{}, std::string("ROM selection failed: ") + SDL_GetError()});
        else imports.cancel(); // dismissed without a selection
        return;
    }
    const std::string source = files[0];
    std::thread finished;
    std::unique_lock lock(import_mutex);
    // begin() admits one choice at a time, so a previous worker has completed;
    // join it without holding the mutex it released on completion.
    finished = std::move(import_worker);
    lock.unlock();
    if (finished.joinable()) finished.join();
    lock.lock();
    import_worker = std::thread([source, root = storage_root] {
        auto result = copy_rom(source, root);
        std::lock_guard lock(import_mutex);
        if (const auto error = imports.complete(std::move(result)))
            sc64::shell::log::warning("ROM import cleanup: %s", error.message().c_str());
    });
}
}

void sc64_platform_attach_window(SDL_Window* value) { window = value; }

bool sc64_choose_rom() {
    {
        std::lock_guard lock(import_mutex);
        if (!imports.begin()) return false;
    }
    SDL_ShowOpenFileDialog(chosen, nullptr, window, nullptr, 0, nullptr, false);
    return true;
}

Sc64RomSelection sc64_poll_rom() {
    std::lock_guard lock(import_mutex);
    return imports.take();
}

void sc64_platform_initialize(const std::string& storage) {
    std::filesystem::create_directories(storage);
    std::lock_guard lock(import_mutex);
    storage_root = std::filesystem::weakly_canonical(storage);
    // Sweep imports orphaned by an earlier process that ended mid-copy.
    if (const auto error = imports.initialize(storage_root))
        sc64::shell::log::warning("ROM import cleanup: %s", error.message().c_str());
}

void sc64_platform_finalize() {
    std::thread worker;
    {
        std::lock_guard lock(import_mutex);
        worker = std::move(import_worker);
    }
    if (worker.joinable()) worker.join();
    std::lock_guard lock(import_mutex);
    if (const auto error = imports.finalize())
        sc64::shell::log::warning("ROM import cleanup: %s", error.message().c_str());
}

void sc64_restore_mouse_cursor() {
    if (cursor_hidden) { SDL_ShowCursor(); cursor_hidden = false; }
}

bool sc64_update_mouse_cursor(bool requested, const Sc64CursorRegion& region,
                              const Sc64CursorRegion* visible_region, const Sc64CursorRegion* minimap_region) {
    bool hide = false;
    if (requested && window && SDL_GetMouseFocus() == window) {
        const auto flags = SDL_GetWindowFlags(window);
        int width = 0, height = 0;
        SDL_GetWindowSize(window, &width, &height);
        if ((flags & SDL_WINDOW_INPUT_FOCUS) && !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN))
            && width > 0 && height > 0) {
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            const double x = mx / width, y = 1 - my / height;
            hide = region.contains(x, y) && !(visible_region && visible_region->contains(x, y))
                && !(minimap_region && minimap_region->contains(x, y));
        }
    }
    // Hide only; the pointer is never captured or warped.
    if (hide && !cursor_hidden) { SDL_HideCursor(); cursor_hidden = true; }
    if (!hide) sc64_restore_mouse_cursor();
    return cursor_hidden;
}

std::vector<std::uint8_t> sc64_read_packaged_rom(const std::string& path) {
    SDL_IOStream* stream = SDL_IOFromFile(path.c_str(), "rb");
    if (!stream) throw std::runtime_error("Packaged ROM unavailable (" + path + "): " + SDL_GetError());
    std::vector<std::uint8_t> rom(sc64::runtime::rom_size);
    std::size_t total = 0;
    while (total < rom.size()) {
        const std::size_t read = SDL_ReadIO(stream, rom.data() + total, std::min<std::size_t>(rom.size() - total, 1 << 16));
        if (read == 0) break;
        total += read;
    }
    std::uint8_t extra = 0;
    const bool longer = SDL_ReadIO(stream, &extra, 1) == 1;
    SDL_CloseIO(stream);
    if (total != rom.size() || longer) throw std::runtime_error("Packaged ROM is not a 32 MiB dump: " + path);
    return rom;
}

#if !defined(__ANDROID__)
std::string sc64_storage_path() {
    char* path = SDL_GetPrefPath("", "StarCraft64");
    if (!path) throw std::runtime_error(std::string("Storage directory unavailable: ") + SDL_GetError());
    std::string result = path;
    SDL_free(path);
    while (result.size() > 1 && (result.back() == '/' || result.back() == '\\')) result.pop_back();
    return result;
}
// Desktop profiles have no vibrator; gamepad rumble is not mapped.
void sc64_platform_vibrate(int, int) {}
// A packaged build (AppImage) places the verified ROM beside the executable.
std::vector<std::uint8_t> sc64_read_bundled_rom() {
    const char* base = SDL_GetBasePath();
    return sc64_read_packaged_rom(std::string(base ? base : "") + "sc64-ntsc-u.z64");
}
#endif
