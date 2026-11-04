#pragma once
#include "guest_addresses.hpp"
#include <cmath>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>

namespace sc64::runtime {
struct CursorTarget { double x, y; std::uint64_t field; };
class CursorInbox {
public:
    void submit(CursorTarget target) { std::lock_guard lock(mutex_); pending_ = target; }
    void clear() { std::lock_guard lock(mutex_); pending_.reset(); applied_field_.store(0); }
    void applied(std::uint64_t field) { applied_field_.store(field + 1); }
    bool recently_applied(std::uint64_t field) const {
        const auto value = applied_field_.load();
        return value && value - 1 <= field && field - (value - 1) <= 12;
    }
    std::optional<CursorTarget> take(std::span<const std::uint8_t> ram, std::uint64_t field) {
        std::optional<CursorTarget> target;
        { std::lock_guard lock(mutex_); target = std::exchange(pending_, {}); }
        if (!target || !std::isfinite(target->x) || !std::isfinite(target->y)
            || target->x < 0 || target->x >= 320 || target->y < 0 || target->y >= 240
            || target->field > field || field - target->field > 12 || ram.size() != 0x800000) return {};
        const auto byte = [&](unsigned a) { return unsigned(ram[a ^ 3]); };
        const auto half = [&](unsigned a) { return (byte(a) << 8) | byte(a + 1); };
        const auto word = [&](unsigned a) { return (half(a) << 16) | half(a + 2); };
        // Exact NTSC-U instructions at the cursor setter and its input-query hook.
        if (word(0x37fe0) != 0x00042400 || word(0x37fe4) != 0x00042403
            || word(0x1ae94) != 0x27bdffd8
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
using CursorHandler = void(*)(std::uint8_t*, void*);
void set_cursor_handler(CursorHandler handler);
}
