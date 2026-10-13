// sc64_setup: builds the game module from your own ROM (ADR-087).
#include "setup.hpp"
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_stdinc.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
namespace fs = std::filesystem;
using namespace sc64::setup;

void usage() {
    std::puts("usage: sc64_setup --rom PATH [--data DIR] [--resources DIR] [--recompiler PATH]\n"
              "                  [--compiler PATH | --use-zig] [--jobs N] [--force] [--keep-work] [--no-download]\n"
              "Builds sc64-game from your NTSC-U ROM into DIR/game (default: the game's storage folder).");
}

fs::path default_data_dir() {
    char* path = SDL_GetPrefPath("", "StarCraft64");
    if (!path) throw SetupError(std::string("Storage directory unavailable: ") + SDL_GetError());
    fs::path result(path);
    SDL_free(path);
    while (result.has_filename() == false && result.parent_path() != result) result = result.parent_path();
    return result;
}

fs::path executable_dir() {
    const char* base = SDL_GetBasePath();
    return base ? fs::path(base) : fs::path(".");
}
}

int main(int argc, char** argv) {
    Options options;
    options.jobs = std::max(1u, std::thread::hardware_concurrency());
    if (const char* cc = std::getenv("CC")) options.cc_env = cc;
    const fs::path here = executable_dir();
    try {
        for (int i = 1; i < argc; ++i) {
            const std::string_view option = argv[i];
            const auto value = [&]() -> const char* {
                if (i + 1 >= argc) throw SetupError(std::string(option) + " needs a value");
                return argv[++i];
            };
            if (option == "--rom") options.rom = fs::u8path(value());
            else if (option == "--data") options.data_dir = fs::u8path(value());
            else if (option == "--resources") options.resources = fs::u8path(value());
            else if (option == "--recompiler") options.recompiler = fs::u8path(value());
            else if (option == "--compiler") options.compiler = fs::u8path(value());
            else if (option == "--jobs") options.jobs = unsigned(std::max(1, std::atoi(value())));
            else if (option == "--force") options.force = true;
            else if (option == "--keep-work") options.keep_work = true;
            else if (option == "--no-download") options.allow_download = false;
            else if (option == "--use-zig") options.use_zig = true;
            else if (option == "--help" || option == "-h") { usage(); return 0; }
            else throw SetupError("Unknown option " + std::string(option));
        }
        if (options.rom.empty()) { usage(); return 2; }
        if (options.data_dir.empty()) options.data_dir = default_data_dir();
        if (options.resources.empty()) options.resources = here / "setup-resources";
        if (options.recompiler.empty()) {
            if (const char* env = std::getenv("SC64_RECOMPILER")) options.recompiler = fs::u8path(env);
            else options.recompiler = here / ("N64Recomp" + host_info().exe_suffix);
        }
        Reporter report;
        report.step = [](int step, int total, const std::string& text) { std::printf("[%d/%d] %s\n", step, total, text.c_str()); std::fflush(stdout); };
        report.info = [](const std::string& text) { std::printf("      %s\n", text.c_str()); std::fflush(stdout); };
        const Result result = run_setup(options, run_process, report);
        std::printf("%s: %s\n", result.rebuilt ? "Built" : "Ready", result.module.string().c_str());
        return 0;
    } catch (const SetupError& error) {
        std::fprintf(stderr, "sc64_setup: %s\n", error.what());
        return 1;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "sc64_setup: %s\n", error.what());
        return 1;
    }
}
