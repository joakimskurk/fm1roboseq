#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Joakim (joakimskurk)
"""The README's film: the host app's frames (tests/host.c with an FPS: OUT-NNNNN.ppm, the screen as the firmware draws
it) into one animated GIF, the pixels doubled. One palette for every frame (the firmware's colours are few), no
dithering, so the pixel art stays as it is drawn.

  python3 tools/art/gif.py build/gif/f docs/screens/roboseq.gif 12"""
import sys
from pathlib import Path
from PIL import Image

def main():
    stem, out, fps = sys.argv[1], Path(sys.argv[2]), int(sys.argv[3])
    paths = sorted(Path(stem).parent.glob(Path(stem).name + "-*.ppm"))
    if not paths:
        raise SystemExit(f"gif.py: no frames {stem}-*.ppm")
    frames = [Image.open(p).convert("RGB") for p in paths]
    while len(frames) > 1 and len(frames[0].getcolors(2) or ()) == 1:   # (the screen dark at boot: not in the film)
        frames.pop(0)
    sheet = Image.new("RGB", (240, 240 * len(frames[::4])))     # every fourth frame: the palette's sample
    for i, f in enumerate(frames[::4]):
        sheet.paste(f, (0, 240 * i))
    pal = sheet.quantize(colors=255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    big = [f.quantize(palette=pal, dither=Image.Dither.NONE).resize((480, 480), Image.NEAREST) for f in frames]
    big[0].save(out, save_all=True, append_images=big[1:], duration=round(1000 / fps), loop=0, optimize=True)
    print(f"gif    -> {out}  {len(frames)} frames, {out.stat().st_size // 1024} KB")

main()
