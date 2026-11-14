#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <optional>
#include <span>
#include <utility>
#include "guest_addresses.hpp"

namespace sc64::runtime {
struct MinimapProfile {
    int x0=0, y0=0, x1=0, y1=0, width=0, height=0;
    int multiplier=0, shift=0, divisor=0, viewport_w=0, viewport_h=0;
    std::uint32_t context=0;
    bool contains(double x, double y) const {
        return std::isfinite(x) && std::isfinite(y) && x>=x0 && x<x1+1 && y>=y0 && y<y1+1;
    }
    bool operator==(const MinimapProfile&) const = default;
};
// Same NTSC-U layout for owned canonical snapshots and guest-word-swapped RAM.
inline std::optional<MinimapProfile> read_minimap(std::span<const std::uint8_t> ram, unsigned lane_xor=0) {
    if (ram.size()!=0x800000) return {};
    auto byte=[&](unsigned a) { return unsigned(ram[a^lane_xor]); };
    auto half=[&](unsigned a) { return int(std::int16_t((byte(a)<<8)|byte(a+1))); };
    if (!guest::one_player_mission(ram,lane_xor) || !byte(0xafd7c)) return {};
    MinimapProfile p;
    p.x0=half(0xafd84); p.y0=half(0xafd86); p.x1=half(0xafd88); p.y1=half(0xafd8a);
    p.width=half(0xef670); p.height=half(0xef672);
    p.multiplier=half(0xef656); p.shift=half(0xef654); p.divisor=half(0xef650);
    p.viewport_w=half(0xa5b04); p.viewport_h=half(0xa5b08);
    if (p.x0<0 || p.y0<0 || p.x1>=320 || p.y1>=240 || p.x1<=p.x0 || p.y1<=p.y0
        || p.width<1 || p.height<1 || p.width>320 || p.height>240
        || p.multiplier<1 || p.multiplier>512 || p.shift<0 || p.shift>10
        || p.divisor<1 || p.divisor>512 || p.viewport_w<1 || p.viewport_w>640
        || p.viewport_h<1 || p.viewport_h>480) return {};
    // Fence a queued click against a different mission/map geometry.
    std::uint32_t hash=2166136261u;
    for (unsigned a : {0xafd84u,0xafd86u,0xafd88u,0xafd8au,0xef670u,0xef672u,
                       0xef656u,0xef654u,0xef650u,0xa5b04u,0xa5b08u,0xafd9cu,0xafd9eu})
        for (unsigned i=0;i<2;++i) hash=(hash^byte(a+i))*16777619u;
    p.context=hash;
    return p;
}
struct MinimapRequest { double x=0,y=0; std::uint32_t context=0; std::uint64_t field=0; };
struct MinimapTarget { int x=0,y=0; std::uint64_t field=0; };
class MinimapInbox {
public:
    void submit(MinimapRequest request) { std::lock_guard lock(mutex_); pending_=request; }
    void clear() { std::lock_guard lock(mutex_); pending_.reset(); }
    std::optional<MinimapTarget> take(const std::optional<MinimapProfile>& profile) {
        std::optional<MinimapRequest> request;
        { std::lock_guard lock(mutex_); request=std::exchange(pending_,{}); }
        if (!request || !profile || request->context!=profile->context
            || !profile->contains(request->x,request->y)) return {};
        const auto& p=*profile;
        // Original 800365CC centres the viewport, 80034DB8 converts minimap
        // pixels to world pixels, then 8000B988 clamps and updates map caches.
        auto axis=[&](double point,int origin,int extent,int viewport) {
            int half_view=(((viewport+31)/32)<<p.shift)/(p.divisor*2);
            int pixel=std::clamp(int(point)-half_view-origin,0,extent-1);
            return ((pixel*p.multiplier)&0xffff)&~15;
        };
        return MinimapTarget{axis(request->x,p.x0,p.width,p.viewport_w),
            axis(request->y,p.y0,p.height,p.viewport_h),request->field};
    }
private:
    std::mutex mutex_;
    std::optional<MinimapRequest> pending_;
};
MinimapInbox& minimap_input();
// Installed before guest startup; invoked exclusively by the guest input hook.
using MinimapHandler=void(*)(std::uint8_t*,void*,const MinimapTarget&);
void set_minimap_handler(MinimapHandler handler);
}
