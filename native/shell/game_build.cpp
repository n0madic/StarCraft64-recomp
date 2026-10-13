#include "game_build.hpp"
#include "locate.hpp"
#include "log.hpp"
#include <SDL3/SDL_filesystem.h>
#include <atomic>
#include <cstdlib>
#include <mutex>

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
    if (const char* env = std::getenv("SC64_RECOMPILER"); env && *env) return setup::path_from_utf8(env);
    return setup::first_file(setup::recompiler_candidates(base_dir(), setup::host_info()));
}
setup::Options base_options(const fs::path& data_dir) {
    setup::Options options;
    options.data_dir = data_dir;
    options.resources = resources_dir();
    options.recompiler = recompiler_path();
    if (const char* cc = std::getenv("CC")) options.cc_env = cc;
    options.jobs = std::max(1u, std::thread::hardware_concurrency());
    return options;
}
}

struct GameBuild::Impl {
    std::mutex mutex;  // guards text and failed
    std::string text = "Preparing the game...";
    bool failed = false;
    std::string rom;   // the ROM given to start(rom, ...), kept for the launch that follows
    std::atomic<bool> finished{false};  // set last: everything above is final once it is true
    std::atomic<bool> cancel{false};
};

GameBuild::GameBuild() = default;

GameBuild::~GameBuild() {
    if (impl_ && worker_.joinable()) {
        impl_->cancel = true;
        worker_.detach();
    }
}

bool GameBuild::available() {
    std::error_code error;
    if (resources_dir().empty() || recompiler_path().empty()) return false;
    return fs::is_regular_file(resources_dir() / "recipe" / "ntsc-u" / "generated.json", error) &&
           fs::is_regular_file(resources_dir() / "module" / "module_main.c", error) &&
           fs::is_regular_file(recompiler_path(), error);
}

bool GameBuild::current(const fs::path& data_dir) { return setup::module_is_current(base_options(data_dir)); }

bool GameBuild::busy() const { return impl_ && !impl_->finished; }

void GameBuild::start(std::vector<std::uint8_t> rom, const fs::path& data_dir) {
    if (busy()) return;
    setup::Options options = base_options(data_dir);
    options.rom_image = std::string(reinterpret_cast<const char*>(rom.data()), rom.size());
    rom = {};  // the one copy of the ROM lives in options, then in the build state
    start(std::move(options));
}

void GameBuild::start(setup::Options options, setup::Runner run) {
    if (busy()) return;
    if (worker_.joinable()) worker_.join();  // a finished build that was never polled
    impl_ = std::make_shared<Impl>();
    worker_ = std::thread([impl = impl_, options = std::move(options), run = std::move(run)]() mutable {
        const setup::Runner guarded = [&](const setup::Command& command) {
            if (impl->cancel) throw setup::SetupError("The game build was cancelled");
            setup::Command copy = command;
            copy.cancel = &impl->cancel;
            return run(copy);
        };
        setup::Reporter report;
        report.step = [&](int step, int total, const std::string& text) {
            log::info("SC64 game build [%d/%d] %s", step, total, text.c_str());
            std::lock_guard lock(impl->mutex);
            impl->text = "Building the game from your ROM (one time)\n\n[" + std::to_string(step) + "/" + std::to_string(total) + "] " + text;
        };
        report.info = [&](const std::string& text) { log::info("SC64 game build: %s", text.c_str()); };
        try {
            setup::run_setup(options, guarded, report);
            if (options.rom_image) impl->rom = std::move(*options.rom_image);
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
    // `finished` first: once it is true the worker has stored its final text and failure.
    const bool finished = impl_->finished;
    {
        std::lock_guard lock(impl_->mutex);
        state->text = impl_->text;
        state->failed = impl_->failed;
    }
    state->running = !finished;
    if (finished) {
        if (worker_.joinable()) worker_.join();
        if (!state->failed) state->rom.assign(impl_->rom.begin(), impl_->rom.end());
        impl_.reset();
    }
    return state;
}
}
