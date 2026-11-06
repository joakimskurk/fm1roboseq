#!/usr/bin/env python3
"""RoboSeq's tiles as pixel art on a 20 x 20 grid of 2 px (firmware/src/rs_screen.c draws them): written out as
firmware/src/rs_tiles.h, and a preview at 8x (build/art/tiles.png).

The light comes from the top left: each figure gets a light edge above and to its left ('h') and a drop shadow below
and to its right ('g'); each tile a frame light above and left ('b'), dark below and right ('k'). The letters are a
tile's palette (rs_screen.c tile_colour): '.' ground, 'g' ground in shadow, 'a' the tile's colour, 'h' it lit,
'b' it dimmed, 'd' it dimmer, 'k' it dark, 'c' a light accent; the crate's are its woods.

  python3 tools/art/tiles.py [--check]"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
N = 20


def grid(ch="."):
    return [[ch] * N for _ in range(N)]


def frame(g):                                       # light above and left, dark below and right
    for i in range(N):
        g[0][i] = g[i][0] = "b"
        g[N - 1][i] = g[i][N - 1] = "k"
    g[0][N - 1] = g[N - 1][0] = "d"


def shade(g, mask, body="a", lit="h", shadow="g", left=True):
    """a figure: its pixels body, the ones with open space above (or to the left) lit, its shadow one down-right"""
    for (x, y) in mask:
        if (x + 1, y + 1) not in mask and 0 < x + 1 < N - 1 and 0 < y + 1 < N - 1 and g[y + 1][x + 1] in ".bd":
            g[y + 1][x + 1] = shadow
    for (x, y) in mask:
        g[y][x] = lit if (x, y - 1) not in mask or (left and (x - 1, y) not in mask) else body


def rows_mask(rows, ch="#", ox=0, oy=0):
    return {(ox + x, oy + y) for y, r in enumerate(rows) for x, c in enumerate(r) if c == ch}


def belt():
    g = grid()
    frame(g)
    for x in range(1, N - 1):                       # the belt's surface: treads that run along it
        t = "d" if (x // 2) % 2 == 0 else "b"
        g[2][x] = g[17][x] = t
        g[3][x] = g[16][x] = "g"
    arrow = rows_mask([
        "........#.....",
        "........##....",
        "........###...",
        "........####..",
        "############..",
        "#############.",
        "#############.",
        "############..",
        "........####..",
        "........###...",
        "........##....",
        "........#.....",
    ], ox=3, oy=4)
    shade(g, arrow)
    return g


def rotator():
    """a gear whose ring is the turn, counter-clockwise: open at the top right, the arc runs from there round the
    left and the bottom up the right, its head pointing up into the gap (the right rotator is this one mirrored)"""
    g = grid()
    frame(g)
    c = 9.5
    ring = set()
    for y in range(1, N - 1):
        for x in range(1, N - 1):
            d, ang = math.hypot(x - c, y - c), math.degrees(math.atan2(c - y, x - c)) % 360
            if 4.4 <= d <= 6.4 and not 10 <= ang <= 75:     # the gap: from east-north-east up to north-east
                ring.add((x, y))
    teeth = set()
    for (x0, y0, w, h) in [(8, 16, 4, 3), (1, 8, 3, 4), (16, 10, 3, 2), (8, 1, 4, 3), (3, 3, 2, 2), (3, 15, 2, 2), (15, 15, 2, 2)]:
        teeth |= {(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)}
    head = rows_mask([                              # at the arc's end, pointing up into the gap
        "...#...",
        "..###..",
        ".#####.",
        "#######",
    ], ox=11, oy=3)
    shade(g, ring | teeth | head)
    for y in range(8, 12):                          # the hub
        for x in range(8, 12):
            if (x, y) not in [(8, 8), (11, 8), (8, 11), (11, 11)]:
                g[y][x] = "c" if (x, y) in [(9, 9), (10, 9), (9, 10)] else "a"
    return g


def zap():
    g = grid()
    frame(g)
    for y in range(2, N - 2):                       # the rails it sparks on
        g[y][4] = g[y][15] = "d" if y % 3 else "b"
    bolt = rows_mask([
        "......#####",
        ".....#####.",
        "....#####..",
        "...#####...",
        "..#####....",
        ".#########.",
        ".#########.",
        ".....####..",
        "....####...",
        "...####....",
        "..###......",
        ".##........",
        ".#.........",
    ], ox=4, oy=2)
    shade(g, bolt, left=False)
    for x in range(6, 13):                          # the hot crossbar
        g[8][x] = "c"
    g[2][3] = g[16][16] = "c"                       # sparks
    return g


def pulse():
    g = grid()
    frame(g)
    c = 9.5
    for y in range(1, N - 1):
        for x in range(1, N - 1):
            d = math.hypot(x - c, y - c)
            if 6.8 <= d <= 8.0:
                g[y][x] = "d"
            elif 4.4 <= d <= 5.6:
                g[y][x] = "b"
    pad = {(x, y) for y in range(7, 13) for x in range(7, 13)} - {(7, 7), (12, 7), (7, 12), (12, 12)}
    shade(g, pad)
    g[9][9] = g[9][10] = g[10][9] = "c"             # its glow
    return g


def crate():
    g = grid()
    for y in range(N):
        for x in range(N):
            if x in (0, N - 1) or y in (0, N - 1):
                ch = "."
            elif x in (1, N - 2) or y in (1, N - 2):
                ch = "k" if x == N - 2 or y == N - 2 else "c"
            elif (x, y) in [(2, 2), (17, 2), (2, 17), (17, 17)]:
                ch = "n"
            elif x == y or x + y == N - 1:
                ch = "n" if x in (9, 10) else "e"
            elif y in (7, 12):
                ch = "s"
            elif y in (2, 8, 13):
                ch = "h"                            # a plank's lit top
            elif 8 <= y <= 11:
                ch = "b"
            else:
                ch = "a"
            g[y][x] = ch
    return g


TILES = {"BELT": belt(), "ROTL": rotator(), "ZAP": zap(), "PULSE": pulse(), "BLOCK": crate()}


def header():
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */\n",
           "/* Generated by tools/art/tiles.py: RoboSeq's tiles, 20 x 20 on a grid of 2 px (the palette letters there). */\n"]
    for k, g in TILES.items():
        out.append(f"static const char ART_{k}[20][21] = {{\n" + ",\n".join(f'    "{"".join(r)}"' for r in g) + "};\n")
    return "".join(out)


def preview(path):
    from PIL import Image
    hx = lambda h: tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    mix = lambda a, b, t: tuple(int(a[i] * t + b[i] * (1 - t)) for i in range(3))
    def pal(ground, main, light="#ffffff"):
        G, M = hx(ground), hx(main)
        return {".": G, "g": mix(G, (0, 0, 0), .45), "a": M, "h": mix((255, 255, 255), M, .45), "b": mix(M, G, .45),
                "d": mix(M, G, .25), "k": mix(M, (0, 0, 0), .35), "c": hx(light)}
    P = {"BELT": pal("#001a2e", "#63d9ff"), "ROTL": pal("#1a1400", "#ffcc00", "#fff3b0"),
         "ZAP": pal("#1a0012", "#ff3366", "#ffd6e0"), "PULSE": pal("#001a00", "#73ff9f", "#eafff0"),
         "BLOCK": {".": hx("#1a1a2e"), "c": hx("#4a2e14"), "k": hx("#2e1c0c"), "a": hx("#6e4b28"), "b": hx("#5c3d1e"),
                   "h": hx("#8a6236"), "s": hx("#4a2e14"), "e": hx("#8b6939"), "n": hx("#aaaaaa")}}
    img = Image.new("RGB", (5 * 44, 40), (9, 46, 44))
    for i, (k, g) in enumerate(TILES.items()):
        for y in range(N):
            for x in range(N):
                for d in range(4):
                    img.putpixel((i * 44 + 2 * x + d % 2, 2 * y + d // 2), P[k][g[y][x]])
    path.parent.mkdir(parents=True, exist_ok=True)
    img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(path)


if __name__ == "__main__":
    out = ROOT / "firmware" / "src" / "rs_tiles.h"
    if "--check" in sys.argv:
        sys.exit(0 if out.read_text() == header() else "firmware/src/rs_tiles.h is not what tools/art/tiles.py writes")
    out.write_text(header())
    preview(ROOT / "build" / "art" / "tiles.png")
    print("wrote", out.relative_to(ROOT), "and build/art/tiles.png")
