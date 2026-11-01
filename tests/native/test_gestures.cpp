// One-to-one port of tests/test_gestures.lua. Lua nil indexing errors are
// mirrored with std::optional::value() and container at() so a missing value
// still fails the test instead of invoking undefined behaviour.
#include "gestures.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace sc64::input;

namespace {
constexpr double pi = std::numbers::pi;
bool has(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

// Raw original-like stick vector for a submitted analog value: the octagonal
// conversion, optionally limited to the unit circle.
std::pair<double, double> raw_stick(double sx, double sy, bool unit_limit) {
    const double angle = std::atan2(sy, sx);
    double octant = floored_mod(angle + 2 * pi, pi / 2);
    if (octant > pi / 4) octant = pi / 2 - octant;
    const double radius = 82 * std::sin(1.39414574) / std::sin(pi - octant - 1.39414574);
    const double length = std::sqrt(sx * sx + sy * sy);
    const double magnitude = (unit_limit ? std::min(1.0, length) : length) * radius;
    return {std::cos(angle) * magnitude, std::sin(angle) * magnitude};
}

// Free mouse hover must follow continuously, including changed targets and
// delayed snapshots; it cannot reuse the action servo's burst/settle cycle.
// Quantized original-like stick conversion is a fixture, not native proof.
std::pair<double, double> motion(double sx, double sy, double divisor) {
    const auto [rx, ry] = raw_stick(sx, sy, true);
    const auto delta = [divisor](double v) {
        const double raw = std::max(0.0, std::floor(std::abs(v)) - 1);
        return (v < 0 ? -1 : 1) * raw * raw / (2 * divisor);
    };
    return {delta(rx), -delta(ry)};
}

using History = std::map<int, std::pair<double, double>>;
}

int main() {
    GestureState s;
    Info info;
    info.fields = 0; info.cursor_field = 0; info.cursor_x = 160; info.cursor_y = 120; info.cursor_valid = true;
    const auto read_at = [&](double field, std::optional<double> x = {}, std::optional<double> y = {}) {
        info.fields = field; info.cursor_field = field;
        if (x) info.cursor_x = *x;
        if (y) info.cursor_y = *y;
        return read(s, info, false);
    };
    const auto bits_at = [&](double field, std::optional<double> x = {}, std::optional<double> y = {}) {
        return read_at(field, x, y).buttons;
    };
    assert(!enqueue(s, -1, 10, 0x8000));
    pointer(s, "1", Phase::down, 160, 120, 0, true);
    pointer(s, "1", Phase::up, 160, 120, 1, true);
    assert(bits_at(1) == 0); assert(bits_at(3) == 0x8000);
    assert(bits_at(10) == 0x8000); assert(bits_at(11) == 0); assert(bits_at(19) == 0);
    clear(s);
    enqueue(s, 200, 80, 0x4000);
    auto out = read_at(20);
    assert(out.buttons == 0 && out.x > 0 && out.y > 0); // top-left target: native Y positive is up
    assert(bits_at(22) == 0);
    read_at(24); assert(bits_at(30) == 0);
    assert(bits_at(32, 200, 80) == 0); assert(bits_at(34) == 0x4000);
    clear(s);
    // A drag waits for its start point, holds A while moving, releases at its end.
    pointer(s, "mouse", Phase::down, 200, 80, 40, false);
    pointer(s, "mouse", Phase::move, 240, 100, 41, false);
    pointer(s, "mouse", Phase::up, 240, 100, 42, false);
    assert(bits_at(42) == 0); assert(bits_at(44) == 0x8000);
    out = read_at(46); assert(out.buttons == 0x8000 && out.x > 0 && out.y < 0);
    read_at(50); assert(bits_at(58, 240, 100) == 0x8000);
    assert(bits_at(60) == 0 && !s.drag);
    assert(bits_at(68) == 0 && !s.hover);
    // Fresh but immobile feedback must not keep A held after physical release.
    // Both release-before-start and a long physically held drag use a release
    // deadline independent of s.active. This is a control fixture, not native proof.
    for (bool touch : {false, true}) for (bool early : {false, true}) {
        GestureState ds;
        Info di;
        di.fields = 1; di.cursor_field = 1; di.cursor_x = 100; di.cursor_y = 100; di.cursor_valid = true;
        const auto poll = [&](double f) {
            di.fields = f; di.cursor_field = f;
            return read(ds, di, false);
        };
        pointer(ds, "1", Phase::down, 100, 100, 0, touch);
        pointer(ds, "1", Phase::move, 200, 100, 1, touch);
        const double release = early ? 2 : 501;
        if (early) pointer(ds, "1", Phase::up, 200, 100, release, touch);
        assert(poll(2).buttons == 0 && poll(3).buttons == 0x8000 && !ds.active);
        if (!early) {
            for (double f = 4; f <= 500; ++f) assert(poll(f).buttons == 0x8000); // no timeout while held
            pointer(ds, "1", Phase::up, 200, 100, release, touch);
        }
        for (double f = std::max(4.0, release); f <= release + 359; ++f) assert(poll(f).buttons == 0x8000);
        const auto b = poll(release + 360);
        assert(b.buttons == 0 && b.x == 0 && b.y == 0 && !ds.drag && !ds.active);
        assert(ds.queue.empty() && !ds.drive && has(ds.notice, "selection cancelled"));
        assert(poll(release + 367).buttons == 0); // neutral gap prevents another controller edge
        pointer(ds, "1", Phase::down, 100, 100, release + 368, touch);
        pointer(ds, "1", Phase::up, 100, 100, release + 368, touch);
        assert(poll(release + 368).buttons == 0 && poll(release + 369).buttons == 0x8000);
    }
    // Two-finger order never emits a prior A that would deselect the units.
    clear(s);
    pointer(s, "1", Phase::down, 240, 100, 70, true);
    pointer(s, "2", Phase::down, 270, 100, 71, true);
    pointer(s, "1", Phase::up, 240, 100, 72, true);
    pointer(s, "2", Phase::up, 270, 100, 73, true);
    assert(s.queue.at(0)->x == 240); // contextual target belongs to the first finger
    assert(bits_at(73) == 0); assert(bits_at(75) == 0x4000);
    clear(s);
    pointer(s, "1", Phase::down, 100, 100, 80, true);
    read_at(110); assert(s.radial && s.queue.empty());
    clear(s);
    enqueue(s, 240, 100, 0x8000);
    read_at(120, 100, 100);
    info.cursor_field = 100;
    out = read(s, info, false); assert(out.buttons == 0 && out.x == 0 && out.y == 0);
    info.cursor_valid = false;
    assert(bits_at(500) == 0 && !s.active && has(s.notice, "cancelled"));
    enqueue(s, 200, 80, 0x8000);
    assert(read(s, info, true).buttons == 0 && s.queue.empty());
    // Exercise the pulse/settle servo with a delayed, quadratic original-like pad.
    // This is a control invariant fixture, not game behavior/parity evidence.
    clear(s); info.cursor_valid = true;
    info.cursor_x = 160; info.cursor_y = 120;
    enqueue(s, 275, 42, 0x8000);
    {
        double pending[2] = {0, 0};
        bool clicked = false;
        for (double field = 600; field <= 950; field += 2) {
            info.fields = field; info.cursor_field = field;
            const auto r = read(s, info, false);
            if (r.buttons != 0) {
                assert(std::abs(info.cursor_x - 275) <= 1.5 && std::abs(info.cursor_y - 42) <= 1.5);
                clicked = true; break;
            }
            const auto delta = [](double v) {
                const double raw = std::max(0.0, std::abs(v) * 82 - 1);
                return (v < 0 ? -1 : 1) * raw * raw / 400;
            };
            info.cursor_x = info.cursor_x + delta(pending[0]);
            info.cursor_y = info.cursor_y - delta(pending[1]);
            pending[0] = r.x; pending[1] = r.y;
        }
        assert(clicked && "servo must converge before the bounded action timeout");
    }
    // Free mouse hover (see motion() above).
    for (int lag : {0, 1, 3}) for (double divisor : {150.0, 200.0}) {
        GestureState hs;
        Info hf;
        hf.fields = 0; hf.cursor_field = 0; hf.cursor_valid = true; hf.cursor_x = 160; hf.cursor_y = 120;
        hf.cursor_divisor = divisor;
        double cx = 160, cy = 120;
        History history{{0, {cx, cy}}};
        double tx = 275, ty = 42;
        int idle = 0;
        pointer(hs, "mouse", Phase::move, tx, ty, 0, false);
        for (int f = 1; f <= 260; ++f) {
            if (f == 100) { tx = 80; ty = 190; pointer(hs, "mouse", Phase::move, tx, ty, f, false); }
            const int observed = std::max(0, f - 1 - lag);
            hf.fields = f; hf.cursor_field = observed;
            std::tie(hf.cursor_x, hf.cursor_y) = history.at(observed);
            const auto r = read(hs, hf, false);
            assert(r.buttons == 0 && !hs.drive && !hs.active);
            const auto [dx, dy] = motion(r.x, r.y, divisor);
            if (f == 1) assert(std::hypot(dx, dy) > 8.3 && "mouse hover must exceed the old 8 px/field limit");
            assert(std::sqrt(dx * dx + dy * dy) <= servo.hover_step_cap + .3 && "hover must not skip large areas");
            idle = (std::abs(tx - cx) + std::abs(ty - cy) > 20 && r.x == 0 && r.y == 0) ? idle + 1 : 0;
            assert(idle < 4 && "hover must not have an eight-field settle gap");
            cx = cx + dx; cy = cy + dy; history[f] = {cx, cy};
            if (f == 95) { assert(std::abs(cx - tx) < 1.5 && std::abs(cy - ty) < 1.5); }
        }
        assert(std::abs(cx - tx) < 1.5 && std::abs(cy - ty) < 1.5);
        hf.cursor_field = 240;
        const auto r = read(hs, hf, false); assert(r.buttons == 0 && r.x == 0 && r.y == 0);
    }
    // A touch selection drag across most of the map completes quickly and both
    // corners are exact despite snapshot lag: A presses and releases only after a
    // snapshot reflects the last steering input (no in-flight overshoot).
    for (int lag : {0, 1, 3, 6}) {
        GestureState ds;
        Info di;
        di.fields = 0; di.cursor_field = 0; di.cursor_valid = true; di.cursor_x = 160; di.cursor_y = 120;
        di.cursor_divisor = 200;
        di.ui_mode = "game"; di.ui_field = 0; di.ui_context = 1; di.ui_token = 3; di.width = 320; di.height = 240;
        double cx = 160, cy = 120;
        History history{{0, {cx, cy}}};
        bool held = false;
        std::optional<int> pressed, done;
        for (int f = 0; f <= 400; ++f) {
            if (f == 0) pointer(ds, "1", Phase::down, 60, 60, f, true, &di);
            if (f >= 3 && f <= 20) {
                const double t = (f - 3) / 17.0;
                pointer(ds, "1", Phase::move, 60 + 200 * t + (f == 3 ? 4 : 0), 60 + 120 * t, f, true, &di);
            }
            if (f == 24) pointer(ds, "1", Phase::up, 260, 180, f, true, &di);
            const int observed = std::max(0, f - 1 - lag);
            di.fields = f; di.cursor_field = observed; di.ui_field = f;
            std::tie(di.cursor_x, di.cursor_y) = history.at(observed);
            const auto r = read(ds, di, false);
            const bool a = (r.buttons & 0x8000) != 0;
            if (a && !held) {
                pressed = f;
                assert(std::abs(cx - 60) <= 1.5 && std::abs(cy - 60) <= 1.5 && "start corner");
            }
            if (held && !a) {
                done = f;
                assert(std::abs(cx - 260) <= 1.5 && std::abs(cy - 180) <= 1.5 && "end corner");
                break;
            }
            held = a;
            const auto [dx, dy] = motion(r.x, r.y, 200); cx = cx + dx; cy = cy + dy; history[f] = {cx, cy};
        }
        assert(pressed && done && "drag must complete");
        // Previously ~103/115/142/178 fields at lag 0/1/3/6 with a 4.5 px/field cap.
        const std::map<int, int> limit{{0, 65}, {1, 85}, {3, 120}, {6, 175}};
        if (*done > limit.at(lag)) std::fprintf(stderr, "drag too slow at lag %d: %d\n", lag, *done);
        assert(*done <= limit.at(lag));
    }
    // Touch arrivals use continuous steering too. Delayed original-like motion
    // must converge and produce A only at the requested point, without burst gaps.
    for (int lag : {0, 1, 3, 6}) {
        GestureState cs;
        Info ci;
        ci.fields = 0; ci.cursor_field = 0; ci.cursor_valid = true; ci.cursor_x = 50; ci.cursor_y = 50;
        ci.cursor_divisor = 200;
        double cx = 50, cy = 50;
        History history{{0, {cx, cy}}};
        bool clicked = false;
        enqueue(cs, 275, 170, 0x8000);
        for (int f = 1; f <= 300; ++f) {
            const int observed = std::max(0, f - 1 - lag);
            ci.fields = f; ci.cursor_field = observed;
            std::tie(ci.cursor_x, ci.cursor_y) = history.at(observed);
            const auto r = read(cs, ci, false);
            if (r.buttons != 0) {
                assert(r.buttons == 0x8000 && std::abs(cx - 275) <= 2 && std::abs(cy - 170) <= 2);
                assert(f < 160 && "touch arrival should not spend seconds in neutral gaps");
                clicked = true; break;
            }
            const auto [dx, dy] = motion(r.x, r.y, 200);
            cx = cx + dx; cy = cy + dy; history[f] = {cx, cy};
        }
        assert(clicked && "continuous touch arrival must converge with delayed feedback");
    }
    // Original-like integer camera motion with delayed snapshots. Stopping fingers
    // bounds total scroll to their displacement, including at map edges. Polling
    // twice in one guest field must not spend another movement pulse.
    for (bool touch : {false, true}) for (int lag : {0, 2, 5}) {
        GestureState ps;
        Info pinfo;
        pinfo.fields = 0; pinfo.cursor_field = 0; pinfo.cursor_valid = true; pinfo.camera_valid = true;
        pinfo.camera_x = 200; pinfo.camera_y = 200; pinfo.camera_min_x = 0; pinfo.camera_max_x = 500;
        pinfo.camera_min_y = 0; pinfo.camera_max_y = 500; pinfo.camera_divisor = 4;
        if (touch) {
            pointer(ps, "1", Phase::down, 100, 100, 0, true); pointer(ps, "2", Phase::down, 120, 100, 0, true);
            pointer(ps, "1", Phase::move, 60, 120, 1, true); pointer(ps, "2", Phase::move, 80, 120, 1, true);
        } else { pan_pointer(ps, Phase::down, 110, 100); pan_pointer(ps, Phase::move, 70, 120); }
        double cx = 200, cy = 200;
        History history{{0, {cx, cy}}};
        bool stopped = false;
        for (int f = 1; f <= 140; ++f) {
            const int observed = std::max(0, f - 1 - lag);
            pinfo.fields = f; pinfo.cursor_field = observed;
            std::tie(pinfo.camera_x, pinfo.camera_y) = history.at(observed);
            const auto r = read(ps, pinfo, false);
            const auto again = read(ps, pinfo, false);
            assert(again.buttons == r.buttons && again.x == r.x && again.y == r.y);
            assert(r.buttons == 0 || r.buttons == 0x2000);
            if (r.buttons != 0) {
                // Octagonal conversion plus original signed deadzone/div/even step.
                const auto [rx, ry] = raw_stick(r.x, r.y, false);
                const auto delta = [](double raw) {
                    double v = std::max(0.0, std::floor(std::abs(raw)) - 1) / 4;
                    v = raw < 0 ? -std::floor(v) : std::floor(v);
                    return std::floor(v / 2) * 4; // world/2 in the native image
                };
                cx = cx + delta(rx); cy = cy - delta(ry);
            }
            history[f] = {cx, cy};
            assert(std::abs(cx - 200) <= 48 && std::abs(cy - 200) <= 28
                   && "stationary fingers cannot pan indefinitely");
            if (f == 110) {
                assert(std::abs(cx - 240) <= 4 && std::abs(cy - 180) <= 4);
                stopped = true;
            } else if (f > 110) { assert(r.buttons == 0 && stopped); }
        }
        pinfo.camera_valid = false;
        assert(read(ps, pinfo, false).buttons == 0);
        pinfo.camera_valid = true; pinfo.cursor_field = 0;
        assert(read(ps, pinfo, false).buttons == 0);
        clear(ps); assert(!ps.camera_drive);
    }
    // Clamping spends excess edge displacement immediately. Reversing a drag
    // must move the map without first undoing all motion beyond the edge.
    {
        GestureState es;
        Info ei;
        ei.fields = 1; ei.cursor_field = 1; ei.cursor_valid = true; ei.camera_valid = true;
        ei.camera_x = 5; ei.camera_y = 200; ei.camera_min_x = 0; ei.camera_max_x = 500;
        ei.camera_min_y = 0; ei.camera_max_y = 500; ei.camera_divisor = 4;
        pan_pointer(es, Phase::down, 100, 100); read(es, ei, false);
        pan_pointer(es, Phase::move, 200, 100); read(es, ei, false);
        assert(es.pan.value().camera_x == 0);
        ei.fields = 12; ei.cursor_field = 12; ei.camera_x = 0;
        pan_pointer(es, Phase::move, 180, 100);
        const auto r = read(es, ei, false);
        assert(es.pan.value().camera_x == 20 && r.buttons == 0x2000 && r.x > 0 && r.y == 0);
        ei.cursor_field = 13; assert(read(es, ei, false).buttons == 0); // future feedback fails closed
        pan_pointer(es, Phase::up, 180, 100); assert(!es.camera_drive);
    }
    clear(s);
    info.camera_valid = true; info.camera_x = 400; info.camera_y = 400;
    info.camera_min_x = 0; info.camera_max_x = 1000; info.camera_min_y = 0; info.camera_max_y = 1000;
    info.camera_divisor = 4;
    pointer(s, "1", Phase::down, 160, 120, 960, true);
    pointer(s, "2", Phase::down, 180, 120, 960, true);
    pointer(s, "1", Phase::move, 140, 130, 962, true);
    pointer(s, "2", Phase::move, 160, 130, 962, true);
    out = read_at(962);
    assert(out.buttons == 0x2000 && out.x > 0 && out.y > 0 && s.queue.empty());
    pointer(s, "1", Phase::up, 140, 130, 964, true);
    assert(bits_at(964) == 0);
    pointer(s, "2", Phase::up, 160, 130, 966, true);
    assert(bits_at(966) == 0 && s.queue.empty()); // no B after a camera drag
    pan_pointer(s, Phase::down, 160, 120);
    pan_pointer(s, Phase::move, 240, 120);
    out = read_at(970); assert(out.buttons == 0x2000 && out.x < 0 && out.y == 0);
    pan_pointer(s, Phase::up, 240, 120); assert(bits_at(972) == 0);
    std::puts("Gesture cancellation, selection, orders and feedback servo passed");
    // Interface actions use fresh mode/selection feedback, never the frozen cursor.
    clear(s);
    info.ui_mode = "message"; info.ui_token = 123; info.ui_field = 1000;
    info.fields = 1000; info.cursor_valid = false;
    ui_pointer(s, "1", Phase::down, 250, 180, info);
    ui_pointer(s, "1", Phase::up, 250, 180, info);
    assert(read(s, info, false).buttons == 0); // early taps survive the original advisor lockout
    info.fields = 1060; info.ui_field = 1060;
    assert(read(s, info, false).buttons == 0x8000);
    info.ui_token = 124; // a different dialog cannot inherit the held confirmation
    assert(read(s, info, false).buttons == 0 && !s.ui);
    // Outside build-panel tap closes through B, then queues A at the requested map
    // point only after a fresh game/cursor snapshot and neutral controller fields.
    clear(s); info.ui_mode = "build"; info.ui_token = 300;
    info.cursor_valid = true;
    info.fields = 1400; info.ui_field = 1400; info.cursor_field = 1400;
    info.ui_bounds = Bounds{.x0 = 232, .y0 = 143, .x1 = 319, .y1 = 223}; info.ui_items.clear();
    ui_pointer(s, "1", Phase::down, 100, 64, info); ui_pointer(s, "1", Phase::up, 100, 64, info);
    assert(read(s, info, false).buttons == 0x4000 && s.queue.empty());
    info.ui_mode = "game"; info.ui_token = 3;
    info.fields = 1402; info.ui_field = 1402; info.cursor_field = 1402;
    assert(read(s, info, false).buttons == 0 && s.queue.empty()); // release B immediately after close
    info.fields = 1408; info.ui_field = 1400; info.cursor_field = 1400;
    assert(read(s, info, false).buttons == 0 && s.queue.empty()); // stale transition cannot select
    info.fields = 1418; info.ui_field = 1418; info.cursor_field = 1418;
    assert(read(s, info, false).buttons == 0 && s.queue.size() == 1 && !s.ui);
    assert(s.queue.at(0)->buttons == 0x8000 && s.queue.at(0)->x == 100 && s.queue.at(0)->y == 64);
    // A gap/disabled slot inside the panel is consumed, never a map selection.
    clear(s); info.ui_mode = "build"; info.ui_token = 300;
    info.fields = 1420; info.ui_field = 1420;
    ui_pointer(s, "1", Phase::down, 258, 175, info); ui_pointer(s, "1", Phase::up, 258, 175, info);
    assert(read(s, info, false).buttons == 0 && s.ui && !s.ui->close_to && s.queue.empty());
    // Changed root/dialog, competing input, pause and timeout cancel replay.
    for (const std::string change : {"root", "modal", "pause", "timeout", "new_click"}) {
        clear(s); info.ui_mode = "build"; info.ui_token = 300;
        info.fields = 1500; info.ui_field = 1500; info.cursor_field = 1500;
        ui_pointer(s, "1", Phase::down, 100, 64, info); ui_pointer(s, "1", Phase::up, 100, 64, info);
        assert(read(s, info, false).buttons == 0x4000);
        if (change == "root") info.ui_token = 301;
        else if (change == "modal") info.ui_mode = "message";
        else if (change == "timeout") { info.fields = 1681; info.ui_field = 1681; }
        else if (change == "new_click") ui_pointer(s, "1", Phase::down, 258, 175, info);
        assert(read(s, info, change == "pause").buttons == 0 && s.queue.empty());
        if (change != "new_click") { assert(!s.ui); } else { assert(s.ui && !s.ui->close_to); }
    }
    clear(s);
    info.ui_mode = "menu"; info.ui_token = 256;
    info.ui_items = {UiItem{.id = 0, .group = 1, .x0 = 220, .y0 = 40, .x1 = 410, .y1 = 68, .selected = true,
                            .activate = true},
                     UiItem{.id = 2, .group = 1, .x0 = 220, .y0 = 120, .x1 = 410, .y1 = 148, .activate = true}};
    info.fields = 1010; info.ui_field = 1010;
    ui_pointer(s, "mouse", Phase::down, 350, 130, info);
    ui_pointer(s, "mouse", Phase::up, 350, 130, info);
    assert(read(s, info, false).buttons == 0x0400); // coordinate outside gameplay 320-wide bounds
    info.fields = 1028; info.ui_field = 1028;
    assert(read(s, info, false).buttons == 0); // do not repeat until selection changes
    info.ui_items[0].selected = false; info.ui_items[1].selected = true;
    info.fields = 1030; info.ui_field = 1030;
    assert(read(s, info, false).buttons == 0x8000); // only confirm the requested selected item
    // Two-Player is unsupported (ADR-070): its main-menu entry is not clickable
    // (other entries stay so) and its setup screen leaves with repeated Back.
    {
        GestureState ts;
        Info ti;
        ti.fields = 2000; ti.ui_field = 2000; ti.ui_mode = "menu"; ti.ui_token = 256; ti.width = 640; ti.height = 480;
        ti.ui_items = {
            UiItem{.id = 0, .group = 1, .x0 = 220, .y0 = 40, .x1 = 410, .y1 = 68, .selected = true, .activate = true},
            UiItem{.id = 1, .group = 1, .x0 = 259, .y0 = 75, .x1 = 398, .y1 = 95, .activate = true},
            UiItem{.id = 2, .group = 1, .x0 = 244, .y0 = 113, .x1 = 391, .y1 = 133, .activate = true}};
        ui_pointer(ts, "mouse", Phase::down, 320, 84, ti); ui_pointer(ts, "mouse", Phase::up, 320, 84, ti);
        assert(!ts.ui && ts.notice == "Two-Player is not supported");
        assert(read(ts, ti, false).buttons == 0);
        ts.notice = "";
        ui_pointer(ts, "mouse", Phase::down, 320, 120, ti); ui_pointer(ts, "mouse", Phase::up, 320, 120, ti);
        assert(ts.ui && ts.ui->target && ts.ui->target->id == 2 && ts.notice == "");
        // Setup screen: B for one pulse, neutral, then a repeat while it stays.
        ts = GestureState{}; ti.ui_token = 267; ti.ui_items.clear();
        std::vector<unsigned> trace;
        for (double f = 3000; f <= 3070; ++f) {
            ti.fields = f; ti.ui_field = f;
            trace.push_back(read(ts, ti, false).buttons);
        }
        int presses = 0;
        unsigned previous = 0;
        for (const unsigned b : trace) {
            assert(b == 0 || b == 0x4000);
            if (b != 0 && previous == 0) ++presses;
            previous = b;
        }
        assert(presses == 3 && trace.at(0) == 0x4000 && trace.at(4) == 0 && ts.notice == "Two-Player is not supported");
        // Clicks and long presses there are consumed without any target or panel.
        ui_pointer(ts, "mouse", Phase::down, 100, 100, ti);
        ti.fields = 3100; ti.ui_field = 3100;
        read(ts, ti, false);
        assert(!ts.ui && !ts.radial);
        // Once the screen changes the exit state resets and nothing is pressed.
        ti.ui_token = 256; ti.fields = 3200; ti.ui_field = 3200;
        assert(read(ts, ti, false).buttons == 0 && !ts.two_player_exit);
        ti.ui_token = 267; ti.fields = 3300; ti.ui_field = 3300;
        assert(read(ts, ti, false).buttons == 0x4000);
        // Stale feedback never presses.
        ts = GestureState{}; ti.fields = 4000; ti.ui_field = 3000;
        assert(read(ts, ti, false).buttons == 0);
    }
    info.ui_mode = "game"; info.ui_token = 3;
    assert(read(s, info, false).buttons == 0 && !s.ui);
    info.ui_mode = "menu"; info.ui_token = 262;
    info.fields = 1040; info.ui_field = 1040;
    info.ui_items = {UiItem{.id = 0, .group = 2, .x0 = 0, .y0 = 0, .x1 = 100, .y1 = 30, .selected = true,
                            .horizontal = true, .activate = false},
                     UiItem{.id = 1, .group = 2, .x0 = 101, .y0 = 0, .x1 = 200, .y1 = 30, .horizontal = true,
                            .activate = false}};
    ui_pointer(s, "1", Phase::down, 130, 15, info); ui_pointer(s, "1", Phase::up, 130, 15, info);
    assert(read(s, info, false).buttons == 0x0100);
    info.fields = 1060; info.ui_field = 1060;
    info.ui_items[0].selected = false; info.ui_items[1].selected = true;
    assert(read(s, info, false).buttons == 0 && s.ui && !s.ui->target); // switching tab must not start mission
    clear(s); info.fields = 1080; info.ui_field = 1040;
    ui_pointer(s, "1", Phase::down, 130, 15, info); assert(!s.ui); // stale UI fails closed
    info.ui_mode = "game"; info.cursor_valid = true;
    enqueue(s, 275, 42, 0x8000);
    info.fields = 1100; info.cursor_field = 1080;
    assert(read(s, info, false).buttons == 0); // no universal A bypass when gameplay snapshot is stale
    std::puts("Interface messages, menu navigation, tab selection and transition cancellation passed");
    clear(s); info.ui_mode = "build"; info.ui_token = 300;
    info.fields = 1200; info.ui_field = 1200;
    info.ui_items = {UiItem{.id = 0, .group = 1, .x0 = 230, .y0 = 150, .x1 = 250, .y1 = 170, .columns = 3,
                            .activate = true},
                     UiItem{.id = 4, .group = 1, .x0 = 255, .y0 = 175, .x1 = 275, .y1 = 195, .columns = 3,
                            .selected = true, .activate = true}};
    ui_pointer(s, "1", Phase::down, 240, 160, info); ui_pointer(s, "1", Phase::up, 240, 160, info);
    out = read(s, info, false); assert(out.buttons == 0 && out.x < 0 && out.y == 0);
    info.fields = 1220; info.ui_field = 1220;
    info.ui_items[1].selected = false;
    info.ui_items.push_back(UiItem{.id = 3, .group = 1, .x0 = 230, .y0 = 175, .x1 = 250, .y1 = 195, .columns = 3,
                                   .selected = true, .activate = true});
    out = read(s, info, false); assert(out.buttons == 0 && out.x == 0 && out.y > 0);
    info.fields = 1240; info.ui_field = 1240;
    info.ui_items[2].selected = false; info.ui_items[0].selected = true;
    assert(read(s, info, false).buttons == 0x8000);
    clear(s); info.fields = 1260; info.ui_field = 1260;
    ui_pointer(s, "1", Phase::down, 240, 160, info); ui_pointer(s, "2", Phase::down, 260, 160, info);
    assert(read(s, info, false).buttons == 0x4000);
    ui_pointer(s, "1", Phase::up, 240, 160, info); ui_pointer(s, "2", Phase::up, 260, 160, info);
    assert(s.ui && !s.ui->target); // two fingers cannot confirm a button after Back
    clear(s); info.fields = 1300; info.ui_field = 1300;
    info.ui_mode = "menu"; info.ui_token = 262;
    info.ui_items = {UiItem{.id = 0, .group = 2, .x0 = 0, .y0 = 0, .x1 = 80, .y1 = 30, .selected = true},
                     UiItem{.id = 0, .group = 3, .x0 = 100, .y0 = 100, .x1 = 200, .y1 = 125, .selected = true,
                            .activate = true}};
    ui_pointer(s, "1", Phase::down, 150, 110, info); ui_pointer(s, "1", Phase::up, 150, 110, info);
    info.ui_items[0].id = 1; // a reused row ID in another tab must not inherit the click
    assert(read(s, info, false).buttons == 0 && !s.ui);

    // A plain tap chooses B only from the original hit test at cursor arrival.
    const auto pick_info = [](double field, double buttons) {
        Info i;
        i.fields = field; i.cursor_field = field; i.cursor_x = 100; i.cursor_y = 80; i.cursor_valid = true;
        i.ui_mode = "game"; i.ui_field = field; i.ui_context = 123; i.pick_required = true;
        i.cursor_pick = CursorPick{.x = 100, .y = 80, .context = 123, .buttons = buttons};
        return i;
    };
    for (bool touch : {true, false}) {
        for (unsigned buttons : {0x8000u, 0x4000u}) {
            s = GestureState{};
            Info observed = pick_info(2000, buttons);
            pointer(s, "1", Phase::down, 100, 80, 2000, touch); pointer(s, "1", Phase::up, 100, 80, 2001, touch);
            assert(read(s, observed, false).buttons == 0);
            observed.fields = 2002; observed.cursor_field = 2002; observed.ui_field = 2002;
            assert(read(s, observed, false).buttons == buttons);
        }
    }
    for (const std::string defect : {"missing", "context", "position", "stale", "future", "mask"}) {
        s = GestureState{};
        Info observed = pick_info(2100, 0x4000);
        pointer(s, "1", Phase::down, 100, 80, 2100, true); pointer(s, "1", Phase::up, 100, 80, 2101, true);
        assert(read(s, observed, false).buttons == 0);
        observed.fields = 2102; observed.cursor_field = 2102; observed.ui_field = 2102;
        if (defect == "missing") observed.cursor_pick.reset();
        else if (defect == "context") observed.cursor_pick->context = 456;
        else if (defect == "position") observed.cursor_pick->x = 104;
        else if (defect == "stale") observed.ui_field = 2089;
        else if (defect == "future") observed.ui_field = 2103;
        else observed.cursor_pick->buttons = 0x1000;
        assert(read(s, observed, false).buttons == 0 && s.active);
        observed = pick_info(2104, 0x4000);
        assert(read(s, observed, false).buttons == 0x4000);
    }
    // Explicit right-click/two-finger B and A-held rectangle selection bypass auto.
    {
        s = GestureState{};
        Info observed = pick_info(2200, 0x4000);
        enqueue(s, 100, 80, 0x8000, true);
        s.drag = std::make_shared<Drag>();
        s.drag->x = 100; s.drag->y = 80; s.drag->released = false; s.drag->started = false;
        assert(read(s, observed, false).buttons == 0);
        observed.fields = 2202; observed.cursor_field = 2202;
        assert(read(s, observed, false).buttons == 0x8000 && s.drag && s.drag->started);
    }
    std::puts("Contextual resource tap regression passed");

    // Multi-contact camera gestures: a re-placed second finger keeps the pan
    // history, a third finger cancels, and neither ends in a contextual B.
    {
        GestureState ms;
        Info mi;
        mi.fields = 0; mi.cursor_field = 0; mi.cursor_x = 100; mi.cursor_y = 100; mi.cursor_valid = true;
        const auto poll = [&](double f) {
            mi.fields = f; mi.cursor_field = f;
            return read(ms, mi, false).buttons;
        };
        // Pan with the second finger, lift and re-place it, then release all.
        pointer(ms, "1", Phase::down, 100, 100, 0, true);
        pointer(ms, "2", Phase::down, 140, 100, 1, true);
        pointer(ms, "2", Phase::move, 170, 100, 2, true);
        pointer(ms, "2", Phase::up, 170, 100, 3, true);
        pointer(ms, "2", Phase::down, 172, 101, 4, true);
        assert(ms.two.value().moved);
        pointer(ms, "2", Phase::up, 172, 101, 5, true);
        pointer(ms, "1", Phase::up, 100, 100, 6, true);
        assert(ms.queue.empty() && !ms.two);
        for (double f = 6; f <= 20; ++f) assert(poll(f) == 0);
        // A HUD-originated two-finger press stays suppressed after re-placement.
        clear(ms);
        pointer(ms, "1", Phase::down, 100, 100, 30, true);
        pointer(ms, "2", Phase::down, 140, 100, 31, true);
        ms.two.value().suppress_tap = true;
        pointer(ms, "2", Phase::up, 140, 100, 32, true);
        pointer(ms, "2", Phase::down, 140, 100, 33, true);
        pointer(ms, "2", Phase::up, 140, 100, 34, true); pointer(ms, "1", Phase::up, 100, 100, 35, true);
        assert(ms.queue.empty());
        // A genuine stationary two-finger tap still orders at the first finger.
        clear(ms);
        pointer(ms, "1", Phase::down, 100, 100, 40, true);
        pointer(ms, "2", Phase::down, 140, 100, 41, true);
        pointer(ms, "2", Phase::up, 140, 100, 42, true); pointer(ms, "1", Phase::up, 100, 100, 43, true);
        assert(ms.queue.size() == 1 && ms.queue.at(0)->buttons == 0x4000 && ms.queue.at(0)->x == 100);
        // A third finger cancels the gesture with or without movement; the
        // remaining releases and moves are not adopted as new contacts.
        for (bool moved : {false, true}) {
            clear(ms);
            pointer(ms, "1", Phase::down, 100, 100, 50, true);
            pointer(ms, "2", Phase::down, 140, 100, 51, true);
            if (moved) pointer(ms, "2", Phase::move, 180, 100, 52, true);
            assert(pointer(ms, "3", Phase::down, 200, 100, 53, true));
            assert(has(ms.notice, "two fingers") && ms.contacts.empty() && !ms.two);
            ms.notice = "";
            for (int id = 1; id <= 3; ++id) {
                assert(!pointer(ms, std::to_string(id), Phase::move, 120, 120, 54, true));
                assert(!pointer(ms, std::to_string(id), Phase::up, 120, 120, 55, true));
            }
            assert(ms.queue.empty() && !ms.two && !ms.drag && !ms.radial);
            for (double f = 55; f <= 100; ++f) assert(poll(f) == 0);
            assert(!ms.radial && ms.contacts.empty());
        }
    }
    std::puts("Re-placed, tapped and three-finger camera gestures passed");

    // A right-click B queued behind a held selection drag waits for the drag,
    // then reaches its own point; it never stalls or fires at a later click.
    {
        GestureState rs;
        Info ri;
        ri.fields = 0; ri.cursor_field = 0; ri.cursor_x = 100; ri.cursor_y = 100; ri.cursor_valid = true;
        const auto poll = [&](double f, std::optional<double> x = {}, std::optional<double> y = {}) {
            ri.fields = f; ri.cursor_field = f;
            if (x) { ri.cursor_x = *x; ri.cursor_y = y.value(); }
            return read(rs, ri, false).buttons;
        };
        pointer(rs, "mouse", Phase::down, 100, 100, 0, false);
        pointer(rs, "mouse", Phase::move, 200, 100, 1, false);
        assert(poll(2) == 0 && poll(3) == 0x8000 && rs.drag && rs.drag->started);
        assert(enqueue(rs, 50, 60, 0x4000));
        assert(poll(4) == 0x8000 && !rs.active && rs.target == rs.drag);
        pointer(rs, "mouse", Phase::up, 200, 100, 5, false);
        double f = 6;
        unsigned held = poll(f, 200, 100);
        while (rs.drag) { assert(held == 0x8000); f = f + 1; held = poll(f); assert(f < 40); }
        assert(held == 0); f = f + 1; // release, then the eight-field neutral gap
        for (int i = 1; i <= 7; ++i) { assert(poll(f) == 0); f = f + 1; }
        unsigned b = poll(f, 50, 60); f = f + 1;
        assert(rs.active && rs.active->buttons == 0x4000 && rs.target && rs.target->x == 50 && rs.target->y == 60);
        while (b != 0x4000) { b = poll(f); f = f + 1; assert(f < 80); }
        assert(rs.queue.empty());
    }
    std::puts("Right-click order queued behind a selection drag passed");

    // Group commands are staged original controller sequences: the modifier is
    // held before the C edge, C is released while it is still held, the modifier
    // is released afterwards, then eight neutral fields follow (research.md).
    // Lua's `extra` table is applied by each caller after construction.
    const auto mission = [](double field) {
        Info m;
        m.fields = field; m.cursor_field = field; m.cursor_x = 100; m.cursor_y = 80; m.cursor_valid = true;
        m.ui_mode = "game"; m.ui_field = field; m.ui_context = 77; m.ui_token = 3; m.width = 320; m.height = 240;
        return m;
    };
    const auto trace = [](GestureState& gs, Info& gi, double from, double to) {
        std::vector<unsigned> result;
        for (double f = from; f <= to; ++f) {
            gi.fields = f; gi.cursor_field = f; gi.ui_field = f;
            result.push_back(read(gs, gi, false).buttons);
        }
        return result;
    };
    const auto expect = [](const std::vector<unsigned>& result, const std::vector<std::pair<unsigned, int>>& runs) {
        std::size_t i = 0;
        for (const auto& [want, count] : runs) {
            for (int n = 0; n < count; ++n) {
                const bool ok = i < result.size() && result[i] == want;
                if (!ok) {
                    std::fprintf(stderr, "field %zu: got %s, want %u\n", i + 1,
                                 i < result.size() ? std::to_string(result[i]).c_str() : "nil", want);
                }
                assert(ok);
                ++i;
            }
        }
    };

    // Double tap on an already selected unit selects every unit of that type on
    // screen (R, R+A, R), like a PC StarCraft double click. The decision uses the
    // game's own hit test at arrival; any other double tap stays a B order.
    {
        const auto double_tap = [&](bool selected, std::optional<double> context = {}) {
            GestureState gs;
            Info gi = mission(1000);
            gi.pick_required = true;
            gi.cursor_pick = CursorPick{.x = 100, .y = 80, .context = gi.ui_context.value(), .buttons = 0x8000,
                                        .target_selected = selected};
            if (context) gi.cursor_pick->context = *context;
            pointer(gs, "1", Phase::down, 100, 80, 1000, true, &gi); pointer(gs, "1", Phase::up, 100, 80, 1001, true, &gi);
            pointer(gs, "2", Phase::down, 101, 80, 1004, true, &gi); pointer(gs, "2", Phase::up, 101, 80, 1005, true, &gi);
            assert(gs.queue.size() == 1 && gs.queue.at(0)->double_tap && gs.queue.at(0)->buttons == 0x4000);
            return std::pair{gs, gi};
        };
        GestureState gs;
        Info gi;
        std::tie(gs, gi) = double_tap(true);
        // Cursor already at the point: one field marks it stable, then the chord.
        expect(trace(gs, gi, 1005, 1034), {{0, 1}, {0x0010, 4}, {0x8010, 8}, {0x0010, 6}, {0, 11}});
        assert(gs.queue.empty() && !gs.seq && !gs.active);
        // An unselected unit or terrain keeps the contextual B order.
        std::tie(gs, gi) = double_tap(false);
        expect(trace(gs, gi, 1005, 1014), {{0, 1}, {0x4000, 8}, {0, 1}});
        // A stale or foreign-context pick waits instead of guessing.
        std::tie(gs, gi) = double_tap(true, 999);
        expect(trace(gs, gi, 1005, 1020), {{0, 16}});
        // Without pick feedback (e.g. build mode) the double tap is a plain B.
        std::tie(gs, gi) = double_tap(true); gi.pick_required = false;
        expect(trace(gs, gi, 1005, 1014), {{0, 1}, {0x4000, 8}, {0, 1}});
        // A dialog, keyboard/gamepad ownership or blocking cancels the chord.
        for (const std::string cause : {"clear", "blocked", "dialog"}) {
            std::tie(gs, gi) = double_tap(true);
            expect(trace(gs, gi, 1005, 1008), {{0, 1}, {0x0010, 3}});
            gi.fields = 1009; gi.ui_field = 1009; gi.cursor_field = 1009;
            if (cause == "clear") { clear(gs); assert(read(gs, gi, false).buttons == 0); }
            else if (cause == "blocked") { assert(read(gs, gi, true).buttons == 0); }
            else { gi.ui_mode = "message"; assert(read(gs, gi, false).buttons == 0 && has(gs.notice, "interrupted")); }
            assert(!gs.seq);
        }
        // Two-finger stationary holds no longer select a type; long ones do nothing.
        gs = GestureState{}; gi = mission(1100);
        pointer(gs, "1", Phase::down, 100, 80, 1100, true, &gi); pointer(gs, "2", Phase::down, 130, 80, 1101, true, &gi);
        pointer(gs, "2", Phase::up, 130, 80, 1140, true, &gi); pointer(gs, "1", Phase::up, 100, 80, 1141, true, &gi);
        assert(gs.queue.empty());
    }
    std::puts("Double-tap same-type selection passed");

    // D-042: map gestures are verified only for the 320x240 one-player image.
    // A 2P (512x384) tap beyond x=320 is consumed with a notice instead of being
    // dropped, a drag crossing x=320 never presses A (which would clear the
    // selection), and long-press still opens the controller panel.
    {
        GestureState gs;
        Info gi = mission(1500);
        gi.width = 512; gi.height = 384;
        assert(pointer(gs, "1", Phase::down, 400, 300, 1500, true, &gi));
        assert(pointer(gs, "1", Phase::up, 400, 300, 1501, true, &gi));
        assert(gs.queue.empty() && has(gs.notice, "one-player"));
        gs.notice = "";
        assert(pointer(gs, "1", Phase::down, 300, 100, 1502, true, &gi));
        pointer(gs, "1", Phase::move, 340, 120, 1503, true, &gi);
        for (double f = 1503; f <= 1520; ++f) {
            gi.fields = f; gi.ui_field = f; gi.cursor_field = f;
            assert(read(gs, gi, false).buttons == 0);
        }
        pointer(gs, "1", Phase::up, 340, 120, 1521, true, &gi);
        assert(!gs.drag && gs.queue.empty());
        // Two fingers neither order nor pan; mouse hover does not steer.
        pointer(gs, "1", Phase::down, 100, 100, 1530, true, &gi); pointer(gs, "2", Phase::down, 140, 100, 1531, true, &gi);
        pointer(gs, "2", Phase::up, 140, 100, 1532, true, &gi); pointer(gs, "1", Phase::up, 100, 100, 1533, true, &gi);
        pointer(gs, "mouse", Phase::move, 200, 100, 1534, false, &gi);
        assert(gs.queue.empty() && !gs.two && !gs.hover);
        // Queued point actions from the panel are discarded with the notice.
        gs.notice = ""; enqueue(gs, 100, 100, 0x4000);
        gi.fields = 1540; gi.ui_field = 1540; gi.cursor_field = 1540;
        assert(read(gs, gi, false).buttons == 0 && gs.queue.empty() && has(gs.notice, "one-player"));
        // Long-press keeps the transient controller fallback panel.
        pointer(gs, "1", Phase::down, 450, 350, 1550, true, &gi);
        for (double f = 1550; f <= 1580; ++f) {
            gi.fields = f; gi.ui_field = f; gi.cursor_field = f;
            read(gs, gi, false);
        }
        assert(gs.radial && gs.radial->x == 450 && gs.radial->await_release);
        // Controller pulses from the panel still work in this layout.
        button(gs, 0x0008, 1600);
        gi.fields = 1600; gi.ui_field = 1600; assert(read(gs, gi, false).buttons == 0x0008);
    }
    std::puts("2P layout gating keeps the controller fallback passed");

    // D-042: a menu whose selected item is not exposed (custom dialog child)
    // cancels with a notice once a newer snapshot agrees, instead of stalling
    // for the 360-field timeout.
    {
        GestureState gs;
        Info gi;
        gi.fields = 2000; gi.ui_field = 2000; gi.ui_mode = "menu"; gi.ui_token = 900; gi.cursor_valid = false;
        gi.ui_items = {UiItem{.id = 0, .group = 1, .x0 = 10, .y0 = 10, .x1 = 100, .y1 = 30, .activate = true},
                       UiItem{.id = 1, .group = 1, .x0 = 10, .y0 = 40, .x1 = 100, .y1 = 60, .activate = true}};
        ui_pointer(gs, "mouse", Phase::down, 50, 50, gi); ui_pointer(gs, "mouse", Phase::up, 50, 50, gi);
        assert(gs.ui && gs.ui->target.value().id == 1);
        assert(read(gs, gi, false).buttons == 0 && gs.ui);
        gi.fields = 2001; // same snapshot: wait
        assert(read(gs, gi, false).buttons == 0 && gs.ui);
        gi.fields = 2002; gi.ui_field = 2002;
        assert(read(gs, gi, false).buttons == 0 && !gs.ui && has(gs.notice, "not exposed"));
        // An exposed selection after a transient gap still navigates normally.
        gs = GestureState{}; gi.fields = 2100; gi.ui_field = 2100;
        ui_pointer(gs, "mouse", Phase::down, 50, 50, gi); ui_pointer(gs, "mouse", Phase::up, 50, 50, gi);
        assert(read(gs, gi, false).buttons == 0);
        gi.ui_items[0].selected = true; gi.fields = 2101; gi.ui_field = 2101;
        assert(read(gs, gi, false).buttons == 0x0400 && gs.ui);
    }
    std::puts("Unexposed menu selection cancels with a notice passed");
}
