#!/usr/bin/env python3
"""The no-sound gate's clips (tools/gate.sh, #140): one line per clip, NAME<TAB>MIDI<TAB>render args.
MIDI is a file name in the gate's MIDI folder. Original material only: no PG-8X renders.
usage: python3 tools/gate_cases.py TREE   (TREE's own converter and params give the factory tones' args)"""
import os
import re
import sys


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


def xmod():
    """FM cross-mod, sample by sample: alone, synced, and with two noises (also sample by sample)."""
    x = s(osc2_wave=1, osc2_pulse_width_pct=30, osc2_fine_cents=7, osc1_level_permille=500,
          osc2_level_permille=500, amp_attack_ms=20, amp_release_ms=600)
    yield "xmod", "chords.mid", x + s(osc_xmod_permille=300, chorus_mode=1)
    yield "xmod-sync", "chords.mid", x + s(osc_xmod_permille=700, osc_sync=1)
    yield "noise-noise", "vel.mid", s(osc1_wave=3, osc2_wave=3, osc2_level_permille=500)


def pwm():
    """Pulse-width modulation from the LFO, on a pulse and a pulse pair with chorus."""
    yield "pwm", "chords.mid", s(osc1_wave=1, osc1_pulse_width_pct=50, lfo_rate_centihz=60, lfo_pw_pct=40,
                                 amp_attack_ms=20, amp_release_ms=600, chorus_mode=1)
    yield "pwm-pair", "unison.mid", s(osc1_wave=1, osc2_wave=1, osc1_pulse_width_pct=30,
                                      osc2_pulse_width_pct=70, osc2_level_permille=600,
                                      lfo_rate_centihz=420, lfo_pw_pct=25, lfo_wave=1)


def controllers():
    """Sustain pedal, mod wheel, pitch bend, all notes off and all sound off, with vibrato and a
    filter envelope so each one shows."""
    yield "controllers", "controllers.mid", s(lfo_rate_centihz=550, lfo_pitch_cents=30, amp_release_ms=400,
                                              filter_cutoff_cents=6000, filter_env_amount_cents=3600,
                                              filter_env_decay_ms=500, chorus_mode=1)
    yield "controllers-unison", "controllers.mid", s(voice_mode=1, unison_detune_cents=20, amp_release_ms=300,
                                                     osc_sync=1, osc2_coarse_semis=5, chorus_mode=2)


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


def unison():
    """Unison: all voices on one note, detuned; plain, with sync and vibrato, and a fifth with chorus."""
    u = s(voice_mode=1, unison_detune_cents=25, amp_attack_ms=5, amp_release_ms=250)
    yield "unison", "unison.mid", u
    yield "unison-sync", "unison.mid", u + s(osc_sync=1, osc2_octave=1, osc2_fine_cents=70,
                                             osc1_level_permille=200, osc2_level_permille=800,
                                             lfo_rate_centihz=550, lfo_pitch_cents=25, lfo_delay_ms=300,
                                             chorus_mode=2)
    yield "unison-fifth", "unison.mid", u + s(osc2_coarse_semis=7, osc2_fine_cents=4,
                                              osc1_level_permille=550, osc2_level_permille=450,
                                              filter_cutoff_cents=9000, filter_resonance_permille=300,
                                              filter_env_amount_cents=2400, chorus_mode=1)


def factory(tree):
    """The 32 JX-8P factory tones (data/jx8p-factory-1-32.syx) as TREE's converter maps them, on a held
    chord, a velocity phrase and a low note (ab.mid), 6 voices at 48 kHz."""
    sys.path.insert(0, os.path.join(tree, "tools"))
    import jx8p
    names = getattr(jx8p, "PARAM_NAMES", None)   # the param list (#196)
    if names is None:                       # a tree older than the list: its params.c's table
        with open(os.path.join(tree, "engine", "params.c")) as fh:
            names = dict(re.findall(r'\{\s*FM_PARAM_(\w+)\s*,\s*"([^"]+)"', fh.read()))
    with open(os.path.join(tree, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
        tones = jx8p.parse_syx(fh.read())
    for i, t in enumerate(tones, 1):
        e = jx8p.to_engine(t)
        args = ["-r", "48000", "-t", "3000", "-v", "6"]
        for f in jx8p.FIELDS:
            if f.param:
                args += ["-s", f"{names[f.param]}={e[f.name]}"]
        yield f"tone-{i:02d}", "ab.mid", args


def cases(tree):
    yield "demo", "demo.mid", []
    yield from pair_matrix()
    yield from xmod()
    yield from pwm()
    yield from controllers()
    yield from hpf_steps()
    yield from filter_env()
    yield from unison()
    yield from factory(tree)


if __name__ == "__main__":
    for name, mid, args in cases(sys.argv[1]):
        print("\t".join([name, mid, " ".join(args)]))
