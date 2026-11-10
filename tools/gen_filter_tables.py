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
