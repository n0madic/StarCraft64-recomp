// SDL3 application shell: window, renderer, event translation and lifecycle.
// The main thread owns every SDL video call; the guest runtime, raster helpers
// and the audio callback run on their own threads (session.cpp, audio.cpp).
#define SDL_MAIN_USE_CALLBACKS 1
#include "log.hpp"
#include "loop_watch.hpp"
#include "native.hpp"
#include "platform_sdl.hpp"
#include "scripts.hpp"
#include "video.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>

#ifndef SC64_APP_ID
#define SC64_APP_ID "com.nomadic.sc64.personal"
#endif

namespace {
using namespace sc64::shell;

// Keyboard bindings (former game.input_binding).
struct KeyBinding { SDL_Scancode scancode; const char* action; };
constexpr std::array<KeyBinding, 32> key_bindings = {{
    {SDL_SCANCODE_X, "a"}, {SDL_SCANCODE_C, "b"}, {SDL_SCANCODE_ESCAPE, "escape_back"},
    {SDL_SCANCODE_AC_BACK, "android_back"}, {SDL_SCANCODE_RETURN, "start"}, {SDL_SCANCODE_KP_ENTER, "start"},
    {SDL_SCANCODE_Z, "z"}, {SDL_SCANCODE_Q, "l"}, {SDL_SCANCODE_E, "r"},
    {SDL_SCANCODE_I, "cup"}, {SDL_SCANCODE_K, "cdown"}, {SDL_SCANCODE_J, "cleft"}, {SDL_SCANCODE_L, "cright"},
    {SDL_SCANCODE_LEFT, "dleft"}, {SDL_SCANCODE_RIGHT, "dright"}, {SDL_SCANCODE_UP, "dup"},
    {SDL_SCANCODE_DOWN, "ddown"}, {SDL_SCANCODE_A, "left"}, {SDL_SCANCODE_D, "right"}, {SDL_SCANCODE_W, "up"},
    {SDL_SCANCODE_S, "down"}, {SDL_SCANCODE_P, "pause"}, {SDL_SCANCODE_O, "open_rom"},
    {SDL_SCANCODE_F3, "touch_toggle"}, {SDL_SCANCODE_F4, "touch_edit"}, {SDL_SCANCODE_F6, "touch_player"},
    {SDL_SCANCODE_EQUALS, "touch_larger"}, {SDL_SCANCODE_KP_PLUS, "touch_larger"},
    {SDL_SCANCODE_MINUS, "touch_smaller"}, {SDL_SCANCODE_LEFTBRACKET, "touch_opacity_down"},
    {SDL_SCANCODE_RIGHTBRACKET, "touch_opacity_up"}, {SDL_SCANCODE_TAB, "pointer_commands"},
}};
// R resets the diagnostic layout only while editing; it has no game meaning.
constexpr KeyBinding reset_binding{SDL_SCANCODE_R, "touch_reset"};
struct ButtonBinding { SDL_GamepadButton button; const char* action; };
constexpr std::array<ButtonBinding, 9> button_bindings = {{
    {SDL_GAMEPAD_BUTTON_SOUTH, "a"}, {SDL_GAMEPAD_BUTTON_EAST, "b"}, {SDL_GAMEPAD_BUTTON_START, "start"},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "l"}, {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, "r"},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, "dleft"}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, "dright"},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, "dup"}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, "ddown"},
}};
// Each stick direction is its own analog action, like the former bindings.
struct AxisBinding { SDL_GamepadAxis axis; const char* negative; const char* positive; };
constexpr std::array<AxisBinding, 5> axis_bindings = {{
    {SDL_GAMEPAD_AXIS_LEFTX, "left", "right"}, {SDL_GAMEPAD_AXIS_LEFTY, "up", "down"},
    {SDL_GAMEPAD_AXIS_RIGHTX, "cleft", "cright"}, {SDL_GAMEPAD_AXIS_RIGHTY, "cup", "cdown"},
    {SDL_GAMEPAD_AXIS_LEFT_TRIGGER, nullptr, "z"},
}};
constexpr double analog_press = 0.5;

struct App {
    Config config;
    SDL_Window* window = nullptr;
    Video video;
    std::unique_ptr<Native> native;
    Shared shared;
    std::unique_ptr<FileLayoutStore> layouts;
    std::unique_ptr<HostScript> host;
    std::unique_ptr<GuiScript> gui;
    std::unique_ptr<StatusScript> status;
    Uint64 last_ticks = 0;
    // D-056: report main-loop gaps that age the guest UI feedback past its
    // freshness window. Lifecycle transitions stop the loop on purpose: they
    // stamp `lifecycle_ns` (the Android watch runs on the Java thread) and the
    // next iteration drops the gap that contains them.
    LoopWatch loop_watch;
    std::atomic<Uint64> lifecycle_ns{0};
    Uint64 lifecycle_seen = 0;
    std::optional<int> exit_code;
    // Set by SDL_EVENT_RENDER_DEVICE_RESET. SDL delivers it while its renderer
    // is still recovering (e.g. Android foreground); recreate on the next frame.
    bool device_reset = false;
    std::map<SDL_JoystickID, std::pair<int, SDL_Gamepad*>> gamepads;
    std::map<std::pair<int, std::string>, double> analog;

    std::pair<double, double> pixels() const { return video.size(); }
    // Pointer positions in window pixels (top-left) to the former action shape.
    InputEvent pointer_event(std::optional<std::string> id, float x_points, float y_points) const {
        const float density = SDL_GetWindowPixelDensity(window);
        const auto [w, h] = pixels();
        const double px = x_points * density, py = y_points * density;
        InputEvent event;
        event.id = std::move(id);
        event.screen_x = px; event.screen_y = h - py;
        event.x = w > 0 ? px * design_width / w : 0;
        event.y = h > 0 ? (h - py) * design_height / h : 0;
        return event;
    }
    void dispatch(const InputEvent& event) {
        if (!gui->on_input(event.id, event) && event.id) host->on_input(*event.id, event);
    }
    void action(const char* id, bool pressed, std::optional<int> gamepad = std::nullopt,
                std::optional<double> value = std::nullopt) {
        InputEvent event;
        event.id = id; event.pressed = pressed; event.released = !pressed && !value;
        event.gamepad = gamepad; event.value = value;
        dispatch(event);
    }
    void analog_action(int slot, const char* id, double value) {
        double& previous = analog[{slot, id}];
        InputEvent event;
        event.id = id; event.gamepad = slot;
        event.pressed = previous <= analog_press && value > analog_press;
        if (value == 0 && previous != 0) event.released = true; else event.value = value;
        previous = value;
        dispatch(event);
    }
    void window_event(WindowEvent event) {
        host->window_event(event);
        if (event != WindowEvent::resized) native->lifecycle(event);
        if (event == WindowEvent::iconified || event == WindowEvent::restored) lifecycle_ns = SDL_GetTicksNS();
    }
};

// Android activity pause/resume arrives on the Java thread; suspend guest
// workers before SDL blocks the main loop.
bool SDLCALL background_watch(void* userdata, SDL_Event* event) {
    auto* app = static_cast<App*>(userdata);
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) app->native->suspend(true);
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) app->native->suspend(false);
    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND || event->type == SDL_EVENT_DID_ENTER_FOREGROUND)
        app->lifecycle_ns = SDL_GetTicksNS();
    return true;
}

std::string resource_path(const char* name) {
#if defined(__ANDROID__)
    return name; // APK asset, read through SDL_IOFromFile
#else
    const char* base = SDL_GetBasePath();
    return std::string(base ? base : "") + name;
#endif
}
}

SDL_AppResult SDL_AppInit(void** state, int argc, char* argv[]) {
    auto app = std::make_unique<App>();
    try {
#if defined(__ANDROID__)
        // Packaged defaults (bundled ROM, compact telemetry) precede intent arguments.
        if (SDL_IOStream* stream = SDL_IOFromFile("sc64/config.ini", "rb")) {
            std::string text(std::size_t(std::max<Sint64>(0, SDL_GetIOSize(stream))), '\0');
            SDL_ReadIO(stream, text.data(), text.size());
            SDL_CloseIO(stream);
            app->config.parse_text(text);
        }
#endif
        app->config.parse_arguments(argc, argv);
        SDL_SetAppMetadata("StarCraft 64", "1.0", SC64_APP_ID);
        // Touch and mouse stay separate sources; the adapters own aliasing.
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
            log::error("SDL initialization: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#if defined(__ANDROID__)
        // Landscape, immersive full screen like the former engine profile.
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        flags |= SDL_WINDOW_FULLSCREEN;
#endif
        app->window = SDL_CreateWindow("StarCraft 64", int(design_width), int(design_height), flags);
        if (!app->window) { log::error("Window creation: %s", SDL_GetError()); return SDL_APP_FAILURE; }
        sc64_platform_attach_window(app->window);
        app->video.create(app->window, app->config.string("sc64.render_driver"),
                          resource_path("DejaVuSansMono-Bold.ttf"));
        auto* raw = app.get();
        app->native = std::make_unique<Native>(app->config, Native::Window{
            [raw] { return raw->pixels(); },
            [raw](int code) { raw->exit_code = code; },
            [raw] { return raw->video.name(); }});
        app->native->initialize();
        app->layouts = std::make_unique<FileLayoutStore>(std::filesystem::path(app->native->storage())
                                                         / "touch-layout-v2.json");
        app->host = std::make_unique<HostScript>(app->shared, *app->native, app->config);
        app->gui = std::make_unique<GuiScript>(app->shared, *app->native, app->config, *app->layouts);
        app->status = std::make_unique<StatusScript>(app->shared, *app->native);
        app->host->init();
        app->gui->init();
        SDL_AddEventWatch(background_watch, raw);
        app->last_ticks = SDL_GetTicksNS();
    } catch (const std::exception& error) {
        log::error("SC64 initialization: %s", error.what());
        *state = app.release();
        return SDL_APP_FAILURE;
    }
    *state = app.release();
    return SDL_APP_CONTINUE;
}

// Window conditions that explain a blocked present, for the loop-gap report.
std::string window_state(SDL_WindowFlags flags) {
    std::string state;
    if (flags & SDL_WINDOW_OCCLUDED) state += "occluded";
    if (!(flags & SDL_WINDOW_INPUT_FOCUS)) state += state.empty() ? "unfocused" : ",unfocused";
    return state;
}

SDL_AppResult SDL_AppIterate(void* state) {
    auto& app = *static_cast<App*>(state);
    try {
        const Uint64 now = SDL_GetTicksNS();
        const double dt = double(now - app.last_ticks) / 1e9;
        app.last_ticks = now;
        const auto window_flags = SDL_GetWindowFlags(app.window);
        if (const Uint64 lifecycle = app.lifecycle_ns; lifecycle != app.lifecycle_seen) {
            app.lifecycle_seen = lifecycle;
            app.loop_watch.reset();
        }
        // A hidden or minimized window stops presenting by design; an occluded or
        // unfocused one is exactly what the report should name.
        if (window_flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) app.loop_watch.reset();
        if (const auto gap = app.loop_watch.begin(now, window_state(window_flags)))
            log::warning("%s", gap->c_str());
        if (std::exchange(app.device_reset, false)) app.video.reset();
        if (const auto upload = app.native->tick()) app.video.upload(*upload);
        app.loop_watch.lap(LoopWatch::tick, SDL_GetTicksNS());
        app.host->update(dt);
        app.gui->update(dt);
        app.status->update();
        app.loop_watch.lap(LoopWatch::scripts, SDL_GetTicksNS());
        const bool probe = app.native->probing() || !app.config.string("sc64.gpu_probe").empty();
        app.video.draw(app.native->visible(), app.native->width(), app.native->height(),
                       probe ? nullptr : &app.gui->nodes, probe ? nullptr : &app.status->node);
        app.native->after_render([&] { return app.video.readback(); });
        app.loop_watch.lap(LoopWatch::draw, SDL_GetTicksNS());
        app.video.present();
        app.loop_watch.lap(LoopWatch::present, SDL_GetTicksNS());
    } catch (const std::exception& error) {
        log::error("SC64 frame: %s", error.what());
        return SDL_APP_FAILURE;
    }
    if (app.exit_code) return *app.exit_code == 0 ? SDL_APP_SUCCESS : SDL_APP_FAILURE;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* state, SDL_Event* event) {
    auto& app = *static_cast<App*>(state);
    try {
        switch (event->type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_TERMINATING:
            return SDL_APP_SUCCESS;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP: {
            if (event->key.repeat) break;
            const bool pressed = event->type == SDL_EVENT_KEY_DOWN;
            for (const auto& binding : key_bindings)
                if (binding.scancode == event->key.scancode) app.action(binding.action, pressed);
            if (reset_binding.scancode == event->key.scancode) app.action(reset_binding.action, pressed);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            const char* id = event->button.button == SDL_BUTTON_LEFT ? "touch_mouse"
                : event->button.button == SDL_BUTTON_RIGHT ? "pointer_order"
                : event->button.button == SDL_BUTTON_MIDDLE ? "pointer_pan" : nullptr;
            if (!id) break;
            auto input = app.pointer_event(id, event->button.x, event->button.y);
            input.pressed = event->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            input.released = !input.pressed;
            app.dispatch(input);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
            app.dispatch(app.pointer_event(std::nullopt, event->motion.x, event->motion.y));
            break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED: {
            int w = 0, h = 0;
            SDL_GetWindowSize(app.window, &w, &h);
            const auto point = app.pointer_event(std::nullopt, event->tfinger.x * float(w), event->tfinger.y * float(h));
            Touch touch;
            touch.id = std::to_string(event->tfinger.fingerID);
            touch.pressed = event->type == SDL_EVENT_FINGER_DOWN;
            touch.released = event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED;
            touch.x = *point.x; touch.y = *point.y; touch.screen_x = point.screen_x; touch.screen_y = point.screen_y;
            InputEvent input;
            input.id = "touch_multi";
            input.touch.push_back(touch);
            app.dispatch(input);
            break;
        }
        case SDL_EVENT_GAMEPAD_ADDED: {
            const auto id = event->gdevice.which;
            if (app.gamepads.contains(id)) break;
            int slot = 0;
            for (; slot < 2; ++slot) {
                bool used = false;
                for (const auto& [_, entry] : app.gamepads) used = used || entry.first == slot;
                if (!used) break;
            }
            if (slot >= 2) break; // two controllers are supported
            SDL_Gamepad* pad = SDL_OpenGamepad(id);
            if (!pad) break;
            app.gamepads[id] = {slot, pad};
            app.action("connected", true, slot);
            break;
        }
        case SDL_EVENT_GAMEPAD_REMOVED: {
            const auto found = app.gamepads.find(event->gdevice.which);
            if (found == app.gamepads.end()) break;
            const int slot = found->second.first;
            SDL_CloseGamepad(found->second.second);
            app.gamepads.erase(found);
            std::erase_if(app.analog, [slot](const auto& entry) { return entry.first.first == slot; });
            app.action("disconnected", true, slot);
            break;
        }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP: {
            const auto found = app.gamepads.find(event->gbutton.which);
            if (found == app.gamepads.end()) break;
            for (const auto& binding : button_bindings)
                if (binding.button == event->gbutton.button)
                    app.action(binding.action, event->gbutton.down, found->second.first);
            break;
        }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
            const auto found = app.gamepads.find(event->gaxis.which);
            if (found == app.gamepads.end()) break;
            const double value = std::max(-1.0, double(event->gaxis.value) / 32767.0);
            for (const auto& binding : axis_bindings) {
                if (binding.axis != event->gaxis.axis) continue;
                if (binding.negative) app.analog_action(found->second.first, binding.negative, value < 0 ? -value : 0);
                app.analog_action(found->second.first, binding.positive, value > 0 ? value : 0);
            }
            break;
        }
        case SDL_EVENT_WINDOW_FOCUS_LOST: app.window_event(WindowEvent::focus_lost); break;
        case SDL_EVENT_WINDOW_FOCUS_GAINED: app.window_event(WindowEvent::focus_gained); break;
        case SDL_EVENT_WINDOW_MINIMIZED: app.window_event(WindowEvent::iconified); break;
        case SDL_EVENT_WINDOW_RESTORED: app.window_event(WindowEvent::restored); break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: app.window_event(WindowEvent::resized); break;
        case SDL_EVENT_RENDER_DEVICE_RESET: app.device_reset = true; break;
        default: break;
        }
    } catch (const std::exception& error) {
        log::error("SC64 event: %s", error.what());
        return SDL_APP_FAILURE;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* state, SDL_AppResult) {
    std::unique_ptr<App> app(static_cast<App*>(state));
    if (!app) return;
    SDL_RemoveEventWatch(background_watch, app.get());
    if (app->gui) app->gui->final();
    if (app->host) app->host->final();
    if (app->native) app->native->finalize();
    for (const auto& [_, entry] : app->gamepads) SDL_CloseGamepad(entry.second);
    sc64_platform_attach_window(nullptr);
    app->video.destroy();
    if (app->window) SDL_DestroyWindow(app->window);
}
