// The start screen (launcher_screen.hpp): what it says in every state, where its buttons are, and when it
// gives way to the game. Layout checks use the design space (960x720, bottom-left origin).
#include "launcher_screen.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace sc64::shell;

namespace {
[[noreturn]] void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    std::abort();
}
#define CHECK(expr) do { if (!(expr)) fail(__FILE__, __LINE__, #expr); } while (0)

struct MockNative final : NativeApi {
    sc64::input::Info guest;
    LauncherFacts facts;
    std::optional<LauncherButton::Action> picking;
    std::pair<double, double> size{960, 720};
    sc64::input::Info info() override { return guest; }
    nlohmann::json telemetry() override { return nlohmann::json::object(); }
    nlohmann::json take_audio_timing() override { return nlohmann::json::object(); }
    std::optional<std::string> load_rom(const std::string&) override { return std::nullopt; }
    std::optional<std::string> load_bundled_rom() override { return std::nullopt; }
    std::optional<std::string> choose_rom() override { return std::nullopt; }
    std::optional<ImportEvent> poll_import() override { return std::nullopt; }
    std::optional<std::string> stored_rom() override { return std::nullopt; }
    LauncherFacts launcher_facts() override { return facts; }
    std::optional<LauncherButton::Action> choosing() override { return picking; }
    void pause(bool) override {}
    void set_pad(int, unsigned, double, double) override {}
    bool navigate_minimap(double, double, double, double) override { return false; }
    void set_cursor_hidden(const std::optional<std::array<double, 4>>&) override {}
    void vibrate(int, int) override {}
    void set_haptics(bool) override {}
    void exit(int) override {}
    std::pair<double, double> window_size() override { return size; }
    void log(const std::string&) override {}
};

const OverlayNode* find_text(const LauncherLayout& layout, const std::string& text) {
    for (const auto& node : layout.nodes)
        if (node.kind == OverlayNode::Kind::text && node.text == text) return &node;
    return nullptr;
}
bool has_choice(const LauncherView& view, LauncherButton::Action action, bool primary) {
    return std::any_of(view.choices.begin(), view.choices.end(),
                       [&](const LauncherChoice& choice) { return choice.action == action && choice.primary == primary; });
}
using Stage = Shared::Stage;
using Action = LauncherButton::Action;

void describes_every_state() {
    LauncherFacts built_in;  // nothing to choose but the ROM (the module is built in or built by the application)
    auto view = describe_launcher(Stage::idle, "", built_in);
    CHECK(view.headline == "Choose your NTSC-U ROM" && view.tone == LauncherView::Tone::normal);
    CHECK(view.choices.size() == 1 && has_choice(view, Action::choose_rom, true));

    LauncherFacts fresh{.module_selectable = true};  // an Android launcher on first start
    view = describe_launcher(Stage::idle, "", fresh);
    CHECK(view.headline == "Choose your NTSC-U ROM and the game module");
    CHECK(view.choices.size() == 2 && has_choice(view, Action::choose_module, true) && has_choice(view, Action::choose_rom, false));

    LauncherFacts only_rom{.module_selectable = true, .module_installed = true};
    view = describe_launcher(Stage::idle, "", only_rom);
    CHECK(view.headline == "Choose your NTSC-U ROM" && has_choice(view, Action::choose_rom, true) && has_choice(view, Action::choose_module, false));

    LauncherFacts only_module{.module_selectable = true, .rom_stored = true};
    view = describe_launcher(Stage::idle, "", only_module);
    CHECK(view.headline == "Choose the game module" && has_choice(view, Action::choose_module, true));

    // A notice (for example "module installed") does not turn into an error.
    view = describe_launcher(Stage::idle, "The game module is installed. Now choose your ROM.", only_rom);
    CHECK(view.tone == LauncherView::Tone::normal && view.choices.size() == 2);

    // An error keeps the buttons, with the details under them, and points at the missing piece.
    view = describe_launcher(Stage::failed, "The game module sc64-game.so was not found.\n  /a/b", fresh);
    CHECK(view.tone == LauncherView::Tone::error && view.headline == "Cannot start the game");
    CHECK(view.detail.starts_with("The game module") && view.detail.find("/a/b") != std::string::npos);
    CHECK(view.choices.size() == 2 && has_choice(view, Action::choose_module, true));
    view = describe_launcher(Stage::failed, "Expected a 32 MiB ROM dump", only_rom);
    CHECK(has_choice(view, Action::choose_rom, true) && !has_choice(view, Action::choose_module, true));

    // Work in progress: the first line is the headline, how far it is goes under it, nothing to click.
    view = describe_launcher(Stage::working, "Building the game from your ROM (one time)\n\n[3/5] Generating the game code", built_in);
    CHECK(view.tone == LauncherView::Tone::working && view.choices.empty());
    CHECK(view.headline == "Building the game from your ROM (one time)" && view.detail == "[3/5] Generating the game code");
    view = describe_launcher(Stage::working, "Starting game...", built_in);
    CHECK(view.headline == "Starting game..." && view.detail.empty());
    std::puts("start screen states passed");
}

void points_at_the_module_when_it_is_refused() {
    // Installed but refused (ABI mismatch after an update): the module is what needs choosing, not the ROM.
    LauncherFacts refused{.module_selectable = true, .module_installed = true, .module_refused = true, .rom_stored = true};
    auto view = describe_launcher(Stage::failed, "ABI mismatch", refused);
    CHECK(has_choice(view, Action::choose_module, true) && has_choice(view, Action::choose_rom, false));
    // ... and a module that was fine stays secondary.
    refused.module_refused = false;
    view = describe_launcher(Stage::failed, "Expected a 32 MiB ROM dump", refused);
    CHECK(has_choice(view, Action::choose_rom, true) && has_choice(view, Action::choose_module, false));

    // A notice (a next step) is the headline, not a footnote; details with several lines stay small.
    LauncherFacts installed{.module_selectable = true, .module_installed = true};
    view = describe_launcher(Stage::idle, "The game module is installed. Now choose your ROM.", installed);
    CHECK(view.headline == "The game module is installed. Now choose your ROM." && view.detail.empty());
    view = describe_launcher(Stage::idle, "line one\nline two", installed);
    CHECK(view.headline == "Choose your NTSC-U ROM" && view.detail == "line one\nline two");
    std::puts("start screen module and notice passed");
}

void clamps_long_details() {
    std::string many;
    for (int i = 0; i < 40; ++i) many += "error line " + std::to_string(i) + "\n";
    LauncherView view;
    view.tone = LauncherView::Tone::error;
    view.headline = "Cannot start the game";
    view.detail = many;
    auto layout = layout_launcher(view, true);
    const OverlayNode* detail = nullptr;
    for (const auto& node : layout.nodes) if (node.kind == OverlayNode::Kind::text && node.text.starts_with("error line 0")) detail = &node;
    CHECK(detail);
    CHECK(std::count(detail->text.begin(), detail->text.end(), '\n') < 10 && detail->text.ends_with("..."));
    // One very long line wraps to several; it is cut to the same budget.
    view.detail = std::string(5000, 'x');
    layout = layout_launcher(view, true);
    for (const auto& node : layout.nodes) if (node.kind == OverlayNode::Kind::text && node.text.starts_with("xxx")) detail = &node;
    CHECK(detail->text.size() <= 10 * 95 && detail->text.ends_with("..."));
    // Short details are untouched.
    view.detail = "short\nreason";
    layout = layout_launcher(view, true);
    CHECK(find_text(layout, "short\nreason"));
    std::puts("start screen detail clamp passed");
}

void lays_out_inside_the_design_space() {
    LauncherFacts fresh{.module_selectable = true};
    for (const bool icon : {false, true}) {
        const auto layout = layout_launcher(describe_launcher(Stage::failed, "details", fresh), icon);
        CHECK(find_text(layout, "STARCRAFT 64") && find_text(layout, "Cannot start the game") && find_text(layout, "details"));
        CHECK(find_text(layout, "Select ROM") && find_text(layout, "Select game module"));
        const bool has_image = std::any_of(layout.nodes.begin(), layout.nodes.end(),
                                           [](const OverlayNode& node) { return node.kind == OverlayNode::Kind::image; });
        CHECK(has_image == icon);
        CHECK(layout.buttons.size() == 2);
        for (const auto& button : layout.buttons) {
            CHECK(button.x - button.width / 2 >= 0 && button.x + button.width / 2 <= design_width);
            CHECK(button.y - button.height / 2 >= 0 && button.y + button.height / 2 <= design_height);
        }
        // Buttons do not overlap each other nor the details at the bottom.
        const auto& first = layout.buttons[0];
        const auto& second = layout.buttons[1];
        CHECK(first.y - first.height / 2 > second.y + second.height / 2);
        CHECK(second.y - second.height / 2 > find_text(layout, "details")->y + 80);
        // The title is above the headline above the buttons.
        CHECK(find_text(layout, "STARCRAFT 64")->y > find_text(layout, "Cannot start the game")->y);
        CHECK(find_text(layout, "Cannot start the game")->y > first.y + first.height / 2);
        // A click in the middle of a button hits it; beside it does not.
        CHECK(launcher_hit(layout.buttons, first.x, first.y) == Action::choose_rom);
        CHECK(launcher_hit(layout.buttons, second.x, second.y) == Action::choose_module);
        CHECK(!launcher_hit(layout.buttons, first.x, first.y + first.height));
        CHECK(!launcher_hit(layout.buttons, 5, 5));
    }
    // Progress without choices has no buttons.
    CHECK(layout_launcher(describe_launcher(Stage::working, "Loading ROM...", {}), true).buttons.empty());
    std::puts("start screen layout passed");
}

void keeps_buttons_undistorted_on_any_window() {
    // The overlay stretches 960x720 over the window: the design sizes shrink along the longer axis, so that a
    // button is the same shape in pixels on a phone (wide), on the design aspect and in a tall window.
    for (const auto [width, height] : {std::pair{2400.0, 1080.0}, {960.0, 720.0}, {540.0, 1200.0}}) {
        MockNative native;
        Shared shared;
        LauncherScript screen(shared, native, true);
        native.size = {width, height};
        screen.update();
        CHECK(shared.buttons.size() == 1);
        const double uniform = std::min(width / design_width, height / design_height);
        const auto& button = shared.buttons[0];
        // The size in pixels is the size of the layout at the design aspect, scaled uniformly.
        const auto reference = layout_launcher(describe_launcher(Stage::idle, "", {}), true).buttons[0];
        CHECK(std::abs(button.width * width / design_width - reference.width * uniform) < 1e-6);
        CHECK(std::abs(button.height * height / design_height - reference.height * uniform) < 1e-6);
        const auto icon = std::find_if(screen.nodes.begin(), screen.nodes.end(),
                                       [](const OverlayNode& node) { return node.kind == OverlayNode::Kind::image; });
        CHECK(icon != screen.nodes.end());
        CHECK(std::abs(icon->width * width / design_width - icon->height * height / design_height) < 1e-6);  // square
    }
    std::puts("start screen aspect passed");
}

// The colors of everything drawn at a button (frame, plate, label), to compare how it looks.
std::vector<std::array<double, 4>> look_of(const std::vector<OverlayNode>& nodes, const LauncherButton& button) {
    std::vector<std::array<double, 4>> colors;
    for (const auto& node : nodes)
        if (node.y == button.y && node.kind != OverlayNode::Kind::image) colors.push_back(node.color);
    return colors;
}

void reacts_to_pointer_and_press() {
    MockNative native;
    native.facts.module_selectable = true;
    Shared shared;
    LauncherScript screen(shared, native, false);
    screen.update();
    CHECK(shared.buttons.size() == 2);
    const auto rom = shared.buttons[0], module = shared.buttons[1];
    const auto idle_rom = look_of(screen.nodes, rom), idle_module = look_of(screen.nodes, module);
    CHECK(idle_rom.size() == 3 && idle_rom != idle_module);

    // The mouse over a button highlights that one only.
    shared.pointer = std::pair{rom.x, rom.y};
    screen.update();
    const auto hover_rom = look_of(screen.nodes, rom);
    CHECK(hover_rom != idle_rom && look_of(screen.nodes, module) == idle_module);
    shared.pointer = std::pair{5.0, 5.0};
    screen.update();
    CHECK(look_of(screen.nodes, rom) == idle_rom);

    // A press, or an open picker, looks different from hover; the button keeps it under the mouse too.
    shared.pointer = std::pair{rom.x, rom.y};
    shared.pressed = Action::choose_rom;
    screen.update();
    const auto pressed_rom = look_of(screen.nodes, rom);
    CHECK(pressed_rom != hover_rom && pressed_rom != idle_rom && look_of(screen.nodes, module) == idle_module);
    shared.pressed.reset();
    native.picking = Action::choose_module;
    screen.update();
    CHECK(look_of(screen.nodes, module) == pressed_rom);  // an open picker looks like a press
    CHECK(look_of(screen.nodes, rom) == hover_rom);
    native.picking.reset();
    shared.pointer.reset();
    screen.update();
    CHECK(look_of(screen.nodes, rom) == idle_rom && look_of(screen.nodes, module) == idle_module);
    std::puts("start screen pointer and press looks passed");
}

void follows_the_game() {
    MockNative native;
    Shared shared;
    LauncherScript screen(shared, native, false);
    screen.update();
    CHECK(!screen.nodes.empty() && shared.buttons.size() == 1);
    // An error of the starting game replaces whatever the host last said.
    native.guest.error = "Guest crashed";
    screen.update();
    CHECK(find_text({screen.nodes, {}}, "Guest crashed"));
    native.guest.error.clear();
    // Paused while the game starts (focus lost): the screen says so.
    native.guest.running = true; native.guest.paused = true;
    shared.stage = Shared::Stage::working; shared.message = "Starting game...";
    screen.update();
    CHECK(find_text({screen.nodes, {}}, "Paused") && find_text({screen.nodes, {}}, "Starting game..."));
    native.guest.running = false; native.guest.paused = false;
    // The first presented frame ends the start screen and its buttons.
    native.guest.presented = 1;
    screen.update();
    CHECK(screen.nodes.empty() && shared.buttons.empty());
    // ... for good: nothing is rebuilt afterwards, not even when the host publishes a message.
    shared.stage = Shared::Stage::failed; shared.message = "late";
    screen.update();
    CHECK(screen.nodes.empty() && shared.buttons.empty());
    std::puts("start screen follows the game passed");
}
}

int main() {
    describes_every_state();
    points_at_the_module_when_it_is_refused();
    clamps_long_details();
    lays_out_inside_the_design_space();
    keeps_buttons_undistorted_on_any_window();
    reacts_to_pointer_and_press();
    follows_the_game();
    return 0;
}
