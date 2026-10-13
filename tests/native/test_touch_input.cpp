// One-to-one port of tests/test_touch_input.lua: diagnostic touch pad, Fit
// viewport, and the real GUI/host/status scripts (scripts.hpp) driven through
// NativeApi/LayoutStore mocks. The Lua test mocked engine globals and dofile'd
// the scripts; here one Shared state plays the role of the shared Lua modules
// (touch_pad.state, gestures.state, ui_state) and each Lua scene table is a
// fresh script object over it. Lua nil indexing errors are mirrored with
// std::optional::value() and container at() so a missing value still fails.
#include "scripts.hpp"
#include "launcher_screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {
[[noreturn]] void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    std::abort();
}
}
#define CHECK(expr) do { if (!(expr)) fail(__FILE__, __LINE__, #expr); } while (0)
#define NEAR(a, b) do { \
    const double near_a = (a), near_b = (b); \
    if (!(std::abs(near_a - near_b) < 1e-6)) { \
        std::fprintf(stderr, "%s:%d: NEAR failed: %s ~= %s (%.17g ~= %.17g)\n", __FILE__, __LINE__, #a, #b, \
                     near_a, near_b); \
        std::abort(); \
    } \
} while (0)

using namespace sc64::shell;
using namespace sc64::input;
using std::nullopt;

namespace {
bool has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

// The former `sc64` Lua module, `window.get_size` and the recording mocks.
struct MockNative final : NativeApi {
    Info guest;
    std::pair<double, double> size{960, 720};
    struct Submitted { unsigned bits; double x, y; };
    std::optional<Submitted> submitted;
    struct Vibration { int ms, amplitude; };
    std::vector<Vibration> vibrations;
    std::optional<bool> haptics_enabled;
    std::vector<bool> pause_calls;
    std::vector<std::string> logs;
    std::optional<nlohmann::json> logged; // last "SC64 pad" row for player 1
    int selected = 0;
    std::optional<std::string> load_path, stored;
    std::optional<bool> poll_result;
    std::optional<LauncherButton::Action> picking;  // the picker that is open, for choosing()
    bool trace = true;
    std::vector<std::string> warnings;
    int module_selected = 0;
    LauncherFacts facts;
    int bundled_calls = 0;
    std::optional<std::string> bundled_error;
    std::optional<std::array<double, 4>> cursor_hidden;
    std::optional<int> exit_code;

    Info info() override { return guest; }
    nlohmann::json telemetry() override { return nlohmann::json::object(); }
    nlohmann::json take_audio_timing() override { return nlohmann::json::object(); }
    std::optional<std::string> load_rom(const std::string& path) override { load_path = path; return nullopt; }
    std::optional<std::string> load_bundled_rom() override { ++bundled_calls; return bundled_error; }
    std::optional<std::string> choose_rom() override { ++selected; return nullopt; }
    std::optional<std::string> choose_module() override { ++module_selected; return nullopt; }
    LauncherFacts launcher_facts() override { return facts; }
    std::optional<LauncherButton::Action> choosing() override { return picking; }
    bool tracing() override { return trace; }
    void warn(const std::string& line) override { warnings.push_back(line); logs.push_back(line); }
    std::optional<ImportEvent> poll_import() override {
        if (!poll_result) return nullopt;
        const bool ok = *poll_result;
        poll_result.reset();
        return ok ? ImportEvent{ImportEvent::Kind::started, {}} : ImportEvent{ImportEvent::Kind::failed, "invalid ROM"};
    }
    std::optional<std::string> stored_rom() override { return stored; }
    void pause(bool paused) override { pause_calls.push_back(paused); }
    void set_pad(int player, unsigned buttons, double x, double y) override {
        if (player == 1) submitted = Submitted{buttons, x, y};
    }
    bool navigate_minimap(double, double, double, double) override { return false; }
    void set_cursor_hidden(const std::optional<std::array<double, 4>>& region) override { cursor_hidden = region; }
    void vibrate(int milliseconds, int amplitude) override { vibrations.push_back({milliseconds, amplitude}); }
    void set_haptics(bool enabled) override { haptics_enabled = enabled; }
    void exit(int code) override { exit_code = code; }
    std::pair<double, double> window_size() override { return size; }
    void log(const std::string& line) override {
        logs.push_back(line);
        constexpr std::string_view prefix = "SC64 pad ";
        if (line.starts_with(prefix)) {
            auto row = nlohmann::json::parse(line.substr(prefix.size()));
            if (row.value("player", 0) == 1) logged = std::move(row);
        }
    }
};

// The former sys.load/sys.save pair.
struct MockStore final : LayoutStore {
    enum class Save { ok, fail, raise } save_result = Save::ok;
    std::optional<nlohmann::json> load_result = nlohmann::json::object();
    std::optional<nlohmann::json> load() override { return load_result; }
    std::optional<std::string> save(const nlohmann::json& layout) override {
        CHECK(layout.value("version", 0) == 1);
        switch (save_result) {
        case Save::ok: return nullopt;
        case Save::fail: return std::string("save returned false");
        case Save::raise: return std::string("disk full"); // Lua error("disk full")
        }
        return nullopt;
    }
};

InputEvent pressed_event() { InputEvent a; a.pressed = true; return a; }
InputEvent released_event() { InputEvent a; a.released = true; return a; }
InputEvent at_point(double x, double y, std::optional<double> sx = nullopt, std::optional<double> sy = nullopt) {
    InputEvent a; a.x = x; a.y = y; a.screen_x = sx; a.screen_y = sy;
    return a;
}
// One touch_multi event carrying `a` as contact `id`.
InputEvent multi(const std::string& id, const InputEvent& a) {
    InputEvent m;
    m.touch.push_back(Touch{id, a.pressed, a.released, a.x.value_or(0), a.y.value_or(0), a.screen_x, a.screen_y});
    return m;
}
InputEvent gamepad_event(int pad, bool pressed = false) { InputEvent a; a.gamepad = pad; a.pressed = pressed; return a; }

// The Lua test held `scene.edges`; GuiScript creates them right after one box
// and one text node per pad control and the hint text, in that order.
std::array<const OverlayNode*, 4> edges(const GuiScript& gui) {
    const std::size_t first = 2 * touch_pad::controls().size() + 1;
    std::array<const OverlayNode*, 4> result{};
    for (std::size_t i = 0; i < 4; ++i) result[i] = &gui.nodes.at(first + i);
    return result;
}
// Creation-order sanity of the edge lookup above (not a Lua assertion).
void check_edge_layout(const GuiScript& gui) {
    const std::size_t hint = 2 * touch_pad::controls().size();
    CHECK(gui.nodes.at(hint).kind == OverlayNode::Kind::text);
    for (const auto* edge : edges(gui)) {
        CHECK(edge->kind == OverlayNode::Kind::box && !edge->enabled);
        CHECK(edge->color[0] == .8 && edge->color[1] == .66 && edge->color[2] == .3 && edge->color[3] == .3);
    }
}
}

int main() {
    // ------------------------------------------------ touch pad and viewport
    {
        touch_pad::State s;
        const double w = 960, h = 720;
        const auto at = [&](std::string_view id) { return touch_pad::geometry(s, id, w, h); };
        const auto down = [&](int pointer, std::string_view control, double field = 10) {
            const auto g = at(control);
            CHECK(touch_pad::pointer(s, std::to_string(pointer), Phase::down, g.x, g.y, field, w, h));
        };
        const auto read = [&](double field = 20) { return touch_pad::read(s, field, w, h); };
        down(1, "a"); down(2, "b");
        CHECK(read().buttons == 0xc000);
        touch_pad::pointer(s, "1", Phase::up, 0, 0, 20, w, h);
        CHECK(read().buttons == 0x4000);
        touch_pad::pointer(s, "2", Phase::up, 0, 0, 20, w, h);
        CHECK(read().buttons == 0);
        // A short same-frame click survives the slower guest poll, then expires.
        down(1, "start", 30);
        touch_pad::pointer(s, "1", Phase::up, 0, 0, 30, w, h);
        CHECK(read(30).buttons == 0x1000 && read(36).buttons == 0);
        touch_pad::clear(s);
        down(1, "a"); down(2, "a");
        touch_pad::pointer(s, "1", Phase::up, 0, 0, 20, w, h);
        CHECK(read().buttons == 0x8000);
        touch_pad::pointer(s, "2", Phase::move, 480, 500, 20, w, h);
        CHECK(read().buttons == 0);
        const auto a = at("a");
        CHECK(!touch_pad::pointer(s, "2", Phase::move, a.x, a.y, 21, w, h));
        down(3, "stick");
        const auto stick = at("stick");
        const double cx = stick.x, cy = stick.y, size = stick.size;
        touch_pad::pointer(s, "3", Phase::move, cx + size, cy + size, 20, w, h);
        {
            const auto out = read();
            NEAR(out.x * out.x + out.y * out.y, 1);
        }
        down(4, "stick"); // second contact cannot replace the first stick owner
        CHECK(!s.pointers.contains("4"));
        touch_pad::pointer(s, "3", Phase::move, cx + size * .05, cy, 20, w, h);
        { const auto dead = read().x; CHECK(dead == 0); }
        touch_pad::block(s, true); CHECK(read().buttons == 0 && s.pointers.empty());
        touch_pad::block(s, false); CHECK(read().buttons == 0);
        down(5, "z"); touch_pad::pointer(s, "5", Phase::cancel, 0, 0, 20, w, h); CHECK(read().buttons == 0);
        down(6, "b"); touch_pad::device(s, 0, true);
        CHECK(!touch_pad::visible(s) && read().buttons == 0);
        touch_pad::device(s, 1, true); touch_pad::device(s, 0, false); CHECK(!touch_pad::visible(s));
        touch_pad::device(s, 1, false); CHECK(touch_pad::visible(s) && read().buttons == 0);
        touch_pad::edit(s, true); down(7, "a");
        touch_pad::pointer(s, "7", Phase::move, 480, 360, 20, w, h);
        NEAR(s.layout.controls.at("a").x, .5); NEAR(s.layout.controls.at("a").y, .5);
        CHECK(read().buttons == 0);
        touch_pad::adjust(s, 1, 1);
        CHECK(s.layout.controls.at("a").size == .30 && s.layout.controls.at("a").opacity == .9);
        // pad.new(s.layout) re-validates the saved table: round-trip it.
        touch_pad::State restored;
        restored.layout = touch_layout_from(touch_layout_json(s.layout));
        NEAR(restored.layout.controls.at("a").x, .5);
        touch_pad::edit(s, false); CHECK(read().buttons == 0);
        const auto corrupt = touch_layout_from(nlohmann::json{{"version", 1}, {"controls", {{"a", {
            {"x", std::numeric_limits<double>::quiet_NaN()}, {"y", std::numeric_limits<double>::infinity()},
            {"size", -5}, {"opacity", "bad"}}}}}});
        CHECK(corrupt.controls.at("a").x == .90 && corrupt.controls.at("a").y == .22);
        CHECK(corrupt.controls.at("a").size == .04 && corrupt.controls.at("a").opacity == .45);
        CHECK(touch_layout_from(nlohmann::json{{"version", 99}}).controls.at("a").x == .90);
        // Geometry keeps a fully visible hit box on both portrait and wide layouts.
        for (const auto& [dw, dh] : {std::pair{720.0, 1280.0}, std::pair{2560.0, 720.0}}) {
            for (const auto& c : touch_pad::controls()) {
                const auto [px, py, side] = touch_pad::geometry(s, c.id, dw, dh);
                CHECK(px >= side / 2 && py >= side / 2 && px + side / 2 <= dw && py + side / 2 <= dh);
            }
        }
        // The Lua viewport took an obsolete fifth scale argument ("integer",
        // "fit"); the C++ rectangle has only the fixed Fit form.
        auto r = viewport::rectangle(1920, 1440, 320, 240);
        CHECK(r->width == 1920 && r->height == 1440);
        {
            const auto native = viewport::to_native(r, 2.5, 2.5).value();
            NEAR(native.x, 0); NEAR(native.y, 239);
        }
        r = viewport::rectangle(1920, 1080, 320, 240);
        CHECK(r->x == 240 && r->y == 0 && r->width == 1440 && r->height == 1080);
        // Even an obsolete integer request cannot change the fixed Fit viewport.
        const auto fit = viewport::rectangle(1920, 1080, 320, 240);
        CHECK(fit->x == r->x && fit->y == r->y && fit->width == r->width && fit->height == r->height);
        CHECK(!viewport::to_native(r, 239, 60));
        CHECK(!viewport::to_native(r, 1680, 60));
        r = viewport::rectangle(1000, 700, 320, 240);
        CHECK(r->width == 933 && r->height == 700);
        CHECK(!viewport::rectangle(0, 700, 320, 240));
    }

    // Exercise the actual GUI save/error control flow, independently of the engine.
    const double w = design_width, h = design_height;
    MockNative native;
    Config config;
    MockStore store;
    Shared shared;
    auto& guest = native.guest;
    auto& g = shared.gestures;
    const auto gread = [&] { return read(shared.gestures, native.guest, false); };
    guest.running = true; guest.paused = false; guest.fields = 50;
    shared.pad = {};
    double pad_ax = 0, pad_ay = 0;
    {
        GuiScript scene(shared, native, config, store);
        scene.init();
        CHECK(!shared.pad.enabled); // rejected overlay is opt-in only
        scene.on_input("touch_toggle", pressed_event());
        scene.on_input("touch_edit", pressed_event());
        CHECK(shared.pad.editing);
        store.save_result = MockStore::Save::fail;
        scene.on_input("touch_edit", pressed_event());
        CHECK(shared.pad.editing && has(scene.notice, "failed"));
        CHECK(native.warnings.size() == 1 && has(native.warnings[0], "Layout save failed"));  // a failure is not a trace
        store.save_result = MockStore::Save::raise;
        scene.on_input("touch_edit", pressed_event());
        CHECK(shared.pad.editing && has(scene.notice, "disk full"));
        store.save_result = MockStore::Save::ok;
        scene.on_input("touch_edit", pressed_event());
        CHECK(!shared.pad.editing && scene.notice == "Touch layout saved");
        const auto ga = touch_pad::geometry(shared.pad, "a", w, h);
        pad_ax = ga.x; pad_ay = ga.y;
        { auto e = at_point(pad_ax, pad_ay); e.pressed = true; CHECK(scene.on_input("touch_mouse", e)); }
        CHECK(touch_pad::read(shared.pad, 50, w, h).buttons == 0x8000);
        guest.paused = true;
        scene.on_input(nullopt, at_point(pad_ax, pad_ay));
        CHECK(touch_pad::read(shared.pad, 50, w, h).buttons == 0 && shared.pad.pointers.empty());
        guest.paused = false;
        { auto e = at_point(pad_ax, pad_ay); e.pressed = true; scene.on_input("touch_multi", multi("1", e)); }
        CHECK(shared.pad.multi && shared.pad.pointers.contains("1") && !shared.pad.pointers.contains("mouse"));
        { auto e = at_point(pad_ax, pad_ay); e.pressed = true; scene.on_input("touch_mouse", e); }
        CHECK(!shared.pad.pointers.contains("mouse"));
        scene.final();
    }
    // The host's real merge forwards the touch state to the existing controller API.
    {
        native.size = {960, 720};
        HostScript host(shared, native, config);
        host.init();
        touch_pad::pointer(shared.pad, "9", Phase::down, pad_ax, pad_ay, 50, w, h);
        host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0x8000);
        host.window_event(WindowEvent::focus_lost);
        host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0);
        host.final();
        // Lua asserted `listener==nil` (final detached window.set_listener).
        // The C++ host has no listener: app.cpp dispatches window_event().
        // Substitute: final releases the host's cursor-hiding hook.
        CHECK(!native.cursor_hidden);
    }
    // Actual GUI gesture adapter uses physical screen coordinates on a HiDPI
    // window, consumes right click, and lets Continue A bypass a frozen cursor.
    shared.pad = {}; shared.gestures = {};
    guest.width = 320; guest.height = 240;
    native.size = {1920, 1440};
    {
        GuiScript pointer_scene(shared, native, config, store);
        pointer_scene.init();
        CHECK(!shared.pad.enabled);
        { auto e = at_point(661.5, 419, 1322.5, 837.5); e.pressed = true; pointer_scene.on_input("pointer_order", e); }
        CHECK(g.queue.size() == 1 && g.queue.at(0)->buttons == 0x4000);
        NEAR(g.queue.at(0)->x, 220); NEAR(g.queue.at(0)->y, 100);
        pointer_scene.on_input("pointer_commands", pressed_event());
        CHECK(g.radial);
        // Hidden panel: Continue (A) is the top-left of six entries around centre.
        { auto e = at_point(354, 386); e.released = true; pointer_scene.on_input("touch_mouse", e); }
        CHECK(g.pulse.value().buttons == 0x8000 && !g.radial);
        pointer_scene.on_input("pointer_commands", pressed_event());
        { auto e = at_point(606, 386); e.released = true; pointer_scene.on_input("touch_mouse", e); } // Back (B)
        CHECK(g.pulse.value().buttons == 0x4000 && !g.radial);
        pointer_scene.on_input("pointer_commands", pressed_event());
        { auto e = at_point(480, 334); e.released = true; pointer_scene.on_input("touch_mouse", e); } // Menu down
        CHECK(g.pulse.value().buttons == 0x0400 && !g.radial);
        // Corner buttons: the sandwich sends Start, the map button Z+L, including a
        // tap in the letterbox beside the image. Drawing must not fail.
        guest.ui_mode = "game"; guest.ui_field = guest.fields; guest.running = true;
        clear(g);
        const auto window_point = [](double nx, double ny) { // inverse of viewport::to_native_raw at 1920x1440
            return std::pair{(nx + .5) * 6 - .5, (239.5 - ny) * 6 - .5};
        };
        auto [bx, by] = window_point(8, 8);
        auto press = at_point(bx / 2, by / 2 + 1, bx, by + 1);
        press.pressed = true;
        CHECK(pointer_scene.on_input("touch_mouse", press));
        pointer_scene.update(1.0 / 60);
        press.pressed = false; press.released = true;
        CHECK(pointer_scene.on_input("touch_mouse", press));
        CHECK(g.pulse.value().buttons == 0x1000 && g.queue.empty());
        std::tie(bx, by) = window_point(8, 230);
        press = at_point(bx / 2, by / 2 + 1, bx, by + 1);
        press.pressed = true;
        guest.fields = guest.fields + 20; guest.ui_field = guest.fields; clear(g);
        CHECK(pointer_scene.on_input("touch_mouse", press));
        press.pressed = false; press.released = true;
        CHECK(pointer_scene.on_input("touch_mouse", press));
        CHECK(g.pulse.value().buttons == 0x2020);
        // Encyclopedia buttons draw (chevron rotations) and dispatch through the GUI.
        guest.ui_mode = "menu"; guest.ui_token = 264; guest.ui_field = guest.fields;
        guest.width = 640; guest.height = 480;
        clear(g);
        pointer_scene.update(1.0 / 60);
        bx = (2 * 312 + .5) * 3 - .5; by = (479.5 - 2 * 232) * 3 - .5; // 640x480 image at 1920x1440
        press = at_point(bx / 2, by / 2 + 1, bx, by + 1);
        press.pressed = true;
        CHECK(pointer_scene.on_input("touch_mouse", press));
        press.pressed = false; press.released = true;
        CHECK(pointer_scene.on_input("touch_mouse", press));
        CHECK(g.pulse.value().buttons == 0x0100);
        guest.width = 320; guest.height = 240; guest.ui_token.reset();
        guest.ui_mode.reset(); guest.ui_field.reset(); guest.fields = 50; clear(g);
        pointer_scene.final();
    }
    // Actual GUI dispatch for a locked message, including physical HiDPI units.
    shared.pad = {}; shared.gestures = {};
    guest.ui_mode = "message"; guest.ui_token = 123; guest.ui_field = 50;
    guest.cursor_valid = false;
    {
        GuiScript modal_scene(shared, native, config, store);
        modal_scene.init();
        { auto e = at_point(480, 360, 962.5, 718.5); e.pressed = true; modal_scene.on_input("touch_mouse", e); }
        { auto e = at_point(480, 360, 962.5, 718.5); e.released = true; modal_scene.on_input("touch_mouse", e); }
        CHECK(gread().buttons == 0);
        guest.fields = 110; guest.ui_field = 110;
        CHECK(gread().buttons == 0x8000);
        guest.paused = true; modal_scene.on_input(nullopt, at_point(480, 360));
        CHECK(!g.ui);
        guest.paused = false;
        guest.ui_mode = "menu"; guest.ui_token = 256; guest.ui_field = 110;
        guest.width = 640; guest.height = 480;
        guest.ui_items = {UiItem{.id = 0, .group = 1, .x0 = 240, .y0 = 36, .x1 = 410, .y1 = 66, .selected = true,
                                 .activate = true},
                          UiItem{.id = 2, .group = 1, .x0 = 240, .y0 = 73, .x1 = 410, .y1 = 100, .activate = true}};
        // Credits; item 1 (Two-Player) is blocked
        auto click = at_point(480, 594, 961, 1187); // native (320,84)
        click.pressed = true; modal_scene.on_input("touch_mouse", click);
        click.pressed = false; click.released = true; modal_scene.on_input("touch_mouse", click);
        CHECK(gread().buttons == 0x0400);
        modal_scene.final();
    }
    // Press feedback uses the same native bounds, projected through the HiDPI
    // viewport into GUI coordinates; cancellation immediately removes the overlay.
    shared.pad = {}; shared.gestures = {};
    guest.width = 320; guest.height = 240; guest.ui_mode = "game";
    guest.ui_token = 3; guest.ui_field = 200; guest.fields = 200;
    const UiItem hint_item{.id = 1, .group = 4, .x0 = 195, .y0 = 178, .x1 = 240, .y1 = 195, .buttons = 2,
                           .context = 123, .label = 5};
    guest.ui_items = {hint_item};
    {
        GuiScript hud_scene(shared, native, config, store);
        hud_scene.init();
        check_edge_layout(hud_scene);
        auto hud_contact = at_point(660, 165, 1322.5, 327.5);
        hud_contact.pressed = true;
        CHECK(hud_scene.on_input("touch_mouse", hud_contact));
        hud_scene.update(1.0 / 60);
        // The highlight is an outline: four edge boxes around the 135x51 hint rectangle.
        const auto outline = [](const GuiScript& scene) {
            double x0 = std::numeric_limits<double>::infinity(), x1 = -x0, y0 = x0, y1 = -x0;
            for (const auto* edge : edges(scene)) {
                CHECK(edge->enabled);
                x0 = std::min(x0, edge->x - edge->width / 2); x1 = std::max(x1, edge->x + edge->width / 2);
                y0 = std::min(y0, edge->y - edge->height / 2); y1 = std::max(y1, edge->y + edge->height / 2);
            }
            return std::array<double, 4>{x0, x1, y0, y1};
        };
        CHECK(edges(hud_scene).size() == 4);
        {
            const auto [x0, x1, y0, y1] = outline(hud_scene);
            NEAR(x0, 585); NEAR(x1, 720); NEAR(y0, 135); NEAR(y1, 186);
            NEAR(edges(hud_scene)[0]->width, 135); CHECK(edges(hud_scene)[0]->height < 10);
        }
        guest.ui_items = {}; hud_scene.update(1.0 / 60);
        for (const auto* edge : edges(hud_scene)) CHECK(!edge->enabled);
        guest.ui_items = {hint_item};
        clear(g);
        CHECK(hud_scene.on_input("pointer_pan", hud_contact));
        CHECK(g.pan && !g.ui); // middle drag over a hint stays camera input
        guest.cursor_valid = true; guest.cursor_field = guest.fields;
        guest.camera_valid = true; guest.camera_x = 400; guest.camera_y = 400;
        guest.camera_min_x = 0; guest.camera_max_x = 1000; guest.camera_min_y = 0; guest.camera_max_y = 1000;
        guest.camera_divisor = 4;
        // Pointer movement is dispatched with no action ID, including while the middle
        // button is held. It must keep updating the camera over hints and HUD padding.
        const double pan_x = g.pan.value().origin_x, pan_y = g.pan.value().origin_y;
        auto hud_move = at_point(690, 165, 1382.5, 327.5);
        CHECK(hud_scene.on_input(nullopt, hud_move)); // native (230,185), inside the hint
        NEAR(g.pan.value().x, 230); NEAR(g.pan.value().y, 185);
        {
            const auto p = gread();
            CHECK(p.buttons == 0x2000 && p.x < 0 && !g.ui);
        }
        guest.ui_context = 123; guest.ui_bounds = Bounds{176, 160, 312, 220, 123};
        hud_move = at_point(900, 75, 1802.5, 147.5);
        CHECK(hud_scene.on_input(nullopt, hud_move)); // native (300,215), transparent padding
        NEAR(g.pan.value().x, 300); NEAR(g.pan.value().y, 215);
        guest.fields = guest.fields + 8; guest.cursor_field = guest.fields;
        {
            const auto p = gread();
            CHECK(p.buttons == 0x2000 && p.x < 0 && p.y > 0 && !g.ui);
        }
        NEAR(g.pan.value().origin_x, pan_x); NEAR(g.pan.value().origin_y, pan_y);
        hud_move.released = true;
        CHECK(hud_scene.on_input("pointer_pan", hud_move));
        CHECK(!g.pan && gread().buttons == 0);
        // A mode change must still route through the modal handler and stop the pan.
        CHECK(hud_scene.on_input("pointer_pan", hud_contact));
        guest.ui_mode = "message"; guest.ui_token = 123;
        hud_move.released = false;
        CHECK(hud_scene.on_input(nullopt, hud_move));
        CHECK(gread().buttons == 0 && !g.pan);
        guest.ui_mode = "game"; guest.ui_token = 3; guest.ui_bounds.reset();
        hud_scene.final();
    }
    // Exercise real GUI dispatch: moving onto the HUD must stop map hover before
    // either a hint or its transparent padding receives a mouse/touch press.
    shared.pad = {}; shared.gestures = {};
    guest.fields = 300; guest.ui_field = 300; guest.cursor_field = 300;
    guest.ui_context = 123; guest.cursor_valid = true; guest.cursor_x = 100; guest.cursor_y = 64;
    guest.ui_bounds = Bounds{176, 160, 312, 220, 123};
    {
        GuiScript routed(shared, native, config, store);
        routed.init();
        set_target(g, 100, 64);
        auto hover = at_point(660, 165, 1322.5, 327.5);
        CHECK(routed.on_input(nullopt, hover) && !g.hover);
        routed.update(1.0 / 60); CHECK(edges(routed)[0]->enabled && edges(routed)[0]->color[3] == .3);
        hover.pressed = true; CHECK(routed.on_input("touch_mouse", hover));
        routed.update(1.0 / 60); CHECK(edges(routed)[0]->color[3] == .6);
        hover.pressed = false; hover.released = true; CHECK(routed.on_input("touch_mouse", hover));
        {
            const auto p = gread();
            CHECK(p.buttons == 2 && p.x == 0 && p.y == 0 && g.queue.empty());
        }
        clear(g);
        auto padding = at_point(900, 75, 1802.5, 147.5);
        padding.pressed = true;
        CHECK(routed.on_input("touch_mouse", padding));
        padding.pressed = false; padding.released = true; CHECK(routed.on_input("touch_mouse", padding));
        CHECK(gread().buttons == 0 && g.queue.empty());
        // A real touch release must remove feedback rather than leave mouse hover.
        clear(g);
        auto hint_touch = at_point(660, 165, 1322.5, 327.5);
        hint_touch.pressed = true;
        CHECK(routed.on_input("touch_multi", multi("8", hint_touch)));
        hint_touch.pressed = false; hint_touch.released = true;
        CHECK(routed.on_input("touch_multi", multi("8", hint_touch)));
        CHECK(!g.hud_mouse);
        clear(g);
        // Actual GUI routing: hints/padding may own taps, never sliding map gestures.
        const auto previous_divisor = guest.cursor_divisor;
        guest.cursor_divisor = 200;
        const auto fresh_field = [&] {
            guest.fields = guest.fields + 1;
            guest.ui_field = guest.fields; guest.cursor_field = guest.fields;
        };
        const auto route_native = [&](int id, std::string_view phase, double x, double y, bool touch) {
            fresh_field();
            auto a = at_point(x * 3, (240 - y) * 3, x * 6 + 2.5, (240 - y) * 6 - 2.5);
            a.pressed = phase == "down"; a.released = phase == "up";
            if (touch) return routed.on_input("touch_multi", multi(std::to_string(id), a));
            shared.pad.multi = false;
            return routed.on_input(phase == "move" ? std::optional<std::string>() : std::optional<std::string>(
                "touch_mouse"), a);
        };
        guest.ui_bounds = Bounds{176, 160, 312, 220, 123};
        using Points = std::array<std::array<double, 2>, 2>;
        for (const Points& points : {Points{{{100, 100}, {220, 185}}}, Points{{{220, 185}, {100, 100}}},
                                     Points{{{220, 185}, {300, 215}}}, Points{{{300, 215}, {220, 185}}}}) {
            clear(g);
            CHECK(route_native(1, "down", points[0][0], points[0][1], true));
            CHECK(route_native(2, "down", points[1][0], points[1][1], true));
            CHECK(g.two && !g.ui && !g.hud_mouse);
            CHECK(route_native(1, "move", points[0][0] - 20, points[0][1], true));
            CHECK(route_native(2, "move", points[1][0] - 20, points[1][1], true));
            const auto p = gread();
            CHECK(p.buttons == 0x2000 && p.x > 0 && !ui_feedback(g, guest));
            CHECK(route_native(1, "up", points[0][0] - 20, points[0][1], true));
            CHECK(gread().buttons == 0);
            CHECK(route_native(2, "up", points[1][0] - 20, points[1][1], true));
            CHECK(gread().buttons == 0 && g.queue.empty());
        }
        for (const bool touch : {true, false}) {
            for (const Points& points : {Points{{{100, 100}, {220, 185}}}, Points{{{220, 185}, {100, 100}}},
                                         Points{{{220, 185}, {230, 185}}}, Points{{{300, 215}, {220, 185}}}}) {
                clear(g);
                guest.cursor_x = points[0][0]; guest.cursor_y = points[0][1];
                CHECK(route_native(1, "down", points[0][0], points[0][1], touch));
                CHECK(route_native(1, "move", points[1][0], points[1][1], touch));
                CHECK(g.drag && !g.ui);
                CHECK(!ui_feedback(g, guest));
                CHECK(gread().buttons == 0);
                fresh_field(); CHECK(gread().buttons == 0x8000);
                fresh_field(); CHECK(gread().buttons == 0x8000);
                CHECK(route_native(1, "up", points[1][0], points[1][1], touch));
                guest.cursor_x = points[1][0]; guest.cursor_y = points[1][1];
                CHECK(gread().buttons == 0x8000);
                // A is released once a snapshot postdates the last steering input.
                bool released = false;
                for (int i = 1; i <= int(servo.arrival_margin) + 2; ++i) {
                    fresh_field();
                    const auto b = gread().buttons;
                    if (b == 0) { released = true; break; }
                    CHECK(b == 0x8000);
                }
                CHECK(released && !g.drag && !g.ui && g.queue.empty());
            }
        }
        // A stationary two-finger HUD press cancels the hint without ordering terrain.
        clear(g);
        route_native(1, "down", 220, 185, true); route_native(2, "down", 230, 185, true);
        route_native(1, "up", 220, 185, true); route_native(2, "up", 230, 185, true);
        CHECK(gread().buttons == 0 && g.queue.empty());
        // Joining an already sliding HUD contact starts panning at its current point,
        // without spending the displacement that belonged to the selection gesture.
        clear(g);
        route_native(1, "down", 220, 185, true); route_native(1, "move", 200, 185, true);
        route_native(2, "down", 300, 215, true);
        NEAR(g.two.value().origin_x, 250); NEAR(g.two.value().origin_y, 200);
        CHECK(gread().buttons == 0 && g.queue.empty());
        // Small tap jitter still activates the original hint, without a selection drag.
        clear(g);
        route_native(1, "down", 220, 185, true); route_native(1, "move", 222, 185, true);
        route_native(1, "up", 222, 185, true);
        CHECK(gread().buttons == 2 && !g.drag);
        clear(g);
        guest.cursor_divisor = previous_divisor;
        guest.fields = 300; guest.ui_field = 300; guest.cursor_field = 300;
        guest.cursor_x = 100; guest.cursor_y = 64;
        // Actual touch dispatch outside the modal build panel uses the same replay.
        guest.ui_mode = "build"; guest.ui_token = 300;
        guest.ui_bounds = Bounds{232, 143, 319, 223}; guest.ui_items = {};
        auto map_touch = at_point(300, 528, 602.5, 1053.5);
        map_touch.pressed = true;
        CHECK(routed.on_input("touch_multi", multi("9", map_touch)));
        map_touch.pressed = false; map_touch.released = true;
        CHECK(routed.on_input("touch_multi", multi("9", map_touch)));
        CHECK(gread().buttons == 0x4000);
        guest.ui_mode = "game"; guest.ui_token = 3; guest.ui_bounds.reset();
        guest.fields = 318; guest.ui_field = 318; guest.cursor_field = 318;
        CHECK(gread().buttons == 0 && g.queue.size() == 1);
        NEAR(g.queue.at(0)->x, 100); NEAR(g.queue.at(0)->y, 64);
        routed.final();
    }
    std::puts("Touch ownership, layout and viewport regressions passed");

    // Final telemetry must agree with the real host merge and paused clamp.
    config.set("sc64.debug_input", "1");
    shared.pad = {}; shared.gestures = {};
    {
        HostScript telemetry_host(shared, native, config);
        telemetry_host.init();
        guest.ui_mode = "game"; guest.ui_token = 3; guest.ui_field = guest.fields;
        guest.ui_items = {UiItem{.id = 1, .group = 4, .x0 = 195, .y0 = 178, .x1 = 240, .y1 = 195, .buttons = 2,
                                 .context = 123}};
        ui_pointer(g, "1", Phase::down, 220, 185, guest);
        ui_pointer(g, "1", Phase::up, 220, 185, guest);
        // Lua fed pads.input(host.pads, ...) directly; HostScript's pad state is
        // private, so the same key-down goes through the host's own on_input.
        telemetry_host.on_input("a", pressed_event());
        telemetry_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 32768 && native.logged.value()["buttons"] == 32768
              && native.logged.value()["source"]["controller"] == true);
        // Lua pads.clear(host.pads): the focus-loss handler clears the same pad
        // state (touch pad and gestures are already empty here).
        CHECK(shared.pad.pointers.empty() && g.queue.empty() && !g.ui);
        telemetry_host.window_event(WindowEvent::focus_lost); guest.fields = guest.fields + 1;
        HostScript::RecipeItem item;
        item.field = guest.fields; item.duration = 10; item.buttons = 64; item.x = .25; item.y = -.5;
        telemetry_host.recipe = {item};
        telemetry_host.update(1.0 / 60);
        const auto& logged = native.logged.value();
        CHECK(native.submitted.value().bits == 64 && logged["buttons"] == 64 && logged["x"] == .25
              && logged["y"] == -.5);
        CHECK(logged["stage"] == "submitted" && logged["player"] == 1 && logged["source"]["recipe"] == true);
        guest.paused = true; telemetry_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0 && native.submitted.value().x == 0
              && native.logged.value()["buttons"] == 0 && native.logged.value()["x"] == 0);
        guest.paused = false;
        // Lua injected "a" into host.pads while input_active==false (bypassing
        // the host guard); inject it through on_input just before deactivation.
        guest.input_active.reset();
        telemetry_host.on_input("a", pressed_event());
        guest.input_active = false;
        enqueue(g, 100, 100, 0x8000);
        telemetry_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0 && native.submitted.value().x == 0 && g.queue.empty());
        telemetry_host.on_input("a", pressed_event());
        // Lua: assert(pads.read(host.pads,1,fields)==0). The pad state is private;
        // the next update runs with input active and no recipe, so a held "a"
        // would submit 0x8000 there — the assertion below covers this one.
        guest.input_active = true; telemetry_host.recipe.clear();
        telemetry_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0 && native.submitted.value().x == 0);
        guest.input_active.reset(); telemetry_host.final();
    }
    std::puts("Final controller telemetry matches keyboard ownership, recipe merge and pause");

    // Back aliases traverse the GUI focus layer and actual host merge. A pending
    // HUD/terrain action must not confirm a selection after original B closes it.
    shared.pad = {}; shared.gestures = {};
    guest.running = true; guest.paused = false; guest.fields = 400;
    {
        GuiScript back_gui(shared, native, config, store);
        back_gui.init();
        // Leave headroom so one more panel entry cannot silently break the GUI.
        CHECK(back_gui.nodes.size() + 8 <= GuiScript::max_nodes);
        HostScript back_host(shared, native, config);
        back_host.init();
        for (const char* id : {"escape_back", "android_back"}) {
            guest.ui_mode = "game"; guest.ui_field = guest.fields;
            g.radial = Radial{100, 100};
            enqueue(g, 120, 100, 0x8000);
            CHECK(!back_gui.on_input(std::string(id), pressed_event()));
            back_host.on_input(id, pressed_event());
            back_host.on_input(id, released_event());
            back_host.update(1.0 / 60);
            CHECK(native.submitted.value().bits == 0x1000 && native.submitted.value().x == 0
                  && native.submitted.value().y == 0);
            CHECK(!g.radial && g.queue.empty());
            guest.fields = guest.fields + 6;
            back_host.update(1.0 / 60);
            CHECK(native.submitted.value().bits == 0);
            guest.ui_mode = "modal"; guest.ui_field = guest.fields;
            back_host.on_input(id, pressed_event()); back_host.on_input(id, released_event());
            back_host.update(1.0 / 60);
            CHECK(native.submitted.value().bits == 0x4000);
            guest.fields = guest.fields + 6; back_host.update(1.0 / 60);
            CHECK(native.submitted.value().bits == 0);
        }
        back_host.on_input("escape_back", pressed_event());
        back_host.window_event(WindowEvent::focus_lost);
        back_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0);
        back_host.final(); back_gui.final();
    }
    std::puts("Esc/Android Back open the mission menu, go back in modals and clear pointer actions");

    // Actual GUI dispatch at the 640x480 frontend resolution uses native carousel
    // and warning geometry, including the physical-pixel Y convention.
    shared.pad = {}; shared.gestures = {};
    native.size = {1920, 1440};
    guest.width = 640; guest.height = 480;
    guest.fields = 1600; guest.ui_field = 1600;
    guest.ui_mode = "menu"; guest.ui_token = 256;
    guest.ui_bounds.reset();
    guest.ui_carousel = Carousel{2, 60, 220, 580, 460, true};
    guest.ui_items = {UiItem{.id = 0, .group = 1, .selected = true, .activate = true},
                      UiItem{.id = 2, .group = 5, .x0 = 240, .y0 = 240, .x1 = 360, .y1 = 460, .selected = true}};
    {
        GuiScript menu_gui(shared, native, config, store);
        menu_gui.init();
        auto swipe_start = at_point(600.5, 254.5, 1201, 509);
        swipe_start.pressed = true;
        CHECK(menu_gui.on_input("touch_multi", multi("12", swipe_start))); // native (400,310)
        auto swipe_end = at_point(420.5, 254.5, 841, 509);
        swipe_end.released = true;
        CHECK(menu_gui.on_input("touch_multi", multi("12", swipe_end))); // native (280,310)
        CHECK(gread().buttons == 0x200 && g.queue.empty());
        clear(g); guest.fields = 1700; guest.ui_field = 1700;
        auto selected = at_point(480.5, 269.5, 961, 539);
        selected.pressed = true;
        CHECK(menu_gui.on_input("touch_multi", multi("13", selected)));
        selected.pressed = false; selected.released = true;
        CHECK(menu_gui.on_input("touch_multi", multi("13", selected)));
        CHECK(gread().buttons == 0x8000);
        guest.ui_items.push_back(UiItem{.id = 1, .group = 5, .x0 = 60, .y0 = 240, .x1 = 180, .y1 = 460,
                                        .selected = false});
        guest.ui_items.push_back(UiItem{.id = 3, .group = 5, .x0 = 450, .y0 = 240, .x1 = 580, .y1 = 460,
                                        .selected = false});
        clear(g); guest.fields = 1720; guest.ui_field = 1720; shared.pad.multi = false;
        auto side = at_point(150.5, 224.5, 301, 449); // native (100,330)
        side.pressed = true;
        CHECK(menu_gui.on_input("touch_mouse", side));
        side.pressed = false; side.released = true; CHECK(menu_gui.on_input("touch_mouse", side));
        CHECK(gread().buttons == 0x100 && g.ui->target.value().id == 1);
        clear(g); guest.fields = 1740; guest.ui_field = 1740;
        side = at_point(750.5, 224.5, 1501, 449); // native (500,330)
        side.pressed = true;
        CHECK(menu_gui.on_input("touch_multi", multi("14", side)));
        side.pressed = false; side.released = true; CHECK(menu_gui.on_input("touch_multi", multi("14", side)));
        CHECK(gread().buttons == 0x200 && g.ui->target.value().id == 3);
        // The seam uses the same GUI path for mouse and touch, one pulse then arrival.
        struct SeamCase { double from, to; unsigned bits; const char* input; };
        for (const auto& c : {SeamCase{0, 5, 0x100, "touch_mouse"}, SeamCase{5, 0, 0x200, "touch_multi"}}) {
            clear(g); guest.fields = 1760; guest.ui_field = 1760; shared.pad.multi = false;
            guest.ui_carousel->selected = c.from;
            guest.ui_items = {UiItem{.id = 0, .group = 1, .selected = true, .activate = true},
                              UiItem{.id = c.from, .group = 5, .x0 = 240, .y0 = 240, .x1 = 360, .y1 = 460,
                                     .selected = true},
                              UiItem{.id = c.to, .group = 5, .x0 = 60, .y0 = 240, .x1 = 180, .y1 = 460,
                                     .selected = false}};
            side = at_point(150.5, 224.5, 301, 449);
            side.pressed = true;
            const std::string input = c.input;
            const auto dispatch = [&] {
                return menu_gui.on_input(input, input == "touch_multi" ? multi("15", side) : side);
            };
            CHECK(dispatch()); side.pressed = false; side.released = true; CHECK(dispatch());
            CHECK(gread().buttons == c.bits);
            guest.fields = 1860; guest.ui_field = 1860; guest.ui_carousel->selected = c.to;
            guest.ui_items.at(1).selected = false; guest.ui_items.at(2).selected = true;
            CHECK(gread().buttons == 0 && g.ui && !g.ui->target);
        }
        // ADR-067 timing through actual GUI dispatch: a row two steps away emits two
        // 4-field Down pulses separated by neutral and fresh acknowledgement, then A.
        // A repeated tap during a pulse or its neutral interval is ignored.
        for (const std::string input_id : {"touch_mouse", "touch_multi"}) {
            clear(g); shared.pad.multi = false;
            guest.ui_mode = "menu"; guest.ui_token = 262; guest.ui_carousel.reset();
            const auto rows = [&](double selected_row) {
                guest.ui_items.clear();
                for (int id = 0; id <= 3; ++id)
                    guest.ui_items.push_back(UiItem{.id = double(id), .group = 3, .x0 = 124, .y0 = 134.0 + id * 24,
                                                    .x1 = 282, .y1 = 157.0 + id * 24, .selected = id == selected_row,
                                                    .activate = true});
            };
            rows(0); guest.fields = 2000; guest.ui_field = 1997;
            const auto tap = [&](double nx, double ny) {
                InputEvent e;
                e.screen_x = 3 * nx + 1; e.screen_y = 1440 - 3 * ny - 1; e.pressed = true;
                e.x = *e.screen_x / 2; e.y = *e.screen_y / 2;
                const auto dispatch = [&] {
                    return menu_gui.on_input(input_id, input_id == "touch_multi" ? multi("16", e) : e);
                };
                CHECK(dispatch()); e.pressed = false; e.released = true; CHECK(dispatch());
            };
            tap(200, 194); // row 2
            std::string seq;
            double selected_row = 0;
            for (int at = 2000; at <= 2040; ++at) {
                guest.fields = at; guest.ui_field = at - 3;
                if (at == 2006) rows(1); // first move visible 6 fields after the press
                if (at == 2003) tap(200, 170); // fast second tap is ignored
                if (at >= 2014 && selected_row == 0) { rows(2); selected_row = 2; }
                seq += (seq.empty() ? "" : ",") + std::to_string(gread().buttons);
            }
            constexpr std::string_view expected =
                "1024,1024,1024,1024,0,0,0,0,1024,1024,1024,1024,0,0,0,0,32768";
            if (!seq.starts_with(expected)) std::fprintf(stderr, "%s\n", seq.c_str());
            CHECK(seq.starts_with(expected));
            CHECK(g.ui && !g.ui->target);
        }
        guest.ui_mode = "campaign_warning"; guest.ui_token = 900;
        guest.ui_bounds = Bounds{160, 222, 480, 388}; guest.ui_carousel.reset(); guest.ui_items = {};
        clear(g); guest.fields = 1800; guest.ui_field = 1800;
        selected.released = false; selected.pressed = true;
        CHECK(menu_gui.on_input("touch_multi", multi("13", selected)));
        selected.pressed = false; selected.released = true;
        CHECK(menu_gui.on_input("touch_multi", multi("13", selected)));
        CHECK(gread().buttons == 0x1000);
        clear(g); guest.fields = 1900; guest.ui_field = 1900;
        shared.pad.multi = false;
        auto outside = at_point(75.5, 569.5, 151, 1139); // native (50,100)
        outside.pressed = true;
        CHECK(menu_gui.on_input("touch_mouse", outside));
        outside.pressed = false; outside.released = true;
        CHECK(menu_gui.on_input("touch_mouse", outside));
        CHECK(gread().buttons == 0x4000 && g.queue.empty());
        for (const std::string input_id : {"touch_mouse", "touch_multi"}) {
            clear(g); shared.pad.multi = false;
            guest.fields = 1950; guest.ui_field = 1950;
            guest.ui_mode = "results"; guest.ui_token = 700; guest.ui_bounds.reset();
            guest.ui_items = {};
            const double tab_edges[] = {232, 373, 445, 521, 592};
            for (int tab = 0; tab <= 3; ++tab)
                guest.ui_items.push_back(UiItem{.id = double(tab), .group = 6, .x0 = tab_edges[tab], .y0 = 44,
                                                .x1 = tab_edges[tab + 1] - 1, .y1 = 72, .selected = tab == 0,
                                                .horizontal = true, .activate = false});
            auto a = at_point(600.5, 634.5, 1201, 1269); // native (400,58)
            a.pressed = true;
            const auto result_dispatch = [&] {
                return menu_gui.on_input(input_id, input_id == "touch_multi" ? multi("16", a) : a);
            };
            CHECK(result_dispatch()); a.pressed = false; a.released = true; CHECK(result_dispatch());
            CHECK(gread().buttons == 0x100 && g.queue.empty());
            clear(g); guest.fields = 1980; guest.ui_field = 1980;
            a = at_point(150.5, 419.5, 301, 839); // native (100,200)
            a.pressed = true;
            CHECK(result_dispatch()); a.pressed = false; a.released = true; CHECK(result_dispatch());
            CHECK(gread().buttons == 0x8000 && g.queue.empty());
        }
        menu_gui.final();
    }
    std::puts("Frontend touch swipes, selected entry and mouse warning routing pass GUI dispatch");

    // Real GUI dispatch across a grey build cell must continue to the requested
    // upper-left cell, for mouse and physical touch coordinate conventions alike.
    {
        native.size = {1920, 1440};
        guest.width = 320; guest.height = 240;
        guest.ui_mode = "build"; guest.ui_token = 300; guest.ui_carousel.reset();
        guest.ui_bounds = Bounds{232, 143, 319, 223};
        for (const std::string input_id : {"touch_mouse", "touch_multi"}) {
            shared.pad = {}; shared.gestures = {};
            guest.fields = 2000; guest.ui_field = 2000;
            guest.ui_items = {};
            for (int id = 0; id <= 8; ++id) {
                const double x0 = 237 + 24 * (id % 3), y0 = 150 + 24 * (id / 3);
                guest.ui_items.push_back(UiItem{.id = double(id), .group = 1, .x0 = x0, .y0 = y0, .x1 = x0 + 19,
                                                .y1 = y0 + 19, .columns = 3, .selected = id == 4,
                                                .activate = id != 3});
            }
            GuiScript scene(shared, native, config, store);
            scene.init();
            auto contact = at_point(739.25, 241.75, 1478.5, 483.5);
            contact.pressed = true;
            const auto dispatch = [&] {
                return scene.on_input(input_id, input_id == "touch_multi" ? multi("21", contact) : contact);
            };
            CHECK(dispatch()); contact.pressed = false; contact.released = true; CHECK(dispatch());
            auto p = gread();
            CHECK(p.buttons == 0 && p.x < 0 && p.y == 0);
            guest.fields = 2020; guest.ui_field = 2020;
            guest.ui_items.at(4).selected = false; guest.ui_items.at(3).selected = true; // disabled middle-left
            p = gread(); CHECK(p.buttons == 0 && p.x == 0 && p.y > 0);
            guest.fields = 2040; guest.ui_field = 2040;
            guest.ui_items.at(3).selected = false; guest.ui_items.at(0).selected = true;
            CHECK(gread().buttons == 0x8000);
            CHECK(g.queue.empty()); // never a map click under the panel
            scene.final();
        }
    }
    std::puts("GUI mouse/touch build selection crosses disabled cells");

    // The production Android MULTI_TOUCH path passes fresh selection metadata to
    // the tap coalescer and ignores the duplicate emulated mouse event.
    {
        shared.pad = {}; shared.gestures = {};
        guest.running = true; guest.paused = false; guest.input_active = true;
        guest.ui_mode = "game"; guest.ui_token = 3; guest.ui_context = 123;
        guest.width = 320; guest.height = 240; guest.ui_items = {}; guest.ui_bounds.reset();
        guest.pick_required = true; guest.cursor_valid = true; guest.cursor_divisor = 200;
        guest.cursor_x = 100; guest.cursor_y = 80;
        guest.cursor_pick = CursorPick{.x = 100, .y = 80, .context = 123, .buttons = 0x8000};
        guest.fields = 2300; guest.ui_field = 2300; guest.cursor_field = 2300;
        GuiScript scene(shared, native, config, store);
        scene.init();
        auto a = at_point(300, 480, 602.5, 957.5);
        a.pressed = true;
        CHECK(scene.on_input("touch_multi", multi("31", a)));
        a.pressed = false; a.released = true;
        CHECK(scene.on_input("touch_multi", multi("31", a)));
        CHECK(gread().buttons == 0 && g.tap);
        for (int f = 2301; f <= 2309; ++f) {
            guest.fields = f; guest.ui_field = f; guest.cursor_field = f;
            CHECK(gread().buttons == 0);
        }
        guest.fields = 2310; guest.ui_field = 2310; guest.cursor_field = 2310;
        a.pressed = true; a.released = false; // Lua a.id=32
        CHECK(scene.on_input("touch_multi", multi("32", a)));
        scene.on_input("touch_mouse", a);
        CHECK(gread().buttons == 0 && !g.contacts.contains("mouse"));
        a.pressed = false; a.released = true;
        CHECK(scene.on_input("touch_multi", multi("32", a)));
        CHECK(gread().buttons == 0);
        guest.fields = 2311; guest.ui_field = 2311; guest.cursor_field = 2311;
        CHECK(gread().buttons == 0x4000);
        CHECK(!g.drag && g.queue.empty());
        scene.final();
    }
    std::puts("Android GUI double-tap routing suppresses selection and duplicate mouse input");

    // The real host imports by a click or tap on a start-screen button and handles asynchronous SAF output.
    // choose_rom() only starts the picker in both versions; its result arrives
    // through poll_import() (started, or the error message).
    shared.pad = {}; shared.gestures = {};
    guest.running = false; guest.paused = false; guest.input_active = true; guest.presented = 0; guest.error.clear();
    native.selected = 0; native.load_path.reset();
    native.stored = "private/sc64-ntsc-u.z64";
    {
        HostScript importer(shared, native, config);
        importer.init();
        CHECK(native.load_path == "private/sc64-ntsc-u.z64");
        // Only the buttons choose; the rest of the start screen does nothing.
        LauncherScript start(shared, native, false);
        start.update();
        CHECK(shared.stage == Shared::Stage::working && shared.buttons.empty());  // the stored ROM is being started
        shared.stage = Shared::Stage::idle; shared.message.clear();
        start.update();
        CHECK(shared.buttons.size() == 1 && shared.buttons[0].action == LauncherButton::Action::choose_rom);
        const auto rom_button = shared.buttons[0];
        auto at = [](double x, double y) { InputEvent e = pressed_event(); e.x = x; e.y = y; return e; };
        importer.on_input("touch_mouse", at(10, 10)); CHECK(native.selected == 0);
        importer.on_input("touch_mouse", pressed_event()); CHECK(native.selected == 0);  // no position
        importer.on_input("touch_mouse", at(rom_button.x, rom_button.y)); CHECK(native.selected == 1);
        // The pressed look holds while the contact does and for a moment after a short click.
        CHECK(shared.pressed == LauncherButton::Action::choose_rom);
        importer.update(1.0); CHECK(shared.pressed && !shared.press_released);
        { InputEvent e; e.released = true; importer.on_input("touch_mouse", e); }
        importer.update(0.001); CHECK(!shared.pressed);  // held for long: the look ends with the contact
        importer.on_input("touch_mouse", at(rom_button.x, rom_button.y)); CHECK(native.selected == 2);
        { InputEvent e; e.released = true; importer.on_input("touch_mouse", e); }
        importer.update(pressed_look_seconds / 3); CHECK(shared.pressed);  // a quick click is still seen
        importer.update(pressed_look_seconds); CHECK(!shared.pressed);
        native.selected = 1;
        importer.on_input("touch_mouse", at(rom_button.x + rom_button.width / 2 + 1, rom_button.y)); CHECK(native.selected == 1);
        { InputEvent e; e.touch.push_back(Touch{.id = "1", .pressed = true, .x = rom_button.x, .y = rom_button.y}); importer.on_input("touch_multi", e); }
        CHECK(native.selected == 2);
        { InputEvent e; e.touch.push_back(Touch{.id = "1", .pressed = true, .x = 10, .y = 10}); importer.on_input("touch_multi", e); }
        CHECK(native.selected == 2);
        { InputEvent e; e.touch.push_back(Touch{.id = "1", .released = true, .x = rom_button.x, .y = rom_button.y}); importer.on_input("touch_multi", e); }
        CHECK(native.selected == 2);
        importer.on_input("open_rom", pressed_event()); CHECK(native.selected == 3);
        CHECK(shared.pressed && shared.press_released);  // a key press flashes the button for a moment
        importer.update(pressed_look_seconds * 2); CHECK(!shared.pressed);
        // Only the finger that pressed ends the look; another finger lifting does not.
        { InputEvent e; e.touch.push_back(Touch{.id = "7", .pressed = true, .x = rom_button.x, .y = rom_button.y}); importer.on_input("touch_multi", e); }
        CHECK(shared.pressed && shared.press_owner == "7");
        { InputEvent e; e.touch.push_back(Touch{.id = "8", .released = true}); importer.on_input("touch_multi", e); }
        importer.update(1.0); CHECK(shared.pressed && !shared.press_released);
        { InputEvent e; e.touch.push_back(Touch{.id = "7", .released = true}); importer.on_input("touch_multi", e); }
        importer.update(1.0); CHECK(!shared.pressed);
        // A release the picker swallowed cannot leave the look stuck: once the picker is open `choosing()` shows it.
        importer.on_input("touch_mouse", at(rom_button.x, rom_button.y));
        CHECK(shared.pressed);
        native.picking = LauncherButton::Action::choose_rom;
        importer.update(1.0 / 60); CHECK(!shared.pressed);
        native.picking.reset();
        // Losing the focus ends a press whose release may never arrive.
        importer.on_input("touch_mouse", at(rom_button.x, rom_button.y));
        CHECK(shared.pressed);
        importer.window_event(WindowEvent::focus_lost); CHECK(!shared.pressed);
        native.selected = 3;
        importer.on_input("open_module", pressed_event()); CHECK(native.module_selected == 0);  // nothing to choose here
        native.poll_result = false; importer.update(1.0 / 60);
        CHECK(shared.message == "invalid ROM" && shared.stage == Shared::Stage::failed);
        native.poll_result = true; importer.update(1.0 / 60);
        CHECK(shared.message == "Starting game..." && shared.stage == Shared::Stage::working);
        start.update();
        CHECK(shared.buttons.empty());  // the game is being started
        // Where the application cannot build the module itself, the user supplies it with a second button.
        shared.stage = Shared::Stage::idle; shared.message.clear();
        native.facts.module_selectable = true;
        start.update();
        CHECK(shared.buttons.size() == 2 && shared.buttons[1].action == LauncherButton::Action::choose_module);
        importer.on_input("touch_mouse", at(shared.buttons[1].x, shared.buttons[1].y)); CHECK(native.module_selected == 1);
        importer.on_input("open_module", pressed_event()); CHECK(native.module_selected == 2);
        guest.running = true;
        importer.on_input("touch_mouse", at(rom_button.x, rom_button.y)); CHECK(native.selected == 3);
        importer.on_input("open_module", pressed_event()); CHECK(native.module_selected == 2);
        guest.running = false; native.facts = {};
        importer.final();
    }
    std::puts("Start-screen ROM/module choice, stored-ROM restart and asynchronous error routing passed");

    // The telemetry snapshot is only built (and its audio timing consumed) while it is written.
    {
        auto telemetry_lines = [&] { return std::count_if(native.logs.begin(), native.logs.end(), [](const std::string& line) { return line.starts_with("SC64 {"); }); };
        native.logs.clear(); native.trace = false;
        HostScript quiet(shared, native, config);
        quiet.init(); quiet.update(1.0 / 60); quiet.update(6);
        CHECK(telemetry_lines() == 0);
        native.trace = true;
        quiet.update(6);
        CHECK(telemetry_lines() == 1);
        quiet.final();
    }
    std::puts("Telemetry is built only while it is written");

    // Bundled APK startup takes priority over an older imported ROM and surfaces
    // verification/read errors without opening the picker or using the disk path.
    config.set("sc64.bundled_rom", "1");
    native.load_path.reset(); native.selected = 0;
    {
        HostScript bundled(shared, native, config);
        bundled.init();
        CHECK(native.bundled_calls == 1 && !native.load_path && native.selected == 0);
        CHECK(shared.message == "Starting game...");
        bundled.final();
    }
    native.bundled_error = "Bundled ROM SHA-1 mismatch";
    {
        HostScript bundled(shared, native, config);
        bundled.init();
        CHECK(!native.load_path && native.selected == 0);
        CHECK(shared.message == "Bundled ROM SHA-1 mismatch");
        bundled.final();
    }
    config.set("sc64.bundled_rom", "0");
    std::puts("Bundled-ROM autostart, imported-ROM precedence and error routing passed");

    // Later fixtures run without the debug-input telemetry configuration.
    config.set("sc64.debug_input", "0");

    // An idle connected gamepad hides only the diagnostic pad: pointer gestures
    // still route through the real GUI and host, while actual pad input owns them.
    {
        shared.pad = {}; shared.gestures = {};
        guest.running = true; guest.paused = false; guest.input_active = true;
        guest.width = 320; guest.height = 240; guest.ui_mode = "game"; guest.ui_token = 3;
        guest.ui_items = {}; guest.ui_bounds.reset(); guest.minimap.reset();
        guest.fields = 2400; guest.ui_field = 2400; guest.cursor_field = 2400;
        native.size = {1920, 1440};
        GuiScript pad_gui(shared, native, config, store);
        pad_gui.init();
        HostScript pad_host(shared, native, config);
        pad_host.init();
        pad_host.on_input("connected", gamepad_event(0));
        CHECK(!shared.pad.physical.empty() && !touch_pad::visible(shared.pad));
        pad_host.update(1.0 / 60);
        CHECK(g.enabled);
        {
            auto e = at_point(661.5, 419, 1322.5, 837.5);
            e.pressed = true;
            CHECK(pad_gui.on_input("pointer_order", e));
        }
        CHECK(g.queue.size() == 1 && g.queue.at(0)->buttons == 0x4000);
        pad_host.on_input("a", gamepad_event(0, true));
        pad_host.update(1.0 / 60);
        CHECK(native.submitted.value().bits == 0x8000 && g.queue.empty() && !g.active);
        pad_host.on_input("disconnected", gamepad_event(0));
        pad_host.final(); pad_gui.final();
    }
    std::puts("Connected idle gamepad leaves pointer gestures available");

    // P toggles only the manual pause of a running game; the status hint names
    // P only for that cause, not for focus-loss/iconify pauses.
    {
        native.pause_calls.clear();
        guest.running = false; guest.paused = false; guest.error = ""; guest.presented = 0;
        HostScript pause_host(shared, native, config);
        pause_host.init();
        pause_host.on_input("pause", pressed_event());
        CHECK(native.pause_calls.empty() && !shared.user_paused);
        guest.running = true;
        pause_host.on_input("pause", pressed_event());
        CHECK(native.pause_calls.at(0) == true && shared.user_paused);
        StatusScript status(shared, native);
        guest.paused = true; guest.presented = 1;
        status.update(); CHECK(status.node.text == "Paused \xE2\x80\x94 press P to resume");
        pause_host.on_input("pause", pressed_event());
        CHECK(native.pause_calls.at(1) == false && !shared.user_paused);
        status.update(); CHECK(status.node.text == "Paused"); // e.g. focus loss remains
        guest.paused = false; status.update(); CHECK(status.node.text.empty());
        pause_host.final();
    }
    std::puts("Manual pause toggle and pause-cause status hint passed");

    // The former render script was removed; Video::draw in
    // native/shell/video.cpp now owns presentation and is not unit-testable here.
    // Its Lua assertions are ported to the shared Fit geometry it consumes:
    // an empty (minimised) window yields no image rectangle, so nothing indexes
    // a nil viewport and the image is not drawn; a 1920x1080 window draws the
    // image through the shared Fit rectangle. The image-before-overlay draw
    // order is Video::draw's fixed sequence (image, then overlay nodes).
    {
        guest.width = 320; guest.height = 240;
        CHECK(!viewport::rectangle(0, 0, guest.width.value(), guest.height.value()));
        const auto fit = viewport::rectangle(1920, 1080, guest.width.value(), guest.height.value());
        CHECK(fit && fit->x == 240 && fit->y == 0 && fit->width == 1440 && fit->height == 1080);
    }
    std::puts("Render script empty-window guard and Fit viewport passed");
    // Host init enables configured haptics; pointer-submitted Start/B edges pulse.
    CHECK(native.haptics_enabled == true && !native.vibrations.empty());
    for (const auto& v : native.vibrations)
        CHECK((v.ms == 12 && v.amplitude == 90) || (v.ms == 22 && v.amplitude == 170));
    std::puts("Pointer haptics passed");
    return 0;
}
