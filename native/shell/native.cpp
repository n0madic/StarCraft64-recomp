#include "native.hpp"
#include "game_module.hpp"
#include "audio.hpp"
#include "log.hpp"
#include "platform.hpp"
#include "sha.hpp"
#include "../runtime/minimap.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace sc64::shell {
namespace {
using desktop::session;
bool inside_private(const std::filesystem::path& path) {
    const auto relative = path.lexically_relative(std::filesystem::weakly_canonical(SC64_PRIVATE_ROOT));
    return !relative.empty() && *relative.begin() != "..";
}
std::uint64_t config_integer(const Config& config, const char* key) {
    const double value = config.number(key, 0);
    return value > 0 ? std::uint64_t(value) : 0;
}
}

Native::Native(const Config& config, Window window) : config_(config), window_(std::move(window)) {}

void Native::initialize() {
    storage_ = config_.string("sc64.storage");
    if (storage_.empty()) storage_ = sc64_storage_path();
    sc64_platform_initialize(storage_);
    desktop::game::prepare_staging();  // Android launcher: the folder for `adb push` must exist before a ROM is chosen
    static std::string backend;
    backend = config_.string("sc64.audio_backend", "aaudio");
    desktop::audio::configure(backend.c_str());
    background_input_probe_ = config_.number("sc64.background_input_probe", 0) == 1
        && !config_.string("sc64.inputs").empty() && config_.number("sc64.seconds", 0) > 0;
    const auto probe = config_.string("sc64.gpu_probe");
    if (!probe.empty()) {
        probe_out_ = std::filesystem::weakly_canonical(config_.string("sc64.probe_out"));
        if (!inside_private(probe_out_) || std::filesystem::exists(probe_out_))
            throw std::runtime_error("GPU probe requires a new ignored local/ output directory");
        std::ifstream input(probe);
        probe_cases_ = nlohmann::json::parse(input).at("cases");
        if (probe_cases_.size() != 20) throw std::runtime_error("GPU probe manifest must contain 20 cases");
        std::filesystem::create_directories(probe_out_);
    }
    const auto trace = config_.string("sc64.trace_out");
    if (!trace.empty()) {
        trace_out_ = std::filesystem::weakly_canonical(trace);
        trace_from_ = config_integer(config_, "sc64.trace_from");
        trace_to_ = config_integer(config_, "sc64.trace_to");
        if (!inside_private(trace_out_) || !trace_from_ || trace_to_ < trace_from_ || trace_to_ - trace_from_ > 600
            || !probe_cases_.empty())
            throw std::runtime_error("Frame trace requires a private path and at most 600 VI fields");
        std::filesystem::create_directories(trace_out_);
        trace_log_.open(trace_out_ / "frames.jsonl");
        trace_log_.exceptions(std::ios::failbit | std::ios::badbit);
        session().scanout.trace(trace_from_, trace_to_);
    }
    const auto audio_trace = config_.string("sc64.audio_trace_out");
    if (!audio_trace.empty()) {
        const auto out = std::filesystem::weakly_canonical(audio_trace);
        if (!inside_private(out) || std::filesystem::exists(out))
            throw std::runtime_error("Audio trace requires a new private output path");
        desktop::audio::trace(out, config_integer(config_, "sc64.audio_trace_from"),
                              config_integer(config_, "sc64.audio_trace_to"));
    }
}

void Native::restore_cursor() { sc64_restore_mouse_cursor(); system_cursor_hidden_ = false; }

void Native::reset_feedback() {
    cursor_requested_ = false; restore_cursor();
    cursor_ = {}; ui_ = {};
    pause_policy_.in_level = false;
    apply_pause();
}

void Native::apply_pause() {
    pause_policy_.user = user_paused_;
    pause_policy_.inactive = inactive_;
    pause_policy_.iconified = iconified_;
    pause_policy_.background_probe = background_input_probe_;
    auto& s = session();
    if (s.requested_paused.load() != pause_policy_.paused()) s.pause(pause_policy_.paused());
    if (!pause_policy_.input_active()) {
        for (auto& pad : s.pads) { pad.buttons = 0; pad.x = 0; pad.y = 0; }
        runtime::minimap_input().clear();
    }
    if (user_paused_ || inactive_ || iconified_) { cursor_requested_ = false; restore_cursor(); }
}

void Native::update_cursor() {
    auto& s = session();
    const auto field = s.fields.load();
    const bool fresh = field >= cursor_.field && field - cursor_.field <= 12;
    std::optional<Sc64CursorRegion> hud_region, minimap_region;
    if (ui_.mode == "game" && ui_.bounds && field >= ui_.field && field - ui_.field <= 12 && width_ && height_)
        hud_region = sc64_project_cursor_region(cursor_region_, ui_.bounds->x0, ui_.bounds->y0,
                                                ui_.bounds->x1, ui_.bounds->y1, width_, height_);
    if (ui_.mode == "game" && cursor_.minimap && fresh && width_ && height_) {
        const auto& m = *cursor_.minimap;
        minimap_region = sc64_project_cursor_region(cursor_region_, m.x0, m.y0, m.x1 + 1, m.y1 + 1, width_, height_);
    }
    system_cursor_hidden_ = sc64_update_mouse_cursor(cursor_requested_ && !inactive_ && !iconified_
        && s.running && !s.paused && ui_.mode == "game" && cursor_.valid && fresh
        && scanout_visible_ && width_ == 320 && height_ == 240, cursor_region_,
        hud_region ? &*hud_region : nullptr, minimap_region ? &*minimap_region : nullptr);
}

void Native::update_rumble() {
    auto& s = session();
    const bool wanted = haptics_enabled_ && s.rumble.load() && s.running.load()
        && !s.paused.load() && !inactive_ && !iconified_;
    const int request = rumble_motor_.update(wanted, desktop::RumbleMotor::Clock::now());
    if (request != desktop::RumbleMotor::none) sc64_platform_vibrate(request, desktop::RumbleMotor::amplitude);
}

void Native::load_probe_case() {
    auto& item = probe_cases_.at(probe_case_);
    std::ifstream input(item.at("source_ram").get<std::string>(), std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != 0x800000) throw std::runtime_error("GPU probe RAM must be 8 MiB");
    std::vector<std::uint8_t> ram(0x800000);
    input.seekg(0); input.read(reinterpret_cast<char*>(ram.data()), ram.size());
    if (!input) throw std::runtime_error("Incomplete GPU probe RAM");
    probe_ram_sha256_ = runtime::sha256_hex(ram);
    if (probe_ram_sha256_ != item.at("ram_sha256").get<std::string>()) throw std::runtime_error("GPU probe RAM hash mismatch");
    auto list = std::make_shared<gfx::DrawList>(gfx::interpret_gbi(ram, item.at("root")));
    session().mailbox.publish(std::move(list));
    probe_loaded_ = true; probe_uploaded_ = false;
}

std::optional<Native::Upload> Native::tick() {
    try {
        update_cursor();
        update_rumble();
        if (ui_.guest_mode >= 0) desktop::audio::menu_profile(ui_.guest_mode == 4);
        for (const auto& vi : session().scanout.take_trace()) {
            trace_log_ << nlohmann::json{{"kind", "vi"}, {"field", vi.field}, {"revision", vi.revision},
                {"regs", vi.regs}, {"task", vi.completed_task}, {"minimum_task", vi.minimum_task},
                {"blank", vi.blank()}}.dump() << "\n";
            trace_log_.flush();
        }
        const bool probe = !probe_cases_.empty();
        if (probe && !probe_loaded_) load_probe_case();
        runtime::FrontendFeedback frontend;
        std::shared_ptr<const desktop::RenderedFrame> rendered;
        auto frame = session().mailbox.take(&frontend, &rendered);
        if (frame && !frame->rectangles.empty()) {
            const auto frame_cursor = desktop::read_cursor(frame->initial_ram, frame->identity.field);
            const auto frame_ui = desktop::read_ui(frame->initial_ram, frame->identity.field, frontend);
            pause_policy_.observe_mode(frame_ui.guest_mode);
            apply_pause();
            const auto started = std::chrono::steady_clock::now();
            gfx::SoftwareResult fixture;
            if (!rendered) {
                if (!probe) throw std::runtime_error("Live task lacks completed RDP result");
                fixture = gfx::render_software(*frame);
            }
            const auto& result = rendered ? rendered->pixels : fixture;
            std::vector<gfx::Image> targets;
            if (probe) targets.push_back(frame->rectangles.back().target);
            else for (const auto& rect : frame->rectangles)
                if (std::find(targets.begin(), targets.end(), rect.target) == targets.end()) targets.push_back(rect.target);
            for (const auto& target : targets) {
                unsigned frame_height = 0;
                for (const auto& rect : frame->rectangles)
                    if (rect.target == target)
                        frame_height = std::max(frame_height, unsigned(std::max(0, rect.state.scissor.y1 / 4)));
                if (!target.width || target.width > 1024 || !frame_height || frame_height > 1024)
                    throw std::runtime_error("Unsupported native target extent");
                // Scissor height may exceed the rows below the top of RDRAM.
                // Such a target never matches VI scanout; skip its export.
                if (!desktop::target_in_ram(target, frame_height, result.ram.size())) continue;
                auto image = std::make_shared<desktop::ScanoutImage>();
                image->image = target; image->height = frame_height; image->task = frame->identity.task;
                image->cursor = frame_cursor; image->ui = frame_ui;
                image->rgba = gfx::target_rgba(result.ram, target, frame_height);
                // Native RDP alpha carries coverage; presentation is opaque.
                for (std::size_t i = 3; i < image->rgba.size(); i += 4) image->rgba[i] = 255;
                scanout_images_.add(image);
                if (probe) { uploaded_image_.reset(); cpu_image_ = image; }
            }
            software_ms_ = rendered ? rendered->software_ms
                : std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            software_service_ms_ = rendered ? rendered->software_service_ms : 0;
        }
        scanout_ = session().scanout.latest();
        std::shared_ptr<const desktop::ScanoutImage> image = probe ? cpu_image_ : scanout_images_.select(scanout_);
        if (!probe) {
            // A CPU swap newer than the matching RDP image is what VI scans.
            auto cpu = session().cpu_scanouts.select(scanout_, 0x800000);
            if (cpu && (!image || cpu->after_task >= image->task)) {
                if (cpu != cpu_source_) {
                    auto converted = std::make_shared<desktop::ScanoutImage>();
                    converted->image = cpu->image; converted->height = cpu->height;
                    converted->task = cpu->after_task;
                    auto local = cpu->image; local.address = 0;
                    converted->rgba = gfx::target_rgba(cpu->canonical, local, cpu->height);
                    for (std::size_t i = 3; i < converted->rgba.size(); i += 4) converted->rgba[i] = 255;
                    // Keep the older feedback and its field stamp: no new guest
                    // UI state was observed for this CPU-only frame.
                    if (image && image->image.address == cpu->image.address) {
                        converted->cursor = image->cursor; converted->ui = image->ui;
                    }
                    cpu_source_ = cpu; cpu_image_ = converted;
                }
                image = cpu_image_;
            }
        }
        scanout_visible_ = bool(image);
        cursor_ = image ? image->cursor : desktop::CursorFeedback{};
        ui_ = image ? image->ui : desktop::UiFeedback{};
        update_cursor();
        const bool tracing = trace_log_.is_open() && scanout_.field >= trace_from_ && scanout_.field <= trace_to_;
        if (tracing) {
            trace_log_ << nlohmann::json{{"kind", "present"}, {"field", scanout_.field}, {"revision", scanout_.revision},
                {"visible", scanout_visible_}, {"task", image ? image->task : 0}, {"cpu", image && image == cpu_image_},
                {"address", image ? image->image.address : 0}, {"width", image ? image->image.width : 0}}.dump() << "\n";
            trace_log_.flush();
        }
        if (!image || image == uploaded_image_) return std::nullopt;
        const auto target = image->image;
        const auto frame_height = image->height;
        presented_region_ = probe ? desktop::ViRegion{target.width, frame_height}
                                  : desktop::vi_region(scanout_, target.width, frame_height);
        auto rgba = std::make_shared<std::vector<std::uint8_t>>(
            desktop::vi_present(image->rgba, target.width, frame_height, presented_region_));
        if (tracing) {
            // CPU frames share their preceding task number; name them apart.
            const auto name = (image == cpu_image_ && !probe ? "cpu-" + std::to_string(scanout_.revision) + "-after-"
                                                             : std::string("task-")) + std::to_string(image->task) + ".rgba";
            std::ofstream capture(trace_out_ / name, std::ios::binary);
            capture.exceptions(std::ios::failbit | std::ios::badbit);
            capture.write(reinterpret_cast<const char*>(rgba->data()), std::streamsize(rgba->size()));
            trace_log_ << nlohmann::json{{"kind", "upload"}, {"task", image->task}, {"field", scanout_.field},
                {"width", target.width}, {"height", frame_height}, {"cpu", image == cpu_image_},
                {"vi_columns", presented_region_.width}, {"vi_rows", presented_region_.height},
                {"address", target.address}, {"ui_mode", ui_.mode}, {"rgba", name}}.dump() << "\n";
            trace_log_.flush();
        }
        if (probe) {
            const auto& target_info = probe_cases_.at(probe_case_).at("target");
            if (target_info.at("width") != target.width || target_info.at("height") != frame_height)
                throw std::runtime_error("GPU probe native target extent differs from manifest");
            std::ofstream expected(probe_out_ / ("case-" + std::to_string(probe_case_) + ".rgba"), std::ios::binary);
            expected.exceptions(std::ios::failbit | std::ios::badbit);
            expected.write(reinterpret_cast<const char*>(rgba->data()), std::streamsize(rgba->size()));
            probe_uploaded_ = true; probe_age_ = 0;
        }
        width_ = target.width; height_ = frame_height;
        uploaded_image_ = image;
        upload_bytes_ += rgba->size(); ++presented_;
        return Upload{std::move(rgba), width_, height_};
    } catch (const std::exception& error) {
        log::error("%s", error.what());
        session().fail(error.what());
        return std::nullopt;
    }
}

void Native::after_render(const std::function<Readback()>& readback) {
    try {
        if (trace_log_.is_open() && scanout_.field >= trace_from_ && scanout_.field <= trace_to_ && trace_readbacks_ < 40) {
            if (scanout_visible_ != trace_last_visible_ || width_ != trace_last_width_) trace_readback_tail_ = 8;
            trace_last_visible_ = scanout_visible_; trace_last_width_ = width_;
            if (trace_readback_tail_) {
                // Bounded debug readback; the normal renderer never reads the GPU.
                const auto window = readback();
                const auto name = "window-" + std::to_string(trace_readbacks_++) + ".bgra";
                std::ofstream capture(trace_out_ / name, std::ios::binary);
                capture.exceptions(std::ios::failbit | std::ios::badbit);
                capture.write(reinterpret_cast<const char*>(window.bgra.data()), std::streamsize(window.bgra.size()));
                trace_log_ << nlohmann::json{{"kind", "window"}, {"field", scanout_.field}, {"width", window.width},
                    {"height", window.height}, {"visible", scanout_visible_}, {"bgra", name}}.dump() << "\n";
                trace_log_.flush(); // Readback order is evidence, not a VI pixel parity claim.
                trace_readback_tail_--;
            }
        }
        // Keep a static fixture for several presents so the readback cannot
        // confuse current and pending swapchain work.
        if (probe_cases_.empty() || !probe_uploaded_ || ++probe_age_ < 6) return;
        const auto window = readback();
        const auto prefix = probe_out_ / ("case-" + std::to_string(probe_case_));
        std::ofstream raw(prefix.string() + ".bgra", std::ios::binary);
        raw.exceptions(std::ios::failbit | std::ios::badbit);
        raw.write(reinterpret_cast<const char*>(window.bgra.data()), std::streamsize(window.bgra.size()));
        std::ofstream metadata(prefix.string() + ".json");
        metadata.exceptions(std::ios::failbit | std::ios::badbit);
        metadata << nlohmann::json{{"case", probe_cases_.at(probe_case_).at("id")},
            {"window_width", window.width}, {"window_height", window.height}, {"native_width", width_},
            {"native_height", height_}, {"renderer", window_.renderer()}, {"presented", presented_},
            {"source_ram_sha256", probe_ram_sha256_}, {"sdl_version", SC64_SDL_VERSION},
            {"origin", "top-left"}}.dump(2);
        log::info("SC64 GPU probe captured case %u", probe_case_);
        ++probe_case_;
        probe_age_ = 0; probe_loaded_ = false; probe_uploaded_ = false;
        if (probe_case_ == probe_cases_.size()) {
            log::info("SC64 GPU probe complete");
            probe_cases_.clear();
        }
    } catch (const std::exception& error) {
        log::error("SC64 GPU probe: %s", error.what());
        probe_cases_.clear();
        session().fail(error.what());
    }
}

void Native::lifecycle(WindowEvent event) {
    log::info("SC64 lifecycle event %d", int(event));
    if (event == WindowEvent::focus_lost) inactive_ = true;
    if (event == WindowEvent::focus_gained) inactive_ = false;
    if (event == WindowEvent::iconified) iconified_ = true;
    if (event == WindowEvent::restored) iconified_ = false;
    apply_pause();
    // Stop the motor immediately; the main loop may not run in background.
    update_rumble();
}

void Native::suspend(bool suspended) {
    auto& s = session();
    // tools/android/run.py pairs these counters across Home/return.
    log::info("SC64 Android suspended=%d fields=%llu tasks=%llu samples=%llu", suspended ? 1 : 0,
              static_cast<unsigned long long>(s.fields.load()), static_cast<unsigned long long>(s.tasks.load()),
              static_cast<unsigned long long>(s.samples.load()));
    s.suspend(suspended);
}

void Native::finalize() {
    if (rumble_motor_.update(false, desktop::RumbleMotor::Clock::now()) == 0) sc64_platform_vibrate(0, 0);
    sc64_platform_finalize();
    restore_cursor(); cursor_requested_ = false;
    session().stop();
}

input::Info Native::info() {
    auto& s = session();
    input::Info info;
    const bool native_layout = scanout_visible_ && width_ == 320 && height_ == 240;
    info.fields = double(s.fields.load());
    info.running = s.running; info.paused = s.paused;
    info.input_active = pause_policy_.input_active() && !s.paused.load();
    info.width = width_; info.height = height_;
    info.presented = double(presented_);
    info.cursor_x = cursor_.x; info.cursor_y = cursor_.y; info.cursor_field = double(cursor_.field);
    info.cursor_divisor = cursor_.divisor;
    info.camera_x = cursor_.camera_x; info.camera_y = cursor_.camera_y;
    info.camera_min_x = cursor_.camera_min_x; info.camera_max_x = cursor_.camera_max_x;
    info.camera_min_y = cursor_.camera_min_y; info.camera_max_y = cursor_.camera_max_y;
    info.camera_divisor = cursor_.camera_divisor;
    info.camera_valid = cursor_.camera_valid && native_layout;
    info.cursor_valid = cursor_.valid && native_layout;
    if (cursor_.minimap && native_layout && ui_.mode == "game") {
        const auto& m = *cursor_.minimap;
        info.minimap = input::Bounds{double(m.x0), double(m.y0), double(m.x1 + 1), double(m.y1 + 1), double(m.context)};
    }
    info.ui_mode = ui_.mode;
    info.ui_token = double(ui_.token); info.ui_field = double(ui_.field);
    info.ui_context = double(ui_.context);
    info.pick_required = ui_.pick_required;
    if (ui_.pick)
        info.cursor_pick = input::CursorPick{ui_.pick->x, ui_.pick->y, double(ui_.pick->context),
            double(ui_.pick->buttons), double(ui_.pick->target_type), ui_.pick->worker_selected,
            ui_.pick->target_selected};
    if (ui_.bounds)
        info.ui_bounds = input::Bounds{ui_.bounds->x0, ui_.bounds->y0, ui_.bounds->x1, ui_.bounds->y1,
                                       double(ui_.bounds->context)};
    if (ui_.main && ui_.carousel_bounds)
        info.ui_carousel = input::Carousel{double(ui_.main->campaign), ui_.carousel_bounds->x0,
            ui_.carousel_bounds->y0, ui_.carousel_bounds->x1, ui_.carousel_bounds->y1, ui_.main->ready};
    info.guest_mode = ui_.guest_mode;
    info.message_id = ui_.message_id;
    for (const auto& item : ui_.items)
        info.ui_items.push_back(input::UiItem{double(item.id), double(item.group), item.x0, item.y0, item.x1, item.y1,
            double(item.columns), double(item.buttons), double(item.context), double(item.label),
            item.selected, item.horizontal, item.activate});
    info.minimap_jumps = double(s.minimap_jumps.load()); info.minimap_field = double(s.minimap_field.load());
    info.error = s.error();
    return info;
}

nlohmann::json Native::telemetry() {
    namespace audio = desktop::audio;
    auto& s = session();
    auto result = to_json(info());
    result.update({{"tasks", s.tasks.load()}, {"samples", s.samples.load()},
        {"host_seconds", std::chrono::duration<double>(std::chrono::steady_clock::now() - host_started_).count()},
        {"scanout_visible", scanout_visible_}, {"vi_revision", scanout_.revision}, {"vi_width", scanout_.regs[2]},
        {"vi_origin", scanout_.regs[1]}, {"cpu_swaps", s.cpu_swaps.load()}, {"vi_rows", presented_region_.height},
        {"render_driver", window_.renderer()}, {"upload_bytes", upload_bytes_}, {"software_ms", software_ms_},
        {"software_service_ms", software_service_ms_}, {"raster_helpers", s.raster ? s.raster->helpers() : 0},
        {"rumble_starts", s.rumble_starts.load()}, {"capture_snapshot_ms", s.capture_snapshot_ms.load()},
        {"capture_interpret_ms", s.capture_interpret_ms.load()}, {"system_cursor_hidden", system_cursor_hidden_},
        {"audio_underruns", audio::underruns()}, {"audio_dropped_frames", audio::dropped()},
        {"audio_played_frames", audio::played()}, {"audio_nonzero_frames", audio::nonzero()},
        {"audio_wait_callbacks", audio::waits()},
        {"audio_buffer_ms", audio::buffered_ms()}, {"audio_source_hz", audio::source_hz()},
        {"audio_backend", audio::backend()}, {"audio_dma_ms", audio::dma_ms()}, {"audio_target_ms", audio::target_ms()},
        {"audio_device_xruns", audio::hardware_xruns()}, {"audio_device_buffer_frames", audio::hardware_buffer_frames()},
        {"audio_buffering", audio::rebuffering()}, {"background_input_probe", background_input_probe_},
        {"renderer", "software-native-target"}});
    return result;
}

nlohmann::json Native::take_audio_timing() {
    const auto timing = desktop::audio::take_timing();
    return {{"audio_source_gap_ms", timing.source_gap_ms}, {"audio_callback_gap_ms", timing.callback_gap_ms},
            {"audio_callback_work_ms", timing.callback_work_ms}, {"audio_source_frames", timing.source_frames},
            {"audio_source_nonzero_frames", timing.source_nonzero_frames}};
}

std::optional<std::string> Native::load_rom(const std::string& path) {
    try {
        session().load(path, storage_);
        reset_feedback();
        return std::nullopt;
    } catch (const std::exception& error) { return std::string(error.what()); }
}

std::optional<std::string> Native::load_bundled_rom() {
    try {
        session().load_bundled(sc64_read_bundled_rom(), storage_);
        reset_feedback();
        log::info("SC64 ROM loaded from the packaged build; no private ROM copy");
        return std::nullopt;
    } catch (const std::exception& error) { return std::string(error.what()); }
}

std::optional<std::string> Native::choose_rom() {
    try {
        restore_cursor();
        sc64_choose_rom();
        return std::nullopt;
    } catch (const std::exception& error) { return std::string(error.what()); }
}

std::optional<std::string> Native::poll_rom() {
    if (auto build = session().poll_build()) {
        if (build->empty()) reset_feedback();
        return build;
    }
    auto result = sc64_poll_rom();
    if (result.path.empty()) {
        if (result.error.empty()) return std::nullopt;
        return result.error;
    }
    if (const auto error = load_rom(result.path)) result.error = *error;
    // The import is an owned temporary copy in storage, never the user's file.
    std::error_code cleanup_error;
    std::filesystem::remove(result.path, cleanup_error);
    if (cleanup_error) log::warning("ROM temporary cleanup: %s", cleanup_error.message().c_str());
    return result.error;
}

std::optional<std::string> Native::stored_rom() {
    try {
        const auto path = std::filesystem::path(storage_) / "sc64-ntsc-u.z64";
        if (std::filesystem::is_regular_file(path)) return path.string();
    } catch (const std::exception&) {}
    return std::nullopt;
}

void Native::pause(bool paused) { user_paused_ = paused; apply_pause(); }

void Native::set_pad(int player, unsigned buttons, double x, double y) {
    if (player < 1 || player > 2 || !std::isfinite(x) || !std::isfinite(y)) return;
    auto& pad = session().pads[std::size_t(player - 1)];
    if (pause_policy_.input_active() && !session().paused.load()) {
        pad.buttons = buttons & 0xffff;
        pad.x = float(std::clamp(x, -1.0, 1.0)); pad.y = float(std::clamp(y, -1.0, 1.0));
    }
}

bool Native::navigate_minimap(double x, double y, double context, double source_field) {
    auto& s = session();
    const auto field = s.fields.load();
    const bool valid = std::isfinite(context) && context >= 0 && context <= UINT32_MAX
        && std::isfinite(source_field) && source_field >= 0 && source_field <= double(field)
        && double(field) - source_field <= 12 && cursor_.minimap
        && cursor_.minimap->context == context && cursor_.minimap->contains(x, y)
        && source_field == double(cursor_.field) && scanout_visible_ && width_ == 320 && height_ == 240
        && ui_.mode == "game" && pause_policy_.input_active() && s.running && !s.paused;
    if (valid) runtime::minimap_input().submit({x, y, std::uint32_t(context), std::uint64_t(source_field)});
    return valid;
}

void Native::set_cursor_hidden(const std::optional<std::array<double, 4>>& region) {
    cursor_requested_ = false;
    if (region) {
        const Sc64CursorRegion r{(*region)[0], (*region)[1], (*region)[2], (*region)[3]};
        if (std::isfinite(r.x0) && std::isfinite(r.y0) && std::isfinite(r.x1) && std::isfinite(r.y1)
            && r.x0 >= 0 && r.y0 >= 0 && r.x1 <= 1 && r.y1 <= 1 && r.x0 < r.x1 && r.y0 < r.y1) {
            cursor_region_ = r; cursor_requested_ = true;
        }
    }
    update_cursor();
}

// Short touch confirmation; never interrupt an active guest motor.
void Native::vibrate(int milliseconds, int amplitude) {
    milliseconds = std::clamp(milliseconds, 1, 200);
    amplitude = std::clamp(amplitude, 1, 255);
    if (haptics_enabled_ && !rumble_motor_.on()) sc64_platform_vibrate(milliseconds, amplitude);
}

// Gates touch confirmations and Rumble Pak output.
void Native::set_haptics(bool enabled) { haptics_enabled_ = enabled; }
void Native::exit(int code) { window_.exit(code); }
std::pair<double, double> Native::window_size() { return window_.size(); }
void Native::log(const std::string& line) { log::line(line); }
}
