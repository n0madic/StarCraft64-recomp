// Screen gestures use controller input except direct minimap navigation. The
// original game owns orders, hit testing and selection. Cursor feedback comes
// from owned render snapshots.
#include "gestures.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace sc64::input {
ServoTuning servo;

namespace {
constexpr double pi = std::numbers::pi;
double square(double v) { return v * v; }
bool in_image(double x, double y) { return finite(x) && finite(y) && x >= 0 && x < 320 && y >= 0 && y < 240; }
// The modifier is held before the A edge and still held after its release:
// the original copies R from held state before acting on A.
Steps chord(unsigned modifier, unsigned bits) { return {{modifier, 4}, {modifier + bits, 8}, {modifier, 6}}; }

std::pair<double, double> stick_from_raw(double x, double y) {
    if (x == 0 && y == 0) return {0, 0};
    // Inverse of pinned ultramodern's octagonal raw-stick conversion.
    const double angle = std::atan2(y, x);
    double octant = floored_mod(angle + 2 * pi, pi / 2);
    if (octant > pi / 4) octant = pi / 2 - octant;
    const double radius = 82 * std::sin(1.39414574) / std::sin(pi - octant - 1.39414574);
    const double length = std::sqrt(x * x + y * y);
    const double scale = std::min(1.0, length / radius) / length;
    return {x * scale, y * scale};
}

PadOutput camera(GestureState& s, CameraGesture& gesture, double x, double y, const Info& info) {
    const double field = info.fields;
    if (!info.camera_valid || !finite(info.camera_divisor) || *info.camera_divisor < 1) {
        s.camera_drive.reset(); return {};
    }
    if (!gesture.camera_x) {
        gesture.camera_x = info.camera_x; gesture.camera_y = info.camera_y;
        gesture.previous_x = gesture.origin_x; gesture.previous_y = gesture.origin_y;
    }
    // The target is a displacement, never a held joystick velocity. Bounds
    // discard impossible movement at map edges rather than storing a backlog.
    const double tx = std::max(info.camera_min_x, std::min(info.camera_max_x, *gesture.camera_x + gesture.previous_x - x));
    const double ty = std::max(info.camera_min_y, std::min(info.camera_max_y, *gesture.camera_y + gesture.previous_y - y));
    gesture.camera_x = tx; gesture.camera_y = ty;
    gesture.previous_x = x; gesture.previous_y = y;
    if (const auto drive = s.camera_drive) {
        if (field < drive->until_field) return {button_z, drive->x, drive->y};
        if (field < drive->settle_field || info.cursor_field.value_or(0) <= drive->until_field) return {};
        s.camera_drive.reset();
    }
    const double dx = tx - info.camera_x, dy = ty - info.camera_y;
    // The original truncates raw input, divides by an integer, rounds scroll
    // to even units, then multiplies by four. One native-image step is 4 px.
    // Short pulses with neutral fields let delayed snapshots retire the motion.
    const auto raw = [&](double error) {
        if (std::abs(error) <= 4) return 0.0;
        const double units = std::abs(error) > 24 ? 4 : 2;
        const double n = 1.5 + units * *info.camera_divisor;
        return error < 0 ? -n : n;
    };
    const auto [sx, sy] = stick_from_raw(raw(dx), -raw(dy));
    if (sx == 0 && sy == 0) return {};
    s.camera_drive = CameraDrive{sx, sy, field + 2, field + 8};
    return {button_z, sx, sy};
}

double axis(double error) {
    if (std::abs(error) < 1) return 0;
    // The original squares stick magnitude. Pulse/settle feedback avoids
    // pretending stick speed is a linear pixel delta (research.md).
    const double magnitude = std::min(.85, .015 + .12 * std::sqrt(std::abs(error)));
    return error < 0 ? -magnitude : magnitude;
}

std::pair<double, double> follow_mouse(GestureState& s, const Info& info, const Point& target, double cap,
                                       double horizon, double age_gain) {
    const double cursor_field = info.cursor_field.value_or(0);
    const double age = info.fields - cursor_field;
    const auto previous = s.follow;
    double vx = 0, vy = 0;
    if (previous && cursor_field >= previous->field) {
        if (cursor_field == previous->field) { vx = previous->vx; vy = previous->vy; }
        else {
            const double elapsed = cursor_field - previous->field;
            if (elapsed <= 12) { vx = (info.cursor_x - previous->x) / elapsed; vy = (info.cursor_y - previous->y) / elapsed; }
        }
    }
    s.follow = Follow{cursor_field, info.cursor_x, info.cursor_y, vx, vy};
    // Predict the in-flight motion using observed snapshots, then continuously
    // steer toward the latest pointer. No 4-on/8-off duty cycle for mouse hover.
    const double dx = target.x - info.cursor_x - vx * (age + 1);
    const double dy = target.y - info.cursor_y - vy * (age + 1);
    horizon = horizon + age_gain * age;
    double x = std::abs(dx) < .5 ? 0 : dx / horizon;
    double y = std::abs(dy) < .5 ? 0 : -dy / horizon;
    const double speed = std::sqrt(x * x + y * y);
    if (speed > cap) { x = x * cap / speed; y = y * cap / speed; }
    if (!finite(info.cursor_divisor) || *info.cursor_divisor <= 0) return {0, 0};
    const double divisor = *info.cursor_divisor;
    const auto raw = [&](double v) {
        if (v == 0) return 0.0;
        // Original deadzone is one raw stick unit; displacement in the native
        // image is signed raw^2/(2*divisor). Half a unit offsets truncation.
        const double n = 1.5 + std::sqrt(std::abs(v) * 2 * divisor);
        return v < 0 ? -n : n;
    };
    x = raw(x); y = raw(y);
    if (x == 0 && y == 0) return {0, 0};
    return stick_from_raw(x, y);
}
}

void clear(GestureState& s) {
    s.queue.clear(); s.contacts.clear(); s.target.reset(); s.active.reset(); s.radial.reset();
    s.hover.reset(); s.drag.reset(); s.pulse.reset(); s.neutral_until = 0; s.stable.reset();
    s.two.reset(); s.drive.reset(); s.settle_until = 0; s.moved_field.reset();
    s.pan.reset(); s.camera_drive.reset();
    s.ui.reset();
    s.hud_region.reset();
    s.hud_mouse.reset(); s.follow.reset();
    s.minimap.reset(); s.minimap_request.reset();
    s.hud_button.reset(); s.button_hover.reset();
    s.tap.reset();
    s.seq.reset();
}

bool set_target(GestureState& s, double x, double y, bool touch) {
    if (!in_image(x, y)) return false;
    s.hover = Hover{x, y, touch};
    return true;
}

bool enqueue(GestureState& s, double x, double y, unsigned buttons, bool hold, bool auto_pick, bool direct_mouse) {
    if (s.queue.size() >= 8 || !set_target(s, x, y)) {
        s.notice = "Input queue full or point outside image"; return false;
    }
    auto action = std::make_shared<Action>();
    action->x = x; action->y = y; action->buttons = buttons; action->hold = hold; action->auto_pick = auto_pick;
    action->direct_mouse = direct_mouse;
    s.queue.push_back(std::move(action));
    return true;
}

void button(GestureState& s, unsigned buttons, double field) {
    clear(s);
    s.pulse = Pulse{buttons, field + 8, std::nullopt};
}

// Each step lasts its field count from its first submission, so a late poll
// can lengthen a step but never skip one. Completion leaves eight neutral fields.
void sequence(GestureState& s, Steps steps, double field) {
    clear(s);
    const double first = steps.front().second;
    s.seq = Sequence{std::move(steps), 0, field + first};
}

bool ui_pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
                bool right, bool is_touch) {
    if (minimap_input::pointer(s, id, phase, x, y, info, right, is_touch)) return true;
    return ui_input::pointer(s, id, phase, x, y, info, right, is_touch);
}

// Corner buttons take pointer events before the UI/map routing. x/y are raw
// native coordinates and may lie in the letterbox.
bool button_pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, const Info& info,
                    bool is_touch) {
    if (!s.enabled) return false;
    const auto result = hud_buttons::pointer(s, id, phase, x, y, info, is_touch);
    if (result.bits) button(s, *result.bits, info.fields);
    return result.consumed;
}

std::optional<MinimapRequest> take_minimap(GestureState& s, const Info& info) { return minimap_input::take(s, info); }
std::optional<Feedback> ui_feedback(GestureState& s, const Info& info) { return hud_input::feedback(s, info); }

bool pointer(GestureState& s, const ContactId& id, Phase phase, double x, double y, double field, bool is_touch,
             const Info* info) {
    if (!s.enabled) return false;
    if (phase == Phase::cancel) { clear(s); return true; }
    const bool layout = !info || map_layout(*info);
    if (phase == Phase::down && !layout) {
        // Unverified layouts (e.g. 2P 512x384) keep only the long-press
        // controller panel; no map tap, drag, order or camera gesture.
        if (!finite(x) || !finite(y) || x < 0 || y < 0 || x >= info->width.value_or(0) || y >= info->height.value_or(0))
            return false;
        if (s.contacts.empty()) {
            auto contact = std::make_shared<Contact>();
            contact->x = x; contact->y = y; contact->last_x = x; contact->last_y = y;
            contact->field = field; contact->touch = is_touch; contact->fallback = true;
            s.contacts[id] = std::move(contact);
        }
        return true;
    }
    if (phase == Phase::down) {
        if (!set_target(s, x, y, is_touch)) return false;
        if (s.contacts.contains(id)) return true;
        const std::size_t count = s.contacts.size();
        const auto first = count ? s.contacts.begin()->second : nullptr;
        if (count >= 2) { clear(s); s.notice = "Use at most two fingers"; return true; }
        auto contact = std::make_shared<Contact>();
        contact->x = x; contact->y = y; contact->last_x = x; contact->last_y = y;
        contact->field = field; contact->touch = is_touch;
        s.contacts[id] = contact;
        if (count == 0) map_taps::down(s, *contact, field, info);
        if (count == 1) {
            // Two-finger tap is a contextual B order. Cancel any one-finger
            // selection before it starts; never issue A followed by B at terrain.
            // A finger re-placed during an unfinished two-finger gesture keeps
            // its history: an earlier pan or HUD handoff can never become a tap.
            const auto prior = s.two;
            Two two;
            two.x = first->x; two.y = first->y;
            two.field = prior ? prior->field : field;
            two.moved = s.drag != nullptr || (prior && prior->moved);
            two.suppress_tap = prior && prior->suppress_tap;
            s.queue.clear(); s.active.reset(); s.drag.reset(); s.target.reset(); s.hover.reset();
            s.drive.reset(); s.stable.reset(); s.pulse.reset(); s.camera_drive.reset();
            s.tap.reset();
            double cx = 0, cy = 0;
            // A first finger may already be dragging when the second arrives.
            // Begin camera displacement at their current positions.
            for (const auto& [_, c] : s.contacts) { cx += c->last_x; cy += c->last_y; }
            two.origin_x = cx / 2; two.origin_y = cy / 2;
            s.two = two;
        }
        return true;
    }
    const auto found = s.contacts.find(id);
    if (found == s.contacts.end()) {
        if (phase == Phase::move && !is_touch && !s.drag && layout) set_target(s, x, y);
        return false;
    }
    const auto p = found->second;
    if (p->fallback) {
        if (phase == Phase::up) {
            s.contacts.erase(id);
            if (!s.radial) s.notice = layout_notice;
        }
        return true;
    }
    if (!in_image(x, y)) { clear(s); return true; }
    p->last_x = x; p->last_y = y;
    // A recognised second tap tolerates finger jitter without becoming an
    // A-held box. Deliberate motion still cancels the order and starts a drag.
    const bool moved = square(x - p->x) + square(y - p->y) > (p->order_tap ? 100 : 9);
    if (s.two) {
        s.two->moved = s.two->moved || moved;
    } else if (moved && !s.radial) {
        map_taps::cancel(s, *p);
        if (!s.drag) {
            s.drag = std::make_shared<Drag>();
            s.drag->x = x; s.drag->y = y;
            s.drag->direct_mouse = !p->touch;
            enqueue(s, p->x, p->y, button_a, true, false, !p->touch);
            s.hover.reset();
        }
        s.drag->x = x; s.drag->y = y;
    }
    if (phase == Phase::up) {
        s.contacts.erase(id);
        if (s.two) {
            s.camera_drive.reset();
            if (s.contacts.empty()) {
                const auto two = *s.two;
                s.two.reset();
                if (!two.moved && !two.suppress_tap && field - two.field < 36) enqueue(s, two.x, two.y, button_b);
            }
        } else if (s.radial) {
            // The GUI resolves the selected radial item before releasing.
        } else if (s.drag) {
            s.drag->released = true; s.drag->released_field = field;
        } else if (!map_taps::release(s, *p, x, y, info)) {
            if (enqueue(s, x, y, button_a, false, true, !p->touch)) map_taps::defer(s, s.queue.back(), field, info);
        }
    }
    return true;
}

void pan_pointer(GestureState& s, Phase phase, double x, double y) {
    if (!finite(x) || !finite(y)) { clear(s); return; }
    if (phase == Phase::down) {
        clear(s);
        Pan pan;
        pan.origin_x = x; pan.origin_y = y; pan.x = x; pan.y = y;
        s.pan = pan;
    } else if (phase == Phase::up || phase == Phase::cancel) clear(s);
    else if (s.pan) { s.pan->x = x; s.pan->y = y; }
}

PadOutput read(GestureState& s, const Info& info, bool blocked) {
    const double field = info.fields;
    if (blocked || !s.enabled) { clear(s); return {}; }
    if (s.minimap || s.minimap_request) {
        if (!mode_is(info, "game") || !info.minimap) s.minimap_request.reset();
        return {};
    }
    if (const auto ui = ui_input::read(s, info)) return *ui;
    if (s.pulse) {
        if (field < s.pulse->until_field) return {s.pulse->buttons};
        s.pulse.reset(); s.neutral_until = field + 8;
    }
    if (s.seq) {
        auto& q = *s.seq;
        if (!mode_is(info, "game") && fresh_ui(info)) {
            // A dialog/menu appeared: do not finish the chord inside it.
            clear(s); s.neutral_until = field + 8;
            s.notice = "Same-type selection interrupted"; return {};
        }
        while (field >= q.until_field) {
            ++q.index;
            if (q.index >= q.steps.size()) { s.seq.reset(); s.neutral_until = field + 8; return {}; }
            q.until_field = field + q.steps[q.index].second;
        }
        return {q.steps[q.index].first};
    }
    if (field < s.neutral_until) return {};
    if (ui_input::active(info)) {
        s.queue.clear(); s.target.reset(); s.active.reset(); s.hover.reset(); s.drag.reset();
        s.pan.reset(); s.two.reset(); s.drive.reset(); s.contacts.clear();
        s.tap.reset();
        return {};
    }
    if (!map_layout(info)) {
        if (!s.queue.empty() || s.active || s.drag || s.hover || s.two || s.pan) s.notice = layout_notice;
        s.queue.clear(); s.active.reset(); s.target.reset(); s.hover.reset(); s.drag.reset(); s.two.reset();
        s.pan.reset(); s.drive.reset(); s.camera_drive.reset(); s.tap.reset(); s.follow.reset();
        for (const auto& [_, p] : s.contacts)
            if (field - p->field >= 30 && !s.radial) s.radial = Radial{p->x, p->y, true};
        return {};
    }
    if (s.pan || (s.two && s.two->moved)) {
        if (!info.cursor_valid || !fresh(info, info.cursor_field, 0)) { s.camera_drive.reset(); return {}; }
        if (s.pan) return camera(s, *s.pan, s.pan->x, s.pan->y, info);
        std::size_t count = 0;
        double cx = 0, cy = 0;
        for (const auto& [_, p] : s.contacts) { ++count; cx += p->last_x; cy += p->last_y; }
        if (count != 2) return {};
        return camera(s, *s.two, cx / 2, cy / 2, info);
    }
    if (s.two) return {};
    for (const auto& [_, p] : s.contacts) {
        if (!s.two && !s.drag && field - p->field >= 30) {
            if (!s.radial) s.radial = Radial{p->x, p->y, true};
            s.hover.reset();
        }
    }
    if (s.radial) return {};
    // Once A has started, s.active no longer exists. Bound completion from
    // physical release independently, while allowing a held drag to continue.
    if (s.drag && s.drag->released && field - s.drag->released_field >= 360) {
        clear(s); s.neutral_until = field + 8;
        s.notice = "Cursor did not reach point; selection cancelled"; return {};
    }
    // A started selection drag owns the cursor until it completes. Actions
    // queued behind it (e.g. a right-click B) wait for their own target.
    if (!s.active && !(s.drag && s.drag->started) && !s.queue.empty()) {
        s.active = s.queue.front(); s.queue.pop_front(); s.active->started = field;
        s.stable.reset(); s.target = s.active;
    }
    if (s.active && !map_taps::valid(s, s.active, info)) return {};
    if (s.drag && s.drag->started) s.target = s.drag;
    std::optional<Point> target;
    if (s.target) target = Point{s.target->x, s.target->y};
    else if (s.hover) target = Point{s.hover->x, s.hover->y};
    const unsigned held = s.drag && s.drag->started ? button_a : 0;
    if (!target) return {held};
    if (s.active && field - s.active->started.value_or(field) > 360) {
        clear(s); s.notice = "Cursor did not reach point; action cancelled"; return {};
    }
    if (!info.cursor_valid || !fresh(info, info.cursor_field, 0)) {
        // Losing gameplay feedback never turns a terrain click into an A pulse.
        if (s.drag && s.drag->started) { clear(s); s.notice = "Cursor feedback lost; selection cancelled"; }
        return {};
    }
    if (!s.active && !s.drag && s.hover) {
        s.drive.reset(); s.settle_until = 0;
        const bool touch = s.hover->touch;
        const auto [x, y] = follow_mouse(s, info, Point{s.hover->x, s.hover->y},
            touch ? servo.action_step_cap : servo.hover_step_cap,
            touch ? servo.action_horizon : servo.hover_horizon,
            touch ? servo.action_age_gain : servo.hover_age_gain);
        return {0, x, y};
    }
    // Use the same latency-aware continuous controller for touch arrivals and
    // selection drags when the researched divisor is available. The fallback
    // retains bounded pulse steering for incomplete diagnostic feedback.
    const bool continuous = finite(info.cursor_divisor) && *info.cursor_divisor > 0;
    if (continuous) {
        s.drive.reset(); s.settle_until = 0;
        const auto [x, y] = follow_mouse(s, info, *target, servo.action_step_cap, servo.action_horizon,
                                         servo.action_age_gain);
        if (std::abs(target->x - info.cursor_x) > 1.5 || std::abs(target->y - info.cursor_y) > 1.5) {
            s.stable.reset();
            if (x != 0 || y != 0) s.moved_field = field;
            return {held, x, y};
        }
    } else s.follow.reset();
    if (s.drive && field < s.drive->until_field) return {held, s.drive->x, s.drive->y};
    if (s.drive) { s.drive.reset(); s.settle_until = field + 8; }
    if (field < s.settle_until) return {held};
    const double dx = target->x - info.cursor_x, dy = target->y - info.cursor_y;
    const double cursor_field = info.cursor_field.value_or(0);
    if (std::abs(dx) <= 1.5 && std::abs(dy) <= 1.5) {
        if (!s.stable) { s.stable = cursor_field; return {held}; }
        if (cursor_field <= *s.stable) return {held};
        // Arrive only once a snapshot postdates the last stick motion we sent
        // (plus the guest's poll/render delay): a stale snapshot can show the
        // point while in-flight motion still carries the cursor past it.
        if (s.moved_field && cursor_field <= *s.moved_field + servo.arrival_margin) return {held};
        if (s.drag && s.drag->started) {
            if (s.drag->released) {
                s.drag.reset(); s.target.reset(); s.hover.reset(); s.neutral_until = field + 8; return {};
            }
        } else if (s.active) {
            const auto action = s.active;
            if (!map_taps::ready(s, action, field, info)) return {};
            if ((action->auto_pick || action->double_tap) && info.pick_required) {
                const auto& p = info.cursor_pick;
                // Decide at arrival, using the game's hit test at this point
                // and selection. Stale/missing picks wait instead of selecting
                // minerals before a queued gather order can be issued.
                if (!mode_is(info, "game") || !fresh_ui(info) || !p || std::optional<double>(p->context) != info.ui_context
                    || !finite(p->x) || !finite(p->y) || std::abs(p->x - action->x) > 1.5
                    || std::abs(p->y - action->y) > 1.5 || (p->buttons != button_a && p->buttons != button_b))
                    return {};
                if (action->auto_pick) action->buttons = unsigned(p->buttons);
                else if (p->target_selected) {
                    // Double tap on an already selected unit selects every unit
                    // of its type on screen (original R+A), as in PC StarCraft's
                    // double click; other double taps stay B.
                    action->buttons = 0; action->sequence = chord(button_r, button_a);
                }
            }
            s.active.reset(); s.target.reset();
            if (action->sequence) {
                sequence(s, *action->sequence, field);
                return {s.seq->steps.front().first};
            }
            if (action->hold && s.drag) { s.drag->started = true; s.target = s.drag; return {button_a}; }
            if (action->buttons != 0) {
                // Keep an order's target stable for the existing six-field
                // minimum before letting mouse hover resume during its tail.
                const auto hover_after = action->direct_mouse && action->buttons == button_b
                    ? std::optional<double>(field + 6) : std::nullopt;
                s.pulse = Pulse{action->buttons, field + 8, hover_after};
                return {action->buttons};
            }
        }
        return {held};
    }
    s.stable.reset();
    // Native osContGetReadData preserves Y sign; original cursor subtracts Y.
    if (continuous) {
        const auto [x, y] = follow_mouse(s, info, *target, servo.action_step_cap, servo.action_horizon,
                                         servo.action_age_gain);
        return {held, x, y};
    }
    double x = axis(dx), y = -axis(dy);
    const double length = std::sqrt(x * x + y * y);
    if (length > 1) { x /= length; y /= length; }
    s.drive = Drive{x, y, field + 4};
    return {held, x, y};
}
}
