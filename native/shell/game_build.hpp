#pragma once
// First-run game build inside the application (ADR-087): when no game module exists and the
// setup resources and the prebuilt N64Recomp sit beside the executable, sc64_setup's logic runs on a
// worker thread and its progress becomes the start-up message.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace sc64::desktop {
class GameBuild {
public:
    GameBuild();
    ~GameBuild();
    // True when the resources (recipe, module sources) and N64Recomp are present beside the executable.
    static bool available();
    bool busy() const;
    // Starts building from `rom` (any byte order) into <data_dir>/game. Does nothing if already running.
    void start(std::vector<std::uint8_t> rom, const std::filesystem::path& data_dir);
    struct State {
        bool running = false;
        bool failed = false;
        std::string text;  // progress while running, the error when failed
    };
    // Empty when nothing was started; a finished build is reported once and then forgotten.
    std::unique_ptr<State> poll();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
