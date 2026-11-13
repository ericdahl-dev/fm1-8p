#!/usr/bin/env python3
"""The no-sound gate's clips (tools/gate.sh, #140): one line per clip, NAME<TAB>MIDI<TAB>render args.
MIDI is a file name in the gate's MIDI folder. Original material only: no PG-8X renders.
usage: python3 tools/gate_cases.py"""


WAVES = range(4)          # saw, pulse, square, noise (engine/osc.h fm_wave)


def pair_matrix():
    """The ring and sync paths: every osc1 x osc2 wave, with the ring (X-MOD), sync (SYNC 1) or both
    (SYNC 2), at three level pairs."""
    for w1 in WAVES:
        for w2 in WAVES:
            for ring, sy in ((1, 0), (0, 1), (1, 1)):
                for l1, l2 in ((500, 500), (0, 1000), (1000, 300)):
                    yield (f"pair-{w1}{w2}-r{ring}-s{sy}-{l1}-{l2}", "chords.mid",
                           ["-s", f"osc_ring={ring}", "-s", f"osc_sync={sy}", "-s", f"osc1_wave={w1}",
                            "-s", f"osc2_wave={w2}", "-s", "osc1_pulse_width_pct=35",
                            "-s", "osc2_pulse_width_pct=20", "-s", "osc2_coarse_semis=7",
                            "-s", "osc2_fine_cents=13", "-s", f"osc1_level_permille={l1}",
                            "-s", f"osc2_level_permille={l2}", "-s", "amp_attack_ms=20",
                            "-s", "amp_release_ms=400"])


def hpf_steps():
    """The highpass at each step on a bass line (step 0, off, is in every other clip)."""
    for step in (1, 2, 3):
        yield f"hpf-{step}", "unison.mid", ["-s", f"highpass_step={step}", "-s", "osc2_wave=1",
                                            "-s", "osc2_level_permille=500", "-s", "amp_release_ms=200"]


def s(**kw):
    """-s name=value words, in the order given."""
    return [w for k, v in kw.items() for w in ("-s", f"{k}={v}")]


def filter_env():
    """The filter and its envelope: a resonant bass, a slow sweep, inverted, and from envelope 2."""
    bass = s(osc1_wave=0, osc2_wave=1, osc2_octave=-1, osc1_level_permille=600, osc2_level_permille=400,
             amp_attack_ms=2, amp_decay_ms=400, amp_sustain_permille=600, amp_release_ms=120,
             filter_cutoff_cents=4200, filter_resonance_permille=650, filter_key_track_permille=500,
             filter_env_attack_ms=2, filter_env_decay_ms=220, filter_env_sustain_permille=150,
             filter_env_release_ms=150, filter_env_amount_cents=4800, amp_velocity_pct=60)
    yield "vcf-bass", "unison.mid", bass
    yield "vcf-sweep", "chords.mid", s(osc2_wave=1, osc2_pulse_width_pct=30, osc2_fine_cents=7,
                                       osc1_level_permille=500, osc2_level_permille=500, amp_attack_ms=400,
                                       amp_release_ms=900, filter_cutoff_cents=5400,
                                       filter_resonance_permille=550, filter_key_track_permille=500,
                                       filter_env_attack_ms=2500, filter_env_decay_ms=2500,
                                       filter_env_sustain_permille=300, filter_env_release_ms=900,
                                       filter_env_amount_cents=4200)
    yield "vcf-inverted", "vel.mid", s(filter_cutoff_cents=9600, filter_resonance_permille=400,
                                       filter_env_decay_ms=300, filter_env_sustain_permille=200,
                                       filter_env_amount_cents=-4800)
    yield "vcf-env2", "vel.mid", s(filter_cutoff_cents=4800, filter_env_source=1, amp_decay_ms=600,
                                   amp_sustain_permille=300, filter_env_amount_cents=4800)


def cases():
    yield "demo", "demo.mid", []
    yield from pair_matrix()
    yield from hpf_steps()
    yield from filter_env()


if __name__ == "__main__":
    for name, mid, args in cases():
        print("\t".join([name, mid, " ".join(args)]))
