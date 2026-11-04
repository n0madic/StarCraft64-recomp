#include "ui_feedback.hpp"
#include "cursor_target.hpp"
#include "minimap.hpp"
#include <recomp.h>
#include <cassert>
#include <cstring>
#include <vector>

extern "C" void sc64_ui_font(std::uint8_t*, recomp_context*, unsigned);
extern "C" void sc64_ui_blit(std::uint8_t*, recomp_context*);
extern "C" void sc64_ui_briefing(std::uint8_t*, recomp_context*, unsigned);
extern "C" void sc64_ui_hud_begin(std::uint8_t*, recomp_context*);
extern "C" void sc64_ui_hud_font(std::uint8_t*, recomp_context*, unsigned);
extern "C" void sc64_ui_hud_present(std::uint8_t*, recomp_context*);
extern "C" void sc64_ui_hud_end(std::uint8_t*, recomp_context*);
extern "C" void sc64_ui_pick(std::uint8_t*, recomp_context*, unsigned);
extern "C" void sc64_ui_main(std::uint8_t*, recomp_context*, unsigned);
int main() {
    std::vector<std::uint8_t> ram(0x800000);
    auto word = [&](std::uint32_t a, std::uint32_t v) {
        for (unsigned i=0; i<4; ++i) ram[((a & 0x7fffff)+i)^3]=v>>(24-i*8);
    };
    word(0x800a5ab4,4u<<16); word(0x800afd60,0x80300000); word(0x80300004,0x80400000);
    word(0x8030010c,0x80400000); word(0x80500010,100);
    recomp_context font{};
    font.r4=30; font.r5=13; font.r6=179; font.r7=33; font.r19=1;
    auto before=ram; auto saved=font;
    sc64_ui_font(ram.data(),&font,0);
    assert(ram==before && !std::memcmp(&font,&saved,sizeof(font)));
    recomp_context blit{};
    blit.r4=0x80300100; blit.r6=210; blit.r7=24; blit.r29=0x80500000;
    auto saved_blit=blit;
    sc64_ui_blit(ram.data(),&blit);
    auto result=sc64::runtime::take_frontend_feedback();
    assert(ram==before && !std::memcmp(&blit,&saved_blit,sizeof(blit)));
    assert(result.items.size()==1 && result.items[0].selected);
    assert(result.items[0].x0==240 && result.items[0].y0==37 && result.items[0].x1==389);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    // Encyclopedia article links (menu 8): made-by (kind 5), makes (kind 6) and
    // needs (kind 7) rows, with the loop index in s4 and the record in s6.
    {
        word(0x800d1170,8); word(0x800a5a60,0x800a5a30);
        recomp_context link{};
        link.r4=170; link.r5=140; link.r6=333; link.r7=164; link.r20=1; link.r22=0x80341000;
        const auto ram_before_link=ram; const auto saved_link=link;
        sc64_ui_font(ram.data(),&link,5);
        assert(ram==ram_before_link && !std::memcmp(&link,&saved_link,sizeof(link)));
        word(0x800a5a60,0x800a5a38); // not highlighted
        link.r5=156; link.r7=180; link.r20=1;
        sc64_ui_font(ram.data(),&link,6);   // makes: index is s4 itself
        link.r5=172; link.r7=196; link.r20=3;
        sc64_ui_font(ram.data(),&link,7);   // needs: s4 was advanced before the draw
        blit={}; blit.r4=0x80300100; blit.r6=0; blit.r7=0; blit.r29=0x80500000;
        sc64_ui_blit(ram.data(),&blit);
        result=sc64::runtime::take_frontend_feedback();
        assert(result.items.size()==3);
        auto find=[&](unsigned id) -> const sc64::runtime::UiItem& {
            for (const auto& item : result.items) if (item.id==id) return item;
            assert(false); return result.items[0];
        };
        assert(find(0).group==7 && find(0).selected && find(0).context==0x80341000);
        assert(find(0).x0==170 && find(0).y0==140 && find(0).x1==333 && find(0).y1==156);
        assert(find(1).group==7 && !find(1).selected && find(1).y0==156);
        assert(find(2).group==7 && !find(2).selected && find(2).y0==172);
        assert(find(0).activate && !find(0).horizontal);
        // The article is drawn only when it changes: its links persist across
        // frames and a new pass over the made-by row discards them.
        assert(sc64::runtime::take_frontend_feedback().items.size()==3);
        link.r20=1; link.r5=104; link.r7=128; link.r22=0x80342000;
        sc64_ui_font(ram.data(),&link,5);
        assert(sc64::runtime::take_frontend_feedback().items.empty());
        sc64_ui_font(ram.data(),&link,5);
        sc64_ui_blit(ram.data(),&blit);
        result=sc64::runtime::take_frontend_feedback();
        assert(result.items.size()==1 && result.items[0].context==0x80342000);
        // Other menus, a zero index and an out-of-range loop counter export nothing.
        link.r20=1; sc64_ui_font(ram.data(),&link,5); // clears the persistent link
        assert(sc64::runtime::take_frontend_feedback().items.empty());
        word(0x800d1170,6); sc64_ui_font(ram.data(),&link,5);
        word(0x800d1170,8); link.r20=0; sc64_ui_font(ram.data(),&link,5);
        link.r20=40; sc64_ui_font(ram.data(),&link,6);
        sc64_ui_blit(ram.data(),&blit);
        assert(sc64::runtime::take_frontend_feedback().items.empty());
        word(0x800d1170,0);
    }
    // Main carousel geometry comes from the actual scaled hero/platform blits;
    // the warning has a separate foreground image and a stack-local ID.
    word(0x800afd60,0x80300000); word(0x800d1170,0);
    word(0x80500088,2); word(0x805002c8,1); word(0x80500288,100);
    word(0x8050027c,0xffffffff);
    word(0x80300108,(100u<<16)|160u);
    blit={}; blit.r29=0x80500000; blit.r4=0x80300100;
    blit.r6=250; blit.r7=20; blit.r2=200; blit.r19=0; blit.r20=2;
    before=ram; saved_blit=blit;
    sc64_ui_main(ram.data(),&blit,0);
    assert(ram==before && !std::memcmp(&blit,&saved_blit,sizeof(blit)));
    blit.r19=1; blit.r6=240; blit.r7=360;
    word(0x80300108,(120u<<16)|40u);
    sc64_ui_main(ram.data(),&blit,1);
    sc64_ui_main(ram.data(),&blit,2);
    result=sc64::runtime::take_frontend_feedback();
    assert(result.main && result.main->campaign==2 && result.main->ready && result.main->warning==-1);
    assert(result.items.size()==1 && result.items[0].group==5 && result.items[0].selected);
    assert(result.items[0].x0==240 && result.items[0].y0==220 && result.items[0].y1==400);
    word(0x8050027c,0x217); word(0x805002b8,1);
    word(0x80300108,(320u<<16)|166u);
    blit.r6=160; blit.r7=134;
    before=ram; saved_blit=blit;
    sc64_ui_main(ram.data(),&blit,3); sc64_ui_main(ram.data(),&blit,2);
    result=sc64::runtime::take_frontend_feedback();
    assert(result.main && !result.main->ready && result.main->warning==0x217);
    assert(result.main->warning_bounds && result.main->warning_bounds->y0==222
        && result.main->warning_bounds->x1==480 && result.main->warning_bounds->y1==388);
    assert(ram==before && !std::memcmp(&blit,&saved_blit,sizeof(blit)));
    assert(!sc64::runtime::take_frontend_feedback().main); // no cached modal in another task
    blit.r4=0x807ffffc; sc64_ui_main(ram.data(),&blit,3);
    assert(!sc64::runtime::take_frontend_feedback().main);
    blit.r4=0x80300100; word(0x800d1170,6);
    sc64_ui_main(ram.data(),&blit,2); assert(!sc64::runtime::take_frontend_feedback().main);
    word(0x800d1170,0); word(0x80500088,6);
    sc64_ui_main(ram.data(),&blit,2); assert(!sc64::runtime::take_frontend_feedback().main);
    before=ram;
    sc64_ui_briefing(ram.data(),&font,1);
    assert(sc64::runtime::take_frontend_feedback().briefing);
    assert(sc64::runtime::take_frontend_feedback().briefing); // persists across display tasks
    sc64_ui_briefing(ram.data(),&font,0);
    assert(!sc64::runtime::take_frontend_feedback().briefing);
    assert(ram==before && !std::memcmp(&font,&saved,sizeof(font)));
    // State is frame-local: a reused sprite cannot inherit a prior hitbox.
    sc64_ui_blit(ram.data(),&blit); assert(sc64::runtime::take_frontend_feedback().items.empty());
    word(0x800afd60,0x807ffffc); sc64_ui_font(ram.data(),&font,0);
    blit.r29=0x807ffff0; sc64_ui_blit(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    // Original HUD uses an 8-byte CPU canvas, subsequently presented at (176,160).
    // Metadata must survive sprite reuse, but a selection change or blank redraw
    // must immediately make it non-actionable. These are synthetic hook fixtures.
    word(0x800a5ab4,3u<<16); word(0x800afd60,0x800de1e0);
    word(0x800de1e0,(136u<<16)|60u); word(0x800de1e4,0x80400000);
    word(0x800a2e18,0x800a2436); // C-left layout (index 1)
    word(0x800a2444,(52u<<16)|19u);
    word(0x800a5a20,(20u<<16)|20u); word(0x800a5a24,(50u<<16)|30u);
    font.r30=0; font.r17=1; font.r31=0x80001234; // direct native call need not set guest RA
    before=ram; saved=font;
    sc64_ui_hud_begin(ram.data(),&font); sc64_ui_hud_font(ram.data(),&font,0);
    ram[(0x400000+20*136+20)^3]=42; // later overlapping text/icon paint
    before=ram;
    sc64_ui_hud_end(ram.data(),&font);
    assert(ram==before && !std::memcmp(&font,&saved,sizeof(font)));
    blit={}; blit.r4=std::int32_t(0x800de1e0); blit.r5=176; blit.r6=160; blit.r7=255;
    saved_blit=blit; sc64_ui_hud_present(ram.data(),&blit);
    result=sc64::runtime::take_frontend_feedback();
    assert(result.items.size()==1 && result.items[0].buttons==2 && result.items[0].group==4);
    assert(result.items[0].x0==196 && result.items[0].x1==240 && result.items[0].y0==179);
    assert(result.hud_bounds && result.hud_bounds->x0==176 && result.hud_bounds->y1==220);
    assert(ram==before && !std::memcmp(&blit,&saved_blit,sizeof(blit)));
    sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.size()==1);
    blit.r7=3; sc64_ui_hud_present(ram.data(),&blit); // original fade-in below alpha 64 stays actionable
    assert(sc64::runtime::take_frontend_feedback().items.size()==1);
    blit.r7=64; sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.size()==1);
    blit.r7=0; sc64_ui_hud_present(ram.data(),&blit);
    result=sc64::runtime::take_frontend_feedback(); assert(result.items.empty() && !result.hud_bounds);
    blit.r7=255;
    ram[(0x400000+20*136+20)^3]=43; // unobserved reuse cannot retain targets, but still owns painted padding
    sc64_ui_hud_present(ram.data(),&blit);
    result=sc64::runtime::take_frontend_feedback(); assert(result.items.empty() && result.hud_bounds);
    ram[(0x400000+20*136+20)^3]=0;
    word(0x800c6c18,0x80304000);
    sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    sc64_ui_hud_begin(ram.data(),&font);
    sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    sc64_ui_hud_font(ram.data(),&font,2); // skip branch cannot fabricate a B target
    sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    sc64_ui_hud_font(ram.data(),&font,1); sc64_ui_hud_font(ram.data(),&font,2);
    sc64_ui_hud_end(ram.data(),&font);
    sc64_ui_hud_present(ram.data(),&blit);
    result=sc64::runtime::take_frontend_feedback();
    assert(result.items.size()==1 && result.items[0].buttons==0x4000);
    font.r31=0x80001234; sc64_ui_hud_begin(ram.data(),&font);
    sc64_ui_hud_font(ram.data(),&font,2); sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    word(0x800de1e4,0x807ffffc); // descriptor points to an incomplete canvas
    sc64_ui_hud_font(ram.data(),&font,0); sc64_ui_hud_present(ram.data(),&blit);
    assert(sc64::runtime::take_frontend_feedback().items.empty());
    // Observe the original cursor query result without changing RAM/registers.
    word(0x800a5ab4,3u<<16); word(0x800f0c20,0); word(0x800c6a64,0);
    word(0x800afe18,(200u<<16)|160u); word(0x800b00fc,0);
    word(0x800c6c18,0x80300000); word(0x800c6c20,0x80300000);
    word(0x800c6c14,1u<<24); word(0x8030004c,0);
    word(0x8030104c,11u<<24); word(0x80301064,176u<<16);
    word(0x800c99dc+176*4,0x2000);
    font={}; font.r2=std::int32_t(0x80301000);
    for (unsigned worker : {7u,41u,64u}) {
        word(0x80300064,worker<<16); word(0x800c99dc+worker*4,8);
        before=ram; saved=font;
        sc64_ui_pick(ram.data(),&font,1);
        assert(ram==before && !std::memcmp(&font,&saved,sizeof(font)));
        result=sc64::runtime::take_frontend_feedback();
        assert(result.pick && result.pick->worker_selected && result.pick->buttons==0x4000);
        assert(result.pick->target_type==176 && result.pick->x==100 && result.pick->y==80);
        assert(!sc64::runtime::take_frontend_feedback().pick); // frame-local
        sc64_ui_pick(ram.data(),&font,0); // query bypassed over minimap: A
        assert(sc64::runtime::take_frontend_feedback().pick->buttons==0x8000);
    }
    assert(!result.pick->target_selected); // the resource is not selected
    font.r2=std::int32_t(0x80300000); // hit-test returns the selected worker itself
    sc64_ui_pick(ram.data(),&font,1); result=sc64::runtime::take_frontend_feedback();
    assert(result.pick && result.pick->target_selected && result.pick->buttons==0x8000);
    word(0x800c6c14,0); // same unit, empty selection
    sc64_ui_pick(ram.data(),&font,1);
    assert(!sc64::runtime::take_frontend_feedback().pick->target_selected);
    word(0x800c6c14,1u<<24); font.r2=std::int32_t(0x80301000);
    word(0x80300064,0); // infantry still selects the resource
    sc64_ui_pick(ram.data(),&font,1); result=sc64::runtime::take_frontend_feedback();
    assert(result.pick && !result.pick->worker_selected && result.pick->buttons==0x8000);
    word(0x80300064,7u<<16); word(0x80301064,7u<<16); // another worker
    sc64_ui_pick(ram.data(),&font,1);
    assert(sc64::runtime::take_frontend_feedback().pick->buttons==0x8000);
    word(0x80301064,176u<<16); word(0x8030104c,1u<<24); // enemy container
    sc64_ui_pick(ram.data(),&font,1);
    assert(sc64::runtime::take_frontend_feedback().pick->buttons==0x8000);
    word(0x8030104c,11u<<24); word(0x8030004c,1u<<24); // foreign selection
    sc64_ui_pick(ram.data(),&font,1);
    assert(sc64::runtime::take_frontend_feedback().pick->buttons==0x8000);
    word(0x8030004c,0); font.r2=0; // terrain still selects/clears, no automatic movement
    sc64_ui_pick(ram.data(),&font,1);
    assert(sc64::runtime::take_frontend_feedback().pick->buttons==0x8000);
    font.r2=std::int32_t(0x807ffffc);
    sc64_ui_pick(ram.data(),&font,1); assert(!sc64::runtime::take_frontend_feedback().pick);
    font.r2=std::int32_t(0x80301000); word(0x800c6c14,19u<<24);
    sc64_ui_pick(ram.data(),&font,1); assert(!sc64::runtime::take_frontend_feedback().pick);
    word(0x800c6c14,1u<<24); word(0x800c6a64,1u<<24);
    sc64_ui_pick(ram.data(),&font,1); assert(!sc64::runtime::take_frontend_feedback().pick);
    word(0x800c6a64,0); word(0x800f0c20,1u<<16);
    sc64_ui_pick(ram.data(),&font,1); assert(!sc64::runtime::take_frontend_feedback().pick);

    // Navigation dispatch belongs to the original guest input query, never
    // the rendering/host thread or the unit-hit-test continuation.
    word(0x800f0c20,0); word(0x800afd7c,1u<<24);
    word(0x800afd84,(17u<<16)|159u); word(0x800afd88,(79u<<16)|221u);
    word(0x800ef670,(64u<<16)|64u); word(0x800ef654,32u);
    word(0x800ef650,1u<<16); word(0x800a5b04,640u<<16); word(0x800a5b08,480u<<16);
    const auto minimap=sc64::runtime::read_minimap(ram,3); assert(minimap);
    static unsigned calls=0;
    static sc64::runtime::MinimapTarget dispatched;
    sc64::runtime::set_minimap_handler([](std::uint8_t*,void*,const sc64::runtime::MinimapTarget& target) {
        ++calls; dispatched=target;
    });
    auto& inbox=sc64::runtime::minimap_input();
    static unsigned cursor_calls=0;
    sc64::runtime::set_cursor_handler([](std::uint8_t*,void*) { ++cursor_calls; });
    before=ram; saved=font;
    inbox.submit({60,200,minimap->context,400});
    sc64_ui_pick(ram.data(),&font,1); assert(calls==0 && cursor_calls==0);
    sc64_ui_pick(ram.data(),&font,0);
    assert(cursor_calls==1);
    assert(calls==1 && dispatched.x==1056 && dispatched.y==1088 && dispatched.field==400);
    assert(ram==before && !std::memcmp(&font,&saved,sizeof(font)));
    sc64_ui_pick(ram.data(),&font,0); assert(calls==1);
    inbox.submit({60,200,minimap->context,400}); word(0x800afd7c,0);
    sc64_ui_pick(ram.data(),&font,0); assert(calls==1);
    word(0x800afd7c,1u<<24); sc64_ui_pick(ram.data(),&font,0); assert(calls==1);
    inbox.submit({60,200,minimap->context,400}); word(0x800ef670,(32u<<16)|64u);
    sc64_ui_pick(ram.data(),&font,0); assert(calls==1);
    sc64::runtime::set_minimap_handler(nullptr);
    sc64::runtime::set_cursor_handler(nullptr);
}
