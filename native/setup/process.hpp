#pragma once
// Child processes for the setup tool (ADR-087): no shell, arguments passed as given, output appended
// to a log file. The runner is a std::function so the orchestration is testable without processes.
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace sc64::setup {
struct Command {
    std::filesystem::path program;  // a path, or a bare name searched in PATH
    std::vector<std::string> args;  // UTF-8
    std::filesystem::path log;      // stdout and stderr are appended here; empty discards them
    std::vector<std::pair<std::string, std::string>> env;  // set (or replaced) in the child's environment
    // When set to true while the child runs, run_process terminates it and returns 143.
    const std::atomic<bool>* cancel = nullptr;
};

// UTF-8 text of a path on every platform. path::string() is the ANSI code page on Windows, while every
// command line and TOML string of this tool is UTF-8.
std::string path_utf8(const std::filesystem::path& path);
std::filesystem::path path_from_utf8(const std::string& utf8);

using Runner = std::function<int(const Command&)>;  // exit status; a program that cannot start throws (or exits 127)

// Real runner: posix_spawnp on POSIX, CreateProcessW on Windows. A signal death returns 128 + signal.
int run_process(const Command& command);

struct BatchResult {
    bool ok = true;
    std::size_t failed = 0;  // index of the first failed command
    int status = 0;
};
// Runs the commands on up to `jobs` threads, in order of index, and stops starting new ones after a failure.
BatchResult run_batch(const Runner& run, const std::vector<Command>& commands, unsigned jobs,
                      const std::function<void(std::size_t done, std::size_t total)>& progress = {});

// Runs a command and returns what it printed (for `--version`); empty if it failed.
std::string run_capture(const Runner& run, Command command, const std::filesystem::path& scratch_log);

// Windows command-line quoting (also used in logs); exposed for tests.
std::string quote_argument(const std::string& argument);
std::string describe(const Command& command);
}
