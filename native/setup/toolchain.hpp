#pragma once
// C compilers for the game module (ADR-087): a system compiler when one passes a probe build,
// otherwise a pinned zig downloaded on first use. The module is plain C and needs only libc.
#include "process.hpp"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sc64::setup {
struct HostInfo {
    std::string os;      // macos | linux | windows
    std::string arch;    // arm64 | x86_64
    std::string module_suffix;  // .dylib | .so | .dll
    std::string exe_suffix;     // "" | .exe
};
HostInfo host_info();

struct Toolchain {
    std::string kind;                // clang | gcc | zig
    std::filesystem::path program;
    std::vector<std::string> prefix; // arguments before the compile flags, for example zig's "cc -target ..."
    std::string version;             // first line of --version
    std::vector<std::pair<std::string, std::string>> env;  // environment for every invocation
};

struct ModuleSources {
    std::filesystem::path module_dir;     // module_main.c, sc64_module.h, sc64_imports.def, recomp.h, include/
    std::filesystem::path generated_dir;  // funcs.h, recomp_overlays.inl, funcs_*.c
    std::string rom_sha1;
};

// Flags shared by every compiler; kept in step with cmake/game_module.cmake (tested).
std::vector<std::string> compile_flags(const HostInfo& host);
std::vector<std::string> compile_args(const Toolchain& toolchain, const HostInfo& host, const ModuleSources& sources,
                                      const std::filesystem::path& source, const std::filesystem::path& object);
std::vector<std::string> link_args(const Toolchain& toolchain, const HostInfo& host,
                                   const std::vector<std::filesystem::path>& objects, const std::filesystem::path& output);
// zig's -target for this host, with the baseline libc version the module is built against.
std::string zig_target(const HostInfo& host);
Toolchain zig_toolchain(const std::filesystem::path& zig, const HostInfo& host, const std::string& version,
                        const std::filesystem::path& cache_dir);

// Compilers worth probing, in order: $CC (may carry arguments), then the usual names.
std::vector<Toolchain> system_candidates(const HostInfo& host, const std::string& cc_env);
// Fills in kind/version and builds a tiny module the way the real one is built; nullopt if that fails.
// `module_dir` holds the module sources (recomp.h, sections.h shim); the probe uses them when present.
std::optional<Toolchain> probe(const Runner& run, Toolchain candidate, const HostInfo& host,
                               const std::filesystem::path& scratch, const std::filesystem::path& module_dir = {});
// The probe's source. `full` builds what the game module is made of: recomp.h, a function in the shape of
// the recompiler's output (a label followed by a hook block, gotos, a jump-table switch, the memory, float
// and function-lookup macros) and a section table with designated initializers. A compiler that cannot
// build this cannot build the game, and is rejected before any code is generated. Without `full` (no
// recomp.h beside the module sources) a self-contained subset is used.
std::string probe_source(bool full);
}
