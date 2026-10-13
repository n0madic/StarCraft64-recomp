#include "session.hpp"
#include "audio.hpp"
#include "clock_bridge.hpp"
#include "rdp_writeback.hpp"
#include "flash_device.hpp"
#include "guest_fpu.hpp"
#include "../runtime/guest_poll.hpp"
#include "../runtime/rom_image.hpp"
#include "../runtime/minimap.hpp"
#include <librecomp/game.hpp>
#include <librecomp/overlays.hpp>
#include <librecomp/rsp.hpp>
#include <xxHash/xxh3.h>
#include "../runtime/sha.hpp"
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <iomanip>
#include <stdexcept>

#include "game_module.hpp"

namespace sc64::desktop {
namespace {
std::mutex pending_mutex;
std::atomic_flag preparing_task = ATOMIC_FLAG_INIT;
struct SubmissionGuard {
    SubmissionGuard() {
        if (preparing_task.test_and_set()) throw std::runtime_error("Overlapping guest graphics submission");
    }
    ~SubmissionGuard() { preparing_task.clear(); }
};
std::shared_ptr<const gfx::DrawList> pending;
std::shared_ptr<const RenderedFrame> pending_rendered;
sc64::runtime::FrontendFeedback pending_ui;
std::string sha1(std::span<const std::uint8_t> bytes) { return runtime::sha1_hex(bytes); }
class Renderer final : public ultramodern::renderer::RendererContext {
public:
    Renderer() {
        setup_result = ultramodern::renderer::SetupResult::Success; chosen_api = ultramodern::renderer::GraphicsApi::Auto;
    }
    std::uint64_t completed_task_=0;
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return true; }
    void enable_instant_present() override {}
    void send_dl(const OSTask*) override {
        std::lock_guard lock(pending_mutex);
        if (pending) {
            completed_task_=pending->identity.task;
            session().mailbox.publish(std::exchange(pending, {}), std::move(pending_ui),
                                     std::exchange(pending_rendered, {}));
        }
    }
    void send_dummy_workload(std::uint32_t) override {}
    void update_screen() override {
        const auto& v=*ultramodern::renderer::get_vi_regs();
        session().scanout.publish({v.VI_STATUS_REG,v.VI_ORIGIN_REG,v.VI_WIDTH_REG,v.VI_INTR_REG,
            v.VI_V_CURRENT_LINE_REG,v.VI_TIMING_REG,v.VI_V_SYNC_REG,v.VI_H_SYNC_REG,
            v.VI_LEAP_REG,v.VI_H_START_REG,v.VI_V_START_REG,v.VI_V_BURST_REG,
            v.VI_X_SCALE_REG,v.VI_Y_SCALE_REG},session().fields.load(),completed_task_);
    }
    void shutdown() override {}
    std::uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};
}
Session& session() { static Session value; return value; }
// Orderly shutdown belongs to the extension finalize callbacks (stop()).
// Static destruction can run from std::exit() on a guest thread, where
// quit()/join() would wait for that same thread, and other translation units'
// runtime statics have no ordering guarantee here. Only release our handle.
Session::~Session() { if (worker_.joinable()) worker_.detach(); }
namespace {
// Picker imports survive cold starts. Packaged ROMs (APK assets, AppImage) already have persistent
// storage, so that path never creates a second ROM file.
void persist_rom(const std::vector<std::uint8_t>& rom, const std::filesystem::path& storage) {
    const auto temporary = storage / ".sc64-rom.tmp";
    std::ofstream output(temporary, std::ios::binary);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(reinterpret_cast<const char*>(rom.data()), rom.size());
    output.close();
    std::filesystem::rename(temporary, storage / "sc64-ntsc-u.z64");
}
}
void Session::load(const std::filesystem::path& path, const std::filesystem::path& storage) {
    if (launched_) throw std::runtime_error("Runtime supports one session per process; restart to change ROM");
    std::vector<std::uint8_t> rom(runtime::rom_size);
    {
        // Close the source before persisting: the path may be the stored copy
        // itself, and Windows cannot replace a file that is still open.
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file || file.tellg() != std::streamoff(runtime::rom_size)) throw std::runtime_error("Expected a 32 MiB ROM dump");
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(rom.data()), rom.size())) throw std::runtime_error("ROM read failed");
    }
    launch_rom(std::move(rom), storage, true);
}
void Session::load_bundled(std::vector<std::uint8_t> rom, const std::filesystem::path& storage) {
    launch_rom(std::move(rom), storage, false);
}
void Session::launch_rom(std::vector<std::uint8_t> rom, const std::filesystem::path& storage, bool persist, bool may_build) {
    if (launched_) throw std::runtime_error("Runtime supports one session per process; restart to change ROM");
    runtime::validate_rom(rom, sha1);
#if defined(SC64_HAS_SETUP)
    // A launcher package builds the module in the game folder; one left by an older version of the
    // application (its receipt no longer matches) is replaced the same way. A module chosen explicitly
    // with $SC64_GAME_MODULE is the developer's own and is never rebuilt.
    const char* explicit_module = std::getenv("SC64_GAME_MODULE");
    const bool buildable = may_build && !pending_ && GameBuild::available();
    const bool outdated = buildable && !(explicit_module && *explicit_module) && !GameBuild::current(storage);
#else
    constexpr bool buildable = false, outdated = false;
#endif
    try {
        if (outdated) throw game::ModuleError("The game module does not match this version of the application");
        game::load(storage);  // fails before any side effect when the game module is missing or mismatched
    } catch (const game::ModuleError&) {
#if defined(SC64_HAS_SETUP)
        // No usable module yet: build it from this ROM in the background and start once it exists.
        if (buildable) {
            pending_ = PendingLaunch{storage, persist};
            build_.start(std::move(rom), storage);
            return;
        }
#endif
        // The ROM was validated: keep it, so that a module pushed to the device later starts without choosing it again.
        if (persist) {
            try {
                std::filesystem::create_directories(storage);
                persist_rom(rom, storage);
            } catch (const std::exception&) {}
        }
        throw;
    }
    std::filesystem::create_directories(storage);
    // Validate every user-owned input before process-wide side effects. The
    // save image may be malformed; failing here leaves no audio device open.
    runtime::flash_device().initialize(storage / "sc64.fla");
    if (persist) persist_rom(rom, storage);
    audio::initialize();
    try {
        start_runtime(std::move(rom), storage);
    } catch (...) {
        // Registration is assignment/insert-only and idempotent for this
        // single validated game; only the audio device needs rollback.
        running = false; rom_preloaded = false;
        audio::shutdown();
        throw;
    }
}
std::optional<std::string> Session::poll_build() {
#if defined(SC64_HAS_SETUP)
    if (!pending_) return std::nullopt;
    const auto state = build_.poll();
    if (!state) return std::nullopt;
    if (state->running) return state->text;
    auto pending = std::move(*pending_);
    pending_.reset();
    if (state->failed) return state->text;
    try {
        launch_rom(std::move(state->rom), pending.storage, pending.persist, false);
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
    return std::string();
#else
    return std::nullopt;
#endif
}
void Session::start_runtime(std::vector<std::uint8_t> rom, const std::filesystem::path& storage) {
    raster = std::make_unique<gfx::RasterPool>(gfx::RasterPool::default_helpers());
    recomp::register_config_path(storage);
    const auto& module = game::info();
    recomp::overlays::register_overlays({module.sections, module.num_sections, module.num_sections}, {nullptr, 0});
    recomp::GameEntry game{};
    game.rom_hash = XXH3_64bits(rom.data(), rom.size());
    game.internal_name = "STARCRAFT64"; game.display_name = "StarCraft 64 NTSC-U";
    game.game_id = u8"sc64-ntsc-u"; game.is_enabled = true; game.save_type = recomp::SaveType::Flashram;
    game.entrypoint_address = std::int64_t(std::int32_t(module.entrypoint_vram)); game.entrypoint = module.entrypoint;
    game.on_init_callback = [](std::uint8_t* rdram, recomp_context*) {
        runtime::initialize_guest_fpu();
        MEM_W(0, std::int64_t(std::int32_t(0x80000314))) = 1;
    };
    game.thread_create_callback = [](std::uint8_t*, recomp_context*) { runtime::initialize_guest_fpu(); };
    runtime::minimap_input().clear();
    runtime::set_minimap_handler([](std::uint8_t* ram, void* context, const runtime::MinimapTarget& target) {
        auto& s=session();
        const auto field=s.fields.load();
        if (s.paused || !s.running || field<target.field || field-target.field>12) return;
        // Use the original setter on its own guest thread and guest stack.
        // Preserve the interrupted input query's complete register context.
        auto* ctx=static_cast<recomp_context*>(context);
        const auto saved=*ctx;
        ctx->r4=target.x; ctx->r5=target.y;
        game::info().minimap_set_position(ram,ctx);
        *ctx=saved;
        s.minimap_field=field;
        ++s.minimap_jumps;
    });
    recomp::register_game(game);
    recomp::Configuration config{};
    config.project_version = {0, 2, 0};
#if defined(__APPLE__)
    config.window_handle = {reinterpret_cast<void*>(1), nullptr};
#elif defined(_WIN32)
    config.window_handle = {reinterpret_cast<HWND>(1), 0};
#else
    config.window_handle = reinterpret_cast<SDL_Window*>(1);
#endif
    config.renderer_callbacks.create_render_context = [](std::uint8_t*, ultramodern::renderer::WindowHandle, bool) -> std::unique_ptr<ultramodern::renderer::RendererContext> {
        return std::make_unique<Renderer>();
    };
    config.rsp_callbacks.get_rsp_microcode = [](const OSTask*) -> RspUcodeFunc* {
        throw std::runtime_error("Unresearched non-graphics RSP task (D-020)");
    };
    config.audio_callbacks = {[](std::int16_t* data, std::size_t count) { audio::queue(data, count); }, audio::remaining, audio::frequency};
    config.input_callbacks.get_input = [](int player, std::uint16_t* buttons, float* x, float* y) {
        if (player < 0 || player >= 2) return false;
        auto& pad = session().pads[player];
        *buttons = std::uint16_t(pad.buttons.load()); *x = pad.x.load(); *y = pad.y.load(); return true;
    };
    config.input_callbacks.get_connected_device_info = [](int player) {
        using namespace ultramodern::input;
        return connected_device_info_t{player >= 0 && player < 2 ? Device::Controller : Device::None,
                                      player >= 0 && player < 2 ? Pak::RumblePak : Pak::None};
    };
    config.input_callbacks.set_rumble = [](int player, bool on) {
        if (player != 0) return;
        if (on && !session().rumble.exchange(true)) ++session().rumble_starts;
        if (!on) session().rumble = false;
    };
    config.events_callbacks.vi_callback = [] { ++session().fields; };
    config.error_handling_callbacks.message_box = [](const char* text) { session().fail(text); };
    recomp::set_rom_contents(std::move(rom));
    rom_preloaded = true;
    started = clock.now();
    running = true;
    recomp::start_game(game.game_id, "");
    worker_ = std::thread([this, config] {
        try { recomp::start(config); }
        catch (const ultramodern::thread_terminated&) {} // cooperative shutdown
        catch (const std::exception& error) { fail(error.what()); }
        running = false;
    });
    launched_ = true;
}
void Session::pause(bool value) {
    std::lock_guard lock(pause_mutex_);
    pause_sources_.requested = value;
    requested_paused = value;
    apply_pause_locked();
}
void Session::suspend(bool value) {
    std::lock_guard lock(pause_mutex_);
    pause_sources_.suspended = value;
    apply_pause_locked();
}
void Session::apply_pause_locked() {
    const bool value = pause_sources_.paused();
    if (value) {
        for (auto& pad : pads) { pad.buttons = 0; pad.x = 0; pad.y = 0; }
        runtime::minimap_input().clear();
    }
    if (value != paused.load()) audio::pause_changed();
    paused = value;
    clock.set(value);
}
void Session::stop() {
    clock.stop();
    if (worker_.joinable()) { ultramodern::quit(); worker_.join(); }
    raster.reset();
    audio::shutdown();
    running = false;
}
void Session::fail(const std::string& message) {
    error_.record(message);
    clock.stop();
    ultramodern::quit();
}
std::string Session::error() { return error_.get(); }
}

bool sc64_rom_is_preloaded(const std::u8string& game_id) {
    return game_id == u8"sc64-ntsc-u" && sc64::desktop::session().rom_preloaded.load()
        && recomp::is_rom_loaded();
}
std::chrono::high_resolution_clock::time_point sc64_runtime_now() { return sc64::desktop::session().clock.now(); }
void sc64_pause_point() { sc64::desktop::session().clock.wait(); }
extern "C" void sc64_capture_submit(std::uint8_t* rdram, const OSTask* task) {
    using namespace sc64::desktop;
    sc64::runtime::report_host_failure<ultramodern::thread_terminated>([&] {
        sc64_pause_point();
        SubmissionGuard submission;
        const auto t = task->t;
        if (t.type != M_GFXTASK || t.ucode_size != 4096) throw std::runtime_error("Unknown graphics task");
        const auto snapshot_start=std::chrono::steady_clock::now();
        std::vector<std::uint8_t> canonical(0x800000);
        for (std::size_t i = 0; i < canonical.size(); ++i) canonical[i] = rdram[i ^ 3];
        unsigned ucode = t.ucode & 0x1fffffff;
        if (ucode > canonical.size() - 4096 || sha1({canonical.data() + ucode, 4096}) != "7d0f686719afde0703d377ccc7a492318c01eec0")
            throw std::runtime_error("Graphics microcode identity mismatch");
        const auto interpret_start=std::chrono::steady_clock::now();
        session().capture_snapshot_ms=std::chrono::duration<double,std::milli>(interpret_start-snapshot_start).count();
        auto last_poll=interpret_start;
        auto service_guest=[&] {
            const auto now=std::chrono::steady_clock::now();
            if(now-last_poll<std::chrono::milliseconds(1))return;
            // The live RAM snapshot is complete. Parsing/texture decoding and
            // rasterization touch owned bytes; service higher guest priorities
            // throughout both stages, with no publication mutex held.
            ultramodern::wait_for_external_message_timed(rdram,0);
            ultramodern::check_running_queue(rdram);
            last_poll=std::chrono::steady_clock::now();
        };
        auto list = std::make_shared<sc64::gfx::DrawList>(sc64::gfx::interpret_gbi(canonical, t.data_ptr & 0x1fffffff,service_guest));
        session().capture_interpret_ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-interpret_start).count();
        list->identity.task = ++session().tasks; list->identity.field = session().fields;
        for (const auto& rect:list->rectangles)
            session().cpu_scanouts.targeted(rect.target.address,list->identity.task);
        { std::lock_guard lock(pending_mutex);
          if (pending) throw std::runtime_error("Unexpected overlapping graphics tasks"); }
        // Rasterize the owned snapshot, then resume guest RAM ownership for
        // byte-granular RDP stores. SP/DP signal only after taking this packet.
        // Dropping presentation packets must never drop RDP memory effects.
        auto rendered=std::make_shared<RenderedFrame>();
        const auto start=std::chrono::steady_clock::now();
        std::chrono::steady_clock::duration serviced{};
        rendered->pixels=sc64::gfx::render_software(*list,[&] {
            const auto before=std::chrono::steady_clock::now();
            service_guest();
            serviced+=std::chrono::steady_clock::now()-before;
        },session().raster.get());
        rendered->software_ms=std::chrono::duration<double,std::milli>(
            std::chrono::steady_clock::now()-start).count();
        rendered->software_service_ms=std::chrono::duration<double,std::milli>(serviced).count();
        commit_rdp_writes(rendered->pixels,{rdram,canonical.size()});
        std::lock_guard lock(pending_mutex);
        pending_rendered=std::move(rendered);
        pending = std::move(list);
        pending_ui = sc64::runtime::take_frontend_feedback();
    }, [](const char* error) { session().fail(error); });
}
// osViSwapBuffer on the guest thread: the guest declares this framebuffer
// complete. Own its bytes only when no RDP task drew it since its last swap.
extern "C" void sc64_vi_swap(std::uint8_t* rdram, std::int32_t pointer) {
    using namespace sc64::desktop;
    sc64::runtime::report_host_failure<ultramodern::thread_terminated>([&] {
        auto& s=session();
        const std::uint32_t address=std::uint32_t(pointer)&0x1fffffff;
        if (address>=0x800000) return;
        const auto submitted=s.tasks.load();
        if (!s.cpu_scanouts.swapped(address,submitted)) return;
        const auto vi=s.scanout.latest();
        const sc64::gfx::Image image{address,vi.regs[2]&0xfff,0,vi.regs[0]&3};
        const unsigned height=vi_region(vi,image.width,1024).height;
        // Only the RGBA16 modes the software target export supports.
        if (vi.blank() || image.size!=2 || !image.width || image.width>1024
            || !(vi.regs[13]&0xfff) || !target_in_ram(image,height,0x800000)) return;
        auto cpu=std::make_shared<CpuScanout>();
        cpu->image=image; cpu->height=height; cpu->after_task=submitted;
        cpu->canonical.resize(std::size_t(image.width)*2*height);
        for (std::size_t i=0;i<cpu->canonical.size();++i) cpu->canonical[i]=rdram[(address+i)^3];
        s.cpu_scanouts.publish(std::move(cpu));
        ++s.cpu_swaps;
    }, [](const char* error) { session().fail(error); });
}
extern "C" void sc64_idle_poll(std::uint8_t* rdram) {
    sc64_pause_point();
    sc64::runtime::idle_boundary(rdram);
}
extern "C" void sc64_guest_poll(std::uint8_t* rdram) {
    sc64_pause_point();
    sc64::runtime::busy_wait_boundary(rdram);
}
extern "C" std::uint32_t sc64_ai_status(std::uint8_t* rdram) {
    sc64_pause_point();
    return sc64::runtime::ai_status_boundary(rdram, sc64::desktop::audio::status);
}
extern "C" void osViGetCurrentLine_recomp(std::uint8_t* rdram, recomp_context* ctx) {
    sc64_guest_poll(rdram);
    auto& s = sc64::desktop::session();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(s.clock.now() - s.started).count();
    ctx->r2 = sc64::runtime::vi_current_line(elapsed, s.fields.load());
}
