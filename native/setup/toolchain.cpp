#include "toolchain.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>

namespace sc64::setup {
HostInfo host_info() {
    HostInfo host;
#if defined(__APPLE__)
    host.os = "macos"; host.module_suffix = ".dylib";
#elif defined(_WIN32)
    host.os = "windows"; host.module_suffix = ".dll"; host.exe_suffix = ".exe";
#else
    host.os = "linux"; host.module_suffix = ".so";
#endif
#if defined(__aarch64__) || defined(_M_ARM64)
    host.arch = "arm64";
#else
    host.arch = "x86_64";
#endif
    return host;
}

std::vector<std::string> compile_flags(const HostInfo& host) {
    std::vector<std::string> flags{"-std=c17", "-O2", "-fno-strict-aliasing", "-ffp-contract=off", "-fexceptions",
                                   "-fvisibility=hidden"};
    if (host.os != "windows") flags.push_back("-fPIC");
    return flags;
}

std::vector<std::string> compile_args(const Toolchain& toolchain, const HostInfo& host, const ModuleSources& sources,
                                      const std::filesystem::path& source, const std::filesystem::path& object) {
    std::vector<std::string> args = toolchain.prefix;
    for (auto& flag : compile_flags(host)) args.push_back(std::move(flag));
    if (host.os == "macos" && toolchain.kind != "zig") args.push_back("-mmacosx-version-min=13.0");
    args.push_back("-DSC64_MODULE_ROM_SHA1=\"" + sources.rom_sha1 + "\"");
    args.push_back("-I" + sources.module_dir.string());
    args.push_back("-I" + (sources.module_dir / "include").string());
    args.push_back("-I" + sources.generated_dir.string());
    args.insert(args.end(), {"-c", source.string(), "-o", object.string()});
    return args;
}

std::vector<std::string> link_args(const Toolchain& toolchain, const HostInfo& host,
                                   const std::vector<std::filesystem::path>& objects, const std::filesystem::path& output) {
    std::vector<std::string> args = toolchain.prefix;
    args.push_back(host.os == "macos" && toolchain.kind != "zig" ? "-dynamiclib" : "-shared");
    if (host.os == "macos" && toolchain.kind != "zig") args.push_back("-mmacosx-version-min=13.0");
    // The install name would otherwise record the temporary work directory.
    if (host.os == "macos") args.push_back("-Wl,-install_name,@rpath/" + output.filename().string());
    if (host.os != "macos") args.push_back("-s");  // symbols are not needed; the module is large otherwise
    if (host.os == "windows") args.push_back("-static-libgcc");
    for (const auto& object : objects) args.push_back(object.string());
    args.insert(args.end(), {"-o", output.string()});
    return args;
}

std::string zig_target(const HostInfo& host) {
    const std::string arch = host.arch == "arm64" ? "aarch64" : "x86_64";
    if (host.os == "macos") return arch + "-macos.13.0";
    if (host.os == "windows") return arch + "-windows-gnu";
    return arch + "-linux-gnu.2.17";
}

Toolchain zig_toolchain(const std::filesystem::path& zig, const HostInfo& host, const std::string& version,
                        const std::filesystem::path& cache_dir) {
    // zig keeps compiled libc support and compiler_rt in a cache; keep it with the tool, not in a profile folder.
    const auto path = [](const std::filesystem::path& p) { const auto text = p.u8string(); return std::string(text.begin(), text.end()); };
    return {"zig", zig, {"cc", "-target", zig_target(host)}, "zig " + version,
            {{"ZIG_GLOBAL_CACHE_DIR", path(cache_dir / "global")}, {"ZIG_LOCAL_CACHE_DIR", path(cache_dir / "local")}}};
}

std::vector<Toolchain> system_candidates(const HostInfo& host, const std::string& cc_env) {
    std::vector<Toolchain> result;
    if (!cc_env.empty()) {
        std::istringstream words(cc_env);
        std::string program, word;
        words >> program;
        Toolchain toolchain{"", program, {}, "", {}};
        while (words >> word) toolchain.prefix.push_back(word);
        if (!program.empty()) result.push_back(std::move(toolchain));
    }
    const std::vector<std::string> names = host.os == "windows" ? std::vector<std::string>{"gcc", "clang", "cc"}
                                                                   : std::vector<std::string>{"cc", "clang", "gcc"};
    for (const auto& name : names) {
        const bool duplicate = std::any_of(result.begin(), result.end(), [&](const Toolchain& t) {
            return t.program == name && t.prefix.empty();
        });
        if (!duplicate) result.push_back({"", name, {}, "", {}});
    }
    return result;
}

std::string probe_source() {
    return R"(#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <assert.h>
typedef struct { void (*f)(uint8_t*, void*); uint32_t n; } Entry;
static void leaf(uint8_t* r, void* c) { (void)c; r[0] = (uint8_t)fabs(-1.0); }
static Entry table[] = { { .f = leaf, .n = 1 } };
__attribute__((visibility("default"))) const Entry* sc64_probe(void) { assert(table[0].n == 1); return table; }
)";
}

std::optional<Toolchain> probe(const Runner& run, Toolchain candidate, const HostInfo& host,
                               const std::filesystem::path& scratch) {
    std::filesystem::create_directories(scratch);
    Command version{candidate.program, candidate.prefix, {}, candidate.env};
    version.args.push_back("--version");
    const std::string text = run_capture(run, version, scratch / "version.txt");
    if (text.empty()) return std::nullopt;
    std::string folded = text;
    std::transform(folded.begin(), folded.end(), folded.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    candidate.kind = folded.find("zig") != std::string::npos ? "zig" : folded.find("clang") != std::string::npos ? "clang" : "gcc";
    candidate.version = text.substr(0, text.find('\n'));
    const auto source = scratch / "probe.c";
    std::ofstream(source) << probe_source();
    const auto object = scratch / "probe.o";
    const auto output = scratch / ("probe" + host.module_suffix);
    ModuleSources none{scratch, scratch, "0"};
    try {
        const auto log = scratch / "probe.log";
        if (run({candidate.program, compile_args(candidate, host, none, source, object), log, candidate.env}) != 0) return std::nullopt;
        if (run({candidate.program, link_args(candidate, host, {object}, output), log, candidate.env}) != 0) return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(output, error)) return std::nullopt;
    return candidate;
}
}
