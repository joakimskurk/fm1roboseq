#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Joakim (joakimskurk)
"""The C sound against the HTML's: renders tools/tone-ref/stimuli.json's stimuli through tests/parity/render (the C
voices and mix) and holds each against tests/golden/html (Tone.Offline renders of the HTML's own buildAudio()); the
C limiter against compressor.py (Chromium's compressor, checked against Chromium by probe_mix.mjs) and the C reverb's
impulse response against what Tone's is (mix_tables.py).

  python3 tools/tone-ref/compare.py [--check] [--json OUT] [name ...]       (make parity)

Per stimulus, in dB, over where the HTML sounds (within 40 dB of its peak):
  level  RMS of C minus the HTML's
  env    mean |difference| of the amplitude envelopes (analytic signal, 5 ms), the level taken out: the shape in time
  tone   |difference| of the 1/3-octave band levels (63 Hz..12.5 kHz), the level taken out, each band weighted by
         its share of the HTML's energy: the timbre
  worst  the band with the largest tone difference
--check fails when a stimulus is outside the tolerances in TOL (the noise voices are random on both sides: their
envelope and spectrum are compared, never their samples). Needs numpy."""
import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
HTML = ROOT / "tests" / "golden" / "html"
OUT = ROOT / "build" / "parity"
SPEC = json.loads((Path(__file__).resolve().parent / "stimuli.json").read_text())
SR = SPEC["sample_rate"]
VOICE = {"botA.main": "A.main", "botA.ping": "A.ping", "botB.fm": "B.fm", "botB.sub": "B.sub",
         "fx.collision": "fx.collision", "fx.belt": "fx.belt", "fx.rotator": "fx.rotator", "fx.zap": "fx.zap",
         "fx.pulse": "fx.pulse", "drums.kick": "drum.kick", "drums.snare": "drum.snare", "drums.hihat": "drum.hihat",
         "coin": "coin", "explosion.thump": "ex.thump", "explosion.noise": "ex.noise", "explosion.crackle": "ex.crackle"}
TOL = {"level": 0.5, "env": 0.75, "tone": 1.0}      # dB: what --check holds the oscillators and membranes to
# the noise voices are random on both sides (Tone loops one 5 s buffer from a random place; here xorshift): their
# 16 repeats' mean still varies, most in the brown noise's lowest bands, so they get more room
TOL_NOISE = {"level": 0.75, "env": 1.0, "tone": 1.5}
# the chains: their noise and the reverb's impulse are random on both sides, so they are held to about what the HTML
# is to itself rendered with another seed (RS_SEED=777 render.mjs: chain_buses env 2.05, song_default env 0.54,
# tone 0.09 / 0.08); the song's envelope gets more, as Chromium starts each oscillator at a phase of its own where
# the song puts it inside a render quantum (D11), which shows where two voices play one note (tick 3: A.main and
# B.sub on A3)
# (and band: no 1/3 octave further off than that; the mutations rs_mix.c must fail: no reverb, no low-pass, no
# makeup gain, a wrong bus level, the send at 0.3)
TOL_CHAIN = {"chain_buses": {"level": 0.15, "env": 2.5, "tone": 0.25, "band": 3.0},
             "song_default": {"level": 0.15, "env": 1.5, "tone": 0.35, "band": 3.0}}


def snap(t):                                          # render.mjs's: a start on a 128-frame quantum (D11)
    return round(t * SR / 128) * 128 / SR


def midi_of(name):
    if name is None:
        return -1
    pcs = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
    acc = 1 if name[1:2] == "#" else -1 if name[1:2] == "b" else 0
    return 12 * (int(name[1 + (acc != 0):]) + 1) + pcs[name[0]] + acc


def chain_notes(st):
    """a chain's calls ("a.fx.zap.triggerAttackRelease(0.06, 3.2, 0.6)") as m lines"""
    out = []
    for call in st["calls"]:
        m = re.match(r"a\.([\w.]+)\.triggerAttackRelease\((.*)\)$", call)
        args = [a.strip() for a in m.group(2).split(",")]
        voice = VOICE[m.group(1)]
        if args[0][0] in "'\"":
            out.append(f"m {voice} {midi_of(args[0][1:-1])} {args[1]} {args[2]} {args[3]}")
        else:
            out.append(f"m {voice} -1 {args[0]} {args[1]} {args[2]}")
    return out


def test_signal():                                    # probe_mix.mjs's limiter signal (render.c makes the same)
    n, x, i = round(14 * SR), np.zeros(round(14 * SR)), 0
    step = int(0.35 * SR)
    t = np.arange(n) / SR
    for k in range(17):
        x[i:i + step] = 10 ** ((-36 + 3 * k) / 20) * np.sin(2 * np.pi * 440 * t[i:i + step])
        i += step
    for b in range(8):
        ln = round((0.03 + 0.04 * b) * SR)
        x[i:i + ln] = 2 * np.sin(2 * np.pi * 220 * t[i:i + ln])
        i += ln
        q = int(0.4 * SR)
        x[i:i + q] = 0.063 * np.sin(2 * np.pi * 220 * t[i:i + q])
        i += q
    return np.round(x * 32768) / 32768


def check_limiter():
    """the gain over 5 ms against the model's: the decision between attack and release flips on a hair near the
    top of a step (Chromium against its own model too), so samples alone would fail where nothing is heard"""
    import compressor
    c = load(OUT / "limiter.wav")
    ref = compressor.compress(test_signal())
    n = min(len(c), len(ref))
    w = int(0.005 * SR)
    k = n // w
    rc = np.sqrt((c[:k * w].reshape(k, w) ** 2).mean(axis=1))
    rr = np.sqrt((ref[:k * w].reshape(k, w) ** 2).mean(axis=1))
    on = rr > 1e-3
    d = float(np.abs(20 * np.log10(rc[on] / rr[on])).max())
    ok = d < 0.15
    print(f"limiter        the gain over 5 ms within {d:.3f} dB of Chromium's model (peak {np.abs(ref).max():.2f})"
          f"{'' if ok else '   <- over 0.15 dB'}")
    return ok


def check_reverb():
    import mix_tables
    h = load(OUT / "reverb_ir.wav")
    e = float((h ** 2).sum())
    d = 10 * np.log10(e / mix_tables.ENERGY)
    w = int(0.01 * SR)                                # 10 ms windows, from 25 ms after the pre-delay to -50 dB
    t0 = mix_tables.PRE + int(0.025 * SR)
    seg = h[t0:t0 + int(0.5 * SR)]
    k = len(seg) // w
    env = 10 * np.log10((seg[:k * w].reshape(k, w) ** 2).mean(axis=1))
    tt = (np.arange(k) + 0.5) * w
    slope = np.polyfit(tt, env, 1)[0]                 # dB a sample
    tau = -20 / np.log(10) / slope                    # power falls 20 / ln 10 / tau dB a sample
    dt = tau / mix_tables.TAU - 1
    early = 10 * np.log10((h[mix_tables.PRE:t0] ** 2).sum() / e)
    want = 10 * np.log10(1 - np.exp(-2 * 0.025 * SR / mix_tables.TAU))
    hr = load(OUT / "reverb_ir.wav", 1)             # the right: its own, as loud, apart from the left
    dr = 10 * np.log10(float((hr ** 2).sum()) / mix_tables.ENERGY)
    corr = float((h * hr).sum() / np.sqrt((h ** 2).sum() * (hr ** 2).sum()))
    ok = abs(d) < 0.25 and abs(dt) < 0.03 and abs(early - want) < 1.0 and abs(dr) < 0.5 and abs(corr) < 0.1
    print(f"reverb_ir      energy {d:+.2f} dB (right {dr:+.2f}), decay {100 * dt:+.1f} %, first 25 ms {early - want:+.2f} dB,"
          f" left and right correlate {corr:+.3f}{'' if ok else '   <- over 0.25 dB / 3 % / 1 dB / 0.5 dB / 0.1'}")
    return ok


def load(path, chan=0):
    b = path.read_bytes()
    assert b[:4] == b"RIFF" and b[8:12] == b"WAVE", path
    pos, ch, fmt, data = 12, 1, 3, None
    while pos < len(b):
        cid, size = b[pos:pos + 4], int.from_bytes(b[pos + 4:pos + 8], "little")
        if cid == b"fmt ":
            fmt = int.from_bytes(b[pos + 8:pos + 10], "little")
            ch = int.from_bytes(b[pos + 10:pos + 12], "little")
        elif cid == b"data":
            data = b[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    if fmt == 1:                                          # 16-bit PCM (the voices' goldens)
        return np.frombuffer(data, dtype="<i2").reshape(-1, ch)[:, min(chan, ch - 1)].astype(np.float64) / 32768.0
    return np.frombuffer(data, dtype="<f4").reshape(-1, ch)[:, min(chan, ch - 1)].astype(np.float64)


def env_db(x, win=int(0.005 * SR)):
    """the amplitude envelope (the analytic signal's magnitude, so a low note's phase doesn't show), 5 ms means"""
    m = 2 * len(x)                                        # zero-padded: no wrap from the end to the start
    X = np.fft.fft(x, m)
    h = np.zeros(m)
    h[0] = h[m // 2] = 1
    h[1:m // 2] = 2
    a = np.abs(np.fft.ifft(X * h))[:len(x)]
    n = len(a) // win
    e = np.sqrt((a[:n * win].reshape(n, win) ** 2).mean(axis=1))
    return 20 * np.log10(np.maximum(e, 1e-9))


def bands_db(x):
    spec = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    f = np.fft.rfftfreq(len(x), 1 / SR)
    centres = 1000 * 2.0 ** (np.arange(-12, 12) / 3)      # 63 Hz .. 12.5 kHz
    out = []
    for c in centres:
        m = (f >= c / 2 ** (1 / 6)) & (f < c * 2 ** (1 / 6))
        out.append(10 * np.log10(max(spec[m].sum(), 1e-20)))
    return centres, np.array(out)


def fold(e, times, every):                            # the mean power of the repeats' envelopes, dB
    k = int(round(every * SR / int(0.005 * SR)))
    segs = np.array([e[r * k:(r + 1) * k] for r in range(times) if (r + 1) * k <= len(e)])
    return 10 * np.log10(np.maximum((10 ** (segs / 10)).mean(axis=0), 1e-18))


def measure(h, c, repeat=None):
    n = min(len(h), len(c))
    h, c = h[:n], c[:n]
    eh, ec = env_db(h), env_db(c)
    if repeat:
        eh, ec = fold(eh, *repeat), fold(ec, *repeat)
    on = eh > eh.max() - 40
    level = 20 * np.log10(np.sqrt((c ** 2).mean()) / max(np.sqrt((h ** 2).mean()), 1e-12))
    env = float(np.abs((ec[on] - eh[on]) - (ec[on] - eh[on]).mean()).mean())
    centres, bh = bands_db(h)
    _, bc = bands_db(c)
    w = 10 ** (bh / 10)                                   # each band weighted by its share of the HTML's energy
    w /= w.sum()
    d = (bc - bh) - ((bc - bh) * w).sum()
    loud = bh > bh.max() - 30
    worst = int(np.argmax(np.abs(d) * loud))
    return {"level": float(level), "env": env, "tone": float((np.abs(d) * w).sum()),
            "band": float(abs(d[worst])), "worst": f"{centres[worst]:.0f} Hz {d[worst]:+.1f}"}


def main():
    args = [a for a in sys.argv[1:]]
    check = "--check" in args
    js = None
    if "--json" in args:
        js = args[args.index("--json") + 1]
        args.remove("--json"), args.remove(js)
    names = [a for a in args if not a.startswith("--")]
    OUT.mkdir(parents=True, exist_ok=True)
    lines = []
    for st in SPEC["voices"]:
        if names and st["name"] not in names:
            continue
        lines.append(f"S {st['name']} {VOICE[st['voice']]} {st['seconds']}")
        times, every = st.get("repeat", [1, 0])
        lines += [f"n {midi_of(n)} {d} {snap(t + r * every)!r} {v}" for r in range(times) for n, d, t, v in st["notes"]]
    for st in SPEC["chains"]:
        if names and st["name"] not in names:
            continue
        lines.append(f"{'G' if st.get('song') else 'M'} {st['name']} {st['seconds']}")
        if not st.get("song"):
            lines += chain_notes(st)
    if not names:
        lines += ["L limiter 14.0", "R reverb_ir 1.3"]
    (OUT / "stimuli.txt").write_text("\n".join(lines) + "\n")
    subprocess.run([str(ROOT / "build" / "test" / "parity_render"), str(OUT / "stimuli.txt"), str(OUT)], check=True)
    results, bad = {}, []
    print(f"{'stimulus':14} {'level':>7} {'env':>6} {'tone':>6}  worst band")
    for st in SPEC["voices"]:
        if names and st["name"] not in names:
            continue
        r = measure(load(HTML / f"{st['name']}.wav"), load(OUT / f"{st['name']}.wav"), st.get("repeat"))
        results[st["name"]] = r
        tol = TOL_NOISE if st.get("repeat") else TOL
        over = [k for k in tol if abs(r[k]) > tol[k]]
        if over:
            bad.append(st["name"])
        print(f"{st['name']:14} {r['level']:+7.2f} {r['env']:6.2f} {r['tone']:6.2f}  {r['worst']}{'   <- ' + ', '.join(over) if over else ''}")
    for st in SPEC["chains"]:
        if names and st["name"] not in names:
            continue
        for chan, side in ((0, ""), (1, " R")):    # both channels: the HTML's reverb is stereo, and so is ours
            r = measure(load(HTML / f"{st['name']}.wav", chan), load(OUT / f"{st['name']}.wav", chan))
            results[st["name"] + side] = r
            tol = TOL_CHAIN[st["name"]]
            over = [k for k in tol if abs(r[k]) > tol[k]]
            if over:
                bad.append(st["name"] + side)
            print(f"{(st['name'] + side):14} {r['level']:+7.2f} {r['env']:6.2f} {r['tone']:6.2f}  {r['worst']}{'   <- ' + ', '.join(over) if over else ''}")
    if not names:
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        if not check_limiter():
            bad.append("limiter")
        if not check_reverb():
            bad.append("reverb_ir")
    if js:
        Path(js).write_text(json.dumps(results, indent=1) + "\n")
    if check and bad:
        print("outside the tolerances:", ", ".join(bad))
        sys.exit(1)


main()
