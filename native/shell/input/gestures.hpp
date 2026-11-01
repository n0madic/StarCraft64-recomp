#pragma once
#include "state.hpp"
#include <array>
#include <string_view>

// Screen gestures produce original controller input, except direct minimap
// navigation. The original game owns orders, hit testing and selection; the
// adapters only read owned feedback snapshots (see docs/input.md).
namespace sc64::input {
constexpr unsigned button_a = 0x8000, button_b = 0x4000, button_z = 0x2000, button_start = 0x1000;
constexpr unsigned button_up = 0x0800, button_down = 0x0400, button_left = 0x0200, button_right = 0x0100;
constexpr unsigned button_l = 0x0020, button_r = 0x0010;

struct Feedback { UiItem item; double alpha = 0; };

// gestures (map, camera, cursor servo)
inline constexpr std::string_view layout_notice =
    "Map gestures need the one-player screen; long-press for controller commands";
struct ServoTuning {
    double hover_step_cap = 16, action_step_cap = 16;
    double hover_horizon = 1, action_horizon = 1;
    double hover_age_gain = 1.5, action_age_gain = 1.5;
    double arrival_margin = 2;
};
extern ServoTuning servo; // ADR-066/094 constants; mutable for focused fixtures.
void clear(GestureState& s);
bool set_target(GestureState& s, double x, double y, bool touch = false);
bool enqueue(GestureState& s, double x, double y, unsigned buttons = 0, bool hold = false, bool auto_pick = false,
             bool direct_mouse = false);
void button(GestureState& s, unsigned buttons, double field);
void sequence(GestureState& s, Steps steps, double field);
bool ui_pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
                bool right = false, bool is_touch = false);
bool button_pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
                    bool is_touch = false);
std::optional<MinimapRequest> take_minimap(GestureState& s, const Info& info);
std::optional<Feedback> ui_feedback(GestureState& s, const Info& info);
// `info` is absent when the HUD replays an abandoned press as a map gesture.
bool pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, double field,
             bool is_touch = false, const Info* info = nullptr);
void pan_pointer(GestureState& s, Phase phase, double x, double y);
PadOutput read(GestureState& s, const Info& info, bool blocked = false);

// map_taps: double tap / double click coalescing into one B order.
namespace map_taps {
void defer(GestureState& s, const std::shared_ptr<Action>& action, double field, const Info* info);
void down(GestureState& s, Contact& contact, double field, const Info* info);
void cancel(GestureState& s, Contact& contact);
bool release(GestureState& s, Contact& contact, double x, double y, const Info* info);
bool valid(GestureState& s, const std::shared_ptr<Action>& action, const Info& info);
bool ready(GestureState& s, const std::shared_ptr<Action>& action, double field, const Info& info);
}

// minimap_input: direct minimap navigation requests.
namespace minimap_input {
bool pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
             bool right, bool is_touch);
std::optional<MinimapRequest> take(GestureState& s, const Info& info);
}

// hud_buttons: always-available corner buttons, one set per screen.
namespace hud_buttons {
constexpr double encyclopedia_token = 264;
constexpr double size = 12, margin = 2;
enum class Glyph { menu, map, prev, next };
struct Def {
    std::string_view id;
    unsigned bits;
    Glyph glyph;
    double x0, y0;
    std::array<double, 4> hit;
};
struct Set { std::string_view name; std::array<Def, 3> defs; std::size_t count; };
const std::array<Set, 2>& sets();
std::optional<std::string_view> set(const Info& info);
struct PointerResult { bool consumed = false; std::optional<unsigned> bits; };
PointerResult pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
                      bool is_touch);
// Visual state: "pressed", "hover" or none.
std::optional<std::string_view> state(const GestureState& s, std::string_view id);
}

// hud_input: in-mission command hints are controller buttons.
namespace hud_input {
bool pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
             bool right, bool is_touch, bool map_replay = true);
std::optional<unsigned> read(GestureState& s, const Info& info);
std::optional<Feedback> feedback(GestureState& s, const Info& info);
}

// ui_input: menu clicks navigate the original selection through D-pad pulses.
namespace ui_input {
constexpr double pulse_fields = 4, neutral_fields = 4, retry_fields = 30;
constexpr double cancel_fields = 90, campaign_cancel_fields = 180;
constexpr double main_menu_token = 256, two_player_item = 1, two_player_setup_token = 267;
constexpr double pager_swipe_pixels = 24;
// The mission location card (guest mode 12) is a scripted shot with a 5 s
// display time, but its timer events are only posted in mode 4, so the original
// waits for a button. The adapter continues after the script's display time
// (300 fields) plus a fade margin; the text briefing (mode 4) never auto-advances.
constexpr double briefing_card_mode = 12, briefing_card_fields = 360;
inline constexpr std::string_view two_player_notice = "Two-Player is not supported";
bool active(const Info& info);
bool pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
             bool right, bool is_touch);
std::optional<PadOutput> read(GestureState& s, const Info& info);
}
}
