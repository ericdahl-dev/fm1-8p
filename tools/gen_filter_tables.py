#!/usr/bin/env python3
"""Print the lookup tables used by engine/filter.c.

exp2_16c: 2^(16 i / 1200) for i = 0..75 in Q2.30, the pitch-to-ratio
          table (one entry per 16 cents across an octave; the engine
          interpolates linearly between entries).
h:        (1 - e^-x) / x for x = i / 16, i = 0..64, in Q1.30 (h(0) = 1).
          The one-pole coefficient is g = x * h(x) with x = 2 pi fc / fs,
          which puts the pole exactly where an analog pole at fc maps
          (matched z-transform) without exp() in the engine.
"""
import math


def dump(name, values):
    print("static const uint32_t %s[%d] = {" % (name, len(values)))
    for i in range(0, len(values), 5):
        row = ", ".join("%du" % v for v in values[i:i + 5])
        print("    " + row + ("," if i + 5 < len(values) else ""))
    print("};")


dump("exp2_16c", [round(2.0 ** (16 * i / 1200.0) * (1 << 30)) for i in range(76)])
h = [1 << 30]
for i in range(1, 65):
    x = i / 16.0
    h.append(round((1.0 - math.exp(-x)) / x * (1 << 30)))
dump("h_tab", h)


# warp: cents added to the lowpass cutoff before its coefficient, by the
# cutoff's distance below the sample rate, -6 octaves to -1 (Nyquist) in
# quarter octaves. A digital ladder's resonant peak sinks toward Nyquist:
# with the one-pole stages, the w = 1/4 zero and the one-sample feedback
# delay, it sits 3 cents lower at 1 kHz (48 kHz) and 270 lower at 10 kHz
# than its place well below. Each entry lifts the coefficient so the peak
# (resonance 875: the stages' feedback 3.675, less kcut below) keeps that place: the peak's
# distance from the cutoff is the same as 6 octaves under the rate. Near
# Nyquist that would take the corner past it: from the last entry whose
# corrected corner is under WARP_TOP (2.25 octaves under the rate) the
# correction tapers linearly to 0 at Nyquist, so the corner keeps rising.
WARP_FROM, WARP_STEP, WARP_N, WARP_TOP = -6 * 1200, 300, 21, -2700
W, K = 0.25, 875 * 4.2 / 1000


def peak_ratio(x, k=K):
    """Peak frequency over the stage corner, for a corner at x = fc / fs, feedback k."""
    g = 1.0 - math.exp(-2.0 * math.pi * x)

    def mag(r):
        z1 = complex(math.cos(2 * math.pi * x * r), -math.sin(2 * math.pi * x * r))
        s = g * ((1 - W) + W * z1) / (1 - (1 - g) * z1)
        return abs(s ** 4 / (1 + k * z1 * s ** 4))
    lo, hi = 0.5, min(1.5, 0.5 / x)
    for _ in range(4):   # a coarse search, then finer around the best
        n = 200
        best = max((mag(lo + (hi - lo) * i / n), lo + (hi - lo) * i / n) for i in range(n + 1))[1]
        span = (hi - lo) / n * 2
        lo, hi = max(best - span, 0.01), min(best + span, 0.5 / x)
    return best


def warp():
    base = 1200 * math.log2(peak_ratio(2.0 ** (WARP_FROM / 1200)))
    out, last = [], None
    for i in range(WARP_N):
        d = WARP_FROM + i * WARP_STEP
        if last is None:
            corr = 0.0
            for _ in range(30):   # the correction moves the corner: iterate
                x = 2.0 ** ((d + corr) / 1200)
                err = 1200 * math.log2(peak_ratio(x, K * (1 - cut_at(d + corr) / 32768)) * x / 2.0 ** (d / 1200)) - base
                corr -= err
            if d + corr <= WARP_TOP:
                out.append(round(corr))
                continue
            last = (d - WARP_STEP, out[-1])
        d0, c0 = last
        out.append(round(c0 * (-1200 - d) / (-1200 - d0)))
    return out





# kcut: the share of the feedback taken off, Q15, by the corner's distance
# below the rate (after the warp), on warp_tab's grid. The loop
# self-oscillates when k reaches 1 / |z^-1 S^4| at the frequency where its
# phase is -180 degrees: 4.0 well below the rate, 3.44 at Nyquist (the
# stages' zero and the one-sample delay add phase up there). Each entry
# scales k by the threshold there over the threshold at WARP_FROM, so a
# resonance sits as far under (or over) self-oscillation at every corner
# as at the bottom of the range: JX-8P RES 127 (935, 3.93) never
# oscillates, resonance 1000 (4.2) always does. Entries round up (more
# cut) and are checked so the interpolated scale never exceeds the exact
# one between them.
def loop_at(x, f):
    g = 1.0 - math.exp(-2.0 * math.pi * x)
    z1 = complex(math.cos(2 * math.pi * f), -math.sin(2 * math.pi * f))
    return z1 * (g * ((1 - W) + W * z1) / (1 - (1 - g) * z1)) ** 4


def threshold(x):
    """Feedback at which the loop oscillates, for a corner at x = fc / fs."""
    n, prev = 20000, None
    for i in range(1, n + 1):
        f = 0.5 * i / n
        v = loop_at(x, f)
        if prev is not None and v.real < 0 and (prev.imag < 0) != (v.imag < 0):
            a, b = 0.5 * (i - 1) / n, f
            for _ in range(60):
                m = (a + b) / 2
                if (loop_at(x, m).imag < 0) == (loop_at(x, a).imag < 0):
                    a = m
                else:
                    b = m
            return 1.0 / abs(loop_at(x, (a + b) / 2))
        prev = v
    raise ValueError("no -180 degree crossing")


def kcut():
    base = threshold(2.0 ** (WARP_FROM / 1200))
    scale = lambda d: threshold(2.0 ** (min(d, -1200) / 1200)) / base
    out = [math.ceil((1 - scale(WARP_FROM + i * WARP_STEP)) * 32768) for i in range(WARP_N)]
    out[0] = 0
    fine = {}
    for i in range(WARP_N - 1):
        for j in range(1, 30):
            d = WARP_FROM + i * WARP_STEP + WARP_STEP * j / 30
            fine[(i, j)] = scale(d)

    def short(i):   # does the interpolated scale go over the exact one in interval i?
        return any(1 - (out[i] + (out[i + 1] - out[i]) * j / 30) / 32768 > fine[(i, j)] for j in range(1, 30))
    changed = True
    while changed:   # raise the cut at an interval's ends until it covers the curve
        changed = False
        for i in range(WARP_N - 1):
            while short(i):
                out[i + 1] += 1
                if i:
                    out[i] += 1
                changed = True
    return out


KCUT = kcut()


def cut_at(d):
    """kcut_tab at d cents from the rate, interpolated as the engine reads it."""
    if d <= WARP_FROM:
        return 0
    if d >= WARP_FROM + (WARP_N - 1) * WARP_STEP:
        return KCUT[-1]
    i, r = divmod(d - WARP_FROM, WARP_STEP)
    i = int(i)
    return KCUT[i] + (KCUT[i + 1] - KCUT[i]) * r / WARP_STEP


print("static const int16_t warp_tab[%d] = {" % WARP_N)
w = warp()
for i in range(0, WARP_N, 7):
    print("    " + ", ".join("%d" % v for v in w[i:i + 7]) + ("," if i + 7 < WARP_N else ""))
print("};")

print("static const int16_t kcut_tab[%d] = {" % WARP_N)
w = KCUT
for i in range(0, WARP_N, 7):
    print("    " + ", ".join("%d" % v for v in w[i:i + 7]) + ("," if i + 7 < WARP_N else ""))
print("};")
