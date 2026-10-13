#include "setup.hpp"
#include "rom_image.hpp"
#include "sha.hpp"
#include "zig.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <json/json.hpp>
#include <regex>
#include <sstream>
#include <thread>

namespace sc64::setup {
namespace fs = std::filesystem;
namespace {
constexpr int kReceiptSchema = 1;

std::string stamp() {
    static std::atomic<unsigned> counter{0};
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char text[32];
    std::strftime(text, sizeof text, "%Y%m%d-%H%M%S", &local);
    return std::string(text) + "-" + std::to_string(counter++);
}

std::string utf8(const fs::path& path) {
    const auto text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::string sha256_of(std::string_view bytes) {
    return runtime::sha256_hex({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
}

int module_abi(const fs::path& module_dir) {
    const std::string header = read_file(module_dir / "sc64_module.h");
    std::smatch match;
    if (!std::regex_search(header, match, std::regex(R"(#define SC64_MODULE_ABI (\d+))")))
        throw SetupError("sc64_module.h does not define SC64_MODULE_ABI");
    return std::stoi(match[1]);
}

bool up_to_date(const fs::path& module, const fs::path& receipt, const nlohmann::json& expected) {
    std::error_code error;
    if (!fs::is_regular_file(module, error) || !fs::is_regular_file(receipt, error)) return false;
    try {
        const auto found = nlohmann::json::parse(read_file(receipt));
        for (const char* key : {"schema", "abi", "rom_sha1", "recipe_sha256", "recompiler_sha256"})
            if (found.at(key) != expected.at(key)) return false;
        return found.at("module_sha256") == file_sha256(module);
    } catch (const std::exception&) {
        return false;
    }
}

struct Logs {
    fs::path directory;
    fs::path file(const std::string& name) const { return directory / (name + ".log"); }
};

Toolchain choose_toolchain(const Options& options, const HostInfo& host, const Runner& run, const Logs& logs,
                           const fs::path& scratch, const Reporter& report) {
    if (options.compiler) {
        auto found = probe(run, {"", *options.compiler, {}, "", {}}, host, scratch / "probe-given", options.resources / "module");
        if (!found) throw SetupError("The compiler " + utf8(*options.compiler) + " cannot build the game module "
                                     "(a probe build failed). Use gcc or clang, or leave --compiler out.");
        return *found;
    }
    int number = 0;
    if (!options.use_zig)
        for (auto& candidate : system_candidates(host, options.cc_env))
            if (auto found = probe(run, candidate, host, scratch / ("probe-" + std::to_string(number++)), options.resources / "module"))
                return *found;
    if (!options.allow_download)
        throw SetupError("No usable C compiler was found (tried $CC, cc, clang, gcc) and downloads are disabled. Install gcc or clang.");
    report.info(options.use_zig ? "Using the pinned zig " + std::string(zig_version())
                                    : "No usable system C compiler found; using zig " + std::string(zig_version()));
    const fs::path zig = ensure_zig(run, host, options.data_dir / "tools", logs.file("zig-download"), report.info);
    const auto version = run_capture(run, {zig, {"version"}, {}}, scratch / "zig-version.txt");
    if (version.empty()) throw SetupError("The downloaded zig does not run on this machine: " + utf8(zig));
    Toolchain toolchain = zig_toolchain(zig, host, version.substr(0, version.find_first_of("\r\n")), options.data_dir / "tools" / "zig-cache");
    if (auto found = probe(run, toolchain, host, scratch / "probe-zig", options.resources / "module")) return *found;
    throw SetupError("zig could not build the probe module; see " + utf8(scratch / "probe-zig" / "probe.log"));
}

std::vector<fs::path> generated_sources(const fs::path& generated) {
    std::vector<fs::path> sources;
    for (const auto& entry : fs::directory_iterator(generated)) {
        const auto name = entry.path().filename().string();
        if (name.starts_with("funcs_") && name.ends_with(".c")) sources.push_back(entry.path());
    }
    std::sort(sources.begin(), sources.end());
    return sources;
}
}

fs::path module_path(const fs::path& data_dir, const HostInfo& host) {
    return data_dir / "game" / ("sc64-game" + host.module_suffix);
}

Result run_setup(const Options& options, const Runner& run, const Reporter& report) {
    const HostInfo host = host_info();
    const auto say = [&](int number, const std::string& text) { if (report.step) report.step(number, kSteps, text); };
    const auto info = [&](const std::string& text) { if (report.info) report.info(text); };
    Result result;
    result.module = module_path(options.data_dir, host);

    say(1, "Checking the ROM");
    std::string bytes;
    if (options.rom_image) {
        bytes = *options.rom_image;
    } else {
        try {
            bytes = read_file(options.rom);
        } catch (const SetupError&) {
            throw SetupError("Cannot read the ROM " + utf8(options.rom));
        }
    }
    const bool big_endian = bytes.size() >= 4 && std::string_view(bytes).substr(0, 4) == std::string_view("\x80\x37\x12\x40", 4);
    try {
        const std::span<std::uint8_t> view{reinterpret_cast<std::uint8_t*>(bytes.data()), bytes.size()};
        runtime::normalize_rom_byte_order(view);
        if (runtime::sha1_hex(view) != options.rom_sha1)
            throw std::runtime_error("ROM SHA-1 does not match the researched NTSC-U revision");
    } catch (const std::runtime_error& error) {
        throw SetupError(std::string(error.what()) + ". This game needs the NTSC-U (USA) dump of StarCraft 64.");
    }
    const fs::path recipe_dir = options.resources / "recipe" / "ntsc-u";
    const fs::path module_dir = options.resources / "module";
    const Manifest manifest = load_recipe(recipe_dir, options.rom_sha1);
    if (!fs::is_regular_file(options.recompiler)) throw SetupError("N64Recomp not found: " + utf8(options.recompiler));
    nlohmann::json receipt = {{"schema", kReceiptSchema}, {"abi", module_abi(module_dir)}, {"rom_sha1", manifest.rom_sha1},
                              {"recipe_sha256", file_sha256(recipe_dir / std::string(kManifestFile))},
                              {"recompiler_sha256", file_sha256(options.recompiler)}};
    const fs::path receipt_path = options.data_dir / "game" / "receipt.json";
    if (!options.force && up_to_date(result.module, receipt_path, receipt)) {
        info("The game module is already built for this ROM");
        return result;
    }

    const std::string run_id = stamp();
    const Logs logs{options.data_dir / "setup" / "logs" / run_id};
    const fs::path work = options.data_dir / "setup" / ("work-" + run_id);
    fs::create_directories(logs.directory);
    fs::create_directories(work);
    result.logs = logs.directory;
    struct WorkGuard {
        fs::path path;
        bool keep;
        ~WorkGuard() { std::error_code ignored; if (!keep) fs::remove_all(path, ignored); }
    } guard{work, options.keep_work};

    say(2, "Looking for a C compiler");
    // Probe builds go beside the logs: the work directory is removed on failure, the diagnostics are not.
    const Toolchain toolchain = choose_toolchain(options, host, run, logs, logs.directory, report);
    result.compiler = toolchain.kind + ": " + toolchain.version;
    info(result.compiler);

    say(3, "Generating the game code");
    fs::path rom_file = options.rom_image ? fs::path() : fs::absolute(options.rom);
    if (!big_endian || options.rom_image) {
        rom_file = work / "rom.z64";
        write_file(rom_file, bytes);
    }
    const fs::path generated = work / "generated";
    copy_bytes(recipe_dir / std::string(kSymbolsFile), work / std::string(kSymbolsFile));
    const fs::path config = work / std::string(kConfigFile);
    write_file(config, render_config(read_file(recipe_dir / std::string(kConfigFile)), rom_file, generated));
    const fs::path recompiler_log = logs.file("recompile");
    const int recompiled = run({options.recompiler, {utf8(config)}, recompiler_log});
    if (recompiled != 0) throw SetupError("N64Recomp failed (exit " + std::to_string(recompiled) + "); see " + utf8(recompiler_log));
    try {
        inspect_output(generated, read_file(recompiler_log));
        if (const auto problems = compare_hashes(manifest.files, hash_directory(generated)); !problems.empty())
            throw SetupError("The generated code does not match the recipe (" + problems.front() + "); see " + utf8(recompiler_log));
    } catch (const SetupError& error) {
        throw SetupError(std::string(error.what()) + "\nThe ROM must be the unmodified NTSC-U dump.");
    }

    say(4, "Compiling the game module");
    const ModuleSources sources{module_dir, generated, manifest.rom_sha1};
    std::vector<fs::path> inputs = generated_sources(generated);
    inputs.push_back(module_dir / "module_main.c");
    fs::create_directories(work / "obj");
    std::vector<Command> compiles;
    std::vector<fs::path> objects;
    for (const auto& source : inputs) {
        objects.push_back(work / "obj" / (source.stem().string() + ".o"));
        compiles.push_back({toolchain.program, compile_args(toolchain, host, sources, source, objects.back()), logs.file("compile"), toolchain.env});
    }
    const BatchResult compiled = run_batch(run, compiles, options.jobs, [&](std::size_t done, std::size_t total) {
        info("Compiled " + std::to_string(done) + "/" + std::to_string(total));
    });
    if (!compiled.ok)
        throw SetupError("Compiling " + inputs[compiled.failed].filename().string() + " failed (exit " +
                         std::to_string(compiled.status) + "); see " + utf8(logs.file("compile")));
    const fs::path built = work / ("sc64-game" + host.module_suffix);
    if (run({toolchain.program, link_args(toolchain, host, objects, built), logs.file("link"), toolchain.env}) != 0 || !fs::is_regular_file(built))
        throw SetupError("Linking the game module failed; see " + utf8(logs.file("link")));

    say(5, "Installing the game module");
    receipt["module_sha256"] = file_sha256(built);
    receipt["compiler"] = result.compiler;
    fs::create_directories(result.module.parent_path());
    const fs::path staged = result.module.parent_path() / (".sc64-game.new" + host.module_suffix);
    copy_bytes(built, staged);
    try {
        fs::rename(staged, result.module);
    } catch (const fs::filesystem_error&) {
        std::error_code ignored;
        fs::remove(staged, ignored);
        throw SetupError("Cannot replace " + utf8(result.module) + "; close the game and run the setup again.");
    }
    write_file(receipt_path, receipt.dump(2) + "\n");
    result.rebuilt = true;
    return result;
}
}
