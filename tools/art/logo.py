#!/usr/bin/env python3
"""RoboSeq's own logo (ROADMAP phase 0: Dinghy's assets/logo.svg is designburg's, not under the GPL): the two robots
of tools/art/robots.py either side of the lettering of tools/art/splash.py, the firmware's own pixel art, on nothing
(transparent): "Robo" mint and "Seq" yellow as on the splash, each letter pixel three, lit on top and shaded at the
foot, extruded two down and right in its own darker shade, and a dark line round it all, so it reads on a light page
as on a dark one; the capitals as tall as the robots, their feet on the baseline. Writes assets/roboseq-logo.svg
(a path a colour, crisp at any size), assets/roboseq-logo.png (about 1200 wide, transparent) and
assets/roboseq-icon.png (the two robots, 512 x 512, on the splash's ground, for a card or an avatar).

  python3 tools/art/logo.py"""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import robots  # noqa: E402  (the 32 x 32 art, BOTS)
import splash  # noqa: E402  (the lettering, letter())

HX = lambda h: tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))
MIX = lambda a, b, t: tuple(int(a[i] * t + b[i] * (1 - t)) for i in range(3))
BG = HX("#092e2c")


def palette(main, dark, acc):
    m, d, a = HX(main), HX(dark), HX(acc)
    return {"o": HX("#05050c"), "m": m, "M": MIX((255, 255, 255), m, .35), "n": MIX(m, d, .5), "d": d, "a": a,
            "v": HX("#0a0a1a"), "w": (255, 255, 255), "t": MIX(a, BG, .6)}


PAL = {"A": palette("#ff6666", "#cc3333", "#8fffd1"), "B": palette("#ff9900", "#cc7700", "#ffcc00")}
# the splash's two inks (rs_splash.c): lit, body, shaded, and the extrusion's darker shade of each
INK = {"mint": {"h": HX("#d6fff0"), "a": HX("#8fffd1"), "n": HX("#3cb896"), "x": HX("#1f6b58")},
       "yellow": {"h": HX("#fff0b0"), "a": HX("#ffcc00"), "n": HX("#c99a00"), "x": HX("#7a5a00")}}
LINE = HX("#061413")                                # the dark line round the lettering
R, S, DEPTH, RIM = 2, 5, 3, 2                       # a robot pixel's size, a letter pixel's; the extrusion; the line
LY = 32 * R - 9 * S                                 # the letters' top: their baseline (row 9) on the robots' feet
FINE = 2                                            # the lit row on a letter pixel's top, the shaded one at its foot


def lettering(x0):
    """the word from x0: {(x, y): colour}, the letters over their extrusion, a line round both; and its right edge"""
    face, deep = {}, {}
    for i, ch in enumerate(splash.WORD):
        ink = INK["mint" if i < 4 else "yellow"]
        g = splash.letter(ch)
        for gy in range(11):
            for gx in range(7):
                k = g[gy][gx]
                if k not in "han":
                    continue
                for dy in range(S):                 # lit only on its top rows, shaded only at its foot: finer than a block
                    for dx in range(S):
                        c = ink["h"] if k == "h" and dy < FINE else ink["n"] if k == "n" and dy >= S - FINE else ink["a"]
                        p = (x0 + i * (7 * S + S) + gx * S + dx, LY + gy * S + dy)
                        face[p] = c
                        for d in range(1, DEPTH + 1):
                            deep[(p[0] + d, p[1] + d)] = ink["x"]
    body = {**deep, **face}
    ring = range(-RIM, RIM + 1)
    line = {(x + dx, y + dy) for x, y in body for dx in ring for dy in ring if abs(dx) + abs(dy) <= RIM + 1} - set(body)
    return {**{p: LINE for p in line}, **body}, max(x for x, _ in line)


def pixels():
    """the logo as {(x, y): colour} on a grid, transparent where none: Scout-7, the word, Echo-3"""
    out = {}
    def bot(k, x0):
        for y in range(32):
            for x in range(32):
                c = robots.BOTS[k][y][x]
                if c != ".":
                    for d in range(R * R):
                        out[(x0 + x * R + d % R, y * R + d // R)] = PAL[k][c]
    bot("A", 0)
    word, right = lettering(32 * R + 6)
    out.update(word)
    bot("B", right + 3)
    return out, right + 3 + 32 * R, max(y for _, y in out) + 1


def main():
    from PIL import Image
    grid, w, h = pixels()
    pad = 2
    assets = ROOT / "assets"
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w + 2 * pad} {h + 2 * pad}" '
           f'width="{(w + 2 * pad) * 4}" height="{(h + 2 * pad) * 4}" shape-rendering="crispEdges">', "<title>RoboSeq</title>"]
    for c in dict.fromkeys(grid.values()):              # (a path a colour: a row's runs of it)
        runs = []
        for y in range(h):
            x = 0
            while x < w:
                if grid.get((x, y)) != c:
                    x += 1
                    continue
                n = 1
                while grid.get((x + n, y)) == c:
                    n += 1
                runs.append(f"M{x + pad} {y + pad}h{n}v1h-{n}z")
                x += n
        svg.append(f'<path fill="#{c[0]:02x}{c[1]:02x}{c[2]:02x}" d="{"".join(runs)}"/>')
    svg.append("</svg>\n")
    (assets / "roboseq-logo.svg").write_text("\n".join(svg))
    img = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    for (x, y), c in grid.items():
        img.putpixel((x + pad, y + pad), c + (255,))
    scale = 1200 // img.width
    img.resize((img.width * scale, img.height * scale), Image.NEAREST).save(assets / "roboseq-logo.png", optimize=True)
    icon = Image.new("RGB", (76, 76), BG)               # the icon: the two robots, side by side
    for i, k in enumerate("AB"):
        for y in range(32):
            for x in range(32):
                c = robots.BOTS[k][y][x]
                if c != ".":
                    icon.putpixel((4 + i * 36 + x, 22 + y), PAL[k][c])
    icon.resize((76 * 6, 76 * 6), Image.NEAREST).resize((512, 512), Image.NEAREST).save(assets / "roboseq-icon.png")
    print("wrote assets/roboseq-logo.svg, assets/roboseq-logo.png and assets/roboseq-icon.png")


if __name__ == "__main__":
    main()
