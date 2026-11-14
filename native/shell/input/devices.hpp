#pragma once
#include "state.hpp"
#include <array>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>

// Keyboard/gamepad controller state, the diagnostic touch pad, haptic cues and
// the presentation viewport shared by rendering and pointer inversion.
namespace sc64::input {
// Bound action event: keyboard (device -1) or gamepad index 0/1.
struct ActionEvent {
    bool pressed = false, released = false;
    std::optional<double> value;
    std::optional<int> gamepad;
};

namespace pad_state {
struct Device {
    std::map<std::string, double> held, until_field;
    std::map<std::string, unsigned> masks;
};
struct State { std::map<int, Device> devices; };
void clear(State& state);
void input(State& state, std::string_view id, const ActionEvent& action, double field, const Info* info);
PadOutput read(const State& state, int player, double field);
}

namespace touch_pad {
struct ControlDef { std::string_view id, label; unsigned mask; double x, y, size; };
const std::array<ControlDef, 15>& controls();
struct ControlLayout { double x = 0, y = 0, size = 0, opacity = 0; };
struct Layout { std::map<std::string, ControlLayout> controls; };
// Values outside a control's limits are clamped; missing values take defaults.
struct SavedControl { std::optional<double> x, y, size, opacity; };
Layout layout(const std::map<std::string, SavedControl>* saved = nullptr);
struct PadPointer { std::string control; double x = 0, y = 0; };
struct State {
    Layout layout = touch_pad::layout();
    std::map<ContactId, PadPointer> pointers;
    std::map<std::string, double> pulses;
    std::set<int> physical;
    bool enabled = true, editing = false, blocked = false, multi = false;
    std::string selected = "stick";
    int player = 1;
};
void clear(State& s);
bool visible(const State& s);
void device(State& s, int id, bool connected);
void block(State& s, bool blocked);
struct Geometry { double x, y, size; };
Geometry geometry(const State& s, std::string_view id, double w, double h);
bool pointer(State& s, const ContactId& id, Phase phase, double x, double y, double field, double w, double h);
PadOutput read(const State& s, double field, double w, double h);
void edit(State& s, bool enabled);
void adjust(State& s, double size_delta, double opacity_delta);
}

namespace haptics {
// Seconds between pulses, so a burst of submitted presses stays one cue.
constexpr double min_gap = 0.06;
struct State { unsigned previous = 0; double last = -std::numeric_limits<double>::infinity(); };
struct Pulse { int milliseconds, amplitude; };
// A rising submitted button edge from pointer or touch-panel input.
std::optional<Pulse> update(State& h, unsigned bits, bool pointer, double now);
}

namespace viewport {
struct Rect { double x, y, width, height, nw, nh; };
// Largest aspect-preserving Fit; nothing for an empty window or image.
std::optional<Rect> rectangle(double w, double h, double nw, double nh);
// Pixel centres, bottom-left host coordinates, top-left native image. The raw
// form is unclipped: coordinates outside the image lie outside [0,nw) x [0,nh).
std::optional<Point> to_native_raw(const std::optional<Rect>& rect, double x, double y);
std::optional<Point> to_native(const std::optional<Rect>& rect, double x, double y);
// The nearest image pixel to a host point, also from the letterbox around the image.
std::optional<Point> to_native_clamped(const std::optional<Rect>& rect, double x, double y);
}

namespace cursor_policy {
// Normalized bottom-left window region where the OS cursor is hidden.
std::optional<std::array<double, 4>> region(const Info& info, double w, double h, bool overlay);
}
}
