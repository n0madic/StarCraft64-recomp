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

// The latest request of one kind, taken by the guest input hook. Requests older
// than cursor_request_max_age fields are dropped; recently_applied reports a
// guest call within the same window so the shell can stop stick steering.
template <class Target>
class RequestInbox {
public:
    void submit(Target target) { std::lock_guard lock(mutex_); pending_ = target; }
    void clear() { std::lock_guard lock(mutex_); pending_.reset(); applied_field_.store(0); }
    void applied(std::uint64_t field) { applied_field_.store(field + 1); }
    bool recently_applied(std::uint64_t field) const {
        const auto value = applied_field_.load();
        return value && value - 1 <= field && field - (value - 1) <= cursor_request_max_age;
    }
protected:
    std::optional<Target> pop(std::span<const std::uint8_t> ram, std::uint64_t field) {
        std::optional<Target> target;
        { std::lock_guard lock(mutex_); target = std::exchange(pending_, {}); }
        if (!target || target->field > field || field - target->field > cursor_request_max_age
            || ram.size() != 0x800000 || !guest::one_player_mission(ram, 3)) return {};
        return target;
    }
    // Live guest RAM is word-swapped.
    static unsigned word(std::span<const std::uint8_t> ram, unsigned a) {
        const auto byte = [&](unsigned b) { return unsigned(ram[b ^ 3]); };
        return (byte(a) << 24) | (byte(a + 1) << 16) | (byte(a + 2) << 8) | byte(a + 3);
    }
private:
    std::mutex mutex_;
    std::atomic<std::uint64_t> applied_field_{0};
    std::optional<Target> pending_;
};

struct CursorTarget { double x, y; std::uint64_t field; };
class CursorInbox : public RequestInbox<CursorTarget> {
public:
    std::optional<CursorTarget> take(std::span<const std::uint8_t> ram, std::uint64_t field) {
        auto target = pop(ram, field);
        if (!target || !std::isfinite(target->x) || !std::isfinite(target->y)
            || target->x < 0 || target->x >= 320 || target->y < 0 || target->y >= 240) return {};
        // Exact instructions at the setter and its input-query hook, within
        // the same 1P mission fence as the minimap setter.
        if (word(ram, guest::physical(guest::cursor_setter)) != 0x00042400
            || word(ram, guest::physical(guest::cursor_setter) + 4) != 0x00042403
            || word(ram, guest::physical(guest::input_query)) != 0x27bdffd8) return {};
        return target;
    }
};

// Direct camera panning (ADR-100): a camera position in world pixels (camera
// words 0x800A2EE8/EC) for the original setter 0x8000B988, which clamps it to the
// map, aligns it and updates the tile cache, as a minimap jump does.
struct CameraTarget { std::int32_t x, y; std::uint64_t field; };
class CameraInbox : public RequestInbox<CameraTarget> {
public:
    std::optional<CameraTarget> take(std::span<const std::uint8_t> ram, std::uint64_t field) {
        auto target = pop(ram, field);
        if (!target || word(ram, guest::physical(guest::input_query)) != 0x27bdffd8) return {};
        return target;
    }
};
CameraInbox& camera_input();
CursorInbox& cursor_input();
// Installed before guest startup; invoked exclusively by the guest input hook.
using CursorHandler = void(*)(std::uint8_t*, void*);
void set_cursor_handler(CursorHandler handler);
void set_camera_handler(CursorHandler handler);
}
