#include "devices.hpp"
#include "gestures.hpp"
#include <algorithm>
#include <cmath>

namespace sc64::input {
namespace pad_state {
namespace {
// Separate Back aliases keep one release from clearing another held source.
constexpr std::array<std::pair<std::string_view, unsigned>, 16> buttons = {{
    {"a", button_a}, {"b", button_b}, {"escape_back", button_b}, {"android_back", button_b},
    {"start", button_start}, {"z", button_z}, {"l", button_l}, {"r", button_r},
    {"cup", 0x0008}, {"cdown", 0x0004}, {"cleft", 0x0002}, {"cright", 0x0001},
    {"dup", button_up}, {"ddown", button_down}, {"dleft", button_left}, {"dright", button_right},
}};
std::optional<unsigned> mask_of(std::string_view id) {
    for (const auto& [name, mask] : buttons) if (name == id) return mask;
    return std::nullopt;
}
double held(const Device& values, const char* id) {
    const auto found = values.held.find(id);
    return found == values.held.end() ? 0 : found->second;
}
}

void clear(State& state) { state.devices.clear(); }

void input(State& state, std::string_view id, const ActionEvent& action, double field, const Info* info) {
    const int device = action.gamepad.value_or(-1);
    if (device < -1 || device > 1) return;
    if (id == "disconnected") { state.devices.erase(device); return; }
    auto& values = state.devices[device];
    const std::string key(id);
    if (action.released) values.held.erase(key);
    else if (action.value) values.held[key] = std::abs(*action.value);
    else if (action.pressed) values.held[key] = 1;
    const auto base = mask_of(id);
    if (base && action.pressed) {
        unsigned mask = *base;
        if (id == "escape_back" || id == "android_back") {
            if (info) {
                const double age = field - info->ui_field.value_or(-1000);
                if (mode_is(*info, "game") && age >= 0 && age <= max_feedback_age) mask = button_start;
            }
        }
        // Original Start leaves the Encyclopedia; Enter follows the highlighted
        // article link instead (A). Gamepad Start keeps its original meaning.
        if (id == "start" && device == -1 && info && mode_is(*info, "menu")
            && info->ui_token == hud_buttons::encyclopedia_token
            && field - info->ui_field.value_or(-1000) <= max_feedback_age)
            mask = button_a;
        // Resolve once at key-down: opening the menu must not turn the held
        // Start source into a B order/back on the next presentation frame.
        values.masks[key] = mask;
        // Preserve a short desktop press until at least one guest input poll.
        // A press/release pair can arrive within one slower presentation frame.
        values.until_field[key] = field + 6;
    }
}

PadOutput read(const State& state, int player, double field) {
    PadOutput out;
    for (const auto& [device, values] : state.devices) {
        if ((device == -1 && player == 1) || device + 1 == player) {
            for (const auto& [name, mask] : buttons) {
                const std::string key(name);
                const auto until = values.until_field.find(key);
                if (held(values, key.c_str()) > 0.5 || (until != values.until_field.end() && until->second > field)) {
                    const auto resolved = values.masks.find(key);
                    out.buttons |= resolved == values.masks.end() ? mask : resolved->second;
                }
            }
            out.x += held(values, "right") - held(values, "left");
            out.y += held(values, "up") - held(values, "down");
        }
    }
    out.x = std::clamp(out.x, -1.0, 1.0); out.y = std::clamp(out.y, -1.0, 1.0);
    return out;
}
}

namespace touch_pad {
namespace {
// Controller input only. Direct guest cursor/order semantics require D-040.
const std::array<ControlDef, 15> control_defs = {{
    {"stick", "STICK", 0, .15, .22, .22},
    {"a", "A", 0x8000, .90, .22, 0}, {"b", "B", 0x4000, .80, .14, 0},
    {"z", "Z", 0x2000, .80, .30, 0}, {"start", "START", 0x1000, .50, .10, 0},
    {"l", "L", 0x0020, .10, .87, 0}, {"r", "R", 0x0010, .90, .87, 0},
    {"cup", "C UP", 0x0008, .88, .62, 0}, {"cdown", "C DN", 0x0004, .88, .42, 0},
    {"cleft", "C LT", 0x0002, .80, .52, 0}, {"cright", "C RT", 0x0001, .96, .52, 0},
    {"dup", "UP", 0x0800, .12, .62, 0}, {"ddown", "DOWN", 0x0400, .12, .42, 0},
    {"dleft", "LEFT", 0x0200, .04, .52, 0}, {"dright", "RIGHT", 0x0100, .20, .52, 0},
}};
double clamp(double v, double a, double b) { return std::max(a, std::min(b, v)); }
const ControlDef& def(std::string_view id) {
    for (const auto& c : control_defs) if (c.id == id) return c;
    return control_defs[0];
}
bool inside(const State& s, std::string_view id, double x, double y, double w, double h) {
    const auto [cx, cy, size] = geometry(s, id, w, h);
    if (id == "stick") return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= (size / 2) * (size / 2);
    return std::abs(x - cx) <= size / 2 && std::abs(y - cy) <= size / 2;
}
}

const std::array<ControlDef, 15>& controls() { return control_defs; }

Layout layout(const std::map<std::string, SavedControl>* saved) {
    Layout result;
    for (const auto& c : control_defs) {
        SavedControl old;
        if (saved) if (const auto found = saved->find(std::string(c.id)); found != saved->end()) old = found->second;
        const auto value = [](std::optional<double> v, double fallback, double lo, double hi) {
            return finite(v) ? clamp(*v, lo, hi) : fallback;
        };
        result.controls[std::string(c.id)] = {value(old.x, c.x, .02, .98), value(old.y, c.y, .02, .98),
            value(old.size, c.size > 0 ? c.size : .085, .04, .30), value(old.opacity, .45, .1, .9)};
    }
    return result;
}

void clear(State& s) { s.pointers.clear(); s.pulses.clear(); }
bool visible(const State& s) { return s.enabled && s.physical.empty(); }
void device(State& s, int id, bool connected) {
    if (connected) s.physical.insert(id); else s.physical.erase(id);
    clear(s);
}
void block(State& s, bool blocked) {
    if (blocked && !s.blocked) clear(s);
    s.blocked = blocked;
}
Geometry geometry(const State& s, std::string_view id, double w, double h) {
    const auto& c = s.layout.controls.at(std::string(id));
    const double size = c.size * std::min(w, h);
    // Keep the whole hit box visible after aspect changes or layout edits.
    return {clamp(c.x * w, size / 2, w - size / 2), clamp(c.y * h, size / 2, h - size / 2), size};
}

bool pointer(State& s, const ContactId& id, Phase phase, double x, double y, double field, double w, double h) {
    if (phase == Phase::cancel) { s.pointers.erase(id); s.pulses.clear(); return true; }
    if (phase == Phase::up) return s.pointers.erase(id) > 0;
    if (!visible(s) || (s.blocked && !s.editing)) return false;
    auto found = s.pointers.find(id);
    // A repeated press of a still-captured contact keeps acting on its previous
    // capture when it misses every control, as the original adapter did.
    std::optional<PadPointer> previous;
    if (phase == Phase::down) {
        if (found != s.pointers.end()) { previous = found->second; s.pointers.erase(found); }
        found = s.pointers.end();
        for (const auto& c : control_defs) {
            if (inside(s, c.id, x, y, w, h)) {
                for (const auto& [_, other] : s.pointers)
                    if (other.control == c.id && (s.editing || c.id == "stick")) return true;
                found = s.pointers.emplace(id, PadPointer{std::string(c.id), x, y}).first;
                previous.reset();
                if (s.editing) s.selected = c.id;
                else if (c.mask) s.pulses[std::string(c.id)] = field + 6;
                break;
            }
        }
    }
    if (found == s.pointers.end() && !previous) return false;
    auto& p = found != s.pointers.end() ? found->second : *previous;
    p.x = x; p.y = y;
    if (s.editing) {
        auto& c = s.layout.controls.at(p.control);
        c.x = clamp(x / w, .02, .98); c.y = clamp(y / h, .02, .98);
    } else if (p.control != "stick" && !inside(s, p.control, x, y, w, h)) {
        // Dragging out cancels capture; sliding back cannot generate a new press.
        s.pulses.erase(p.control);
        s.pointers.erase(id);
    }
    return true;
}

PadOutput read(const State& s, double field, double w, double h) {
    if (!visible(s) || s.blocked || s.editing) return {};
    PadOutput out;
    for (const auto& c : control_defs) {
        const auto pulse = s.pulses.find(std::string(c.id));
        if (c.mask && pulse != s.pulses.end() && pulse->second > field) out.buttons |= c.mask;
    }
    for (const auto& [_, p] : s.pointers) {
        if (p.control == "stick") {
            const auto [cx, cy, size] = geometry(s, "stick", w, h);
            const double dx = (p.x - cx) / (size / 2), dy = (p.y - cy) / (size / 2);
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance > .15) {
                const double magnitude = clamp((distance - .15) / .85, 0, 1);
                out.x = dx / distance * magnitude; out.y = dy / distance * magnitude;
            }
        } else out.buttons |= def(p.control).mask;
    }
    return out;
}

void edit(State& s, bool enabled) { clear(s); s.editing = enabled; }
void adjust(State& s, double size_delta, double opacity_delta) {
    auto& c = s.layout.controls.at(s.selected);
    c.size = clamp(c.size + size_delta, .04, .30);
    c.opacity = clamp(c.opacity + opacity_delta, .1, .9);
}
}

namespace haptics {
namespace {
// Orders (B) are stronger than selections/confirmations (A, Start).
struct Cue { unsigned button; int ms, amplitude; };
constexpr std::array<Cue, 3> cues = {{{button_b, 22, 170}, {button_a, 12, 90}, {button_start, 12, 90}}};
}
std::optional<Pulse> update(State& h, unsigned bits, bool pointer, double now) {
    const unsigned rising = bits & ~h.previous;
    h.previous = bits;
    if (!pointer || rising == 0 || now - h.last < min_gap) return std::nullopt;
    for (const auto& cue : cues) {
        if (rising & cue.button) {
            h.last = now;
            return Pulse{cue.ms, cue.amplitude};
        }
    }
    return std::nullopt;
}
}

namespace viewport {
std::optional<Rect> rectangle(double w, double h, double nw, double nh) {
    if (w <= 0 || h <= 0 || nw <= 0 || nh <= 0) return std::nullopt;
    const double scale = std::min(w / nw, h / nh);
    const double width = std::floor(nw * scale), height = std::floor(nh * scale);
    return Rect{std::floor((w - width) / 2), std::floor((h - height) / 2), width, height, nw, nh};
}
std::optional<Point> to_native_raw(const std::optional<Rect>& rect, double x, double y) {
    if (!rect) return std::nullopt;
    return Point{(x - rect->x + .5) * rect->nw / rect->width - .5,
                 rect->nh - .5 - (y - rect->y + .5) * rect->nh / rect->height};
}
std::optional<Point> to_native(const std::optional<Rect>& rect, double x, double y) {
    if (!rect || x < rect->x || y < rect->y || x >= rect->x + rect->width || y >= rect->y + rect->height)
        return std::nullopt;
    auto point = to_native_raw(rect, x, y);
    // Pixel-centre inversion can put an in-viewport edge pixel slightly
    // outside the image at scaled sizes. Keep real edge input reachable.
    point->x = std::clamp(point->x, 0.0, std::nextafter(rect->nw, 0.0));
    point->y = std::clamp(point->y, 0.0, std::nextafter(rect->nh, 0.0));
    return point;
}
}

namespace cursor_policy {
std::optional<std::array<double, 4>> region(const Info& info, double w, double h, bool overlay) {
    if (!info.running || info.paused || !mode_is(info, "game") || !info.cursor_valid
        || !fresh(info, info.cursor_field) || overlay || info.width != 320 || info.height != 240)
        return std::nullopt;
    const auto rect = viewport::rectangle(w, h, *info.width, *info.height);
    if (!rect) return std::nullopt;
    return std::array<double, 4>{rect->x / w, rect->y / h, (rect->x + rect->width) / w, (rect->y + rect->height) / h};
}
}
}
