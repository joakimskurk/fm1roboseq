#!/usr/bin/env python3
"""RoboSeq's own logo (ROADMAP phase 0: Dinghy's assets/logo.svg is designburg's, not under the GPL): the two robots
of tools/art/robots.py and the lettering of tools/art/splash.py, the firmware's own pixel art, put together. Writes
assets/roboseq-logo.svg (every pixel a rect, crisp at any size), assets/roboseq-logo.png (1200 x 400) and
assets/roboseq-icon.png (the two robots, 512 x 512, for a card or an avatar).

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
LETTER = {"h": (0xd6, 0xff, 0xf0), "a": (0x8f, 0xff, 0xd1), "n": (0x3c, 0xb8, 0x96)}


def pixels():
    """the logo as (x, y, colour) on a grid: Scout-7, the word, Echo-3"""
    out = []
    for y in range(32):
        for x in range(32):
            c = robots.BOTS["A"][y][x]
            if c != ".":
                out.append((x, y, PAL["A"][c]))
    x0 = 36
    for i, ch in enumerate(splash.WORD):            # the lettering at 2 grid pixels a letter pixel, centred on the bots
        g = splash.letter(ch)
        for y in range(11):
            for x in range(7):
                if g[y][x] in LETTER:
                    for d in range(4):
                        out.append((x0 + i * 16 + x * 2 + d % 2, 5 + y * 2 + d // 2, LETTER[g[y][x]]))
    xb = x0 + len(splash.WORD) * 16 + 2
    for y in range(32):
        for x in range(32):
            c = robots.BOTS["B"][y][x]
            if c != ".":
                out.append((xb + x, y, PAL["B"][c]))
    return out, xb + 32


def main():
    from PIL import Image
    px, w = pixels()
    h, pad = 32, 4
    assets = ROOT / "assets"
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w + 2 * pad} {h + 2 * pad}" shape-rendering="crispEdges">',
           f'<title>RoboSeq</title><rect width="100%" height="100%" fill="#{BG[0]:02x}{BG[1]:02x}{BG[2]:02x}"/>']
    grid = {(x, y): c for x, y, c in px}
    for y in range(h):                                  # (a row's runs of one colour: one rect each)
        x = 0
        while x < w:
            c = grid.get((x, y))
            if c is None:
                x += 1
                continue
            n = 1
            while grid.get((x + n, y)) == c:
                n += 1
            svg.append(f'<rect x="{x + pad}" y="{y + pad}" width="{n}" height="1" fill="#{c[0]:02x}{c[1]:02x}{c[2]:02x}"/>')
            x += n
    svg.append("</svg>\n")
    (assets / "roboseq-logo.svg").write_text("\n".join(svg))
    img = Image.new("RGB", (w + 2 * pad, h + 2 * pad), BG)
    for x, y, c in px:
        img.putpixel((x + pad, y + pad), c)
    scale = 1200 // img.width
    img.resize((img.width * scale, img.height * scale), Image.NEAREST).save(assets / "roboseq-logo.png")
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
