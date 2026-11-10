#!/usr/bin/env python3
"""Print the top-octave pitch table used by engine/osc.c.

Each entry is the frequency of MIDI notes 120..131 in Q16.16 Hz,
440 * 2^((n - 69) / 12) * 65536, rounded. Lower notes are exact right
shifts of these, so the engine never needs pow() or floating point.
"""
for i, n in enumerate(range(120, 132)):
    hz = 440.0 * 2.0 ** ((n - 69) / 12.0)
    end = ",\n" if i % 4 == 3 else ", "
    print(("    " if i % 4 == 0 else "") + "%du" % round(hz * 65536), end=end)
