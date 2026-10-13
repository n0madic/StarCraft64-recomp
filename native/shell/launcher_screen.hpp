#pragma once
#include "scripts.hpp"
#include <string>
#include <vector>

// The start screen shown until the game presents its first frame: icon, title, what is needed next, one
// button per thing the user can choose, and the details of a failure. SDL-free like the other scripts:
// video.cpp draws the nodes, scripts.cpp turns a click on a published button into the choice.
namespace sc64::shell {
// One thing the user can choose; `primary` marks the one that is needed next.
struct LauncherChoice {
    LauncherButton::Action action = LauncherButton::Action::choose_rom;
    std::string label;
    bool primary = false;
};
// What the screen says, before it is laid out.
struct LauncherView {
    enum class Tone { normal, working, error } tone = Tone::normal;
    std::string headline, detail;
    std::vector<LauncherChoice> choices;
};
// Which buttons look hovered (mouse over) or active (pressed, or its picker is open).
struct LauncherLook {
    std::optional<LauncherButton::Action> hover, active;
};
struct LauncherLayout {
    std::vector<OverlayNode> nodes;
    std::vector<LauncherButton> buttons;
};

// Decides the headline, the details and the choices from the state of the launch and what can be chosen.
LauncherView describe_launcher(Shared::Stage stage, const std::string& message, const LauncherFacts& facts);
// Places `view` in the design space. `icon` reserves the space above the title for the icon (an image node).
// The overlay is stretched over the window, so sizes are corrected by (kx, ky) to keep buttons and icon undistorted.
LauncherLayout layout_launcher(const LauncherView& view, bool icon, double kx = 1, double ky = 1, const LauncherLook& look = {});

class LauncherScript {
public:
    LauncherScript(Shared& shared, NativeApi& native, bool icon) : shared_(shared), native_(native), icon_(icon) {}
    // Rebuilds `nodes` and publishes the buttons (none once the game presents its frames).
    void update();
    std::vector<OverlayNode> nodes;
private:
    Shared& shared_;
    NativeApi& native_;
    bool icon_;
    bool finished_ = false;
};
}
