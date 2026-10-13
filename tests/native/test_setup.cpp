// sc64_setup (ADR-087): recipe, toolchain commands and the whole flow against a fake process runner.
#include "locate.hpp"
#include "process.hpp"
#include "recipe.hpp"
#include "rom_image.hpp"
#include "setup.hpp"
#include "sha.hpp"
#include "toolchain.hpp"
#include "zig.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <json/json.hpp>
#include <mutex>
#include <string>
#include <thread>

namespace fs = std::filesystem;
using namespace sc64::setup;

namespace {
int failures = 0;
void check(bool condition, const char* what, int line) {
    if (!condition) { std::fprintf(stderr, "FAIL line %d: %s\n", line, what); ++failures; }
}
#define CHECK(c) check((c), #c, __LINE__)
template<class F> std::string error_of(F&& action) {
    try { action(); } catch (const SetupError& error) { return error.what(); }
    return {};
}
#define CHECK_ERROR(action, fragment) do { const std::string text_ = error_of([&] { action; }); \
    check(text_.find(fragment) != std::string::npos, #action " should fail with " fragment, __LINE__); \
    if (text_.find(fragment) == std::string::npos) std::fprintf(stderr, "  got: %s\n", text_.c_str()); } while (0)

std::string argv0;

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}
std::string sha256(const std::string& text) {
    return sc64::runtime::sha256_hex({reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
}
// Windows joins paths with a backslash; compare arguments by their generic form.
std::string generic(std::string text) {
    for (char& c : text) if (c == '\\') c = '/';
    return text;
}
bool has(const std::vector<std::string>& args, const std::string& text) {
    for (const auto& a : args) if (generic(a) == generic(text)) return true;
    return false;
}
bool has_prefix(const std::vector<std::string>& args, const std::string& prefix) {
    for (const auto& a : args) if (a.starts_with(prefix)) return true;
    return false;
}

// ---- processes ----
void test_process() {
    CHECK(quote_argument("plain") == "plain");
    CHECK(quote_argument("a b") == "\"a b\"");
    CHECK(quote_argument("") == "\"\"");
    CHECK(quote_argument("say \"hi\"") == "\"say \\\"hi\\\"\"");
    CHECK(quote_argument("dir with space\\") == "\"dir with space\\\\\"");
    CHECK(quote_argument("-DX=\"abc\"") == "\"-DX=\\\"abc\\\"\"");

    const fs::path log = fs::temp_directory_path() / "sc64-setup-test-process.log";
    fs::remove(log);
    CHECK(run_process({argv0, {"--child", "exit3"}, log}) == 3);
    CHECK(run_process({argv0, {"--child", "print"}, log}) == 0);
    const std::string text = [&] { std::ifstream in(log); return std::string(std::istreambuf_iterator<char>(in), {}); }();
    CHECK(text.find("child stdout") != std::string::npos && text.find("child stderr") != std::string::npos);
    // A program that cannot start is an exception, or exit 127 where the exec failure surfaces only in the child.
    bool failed = false;
    try { failed = run_process({"definitely-not-a-program-sc64", {}, {}}) != 0; } catch (const std::exception&) { failed = true; }
    CHECK(failed);

    // Environment overrides reach the child, replacing inherited values.
    CHECK(run_process({argv0, {"--child", "env"}, {}, {{"SC64_TEST_ENV", "x y"}}}) == 0);
    CHECK(run_process({argv0, {"--child", "env"}, {}, {{"SC64_TEST_ENV", "other"}}}) == 4);

    // Cancelling terminates a running child and returns 143; a command that already finished is unaffected.
    std::atomic<bool> cancel{false};
    Command sleeper{argv0, {"--child", "sleep"}, {}};
    sleeper.cancel = &cancel;
    std::thread canceller([&] { std::this_thread::sleep_for(std::chrono::milliseconds(300)); cancel = true; });
    const auto began = std::chrono::steady_clock::now();
    CHECK(run_process(sleeper) == 143);
    CHECK(std::chrono::steady_clock::now() - began < std::chrono::seconds(30));
    canceller.join();
    Command quick{argv0, {"--child", "exit3"}, {}};
    std::atomic<bool> never{false};
    quick.cancel = &never;
    CHECK(run_process(quick) == 3);

    // Paths cross the process boundary as UTF-8 on every platform.
    const std::string accented = "caf\xc3\xa9";
    CHECK(path_utf8(path_from_utf8(accented)) == accented);

    // Batch: runs everything, reports the first failure and stops starting new commands.
    std::mutex mutex;
    int started = 0;
    const Runner counting = [&](const Command& c) { std::lock_guard lock(mutex); ++started; return c.args[0] == "bad" ? 7 : 0; };
    std::vector<Command> commands;
    for (int i = 0; i < 40; ++i) commands.push_back({"x", {i == 3 ? "bad" : "ok"}, {}});
    const BatchResult single = run_batch(counting, commands, 1);
    CHECK(!single.ok && single.failed == 3 && single.status == 7 && started == 4);
    started = 0;
    const BatchResult all = run_batch(counting, std::vector<Command>(10, Command{"x", {"ok"}, {}}), 4);
    CHECK(all.ok && started == 10);
    std::size_t last = 0;
    run_batch(counting, std::vector<Command>(5, Command{"x", {"ok"}, {}}), 2, [&](std::size_t done, std::size_t total) { last = done; CHECK(total == 5); });
    CHECK(last == 5);
    bool batch_threw = false;
    try { run_batch([](const Command&) -> int { throw std::runtime_error("spawn"); }, {Command{"x", {}, {}}}, 2); }
    catch (const std::runtime_error&) { batch_threw = true; }
    CHECK(batch_threw);
}

// ---- locations ----
void test_locate() {
    const Platform linux{"linux", "x86_64", ".so", ""}, windows{"windows", "x86_64", ".dll", ".exe"};
    const fs::path root = fs::temp_directory_path() / "sc64-setup-test-locate";
    fs::remove_all(root);
    // A macOS bundle: base is Contents/Resources, N64Recomp sits in Contents/MacOS.
    const fs::path resources = root / "App.app/Contents/Resources", macos = root / "App.app/Contents/MacOS";
    write(resources / "setup-resources/recipe/x", "x");
    write(macos / "N64Recomp", "binary");
    CHECK(first_directory(resource_candidates(resources)) == (resources / "setup-resources"));
    CHECK(first_file(recompiler_candidates(resources, linux)) == (macos / "N64Recomp"));
    CHECK(recompiler_candidates(resources, windows)[0].filename() == "N64Recomp.exe");
    // A plain folder: everything beside the executable, and that wins over the bundle layout.
    write(resources / "N64Recomp", "binary");
    CHECK(first_file(recompiler_candidates(resources, linux)) == (resources / "N64Recomp"));
    CHECK(first_directory(resource_candidates(root / "nowhere")).empty());
    CHECK(first_file(recompiler_candidates(root / "nowhere", linux)).empty());
    fs::remove_all(root);
}

// ---- recipe ----
void test_recipe() {
    CHECK(toml_string("a\"b\\c") == "\"a\\\"b\\\\c\"");
    CHECK(toml_string("C:\\Users\\Игрок") == "\"C:\\\\Users\\\\Игрок\"");
    CHECK(toml_string(std::string("t\tn\n") + char(1)) == "\"t\\tn\\n\\u0001\"");
    const std::string tmpl = "rom_file_path = \"@ROM@\"\noutput_func_path = \"@OUTPUT@\"\n";
    CHECK(render_config(tmpl, "/r/x y.z64", "/o") == "rom_file_path = \"/r/x y.z64\"\noutput_func_path = \"/o\"\n");
    CHECK_ERROR(render_config("rom_file_path = \"@ROM@\"\n", "/r", "/o"), "exactly once");
    CHECK_ERROR(render_config(tmpl + "x = \"@ROM@\"\n", "/r", "/o"), "exactly once");

    const std::string good = R"({"schema":1,"rom_sha1":"abc","symbols_sha256":")" + std::string(64, 'a') +
        R"(","config_sha256":")" + std::string(64, 'b') + R"(","files":{"funcs_0.c":")" + std::string(64, 'c') + R"("}})";
    const Manifest manifest = parse_manifest(good, "abc");
    CHECK(manifest.files.size() == 1 && manifest.rom_sha1 == "abc");
    CHECK_ERROR(parse_manifest(good, "other"), "different ROM");
    CHECK_ERROR(parse_manifest("{", "abc"), "generated.json");
    std::string bad = good;
    bad.replace(bad.find("\"schema\":1"), 10, "\"schema\":2");
    CHECK_ERROR(parse_manifest(bad, "abc"), "schema");
    bad = good;
    bad.replace(bad.find("funcs_0.c"), 9, "../x/y.c");
    CHECK_ERROR(parse_manifest(bad, "abc"), "bad entry");
    bad = good;
    bad.replace(bad.find(std::string(64, 'c')), 64, "XYZ");
    CHECK_ERROR(parse_manifest(bad, "abc"), "bad entry");

    const std::map<std::string, std::string> expected{{"a", "1"}, {"b", "2"}};
    CHECK(compare_hashes(expected, expected).empty());
    const auto problems = compare_hashes(expected, {{"a", "9"}, {"c", "3"}});
    CHECK(problems == (std::vector<std::string>{"missing b", "unexpected c", "hash differs: a"}));

    // Recompiler output checks.
    const fs::path dir = fs::temp_directory_path() / "sc64-setup-test-output";
    const auto populate = [&] {
        fs::remove_all(dir);
        write(dir / "funcs.h", "x");
        write(dir / "lookup.cpp", "x");
        write(dir / "recomp_overlays.inl", "static SectionTableEntry section_table[] = {};");
        write(dir / "funcs_0.c", "RECOMP_FUNC void recomp_entrypoint(uint8_t* rdram) {}");
    };
    populate();
    inspect_output(dir, "[Info] Function count: 3\n[Warn] something harmless\n");
    for (const char* line : {"Error in foo", "Unhandled instruction", "Failed to write", "No function found at 0x80000000", "FAILED loudly"}) {
        CHECK_ERROR(inspect_output(dir, std::string("[Info] fine\n") + line + "\n"), "diagnostic rejected");
    }
    fs::remove(dir / "lookup.cpp");
    CHECK_ERROR(inspect_output(dir, ""), "incomplete");
    populate();
    write(dir / "funcs_0.c", "int not_the_entrypoint;");
    CHECK_ERROR(inspect_output(dir, ""), "Entrypoint was not emitted");
    populate();
    write(dir / "recomp_overlays.inl", "int nothing;");
    CHECK_ERROR(inspect_output(dir, ""), "Section registration table");
    populate();
    write(dir / "funcs_1.c", "");
    CHECK_ERROR(inspect_output(dir, ""), "incomplete");
    fs::remove_all(dir);
}

// ---- toolchain commands ----
void test_toolchain() {
    const Platform mac{"macos", "arm64", ".dylib", ""}, linux{"linux", "x86_64", ".so", ""}, windows{"windows", "x86_64", ".dll", ".exe"};
    const ModuleSources sources{"/res/module", "/gen", "deadbeef"};
    const Toolchain clang{"clang", "cc", {}, "clang 17", {}}, gcc{"gcc", "gcc", {}, "gcc 13", {}};
    const Toolchain zig = zig_toolchain("/t/zig", linux, "0.0.0", "/t/zig-cache");
    CHECK(zig.env.size() == 2 && zig.env[0].first == "ZIG_GLOBAL_CACHE_DIR" && generic(zig.env[0].second) == "/t/zig-cache/global");

    const auto flags = compile_flags(linux);
    for (const char* flag : {"-std=c17", "-O2", "-fno-strict-aliasing", "-ffp-contract=off", "-fexceptions", "-fvisibility=hidden", "-fPIC"})
        CHECK(has(flags, flag));
    CHECK(!has(compile_flags(windows), "-fPIC"));

    const auto compile = compile_args(gcc, linux, sources, "/gen/funcs_1.c", "/w/funcs_1.o");
    CHECK(has(compile, "-DSC64_MODULE_ROM_SHA1=\"deadbeef\""));
    CHECK(has(compile, "-I/res/module") && has(compile, "-I/res/module/include") && has(compile, "-I/gen"));
    CHECK(compile[compile.size() - 4] == "-c" && compile.back() == "/w/funcs_1.o");
    CHECK(has(compile_args(clang, mac, sources, "a.c", "a.o"), "-mmacosx-version-min=13.0"));
    CHECK(!has(compile_args(gcc, linux, sources, "a.c", "a.o"), "-mmacosx-version-min=13.0"));

    CHECK(has(link_args(clang, mac, {"a.o"}, "m.dylib"), "-dynamiclib"));
    CHECK(has(link_args(clang, mac, {"a.o"}, "/tmp/work/sc64-game.dylib"), "-Wl,-install_name,@rpath/sc64-game.dylib"));
    CHECK(!has_prefix(link_args(gcc, linux, {"a.o"}, "m.so"), "-Wl,-install_name"));
    CHECK(has(link_args(gcc, linux, {"a.o", "b.o"}, "m.so"), "-shared"));
    CHECK(has(link_args(gcc, windows, {"a.o"}, "m.dll"), "-static-libgcc"));
    CHECK(has(link_args(gcc, linux, {"a.o"}, "m.so"), "-s") && has(link_args(zig, windows, {"a.o"}, "m.dll"), "-s"));
    CHECK(!has(link_args(clang, mac, {"a.o"}, "m.dylib"), "-s"));
    CHECK(link_args(gcc, linux, {"a.o", "b.o"}, "m.so").back() == "m.so");

    CHECK(zig_target(mac) == "aarch64-macos.13.0");
    CHECK(zig_target(linux) == "x86_64-linux-gnu.2.17");
    CHECK(zig_target(windows) == "x86_64-windows-gnu");
    CHECK(zig.prefix == (std::vector<std::string>{"cc", "-target", "x86_64-linux-gnu.2.17"}));
    CHECK(compile_args(zig, linux, sources, "a.c", "a.o")[0] == "cc");
    CHECK(has(link_args(zig, mac, {"a.o"}, "m.dylib"), "-shared"));  // zig links macOS modules like any other

    const auto candidates = system_candidates(linux, "ccache gcc -m64");
    CHECK(candidates.size() == 4 && candidates[0].program == "ccache" &&
          candidates[0].prefix == (std::vector<std::string>{"gcc", "-m64"}));
    CHECK(candidates[1].program == "cc" && candidates[3].program == "gcc");
    CHECK(system_candidates(windows, "")[0].program == "gcc");
    CHECK(system_candidates(mac, "cc").size() == 3);  // $CC=cc is not probed twice
    // Without the developer tools the xcode-select shims are not run; a real compiler named by $CC still is.
    CHECK(system_candidates(mac, "", false).empty() && system_candidates(mac, "cc", false).empty());
    CHECK(system_candidates(mac, "/opt/homebrew/bin/gcc-14 -m64", false).size() == 1);
    {
        const fs::path directory = fs::temp_directory_path() / "sc64-setup-test-xcode";
        fs::create_directories(directory);
        const auto answers = [&](const std::string& text, int status) {
            return Runner([text, status](const Command& c) { write(c.log, text); return status; });
        };
        CHECK(macos_developer_tools(answers(directory.string() + "\n", 0), directory / "a"));
        CHECK(!macos_developer_tools(answers((directory / "absent").string() + "\n", 0), directory / "b"));
        CHECK(!macos_developer_tools(answers("xcode-select: error\n", 2), directory / "c"));
        CHECK(!macos_developer_tools([](const Command&) -> int { throw std::runtime_error("no xcode-select"); }, directory / "d"));
        fs::remove_all(directory);
    }
    // A non-ASCII path reaches the compiler as the same UTF-8 bytes.
    {
        const auto accented = compile_args(gcc, linux, ModuleSources{path_from_utf8("/r\xc3\xa9s"), "/gen", "x"},
                                           path_from_utf8("/g\xc3\xa9n/f.c"), path_from_utf8("/w\xc3\xa9/f.o"));
        CHECK(has(accented, "-I/r\xc3\xa9s") && has(accented, "/g\xc3\xa9n/f.c") && has(accented, "/w\xc3\xa9/f.o"));
    }
    // The probe builds what the game is made of, shape by shape.
    const std::string full = probe_source(true), small = probe_source(false);
    for (const char* shape : {"#include \"recomp.h\"", "RECOMP_FUNC void", "goto L_80000420;", "switch (jr_addend_80005244 >> 2)",
                              "default: switch_error(__func__", "LOOKUP_FUNC(ctx->r8)(rdram, ctx);", "MEM_W(0X10, ctx->r29)",
                              "CVT_S_W(", ".func = sc64_probe_function", ".relocs = nullptr", "ARRLEN(probe_funcs)",
                              "gpr jr_addend_80005244", "recomp_func_t* get_function(int32_t vram)", "void switch_error(const char* func",
                              "void sc64_guest_poll(uint8_t* rdram) {"})
        check(full.find(shape) != std::string::npos, shape, __LINE__);
    // A hook block directly after a label: the construct that pre-C23 compilers reject when it is a bare declaration.
    CHECK(full.find("L_80000420:\n    { extern void sc64_guest_poll(uint8_t*); sc64_guest_poll(rdram); }") != std::string::npos);
    CHECK(full.find("extern void sc64_guest_poll(uint8_t*);\n") == std::string::npos);  // never bare
    CHECK(small.find("recomp.h") == std::string::npos && small.find("visibility") != std::string::npos);
}


// ---- Android target (cross-built module) ----
void test_android_target() {
    const Platform android = parse_target("android-arm64"), linux{"linux", "x86_64", ".so", ""}, mac{"macos", "arm64", ".dylib", ""};
    CHECK(android.os == "android" && android.arch == "arm64" && android.module_suffix == ".so" && android.exe_suffix.empty());
    CHECK(platform_name(android) == "android-arm64" && target_names() == std::vector<std::string>{"android-arm64"});
    CHECK(same_platform(android, parse_target("android-arm64")) && !same_platform(android, linux));
    CHECK_ERROR(parse_target("amiga-m68k"), "Unknown target amiga-m68k (known: android-arm64)");

    const ModuleSources sources{"/res/module", "/gen", "deadbeef"};
    const Toolchain ndk = ndk_toolchain("/ndk/bin/clang");
    CHECK(ndk.program == fs::path("/ndk/bin/clang") && ndk.prefix == std::vector<std::string>{"--target=aarch64-linux-android28"});
    const auto compile = compile_args(ndk, android, sources, "/gen/funcs_1.c", "/w/funcs_1.o");
    CHECK(compile[0] == "--target=aarch64-linux-android28" && has(compile, "-fPIC") && has(compile, "-fexceptions"));
    CHECK(!has(compile, "-mmacosx-version-min=13.0"));
    const auto link = link_args(ndk, android, {"a.o", "b.o"}, "/w/sc64-game.so");
    CHECK(link[0] == "--target=aarch64-linux-android28" && has(link, "-shared") && has(link, "-s") && link.back() == "/w/sc64-game.so");
    for (const char* flag : {"-Wl,-z,max-page-size=16384", "-Wl,-z,common-page-size=16384", "-Wl,-z,noexecstack", "-Wl,-z,relro", "-Wl,-z,now"})
        CHECK(has(link, flag));
    CHECK(!has(link, "-dynamiclib") && !has_prefix(link, "-Wl,-install_name") && !has(link, "-static-libgcc"));
    CHECK(!has(link_args(Toolchain{"gcc", "gcc", {}, "", {}}, linux, {"a.o"}, "m.so"), "-Wl,-z,max-page-size=16384"));
    // An Android module is compiled with the flags of any other position-independent module.
    CHECK(compile_flags(android) == compile_flags(linux) && compile_flags(android) != compile_flags(Platform{"windows", "x86_64", ".dll", ".exe"}));

    // NDK discovery: explicit roots first, then the newest ndk/<version> of each SDK.
    const fs::path root = fs::temp_directory_path() / ("sc64-setup-test-ndk-" + std::to_string(std::rand()));
    fs::remove_all(root);
    const fs::path sdk = root / "sdk", home = root / "home";
    for (const char* version : {"9.0.1", "29.0.14206865", "27.1.12297006"}) fs::create_directories(sdk / "ndk" / version);
    fs::create_directories(root / "explicit");
    fs::create_directories(home / "Android" / "Sdk" / "ndk" / "26.0.1");
    const auto env = [&](const char* name) -> std::string {
        const std::string key = name;
        if (key == "ANDROID_NDK_ROOT") return (root / "explicit").string();
        if (key == "ANDROID_SDK_ROOT") return sdk.string();
        if (key == "ANDROID_HOME") return (root / "missing").string();
        if (key == "HOME") return home.string();
        return {};
    };
    const auto found = ndk_candidates(linux, env);
    CHECK(found.size() == 5 && found[0] == root / "explicit");
    CHECK(found[1] == sdk / "ndk" / "29.0.14206865" && found[2] == sdk / "ndk" / "27.1.12297006" && found[3] == sdk / "ndk" / "9.0.1");
    CHECK(found[4] == home / "Android" / "Sdk" / "ndk" / "26.0.1");  // the SDK's usual place for the OS
    CHECK(ndk_candidates(linux, [](const char*) { return std::string(); }).empty());
    CHECK(ndk_candidates(mac, [&](const char* name) { return std::string(name) == "HOME" ? home.string() : std::string(); }).empty());
    // A driver is found only where the NDK ships one for this host.
    const fs::path clang = sdk / "ndk" / "29.0.14206865" / "toolchains" / "llvm" / "prebuilt" / "linux-x86_64" / "bin" / "clang";
    CHECK(ndk_clang(sdk / "ndk" / "29.0.14206865", linux).empty());
    write(clang, "");
    CHECK(ndk_clang(sdk / "ndk" / "29.0.14206865", linux) == clang);
    CHECK(ndk_clang(sdk / "ndk" / "29.0.14206865", mac).empty());
    fs::remove_all(root);
}

// ---- the whole flow ----
struct World {
    fs::path root, data, resources, recompiler, rom;
    std::string rom_sha1;
    std::vector<Command> commands;
    std::mutex mutex;
    bool tamper = false, recompiler_exit = false, diagnostics = false, compile_fail = false, no_compiler = false;
    bool zig_download_fails = true;
    bool with_recomp_header = false;
    bool no_developer_tools = false;  // macOS: xcode-select reports nothing installed
    std::string module_marker = "v1";

    std::map<std::string, std::string> generated_files() const {
        return {{"funcs.h", "// funcs\n"},
                {"funcs_0.c", "RECOMP_FUNC void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx) {}\n"},
                {"funcs_1.c", "// second\n"}, {"lookup.cpp", "// lookup\n"},
                {"recomp_overlays.inl", "static SectionTableEntry section_table[] = {};\n"}};
    }
    void make(bool little_endian_rom = false) {
        root = fs::temp_directory_path() / ("sc64-setup-test-" + std::to_string(std::rand()));
        fs::remove_all(root);
        data = root / "data"; resources = root / "res"; recompiler = root / "N64Recomp"; rom = root / "game.z64";
        std::string bytes(sc64::runtime::rom_size, '\x11');
        const unsigned char magic[4] = {0x80, 0x37, 0x12, 0x40};
        for (int i = 0; i < 4; ++i) bytes[i] = char(magic[i]);
        rom_sha1 = sc64::runtime::sha1_hex({reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()});
        if (little_endian_rom)
            for (std::size_t i = 0; i < bytes.size(); i += 4) std::swap(bytes[i], bytes[i + 3]), std::swap(bytes[i + 1], bytes[i + 2]);
        write(rom, bytes);
        write(recompiler, "binary");
        const std::string syms = "[[section]]\n", config = "[input]\nrom_file_path = \"@ROM@\"\noutput_func_path = \"@OUTPUT@\"\n";
        nlohmann::json files = nlohmann::json::object();
        for (const auto& [name, text] : generated_files()) files[name] = sha256(text);
        const nlohmann::json manifest = {{"schema", 1}, {"rom_sha1", rom_sha1}, {"symbols_sha256", sha256(syms)},
                                         {"config_sha256", sha256(config)}, {"files", files}};
        write(resources / "recipe/ntsc-u/sc64.syms.toml", syms);
        write(resources / "recipe/ntsc-u/sc64.toml", config);
        write(resources / "recipe/ntsc-u/generated.json", manifest.dump());
        write(resources / "module/module_main.c", "int main_module;\n");
        if (with_recomp_header) write(resources / "module/recomp.h", "// stand-in\n");
        set_abi(1);
    }
    void set_abi(int abi) { write(resources / "module/sc64_module.h", "#define SC64_MODULE_ABI " + std::to_string(abi) + "\n"); }
    Options options() const {
        Options o;
        o.rom = rom; o.data_dir = data; o.resources = resources; o.recompiler = recompiler;
        o.rom_sha1 = rom_sha1; o.allow_download = false; o.jobs = 2; o.cc_env = "";
        return o;
    }
    static std::string value_after(const std::vector<std::string>& args, const std::string& key) {
        for (std::size_t i = 0; i + 1 < args.size(); ++i) if (args[i] == key) return args[i + 1];
        return {};
    }
    int run(const Command& c) {
        {
            std::lock_guard lock(mutex);
            commands.push_back(c);
        }
        const auto append = [&](const std::string& text) { if (!c.log.empty()) { fs::create_directories(c.log.parent_path()); std::ofstream(c.log, std::ios::app) << text; } };
        if (c.program == recompiler) {
            if (recompiler_exit) return 3;
            const std::string config = [&] { std::ifstream in(c.args[0]); return std::string(std::istreambuf_iterator<char>(in), {}); }();
            const auto at = config.find("output_func_path = \"") + 20;
            const fs::path output = config.substr(at, config.find('"', at) - at);
            for (const auto& [name, text] : generated_files()) write(output / name, tamper && name == "funcs_1.c" ? "// changed\n" : text);
            append(diagnostics ? "Error in function foo\n" : "[Info] Function count: 2\n");
            return 0;
        }
        if (c.program == "curl") return zig_download_fails ? 22 : 0;
        if (c.program == "xcode-select") {
            if (no_developer_tools) return 2;
            append(root.string() + "\n");
            return 0;
        }
        if (no_compiler) throw std::runtime_error("not found");
        const bool compiling = has(c.args, "-c");
        const bool linking = has(c.args, "-shared") || has(c.args, "-dynamiclib");
        if (!compiling && !linking) { append("fakecc version 1.0\n"); return 0; }
        if (compiling) {
            if (compile_fail && value_after(c.args, "-o").find("funcs_1") != std::string::npos) { append("funcs_1.c:1: error: boom\n"); return 1; }
            write(value_after(c.args, "-o"), "object");
        } else {
            write(value_after(c.args, "-o"), "module " + module_marker);
        }
        return 0;
    }
    Runner runner() { return [this](const Command& c) { return run(c); }; }
};

void test_flow() {
    std::vector<std::pair<int, std::string>> steps;
    Reporter report;
    report.step = [&](int n, int total, const std::string& text) { CHECK(total == kSteps); steps.push_back({n, text}); };
    report.info = [](const std::string&) {};
    const Platform host = host_info();

    {   // Success, then up to date, then forced, then a changed ABI.
        World w; w.make();
        Result first = run_setup(w.options(), w.runner(), report);
        CHECK(first.rebuilt && fs::is_regular_file(first.module) && first.module == module_path(w.data, host));
        CHECK(steps.size() == 5 && steps.front().first == 1 && steps.back().first == 5);
        CHECK(first.compiler.find("fakecc") != std::string::npos);
        const auto receipt = nlohmann::json::parse(sc64::setup::read_file(w.data / "game/receipt.json"));
        CHECK(receipt["rom_sha1"] == w.rom_sha1 && receipt["abi"] == 1 && receipt["module_sha256"] == file_sha256(first.module));
        CHECK(fs::exists(first.logs / "recompile.log") && fs::exists(first.logs / "compile.log" ) == false);
        std::size_t work = 0;
        for (const auto& e : fs::directory_iterator(w.data / "setup")) work += e.path().filename().string().starts_with("work-");
        CHECK(work == 0);  // the generated C is not kept
        std::size_t compiles = 0, links = 0;
        for (const auto& c : w.commands) { compiles += has(c.args, "-c"); links += has(c.args, "-shared") || has(c.args, "-dynamiclib"); }
        CHECK(compiles == 3 + 1 && links == 1 + 1);  // funcs_0, funcs_1 + module_main; plus the probe's compile and link

        const std::size_t before = w.commands.size();
        steps.clear();
        Result second = run_setup(w.options(), w.runner(), report);
        CHECK(!second.rebuilt && w.commands.size() == before && steps.size() == 1);

        CHECK(receipt.contains("sources_sha256"));
        // The module is also rebuilt when its sources change although ABI, ROM, recipe and recompiler do not.
        CHECK(module_is_current(w.options()));
        write(w.resources / "module/module_main.c", "int changed_module;\n");
        CHECK(!module_is_current(w.options()));
        CHECK(run_setup(w.options(), w.runner(), report).rebuilt);
        CHECK(module_is_current(w.options()));
        write(w.resources / "module/sc64_imports.def", "// a new import\n");
        CHECK(!module_is_current(w.options()));
        CHECK(run_setup(w.options(), w.runner(), report).rebuilt && module_is_current(w.options()));
        Options absent = w.options(); absent.data_dir = w.root / "nothing-built";
        CHECK(!module_is_current(absent));

        Options forced = w.options(); forced.force = true;
        CHECK(run_setup(forced, w.runner(), report).rebuilt);
        w.set_abi(2);
        CHECK(run_setup(w.options(), w.runner(), report).rebuilt);
        const auto again = nlohmann::json::parse(sc64::setup::read_file(w.data / "game/receipt.json"));
        CHECK(again["abi"] == 2);
        write(first.module, "tampered module");  // a module that no longer matches its receipt is rebuilt
        CHECK(run_setup(w.options(), w.runner(), report).rebuilt);
        fs::remove_all(w.root);
    }
    {   // A data folder given relative to the working directory (--data local/x) works like an absolute one.
        World w; w.make();
        Options relative = w.options();
        relative.data_dir = fs::relative(w.data, fs::current_path());
        CHECK(relative.data_dir.is_relative());
        CHECK(run_setup(relative, w.runner(), report).rebuilt);  // the recompiler wrote where the config said, so the paths were absolute
        CHECK(fs::is_regular_file(module_path(w.data, host)));
        fs::remove_all(w.root);
    }
    {   // Cross-building for Android: the NDK compiles, nothing of the host is probed, the host module is left alone.
        World w; w.make();
        Options android = w.options();
        android.target = parse_target("android-arm64");
        CHECK_ERROR(run_setup(android, w.runner(), report), "Android NDK was not found");
        android.ndk = w.root / "ndk";
        fs::create_directories(*android.ndk);
        CHECK_ERROR(run_setup(android, w.runner(), report), "No clang driver");
        for (const char* tag : {"darwin-x86_64", "linux-x86_64", "windows-x86_64"})
            write(*android.ndk / "toolchains" / "llvm" / "prebuilt" / tag / "bin" / ("clang" + host.exe_suffix), "");
        w.commands.clear();
        const Result built = run_setup(android, w.runner(), report);
        CHECK(built.rebuilt && built.module == w.data / "game-android-arm64" / "sc64-game.so" && fs::is_regular_file(built.module));
        CHECK(!fs::exists(w.data / "game"));
        const auto receipt = nlohmann::json::parse(sc64::setup::read_file(w.data / "game-android-arm64" / "receipt.json"));
        CHECK(receipt["target"] == "android-arm64" && receipt["module_sha256"] == file_sha256(built.module));
        std::size_t ndk_compiles = 0;
        for (const auto& c : w.commands) {
            CHECK(c.program == w.recompiler || c.program.filename() == "clang" + host.exe_suffix);
            if (c.program != w.recompiler) CHECK(!c.args.empty() && c.args[0] == "--target=aarch64-linux-android28");
            ndk_compiles += has(c.args, "-c");
        }
        CHECK(ndk_compiles == 3 + 1);
        CHECK(module_is_current(android) && !module_is_current(w.options()));  // the host module is a different thing
        const std::size_t before = w.commands.size();
        CHECK(!run_setup(android, w.runner(), report).rebuilt && w.commands.size() == before);
        // A host build next to it does not disturb the Android one, and an old receipt without a target never matches.
        CHECK(run_setup(w.options(), w.runner(), report).rebuilt && module_is_current(android));
        auto stale = nlohmann::json::parse(sc64::setup::read_file(w.data / "game-android-arm64" / "receipt.json"));
        stale.erase("target");
        write(w.data / "game-android-arm64" / "receipt.json", stale.dump());
        CHECK(!module_is_current(android));
        // --compiler still wins over the NDK, and is probed for the target.
        Options given = android; given.force = true; given.compiler = w.root / "some-clang";
        w.commands.clear();
        CHECK(run_setup(given, w.runner(), report).rebuilt);
        for (const auto& c : w.commands) CHECK(c.program == w.recompiler || c.program == given.compiler);
        fs::remove_all(w.root);
    }
    {   // macOS without the command line tools must not run the cc/clang/gcc shims (they open an installer dialog).
        World w; w.make(); w.no_developer_tools = true;
        if (host.os == "macos") {
            CHECK_ERROR(run_setup(w.options(), w.runner(), report), "No usable C compiler");
            for (const auto& c : w.commands) CHECK(c.program != "cc" && c.program != "clang" && c.program != "gcc");
        } else {
            CHECK(run_setup(w.options(), w.runner(), report).rebuilt);  // elsewhere xcode-select is never asked
            for (const auto& c : w.commands) CHECK(c.program != "xcode-select");
        }
        fs::remove_all(w.root);
    }
    {   // The probe is the real-shape one only when recomp.h is beside the module sources.
        for (const bool header : {false, true}) {
            World w; w.with_recomp_header = header; w.make();
            const Result built = run_setup(w.options(), w.runner(), report);
            std::string probe;
            for (const auto& e : fs::recursive_directory_iterator(built.logs))
                if (e.path().filename() == "probe.c" && e.path().parent_path().filename() == "probe-0") probe = sc64::setup::read_file(e.path());
            CHECK(probe == probe_source(header));
            fs::remove_all(w.root);
        }
    }
    {   // The recompiler and its output are verified before anything is compiled.
        World w; w.make(); w.tamper = true;
        CHECK_ERROR(run_setup(w.options(), w.runner(), report), "does not match the recipe");
        CHECK(!fs::exists(module_path(w.data, host)));
        World d; d.make(); d.diagnostics = true;
        CHECK_ERROR(run_setup(d.options(), d.runner(), report), "diagnostic rejected");
        World x; x.make(); x.recompiler_exit = true;
        CHECK_ERROR(run_setup(x.options(), x.runner(), report), "N64Recomp failed (exit 3)");
        for (World* world : {&w, &d, &x}) {
            for (const auto& c : world->commands) CHECK(!has(c.args, "-shared") || c.args.back().find("probe") != std::string::npos);
            fs::remove_all(world->root);
        }
    }
    {   // A failed compile leaves an existing module alone and names the log.
        World w; w.make();
        run_setup(w.options(), w.runner(), report);
        const std::string old = sc64::setup::read_file(module_path(w.data, host));
        w.module_marker = "v2"; w.compile_fail = true;
        Options forced = w.options(); forced.force = true;
        CHECK_ERROR(run_setup(forced, w.runner(), report), "Compiling funcs_1.c failed");
        CHECK(sc64::setup::read_file(module_path(w.data, host)) == old);
        fs::remove_all(w.root);
    }
    {   // No compiler: clear message, zig download failure keeps the manual hint.
        World w; w.make(); w.no_compiler = true;
        CHECK_ERROR(run_setup(w.options(), w.runner(), report), "No usable C compiler");
        Options online = w.options(); online.allow_download = true;
        CHECK_ERROR(run_setup(online, w.runner(), report), "Download of zig failed");
        Options given = w.options(); given.compiler = w.root / "missing-cc";
        CHECK_ERROR(run_setup(given, w.runner(), report), "cannot build the game module");
        // Diagnostics named in errors must outlive the work directory.
        bool probe_logs = false;
        for (const auto& e : fs::recursive_directory_iterator(w.data / "setup/logs")) probe_logs |= e.path().filename().string().starts_with("probe-");
        CHECK(probe_logs);
        fs::remove_all(w.root);
    }
    {   // ROM checks.
        World w; w.make();
        Options wrong = w.options(); wrong.rom_sha1 = std::string(40, '0');
        CHECK_ERROR(run_setup(wrong, w.runner(), report), "NTSC-U");
        Options missing = w.options(); missing.rom = w.root / "nope.z64";
        CHECK_ERROR(run_setup(missing, w.runner(), report), "Cannot read the ROM");
        write(w.root / "short.z64", "short");
        Options truncated = w.options(); truncated.rom = w.root / "short.z64";
        CHECK_ERROR(run_setup(truncated, w.runner(), report), "32 MiB");
        Options no_recompiler = w.options(); no_recompiler.recompiler = w.root / "absent";
        CHECK_ERROR(run_setup(no_recompiler, w.runner(), report), "N64Recomp not found");
        fs::remove_all(w.root);
    }
    {   // A z64 ROM is used where it is; another byte order is normalized into the work directory.
        World w; w.make();
        run_setup(w.options(), w.runner(), report);
        std::string config_rom;
        for (const auto& c : w.commands) if (c.program == w.recompiler) config_rom = c.args[0];
        CHECK(!config_rom.empty());
        World v; v.make(true);
        run_setup(v.options(), v.runner(), report);
        CHECK(v.rom_sha1 == w.rom_sha1);
        fs::remove_all(w.root); fs::remove_all(v.root);
    }
}

void test_zig_pins() {
    const Platform hosts[] = {{"macos", "arm64", ".dylib", ""}, {"linux", "x86_64", ".so", ""}, {"windows", "x86_64", ".dll", ".exe"}};
    for (const auto& host : hosts) {
        const auto pin = zig_pin(host);
        CHECK(pin.has_value());
        if (!pin) continue;
        CHECK(pin->url.starts_with("https://") && pin->sha256.size() == 64 && !pin->directory.empty());
        CHECK(pin->url.find(zig_version()) != std::string_view::npos);
    }
}
}

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--child") {
        const std::string mode = argv[2];
        if (mode == "env") { const char* v = std::getenv("SC64_TEST_ENV"); return v && std::string(v) == "x y" ? 0 : 4; }
        if (mode == "print") { std::fputs("child stdout\n", stdout); std::fflush(stdout); std::fputs("child stderr\n", stderr); return 0; }
        if (mode == "sleep") { std::this_thread::sleep_for(std::chrono::seconds(60)); return 0; }
        return 3;
    }
    argv0 = argv[0];
    test_process();
    test_locate();
    test_recipe();
    test_toolchain();
    test_android_target();
    test_flow();
    test_zig_pins();
    std::puts(failures ? "setup FAILED" : "setup OK");
    return failures ? 1 : 0;
}
