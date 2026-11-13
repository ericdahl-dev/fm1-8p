#!/usr/bin/env python3
"""Print the output soft clip's table used by engine/engine.c (#24).

After the +6 dB output makeup, a sample past the knee (0.7202 of full scale,
-2.85 dBFS: #102's +3.17 dB over the old 0.5, so all that was linear stays so)
by d becomes knee + h * d / (h + d), with h = full scale - knee:
slope 1 at the knee, monotonic, and short of full scale. The engine reads
this curve from a table instead of dividing per sample. Entry i is the
curve at d = i * 2^18 in Q8.24 (1.0 = 2^24), d from 0 to 2.0 in 128
steps; the engine interpolates linearly between entries and holds the last
one past 2.0, so the output never goes above 0.966 of full scale.
"""
KNEE = 12083420                              # 0.5 x 10^(3.17 / 20) of 2^24, even
FULL = 1 << 24
STEP_SHIFT = 18
N = 129


def entry(i):
    d = i << STEP_SHIFT
    h = FULL - KNEE
    return round(h * d / (h + d))


def main():
    vals = [entry(i) for i in range(N)]
    print("static const int32_t out_clip[%d] = {" % N)
    for k in range(0, N, 6):
        print("    " + ", ".join("%d" % v for v in vals[k:k + 6]) + ",")
    print("};")


if __name__ == "__main__":
    main()
