#include "toolchain.hpp"
#include "recipe.hpp"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sc64::setup {
Platform host_info() {
    Platform host;
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

Platform parse_target(const std::string& name) {
    if (name == "android-arm64") return {"android", "arm64", ".so", ""};
    std::string known;
    for (const auto& entry : target_names()) known += (known.empty() ? "" : ", ") + entry;
    throw SetupError("Unknown target " + name + " (known: " + known + ")");
}

std::vector<std::string> target_names() { return {"android-arm64"}; }

std::string platform_name(const Platform& platform) { return platform.os + "-" + platform.arch; }

bool same_platform(const Platform& a, const Platform& b) { return a.os == b.os && a.arch == b.arch; }

std::vector<std::string> compile_flags(const Platform& target) {
    std::vector<std::string> flags{"-std=c17", "-O2", "-fno-strict-aliasing", "-ffp-contract=off", "-fexceptions",
                                   "-fvisibility=hidden"};
    if (target.os != "windows") flags.push_back("-fPIC");
    return flags;
}

std::vector<std::string> compile_args(const Toolchain& toolchain, const Platform& target, const ModuleSources& sources,
                                      const std::filesystem::path& source, const std::filesystem::path& object) {
    std::vector<std::string> args = toolchain.prefix;
    for (auto& flag : compile_flags(target)) args.push_back(std::move(flag));
    if (target.os == "macos" && toolchain.kind != "zig") args.push_back("-mmacosx-version-min=13.0");
    args.push_back("-DSC64_MODULE_ROM_SHA1=\"" + sources.rom_sha1 + "\"");
    // Source paths end up in assert messages: keep the (temporary) build directories out of the module.
    args.push_back("-ffile-prefix-map=" + path_utf8(sources.generated_dir) + "=generated");
    args.push_back("-ffile-prefix-map=" + path_utf8(sources.module_dir) + "=module");
    args.push_back("-I" + path_utf8(sources.module_dir));
    args.push_back("-I" + path_utf8(sources.module_dir / "include"));
    args.push_back("-I" + path_utf8(sources.generated_dir));
    args.insert(args.end(), {"-c", path_utf8(source), "-o", path_utf8(object)});
    return args;
}

std::vector<std::string> link_args(const Toolchain& toolchain, const Platform& target,
                                   const std::vector<std::filesystem::path>& objects, const std::filesystem::path& output) {
    std::vector<std::string> args = toolchain.prefix;
    args.push_back(target.os == "macos" && toolchain.kind != "zig" ? "-dynamiclib" : "-shared");
    if (target.os == "macos" && toolchain.kind != "zig") args.push_back("-mmacosx-version-min=13.0");
    // The install name would otherwise record the temporary work directory.
    if (target.os == "macos") args.push_back("-Wl,-install_name,@rpath/" + path_utf8(output.filename()));
    if (target.os != "macos") args.push_back("-s");  // symbols are not needed; the module is large otherwise
    if (target.os == "windows") args.push_back("-static-libgcc");
    if (target.os == "android")  // 16 KiB pages (the app's own libraries use the same alignment), no executable stack
        // --no-undefined: a symbol missing from the NDK sysroot is a build error, not a dlopen failure on the phone.
        args.insert(args.end(), {"-Wl,-z,max-page-size=16384", "-Wl,-z,common-page-size=16384", "-Wl,-z,noexecstack",
                                 "-Wl,-z,relro", "-Wl,-z,now", "-Wl,--no-undefined"});
    for (const auto& object : objects) args.push_back(path_utf8(object));
    args.insert(args.end(), {"-o", path_utf8(output)});
    return args;
}

std::string zig_target(const Platform& platform) {
    const std::string arch = platform.arch == "arm64" ? "aarch64" : "x86_64";
    if (platform.os == "macos") return arch + "-macos.13.0";
    if (platform.os == "windows") return arch + "-windows-gnu";
    return arch + "-linux-gnu.2.17";
}

Toolchain zig_toolchain(const std::filesystem::path& zig, const Platform& host, const std::string& version,
                        const std::filesystem::path& cache_dir) {
    // zig keeps compiled libc support and compiler_rt in a cache; keep it with the tool, not in a profile folder.
    return {"zig", zig, {"cc", "-target", zig_target(host)}, "zig " + version,
            {{"ZIG_GLOBAL_CACHE_DIR", path_utf8(cache_dir / "global")}, {"ZIG_LOCAL_CACHE_DIR", path_utf8(cache_dir / "local")}}};
}

namespace {
std::string ndk_prebuilt_tag(const Platform& host) {
    // The NDK ships one x86_64 host toolchain per OS (it runs on Apple silicon through Rosetta).
    return host.os == "macos" ? "darwin-x86_64" : host.os == "windows" ? "windows-x86_64" : "linux-x86_64";
}

std::filesystem::path sdk_default(const Platform& host, const std::function<std::string(const char*)>& env) {
    if (host.os == "windows") {
        const std::string local = env("LOCALAPPDATA");
        return local.empty() ? std::filesystem::path() : path_from_utf8(local) / "Android" / "Sdk";
    }
    const std::string home = env("HOME");
    if (home.empty()) return {};
    return host.os == "macos" ? path_from_utf8(home) / "Library" / "Android" / "sdk" : path_from_utf8(home) / "Android" / "Sdk";
}
}

std::vector<std::filesystem::path> ndk_candidates(const Platform& host, const std::function<std::string(const char*)>& env) {
    namespace fs = std::filesystem;
    std::vector<fs::path> result;
    const auto add = [&](const fs::path& path) {
        std::error_code error;
        if (!path.empty() && fs::is_directory(path, error) && std::find(result.begin(), result.end(), path) == result.end())
            result.push_back(path);
    };
    for (const char* name : {"ANDROID_NDK_ROOT", "ANDROID_NDK_HOME", "ANDROID_NDK"})
        if (const std::string value = env(name); !value.empty()) add(path_from_utf8(value));
    std::vector<fs::path> sdks;
    for (const char* name : {"ANDROID_SDK_ROOT", "ANDROID_HOME", "ANDROID_SDK"})
        if (const std::string value = env(name); !value.empty()) sdks.push_back(path_from_utf8(value));
    sdks.push_back(sdk_default(host, env));
    for (const auto& sdk : sdks) {
        std::error_code error;
        std::vector<fs::path> versions;
        if (fs::is_directory(sdk / "ndk", error))
            for (const auto& entry : fs::directory_iterator(sdk / "ndk", error))
                if (entry.is_directory(error)) versions.push_back(entry.path());
        std::sort(versions.begin(), versions.end(), [](const fs::path& a, const fs::path& b) {
            const auto number = [](const fs::path& p) {  // 29.0.14206865 sorts after 9.x
                return std::strtol(path_utf8(p.filename()).c_str(), nullptr, 10);
            };
            return number(a) != number(b) ? number(a) > number(b) : a.filename() > b.filename();
        });
        for (const auto& version : versions) add(version);
        add(sdk / "ndk-bundle");
    }
    return result;
}

std::filesystem::path ndk_clang(const std::filesystem::path& ndk, const Platform& host) {
    const auto clang = ndk / "toolchains" / "llvm" / "prebuilt" / ndk_prebuilt_tag(host) / "bin" / ("clang" + host.exe_suffix);
    std::error_code error;
    return std::filesystem::is_regular_file(clang, error) ? clang : std::filesystem::path();
}

std::optional<std::filesystem::path> find_ndk(const Platform& host, const std::function<std::string(const char*)>& env) {
    for (const auto& candidate : ndk_candidates(host, env))
        if (!ndk_clang(candidate, host).empty()) return candidate;
    return std::nullopt;
}

Toolchain ndk_toolchain(const std::filesystem::path& clang) {
    // `clang --target=...` is what the NDK's aarch64-linux-androidNN-clang wrappers run (a .cmd on Windows).
    return {"clang", clang, {"--target=aarch64-linux-android" + std::to_string(kAndroidApi)}, "", {}};
}

bool macos_developer_tools(const Runner& run, const std::filesystem::path& scratch) {
    std::filesystem::create_directories(scratch);
    std::string path = run_capture(run, {"xcode-select", {"-p"}, {}, {}}, scratch / "xcode-select.txt");
    path = path.substr(0, path.find_first_of("\r\n"));
    std::error_code error;
    return !path.empty() && std::filesystem::is_directory(path_from_utf8(path), error);
}

std::vector<Toolchain> system_candidates(const Platform& host, const std::string& cc_env, bool shims) {
    std::vector<Toolchain> result;
    if (!cc_env.empty()) {
        std::istringstream words(cc_env);
        std::string program, word;
        words >> program;
        Toolchain toolchain{"", program, {}, "", {}};
        while (words >> word) toolchain.prefix.push_back(word);
        const bool shim = program == "cc" || program == "clang" || program == "gcc";
        if (!program.empty() && (shims || !shim)) result.push_back(std::move(toolchain));
    }
    if (!shims) return result;
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

std::string probe_source(bool full) {
    if (!full) {
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
    // Shapes copied from the generated game: funcs_*.c and recomp_overlays.inl. The host functions the game
    // calls are defined here as the trampolines of module_main.c are, so that the shared object links.
    return R"(#include "recomp.h"
#include "librecomp/sections.h"

recomp_func_t* get_function(int32_t vram) { (void)vram; return 0; }
void switch_error(const char* func, uint32_t vram, uint32_t jtbl) { (void)func; (void)vram; (void)jtbl; }
void sc64_guest_poll(uint8_t* rdram) { (void)rdram; }

RECOMP_FUNC void sc64_probe_function(uint8_t* rdram, recomp_context* ctx) {
    uint64_t hi = 0, lo = 0, result = 0;
    int c1cs = 0;
    gpr jr_addend_80005244 = ctx->r3;
    (void)hi; (void)lo; (void)result; (void)c1cs;
    ctx->r29 = S32(0X803F << 16);
    ctx->r8 = ADD32(ctx->r8, 0X5A40);
    if (ctx->r8 == ctx->r9) {
        ctx->r6 = ADD32(ctx->r4, 0X1);

            goto L_80000430;
    }
L_80000420:
    { extern void sc64_guest_poll(uint8_t*); sc64_guest_poll(rdram); }
    ctx->r2 = MEM_W(ctx->r3, 0X0);
    MEM_W(0X10, ctx->r29) = ctx->r31;
    ctx->f0.fl = CVT_S_W(ctx->f0.u32l);
    if (ctx->r2 != 0) {

            goto L_80000420;
    }
L_80000430:
    switch (jr_addend_80005244 >> 2) {
        case 0: goto L_80000420; break;
        case 1: goto L_80000430; break;
        default: switch_error(__func__, 0x80005244, 0x80000518);
    }
    LOOKUP_FUNC(ctx->r8)(rdram, ctx);
    return;
}

static FuncEntry probe_funcs[] = {
    { .func = sc64_probe_function, .offset = 0x00000000, .rom_size = 0x0000003C },
};
static SectionTableEntry probe_sections[] = {
    { .rom_addr = 0x00001000, .ram_addr = 0x80000400, .size = 0x000D5640, .funcs = probe_funcs,
      .num_funcs = ARRLEN(probe_funcs), .relocs = nullptr, .num_relocs = 0, .index = 0 },
};
__attribute__((visibility("default"))) const SectionTableEntry* sc64_probe(void) { return probe_sections; }
)";
}

std::optional<Toolchain> probe(const Runner& run, Toolchain candidate, const Platform& target,
                               const std::filesystem::path& scratch, const std::filesystem::path& module_dir) {
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
    std::error_code header_error;
    const bool full = !module_dir.empty() && std::filesystem::is_regular_file(module_dir / "recomp.h", header_error);
    write_file(source, probe_source(full));  // binary: the same bytes on every host
    const auto object = scratch / "probe.o";
    const auto output = scratch / ("probe" + target.module_suffix);
    ModuleSources none{full ? module_dir : scratch, scratch, "0"};
    try {
        const auto log = scratch / "probe.log";
        if (run({candidate.program, compile_args(candidate, target, none, source, object), log, candidate.env}) != 0) return std::nullopt;
        if (run({candidate.program, link_args(candidate, target, {object}, output), log, candidate.env}) != 0) return std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(output, error)) return std::nullopt;
    return candidate;
}
}
