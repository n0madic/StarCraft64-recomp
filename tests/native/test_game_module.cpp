// Loader for the game module (ADR-086), against synthetic modules built like the real one.
#include "game_module.hpp"
#include <SDL3/SDL_loadso.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>

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

int main(int argc, char** argv) {
    if (argc != 8) { std::fprintf(stderr, "usage: good bad_abi bad_layout bad_rom refuse no_init missing\n"); return 2; }
    const std::filesystem::path good = argv[1];

    // Search order and file name.
    const auto places = game::candidates("/storage", "/app/resources/", "/custom/module");
    CHECK(places.size() == 3);
    CHECK(places[0] == std::filesystem::path("/custom/module"));
    CHECK(places[1] == std::filesystem::path("/app/resources/") / game::module_file_name());
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
