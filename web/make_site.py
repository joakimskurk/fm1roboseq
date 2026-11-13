#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Joakim (joakimskurk)
"""RoboSeq's site (GitHub Pages, https://joakimskurk.github.io/fm1roboseq/): its page, its installer in the browser,
the firmware, the demos.

  index.html                 web/index.html with fm1pkg.js and fm1ota.js (SLOOP's, unchanged) and the metadata
                             inlined: the installer (Web MIDI, Chrome or Edge), the demos, the robots, the controls
  firmware/roboseq-VER.fwsc  the package the page installs (its SHA-256 in the page: a damaged download is never
                             offered)
  audio/                     BAND and ORIGINAL (web/render_demos.sh: the app itself on the host, as the FM-1 sends its
                             sound over USB), AAC, and each one's spectrum as a picture (WebP), low to high
  app/                       RoboSeq to play on the page: roboseq.wasm (web/app/build.sh: the firmware's own code
                             on web/app/web_host.c), worklet.js (the audio thread), play.js (the FM-1 drawn)
  screens/                   the pictures: the logo, the film, the screens (make screens), the link preview
                             (web/make_og.py)
  art/                       the README's art (assets/readme: the robots, the tiles, the moves, the strips)
  .nojekyll                  (served as it is)

  sh web/render_demos.sh     (first: the demos)
  sh web/app/build.sh        (and the firmware for the browser)
  python3 web/make_site.py build/roboseq.fwsc VERSION OUT_DIR

The package must be RoboSeq's (its identity FM-1_965) with its update loader in it (which the installer talks to).
The audio is encoded by ffmpeg, or afconvert on a Mac. After Star Distroyer's and VaveHero's web/make_site.py, after
SLOOP's (Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; isod89).
"""
import hashlib
import json
import re
import shutil
import subprocess
import sys
import wave
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
PICTURES = {"assets/roboseq-logo.svg": "logo.svg", "assets/roboseq-icon.png": "icon.png",
            "docs/screens/roboseq.gif": "film.gif", "web/og.png": "og.png"}
APP = {"build/web/roboseq.wasm": "roboseq.wasm", "web/app/worklet.js": "worklet.js", "web/app/play.js": "play.js"}
SCREENS = ("splash", "home-playing", "home-lead", "seq", "voicemod", "seq2", "drums", "mix", "boards", "builder", "glo",
           "battle", "battle-countdown", "battle-strike", "battle-winner")
DEMOS = [
    {"id": "band", "title": "BAND",
     "about": "RoboSeq's own engine. Scout-7 plays a tune that comes back every bar, Echo-3 answers it, and the "
              "board adds a bass groove and a drummer. 80 BPM."},
    {"id": "original", "title": "ORIGINAL",
     "about": "The same song as the original browser version plays it, note for note. Scout-7 plays a note on "
              "every step, Echo-3 adds harmony and bass, and the drums play the pattern as it is."},
]
# the spectrum's picture: 40 Hz .. 16 kHz on a log scale, 75 dB of range, in the splash's teal to mint
SPEC_W, SPEC_H, SPEC_N = 1600, 240, 8192
SPEC_LO, SPEC_HI, SPEC_DB = 40.0, 16000.0, 75.0
MINT = [(0.00, (11, 35, 36)), (0.30, (16, 47, 47)), (0.55, (47, 106, 99)), (0.78, (143, 255, 209)),
        (0.92, (214, 255, 240)), (1.00, (255, 255, 255))]


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def read_wav(path):
    with wave.open(str(path)) as f:
        if f.getsampwidth() != 2:
            raise SystemExit(f"{path}: not 16-bit")
        ch, fs = f.getnchannels(), f.getframerate()
        x = np.frombuffer(f.readframes(f.getnframes()), dtype="<i2").astype(np.float32)
    return x.reshape(-1, ch).mean(axis=1) / 32768.0, fs


def spectrum(x, fs, pic):
    """the piece's spectrum, left to right: a column a slice, the louder the brighter"""
    win = np.hanning(SPEC_N).astype(np.float32)
    x = np.concatenate([x, np.zeros(SPEC_N, np.float32)])
    hop = (len(x) - 2 * SPEC_N) / (SPEC_W - 1)
    freqs = np.fft.rfftfreq(SPEC_N, 1.0 / fs)
    edges = np.geomspace(SPEC_LO, SPEC_HI, SPEC_H + 1)
    lo = np.searchsorted(freqs, edges[:-1])
    cut = np.append(lo, np.searchsorted(freqs, edges[-1]))
    db = np.empty((SPEC_H, SPEC_W), np.float32)
    for c in range(SPEC_W):
        s = int(c * hop)
        m = np.abs(np.fft.rfft(x[s:s + SPEC_N] * win))
        db[:, c] = 20.0 * np.log10(np.maximum.reduceat(m, cut)[:-1] + 1e-9)   # each row: its bins' loudest
    ref = np.percentile(db, 99.8)
    t = np.clip((db - (ref - SPEC_DB)) / SPEC_DB, 0.0, 1.0)[::-1] ** 1.25   # high at the top; the floor darker
    rgb = np.zeros(t.shape + (3,), np.float32)
    for (t0, c0), (t1, c1) in zip(MINT, MINT[1:]):
        k = ((t >= t0) & (t <= t1))[..., None]
        f = ((t - t0) / (t1 - t0))[..., None]
        rgb = np.where(k, np.array(c0) + f * (np.array(c1) - np.array(c0)), rgb)
    Image.fromarray(rgb.astype(np.uint8), "RGB").save(pic, quality=84, method=6)   # (WebP: a fifth of a PNG)


def encode(wav, m4a):
    if shutil.which("ffmpeg"):
        cmd = ["ffmpeg", "-loglevel", "error", "-y", "-i", str(wav), "-c:a", "aac", "-b:a", "160k", "-movflags",
               "+faststart", str(m4a)]
    elif shutil.which("afconvert"):
        cmd = ["afconvert", "-f", "m4af", "-d", "aac", "-b", "160000", str(wav), str(m4a)]
    else:
        raise SystemExit("make_site: no ffmpeg or afconvert for the audio")
    subprocess.run(cmd, check=True)


def main(pkg, version, out):
    pkg, out = Path(pkg), Path(out)
    raw = pkg.read_bytes()
    product = product_of(raw)
    if product != "FM-1_965":
        raise SystemExit(f"{pkg}: identity {product!r} is not RoboSeq's (FM-1_965)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{pkg}: no update loader in it: the installer could not write it")
    if not (ROOT / "build" / "web" / "roboseq.wasm").exists():
        raise SystemExit("build/web/roboseq.wasm: missing (sh web/app/build.sh builds it)")
    for d in DEMOS:
        if not (ROOT / "build" / "audio" / f"{d['id']}.wav").exists():
            raise SystemExit(f"build/audio/{d['id']}.wav: missing (sh web/render_demos.sh renders it)")
    name = f"roboseq-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    if out.exists():
        shutil.rmtree(out)
    for sub in ("firmware", "screens", "audio", "art", "app"):
        (out / sub).mkdir(parents=True)
    shutil.copy(pkg, out / "firmware" / name)
    for src, dst in PICTURES.items():
        shutil.copy(ROOT / src, out / "screens" / dst)
    for src, dst in APP.items():
        shutil.copy(ROOT / src, out / "app" / dst)
    for s in SCREENS:
        shutil.copy(ROOT / "docs" / "screens" / f"{s}.png", out / "screens" / f"{s}.png")
    for p in sorted((ROOT / "assets" / "readme").glob("*.svg")):
        shutil.copy(p, out / "art" / p.name)
    demos = []
    for d in DEMOS:
        wav = ROOT / "build" / "audio" / f"{d['id']}.wav"
        x, fs = read_wav(wav)
        spectrum(x, fs, out / "audio" / f"{d['id']}.webp")
        encode(wav, out / "audio" / f"{d['id']}.m4a")
        demos.append({"id": d["id"], "title": d["title"], "about": d["about"], "src": f"audio/{d['id']}.m4a",
                      "spectrum": f"audio/{d['id']}.webp", "secs": round(len(x) / fs, 1)})
    meta = json.dumps({"version": version, "product": product, "pkg": "firmware/" + name,
                       "sha256": hashlib.sha256(raw).hexdigest(), "size": len(raw)})
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8"))
    html = (HERE / "index.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*META*/", "/*DEMOS*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"web/index.html must hold {mark} once")
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("/*DEMOS*/", json.dumps(demos))
    (out / "index.html").write_text(html, encoding="utf-8")
    (out / ".nojekyll").write_text("")
    size = sum(p.stat().st_size for p in out.rglob("*") if p.is_file())
    print(f"site: {out}: index.html ({len(html)} B), firmware/{name} ({len(raw)} B, {product}), "
          f"{len(demos)} demos, RoboSeq to play ({(out / 'app' / 'roboseq.wasm').stat().st_size // 1024} KB), "
          f"{len(PICTURES) + len(SCREENS)} pictures, {size // 1024} KB in all")


if __name__ == "__main__":
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    main(*sys.argv[1:])
