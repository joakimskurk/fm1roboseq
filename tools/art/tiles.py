#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Joakim (joakimskurk)
"""RoboSeq's tiles as pixel art, 40 x 40 at 1 px (firmware/src/rs_screen.c draws them 1:1 in a cell; each way a
belt points and each rotator drawn on its own, so the light stays above): written out as
firmware/src/rs_tiles.h, and a preview at 4x (build/art/tiles.png).

The light comes from straight above, so what is symmetric stays so: a figure gets a dark outline ('k'), a lit edge
where it faces up ('h') and a drop shadow two pixels below it ('g'); a tile a frame light above ('b'), dark below
('k'), between at the sides ('d').
The letters are a tile's palette (rs_screen.c tile_colour): '.' ground, 'g' ground in shadow, 'a' the tile's colour,
'h' it lit, 'b' it dimmed, 'd' it dimmer, 'k' it dark, 'c' a light accent; the crate's are its woods ('n' nails).

  python3 tools/art/tiles.py [--check]"""
import math
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
N = 40
C = (N - 1) / 2


def grid(ch="."):
    return [[ch] * N for _ in range(N)]


def frame(g):
    for i in range(N):
        g[i][0] = g[i][N - 1] = "d"
    for i in range(N):
        g[0][i], g[N - 1][i] = "b", "k"
        if 0 < i < N - 1:
            g[1][i] = "d"


def poly(pts):
    """the pixels whose centres are inside the polygon (nonzero rule)"""
    out = set()
    for y in range(N):
        for x in range(N):
            px, py, w = x + 0.5, y + 0.5, 0
            for i in range(len(pts)):
                (x0, y0), (x1, y1) = pts[i], pts[(i + 1) % len(pts)]
                if y0 <= py < y1 and (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) > 0:
                    w += 1
                elif y1 <= py < y0 and (x1 - x0) * (py - y0) - (px - x0) * (y1 - y0) < 0:
                    w -= 1
            if w:
                out.add((x, y))
    return out


def disc(cx, cy, r0, r1=None, a0=None, a1=None):
    """an annulus r0..r1 (or a disc r0) around cx, cy, optionally without the angles a0..a1 (degrees, ccw from east)"""
    out = set()
    for y in range(N):
        for x in range(N):
            d = math.hypot(x - cx, y - cy)
            if (d <= r0 if r1 is None else r0 <= d <= r1):
                ang = math.degrees(math.atan2(cy - y, x - cx)) % 360
                if a0 is None or not a0 <= ang <= a1:
                    out.add((x, y))
    return out


def rect(x0, y0, w, h):
    return {(x, y) for y in range(y0, y0 + h) for x in range(x0, x0 + w)}


def inside(x, y):
    return 1 < x < N - 2 and 1 < y < N - 2


def shade(g, mask, body="a", lit="h", line="k", shadow="g", ground=".bd"):
    """a figure, lit from above: outline, lit top edge, body, its shadow below"""
    for (x, y) in mask:
        for dx, dy in [(0, 1), (0, 2)]:
            p = (x + dx, y + dy)
            if p not in mask and inside(*p) and g[p[1]][p[0]] in ground:
                g[p[1]][p[0]] = shadow
    for (x, y) in mask:
        for dx, dy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            p = (x + dx, y + dy)
            if p not in mask and inside(*p) and g[p[1]][p[0]] in ground + "g":
                g[p[1]][p[0]] = line
    for (x, y) in mask:
        g[y][x] = lit if (x, y - 1) not in mask else body


def turn(mask, d):
    """a shape drawn pointing east, turned to d (N E S W): turned before it is lit, so the light stays above"""
    f = {"E": lambda x, y: (x, y), "W": lambda x, y: (N - 1 - x, y),
         "N": lambda x, y: (y, N - 1 - x), "S": lambda x, y: (N - 1 - y, x)}[d]
    return {f(x, y) for (x, y) in mask}


def belt(d):
    g = grid()
    frame(g)
    tread = set()
    for x in range(2, N - 2):                       # the belt's surface: slats across it, as it runs
        for y0 in (3, 33):
            for k in range(4):
                tread.add((x, y0 + k, (x - 2) % 6 < 3))
    for (x, y, lit) in tread:
        (tx, ty), = turn({(x, y)}, d)
        g[ty][tx] = "b" if lit else "d"
    shade(g, turn(poly([(6, 16), (21, 16), (21, 8), (34, 20), (21, 32), (21, 24), (6, 24)]), d))
    return g


def rotator(right=False):
    """a gear whose ring is the turn, counter-clockwise: open at the top right, running round the left and the
    bottom and up the right, its head pointing up into the gap (the right rotator is this one mirrored)"""
    g = grid()
    frame(g)
    ring = disc(C, C, 9.0, 13.0, 8, 80)
    teeth = rect(17, 3, 6, 5) | rect(17, 32, 6, 5) | rect(3, 17, 5, 6)
    for a in (135, 225, 315):                       # the diagonal teeth
        cx, cy = C + 13.5 * math.cos(math.radians(a)), C - 13.5 * math.sin(math.radians(a))
        teeth |= disc(cx, cy, 2.6)
    head = poly([(30.5, 5), (37.5, 15), (33.0, 15), (33.0, 19), (28.0, 19), (28.0, 15), (23.5, 15)])
    fig = ring | teeth | head
    if right:                                       # clockwise: the mirror image, lit as itself
        fig = {(N - 1 - x, y) for (x, y) in fig}
    shade(g, fig)
    hub = disc(C, C, 3.6)
    shade(g, hub)
    for (x, y) in [(18, 18), (19, 18), (18, 19)]:
        g[y][x] = "c"
    return g


def zap():
    g = grid()
    frame(g)
    for y in range(3, N - 3):                       # the rails it sparks on
        for x in (7, 32):
            g[y][x] = "b" if y % 6 < 4 else "d"
    bolt = poly([(25, 4), (14, 20), (22, 20), (13, 36), (29, 17), (21, 17), (31, 4)])
    shade(g, bolt)
    for (x, y) in sorted(bolt):                     # the hot core: the bolt's middle, one pixel wide
        if (x - 1, y) in bolt and (x + 1, y) in bolt and (x, y - 1) in bolt and (x, y + 1) in bolt and \
                (x - 2, y) in bolt and (x + 2, y) in bolt:
            g[y][x] = "c"
    for (sx, sy) in [(6, 6), (33, 33)]:             # sparks
        for dx, dy in [(0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)]:
            g[sy + dy][sx + dx] = "c" if (dx, dy) == (0, 0) else "a"
    return g


def pulse():
    g = grid()
    frame(g)
    for (x, y) in disc(C, C, 14.0, 15.5):
        if inside(x, y):
            g[y][x] = "d"
    for (x, y) in disc(C, C, 10.0, 11.5):
        g[y][x] = "b"
    pad = rect(14, 14, 12, 12) - {(14, 14), (25, 14), (14, 25), (25, 25)}
    shade(g, pad)
    for (x, y) in disc(C, 17.0, 2.2):             # its glow, where the light falls
        g[y][x] = "c"
    return g


def crate():
    g = grid()
    for y in range(N):
        for x in range(N):
            if x in (0, N - 1) or y in (0, N - 1) or x in (1, N - 2) or y in (1, N - 2):
                ch = "."
            elif x in (2, 3) or y in (2, 3):
                ch = "c"
            elif x in (N - 4, N - 3) or y in (N - 4, N - 3):
                ch = "k"
            elif y in (14, 15, 25, 26):
                ch = "s" if y in (15, 26) else "k"
            else:
                band = 0 if y < 14 else 1 if y < 25 else 2
                ch = "b" if band == 1 else "a"
                if y in (4, 16, 27):
                    ch = "h"                        # a plank's lit top
                elif (x * 7 + y * 3 + band * 5) % 23 == 0:
                    ch = "s"                        # the grain
            g[y][x] = ch
    braces = poly([(4, 6), (6, 4), (36, 34), (34, 36)]) | poly([(34, 4), (36, 6), (6, 36), (4, 34)])
    for (x, y) in braces:
        if 3 < x < N - 4 and 3 < y < N - 4:
            g[y][x] = "e"
    for (x, y) in [(4, 4), (34, 4), (4, 34), (34, 34), (19, 19)]:
        g[y][x], g[y][x + 1], g[y + 1][x], g[y + 1][x + 1] = "c", "n", "n", "n"
        g[y][x] = "h"
    return g


TILES = {"BELT_N": belt("N"), "BELT_E": belt("E"), "BELT_S": belt("S"), "BELT_W": belt("W"), "ROTL": rotator(),
         "ROTR": rotator(True), "ZAP": zap(), "PULSE": pulse(), "BLOCK": crate()}


def header():
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */\n/* Copyright (C) 2026 Joakim (joakimskurk) */\n",
           "/* Generated by tools/art/tiles.py: RoboSeq's tiles, 40 x 40 at 1 px (the palette letters there). */\n"]
    for k, g in TILES.items():
        out.append(f"static const char ART_{k}[{N}][{N + 1}] = {{\n" + ",\n".join(f'    "{"".join(r)}"' for r in g) + "};\n")
    return "".join(out)


def preview(path):
    from PIL import Image
    hx = lambda h: tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
    mix = lambda a, b, t: tuple(int(a[i] * t + b[i] * (1 - t)) for i in range(3))
    def pal(ground, main, light="#ffffff"):
        G, M = hx(ground), hx(main)
        return {".": G, "g": mix(G, (0, 0, 0), .45), "a": M, "h": mix((255, 255, 255), M, .45), "b": mix(M, G, .45),
                "d": mix(M, G, .25), "k": mix(M, (0, 0, 0), .35), "c": hx(light)}
    belt_pal, rot_pal = pal("#001a2e", "#63d9ff"), pal("#1a1400", "#ffcc00", "#fff3b0")
    P = {"BELT_N": belt_pal, "BELT_E": belt_pal, "BELT_S": belt_pal, "BELT_W": belt_pal, "ROTL": rot_pal, "ROTR": rot_pal,
         "ZAP": pal("#1a0012", "#ff3366", "#ffd6e0"), "PULSE": pal("#001a00", "#73ff9f", "#eafff0"),
         "BLOCK": {".": hx("#1a1a2e"), "c": hx("#4a2e14"), "k": hx("#2e1c0c"), "a": hx("#6e4b28"), "b": hx("#5c3d1e"),
                   "h": hx("#8a6236"), "s": hx("#4a2e14"), "e": hx("#8b6939"), "n": hx("#aaaaaa")}}
    img = Image.new("RGB", (len(TILES) * (N + 4), N), (9, 46, 44))
    for i, (k, g) in enumerate(TILES.items()):
        for y in range(N):
            for x in range(N):
                img.putpixel((i * (N + 4) + x, y), P[k][g[y][x]])
    path.parent.mkdir(parents=True, exist_ok=True)
    img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(path)


if __name__ == "__main__":
    out = ROOT / "firmware" / "src" / "rs_tiles.h"
    if "--check" in sys.argv:
        sys.exit(0 if out.read_text() == header() else "firmware/src/rs_tiles.h is not what tools/art/tiles.py writes")
    out.write_text(header())
    preview(ROOT / "build" / "art" / "tiles.png")
    print("wrote", out.relative_to(ROOT), "and build/art/tiles.png")
