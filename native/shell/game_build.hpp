#pragma once
// First-run game build inside the application (ADR-087): when no game module exists, or the one in the
// game folder no longer matches this application, and the setup resources and the prebuilt N64Recomp sit
// beside the executable, sc64_setup's logic runs on a worker thread and its progress becomes the start-up message.
#include "setup.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace sc64::desktop {
class GameBuild {
public:
    GameBuild();
    // Cancels a running build (its child process is terminated) and lets the worker finish on its own.
    ~GameBuild();
    // True when the resources (recipe, module sources) and N64Recomp are present beside the executable.
    static bool available();
    // True when <data_dir>/game holds a module built from exactly these resources (its receipt matches).
    static bool current(const std::filesystem::path& data_dir);
    bool busy() const;
    // Starts building from `rom` (any byte order) into <data_dir>/game. Does nothing if already running.
    void start(std::vector<std::uint8_t> rom, const std::filesystem::path& data_dir);
    // The same with explicit options and process runner (tests); `run` sees Command::cancel.
    void start(setup::Options options, setup::Runner run = setup::run_process);
    struct State {
        bool running = false;
        bool failed = false;
        std::string text;                // progress while running, the error when failed
        std::vector<std::uint8_t> rom;   // the ROM given to start(rom, ...), handed back once the build succeeded
    };
    // Empty when nothing was started; a finished build is reported once and then forgotten.
    std::unique_ptr<State> poll();
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;  // the worker holds a reference too, so it may outlive this object
    std::thread worker_;
};
}
