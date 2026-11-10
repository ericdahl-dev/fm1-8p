#!/usr/bin/env python3
"""Write examples/demo.mid, the sample file CI renders.

A format-1 Standard MIDI File: a tempo track (120 BPM, then 90 BPM) and
a monophonic phrase that runs from low notes up to high ones, with
held and short notes so the envelope release is audible. Original
material, written for this project.
"""
import struct
import sys


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


def track(events):
    body = b"".join(vlq(d) + e for d, e in events) + b"\x00\xff\x2f\x00"
    return b"MTrk" + struct.pack(">I", len(body)) + body


def tempo(us):
    return b"\xff\x51\x03" + us.to_bytes(3, "big")


Q = 480
tempo_track = track([(0, tempo(500000)), (Q * 16, tempo(666667))])

# (note, length in ticks, gap after in ticks)
phrase = [
    (45, Q, 0), (52, Q // 2, 0), (57, Q // 2, 0), (60, Q, Q // 2),
    (64, Q // 2, 0), (67, Q // 2, 0), (69, Q * 2, Q // 2),
    (72, Q // 4, Q // 4), (76, Q // 4, Q // 4), (79, Q // 4, Q // 4),
    (84, Q // 4, Q // 4), (88, Q, Q), (96, Q * 2, 0),
    (57, Q * 3, 0),
]
ev, wait = [], 0
for note, length, gap in phrase:
    ev.append((wait, bytes([0x90, note, 100])))
    ev.append((length, bytes([0x80, note, 64])))
    wait = gap
note_track = track(ev)

data = b"MThd" + struct.pack(">IHHH", 6, 1, 2, Q) + tempo_track + note_track
out = sys.argv[1] if len(sys.argv) > 1 else "examples/demo.mid"
with open(out, "wb") as f:
    f.write(data)
