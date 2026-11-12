#!/usr/bin/env python3
"""Print the output soft clip's table used by engine/engine.c (#24).

After the +6 dB output makeup, a sample past the knee (0.5 of full scale)
by d becomes knee + h * d / (h + d), with h = full scale - knee = knee:
slope 1 at the knee, monotonic, and short of full scale. The engine reads
this curve from a table instead of dividing per sample. Entry i is the
curve at d = i * 2^18 in Q8.24 (1.0 = 2^24), d from 0 to 2.0 in 128
steps; the engine interpolates linearly between entries and holds the last
one past 2.0, so the output never goes above 0.9 of full scale.
"""
KNEE = 1 << 23
STEP_SHIFT = 18
N = 129


def entry(i):
    d = i << STEP_SHIFT
    return round(KNEE * d / (KNEE + d))


def main():
    vals = [entry(i) for i in range(N)]
    print("static const int32_t out_clip[%d] = {" % N)
    for k in range(0, N, 6):
        print("    " + ", ".join("%d" % v for v in vals[k:k + 6]) + ",")
    print("};")


if __name__ == "__main__":
    main()
