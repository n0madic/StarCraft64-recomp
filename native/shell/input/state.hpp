#pragma once
#include "info.hpp"
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Pointer gesture state shared by the map, menu, HUD, minimap and corner-button
// adapters. Contact identifiers are "mouse", "recipe" or a touch finger id.
namespace sc64::input {
using ContactId = std::string;
using Step = std::pair<unsigned, double>; // controller mask, field count
using Steps = std::vector<Step>;

// Actions and drags are shared as cursor targets through shared_ptr<Point>;
// the control block keeps the derived deleter, so no virtual destructor.
struct Point { double x = 0, y = 0; };
struct Action : Point {
    unsigned buttons = 0;
    bool hold = false, auto_pick = false, double_tap = false, tap_held = false, direct_mouse = false;
    std::optional<double> started, tap_until, context, token;
    std::optional<Steps> sequence;
};
struct Drag : Point {
    bool released = false, started = false, direct_mouse = false;
    double released_field = 0;
};
struct Contact {
    double x = 0, y = 0, last_x = 0, last_y = 0, field = 0;
    bool touch = false, fallback = false;
    std::shared_ptr<Action> order_tap;
};
struct Hover { double x = 0, y = 0; bool touch = false; };
struct Radial { double x = 0, y = 0; bool await_release = false; };
struct Pulse { unsigned buttons = 0; double until_field = 0; std::optional<double> mouse_hover_after; };
// Two-finger and middle-button camera gestures share displacement tracking.
struct CameraGesture {
    double origin_x = 0, origin_y = 0;
    std::optional<double> camera_x, camera_y;
    double previous_x = 0, previous_y = 0;
};
struct Two : CameraGesture {
    double x = 0, y = 0, field = 0;
    bool moved = false, suppress_tap = false;
};
struct Pan : CameraGesture { double x = 0, y = 0; };
struct Drive { double x = 0, y = 0, until_field = 0; };
struct CameraDrive { double x = 0, y = 0, until_field = 0, settle_field = 0; };
struct Follow { double field = 0, x = 0, y = 0, vx = 0, vy = 0; };
struct Sequence { Steps steps; std::size_t index = 0; double until_field = 0; };
struct HudRegion { Bounds bounds; std::optional<double> context; double field = 0; };
struct MinimapGesture {
    ContactId contact;
    std::set<ContactId> contacts;
    Bounds bounds;
    std::optional<double> context;
    double x = 0, y = 0, last_x = 0, last_y = 0;
    bool cancelled = false;
};
struct MinimapRequest { double x = 0, y = 0; std::optional<double> context; double field = 0; };
struct ButtonContact {
    ContactId contact;
    std::string id, set;
    std::set<ContactId> contacts;
    bool cancelled = false;
};
struct UiObserved { std::optional<double> token; std::optional<std::string> mode; double since = 0; };
struct TwoPlayerExit { double next_field = 0, until_field = 0; };

// Menu navigation target: an item id within a group, optionally pinned to a
// tab selection or an Encyclopedia article context.
struct UiTarget { double id = 0, group = 0; std::optional<double> context, tab; };
struct UiWait { double id = 0, field = 0, sent = 0, retries = 0; unsigned bits = 0; double x = 0, y = 0; };
struct HudOrigin { double x = 0, y = 0, field = 0; bool touch = false, right = false; };
struct HudPulse { UiItem target; double until_field = 0; };

// Menu pulses and HUD hint presses own the pointer through one record; `hud`
// selects which half of the fields is meaningful.
struct UiState {
    bool hud = false;
    std::optional<double> token;
    std::optional<std::string> mode;
    std::optional<ContactId> contact;
    double x = 0, y = 0, started = 0;
    // Menu navigation.
    bool carousel = false, pager = false, confirm = false;
    double campaign = 0;
    unsigned bits = 0;
    double until_field = 0, neutral_until = 0, x_axis = 0, y_axis = 0, max_move = 0;
    std::optional<UiTarget> target;
    std::optional<Point> close_to;
    std::optional<double> sent_field, unexposed;
    std::optional<UiWait> wait;
    // HUD hint presses.
    std::set<ContactId> contacts;
    std::vector<UiItem> queue;
    std::optional<HudOrigin> origin;
    std::optional<UiItem> hud_target;
    bool pressed = false, cancelled = false;
    std::optional<HudPulse> pulse, flash;
};

struct GestureState {
    std::deque<std::shared_ptr<Action>> queue;
    std::map<ContactId, std::shared_ptr<Contact>> contacts;
    std::string notice;
    bool enabled = true;
    std::shared_ptr<Point> target;
    std::shared_ptr<Action> active;
    std::optional<Radial> radial;
    std::optional<Hover> hover;
    std::shared_ptr<Drag> drag;
    std::optional<Pulse> pulse;
    double neutral_until = 0;
    std::optional<double> stable;
    std::optional<Two> two;
    std::optional<Drive> drive;
    double settle_until = 0;
    std::optional<double> moved_field;
    std::optional<Pan> pan;
    std::optional<CameraDrive> camera_drive;
    std::shared_ptr<UiState> ui;
    std::optional<HudRegion> hud_region;
    std::optional<Point> hud_mouse;
    std::optional<Follow> follow;
    std::optional<MinimapGesture> minimap;
    std::optional<MinimapRequest> minimap_request;
    std::optional<ButtonContact> hud_button;
    std::optional<std::string> button_hover;
    std::shared_ptr<Action> tap;
    std::optional<Sequence> seq;
    std::optional<UiObserved> ui_observed;
    std::optional<TwoPlayerExit> two_player_exit;
};

enum class Phase { down, move, up, cancel };
// A pointer read result: controller mask and analog stick.
struct PadOutput {
    unsigned buttons = 0;
    double x = 0, y = 0;
    bool operator==(const PadOutput&) const = default;
};
}
