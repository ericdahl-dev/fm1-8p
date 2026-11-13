#!/usr/bin/env python3
"""The no-sound gate's MIDI files (tools/gate.sh, #140), written from note lists: original material.
usage: python3 tools/gate_midi.py DIR    (writes DIR/chords.mid, unison.mid, vel.mid, ab.mid, controllers.mid)"""
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


def smf(notes, other=()):
    """A format-0 file of (note, start s, end s, velocity) on channel 1, plus `other`: (time s, status,
    data1, data2) channel messages (controllers, pitch bend)."""
    ev = []
    for note, s, e, v in notes:
        ev.append((round(s * TPS), 1, bytes([0x90, note, v])))
        ev.append((round(e * TPS), 0, bytes([0x80, note, 0])))
    for t, st, d1, d2 in other:
        ev.append((round(t * TPS), 2, bytes([st, d1, d2])))
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


def controllers():
    """The sustain pedal holding released keys, then letting go; the mod wheel swept; pitch bend up and
    back; all notes off under the pedal; all sound off."""
    notes = [(60, 0.0, 0.3, 100), (64, 0.4, 0.7, 90), (67, 0.8, 1.1, 80),      # keys up under the pedal
             (48, 2.2, 4.4, 100), (55, 2.2, 4.4, 100),                           # held through the wheel
             (72, 4.6, 6.0, 100), (76, 4.6, 6.0, 100),                           # the pedal holds them
             (60, 6.4, 7.4, 100)]                                                # cut by all sound off
    other = [(0.0, 0xB0, 64, 127), (1.6, 0xB0, 64, 0)]                          # pedal down, up
    other += [(2.4 + 0.1 * i, 0xB0, 1, min(127, 13 * i)) for i in range(11)]    # mod wheel up
    other += [(3.6 + 0.1 * i, 0xE0, 0, 64 + 6 * i) for i in range(10)]          # bend up ...
    other += [(4.5, 0xE0, 0, 64), (4.5, 0xB0, 1, 0)]                            # ... back; wheel off
    other += [(4.6, 0xB0, 64, 127), (5.4, 0xB0, 123, 0), (5.8, 0xB0, 64, 0)]    # all notes off held
    other += [(7.0, 0xB0, 120, 0)]                                              # all sound off
    return notes, other


FILES = {"chords.mid": chords, "unison.mid": unison, "vel.mid": vel, "ab.mid": ab,
         "controllers.mid": controllers}

if __name__ == "__main__":
    d = sys.argv[1]
    os.makedirs(d, exist_ok=True)
    for name, f in FILES.items():
        got = f()
        with open(os.path.join(d, name), "wb") as fh:
            fh.write(smf(*got) if isinstance(got, tuple) else smf(got))
