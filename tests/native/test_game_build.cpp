// GameBuild (ADR-087): the worker thread behind the first-run build. A fake process runner stands in for
// the compiler, so no ROM, compiler or generated code is needed.
#include "game_build.hpp"
#include "rom_image.hpp"
#include "sha.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <json/json.hpp>
#include <thread>

namespace fs = std::filesystem;
using namespace sc64::desktop;
namespace setup = sc64::setup;
using namespace std::chrono_literals;

namespace {
int failures = 0;
void check(bool condition, const char* what, int line) {
    if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, what); ++failures; }
}
#define CHECK(c) check((c), #c, __LINE__)

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
std::string sha256(const std::string& text) {
    return sc64::runtime::sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}

// Just enough of a recipe and a ROM for run_setup to reach its first child process.
struct Fixture {
    fs::path root = fs::temp_directory_path() / ("sc64-game-build-test-" + std::to_string(std::rand()));
    std::string rom;
    setup::Options options;
    Fixture() {
        fs::remove_all(root);
        rom.assign(sc64::runtime::rom_size, '\x11');
        rom[0] = char(0x80); rom[1] = 0x37; rom[2] = 0x12; rom[3] = 0x40;
        const std::string syms = "[[section]]\n", config = "[input]\nrom_file_path = \"@ROM@\"\noutput_func_path = \"@OUTPUT@\"\n";
        options.rom_sha1 = sc64::runtime::sha1_hex({reinterpret_cast<const std::uint8_t*>(rom.data()), rom.size()});
        const nlohmann::json manifest = {{"schema", 1}, {"rom_sha1", options.rom_sha1}, {"symbols_sha256", sha256(syms)},
                                         {"config_sha256", sha256(config)}, {"files", {{"funcs.h", sha256("x")}}}};
        write(root / "res/recipe/ntsc-u/sc64.syms.toml", syms);
        write(root / "res/recipe/ntsc-u/sc64.toml", config);
        write(root / "res/recipe/ntsc-u/generated.json", manifest.dump());
        write(root / "res/module/sc64_module.h", "#define SC64_MODULE_ABI 1\n");
        write(root / "N64Recomp", "binary");
        options.data_dir = root / "data";
        options.resources = root / "res";
        options.recompiler = root / "N64Recomp";
        options.allow_download = false;
        options.rom_image = rom;
    }
    ~Fixture() { std::error_code ignored; fs::remove_all(root, ignored); }
};

template<class Done> bool wait_for(Done&& done, std::chrono::seconds limit = 30s) {
    const auto end = std::chrono::steady_clock::now() + limit;
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(5ms);
    }
    return true;
}

std::unique_ptr<GameBuild::State> finish(GameBuild& build) {
    std::unique_ptr<GameBuild::State> state;
    wait_for([&] { state = build.poll(); return !state || !state->running; });
    return state;
}

void test_failure_is_reported_once_with_its_reason() {
    Fixture f;
    f.options.rom_image.reset();
    f.options.rom = f.root / "missing.z64";
    GameBuild build;
    CHECK(!build.poll());  // nothing started
    build.start(f.options);
    const auto state = finish(build);
    CHECK(state && !state->running && state->failed);
    CHECK(state && state->text.find("Cannot read the ROM") != std::string::npos);
    CHECK(state && state->rom.empty());
    CHECK(!build.poll());  // reported once, then forgotten
    CHECK(!build.busy());
}

void test_a_wrong_rom_is_a_failure_not_a_launch() {
    Fixture f;
    f.options.rom_sha1 = std::string(40, '0');
    GameBuild build;
    build.start(f.options);
    const auto state = finish(build);
    CHECK(state && state->failed && state->text.find("NTSC-U") != std::string::npos && state->rom.empty());
}

void test_a_failure_after_the_recompiler_is_reported() {
    Fixture f;
    // The fake runner answers the compiler probe, and fails once the recompiler has run.
    std::atomic<int> calls{0};
    const setup::Runner runner = [&](const setup::Command& command) {
        ++calls;
        if (command.program == "xcode-select") { write(command.log, f.root.string() + "\n"); return 0; }  // macOS: tools present
        if (command.program == f.options.recompiler) {
            const std::string text = [&] { std::ifstream in(command.args[0]); return std::string(std::istreambuf_iterator<char>(in), {}); }();
            const auto at = text.find("output_func_path = \"") + 20;
            write(fs::path(text.substr(at, text.find('"', at) - at)) / "funcs.h", "x");
            throw setup::SetupError("stop after the recompiler");  // the recipe of this fixture is not a real one
        }
        if (!command.log.empty()) write(command.log, "fakecc 1.0\n");
        for (std::size_t i = 0; i + 1 < command.args.size(); ++i)
            if (command.args[i] == "-o") write(command.args[i + 1], "object");  // compile or link output of the probe
        return 0;
    };
    GameBuild build;
    build.start(f.options, runner);
    const auto state = finish(build);
    CHECK(state && state->failed && state->text.find("stop after the recompiler") != std::string::npos);
    CHECK(calls > 0);
}

void test_the_runner_sees_the_cancel_flag_and_outlives_the_object() {
    Fixture f;
    std::atomic<bool> entered{false}, saw_cancel{false}, left{false};
    const setup::Runner runner = [&](const setup::Command& command) {
        entered = true;
        if (!command.cancel) return 1;
        while (!command.cancel->load()) std::this_thread::sleep_for(5ms);  // a long compile
        saw_cancel = true;
        left = true;
        return 143;
    };
    {
        GameBuild build;
        build.start(f.options, runner);
        CHECK(wait_for([&] { return entered.load(); }));
        CHECK(build.busy());
        const auto state = build.poll();
        CHECK(state && state->running && !state->failed);
        build.start(f.options, runner);  // already running: ignored
    }   // destroyed while the worker is inside the runner: it is cancelled, not abandoned
    CHECK(wait_for([&] { return left.load(); }));
    CHECK(saw_cancel);
    std::this_thread::sleep_for(300ms);  // the worker finishes against its own state, not the destroyed object
}
}

int main() {
    test_failure_is_reported_once_with_its_reason();
    test_a_wrong_rom_is_a_failure_not_a_launch();
    test_a_failure_after_the_recompiler_is_reported();
    test_the_runner_sees_the_cancel_flag_and_outlives_the_object();
    std::puts(failures ? "game build FAILED" : "game build OK");
    return failures ? 1 : 0;
}
