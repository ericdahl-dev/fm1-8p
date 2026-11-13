#!/usr/bin/env python3
"""The no-sound gate's clips (tools/gate.sh, #140): one line per clip, NAME<TAB>MIDI<TAB>render args.
MIDI is a file name in the gate's MIDI folder. Original material only: no PG-8X renders.
usage: python3 tools/gate_cases.py"""


WAVES = range(4)          # saw, pulse, square, noise (engine/osc.h fm_wave)


def ring_matrix():
    """The ring and sync paths: every osc1 x osc2 wave, sync off and on, three level pairs."""
    for w1 in WAVES:
        for w2 in WAVES:
            for sy in (0, 1):
                for l1, l2 in ((500, 500), (0, 1000), (1000, 300)):
                    yield (f"ring-{w1}{w2}-s{sy}-{l1}-{l2}", "chords.mid",
                           ["-s", "osc_ring=1", "-s", f"osc_sync={sy}", "-s", f"osc1_wave={w1}",
                            "-s", f"osc2_wave={w2}", "-s", "osc1_pulse_width_pct=35",
                            "-s", "osc2_pulse_width_pct=20", "-s", "osc2_coarse_semis=7",
                            "-s", "osc2_fine_cents=13", "-s", f"osc1_level_permille={l1}",
                            "-s", f"osc2_level_permille={l2}", "-s", "amp_attack_ms=20",
                            "-s", "amp_release_ms=400"])


def cases():
    yield "demo", "demo.mid", []
    yield from ring_matrix()


if __name__ == "__main__":
    for name, mid, args in cases():
        print("\t".join([name, mid, " ".join(args)]))
