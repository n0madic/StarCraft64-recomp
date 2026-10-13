#include "scripts.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <system_error>

namespace sc64::shell {
using namespace sc64::input;

// ---------------------------------------------------------------- Config
std::string Config::string(const std::string& key, const std::string& fallback) const {
    const auto found = values_.find(key);
    return found == values_.end() ? fallback : found->second;
}
double Config::number(const std::string& key, double fallback) const {
    const auto found = values_.find(key);
    if (found == values_.end() || found->second.empty()) return fallback;
    try {
        std::size_t used = 0;
        const double value = std::stod(found->second, &used);
        return used == found->second.size() ? value : fallback;
    } catch (const std::exception&) { return fallback; }
}
void Config::parse_arguments(int argc, const char* const* argv) {
    constexpr std::string_view prefix = "--config=";
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (!argument.starts_with(prefix)) continue;
        const auto pair = argument.substr(prefix.size());
        const auto equals = pair.find('=');
        if (equals == std::string_view::npos) continue;
        set(std::string(pair.substr(0, equals)), std::string(pair.substr(equals + 1)));
    }
}
void Config::parse_text(const std::string& text) {
    std::istringstream lines(text);
    std::string line;
    const auto trim = [](std::string value) {
        const auto first = value.find_first_not_of(" \t\r");
        if (first == std::string::npos) return std::string();
        return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
    };
    while (std::getline(lines, line)) {
        if (const auto comment = line.find('#'); comment != std::string::npos) line.resize(comment);
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        set(trim(line.substr(0, equals)), trim(line.substr(equals + 1)));
    }
}

// ---------------------------------------------------------------- JSON
nlohmann::json number(double value) {
    if (std::isfinite(value) && std::floor(value) == value && std::abs(value) < 9.0e15)
        return static_cast<std::int64_t>(value);
    return value;
}
namespace {
void put(nlohmann::json& object, const char* key, const std::optional<double>& value) {
    if (value) object[key] = number(*value);
}
nlohmann::json bounds_json(const Bounds& b) {
    nlohmann::json result{{"x0", number(b.x0)}, {"y0", number(b.y0)}, {"x1", number(b.x1)}, {"y1", number(b.y1)}};
    put(result, "context", b.context);
    return result;
}
nlohmann::json carousel_json(const Carousel& c) {
    return {{"selected", number(c.selected)}, {"x0", number(c.x0)}, {"y0", number(c.y0)}, {"x1", number(c.x1)},
            {"y1", number(c.y1)}, {"ready", c.ready}};
}
nlohmann::json pick_json(const CursorPick& p) {
    return {{"x", number(p.x)}, {"y", number(p.y)}, {"context", number(p.context)}, {"buttons", number(p.buttons)},
            {"target_type", number(p.target_type)}, {"worker_selected", p.worker_selected},
            {"target_selected", p.target_selected}};
}
nlohmann::json items_json(const std::vector<UiItem>& items) {
    auto result = nlohmann::json::array();
    for (const auto& item : items) result.push_back(to_json(item));
    return result;
}
nlohmann::json optional_json(const auto& value, auto convert) {
    return value ? convert(*value) : nlohmann::json();
}
void put_json(nlohmann::json& object, const char* key, nlohmann::json value) {
    if (!value.is_null()) object[key] = std::move(value);
}
// Lua number concatenation: integral values print without a fraction.
std::string lua_number(double value) {
    if (std::floor(value) == value && std::abs(value) < 1e15) return std::to_string(static_cast<long long>(value));
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.14g", value);
    return buffer;
}
nlohmann::json ui_target_json(const UiTarget& t) {
    nlohmann::json result{{"id", number(t.id)}, {"group", number(t.group)}};
    put(result, "context", t.context);
    put(result, "tab", t.tab);
    return result;
}
Phase phase_of(const std::string& name) {
    if (name == "down") return Phase::down;
    if (name == "up") return Phase::up;
    if (name == "cancel") return Phase::cancel;
    return Phase::move;
}
std::string contact_id(const nlohmann::json& value) {
    if (value.is_string()) return value.get<std::string>();
    if (value.is_number()) return lua_number(value.get<double>());
    return value.dump();
}
}

nlohmann::json to_json(const UiItem& item) {
    nlohmann::json result{{"id", number(item.id)}, {"group", number(item.group)}, {"x0", number(item.x0)},
        {"y0", number(item.y0)}, {"x1", number(item.x1)}, {"y1", number(item.y1)}, {"selected", item.selected},
        {"horizontal", item.horizontal}, {"activate", item.activate}};
    put(result, "columns", item.columns);
    put(result, "buttons", item.buttons);
    put(result, "context", item.context);
    put(result, "label", item.label);
    return result;
}

nlohmann::json to_json(const Info& info) {
    nlohmann::json result{{"fields", number(info.fields)}, {"running", info.running}, {"paused", info.paused},
        {"cursor_x", number(info.cursor_x)}, {"cursor_y", number(info.cursor_y)},
        {"cursor_valid", info.cursor_valid}, {"camera_valid", info.camera_valid},
        {"camera_x", number(info.camera_x)}, {"camera_y", number(info.camera_y)},
        {"camera_min_x", number(info.camera_min_x)}, {"camera_max_x", number(info.camera_max_x)},
        {"camera_min_y", number(info.camera_min_y)}, {"camera_max_y", number(info.camera_max_y)},
        {"pick_required", info.pick_required}, {"minimap_jumps", number(info.minimap_jumps)},
        {"minimap_field", number(info.minimap_field)}, {"presented", number(info.presented)},
        {"message_id", number(info.message_id)}, {"guest_mode", number(info.guest_mode)},
        {"error", info.error}, {"ui_items", items_json(info.ui_items)}};
    put(result, "ui_field", info.ui_field);
    put(result, "cursor_field", info.cursor_field);
    put(result, "width", info.width);
    put(result, "height", info.height);
    if (info.ui_mode) result["ui_mode"] = *info.ui_mode;
    put(result, "ui_token", info.ui_token);
    put(result, "ui_context", info.ui_context);
    if (info.input_active) result["input_active"] = *info.input_active;
    put(result, "cursor_divisor", info.cursor_divisor);
    put(result, "camera_divisor", info.camera_divisor);
    put_json(result, "minimap", optional_json(info.minimap, bounds_json));
    put_json(result, "ui_bounds", optional_json(info.ui_bounds, bounds_json));
    put_json(result, "ui_carousel", optional_json(info.ui_carousel, carousel_json));
    put_json(result, "cursor_pick", optional_json(info.cursor_pick, pick_json));
    return result;
}

nlohmann::json touch_layout_json(const touch_pad::Layout& layout) {
    nlohmann::json controls = nlohmann::json::object();
    for (const auto& [id, c] : layout.controls)
        controls[id] = {{"x", c.x}, {"y", c.y}, {"size", c.size}, {"opacity", c.opacity}};
    return {{"version", 1}, {"controls", controls}};
}

touch_pad::Layout touch_layout_from(const std::optional<nlohmann::json>& saved) {
    std::map<std::string, touch_pad::SavedControl> controls;
    if (saved && saved->is_object() && saved->value("version", 0) == 1 && saved->contains("controls")
        && (*saved)["controls"].is_object()) {
        for (const auto& [id, entry] : (*saved)["controls"].items()) {
            if (!entry.is_object()) continue;
            touch_pad::SavedControl c;
            const auto value = [&](const char* key) -> std::optional<double> {
                if (entry.contains(key) && entry[key].is_number()) return entry[key].get<double>();
                return std::nullopt;
            };
            c.x = value("x"); c.y = value("y"); c.size = value("size"); c.opacity = value("opacity");
            controls[id] = c;
        }
    }
    return touch_pad::layout(&controls);
}

std::optional<nlohmann::json> FileLayoutStore::load() {
    std::error_code error;
    if (!std::filesystem::exists(path_, error)) {
        if (error) throw std::runtime_error(error.message());
        return std::nullopt;
    }
    std::ifstream file(path_, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open " + path_.string());
    return nlohmann::json::parse(file);
}

std::optional<std::string> FileLayoutStore::save(const nlohmann::json& layout) {
    try {
        std::filesystem::create_directories(path_.parent_path());
        const auto temporary = path_.parent_path() / (path_.filename().string() + ".tmp");
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.exceptions(std::ios::failbit | std::ios::badbit);
            file << layout.dump(2) << "\n";
        }
        // Publish by rename, as the former engine save did.
        std::filesystem::rename(temporary, path_);
        return std::nullopt;
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
}

// ---------------------------------------------------------------- HostScript
HostScript::HostScript(Shared& shared, NativeApi& native, const Config& config)
    : shared_(shared), native_(native), config_(config) {}

bool HostScript::debug() const { return config_.number("sc64.debug_input", 0) == 1; }

// Manual pause is one of several native pause causes. Only this cause is
// toggled by P, so its state is shared with the status overlay for the hint.
void HostScript::set_user_pause(bool value) {
    paused = value;
    shared_.user_paused = value;
    native_.pause(value);
}

void HostScript::window_event(WindowEvent event) {
    // A contact that began before the window lost the focus may never be reported as ended.
    if (event == WindowEvent::focus_lost || event == WindowEvent::iconified) shared_.pressed.reset();
    if (event == WindowEvent::focus_lost || event == WindowEvent::iconified || event == WindowEvent::resized) {
        pad_state::clear(pads_);
        touch_pad::clear(shared_.pad);
        input::clear(shared_.gestures);
    }
}

void HostScript::init() {
    native_.set_haptics(config_.number("sc64.haptics", 1) == 1);
    elapsed = 0; next_log_ = 0; paused = false;
    shared_.user_paused = false;
    recipe.clear();
    const auto recipe_path = config_.string("sc64.inputs");
    if (!recipe_path.empty()) {
        std::ifstream file(recipe_path, std::ios::binary);
        if (!file) throw std::runtime_error("Cannot open input recipe " + recipe_path);
        for (const auto& entry : nlohmann::json::parse(file)) {
            RecipeItem item;
            item.field = entry.value("field", 0.0);
            if (entry.contains("duration")) item.duration = entry["duration"].get<double>();
            if (entry.contains("key")) item.key = entry["key"].get<std::string>();
            if (entry.contains("when_ui")) item.when_ui = entry["when_ui"].get<std::string>();
            if (entry.contains("when_token")) item.when_token = entry["when_token"].get<double>();
            if (entry.contains("pointer")) {
                const auto& p = entry["pointer"];
                item.pointer = std::array<nlohmann::json, 4>{p.at(0), p.at(1), p.at(2), p.at(3)};
            }
            item.touch = entry.value("touch", false);
            item.right = entry.value("right", false);
            item.auto_pick = entry.value("auto", false);
            const auto pair = [&](const char* key) -> std::optional<std::array<double, 2>> {
                if (!entry.contains(key)) return std::nullopt;
                return std::array<double, 2>{entry[key].at(0).get<double>(), entry[key].at(1).get<double>()};
            };
            item.hover = pair("hover"); item.ui_click = pair("ui_click"); item.point = pair("point");
            if (entry.contains("ui_swipe")) {
                const auto& p = entry["ui_swipe"];
                item.ui_swipe = std::array<double, 4>{p.at(0).get<double>(), p.at(1).get<double>(),
                                                      p.at(2).get<double>(), p.at(3).get<double>()};
            }
            if (entry.contains("pan")) {
                const auto& p = entry["pan"];
                item.pan = std::tuple<std::string, double, double>{p.at(0).get<std::string>(), p.at(1).get<double>(),
                                                                    p.at(2).get<double>()};
            }
            if (entry.contains("buttons")) item.buttons = entry["buttons"].get<double>();
            if (entry.contains("x")) item.x = entry["x"].get<double>();
            if (entry.contains("y")) item.y = entry["y"].get<double>();
            item.player = entry.value("player", 1);
            recipe.push_back(std::move(item));
        }
    }
    if (!config_.string("sc64.gpu_probe").empty()) return;
    if (config_.number("sc64.bundled_rom", 0) == 1) {
        report(Shared::Stage::working, "Loading bundled ROM...");
        const auto error = native_.load_bundled_rom();
        if (error) report(Shared::Stage::failed, *error); else report(Shared::Stage::working, "Starting game...");
        if (error) native_.warn("SC64 bundled ROM load failed: " + *error);
        return;
    }
    auto path = config_.string("sc64.rom");
    if (path.empty()) path = native_.stored_rom().value_or("");
    if (path.empty()) return;
    report(Shared::Stage::working, "Loading ROM...");
    const auto error = native_.load_rom(path);
    if (error) report(Shared::Stage::failed, *error); else report(Shared::Stage::working, "Starting game...");
    if (error) native_.warn("SC64 load failed: " + *error);
}

void HostScript::report(Shared::Stage stage, std::string text) {
    shared_.stage = stage;
    shared_.message = std::move(text);
}

void HostScript::update(double dt) {
    elapsed += dt;
    // A press shows for at least a moment, so that a quick click is seen. Once its picker is open the button keeps
    // that look through NativeApi::choosing(), so a release lost to the picker's window cannot leave it stuck.
    if (shared_.pressed && (native_.choosing() ||
                            (shared_.press_released && elapsed - shared_.press_started >= pressed_look_seconds)))
        shared_.pressed.reset();
    if (const auto event = native_.poll_import()) {
        using Kind = ImportEvent::Kind;
        switch (event->kind) {
        case Kind::working: report(Shared::Stage::working, event->text); break;
        case Kind::started: report(Shared::Stage::working, "Starting game..."); break;
        case Kind::failed: report(Shared::Stage::failed, event->text); break;
        case Kind::notice: report(Shared::Stage::idle, event->text); break;
        }
    }
    const double pause_at = config_.number("sc64.pause_at", 0), resume_at = config_.number("sc64.resume_at", 0);
    if (pause_at > 0 && elapsed >= pause_at && !test_paused_) { test_paused_ = true; set_user_pause(true); }
    if (resume_at > 0 && elapsed >= resume_at && !test_resumed_) { test_resumed_ = true; set_user_pause(false); }
    const auto info = native_.info();
    auto& g = shared_.gestures;
    if (debug()) {
        // Diagnostic trace of original selection feedback: one line per change
        // of the selected item set, with the snapshot's guest field.
        auto selected = nlohmann::json::array();
        std::string joined;
        for (const auto& item : info.ui_items) {
            if (!item.selected) continue;
            const auto key = lua_number(item.group) + ":" + lua_number(item.id);
            selected.push_back(key);
            joined += (joined.empty() ? "" : ",") + key;
        }
        const bool pending = g.ui && g.ui->target;
        if (ui_target_log_ && !pending)
            native_.log("SC64 ui_end " + nlohmann::json{{"field", number(info.fields)},
                {"ui_field", info.ui_field ? number(*info.ui_field) : nlohmann::json()}, {"notice", g.notice},
                {"cleared", g.ui == nullptr}}.dump());
        ui_target_log_ = pending;
        const auto carousel = info.ui_carousel ? number(info.ui_carousel->selected) : nlohmann::json();
        const auto key = info.ui_mode.value_or("nil") + "/" + (info.ui_token ? lua_number(*info.ui_token) : "nil")
            + "/" + joined + "/" + (info.ui_carousel ? lua_number(info.ui_carousel->selected) : "nil");
        if (key != ui_selection_log_) {
            ui_selection_log_ = key;
            nlohmann::json row{{"field", number(info.fields)}, {"selected", selected}};
            put(row, "ui_field", info.ui_field);
            if (info.ui_mode) row["mode"] = *info.ui_mode;
            put(row, "token", info.ui_token);
            put_json(row, "carousel", carousel);
            native_.log("SC64 ui_sel " + row.dump());
        }
    }
    if (minimap_log_ && info.minimap_jumps > minimap_log_->second
        && info.cursor_field.value_or(0) > info.minimap_field) {
        native_.log("SC64 minimap_result " + nlohmann::json{{"field", number(info.fields)},
            {"applied_field", number(info.minimap_field)}, {"requested", number(minimap_log_->first)},
            {"cursor_x", number(info.cursor_x)}, {"cursor_y", number(info.cursor_y)},
            {"camera_x", number(info.camera_x)}, {"camera_y", number(info.camera_y)},
            {"jumps", number(info.minimap_jumps)}}.dump());
        minimap_log_.reset();
    }
    const bool input_blocked = info.paused || !info.running || info.input_active == false;
    touch_pad::block(shared_.pad, input_blocked);
    // A merely connected gamepad hides the diagnostic pad but leaves pointer
    // gestures available; actual pad input below still takes ownership.
    g.enabled = !touch_pad::visible(shared_.pad);
    const auto [sw, sh] = native_.window_size();
    native_.set_cursor_hidden(cursor_policy::region(info, sw, sh, touch_pad::visible(shared_.pad) || g.radial));
    if (input_blocked) { pad_state::clear(pads_); input::clear(g); }
    // Private bounded recipes exercise the same key-down context as OS Back.
    for (auto& item : recipe) {
        if (item.key && info.fields >= item.field && !item.sent && (!item.when_ui || info.ui_mode == item.when_ui)) {
            pad_state::input(pads_, *item.key, ActionEvent{.pressed = true, .released = false, .value = std::nullopt, .gamepad = std::nullopt}, info.fields, &info);
            item.sent = true; item.release_field = info.fields + item.duration.value_or(6);
        }
        if (item.key && item.release_field && info.fields >= *item.release_field && !item.released) {
            pad_state::input(pads_, *item.key, ActionEvent{.pressed = false, .released = true, .value = std::nullopt, .gamepad = std::nullopt}, info.fields, &info);
            item.released = true;
        }
    }
    const double input_width = config_.number("display.width", design_width);
    const double input_height = config_.number("display.height", design_height);
    for (int player = 1; player <= 2; ++player) {
        auto out = pad_state::read(pads_, player, info.fields);
        struct { bool controller = false, touch = false, gesture = false, recipe = false; } source;
        source.controller = out.buttons != 0 || out.x != 0 || out.y != 0;
        if (player == shared_.pad.player) {
            const auto touch = touch_pad::read(shared_.pad, info.fields, input_width, input_height);
            source.touch = touch.buttons != 0 || touch.x != 0 || touch.y != 0;
            out.buttons |= touch.buttons;
            out.x = std::max(-1.0, std::min(1.0, out.x + touch.x));
            out.y = std::max(-1.0, std::min(1.0, out.y + touch.y));
        }
        if (player == 1) {
            const auto gesture = input::read(g, info, input_blocked);
            // Keyboard/gamepad steering takes ownership, discarding pending
            // pointer actions rather than adding two conflicting analog vectors.
            if (out.buttons != 0 || out.x != 0 || out.y != 0) input::clear(g);
            else { out = gesture; source.gesture = true; }
        }
        for (auto& item : recipe) {
            const bool matches = (!item.when_ui || info.ui_mode == item.when_ui)
                && (!item.when_token || info.ui_token == item.when_token);
            const bool due = player == 1 && !item.sent && info.fields >= item.field;
            if (item.pointer && due && matches) {
                // Private canonical-coordinate replay uses the GUI's same
                // UI-first/fallback routing, including HUD-to-map handoff.
                const auto& p = *item.pointer;
                const auto id = contact_id(p[0]);
                const auto phase = phase_of(p[1].get<std::string>());
                const double x = p[2].get<double>(), y = p[3].get<double>();
                if (!ui_pointer(g, id, phase, x, y, info, false, item.touch))
                    input::pointer(g, id, phase, x, y, info.fields, item.touch, &info);
                native_.log("SC64 pointer " + nlohmann::json{{"field", number(info.fields)}, {"id", p[0]},
                    {"phase", p[1]}, {"x", p[2]}, {"y", p[3]}, {"ui", g.ui != nullptr}, {"drag", g.drag != nullptr},
                    {"two", g.two.has_value()}, {"camera_x", number(info.camera_x)},
                    {"camera_y", number(info.camera_y)},
                    {"ui_context", info.ui_context ? number(*info.ui_context) : nlohmann::json()}}.dump());
                item.sent = true;
            } else if (item.hover && due && matches) {
                const double x = (*item.hover)[0], y = (*item.hover)[1];
                const bool consumed = ui_pointer(g, "recipe", Phase::move, x, y, info, false, false);
                if (!consumed) input::pointer(g, "recipe", Phase::move, x, y, info.fields, false);
                if (debug()) {
                    nlohmann::json row{{"field", number(info.fields)}, {"x", number(x)}, {"y", number(y)},
                                       {"hud", consumed}};
                    put(row, "source_field", info.ui_field);
                    if (info.ui_mode) row["mode"] = *info.ui_mode;
                    native_.log("SC64 hover " + row.dump());
                }
                item.sent = true;
            } else if (item.ui_swipe && due && matches) {
                const auto& p = *item.ui_swipe;
                ui_pointer(g, "recipe", Phase::down, p[0], p[1], info, false, true);
                ui_pointer(g, "recipe", Phase::move, p[2], p[3], info, false, true);
                ui_pointer(g, "recipe", Phase::up, p[2], p[3], info, false, true);
                if (debug()) {
                    nlohmann::json row{{"request_field", number(item.field)}, {"field", number(info.fields)},
                        {"from", {number(p[0]), number(p[1])}}, {"to", {number(p[2]), number(p[3])}},
                        {"queued", g.ui != nullptr}};
                    if (info.ui_mode) row["mode"] = *info.ui_mode;
                    put(row, "token", info.ui_token);
                    put(row, "source_field", info.ui_field);
                    put_json(row, "carousel", optional_json(info.ui_carousel, carousel_json));
                    native_.log("SC64 ui_swipe " + row.dump());
                }
                item.sent = true;
            } else if (item.ui_click && due && matches) {
                const double x = (*item.ui_click)[0], y = (*item.ui_click)[1];
                ui_pointer(g, "recipe", Phase::down, x, y, info, item.right);
                ui_pointer(g, "recipe", Phase::up, x, y, info, item.right);
                if (debug()) {
                    nlohmann::json row{{"request_field", number(item.field)}, {"field", number(info.fields)},
                        {"x", number(x)}, {"y", number(y)}, {"queued", g.ui != nullptr},
                        {"items", items_json(info.ui_items)}};
                    if (item.right) row["right"] = true;
                    if (info.ui_mode) row["mode"] = *info.ui_mode;
                    put(row, "token", info.ui_token);
                    put(row, "source_field", info.ui_field);
                    put_json(row, "carousel", optional_json(info.ui_carousel, carousel_json));
                    put_json(row, "bounds", optional_json(info.ui_bounds, bounds_json));
                    if (g.ui && g.ui->target) row["target"] = ui_target_json(*g.ui->target);
                    native_.log("SC64 ui_click " + row.dump());
                }
                item.sent = true;
            } else if (item.pan && due) {
                const auto& [phase, x, y] = *item.pan;
                pan_pointer(g, phase_of(phase), x, y);
                native_.log("SC64 pan " + nlohmann::json{{"field", number(info.fields)}, {"phase", phase},
                    {"camera_x", number(info.camera_x)}, {"camera_y", number(info.camera_y)},
                    {"camera_valid", info.camera_valid}}.dump());
                item.sent = true;
            } else if (item.point && due) {
                enqueue(g, (*item.point)[0], (*item.point)[1], unsigned(item.buttons.value_or(0)), false,
                        item.auto_pick);
                item.sent = true;
            } else if (!item.pointer && !item.key && !item.pan && !item.point && !item.ui_click && !item.ui_swipe
                       && !item.hover && item.player == player && info.fields >= item.field
                       && info.fields < item.field + item.duration.value_or(0)) {
                source.recipe = true;
                out.buttons |= unsigned(item.buttons.value_or(0));
                out.x = item.x.value_or(out.x); out.y = item.y.value_or(out.y);
            }
        }
        if (input_blocked) out = {};
        if (player == 1) {
            const auto navigation = take_minimap(g, info);
            if (navigation && !input_blocked && !source.controller && !source.touch && !source.recipe) {
                out = {};
                const bool accepted = native_.navigate_minimap(navigation->x, navigation->y,
                    navigation->context.value_or(-1), navigation->field);
                if (debug()) {
                    native_.log("SC64 minimap " + nlohmann::json{{"field", number(info.fields)},
                        {"source_field", number(navigation->field)}, {"x", number(navigation->x)},
                        {"y", number(navigation->y)}, {"accepted", accepted}, {"cursor_x", number(info.cursor_x)},
                        {"cursor_y", number(info.cursor_y)}, {"camera_x", number(info.camera_x)},
                        {"camera_y", number(info.camera_y)}}.dump());
                    if (accepted) minimap_log_ = std::pair{info.fields, info.minimap_jumps};
                }
            }
        }
        out.buttons &= 0xffff;
        out.x = std::max(-1.0, std::min(1.0, out.x)); out.y = std::max(-1.0, std::min(1.0, out.y));
        const auto previous = pad_log_.find(player);
        if (debug() && (previous == pad_log_.end() || !(previous->second == out))) {
            // Log the submitted values after ownership and private recipe merge.
            auto row = to_json(info);
            nlohmann::json logged{{"stage", "submitted"}, {"player", player},
                {"source", {{"controller", source.controller}, {"touch", source.touch},
                            {"gesture", source.gesture}, {"recipe", source.recipe}}},
                {"field", number(info.fields)}, {"buttons", out.buttons}, {"x", number(out.x)}, {"y", number(out.y)}};
            for (const char* key : {"cursor_x", "cursor_y", "cursor_field", "camera_x", "camera_y", "camera_valid",
                                    "ui_mode", "ui_token", "ui_field", "ui_context", "cursor_pick", "pick_required",
                                    "message_id", "ui_items", "ui_carousel", "ui_bounds"})
                if (row.contains(key)) logged[key] = row[key];
            native_.log("SC64 pad " + logged.dump());
            pad_log_[player] = out;
        }
        if (player == 1) {
            if (const auto pulse = haptics::update(haptics_, out.buttons, source.gesture || source.touch, elapsed))
                native_.vibrate(pulse->milliseconds, pulse->amplitude);
        }
        native_.set_pad(player, out.buttons, out.x, out.y);
    }
    if (elapsed >= next_log_ && !native_.tracing()) next_log_ = elapsed + 5;  // nothing writes the snapshot
    if (elapsed >= next_log_) {
        nlohmann::json logged = native_.telemetry();
        logged["script_seconds"] = elapsed;
        if (config_.number("sc64.compact_telemetry", 0) == 1) {
            // Android's log message cap truncates full menu/item snapshots.
            nlohmann::json compact;
            for (const char* key : {"fields", "tasks", "samples", "host_seconds", "script_seconds", "running", "paused",
                                    "input_active", "ui_mode", "guest_mode", "width", "height", "presented",
                                    "render_driver", "software_ms", "software_service_ms", "raster_helpers",
                                    "rumble_starts", "capture_snapshot_ms", "capture_interpret_ms",
                                    "audio_nonzero_frames", "audio_played_frames", "audio_underruns",
                                    "audio_dropped_frames", "audio_buffer_ms", "audio_buffering", "audio_source_hz",
                                    "audio_backend", "audio_dma_ms", "audio_target_ms", "audio_device_xruns",
                                    "audio_device_buffer_frames", "audio_wait_callbacks",
                                    "error"})
                if (logged.contains(key)) compact[key] = logged[key];
            logged = std::move(compact);
        }
        // Keep the snapshot alive: a range-for temporary would dangle before C++23.
        const auto timing = native_.take_audio_timing();
        for (const auto& [key, value] : timing.items()) logged[key] = value;
        native_.log("SC64 " + logged.dump());
        next_log_ = elapsed + 5;
    }
    const double seconds = config_.number("sc64.seconds", 0);
    if (seconds > 0 && elapsed >= seconds) native_.exit(0);
}

void HostScript::on_input(const std::string& id, const InputEvent& action) {
    if (action.gamepad) {
        if (id == "connected") touch_pad::device(shared_.pad, *action.gamepad, true);
        else if (id == "disconnected") touch_pad::device(shared_.pad, *action.gamepad, false);
    }
    if (debug() && (action.pressed || action.released)) {
        nlohmann::json row;
        if (action.pressed) row["pressed"] = true;
        if (action.released) row["released"] = true;
        if (action.value) row["value"] = *action.value;
        if (action.gamepad) row["gamepad"] = *action.gamepad;
        native_.log("SC64 input " + id + " " + row.dump());
    }
    const auto info = native_.info();
    ActionEvent event{action.pressed, action.released, action.value, action.gamepad};
    if (info.input_active != false) pad_state::input(pads_, id, event, info.fields, &info);
    // Before a game runs there is nothing to pause; a stored manual pause
    // would otherwise freeze the first frames of the next ROM.
    if (id == "pause" && action.pressed && info.running) set_user_pause(!paused);
    // The end of the contact that pressed a start-screen button ends its pressed look (update() keeps it for a moment).
    if (shared_.pressed && !shared_.press_released) {
        if (id == "touch_mouse" && action.released && shared_.press_owner == "mouse") shared_.press_released = true;
        if (id == "touch_multi")
            for (const auto& contact : action.touch)
                if (contact.released && contact.id == shared_.press_owner) shared_.press_released = true;
    }
    // Before a game runs the start screen offers the choices; its buttons are clicks or taps, O and M are keys.
    if (info.running) return;
    struct Press { LauncherButton::Action action; std::string owner; bool key; };
    std::optional<Press> press;
    if (id == "open_rom" && action.pressed) {
        press = Press{LauncherButton::Action::choose_rom, {}, true};
    } else if (id == "open_module" && action.pressed && native_.launcher_facts().module_selectable) {
        press = Press{LauncherButton::Action::choose_module, {}, true};
    } else if (id == "touch_mouse" && action.pressed && action.x && action.y) {
        if (const auto hit = launcher_hit(shared_.buttons, *action.x, *action.y)) press = Press{*hit, "mouse", false};
    } else if (id == "touch_multi") {
        for (const auto& contact : action.touch) {
            if (!contact.pressed) continue;
            if (const auto hit = launcher_hit(shared_.buttons, contact.x, contact.y)) { press = Press{*hit, contact.id, false}; break; }
        }
    }
    if (!press) return;
    shared_.pressed = press->action;
    shared_.press_owner = press->owner;
    shared_.press_started = elapsed;
    shared_.press_released = press->key;  // a key has no contact to hold
    const auto error = press->action == LauncherButton::Action::choose_rom ? native_.choose_rom() : native_.choose_module();
    if (error) { report(Shared::Stage::failed, *error); shared_.pressed.reset(); }
}

void HostScript::final() {
    native_.set_cursor_hidden(std::nullopt);
    touch_pad::clear(shared_.pad);
    input::clear(shared_.gestures);
}

// ---------------------------------------------------------------- GuiScript
namespace {
// Hidden fallback panel (long press / Tab) for dialogs without a pointer
// adapter; everything else has a direct gesture, HUD hint or corner button.
struct Command { const char* label; unsigned bits; };
constexpr std::array<Command, 6> commands = {{
    {"Continue (A)", button_a}, {"Menu up", button_up}, {"Back (B)", button_b},
    {"Menu left", button_left}, {"Menu down", button_down}, {"Menu right", button_right},
}};
std::pair<double, double> command_position(std::size_t index, double w, double h) {
    // Transient 3x2 panel, sized in the design domain and kept on screen.
    const double column = double((index - 1) % 3);
    const double row = std::floor(double(index - 1) / 3);
    return {w / 2 + (column - 1) * 126, h / 2 + (.5 - row) * 52};
}
// Corner buttons follow the in-mission HUD: dark navy plates, thin blue frames
// like the resource icons, lavender glyphs and a gold outline on hover/press.
constexpr std::array<double, 4> button_frame{.12, .25, .6, .85}, button_gold{.8, .66, .3, .95},
    button_fill{.03, .04, .12, .82}, button_fill_pressed{.18, .14, .05, .9}, button_glyph{.82, .82, 1, .95},
    button_glyph_dim{.82, .82, 1, .4};
// Glyph boxes as fractions of the button: dx, dy, width, height, angle, y up.
// Chevrons are two thin boxes rotated +-45 degrees.
struct GlyphBox { double dx, dy, width, height, angle; bool cutout; };
std::vector<GlyphBox> glyph_boxes(hud_buttons::Glyph glyph) {
    switch (glyph) {
    case hud_buttons::Glyph::menu: return {{0, .22, .56, .1, 0, false}, {0, 0, .56, .1, 0, false}, {0, -.22, .56, .1, 0, false}};
    case hud_buttons::Glyph::map: return {{0, 0, .64, .5, 0, false}, {0, 0, .5, .36, 0, true}, {-.14, -.007, .1, .1, 0, false}};
    case hud_buttons::Glyph::prev: return {{0, .12, .34, .09, 45, false}, {0, -.12, .34, .09, -45, false}};
    case hud_buttons::Glyph::next: return {{0, .12, .34, .09, -45, false}, {0, -.12, .34, .09, 45, false}};
    }
    return {};
}
OverlayNode text_node(std::string text) {
    OverlayNode node;
    node.kind = OverlayNode::Kind::text; node.text = std::move(text); node.scale = .55;
    return node;
}
OverlayNode box_node(std::array<double, 4> color, bool enabled = true) {
    OverlayNode node;
    node.color = color; node.enabled = enabled;
    return node;
}
}

GuiScript::GuiScript(Shared& shared, NativeApi& native, const Config& config, LayoutStore& store)
    : shared_(shared), native_(native), config_(config), store_(store) {}

std::size_t GuiScript::node(OverlayNode value) {
    nodes.push_back(std::move(value));
    return nodes.size() - 1;
}

void GuiScript::place(std::size_t index, double x, double y, double width, double height) {
    nodes[index].x = x; nodes[index].y = y; nodes[index].width = width; nodes[index].height = height;
}

bool GuiScript::save() {
    const auto error = store_.save(touch_layout_json(shared_.pad.layout));
    notice = error ? "Layout save failed: " + *error : "Touch layout saved";
    notice_time = 5;
    if (error) native_.warn("SC64 " + notice); else native_.log("SC64 " + notice);
    return !error;
}

void GuiScript::init() {
    std::optional<nlohmann::json> saved;
    bool loaded = true;
    try { saved = store_.load(); } catch (const std::exception&) { loaded = false; }
    shared_.pad.layout = touch_layout_from(loaded ? saved : std::nullopt);
    notice = loaded ? "" : "Layout could not be loaded; using defaults";
    notice_time = 5;
    shared_.pad.enabled = config_.number("sc64.touch_pad", 0) == 1 && config_.string("sc64.gpu_probe").empty();
    nodes.clear(); pad_nodes_.clear(); commands_.clear(); buttons_.clear();
    for (const auto& c : touch_pad::controls()) {
        const auto box = node(box_node({1, 1, 1, 1}));
        pad_nodes_[std::string(c.id)] = {box, node(text_node(std::string(c.label)))};
    }
    hint_ = node(text_node(""));
    // Hint highlight is an outline (four edge boxes), not a translucent fill.
    for (auto& edge : edges_) edge = node(box_node({.8, .66, .3, .3}, false));
    for (const auto& command : commands) {
        auto box = box_node({.06, .12, .17, .94});
        box.width = 120; box.height = 46;
        const auto box_index = node(box);
        commands_.push_back({box_index, node(text_node(command.label))});
    }
    for (const auto& set : hud_buttons::sets()) {
        for (std::size_t i = 0; i < set.count; ++i) {
            const auto& def = set.defs[i];
            // Creation order is draw order: plate first, glyphs above it.
            ButtonNodes button{node(box_node(button_frame, false)), node(box_node(button_fill, false)), {}};
            for (const auto& g : glyph_boxes(def.glyph)) {
                auto glyph = box_node(g.cutout ? button_fill : button_glyph, false);
                glyph.rotation = g.angle;
                button.glyph.push_back(node(glyph));
            }
            buttons_[std::string(def.id)] = std::move(button);
        }
    }
}

// `active` is the button set of the current screen (hud_buttons::set) or none.
void GuiScript::update_buttons(std::optional<std::string_view> active, const Info& info,
                               const std::optional<viewport::Rect>& rect, double w, double h, double sw, double sh) {
    for (const auto& set : hud_buttons::sets()) {
        for (std::size_t i = 0; i < set.count; ++i) {
            const auto& def = set.defs[i];
            auto& button = buttons_.at(std::string(def.id));
            const bool show = active == set.name;
            nodes[button.frame].enabled = show; nodes[button.fill].enabled = show;
            for (const auto glyph : button.glyph) nodes[glyph].enabled = show;
            if (!show) continue;
            // Buttons are laid out in 320x240 units of the displayed image.
            const double unit = rect->nw / 320;
            const double kx = rect->width / rect->nw * w / sw * unit, ky = rect->height / rect->nh * h / sh * unit;
            const double size = hud_buttons::size;
            const double left = (rect->x + def.x0 * kx * sw / w) * w / sw;
            const double top = (rect->y + rect->height - def.y0 * ky * sh / h) * h / sh;
            const double width = size * kx, height = size * ky;
            const double cx = left + width / 2, cy = top - height / 2;
            const auto state = hud_buttons::state(shared_.gestures, def.id);
            const double border = std::max(1.0, std::min(kx, ky) / unit);
            place(button.frame, cx, cy, width, height);
            nodes[button.frame].color = state ? button_gold : button_frame;
            place(button.fill, cx, cy, width - 2 * border, height - 2 * border);
            nodes[button.fill].color = state == "pressed" ? button_fill_pressed : button_fill;
            const auto boxes = glyph_boxes(def.glyph);
            for (std::size_t index = 0; index < boxes.size(); ++index) {
                const auto& g = boxes[index];
                place(button.glyph[index], cx + g.dx * width, cy + g.dy * height, g.width * width, g.height * height);
            }
            if (def.glyph == hud_buttons::Glyph::map)
                nodes[button.glyph[2]].color = info.minimap ? button_glyph : button_glyph_dim;
        }
    }
}

void GuiScript::update(double dt) {
    auto& s = shared_.pad;
    auto& g = shared_.gestures;
    const bool visible = touch_pad::visible(s);
    const double w = design_width, h = design_height;
    const auto info = native_.info();
    std::optional<Feedback> pressed;
    if (!visible && !g.radial) pressed = ui_feedback(g, info);
    const auto [sw, sh] = native_.window_size();
    const auto rect = viewport::rectangle(sw, sh, info.width.value_or(0), info.height.value_or(0));
    const bool show = pressed && rect;
    for (const auto edge : edges_) nodes[edge].enabled = show;
    if (show) {
        const auto& item = pressed->item;
        const double kx = rect->width / rect->nw * w / sw, ky = rect->height / rect->nh * h / sh;
        const double left = (rect->x + item.x0 * rect->width / rect->nw) * w / sw;
        const double right = (rect->x + item.x1 * rect->width / rect->nw) * w / sw;
        const double top = (rect->y + (rect->nh - item.y0) * rect->height / rect->nh) * h / sh;
        const double bottom = (rect->y + (rect->nh - item.y1) * rect->height / rect->nh) * h / sh;
        const double lo = std::min(top, bottom), hi = std::max(top, bottom);
        const double t = std::max(2.0, std::min(kx, ky));
        const double cx = (left + right) / 2, cy = (lo + hi) / 2;
        const double width = right - left, height = hi - lo;
        const std::array<std::array<double, 4>, 4> geometry = {{
            {cx, hi - t / 2, width, t}, {cx, lo + t / 2, width, t},
            {left + t / 2, cy, t, height}, {right - t / 2, cy, t, height}}};
        for (std::size_t i = 0; i < 4; ++i) {
            nodes[edges_[i]].color = {.8, .66, .3, pressed->alpha >= .3 ? .6 : .3};
            place(edges_[i], geometry[i][0], geometry[i][1], geometry[i][2], geometry[i][3]);
        }
    }
    std::optional<std::string_view> active;
    if (!visible && !g.radial && rect && g.enabled) active = hud_buttons::set(info);
    update_buttons(active, info, rect, w, h, sw, sh);
    for (std::size_t index = 1; index <= commands_.size(); ++index) {
        const auto [x, y] = command_position(index, w, h);
        auto& command = commands_[index - 1];
        nodes[command.box].x = x; nodes[command.box].y = y;
        nodes[command.text].x = x; nodes[command.text].y = y;
        nodes[command.box].enabled = nodes[command.text].enabled = g.radial.has_value();
    }
    for (const auto& c : touch_pad::controls()) {
        const auto& pad_nodes = pad_nodes_.at(std::string(c.id));
        const auto [x, y, size] = touch_pad::geometry(s, c.id, w, h);
        place(pad_nodes.box, x, y, size, size);
        nodes[pad_nodes.text].x = x; nodes[pad_nodes.text].y = y;
        const bool selected = s.editing && s.selected == c.id;
        bool held = false;
        for (const auto& [_, p] : s.pointers) if (p.control == c.id) held = true;
        nodes[pad_nodes.box].color = {selected ? .7 : .12, held ? .7 : .25, .4,
                                      s.layout.controls.at(std::string(c.id)).opacity};
        nodes[pad_nodes.box].enabled = visible;
        nodes[pad_nodes.text].enabled = visible;
    }
    notice_time = std::max(0.0, notice_time - dt);
    if (!g.notice.empty()) { notice = g.notice; notice_time = 4; g.notice.clear(); }
    std::string hint = s.editing ? "Drag to move | +/- size | [ ] opacity | R reset | F4 save"
                                 : "F3 touch pad | F4 edit layout | F6 player " + std::to_string(s.player);
    if (notice_time > 0 && !notice.empty()) hint = notice;
    nodes[hint_].text = hint;
    nodes[hint_].x = w / 2; nodes[hint_].y = h - 18;
    nodes[hint_].enabled = visible || (notice_time > 0 && !notice.empty());
}

bool GuiScript::contact(const std::optional<std::string>& id, const std::string& key, const InputEvent& a,
                        bool is_touch, const Info& info) {
    auto& g = shared_.gestures;
    if (!a.x || !a.y) return false;
    const double w = design_width, h = design_height;
    if (g.radial) {
        if (a.released) {
            if (g.radial->await_release) {
                g.radial->await_release = false; g.contacts.clear(); return true;
            }
            for (std::size_t index = 1; index <= commands.size(); ++index) {
                const auto [cx, cy] = command_position(index, w, h);
                if (std::abs(*a.x - cx) <= 60 && std::abs(*a.y - cy) <= 23) {
                    input::clear(g);
                    button(g, commands[index - 1].bits, info.fields);
                    return true;
                }
            }
            input::clear(g);
        }
        return true;
    }
    const auto [sw, sh] = native_.window_size();
    const auto rect = viewport::rectangle(sw, sh, info.width.value_or(0), info.height.value_or(0));
    // screen_y counts rows from the bottom edge; to_native expects the
    // bottom-left pixel index, hence minus one.
    double px, py;
    if (a.screen_x && a.screen_y) { px = *a.screen_x; py = *a.screen_y - 1; }
    else { px = *a.x * sw / w - .5; py = *a.y * sh / h - .5; }
    const Phase phase = a.released ? Phase::up : a.pressed ? Phase::down : Phase::move;
    const bool pan_event = id == "pointer_pan" || (!id && g.pan);
    // Corner buttons also accept contacts in the letterbox beside them.
    if (!pan_event && id != "pointer_order") {
        if (const auto raw = viewport::to_native_raw(rect, px, py))
            if (button_pointer(g, key, phase, raw->x, raw->y, info, is_touch)) return true;
    }
    const auto native = viewport::to_native(rect, px, py);
    if (!native) {
        g.hud_mouse.reset();
        if (a.released || g.contacts.contains(key) || g.pan || g.ui || g.minimap) input::clear(g);
        return false;
    }
    const double x = native->x, y = native->y;
    if (!pan_event || !mode_is(info, "game")) {
        if (ui_pointer(g, key, phase, x, y, info, id == "pointer_order", is_touch)) return true;
    }
    if (id == "pointer_order") {
        if (a.pressed) enqueue(g, x, y, button_b);
        return true;
    }
    if (pan_event) { pan_pointer(g, phase, x, y); return true; }
    return input::pointer(g, key, phase, x, y, info.fields, is_touch, &info);
}

bool GuiScript::pad_pointer(const std::string& key, const InputEvent& a, const Info& info) {
    auto& s = shared_.pad;
    if (!a.x || !a.y) return false;
    const Phase phase = a.released ? Phase::up : a.pressed ? Phase::down : Phase::move;
    const bool consumed = touch_pad::pointer(s, key, phase, *a.x, *a.y, info.fields, design_width, design_height);
    if (consumed && config_.number("sc64.debug_input", 0) == 1) {
        const auto out = touch_pad::read(s, info.fields, design_width, design_height);
        const char* names[] = {"down", "move", "up", "cancel"};
        native_.log("SC64 touch " + nlohmann::json{{"id", key}, {"phase", names[int(phase)]},
            {"buttons", out.buttons}, {"x", number(out.x)}, {"y", number(out.y)}, {"player", s.player}}.dump());
    }
    return consumed;
}

bool GuiScript::on_input(const std::optional<std::string>& id, const InputEvent& action) {
    auto& s = shared_.pad;
    auto& g = shared_.gestures;
    if (action.pressed && id) {
        if (*id == "touch_toggle") { s.enabled = !s.enabled; touch_pad::clear(s); input::clear(g); return true; }
        if (*id == "touch_edit" && touch_pad::visible(s)) {
            if (!s.editing || save()) touch_pad::edit(s, !s.editing);
            return true;
        }
        if (*id == "touch_player") { s.player = 3 - s.player; touch_pad::clear(s); return true; }
        if (s.editing) {
            if (*id == "touch_larger") { touch_pad::adjust(s, .01, 0); return true; }
            if (*id == "touch_smaller") { touch_pad::adjust(s, -.01, 0); return true; }
            if (*id == "touch_opacity_up") { touch_pad::adjust(s, 0, .05); return true; }
            if (*id == "touch_opacity_down") { touch_pad::adjust(s, 0, -.05); return true; }
            if (*id == "touch_reset") { s.layout = touch_pad::layout(); touch_pad::clear(s); return true; }
        }
    }
    const auto info = native_.info();
    const bool input_blocked = info.paused || !info.running || info.input_active == false;
    touch_pad::block(s, input_blocked);
    if (!touch_pad::visible(s)) {
        // A connected gamepad alone does not own the pointer; its input
        // cancels gestures in the host merge instead.
        if (input_blocked) { input::clear(g); return false; }
        if (id == "pointer_commands" && action.pressed) {
            const auto target = g.hover ? Point{g.hover->x, g.hover->y} : Point{160, 120};
            input::clear(g); g.radial = Radial{target.x, target.y, false}; return true;
        }
        if (id == "touch_multi") {
            if (!s.multi) { s.multi = true; input::clear(g); }
            bool consumed = false;
            for (const auto& t : action.touch) {
                InputEvent a;
                a.pressed = t.pressed; a.released = t.released; a.x = t.x; a.y = t.y;
                a.screen_x = t.screen_x; a.screen_y = t.screen_y;
                consumed = contact(id, t.id, a, true, info) || consumed;
            }
            return consumed;
        }
        if (!s.multi && (!id || *id == "touch_mouse" || *id == "pointer_order" || *id == "pointer_pan"))
            return contact(id, "mouse", action, false, info);
        return false;
    }
    if (id == "touch_multi") {
        // The same mobile contact also generates mouse-left. Ignore that alias
        // once this platform delivers real multi-touch, avoiding duplicate owners.
        if (!s.multi) { s.multi = true; touch_pad::clear(s); }
        bool consumed = false;
        for (const auto& t : action.touch) {
            InputEvent a;
            a.pressed = t.pressed; a.released = t.released; a.x = t.x; a.y = t.y;
            consumed = pad_pointer(t.id, a, info) || consumed;
        }
        return consumed;
    }
    if (!s.multi && (!id || *id == "touch_mouse")) return pad_pointer("mouse", action, info);
    return false;
}

void GuiScript::final() {
    touch_pad::clear(shared_.pad);
    input::clear(shared_.gestures);
}

// ---------------------------------------------------------------- StatusScript
void StatusScript::update() {
    const auto info = native_.info();
    std::string message;
    // Before the first frame the launcher screen shows the messages and errors.
    if (info.presented <= 0) message.clear();
    else if (!info.error.empty()) message = info.error;
    else if (info.paused && info.running) {
        // Focus loss/iconify pause independently of P; only a manual pause
        // can be resumed with P.
        message = shared_.user_paused ? "Paused \xE2\x80\x94 press P to resume" : "Paused";
    }
    node.text = message;
}
}
