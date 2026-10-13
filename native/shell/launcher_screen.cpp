#include "launcher_screen.hpp"
#include <algorithm>

namespace sc64::shell {
namespace {
using Action = LauncherButton::Action;

// Colors in the spirit of the neutral icon: deep space blue plate, gold lettering.
constexpr std::array<double, 4> backdrop{.024, .04, .1, 1}, gold{1, .69, .16, 1}, text_color{.86, .88, 1, 1},
    error_color{1, .52, .42, 1}, detail_color{.62, .66, .78, 1}, frame{.2, .34, .72, 1}, frame_primary{.9, .72, .3, 1},
    fill{.035, .055, .15, 1}, fill_primary{.1, .09, .06, 1},
    frame_hover{.45, .6, 1, 1}, frame_primary_hover{1, .82, .4, 1}, fill_hover{.07, .1, .26, 1}, fill_primary_hover{.22, .16, .05, 1},
    fill_active{.95, .69, .16, 1}, label_active{.03, .04, .1, 1};

constexpr double design_center = 480;
// Sizes in design units at the uniform scale (see layout_launcher).
constexpr double icon_size = 150, button_width = 400, button_height = 54, button_pitch = 70, detail_width = 840;
// The details are centered above the bottom edge; this many wrapped lines of them fit below the buttons.
constexpr std::size_t detail_max_lines = 10, detail_columns = 95;
constexpr double title_scale = 4, headline_scale = 1.5, label_scale = 1.3, detail_scale = .95, progress_scale = 1.1;

std::string trimmed(std::string text) {
    const auto first = text.find_first_not_of("\n ");
    if (first == std::string::npos) return {};
    text.erase(0, first);
    text.erase(text.find_last_not_of("\n ") + 1);
    return text;
}

// `text` cut to what fits `max_lines` lines of `columns` characters (long lines wrap), ending in "..." when cut.
std::string clamp_lines(const std::string& text, std::size_t max_lines, std::size_t columns) {
    std::string kept;
    std::size_t used = 0, start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto length = (end == std::string::npos ? text.size() : end) - start;
        const std::size_t wrapped = std::max<std::size_t>(1, (length + columns - 1) / columns);
        if (used + wrapped > max_lines) {
            const std::size_t room = (max_lines - used) * columns;
            if (room > 3) kept += text.substr(start, room - 3);
            else if (!kept.empty() && kept.back() == '\n') kept.pop_back();  // the dots end the last line that fits
            return kept + "...";
        }
        kept += text.substr(start, length);
        used += wrapped;
        if (end == std::string::npos) break;
        kept += '\n';
        start = end + 1;
    }
    return kept;
}

OverlayNode text_node(std::string text, double y, double scale, const std::array<double, 4>& color) {
    OverlayNode node;
    node.kind = OverlayNode::Kind::text;
    node.x = design_center; node.y = y; node.scale = scale; node.color = color; node.text = std::move(text);
    return node;
}
OverlayNode box_node(double x, double y, double width, double height, const std::array<double, 4>& color) {
    OverlayNode node;
    node.x = x; node.y = y; node.width = width; node.height = height; node.color = color;
    return node;
}
}

LauncherView describe_launcher(Shared::Stage stage, const std::string& message, const LauncherFacts& facts) {
    LauncherView view;
    if (stage == Shared::Stage::working) {
        // The first line says what is going on, the rest how far it is ("Building the game ...\n\n[3/5] Compiling").
        view.tone = LauncherView::Tone::working;
        const auto line = message.find('\n');
        view.headline = line == std::string::npos ? message : message.substr(0, line);
        view.detail = line == std::string::npos ? std::string() : trimmed(message.substr(line));
        return view;
    }
    const bool module_needed = facts.module_selectable && (!facts.module_installed || facts.module_refused);
    if (stage == Shared::Stage::failed) {
        view.tone = LauncherView::Tone::error;
        view.headline = "Cannot start the game";
    } else if (module_needed && !facts.rom_stored) {
        view.headline = "Choose your NTSC-U ROM and the game module";
    } else if (module_needed) {
        view.headline = "Choose the game module";
    } else {
        view.headline = "Choose your NTSC-U ROM";
    }
    // What a notice says (a next step, not a failure) is the headline; the details of an error stay small below.
    view.detail = trimmed(message);
    if (stage == Shared::Stage::idle && !view.detail.empty() && view.detail.find('\n') == std::string::npos) {
        view.headline = view.detail;
        view.detail.clear();
    }
    view.choices.push_back({Action::choose_rom, "Select ROM", !module_needed});
    if (facts.module_selectable) view.choices.push_back({Action::choose_module, "Select game module", module_needed});
    return view;
}

LauncherLayout layout_launcher(const LauncherView& view, bool icon, double kx, double ky, const LauncherLook& look) {
    LauncherLayout layout;
    // Without the icon the content moves up to stay centered.
    const double lift = icon ? 0 : 55;
    layout.nodes.push_back(box_node(design_center, 360, 960, 720, backdrop));
    if (icon) {
        OverlayNode node;
        node.kind = OverlayNode::Kind::image;
        node.x = design_center; node.y = 585; node.width = icon_size * kx; node.height = icon_size * ky;
        layout.nodes.push_back(node);
    }
    layout.nodes.push_back(text_node("STARCRAFT 64", 450 + lift, title_scale, gold));
    const bool error = view.tone == LauncherView::Tone::error;
    layout.nodes.push_back(text_node(view.headline, 372 + lift, headline_scale, error ? error_color : text_color));
    double y = 288 + lift;
    for (const auto& choice : view.choices) {
        const double width = button_width * kx, height = button_height * ky;
        const bool active = look.active == choice.action, hover = !active && look.hover == choice.action;
        const auto& edge = active ? frame_primary_hover : hover ? (choice.primary ? frame_primary_hover : frame_hover)
                                                                 : (choice.primary ? frame_primary : frame);
        const auto& plate = active ? fill_active : hover ? (choice.primary ? fill_primary_hover : fill_hover)
                                                         : (choice.primary ? fill_primary : fill);
        const auto& label = active ? label_active : choice.primary ? gold : text_color;
        layout.nodes.push_back(box_node(design_center, y, width + 4 * kx, height + 4 * ky, edge));
        layout.nodes.push_back(box_node(design_center, y, width, height, plate));
        layout.nodes.push_back(text_node(choice.label, y, label_scale, label));
        layout.buttons.push_back({choice.action, choice.label, design_center, y, width + 4 * kx, height + 4 * ky});
        y -= button_pitch * ky;
    }
    if (!view.detail.empty()) {
        // Progress sits under the headline; the details of a message (paths, errors) go small to the bottom.
        const bool progress = view.tone == LauncherView::Tone::working;
        auto node = text_node(progress ? view.detail : clamp_lines(view.detail, detail_max_lines, detail_columns),
                              progress ? 332 + lift : 105, progress ? progress_scale : detail_scale,
                              progress ? text_color : detail_color);
        node.width = detail_width * kx;
        node.line_break = true;
        layout.nodes.push_back(std::move(node));
    }
    return layout;
}

void LauncherScript::update() {
    if (finished_) return;  // the first frame ended the screen for good
    const auto info = native_.info();
    if (info.presented > 0) {
        finished_ = true;
        nodes.clear();
        shared_.buttons.clear();
        return;
    }
    // An error of the starting game beats whatever the host last said.
    auto stage = shared_.stage;
    auto message = shared_.message;
    if (!info.error.empty()) { stage = Shared::Stage::failed; message = info.error; }
    // Focus lost or iconified while the game starts: say so, as the status text does once frames are shown.
    else if (info.running && info.paused && stage == Shared::Stage::working) message = "Paused\n\n" + message;
    const auto [width, height] = native_.window_size();
    double kx = 1, ky = 1;
    if (width > 0 && height > 0) {
        // The overlay stretches 960x720 over the window; sizes in design units shrink along the longer axis.
        const double sx = width / design_width, sy = height / design_height, uniform = std::min(sx, sy);
        kx = uniform / sx; ky = uniform / sy;
    }
    // The mouse is tested against the buttons of the previous frame; the layout only moves with the window.
    LauncherLook look;
    if (shared_.pointer) look.hover = launcher_hit(shared_.buttons, shared_.pointer->first, shared_.pointer->second);
    look.active = shared_.pressed ? shared_.pressed : native_.choosing();
    auto layout = layout_launcher(describe_launcher(stage, message, native_.launcher_facts()), icon_, kx, ky, look);
    nodes = std::move(layout.nodes);
    shared_.buttons = std::move(layout.buttons);
}
}
