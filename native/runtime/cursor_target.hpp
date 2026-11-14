#pragma once
#include "guest_addresses.hpp"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

// Direct mouse cursor positioning through the original NTSC-U setter (ADR-095).
// The shell submits the latest native-pixel target; the guest input-query hook
// takes it on the guest thread and calls the setter there.
namespace sc64::runtime {
namespace guest {
constexpr std::uint32_t cursor_setter = 0x80037fe0;  // sets float, rounded and sampled cursor X/Y
constexpr std::uint32_t input_query = 0x8001ae94;    // guarded hook that dispatches UI picks
}
// A request older than this many VI fields is dropped rather than applied late.
inline constexpr std::uint64_t cursor_request_max_age = 12;

struct CursorTarget { double x, y; std::uint64_t field; };
class CursorInbox {
public:
    void submit(CursorTarget target) { std::lock_guard lock(mutex_); pending_ = target; }
    void clear() { std::lock_guard lock(mutex_); pending_.reset(); applied_field_.store(0); }
    void applied(std::uint64_t field) { applied_field_.store(field + 1); }
    bool recently_applied(std::uint64_t field) const {
        const auto value = applied_field_.load();
        return value && value - 1 <= field && field - (value - 1) <= cursor_request_max_age;
    }
    std::optional<CursorTarget> take(std::span<const std::uint8_t> ram, std::uint64_t field) {
        std::optional<CursorTarget> target;
        { std::lock_guard lock(mutex_); target = std::exchange(pending_, {}); }
        if (!target || !std::isfinite(target->x) || !std::isfinite(target->y)
            || target->x < 0 || target->x >= 320 || target->y < 0 || target->y >= 240
            || target->field > field || field - target->field > cursor_request_max_age || ram.size() != 0x800000)
            return {};
        // Live guest RAM is word-swapped.
        const auto byte = [&](unsigned a) { return unsigned(ram[a ^ 3]); };
        const auto half = [&](unsigned a) { return (byte(a) << 8) | byte(a + 1); };
        const auto word = [&](unsigned a) { return (half(a) << 16) | half(a + 2); };
        // Exact instructions at the setter and its input-query hook, then the
        // same 1P mission fence as the minimap setter: player byte, cursor lock
        // (advisor/pause dialogs), build menu and camera-redraw byte (research.md).
        if (word(guest::physical(guest::cursor_setter)) != 0x00042400
            || word(guest::physical(guest::cursor_setter) + 4) != 0x00042403
            || word(guest::physical(guest::input_query)) != 0x27bdffd8
            || half(guest::physical(guest::game_mode)) != 3
            || byte(guest::physical(guest::split_screen)) || byte(0xa5b0e)
            || half(0xf0c20) || byte(0xc6a64) || byte(0xfd038)) return {};
        return target;
    }
private:
    std::mutex mutex_;
    std::atomic<std::uint64_t> applied_field_{0};
    std::optional<CursorTarget> pending_;
};
CursorInbox& cursor_input();
// Installed before guest startup; invoked exclusively by the guest input hook.
using CursorHandler = void(*)(std::uint8_t*, void*);
void set_cursor_handler(CursorHandler handler);
}
