// Loader for the game module (ADR-086), against synthetic modules built like the real one.
#include "game_module.hpp"
#include "module_import.hpp"
#include <SDL3/SDL_loadso.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#include "read_bytes.hpp"

namespace game = sc64::desktop::game;
namespace {
int failures = 0;
void check(bool condition, const char* what, int line) {
    if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, what); ++failures; }
}
#define CHECK(c) check((c), #c, __LINE__)
constexpr std::string_view rom = "a36d91b5aac7bb95df0d7de5f9814f5270e25c2b";

void expect_error(const std::filesystem::path& file, const char* fragment, int line) {
    try {
        game::load_file(file, rom);
        check(false, "load_file should have thrown", line);
    } catch (const game::ModuleError& error) {
        if (std::string(error.what()).find(fragment) == std::string::npos) {
            std::fprintf(stderr, "FAIL line %d: \"%s\" lacks \"%s\"\n", line, error.what(), fragment);
            ++failures;
        }
    }
}
struct Terminated {};
void throw_terminated() { throw Terminated{}; }
}

std::string slurp(const std::filesystem::path& file) { return sc64::runtime::read_bytes(file).value_or(std::string()); }
void spit(const std::filesystem::path& file, const std::string& text) {
    std::filesystem::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

// A module pushed to the Android app's external folder is moved into the storage folder before loading.
void test_install_staged() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("sc64-game-module-test-" + std::to_string(std::rand()));
    fs::remove_all(root);
    const fs::path staged = root / "staged", storage = root / "storage";
    const fs::path source = staged / game::module_file_name(), target = storage / "game" / game::module_file_name();
    CHECK(!game::install_staged(staged, storage) && !game::install_staged({}, storage));  // nothing staged
    spit(source, "module one");
    CHECK(game::install_staged(staged, storage));
    CHECK(!fs::exists(source) && slurp(target) == "module one");
#if !defined(_WIN32)
    CHECK((fs::status(target).permissions() & fs::perms::owner_write) == fs::perms::none);  // loaded code is read-only
#endif
    spit(source, "module one");  // the same bytes again: only the staged copy goes away
    CHECK(!game::install_staged(staged, storage) && !fs::exists(source) && slurp(target) == "module one");
    spit(source, "module two");  // a new one replaces the old read-only file
    CHECK(game::install_staged(staged, storage) && slurp(target) == "module two" && !fs::exists(source));
    spit(source, "");
    try {
        game::install_staged(staged, storage);
        CHECK(false);
    } catch (const game::ModuleError& error) {
        CHECK(std::string(error.what()).find("empty") != std::string::npos);
    }
    CHECK(slurp(target) == "module two");
    std::error_code ignored;
    fs::permissions(target, fs::perms::owner_all, ignored);
    fs::remove_all(root, ignored);
}

// A module the user picked is checked before it replaces the installed one (a refused file changes nothing).
void test_import_module(const std::filesystem::path& good, const char* const* refused, const char* const* messages, int count) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("sc64-game-import-test-" + std::to_string(std::rand()));
    fs::remove_all(root);
    const fs::path picked = root / "sc64-import-1.tmp", target = root / "game" / game::module_file_name();
    const auto place = [&](const fs::path& module) { fs::create_directories(root); fs::copy_file(module, picked, fs::copy_options::overwrite_existing); };
    const auto refuses = [&](const char* fragment, int line) {
        try {
            game::import_module(picked, root);
            check(false, "import_module should have thrown", line);
        } catch (const game::ModuleError& error) {
            if (std::string(error.what()).find(fragment) == std::string::npos) {
                std::fprintf(stderr, "FAIL line %d: \"%s\" lacks \"%s\"\n", line, error.what(), fragment);
                ++failures;
            }
        }
    };
    CHECK(game::importable());
#if !defined(_WIN32)
    // A module named by the environment (or beside the application) is the application's own: nothing to supply.
    setenv("SC64_GAME_MODULE", "/somewhere/sc64-game", 1);
    CHECK(game::bundled());
    unsetenv("SC64_GAME_MODULE");
#endif
    place(good);
#if !defined(_WIN32)
    // A read-only leftover of an earlier import does not stop the next one.
    spit(root / "game" / (".import-" + game::module_file_name()), "stale");
    fs::permissions(root / "game" / (".import-" + game::module_file_name()), fs::perms::owner_read);
#endif
    game::import_module(picked, root);
    CHECK(!fs::exists(picked) && fs::is_regular_file(target) && slurp(target) == slurp(good));
    CHECK(!fs::exists(root / "game" / (".import-" + game::module_file_name())));
    const std::string installed = slurp(target);
    for (int i = 0; i < count; ++i) {
        place(refused[i]);
        refuses(messages[i], __LINE__);
        CHECK(slurp(target) == installed);  // the working module stays
        CHECK(!fs::exists(root / "game" / (".import-" + game::module_file_name())));
    }
    spit(picked, "not a library");
    refuses("not a game module", __LINE__);
    CHECK(slurp(target) == installed);
    place(good);  // the same module again replaces the read-only file
    game::import_module(picked, root);
    CHECK(slurp(target) == installed);
    std::error_code ignored;
    fs::permissions(target, fs::perms::owner_all, ignored);
    fs::remove_all(root, ignored);
}

// A refused module is unloaded: the corrected module installed over the same path is the one that loads next.
void test_reload_after_refusal(const std::filesystem::path& good, const std::filesystem::path& bad_abi) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("sc64-game-reload-test-" + std::to_string(std::rand()));
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path module = root / game::module_file_name();
    fs::copy_file(bad_abi, module);
    try {
        game::load_file(module, rom);
        CHECK(false);
    } catch (const game::ModuleError&) {}
    fs::remove(module);  // a new file (new inode) at the same path, as an import does
    fs::copy_file(good, module);
    const Sc64ModuleInfo& info = game::load_file(module, rom);
    CHECK(info.entrypoint_vram == 0x80000400u);
    std::error_code ignored;
    fs::remove_all(root, ignored);
}

// What follows a module the user picked: install, then start with a kept ROM or ask for it.
void test_module_import_event() {
    using Kind = sc64::shell::ImportEvent::Kind;
    const auto event = [](bool install_ok, std::optional<std::string> rom, std::optional<std::string> load_error, std::string* loaded = nullptr) {
        return sc64::shell::module_import_event(
            [&] { if (!install_ok) throw game::ModuleError("refused"); },
            [&] { return rom; },
            [&](const std::string& path) { if (loaded) *loaded = path; return load_error; });
    };
    auto result = event(false, "rom", std::nullopt);
    CHECK(result.kind == Kind::failed && result.text == "refused");  // a refused module starts nothing
    std::string loaded;
    result = event(true, "stored.z64", std::nullopt, &loaded);
    CHECK(result.kind == Kind::started && loaded == "stored.z64");
    result = event(true, "stored.z64", "Expected a 32 MiB ROM dump");
    CHECK(result.kind == Kind::failed && result.text == "Expected a 32 MiB ROM dump");
    loaded.clear();
    result = event(true, std::nullopt, std::nullopt, &loaded);
    CHECK(result.kind == Kind::notice && loaded.empty() && result.text.find("choose your ROM") != std::string::npos);
    std::puts("module import events passed");
}

// The Android-only decisions are pure functions, so they are checked here.
void test_staging_policy() {
    namespace fs = std::filesystem;
    CHECK(game::staging_directory(30, "/sdcard/Android/data/app/files") == fs::path("/sdcard/Android/data/app/files"));
    CHECK(game::staging_directory(36, "/x") == fs::path("/x"));
    CHECK(game::staging_directory(29, "/x").empty() && game::staging_directory(28, "/x").empty());  // other apps could write it
    CHECK(game::staging_directory(34, nullptr).empty() && game::staging_directory(34, "").empty());
    const std::vector<fs::path> places = {"/data/game/sc64-game.so"};
    const auto android = game::missing_message(places, "/sdcard/files", false, "");
    for (const char* part : {"was not found", "/data/game/sc64-game.so", "sc64_setup --rom your.z64 --target android-arm64", "/sdcard/files", "adb push",
                             "Select game module", "Select ROM", "launcher package"})
        CHECK(android.find(part) != std::string::npos);
    const auto old_android = game::missing_message(places, {}, true, "");
    CHECK(old_android.find("Android 10") != std::string::npos && old_android.find("adb push") == std::string::npos);
    CHECK(old_android.find("Select game module") != std::string::npos);
    // A release user has no repository: nothing in the message may require make or a checkout.
    CHECK(android.find("make ") == std::string::npos && old_android.find("make ") == std::string::npos);
    const auto desktop = game::missing_message(places, {}, false, "");
    CHECK(desktop.find("docs/desktop.md") != std::string::npos && desktop.find("Android") == std::string::npos);
    CHECK(game::missing_message(places, "/sdcard/files", false, "Cannot access the staged module").find("Cannot access the staged module") != std::string::npos);
    // SDL reports "./" as the base path on Android: not a place to load code from.
    CHECK(game::candidates("/storage", "./", "").size() == 1 && game::candidates("/storage", "", "").size() == 1);
    CHECK(game::candidates("/storage", std::filesystem::absolute("/app/"), "").size() == 2);
}

int main(int argc, char** argv) {
    if (argc != 8) { std::fprintf(stderr, "usage: good bad_abi bad_layout bad_rom refuse no_init missing\n"); return 2; }
    const std::filesystem::path good = argv[1];

    // Search order and file name.
    const auto app = std::filesystem::absolute("/app/resources/");  // a drive-less path is not absolute on Windows
    const auto places = game::candidates("/storage", app, "/custom/module");
    CHECK(places.size() == 3);
    CHECK(places[0] == std::filesystem::path("/custom/module"));
    CHECK(places[1] == app / game::module_file_name());
    CHECK(places[2] == std::filesystem::path("/storage") / "game" / game::module_file_name());
    CHECK(game::candidates("", "", "").empty());

    // The host side of the ABI is complete.
    const Sc64HostApi api = game::host_api();
    CHECK(api.abi == SC64_MODULE_ABI && api.size == sizeof(Sc64HostApi));
    int imports = 0, missing = 0;
#define SC64_GUEST_CALL(name) ++imports; missing += api.name == nullptr;
#define SC64_IMPORT_V(name, params, args) ++imports; missing += api.name == nullptr;
#define SC64_IMPORT_R(ret, name, params, args) ++imports; missing += api.name == nullptr;
#include "sc64_imports.def"
#undef SC64_GUEST_CALL
#undef SC64_IMPORT_V
#undef SC64_IMPORT_R
    CHECK(imports > 60 && missing == 0);

    test_install_staged();
    test_staging_policy();
    {
        const char* refused[] = {argv[2], argv[3], argv[4], argv[5], argv[6]};
        const char* messages[] = {"ABI", "layout", "different ROM", "refused", "not a game module"};
        test_import_module(good, refused, messages, 5);
        test_reload_after_refusal(good, argv[2]);
        test_module_import_event();
    }

    // A good module loads, validates and its function pointers work.
    const Sc64ModuleInfo& info = game::load_file(good, rom);
    CHECK(info.entrypoint_vram == 0x80000400u && info.num_sections == 1);
    CHECK(info.sections[0].funcs[0].func == info.entrypoint);
    info.entrypoint(nullptr, nullptr);
    info.minimap_set_position(nullptr, nullptr);
    {
        SDL_SharedObject* object = SDL_LoadObject(good.string().c_str());
        auto calls = reinterpret_cast<int (*)()>(SDL_LoadFunction(object, "fixture_entrypoint_calls"));
        CHECK(calls && calls() == 101);

        // Exceptions thrown by the host cross the module's frames, also on guest threads.
        auto call = reinterpret_cast<void (*)(void (*)(), int)>(SDL_LoadFunction(object, "fixture_call"));
        CHECK(call != nullptr);
        for (int depth : {0, 1, 12}) {
            bool caught = false;
            try { call(throw_terminated, depth); } catch (const Terminated&) { caught = true; }
            CHECK(caught);
        }
        std::atomic<int> caught{0};
        std::thread worker([&] {
            for (int i = 0; i < 200; ++i) try { call(throw_terminated, 3); } catch (const Terminated&) { ++caught; }
        });
        worker.join();
        CHECK(caught == 200);
    }

    // Every defect is refused before the game starts, with a message that says what to do.
    expect_error(argv[2], "ABI", __LINE__);
    expect_error(argv[3], "layout", __LINE__);
    expect_error(argv[4], "different ROM", __LINE__);
    expect_error(argv[5], "refused", __LINE__);
    expect_error(argv[6], "not a game module", __LINE__);
    expect_error(argv[7], "Cannot load", __LINE__);
    try {
        game::load_file(good, "0000000000000000000000000000000000000000");
        CHECK(false);
    } catch (const game::ModuleError& error) {
        CHECK(std::string(error.what()).find("different ROM") != std::string::npos);
    }

    std::puts(failures ? "game_module FAILED" : "game_module OK");
    return failures ? 1 : 0;
}
