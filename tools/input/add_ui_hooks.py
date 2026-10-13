#!/usr/bin/env python3
"""Add guarded UI observers and symmetric campaign wrap to a private config."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from private_paths import require_local_or
from rom_info import NTSC_U_SHA1 as ROM_SHA1
import struct

ROOT = Path(__file__).resolve().parents[2]
# Original Right rejects ID zero before its existing circular list rotation.
# Remove only that boundary check. Pick entry also consumes an optional host
# minimap-navigation request on the guest thread; feedback itself stays read-only.
CAMPAIGN_WRAP = ('sc64_menu_80128C7C', 0x801296C8, 0xE0118, 0x1040000B)
SITES = (
    ('sc64_menu_80128C7C', 0x80129ADC, 0xE052C, 0x0C00FC2C, 0),
    ('sc64_menu_80125E7C', 0x8012793C, 0xDE38C, 0x0C00FC2C, 1),
    ('sc64_menu_80125E7C', 0x80126A40, 0xDD490, 0x0C00FC1C, 2),
    ('sc64_menu_80125E7C', 0x80127010, 0xDDA60, 0x0C00FC1C, 3),
    ('sc64_menu_80125E7C', 0x801277F4, 0xDE244, 0x0C00FC1C, 4),
    ('sc64_resident_800661DC', 0x800661DC, 0x66DDC, 0x27BDFFD8, None),
    ('sc64_menu_80127DB4', 0x80128274, 0xDECC4, 0x9462000A, 'briefing_on'),
    ('sc64_menu_80127DB4', 0x80128900, 0xDF350, 0x03E00008, 'briefing_off'),
    ('sc64_resident_80007C7C', 0x80007DC4, 0x89C4, 0x3C03800A, 'hud_begin'),
    ('sc64_resident_80007C7C', 0x80007EC8, 0x8AC8, 0xAEF059F4, 'hud_font_0'),
    ('sc64_resident_80007C7C', 0x80007FBC, 0x8BBC, 0x0C008476, 'hud_font_1'),
    ('sc64_resident_80007C7C', 0x80007FC4, 0x8BC4, 0x00002021, 'hud_font_2'),
    ('sc64_resident_80007C7C', 0x80007FF8, 0x8BF8, 0xAD2AFD60, 'hud_end'),
    ('sc64_resident_80008048', 0x80008288, 0x8E88, 0x0C019965, 'hud_present'),
    ('sc64_menu_80128C7C', 0x80129D64, 0xE07B4, 0x0C019877, 'main_0'),
    ('sc64_menu_80128C7C', 0x80129BEC, 0xE063C, 0x0C019877, 'main_1'),
    ('sc64_menu_80128C7C', 0x8012A2A8, 0xE0CF8, 0x0C00E7A6, 'main_2'),
    ('sc64_menu_80128C7C', 0x8012A2A0, 0xE0CF0, 0x0C019877, 'main_3'),
    # Encyclopedia article links (menu 8): made-by, makes and needs rows. Each
    # calls 0x8003F070 with the row rectangle in a0..a3; the loop index is in s4.
    ('sc64_menu_801232C8', 0x80123688, 0xDA0D8, 0x0C00FC1C, 5),
    ('sc64_menu_801232C8', 0x8012376C, 0xDA1BC, 0x0C00FC1C, 6),
    ('sc64_menu_801232C8', 0x80123830, 0xDA280, 0x0C00FC1C, 7),
    ('sc64_resident_8001ACF8', 0x8001AD78, 0x1B978, 0x0C01F789, 'pick_1'),
    ('sc64_resident_8001AE94', 0x8001AE94, 0x1BA94, 0x27BDFFD8, 'pick_0'),
)

def hooks(rom: bytes) -> str:
    if hashlib.sha1(rom).hexdigest() != ROM_SHA1:
        raise ValueError('UI observers require the exact normalized NTSC-U profile')
    function, pc, offset, expected = CAMPAIGN_WRAP
    if struct.unpack_from('>I', rom, offset)[0] != expected:
        raise ValueError(f'Campaign wrap instruction anchor changed: {pc:08X}')
    result = f'\n[[patches.instruction]]\nfunc = {json.dumps(function)}\nvram = 0x{pc:08X}\nvalue = 0\n'
    for function, pc, offset, expected, kind in SITES:
        if struct.unpack_from('>I', rom, offset)[0] != expected:
            raise ValueError(f'UI observer instruction anchor changed: {pc:08X}')
        callback = (f'extern void sc64_ui_pick(uint8_t*, recomp_context*, unsigned); sc64_ui_pick(rdram, ctx, {kind[-1]});'
            if isinstance(kind,str) and kind.startswith('pick_') else
            f'extern void sc64_ui_main(uint8_t*, recomp_context*, unsigned); sc64_ui_main(rdram, ctx, {kind[-1]});'
            if isinstance(kind,str) and kind.startswith('main_') else
            f'extern void sc64_ui_hud_font(uint8_t*, recomp_context*, unsigned); sc64_ui_hud_font(rdram, ctx, {kind[-1]});'
            if isinstance(kind,str) and kind.startswith('hud_font_') else
            f'extern void sc64_ui_{kind}(uint8_t*, recomp_context*); sc64_ui_{kind}(rdram, ctx);'
            if isinstance(kind,str) and kind.startswith('hud_') else
            'extern void sc64_ui_briefing(uint8_t*, recomp_context*, unsigned); '
            f'sc64_ui_briefing(rdram, ctx, {int(kind == "briefing_on")});' if isinstance(kind,str) else
            'extern void sc64_ui_blit(uint8_t*, recomp_context*); sc64_ui_blit(rdram, ctx);'
            if kind is None else
            f'extern void sc64_ui_font(uint8_t*, recomp_context*, unsigned); sc64_ui_font(rdram, ctx, {kind});')
        # A block, not bare statements: the hook can follow a label, and C before C23 (and GCC before 11)
        # does not allow a declaration there.
        result += f'\n[[patches.hook]]\nfunc = {json.dumps(function)}\nbefore_vram = 0x{pc:08X}\ntext = {json.dumps("{ " + callback + " }")}\n'
    return result

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', required=True, type=Path)
    parser.add_argument('--config', required=True, type=Path)
    args = parser.parse_args()
    config = require_local_or(sys.exit, args.config.resolve(strict=True), root=ROOT, what='Config')
    original = config.read_text()
    if any(name+'(' in original for name in ('sc64_ui_font','sc64_ui_blit','sc64_ui_briefing','sc64_ui_hud_begin','sc64_ui_pick')):
        raise SystemExit('Config already has UI observers; use it to regenerate')
    try:
        added = hooks(args.rom.read_bytes())
    except ValueError as error:
        raise SystemExit(str(error)) from error
    output = config.with_name(config.stem + '-ui.toml')
    with output.open('x') as stream:
        stream.write(original + added)
    print(output)

if __name__ == '__main__':
    main()
