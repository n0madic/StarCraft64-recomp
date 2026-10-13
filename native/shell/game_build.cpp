#include "game_build.hpp"
#include "locate.hpp"
#include "log.hpp"
#include "setup.hpp"
#include <SDL3/SDL_filesystem.h>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <thread>

namespace sc64::desktop {
namespace fs = std::filesystem;
namespace log = sc64::shell::log;
namespace {
fs::path base_dir() {
    const char* base = SDL_GetBasePath();
    return base ? fs::path(base) : fs::path();
}
fs::path resources_dir() { return setup::first_directory(setup::resource_candidates(base_dir())); }
fs::path recompiler_path() {
    if (const char* env = std::getenv("SC64_RECOMPILER"); env && *env) return fs::u8path(env);
    return setup::first_file(setup::recompiler_candidates(base_dir(), setup::host_info()));
}
}

struct GameBuild::Impl {
    std::thread worker;
    std::mutex mutex;
    std::string text = "Preparing the game...";
    std::atomic<bool> finished{false};
    bool failed = false;
};

GameBuild::GameBuild() = default;

GameBuild::~GameBuild() {
    if (impl_ && impl_->worker.joinable()) impl_->worker.detach();
}

bool GameBuild::available() {
    std::error_code error;
    if (resources_dir().empty() || recompiler_path().empty()) return false;
    return fs::is_regular_file(resources_dir() / "recipe" / "ntsc-u" / "generated.json", error) &&
           fs::is_regular_file(resources_dir() / "module" / "module_main.c", error) &&
           fs::is_regular_file(recompiler_path(), error);
}

bool GameBuild::busy() const { return impl_ && !impl_->finished && impl_->worker.joinable(); }

void GameBuild::start(std::vector<std::uint8_t> rom, const fs::path& data_dir) {
    if (busy()) return;
    impl_ = std::make_unique<Impl>();
    Impl* impl = impl_.get();
    setup::Options options;
    options.rom_image = std::string(reinterpret_cast<const char*>(rom.data()), rom.size());
    options.data_dir = data_dir;
    options.resources = resources_dir();
    options.recompiler = recompiler_path();
    if (const char* cc = std::getenv("CC")) options.cc_env = cc;
    options.jobs = std::max(1u, std::thread::hardware_concurrency());
    impl->worker = std::thread([impl, options = std::move(options)]() mutable {
        setup::Reporter report;
        const auto set = [impl](std::string text) {
            std::lock_guard lock(impl->mutex);
            impl->text = std::move(text);
        };
        report.step = [&](int step, int total, const std::string& text) {
            log::info("SC64 game build [%d/%d] %s", step, total, text.c_str());
            set("Building the game from your ROM (one time)\n\n[" + std::to_string(step) + "/" + std::to_string(total) + "] " + text);
        };
        report.info = [&](const std::string& text) { log::info("SC64 game build: %s", text.c_str()); };
        try {
            setup::run_setup(options, setup::run_process, report);
        } catch (const std::exception& error) {
            log::error("SC64 game build failed: %s", error.what());
            std::lock_guard lock(impl->mutex);
            impl->failed = true;
            impl->text = std::string("Building the game failed:\n") + error.what();
        }
        impl->finished = true;
    });
}

std::unique_ptr<GameBuild::State> GameBuild::poll() {
    if (!impl_) return nullptr;
    auto state = std::make_unique<State>();
    {
        std::lock_guard lock(impl_->mutex);
        state->text = impl_->text;
        state->failed = impl_->failed;
    }
    state->running = !impl_->finished;
    if (!state->running) {
        if (impl_->worker.joinable()) impl_->worker.join();
        impl_.reset();
    }
    return state;
}
}
