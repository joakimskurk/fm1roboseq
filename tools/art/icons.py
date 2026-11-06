#!/usr/bin/env python3
"""RoboSeq's step icons (the registers down the board's sides, a steered bot's badge), 14 x 13 at 1 px, after the
HTML's ⇧ ⇩ ↺ ↻ ⇶ ✦ · and ⇅ (the registers swapped), and the modifiers. Lit from straight above as the tiles: a dark outline, a lit
top edge; a symmetric icon is its left half mirrored, pixel for pixel. The turns are the rotator's arrow in small. Written out as firmware/src/rs_icons.h, a preview
at 8x in build/art/icons.png.

Letters (rs_screen.c icon7): '.' nothing, 'o' outline, 'a' the action's colour, 'h' it lit.

  python3 tools/art/icons.py [--check]"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
W, H = 14, 13


def poly(pts):
    out = set()
    for y in range(H):
        for x in range(W):
            px, py, w = x + 0.5, y + 0.5, 0
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                cr = (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0)
                if y0 <= py < y1 and cr > 0:
                    w += 1
                elif y1 <= py < y0 and cr < 0:
                    w -= 1
            if w:
                out.add((x, y))
    return out


def ring(cx, cy, r0, r1, a0=None, a1=None):
    out = set()
    for y in range(H):
        for x in range(W):
            d = math.hypot(x - cx, y - cy)
            ang = math.degrees(math.atan2(cy - y, x - cx)) % 360
            if r0 <= d <= r1 and (a0 is None or not a0 <= ang <= a1):
                out.add((x, y))
    return out


def rect(x0, y0, w, h):
    return {(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)}


def flip_v(m):
    return {(x, H - 1 - y) for (x, y) in m}


def flip_h(m):
    return {(W - 1 - x, y) for (x, y) in m}


def sym(m):
    """symmetric about the middle, exactly: the left half, mirrored"""
    left = {(x, y) for (x, y) in m if x < W // 2}
    return left | {(W - 1 - x, y) for (x, y) in left}


def draw(mask):
    g = [["."] * W for _ in range(H)]
    for (x, y) in mask:
        for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            p = (x + dx, y + dy)
            if p not in mask and 0 <= p[0] < W and 0 <= p[1] < H:
                g[p[1]][p[0]] = "o"
    for (x, y) in mask:
        g[y][x] = "h" if (x, y - 1) not in mask else "a"
    return g


up = poly([(7, 0.5), (13, 6.5), (9.5, 6.5), (9.5, 12.5), (4.5, 12.5), (4.5, 6.5), (1, 6.5)])
def rounded(x1, y1, x2, y2, r):
    """the pixels of a box x1..x2, y1..y2 (inclusive) with corners rounded to r"""
    out = set()
    for y in range(y1, y2 + 1):
        for x in range(x1, x2 + 1):
            cx, cy = min(max(x, x1 + r), x2 - r), min(max(y, y1 + r), y2 - r)
            if (x - cx) ** 2 + (y - cy) ** 2 <= r * r + 0.5:
                out.add((x, y))
    return out


# the turn: a rounded ring, 2 px, open at the top right, its head going on up from the right side (counter-clockwise,
# as the rotator tile's gear arrow)
turn_l = {(x, y) for (x, y) in rounded(1, 2, 11, 12, 4) - rounded(3, 4, 9, 10, 2) if not (y < 6 and x >= 7)}
turn_l |= {(x, y) for y, (a, b) in enumerate([(10, 11), (9, 12), (8, 13), (10, 11), (10, 11), (10, 11)]) for x in range(a, b + 1)}   # the gear arrow, ccw
chevron = lambda y: poly([(7, y), (13, y + 6), (10, y + 6), (7, y + 3), (4, y + 6), (1, y + 6)])
star = poly([(7, 0.5), (8.6, 4.9), (13, 6.5), (8.6, 8.1), (7, 12.5), (5.4, 8.1), (1, 6.5), (5.4, 4.9)])
dot = ring(6.5, 6.5, 0, 2.3)
small_up = poly([(4, 1.5), (7, 5.5), (5.5, 5.5), (5.5, 11.5), (2.5, 11.5), (2.5, 5.5), (1, 5.5)])
swap = small_up | flip_h(flip_v(small_up))

up, star, dot = sym(up), sym(star), sym(dot)

# the modifiers (the HTML's step card says them in words): steady a level line, bright a sun, low a weight pointing
# down, wobble a wave, glitch a broken zigzag
steady = rect(1, 5, 12, 3)
SUN = ["......##......", "..#...##...#..", "...#......#...", ".....####.....", "....######....",
       "##..######..##", "##..######..##", "....######....", ".....####.....", "...#......#...",
       "..#...##...#..", "......##......", ".............."]
sun = {(x, y) for y, r in enumerate(SUN) for x, c in enumerate(r) if c == "#"}
low = poly([(1, 2.5), (13, 2.5), (7, 11.5)])
wave = {(x, y) for x in range(W) for y in range(H) if abs(y - (6 - 3.2 * math.sin(x / 13 * 2 * math.pi))) <= 1.0}
glitch = poly([(1, 9), (4, 3), (6, 7), (8, 2), (10, 8), (13, 4), (13, 7), (10, 11), (8, 6), (6, 10), (4, 6), (1, 12)])
ICONS = [draw(up), draw(flip_v(up)), draw(turn_l), draw(flip_h(turn_l)), draw(sym(chevron(0.5) | chevron(6.5))),
         draw(star), draw(dot), draw(swap),       # move back turnL turnR dash ping wait, then swapped
         draw(sym(steady)), draw(sym(sun)), draw(sym(low)), draw(wave), draw(glitch)]   # steady bright low wobble glitch


# the heart (love on a tile, the splash), 13 x 11 and exactly symmetric (the HTML's 9 x 8 is not): an outline round
# it ('o'), lit on top of each lobe ('h') with a shine on both ('w'), darker toward its tip ('n')
HEART_SHAPE = ["..###...###..", ".#####.#####.", "#############", "#############", "#############",
               ".###########.", "..#########..", "...#######...", "....#####....", ".....###.....", "......#......"]


def heart():
    on = lambda x, y: 0 <= y < 11 and 0 <= x < 13 and HEART_SHAPE[y][x] == "#"
    g = [["."] * 15 for _ in range(13)]             # one pixel of outline all round
    for y in range(13):
        for x in range(15):
            if on(x - 1, y - 1):
                g[y][x] = "h" if not on(x - 1, y - 2) else "n" if y >= 9 else "a"
            elif any(on(x - 1 + dx, y - 1 + dy) for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]):
                g[y][x] = "o"
    for (x, y) in [(3, 3), (4, 3), (3, 4)]:         # the shine, on both lobes
        g[y][x] = g[y][14 - x] = "w"
    assert all(r == r[::-1] for r in ["".join(r) for r in g])
    return ["".join(r) for r in g]


# digits, 5 x 7 (the step numbers on the sequence page)
DIGITS = [
    [".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."],
    ["..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."],
    [".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"],
    [".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###."],
    ["...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."],
    ["#####", "#....", "####.", "....#", "....#", "#...#", ".###."],
    ["..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."],
    ["#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."],
    [".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."],
    [".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."]]


def header():
    rows = ",\n".join("    {" + ", ".join(f'"{"".join(r)}"' for r in g) + "}" for g in ICONS)
    return ("/* SPDX-License-Identifier: GPL-3.0-only */\n"
            "/* Generated by tools/art/icons.py: the step icons, 14 x 13 at 1 px (move back turnL turnR dash ping wait, swapped,\n"
            " * then the modifiers steady bright low wobble glitch). */\n"
            f"static const char ICON[{len(ICONS)}][{H}][{W + 1}] = {{\n{rows}}};\n"
            "static const char HEART[13][16] = {\n" + ",\n".join(f'    "{r}"' for r in heart()) + "};\n"
            "static const char DIGIT[10][7][6] = {\n" + ",\n".join("    {" + ", ".join(f'"{r}"' for r in d) + "}" for d in DIGITS) + "};\n")


def preview(path):
    from PIL import Image
    cols = ["#73ff9f", "#73b7a9", "#ffcc00", "#63d9ff", "#ff3366", "#59d9c2", "#98d6be", "#ebfff6",
            "#98d6be", "#ffcc00", "#ff9900", "#8fffd1", "#ff3366"]
    hx = lambda h: tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    bg = hx("#1a1a2e")
    img = Image.new("RGB", (len(ICONS) * (W + 4), H + 4), bg)
    for i, g in enumerate(ICONS):
        a = hx(cols[i])
        lit = tuple(int(255 * .45 + c * .55) for c in a)
        for y in range(H):
            for x in range(W):
                c = {"o": (8, 8, 16), "a": a, "h": lit}.get(g[y][x])
                if c:
                    img.putpixel((2 + i * (W + 4) + x, 2 + y), c)
    path.parent.mkdir(parents=True, exist_ok=True)
    img.resize((img.width * 8, img.height * 8), Image.NEAREST).save(path)


if __name__ == "__main__":
    out = ROOT / "firmware" / "src" / "rs_icons.h"
    if "--check" in sys.argv:
        sys.exit(0 if out.read_text() == header() else "firmware/src/rs_icons.h is not what tools/art/icons.py writes")
    out.write_text(header())
    preview(ROOT / "build" / "art" / "icons.png")
    print("wrote", out.relative_to(ROOT), "and build/art/icons.png")
