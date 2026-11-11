#!/usr/bin/env python3
"""Print the DYNAMICS (velocity) table used by engine/dyna.c.

PG-8X 2.0.13 scales every velocity-sensitive depth (DCO pitch env, MIX env,
VCF env, VCA gain) by one shared curve per DYNAMICS position. Measured on
every velocity 1..127 (agent2, #8): each position is piecewise linear, and
these are the fitted segments, share = slope * v + offset, clamped to 0..1.
Each entry is the share in Q15 (32768 is full depth), rounded. Velocity 0
(a note-on never has it) repeats velocity 1.

Position 1 drops at v98 (0.749 at v97, 0.617 at v98): that is PG-8X's
own curve (a PG-8X or JX-8P quirk), kept exactly as measured.
"""
SEGMENTS = {
    1: [(1, 97, 0.00390, 0.3710), (98, 127, 0.00791, -0.1585)],
    2: [(1, 127, 0.00786, -0.0115)],
    3: [(1, 63, 0.00353, 0.0212), (64, 111, 0.00791, -0.2570), (112, 127, 0.02259, -1.8957)],
}


def share(pos, v):
    v = max(v, 1)
    for a, b, k, c in SEGMENTS[pos]:
        if a <= v <= b:
            return min(max(k * v + c, 0.0), 1.0)
    raise ValueError(v)


def q15(pos, v):
    return round(share(pos, v) * 32768)


if __name__ == "__main__":
    import sys
    rows = [int(a) for a in sys.argv[1:]] or [1, 2, 3]
    for pos in rows:
        print("    { /* DYNAMICS %d */" % pos)
        for i in range(128):
            end = ",\n" if i % 8 == 7 else ", "
            print(("        " if i % 8 == 0 else "") + "%5d" % q15(pos, i), end=end)
        print("    },")
