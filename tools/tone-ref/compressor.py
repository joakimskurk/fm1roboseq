#!/usr/bin/env python3
"""Chromium's DynamicsCompressorKernel (third_party/blink/renderer/platform/audio/dynamics_compressor_kernel.cc), in
float, as Tone.Limiter(-6) runs it: threshold -6 dB, knee 30 (Tone's Compressor default), ratio 20, attack 3 ms,
release 10 ms. The reference the C limiter (rs_mix.c) is held to, and the generator of its tables (rs_tables.py).

  python3 tools/tone-ref/compressor.py      checks the model against build/probe/limiter_out.wav (probe_mix.mjs)"""
import math
import sys

import numpy as np

SR = 44100
DIVISION = 32                     # Chromium: the gain is worked out every 32 frames
PRE_DELAY = 0.006                 # s: the look-ahead
METER_RELEASE = 0.325
SAT_RELEASE = 0.0025              # s
ZONES = (0.09, 0.16, 0.42, 0.98)  # the adaptive release's zones
SPACING_DB = 5.0


def db2lin(d):
    return 10.0 ** (d / 20.0)


def lin2db(x):
    return 20.0 * math.log10(x) if x > 0 else -1000.0


class Curve:
    """the static curve: linear below the threshold, a soft knee (1 - exp) up to threshold + knee, then the ratio"""

    def __init__(self, threshold=-6.0, knee=30.0, ratio=20.0):
        self.db_threshold, self.db_knee, self.ratio = threshold, knee, ratio
        self.linear_threshold = db2lin(threshold)
        self.slope = 1.0 / ratio
        self.k = self.k_at_slope(1.0 / ratio)
        self.knee_threshold_db = threshold + knee
        self.knee_threshold = db2lin(self.knee_threshold_db)
        self.y_knee_threshold_db = lin2db(self.knee_curve(self.knee_threshold, self.k))

    def knee_curve(self, x, k):
        if x < self.linear_threshold:
            return x
        return self.linear_threshold + (1.0 - math.exp(-k * (x - self.linear_threshold))) / k

    def slope_at(self, x, k):
        if x < self.linear_threshold:
            return 1.0
        x2 = x * 1.001
        xd, x2d = lin2db(x), lin2db(x2)
        yd, y2d = lin2db(self.knee_curve(x, k)), lin2db(self.knee_curve(x2, k))
        return (y2d - yd) / (x2d - xd)

    def k_at_slope(self, desired):
        x = db2lin(self.db_threshold + self.db_knee)
        lo, hi, k = 0.1, 10000.0, 5.0
        for _ in range(15):
            if self.slope_at(x, k) < desired:
                hi = k
            else:
                lo = k
            k = math.sqrt(lo * hi)
        return k

    def saturate(self, x):
        if x < self.knee_threshold:
            return self.knee_curve(x, self.k)
        return db2lin(self.y_knee_threshold_db + self.slope * (lin2db(x) - self.knee_threshold_db))


def compress(x, threshold=-6.0, knee=30.0, ratio=20.0, attack=0.003, release=0.01, sr=SR):
    """x: mono float array -> the compressed signal (Chromium's kernel, effect blend 1, post gain 0 dB)"""
    c = Curve(threshold, knee, ratio)
    full_range_gain = c.saturate(1.0)
    makeup = (1.0 / full_range_gain) ** 0.6
    attack_frames = max(0.001, attack) * sr
    release_frames = release * sr
    sat_release_frames = SAT_RELEASE * sr
    y1, y2, y3, y4 = (release_frames * z for z in ZONES)
    kA = 0.9999999999999998 * y1 + 1.8432219684323923e-16 * y2 - 1.9373394351676423e-16 * y3 + 8.824516011816245e-18 * y4
    kB = -1.5788320352845888 * y1 + 2.3305837032074286 * y2 - 0.9141194204840429 * y3 + 0.1623677525612032 * y4
    kC = 0.5334142869106424 * y1 - 1.272736789213631 * y2 + 0.9258856042207512 * y3 - 0.18656310191776226 * y4
    kD = 0.08783463138207234 * y1 - 0.1694162967925622 * y2 + 0.08588057951595272 * y3 - 0.00429891410546283 * y4
    kE = -0.042416883008123074 * y1 + 0.1115693827987602 * y2 - 0.09764676325265872 * y3 + 0.028494263462021576 * y4

    pre = int(PRE_DELAY * sr)
    buf = np.zeros(1024)
    wi, ri = pre, 0
    detector, gain, max_att = 0.0, 1.0, -1.0       # Chromium's Reset()
    out = np.zeros(len(x))
    n = len(x) // DIVISION * DIVISION
    for d0 in range(0, n, DIVISION):
        desired = detector
        scaled = math.asin(desired) / (math.pi / 2)
        releasing = scaled > gain
        diff = (lin2db(gain / scaled) if gain > 0 else -1000.0) if scaled > 0 else float("inf")
        if releasing:
            max_att = -1.0
            if not math.isfinite(diff):
                diff = -1.0
            t = 0.25 * (min(0.0, max(-12.0, diff)) + 12.0)
            frames = kA + kB * t + kC * t * t + kD * t ** 3 + kE * t ** 4
            rate = db2lin(SPACING_DB / frames)
        else:
            if not math.isfinite(diff):
                diff = 1.0
            if max_att == -1.0 or max_att < diff:
                max_att = diff
            eff = max(0.5, max_att)
            rate = 1.0 - (0.25 / eff) ** (1.0 / attack_frames)
        for i in range(d0, d0 + DIVISION):
            buf[wi] = x[i]
            a = abs(x[i])
            att = 1.0 if a <= 0.0001 else c.saturate(a) / a
            att_db = max(2.0, -lin2db(att))
            sat_rate = db2lin(att_db / sat_release_frames) - 1.0
            detector += (att - detector) * (sat_rate if att > detector else 1.0)
            detector = min(1.0, detector)
            if rate < 1.0:
                gain += (scaled - gain) * rate
            else:
                gain = min(1.0, gain * rate)
            out[i] = buf[ri] * makeup * math.sin(math.pi / 2 * gain)
            wi, ri = (wi + 1) & 1023, (ri + 1) & 1023
    return out


def _wav(path):
    b = open(path, "rb").read()
    pos, ch, data = 12, 1, None
    while pos < len(b):
        cid, size = b[pos:pos + 4], int.from_bytes(b[pos + 4:pos + 8], "little")
        if cid == b"fmt ":
            ch = int.from_bytes(b[pos + 10:pos + 12], "little")
        elif cid == b"data":
            data = b[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)
    return np.frombuffer(data, dtype="<f4").reshape(-1, ch)[:, 0].astype(np.float64)


if __name__ == "__main__":
    c = Curve()
    print(f"k {c.k:.4f}  full-range gain {c.saturate(1.0):.4f}  makeup {(1 / c.saturate(1.0)) ** 0.6:.4f} "
          f"({lin2db((1 / c.saturate(1.0)) ** 0.6):+.2f} dB)")
    x, ref = _wav("build/probe/limiter_in.wav"), _wav("build/probe/limiter_out.wav")
    y = compress(x)
    n = min(len(y), len(ref))
    err = y[:n] - ref[:n]
    print(f"max |model - Chromium| {np.abs(err).max():.2e}, rms {np.sqrt((err ** 2).mean()):.2e}, "
          f"peak of Chromium's {np.abs(ref).max():.3f}")
    sys.exit(0 if np.abs(err).max() < 5e-3 else 1)
