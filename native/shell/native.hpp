#pragma once
#include "scripts.hpp"
#include "session.hpp"
#include "cursor_feedback.hpp"
#include "ui_feedback.hpp"
#include "mouse_cursor.hpp"
#include "pause_policy.hpp"
#include "rumble_motor.hpp"
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// Native session bridge for the shell main thread: presentation selection,
// lifecycle pause causes, cursor/rumble policy and the script API. It owns no
// graphics objects; video.cpp uploads the images it selects.
namespace sc64::shell {
struct Readback {
    unsigned width = 0, height = 0;
    std::vector<std::uint8_t> bgra; // top-left origin, 4 bytes per pixel
};

class Native final : public NativeApi {
public:
    // Hooks into the window layer, installed by the application.
    struct Window {
        std::function<std::pair<double, double>()> size;   // pixels
        std::function<void(int)> exit;
        std::function<std::string()> renderer;              // SDL renderer name
    };
    Native(const Config& config, Window window);
    // Validates configuration and opens diagnostic outputs; throws on misuse.
    void initialize();
    // One presentation step. Returns the image to upload when it changed.
    struct Upload { std::shared_ptr<const std::vector<std::uint8_t>> rgba; unsigned width = 0, height = 0; };
    std::optional<Upload> tick();
    // Called after drawing and before presenting; may read the window back.
    void after_render(const std::function<Readback()>& readback);
    bool visible() const { return scanout_visible_; }
    unsigned width() const { return width_; }
    unsigned height() const { return height_; }
    bool probing() const { return !probe_cases_.empty(); }
    void lifecycle(WindowEvent event);
    // Android activity pause/resume: an independent suspension cause.
    void suspend(bool suspended);
    void finalize();
    std::string storage() const { return storage_; }

    input::Info info() override;
    nlohmann::json telemetry() override;
    nlohmann::json take_audio_timing() override;
    std::optional<std::string> load_rom(const std::string& path) override;
    std::optional<std::string> load_bundled_rom() override;
    std::optional<std::string> choose_rom() override;
    std::optional<std::string> choose_module() override;
    std::optional<ImportEvent> poll_import() override;
    std::optional<std::string> stored_rom() override;
    LauncherFacts launcher_facts() override;
    std::optional<LauncherButton::Action> choosing() override;
    void pause(bool paused) override;
    void set_pad(int player, unsigned buttons, double x, double y) override;
    bool navigate_minimap(double x, double y, double context, double field) override;
    void set_cursor_hidden(const std::optional<std::array<double, 4>>& region) override;
    void vibrate(int milliseconds, int amplitude) override;
    void set_haptics(bool enabled) override;
    void exit(int code) override;
    std::pair<double, double> window_size() override;
    void log(const std::string& line) override;
    void warn(const std::string& line) override;
    bool tracing() override;

private:
    void apply_pause();
    void update_cursor();
    void update_rumble();
    void restore_cursor();
    void reset_feedback();
    void load_probe_case();
    // The module a user picked, installed; then the stored ROM starts the game when there is one.
    ImportEvent import_module(const std::string& path);
    const Config& config_;
    Window window_;
    std::string storage_;
    // Looking at the disk every frame is wasteful: the facts are read once and again after anything was imported.
    std::optional<LauncherFacts> facts_;
    bool module_failed_ = false;  // the last start failed because of the game module
    unsigned width_ = 0, height_ = 0;
    std::uint64_t presented_ = 0, upload_bytes_ = 0;
    double software_ms_ = 0, software_service_ms_ = 0;
    bool user_paused_ = false, inactive_ = false, iconified_ = false;
    bool haptics_enabled_ = true;
    desktop::RumbleMotor rumble_motor_;
    desktop::PausePolicy pause_policy_;
    bool background_input_probe_ = false;
    bool cursor_requested_ = false, system_cursor_hidden_ = false;
    Sc64CursorRegion cursor_region_;
    desktop::CursorFeedback cursor_;
    desktop::UiFeedback ui_;
    std::chrono::steady_clock::time_point host_started_ = std::chrono::steady_clock::now();
    nlohmann::json probe_cases_;
    std::filesystem::path probe_out_;
    unsigned probe_case_ = 0, probe_age_ = 0;
    bool probe_loaded_ = false, probe_uploaded_ = false;
    std::string probe_ram_sha256_;
    desktop::ScanoutImages scanout_images_;
    desktop::Scanout scanout_;
    std::shared_ptr<const desktop::ScanoutImage> uploaded_image_;
    // Last converted CPU framebuffer, so an unchanged swap is not re-uploaded.
    std::shared_ptr<const desktop::CpuScanout> cpu_source_;
    std::shared_ptr<const desktop::ScanoutImage> cpu_image_;
    desktop::ViRegion presented_region_;
    bool scanout_visible_ = false;
    unsigned trace_readbacks_ = 0, trace_readback_tail_ = 0;
    bool trace_last_visible_ = false;
    unsigned trace_last_width_ = 0;
    std::filesystem::path trace_out_;
    std::ofstream trace_log_;
    std::uint64_t trace_from_ = 0, trace_to_ = 0;
};
}
