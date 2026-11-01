// Port of tests/test_desktop_input.lua: keyboard/gamepad controller state and
// the Esc/Android Back mapping.
#include "devices.hpp"
#include "gestures.hpp"
#include "../../native/runtime/cursor_target.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
[[noreturn]] void fail(const char* file, int line, const char* expr) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", file, line, expr);
    std::abort();
}
}
#define CHECK(expr) do { if (!(expr)) fail(__FILE__, __LINE__, #expr); } while (0)

using namespace sc64::input;
namespace pads = pad_state;

namespace {
void in(pads::State& state, const char* id, const ActionEvent& event, double field) {
    pads::input(state, id, event, field, nullptr);
}
void in(pads::State& state, const char* id, const ActionEvent& event, double field, const Info& info) {
    pads::input(state, id, event, field, &info);
}
Info mode(const char* ui_mode, double ui_field) {
    Info info;
    info.ui_mode = ui_mode;
    info.ui_field = ui_field;
    return info;
}
}

int main() {
    {
        using namespace sc64::runtime;
        std::vector<std::uint8_t> ram(0x800000);
        const auto put_byte = [&](unsigned a, unsigned v) { ram[a ^ 3] = std::uint8_t(v); };
        const auto put_word = [&](unsigned a, std::uint32_t v) {
            for (int i = 0; i < 4; ++i) put_byte(a + i, v >> (24 - i * 8));
        };
        put_word(0x37fe0, 0x00042400); put_word(0x37fe4, 0x00042403);
        put_word(0x1ae94, 0x27bdffd8);
        put_byte(guest::physical(guest::game_mode) + 1, 3);
        CursorInbox inbox;
        inbox.submit({100, 100, 10}); CHECK(inbox.take(ram, 11).has_value());
        inbox.submit({100, 100, 10}); CHECK(!inbox.take(ram, 23));
        inbox.submit({320, 100, 10}); CHECK(!inbox.take(ram, 10));
        put_byte(guest::physical(guest::split_screen), 1);
        inbox.submit({100, 100, 10}); CHECK(!inbox.take(ram, 10));
        put_byte(guest::physical(guest::split_screen), 0);
        put_word(0x37fe0, 0);
        inbox.submit({100, 100, 10}); CHECK(!inbox.take(ram, 10));
        inbox.applied(10); CHECK(inbox.recently_applied(22)); CHECK(!inbox.recently_applied(23));
    }
    pads::State state;
    in(state, "a", {.pressed = true, .value = 1}, 10);
    in(state, "a", {.released = true, .value = 0}, 10);
    CHECK(pads::read(state, 1, 10).buttons == 0x8000);
    CHECK(pads::read(state, 1, 16).buttons == 0);
    in(state, "right", {.value = 0.27, .gamepad = 0}, 20);
    const double x = pads::read(state, 1, 20).x;
    CHECK(std::abs(x - 0.27) < 0.000001);
    in(state, "right", {.released = true, .value = 0, .gamepad = 0}, 20);
    in(state, "right", {.pressed = true, .value = 1}, 20);
    in(state, "right", {.released = true, .value = 0, .gamepad = 0}, 21);
    const double keyboard = pads::read(state, 1, 21).x;
    CHECK(keyboard == 1);
    in(state, "b", {.pressed = true, .value = 1, .gamepad = 1}, 22);
    CHECK(pads::read(state, 2, 22).buttons == 0x4000);
    CHECK(pads::read(state, 1, 22).buttons == 0);
    in(state, "disconnected", {.gamepad = 1}, 23);
    CHECK(pads::read(state, 2, 23).buttons == 0);
    pads::clear(state);
    const auto cleared = pads::read(state, 1, 24);
    CHECK(cleared.buttons == 0 && cleared.x == 0 && cleared.y == 0);
    // Esc and Android Back submit original B, including a same-frame press/release.
    for (const char* id : {"escape_back", "android_back"}) {
        in(state, id, {.pressed = true, .value = 1}, 30);
        in(state, id, {.released = true, .value = 0}, 30);
        CHECK(pads::read(state, 1, 30).buttons == 0x4000);
        CHECK(pads::read(state, 2, 30).buttons == 0);
        CHECK(pads::read(state, 1, 36).buttons == 0);
    }
    // Releasing an alias must not release C/B or another Back source.
    in(state, "b", {.pressed = true}, 40);
    in(state, "escape_back", {.pressed = true}, 40);
    in(state, "android_back", {.pressed = true}, 40);
    in(state, "escape_back", {.released = true}, 41);
    CHECK(pads::read(state, 1, 46).buttons == 0x4000);
    in(state, "b", {.released = true}, 46);
    CHECK(pads::read(state, 1, 47).buttons == 0x4000);
    in(state, "android_back", {.released = true}, 47);
    CHECK(pads::read(state, 1, 47).buttons == 0);
    in(state, "android_back", {.pressed = true}, 50);
    pads::clear(state);
    CHECK(pads::read(state, 1, 50).buttons == 0);
    // Plain gameplay Back opens the mission menu; keep the chosen mask while
    // that very press changes the guest to a modal. The next press is B.
    for (const char* id : {"escape_back", "android_back"}) {
        in(state, id, {.pressed = true}, 60, mode("game", 59));
        CHECK(pads::read(state, 1, 60).buttons == 0x1000);
        in(state, id, {.value = 1}, 61, mode("modal", 61));
        CHECK(pads::read(state, 1, 61).buttons == 0x1000);
        in(state, id, {.released = true}, 62, mode("modal", 62));
        CHECK(pads::read(state, 1, 66).buttons == 0);
        in(state, id, {.pressed = true}, 70, mode("modal", 70));
        in(state, id, {.released = true}, 70, mode("modal", 70));
        CHECK(pads::read(state, 1, 70).buttons == 0x4000);
        pads::clear(state);
    }
    for (const char* ui_mode : {"build", "message", "menu", "campaign_warning"}) {
        in(state, "escape_back", {.pressed = true}, 80, mode(ui_mode, 80));
        CHECK(pads::read(state, 1, 80).buttons == 0x4000); pads::clear(state);
    }
    for (const double stamp : {66, 81}) {
        in(state, "escape_back", {.pressed = true}, 80, mode("game", stamp));
        CHECK(pads::read(state, 1, 80).buttons == 0x4000); pads::clear(state);
    }
    in(state, "b", {.pressed = true}, 90, mode("game", 90));
    CHECK(pads::read(state, 1, 90).buttons == 0x4000); // physical/original B stays an order
    std::puts("Desktop input regression passed");
}
