#include "ui_feedback.hpp"
#include "guest_addresses.hpp"
#include "minimap.hpp"
#include "cursor_target.hpp"
#include <recomp.h>
#include <algorithm>
#include <mutex>
#include <atomic>

namespace {
namespace guest = sc64::runtime::guest;
struct FontItem { sc64::runtime::UiItem item; std::uint32_t pixels; };
std::mutex mutex;
std::vector<FontItem> font_items;
std::vector<FontItem> hud_items;
// The Encyclopedia redraws its text only when the article or highlight changes,
// not every frame. Its links therefore persist, in screen coordinates, until the
// next pass over the article replaces them.
std::vector<sc64::runtime::UiItem> encyclopedia_links;
sc64::runtime::FrontendFeedback presented;
bool briefing_active = false;
thread_local bool b_hint_pending = false;
bool hud_complete = false;
std::atomic<sc64::runtime::MinimapHandler> minimap_handler{nullptr};
std::atomic<sc64::runtime::CursorHandler> cursor_handler{nullptr};
bool address(std::uint32_t a, unsigned length) {
    return a >= 0x80000000 && a <= 0x80800000 - length;
}

extern "C" void sc64_ui_briefing(std::uint8_t*, recomp_context*, unsigned active) {
    std::lock_guard lock(mutex);
    briefing_active = active != 0;
}
std::uint32_t word(const std::uint8_t* ram, std::uint32_t a) {
    unsigned p = a & 0x7fffff;
    return (std::uint32_t(ram[p ^ 3]) << 24) | (std::uint32_t(ram[(p+1) ^ 3]) << 16)
        | (std::uint32_t(ram[(p+2) ^ 3]) << 8) | ram[(p+3) ^ 3];
}
unsigned byte(const std::uint8_t* ram, std::uint32_t a) { return ram[(a & 0x7fffff) ^ 3]; }
int half(const std::uint8_t* ram, std::uint32_t a) {
    return std::int16_t((byte(ram,a)<<8)|byte(ram,a+1));
}
std::uint32_t hint_pixels(const std::uint8_t* ram, std::uint32_t pixels,
                          const sc64::runtime::UiItem& item) {
    constexpr unsigned stride=136, height=60; // guarded 1P canvas in 80007B50
    if (!address(pixels,stride*height)) return 0;
    std::uint32_t hash=2166136261u;
    for (int y=int(item.y0); y<int(item.y1); ++y)
        for (int x=int(item.x0); x<int(item.x1); ++x)
            hash=(hash^byte(ram,pixels+y*stride+x))*16777619u;
    return hash;
}
}

sc64::runtime::MinimapInbox& sc64::runtime::minimap_input() {
    static MinimapInbox inbox;
    return inbox;
}
void sc64::runtime::set_minimap_handler(MinimapHandler handler) { minimap_handler=handler; }
sc64::runtime::CursorInbox& sc64::runtime::cursor_input() {
    static CursorInbox inbox;
    return inbox;
}
void sc64::runtime::set_cursor_handler(CursorHandler handler) { cursor_handler=handler; }

extern "C" void sc64_ui_pick(std::uint8_t* ram, recomp_context* ctx, unsigned kind) {
    if (kind == 0) if (auto handler = cursor_handler.load()) {
        // The handler verifies the request age against the live guest field.
        handler(ram, ctx);
    }
    if (kind==0) if (auto handler=minimap_handler.load()) {
        if (auto target=sc64::runtime::minimap_input().take(sc64::runtime::read_minimap({ram,0x800000},3)))
            handler(ram,ctx,*target);
    }
    // 8001AE94 starts each cursor query with A (including the minimap and
    // explicit command modes which bypass unit hit testing). The navigation
    // inbox above is consumed on this guest thread before the read-only pick.
    // 8001AD78 follows the game's exact cursor hit test (8008A688). Observe
    // its return value before the next call clobbers v0; never call guest code.
    std::optional<sc64::runtime::CursorPick> observed;
    if (kind<=1 && half(ram,guest::game_mode)==3 && byte(ram,0x800a5b0e)==0
        && !byte(ram,guest::split_screen) && !half(ram,0x800f0c20) && !byte(ram,0x800c6a64)) {
        auto unit=[](std::uint32_t a) { return address(a,0x118) && !(a&3); };
        auto flags=[&](std::uint32_t a) {
            auto type=unsigned(half(ram,a+0x64));
            return type<228 ? word(ram,0x800c99dc+type*4) : 0u;
        };
        const auto owner=word(ram,0x800b00fc), selected=word(ram,0x800c6c18);
        const auto target=kind==1 ? std::uint32_t(ctx->r2) : 0u;
        if ((!target || unit(target)) && byte(ram,0x800c6c14)<=18 && owner<12) {
            sc64::runtime::CursorPick pick;
            pick.context=sc64::runtime::hud_context(ram);
            pick.x=half(ram,0x800afe18)*.5f; pick.y=half(ram,0x800afe1a)*.5f;
            pick.camera_x=std::int32_t(word(ram,0x800a2ee8));
            pick.camera_y=std::int32_t(word(ram,0x800a2eec));
            // Bit 3 identifies the three worker types in this ROM's unit table.
            // Only a player's own selection can turn a resource tap into B.
            if (byte(ram,0x800c6c14)>0 && unit(selected) && byte(ram,selected+0x4c)==owner)
                for (unsigned i=0; i<18; ++i) {
                    const auto p=word(ram,0x800c6c20+i*4);
                    if (unit(p) && byte(ram,p+0x4c)==owner && (flags(p)&8))
                        pick.worker_selected=true;
                }
            if (target) {
                pick.target_type=unsigned(half(ram,target+0x64));
                const auto target_owner=byte(ram,target+0x4c);
                // Selection list 0x800C6C20 holds count 0x800C6C14 (<= 18) pointers.
                for (unsigned i=0; target_owner==owner && i<byte(ram,0x800c6c14); ++i)
                    if (word(ram,0x800c6c20+i*4)==target) pick.target_selected=true;
                // Bit 13 is the original resource-container predicate. Keep
                // enemy refineries selectable; B remains the original resolver.
                if (pick.worker_selected && (flags(target)&0x2000)
                    && (target_owner==11 || target_owner==owner)) pick.buttons=0x4000;
            }
            if (pick.x>=0 && pick.x<320 && pick.y>=0 && pick.y<240) observed=pick;
        }
    }
    std::lock_guard lock(mutex);
    presented.pick=observed;
}

extern "C" void sc64_ui_hud_begin(std::uint8_t* ram, recomp_context* ctx) {
    if (ctx->r30 != 0 || half(ram,guest::game_mode)!=3 || byte(ram,guest::split_screen)) return;
    std::lock_guard lock(mutex);
    hud_items.clear(); // canvas is cleared by the original immediately before this hook
    hud_complete=false;
    b_hint_pending=false;
}

extern "C" void sc64_ui_hud_font(std::uint8_t* ram, recomp_context* ctx, unsigned kind) {
    // Recompiled direct native calls do not populate the guest RA register.
    // Observe exact caller PCs instead. B's return PC is also a skip target;
    // require its call-site observer before accepting a completed B label.
    if (kind==1) { b_hint_pending=true; return; }
    if (kind==2) { if (!b_hint_pending) return; b_hint_pending=false; }
    else if (kind!=0) return;
    if (ctx->r30!=0
        || half(ram,guest::game_mode)!=3 || byte(ram,guest::split_screen)) return;
    const auto canvas=word(ram,0x800afd60);
    if (!address(canvas,8) || canvas!=0x800de1e0) return;
    const auto pixels=word(ram,canvas+4);
    if (!address(pixels,136*60)) return;
    sc64::runtime::UiItem item;
    item.group=4; item.context=sc64::runtime::hud_context(ram);
    item.x0=half(ram,0x800a5a20); item.y0=half(ram,0x800a5a22);
    item.x1=half(ram,0x800a5a24)+1; item.y1=half(ram,0x800a5a26)+1;
    int icon_x=34,icon_y=49;
    if (kind==0) {
        item.id=unsigned(ctx->r17);
        if (item.id>3) return;
        constexpr unsigned masks[]={8,2,1,4}; // 80039504..80039550 original C dispatch
        item.buttons=masks[item.id];
        auto layout=word(ram,0x800a2e18);
        if (layout!=0x800a2424+item.id*18) return;
        icon_x=half(ram,layout+14); icon_y=half(ram,layout+16);
    } else { item.id=4; item.buttons=0x4000; }
    // The icon blit source rectangle is (0,0)-(11,10), inclusive.
    item.x0=std::min(item.x0,float(icon_x)); item.y0=std::min(item.y0,float(icon_y));
    item.x1=std::max(item.x1,float(icon_x+12)); item.y1=std::max(item.y1,float(icon_y+11));
    if (item.x0<0 || item.y0<0 || item.x1<=item.x0 || item.y1<=item.y0
        || half(ram,canvas)!=136 || half(ram,canvas+2)!=60 || item.x1>136 || item.y1>60) return;
    std::lock_guard lock(mutex);
    if (hud_items.size()<5) hud_items.push_back({item,pixels});
}

extern "C" void sc64_ui_hud_end(std::uint8_t* ram, recomp_context* ctx) {
    if (ctx->r30!=0 || half(ram,guest::game_mode)!=3 || byte(ram,guest::split_screen)
        || word(ram,0x800afd60)!=0x800de1e0) return;
    std::lock_guard lock(mutex);
    // Later icons/text overlap earlier label boxes. Stamp only after the entire
    // original canvas has been painted, before 80007FF8 restores the old canvas.
    for (auto& font : hud_items) font.item.label=hint_pixels(ram,font.pixels,font.item);
    hud_complete=true;
}

extern "C" void sc64_ui_hud_present(std::uint8_t* ram, recomp_context* ctx) {
    if (half(ram,guest::game_mode)!=3 || byte(ram,guest::split_screen)
        || std::uint32_t(ctx->r4)!=0x800de1e0 || ctx->r7<=0 || ctx->r7>255) return;
    const auto pixels=word(ram,0x800de1e4), context=sc64::runtime::hud_context(ram);
    if (!address(pixels,136*60) || half(ram,0x800de1e0)!=136 || half(ram,0x800de1e2)!=60) return;
    std::lock_guard lock(mutex);
    // The original fade-in also uses alpha 1..63; dimming does not disable a
    // controller command. A painted canvas owns its padding as well as labels.
    const float x=std::int32_t(ctx->r5), y=std::int32_t(ctx->r6);
    if (x<0 || y<0 || x+136>320 || y+60>240) return;
    for (unsigned i=0; i<136*60; ++i) if (byte(ram,pixels+i)) {
        presented.hud_bounds=sc64::runtime::UiBounds{x,y,x+136,y+60,context};
        break;
    }
    if (!hud_complete) return;
    for (const auto& font : hud_items) {
        if (font.pixels!=pixels || font.item.context!=context
            || font.item.label!=hint_pixels(ram,pixels,font.item)) continue;
        auto item=font.item;
        item.x0+=std::int32_t(ctx->r5); item.x1+=std::int32_t(ctx->r5);
        item.y0+=std::int32_t(ctx->r6); item.y1+=std::int32_t(ctx->r6);
        if (item.x0<0 || item.y0<0 || item.x1>320 || item.y1>240) continue;
        if (presented.items.size()<64) presented.items.push_back(item);
    }
}

extern "C" void sc64_ui_font(std::uint8_t* ram, recomp_context* ctx, unsigned kind) {
    if ((word(ram, guest::game_mode) >> 16) != 4 || kind > 7) return;
    // Encyclopedia (menu 8) article links: kinds 5, 6 and 7 are the made-by,
    // makes and needs row draws of one article. s4 counts links from zero; it is
    // already advanced at the made-by and needs sites but not at the makes site.
    // r22 is the article record, so a changed article never reuses a stale link.
    if (kind >= 5 && word(ram, 0x800d1170) != 8) return;
    const auto canvas = word(ram, 0x800afd60);
    if (!address(canvas, 8)) return;
    const auto pixels = word(ram, canvas + 4);
    if (!address(pixels, 1)) return;
    sc64::runtime::UiItem item;
    item.group = kind == 0 ? 1 : kind == 1 ? 2 : kind <= 4 ? 3 : 7;
    item.id = unsigned(kind == 0 ? ctx->r19 - 1 : kind == 2 ? ctx->r18 : kind == 6 ? ctx->r20
        : kind == 5 || kind == 7 ? ctx->r20 - 1 : ctx->r18 - 1);
    if (item.id >= 32) return;
    if (kind >= 5) item.context = std::uint32_t(ctx->r22);
    // Episode rows at the unlock boundary are displayed grey and cannot start.
    if (kind == 2) {
        auto episode = byte(ram, 0x800d1182);
        if (episode > 5 || item.id >= byte(ram, 0x800d13c4 + episode)) return;
    }
    // Rows, lists and links pick the highlight colour before drawing their text.
    item.selected = kind == 0 ? item.id == byte(ram, 0x800d13b8)
        : kind == 1 ? item.id == unsigned(ctx->r30)
        : word(ram, 0x800a5a60) == 0x800a5a30;
    item.horizontal = kind == 1; item.activate = kind != 1;
    item.x0 = std::int32_t(ctx->r4); item.y0 = std::int32_t(ctx->r5);
    if (kind == 2) item.x0 = std::min(item.x0, 109.0f); // include the separately drawn mission number
    item.x1 = std::int32_t(ctx->r6); item.y1 = std::int32_t(ctx->r7);
    // Link rows are 16 pixels apart but their draw rectangle is 24 tall; cut it
    // to the pitch so neighbouring rows never overlap.
    if (kind >= 5) item.y1 = std::min(item.y1, item.y0 + 16);
    if (item.x0 < 0 || item.y0 < 0 || item.x1 <= item.x0 || item.y1 <= item.y0
        || item.x1 > 1024 || item.y1 > 1024) return;
    std::lock_guard lock(mutex);
    // The made-by row opens every pass over an article's links.
    if (kind == 5) encyclopedia_links.clear();
    auto match = [&](const FontItem& old) { return old.item.group == item.group && old.item.id == item.id; };
    auto found = std::find_if(font_items.begin(), font_items.end(), match);
    if (found != font_items.end()) *found = {item, pixels};
    else if (font_items.size() < 64) font_items.push_back({item, pixels});
}

extern "C" void sc64_ui_blit(std::uint8_t* ram, recomp_context* ctx) {
    if ((word(ram, guest::game_mode) >> 16) != 4 || ctx->r5 != 0) return;
    auto image = std::uint32_t(ctx->r4), sp = std::uint32_t(ctx->r29);
    if (!address(image, 16) || !address(sp, 20)) return;
    auto pixels = word(ram, image + 12);
    float scale = std::int32_t(word(ram, sp + 16)) / 100.0f;
    if (scale <= 0 || scale > 4) return;
    std::lock_guard lock(mutex);
    for (const auto& font : font_items) {
        if (font.pixels != pixels) continue;
        auto item = font.item;
        item.x0 = std::int32_t(ctx->r6) + item.x0 * scale;
        item.x1 = std::int32_t(ctx->r6) + item.x1 * scale;
        item.y0 = std::int32_t(ctx->r7) + item.y0 * scale;
        item.y1 = std::int32_t(ctx->r7) + item.y1 * scale;
        auto& list = item.group == 7 ? encyclopedia_links : presented.items;
        auto found = std::find_if(list.begin(), list.end(), [&](const auto& old) {
            return old.group == item.group && old.id == item.id;
        });
        if (found != list.end()) *found = item;
        else if (list.size() < 64) list.push_back(item);
    }
}

extern "C" void sc64_ui_main(std::uint8_t* ram, recomp_context* ctx, unsigned kind) {
    if (half(ram,guest::game_mode)!=4 || word(ram,0x800d1170)!=0 || kind>3) return;
    const auto sp=std::uint32_t(ctx->r29);
    if (!address(sp,0x300)) return;
    const auto campaign=word(ram,sp+0x88);
    if (campaign>5) return;
    if (kind==2) {
        std::lock_guard lock(mutex);
        auto bounds=presented.main ? presented.main->warning_bounds : std::nullopt;
        presented.main=sc64::runtime::MainMenuFeedback{campaign,
            word(ram,sp+0x2b8)==0 && word(ram,sp+0x2ac)==0 && word(ram,sp+0x2c8)!=0,
            std::int32_t(word(ram,sp+0x27c)),bounds};
        return;
    }
    const auto image=std::uint32_t(ctx->r4);
    if (!address(image,16)) return;
    const unsigned width=unsigned(half(ram,image+8)),height=unsigned(half(ram,image+10));
    if (!width || !height || width>1024 || height>1024) return;
    const auto scale=kind==3 ? 100u : word(ram,sp+0x288);
    if (!scale || scale>100) return;
    const auto x=std::int32_t(ctx->r6);
    // These two call sites finish Y in their delay slot. Observers execute
    // before the call, so account for that exact addition without changing it.
    const auto y=std::int64_t(std::int32_t(ctx->r7))
        +(kind==0 ? std::int32_t(ctx->r2) : kind==3 ? 88 : 0);
    sc64::runtime::UiBounds bounds{float(std::max(0,x)),float(std::max<std::int64_t>(0,y)),
        float(std::min<std::int64_t>(640,std::int64_t(x)+width*scale/100)),
        float(std::min<std::int64_t>(480,y+height*scale/100)),campaign};
    if (bounds.x1<=bounds.x0 || bounds.y1<=bounds.y0) return;
    if (kind==3) {
        std::lock_guard lock(mutex);
        if (!presented.main) presented.main=sc64::runtime::MainMenuFeedback{};
        presented.main->warning_bounds=bounds;
        return;
    }
    const auto id=unsigned(ctx->r20),position=unsigned(ctx->r19)-(kind==1 ? 1u : 0u);
    if (id>5 || position>5) return;
    sc64::runtime::UiItem item;
    item.id=id; item.group=5; item.selected=position==0;
    item.horizontal=true; item.activate=item.selected; item.context=campaign;
    item.x0=bounds.x0; item.y0=bounds.y0; item.x1=bounds.x1; item.y1=bounds.y1;
    std::lock_guard lock(mutex);
    auto found=std::find_if(presented.items.begin(),presented.items.end(),[&](const auto& old) {
        return old.group==5 && old.id==id;
    });
    if (found==presented.items.end()) presented.items.push_back(item);
    else {
        found->x0=std::min(found->x0,item.x0); found->y0=std::min(found->y0,item.y0);
        found->x1=std::max(found->x1,item.x1); found->y1=std::max(found->y1,item.y1);
    }
}

sc64::runtime::FrontendFeedback sc64::runtime::take_frontend_feedback() {
    std::lock_guard lock(mutex);
    auto result = std::move(presented);
    result.items.insert(result.items.end(), encyclopedia_links.begin(), encyclopedia_links.end());
    result.briefing = briefing_active;
    presented = {}; font_items.clear();
    // HUD sprites are reused between their original periodic redraws. Their
    // metadata survives only if the same context/pixels is actually presented.
    return result;
}
