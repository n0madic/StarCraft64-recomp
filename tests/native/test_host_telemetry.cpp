// Telemetry merge regression: HostScript must keep the audio timing snapshot
// alive while merging it. A range-for over a temporary's items() dangled under
// C++20 and crashed the Windows build (built with AddressSanitizer here).
#include "scripts.hpp"
#include <cassert>
#include <string>
#include <vector>

namespace {
using namespace sc64::shell;
class Native final : public NativeApi {
public:
    sc64::input::Info guest;
    std::vector<std::string> lines;
    sc64::input::Info info() override { return guest; }
    nlohmann::json telemetry() override { return {{"fields", 7}, {"running", true}}; }
    nlohmann::json take_audio_timing() override {
        // Heap-allocated strings, so a dangling read is observable.
        return {{"audio_callback_gap_ms", 12.5}, {"audio_source_frames", 4096},
                {"audio_backend_detail", std::string(64, 'x')}};
    }
    std::optional<std::string> load_rom(const std::string&) override { return std::nullopt; }
    std::optional<std::string> load_bundled_rom() override { return std::nullopt; }
    std::optional<std::string> choose_rom() override { return std::nullopt; }
    std::optional<ImportEvent> poll_import() override { return std::nullopt; }
    std::optional<std::string> stored_rom() override { return std::nullopt; }
    void pause(bool) override {}
    void set_pad(int, unsigned, double, double) override {}
    bool navigate_minimap(double, double, double, double) override { return false; }
    void set_cursor_hidden(const std::optional<std::array<double, 4>>&) override {}
    void vibrate(int, int) override {}
    void set_haptics(bool) override {}
    void exit(int) override {}
    std::pair<double, double> window_size() override { return {960, 720}; }
    void log(const std::string& line) override { lines.push_back(line); }
};
}

int main() {
    Native native;
    native.guest.running = true;
    Config config;
    Shared shared;
    HostScript host(shared, native, config);
    host.init();
    for (const char* compact : {"0", "1"}) {
        config.set("sc64.compact_telemetry", compact);
        native.lines.clear();
        HostScript fresh(shared, native, config);
        fresh.init();
        fresh.update(1.0 / 60);
        std::string telemetry;
        for (const auto& line : native.lines) if (line.starts_with("SC64 {")) telemetry = line;
        assert(!telemetry.empty());
        const auto row = nlohmann::json::parse(telemetry.substr(5));
        assert(row.at("audio_callback_gap_ms") == 12.5);
        assert(row.at("audio_source_frames") == 4096);
        assert(row.at("audio_backend_detail") == std::string(64, 'x'));
        assert(row.at("fields") == 7);
    }
}
