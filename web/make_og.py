#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The page's link preview, web/og.png (1200 x 630: the card a chat or a post shows for the link): RoboSeq's logo
over three of the firmware's own screens (make screens), on the splash's ground and grid.

  python3 web/make_og.py"""
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
W, H = 1200, 630
BG, GRID, LINE = (11, 35, 36), (16, 47, 47), (47, 106, 99)
CORNER = {"l": (255, 204, 0), "r": (255, 51, 102)}


def main():
    img = Image.new("RGB", (W, H), BG)
    px = img.load()
    for x in range(12, W, 24):                          # the splash's faint grid
        for y in range(H):
            px[x, y] = GRID
    for y in range(12, H, 24):
        for x in range(W):
            px[x, y] = GRID
    for x in range(14, W - 14):                         # its frame, and its corners
        px[x, 14] = px[x, H - 15] = LINE
    for y in range(14, H - 14):
        px[14, y] = px[W - 15, y] = LINE
    for (cx, cy, dx, dy, c) in ((0, 0, 1, 1, "l"), (W - 1, 0, -1, 1, "r"), (0, H - 1, 1, -1, "l"), (W - 1, H - 1, -1, -1, "r")):
        for i in range(40):
            for t in range(6):
                px[cx + dx * i, cy + dy * t] = px[cx + dx * t, cy + dy * i] = CORNER[c]
    logo = Image.open(ROOT / "assets" / "roboseq-logo.png").convert("RGBA")  # the robots and the word, transparent
    logo = logo.resize((960, round(logo.height * 960 / logo.width)), Image.NEAREST)
    img.paste(logo, ((W - logo.width) // 2, 140 - logo.height // 2), logo)
    shots = ["home-playing", "battle-strike", "boards"]
    size, gap = 288, 24
    x0 = (W - (len(shots) * size + (len(shots) - 1) * gap)) // 2
    for i, s in enumerate(shots):
        sh = Image.open(ROOT / "docs" / "screens" / f"{s}.png").convert("RGB").resize((size, size), Image.NEAREST)
        x, y = x0 + i * (size + gap), 282
        for k in range(-2, size + 2):                   # a line round each, as the board's frame
            for t in (-2, -1):
                px[x + k, y + t] = px[x + k, y + size - 1 - t] = LINE
                px[x + t, y + k] = px[x + size - 1 - t, y + k] = LINE
        img.paste(sh, (x, y))
    out = ROOT / "web" / "og.png"
    img.save(out, optimize=True)
    print(f"og     -> {out.relative_to(ROOT)} ({out.stat().st_size // 1024} KB)")


if __name__ == "__main__":
    main()
