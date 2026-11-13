#!/usr/bin/env python3
"""The no-sound gate's MIDI files (tools/gate.sh, #140), written from note lists: original material.
usage: python3 tools/gate_midi.py DIR       (writes DIR/chords.mid, unison.mid, vel.mid, ab.mid)"""
import os
import struct
import sys

TPS = 960  # ticks per second: 480 a beat at the default 120 bpm


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, 0x80 | (n & 0x7F))
        n >>= 7
    return bytes(out)


def smf(notes):
    """A format-0 file of (note, start s, end s, velocity) on channel 1."""
    ev = []
    for note, s, e, v in notes:
        ev.append((round(s * TPS), 1, bytes([0x90, note, v])))
        ev.append((round(e * TPS), 0, bytes([0x80, note, 0])))
    ev.sort(key=lambda x: (x[0], x[1]))
    body, now = b"", 0
    for t, _, msg in ev:
        body += vlq(t - now) + msg
        now = t
    body += b"\x00\xff\x2f\x00"
    return (b"MThd" + struct.pack(">IHHH", 6, 0, 1, TPS // 2) +
            b"MTrk" + struct.pack(">I", len(body)) + body)


def chords():
    """Four five-note chords twice, then an arpeggio."""
    shapes = [(48, 55, 60, 64, 67), (45, 52, 57, 60, 64), (41, 48, 53, 57, 60), (43, 50, 55, 59, 62)]
    out = [(n, k, k + 0.9375, 100) for k in range(8) for n in shapes[k % 4]]
    arp = [60, 64, 67, 72, 76, 72, 67, 64] * 2
    return out + [(n, 8 + 0.25 * i, 8 + 0.25 * i + 0.208, 100) for i, n in enumerate(arp[:16])]


def unison():
    """A bass line, then a high line: single notes, held and short."""
    bass = [(36, .458), (36, .208), (43, .208), (41, .458), (39, .208), (43, .208), (36, .458), (48, .458)]
    out, t = [], 0.0
    for n, d in bass * 2:
        out.append((n, t, t + d, 100))
        t += 0.25 if d < 0.3 else 0.5
    for i, n in enumerate([72, 74, 75, 77, 79, 77, 75, 74, 72]):
        out.append((n, 6 + 0.5 * i, 6 + 0.5 * i + 0.479, 100))
    return out


def vel():
    """Four notes up at rising velocity, then down at falling: 20 .. 127 .. 20."""
    vs = [20, 35, 50, 65, 80, 95, 110, 127]
    ns = [60, 64, 67, 72]
    seq = [(ns[i % 4], v) for i, v in enumerate(vs)] + [(ns[i % 4], v) for i, v in enumerate(reversed(vs))]
    return [(n, 0.25 * i, 0.25 * i + 0.229, v) for i, (n, v) in enumerate(seq)]


def ab():
    """A held C major triad, then a phrase at velocities 110, 60, 90, 40, then a low C held."""
    return [(60, 0, 3, 100), (64, 0, 3, 100), (67, 0, 3, 100), (67, 6.0, 6.4, 110), (72, 6.5, 6.9, 60),
            (71, 7.0, 7.3, 90), (69, 7.4, 8.0, 40), (36, 8.3, 10.0, 100)]


FILES = {"chords.mid": chords, "unison.mid": unison, "vel.mid": vel, "ab.mid": ab}

if __name__ == "__main__":
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    for name, f in FILES.items():
        with open(os.path.join(d, name), "wb") as fh:
            fh.write(smf(f()))
