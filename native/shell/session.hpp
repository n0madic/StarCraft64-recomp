#pragma once
#include "gfx.hpp"
#include "scanout.hpp"
#include "pause_policy.hpp"
#include "../runtime/ui_feedback.hpp"
#include "../runtime/guest_errors.hpp"
#if defined(SC64_HAS_SETUP)
#include "game_build.hpp"
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace sc64::desktop {
struct RenderedFrame {
    gfx::SoftwareResult pixels;
    // Wall time, and the part of it spent servicing other guest threads.
    double software_ms=0, software_service_ms=0;
};
// Three owned slots; the consumer holds a shared packet while the producer can
// replace an obsolete slot. No pointer into guest memory crosses this boundary.
class FrameMailbox {
public:
    void publish(std::shared_ptr<const gfx::DrawList> frame, runtime::FrontendFeedback ui = {},
                 std::shared_ptr<const RenderedFrame> rendered = {}) {
        std::lock_guard lock(mutex_);
        slots_[next_] = std::move(frame);
        ui_[next_] = std::move(ui);
        rendered_[next_] = std::move(rendered);
        latest_ = next_;
        next_ = (next_ + 1) % slots_.size();
        fresh_ = true;
    }
    std::shared_ptr<const gfx::DrawList> take(runtime::FrontendFeedback* ui = nullptr,
                                            std::shared_ptr<const RenderedFrame>* rendered = nullptr) {
        std::lock_guard lock(mutex_);
        if (!fresh_) return {};
        fresh_ = false;
        if (ui) *ui = ui_[latest_];
        if (rendered) *rendered = rendered_[latest_];
        return slots_[latest_];
    }
private:
    std::mutex mutex_;
    std::array<std::shared_ptr<const gfx::DrawList>, 3> slots_{};
    std::array<runtime::FrontendFeedback, 3> ui_{};
    std::array<std::shared_ptr<const RenderedFrame>, 3> rendered_{};
    std::size_t latest_ = 0, next_ = 0;
    bool fresh_ = false;
};

class PauseClock {
public:
    using Clock = std::chrono::high_resolution_clock;
    Clock::time_point now() {
        std::lock_guard lock(mutex_);
        return (paused_ ? pause_start_ : Clock::now()) - offset_;
    }
    void set(bool paused) {
        std::lock_guard lock(mutex_);
        if (paused == paused_) return;
        if (paused) pause_start_ = Clock::now();
        else offset_ += Clock::now() - pause_start_;
        paused_ = paused;
        cv_.notify_all();
    }
    void wait() {
        std::unique_lock lock(mutex_);
        cv_.wait(lock, [&] { return !paused_ || stopping_; });
    }
    void stop() {
        std::lock_guard lock(mutex_);
        stopping_ = true;
        cv_.notify_all();
    }
private:
    std::mutex mutex_;
    std::condition_variable cv_;
    Clock::duration offset_{};
    Clock::time_point pause_start_{};
    bool paused_ = false, stopping_ = false;
};

struct Pad { std::atomic<unsigned> buttons{0}; std::atomic<float> x{0}, y{0}; };
class Session {
public:
    ~Session();
    void load(const std::filesystem::path& rom, const std::filesystem::path& storage);
    void load_bundled(std::vector<std::uint8_t> rom, const std::filesystem::path& storage);
    void pause(bool value);
    void suspend(bool value);
    void stop();
    void fail(const std::string& message);
    std::string error();
    // While the game module is being built from the ROM (ADR-087): the progress or error text, or that the game
    // has been started; nothing when no build was requested. Call from the main thread.
    struct BuildProgress {
        enum class State { running, started, failed } state = State::running;
        std::string text;
    };
    std::optional<BuildProgress> poll_build();
    FrameMailbox mailbox;
    ScanoutMailbox scanout;
    CpuScanouts cpu_scanouts;
    PauseClock clock;
    std::array<Pad, 2> pads;
    std::atomic<std::uint64_t> fields{0}, tasks{0}, samples{0};
    std::atomic<std::uint64_t> cpu_swaps{0};
    std::atomic<double> capture_snapshot_ms{0}, capture_interpret_ms{0};
    std::atomic<std::uint64_t> minimap_jumps{0}, minimap_field{0};
    std::atomic<bool> running{false}, paused{false}, requested_paused{false};
    std::atomic<bool> rom_preloaded{false};
    // Player 1 Rumble Pak motor state requested by the guest (osMotorStart/Stop).
    std::atomic<bool> rumble{false};
    std::atomic<std::uint64_t> rumble_starts{0};
    PauseClock::Clock::time_point started{};
    // Created before guest startup, destroyed in stop() after guest threads join.
    std::unique_ptr<gfx::RasterPool> raster;
private:
    std::thread worker_;
    runtime::FirstError error_;
    std::mutex pause_mutex_;
    PauseSources pause_sources_;
    void apply_pause_locked();
    // `may_build` allows starting the first-run game build when the module is missing or out of date; the launch
    // that follows a finished build passes false, so a module that still cannot be loaded is an error, not a rebuild.
    void launch_rom(std::vector<std::uint8_t> rom, const std::filesystem::path& storage, bool persist, bool may_build = true);
    void start_runtime(std::vector<std::uint8_t> rom, const std::filesystem::path& storage);
    bool launched_ = false;
#if defined(SC64_HAS_SETUP)
    struct PendingLaunch { std::filesystem::path storage; bool persist; };  // the ROM comes back with the finished build
    GameBuild build_;
    std::optional<PendingLaunch> pending_;
#endif
};
Session& session();
}
