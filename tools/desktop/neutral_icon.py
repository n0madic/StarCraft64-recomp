#!/usr/bin/env python3
"""The neutral launcher icon: the text SC64 on a dark space plate, drawn from the pinned DejaVu font.

Unlike the personal icon (icon.py, cropped from the game's title art, ADR-076) this one is authored
here and contains nothing from the game, so release launchers may carry it (ADR-088). The output is
deterministic: the same font and Pillow produce the same files.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tools/desktop'))
import bootstrap
import icon

LABEL = 'SC64'
# Colors in the spirit of the series: deep-space blue, steel frame, and the gold/orange of its lettering.
BACKGROUND = (6, 10, 24, 255)         # top of the plate
BACKGROUND_LOW = (18, 34, 72, 255)    # bottom of the plate
BORDER = (88, 128, 184, 255)
FOREGROUND = (255, 176, 40, 255)
SHADOW = (3, 5, 12, 255)
WIDTH_SHARE = 0.84  # of the plate; the text is a single line
FILES = ('icon-1024.png', 'StarCraft64.png', 'StarCraft64.ico', 'StarCraft64.rc')
MACOS_FILES = ('StarCraft64.icns',)


def master(font_path: Path = bootstrap.FONT, size: int = icon.MASTER_SIZE):
    """The square artwork: a dark plate with a thin border and the label centered on it."""
    from PIL import Image, ImageDraw, ImageFont
    art = Image.new('RGBA', (size, size), BACKGROUND)
    draw = ImageDraw.Draw(art)
    for row in range(size):  # vertical gradient, darkest at the top
        mix = row / (size - 1)
        draw.line((0, row, size, row), fill=tuple(round(a + (b - a) * mix) for a, b in zip(BACKGROUND, BACKGROUND_LOW)))
    border = max(2, size // 64)
    draw.rectangle((0, 0, size - 1, size - 1), outline=BORDER, width=border)
    # Largest font size whose text fits the width share.
    low, high = 8, size
    while low < high:
        middle = (low + high + 1) // 2
        box = ImageFont.truetype(str(font_path), middle).getbbox(LABEL)
        low, high = (middle, high) if box[2] - box[0] <= size * WIDTH_SHARE else (low, middle - 1)
    font = ImageFont.truetype(str(font_path), low)
    left, top, right, bottom = font.getbbox(LABEL)
    x, y = (size - (right - left)) / 2 - left, (size - (bottom - top)) / 2 - top
    offset = max(2, size // 128)
    draw.text((x + offset, y + offset), LABEL, font=font, fill=SHADOW)  # keeps the letters crisp at small sizes
    draw.text((x, y), LABEL, font=font, fill=FOREGROUND)
    return art


def build(output: Path, font_path: Path = bootstrap.FONT) -> dict:
    """Write every icon format into the new directory ``output`` and return a receipt."""
    art = master(font_path)
    output.mkdir(parents=True)
    art.save(output / 'icon-1024.png')
    icon.plate(art, icon.LINUX_SIZE).save(output / 'StarCraft64.png')
    icon.plate(art, 256, rounded=False).save(output / 'StarCraft64.ico', sizes=[(size, size) for size in icon.ICO_SIZES])
    (output / 'StarCraft64.rc').write_text('1 ICON "StarCraft64.ico"\n')
    made_icns = icon.write_icns(art, output / 'StarCraft64.icns')
    names = FILES + (MACOS_FILES if made_icns else ())
    return {'schema': 1, 'label': LABEL, 'game_derived': False, 'font_sha256': hashlib.sha256(font_path.read_bytes()).hexdigest(),
            'files_sha256': {name: hashlib.sha256((output / name).read_bytes()).hexdigest() for name in names}}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='new directory for the icon files')
    args = parser.parse_args()
    print(json.dumps(build(args.output), indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
