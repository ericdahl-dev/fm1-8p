"""Tests for tools/jx8p.py, the JX-8P factory patch converter (run by `make check-patches`)."""
import functools
import os
import re
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import jx8p  # noqa: E402

SYX = os.path.join(ROOT, "data", "jx8p-factory-1-32.syx")


@functools.lru_cache(maxsize=None)
def factory():
    with open(SYX, "rb") as f:
        return tuple(jx8p.parse_syx(f.read()))


class ParseTest(unittest.TestCase):
    def test_the_factory_dump_holds_32_tones_in_panel_order(self):
        tones = factory()
        self.assertEqual(len(tones), 32)
        self.assertEqual(tones[0].name, "PIANO 1")
        self.assertEqual(tones[16].name, "SOUNDTRACK")
        self.assertEqual(tones[31].name, "PSYCHMELLO")

    # a cut-off dump: the message that has no F7 is named by file and offset
    def test_a_message_without_f7_names_the_file_and_offset(self):
        data = b"\xf0\x7d\x01\xf7" + b"\xf0\x41\x35\x00\x21"
        with self.assertRaisesRegex(ValueError, r"cut\.syx: the SysEx message at offset 4 has no F7"):
            jx8p.parse_syx(data, "cut.syx")


def patch(name):
    for t in factory():
        if t.name == name:
            return jx8p.to_engine(t)
    raise KeyError(name)


def patch_with(name, **data):
    """The factory tone name with some data bytes changed (by their jx8p constant name)."""
    for t in factory():
        if t.name == name:
            params = bytearray(t.params)
            for k, v in data.items():
                params[getattr(jx8p, k)] = v
            return jx8p.to_engine(jx8p.Tone(t.name, bytes(params)))
    raise KeyError(name)


class MapTest(unittest.TestCase):
    # engine/osc.h fm_wave: 0 saw, 1 pulse, 2 square, 3 noise
    def test_waveform_switch_positions_map_to_engine_waves(self):
        self.assertEqual(patch("POLY BRASS")["OSC1_WAVE"], 0)    # 96: sawtooth
        self.assertEqual(patch("SQUARELEAD")["OSC1_WAVE"], 2)    # 32: square
        self.assertEqual(patch("BUBBLES")["OSC1_WAVE"], 1)       # 64: pulse
        self.assertEqual(patch("T O M S")["OSC2_WAVE"], 3)       # 0: noise

    # the JX-8P's pulse is a fixed narrow pulse; the engine's square is its own wave
    def test_pulse_is_a_quarter_cycle_wide(self):
        e = patch("BUBBLES")                    # DCO-1 pulse, DCO-2 pulse
        self.assertEqual((e["OSC1_PULSE_WIDTH_PCT"], e["OSC2_PULSE_WIDTH_PCT"]), (25, 25))
        e = patch("SOUNDTRACK")                 # saws: the engine's default width
        self.assertEqual((e["OSC1_PULSE_WIDTH_PCT"], e["OSC2_PULSE_WIDTH_PCT"]), (50, 50))

    # range (16' is octave -1, 8' is 0) plus tune: the pitch of each oscillator in semitones
    def test_range_and_tune_set_each_oscillators_pitch(self):
        def pitch(e, n):
            return 12 * e["OSC%d_OCTAVE" % n] + e["OSC%d_COARSE_SEMIS" % n]
        e = patch("FAT FIFTH")                  # DCO1 16' tune 62, DCO2 16' tune 97
        self.assertEqual(pitch(e, 1), -12)
        self.assertEqual(pitch(e, 2) - pitch(e, 1), 7)
        e = patch("LO STRINGS")                 # DCO1 16' tune 66, DCO2 16' tune 60: unison
        self.assertEqual(pitch(e, 2), pitch(e, 1))
        e = patch("SOUNDTRACK")                 # DCO1 8' tune 0, DCO2 8' tune 2: both -12
        self.assertEqual((pitch(e, 1), pitch(e, 2)), (-12, -12))

    # The engine has no osc1 coarse: osc1 plays the octave nearest its pitch that leaves osc2 in
    # range, and osc2 moves by what osc1 lacks, so the interval holds. OSC1_COARSE_SEMIS keeps what
    # osc1 lacks, for when the engine has it.
    def test_dco1_tune_folds_into_osc2_and_keeps_the_interval(self):
        def sounding(e):                        # each oscillator's pitch as the engine plays it
            return 12 * e["OSC1_OCTAVE"], 12 * e["OSC2_OCTAVE"] + e["OSC2_COARSE_SEMIS"]
        e = patch("MUSIC BOX")                  # DCO-1 2' +4, DCO-2 2' +8
        self.assertEqual(sounding(e), (24, 28))
        self.assertEqual(e["OSC1_COARSE_SEMIS"], 4)
        e = patch("WINDCHIMES")                 # DCO-1 2' -3, DCO-2 2' +12: 15 apart
        self.assertEqual(sounding(e), (12, 27))
        self.assertEqual(e["OSC1_COARSE_SEMIS"], 9)

    def test_dco2_fine_tune_is_plus_minus_50_cents_around_64(self):
        self.assertEqual(patch("HI STRINGS")["OSC2_FINE_CENTS"], 15)    # 83
        self.assertEqual(patch("SOUNDTRACK")["OSC2_FINE_CENTS"], -13)   # 48
        self.assertEqual(patch("PIANO 3")["OSC2_FINE_CENTS"], -33)      # 22

    # PG-8X 2.0.13 sums the DCOs, each at its level byte over 127 (measured, #102)
    def test_each_dco_level_is_its_byte_over_127(self):
        e = patch("ORGAN 2")                    # 105 and 0
        self.assertEqual((e["OSC1_LEVEL_PERMILLE"], e["OSC2_LEVEL_PERMILLE"]), (827, 0))
        e = patch("LO STRINGS")                 # 127 and 127, MIX ENV 0
        self.assertEqual((e["OSC1_LEVEL_PERMILLE"], e["OSC2_LEVEL_PERMILLE"]), (1000, 1000))

    # CROSS MOD: off, 1 (SYNC 1), 2 (SYNC 2: sync and the ring), 3 (X-MOD: the ring). The JX-8P's
    # cross mod is a ring (osc_ring, #105), not the engine's FM (osc_xmod), so XMOD_AM sets osc_ring.
    def test_cross_mod_sets_sync_and_the_ring(self):
        def xm(name):
            e = patch(name)
            return e["OSC_SYNC"], e["XMOD_AM"]
        self.assertEqual(xm("PIANO 1"), (0, 0))      # 0
        self.assertEqual(xm("T O M S"), (1, 0))      # 32
        self.assertEqual(xm("SYNC LEAD"), (1, 1))    # 64
        self.assertEqual(xm("RES BELL"), (0, 1))     # 96
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["XMOD_AM"].param, "OSC_RING")

    # VCF FREQ puts the resonant peak where PG-8X 2.0.13's is. Its peak at VCF RES 127, noise in,
    # C4, no key follow, as a MIDI note (filter sweep, 2026-11-11). The engine's peak at resonance
    # 935 (RES 127) sits ENGINE_PEAK_UNDER semitones under its cutoff, the same up the range
    # (test_filter.c, resonance_peak_keeps_its_place_up_the_range). The peak, not the asymptote:
    # PG-8X's knee is sharper than the engine's, so the same 24 dB/oct asymptote reads about a
    # semitone lower on the engine (match.py --param VCF_FREQ judges that one).
    PG8X_PEAK_NOTE = {48: 40.73, 64: 61.22, 80: 81.84, 96: 102.43}
    ENGINE_PEAK_UNDER = 1.96

    def test_cutoff_puts_the_resonant_peak_on_pg8x(self):
        for v, note in self.PG8X_PEAK_NOTE.items():
            got = jx8p.cutoff_cents(v) / 100 - self.ENGINE_PEAK_UNDER
            self.assertLess(abs(got - note), 0.5, (v, round(got, 2), note))

    def test_key_follow_scales_0_127_to_1290_per_mille(self):
        self.assertEqual(patch("PSYCHMELLO")["FILTER_KEY_TRACK_PERMILLE"], 945)   # key follow 93

    # VCF RES follows PG-8X 2.0.13's peak heights (VCF FREQ 64, noise in): +4 dB at 64, +8 at 80,
    # +16 at 96, +24 at 112, +31 at 127 (#98), including PG-8X's hump at 32 (+6.5 dB, more than 56)
    def test_resonance_follows_pg8x_peak_heights(self):
        r = jx8p.resonance_permille
        self.assertEqual([r(v) for v in (0, 64, 80, 96, 112, 127)], [0, 400, 575, 795, 900, 935])
        self.assertGreater(r(32), r(56))
        self.assertEqual(patch("PSYCHMELLO")["FILTER_RESONANCE_PERMILLE"], 928)     # resonance 122
        self.assertEqual(patch("SOUNDTRACK")["FILTER_RESONANCE_PERMILLE"], 0)

    # HPF as PG-8X 2.0.13 plays it (2026-11-10 probe, docs/patches.md): 1 is flat and 2 and 3 are
    # the engine's steps 2 and 3 (within about 2 dB); 0 is a bass boost the engine lacks, so flat.
    def test_hpf_step_per_position(self):
        self.assertEqual([jx8p.hpf_step(v) for v in (0, 32, 64, 96)], [0, 0, 2, 3])
        self.assertEqual([jx8p.hpf_step(v) for v in (31, 63, 95, 127)], [0, 0, 2, 3])

    def test_hpf_positions_are_the_engines_four_highpass_steps(self):
        self.assertEqual(patch("SOUNDTRACK")["HIGHPASS_STEP"], 0)   # 0
        self.assertEqual(patch("PIANO 1")["HIGHPASS_STEP"], 0)      # 32: flat on PG-8X
        self.assertEqual(patch("WINDCHIMES")["HIGHPASS_STEP"], 2)   # 64

    # VCA mode ENV-2: ENV-2 is the amp envelope. Decay and release are cubic,
    # 2..10000 ms.
    def test_env2_is_the_amp_envelope(self):
        e = patch("SOUNDTRACK")                 # ENV-2: 80, 109, 57, 77
        self.assertEqual(e["AMP_ATTACK_MS"], 1884)   # attack_ms(80)
        self.assertEqual(e["AMP_DECAY_MS"], 7986)   # tau (decay_ms)
        self.assertEqual(e["AMP_SUSTAIN_PERMILLE"], 449)
        self.assertEqual(e["AMP_RELEASE_MS"], 1118)   # tau
        self.assertEqual(e["AMP_GATE"], 0)

    # Attack as PG-8X 2.0.13 plays it (2026-11-11): an RC charge toward 1.6 x the peak; the time to
    # the peak (the RC fit's 0.98 tau, |analytic signal| of a held C5 under a 5 ms moving median; the
    # same probe reads ours within 1 % of its setting). Below 16 it falls to a few ms by 8
    PG8X_ATTACK_TO_PEAK_MS = {16: 29, 24: 61, 32: 98, 40: 157, 48: 255, 56: 419, 64: 687, 72: 1129,
                              80: 1854, 88: 3041, 96: 5000, 104: 8218, 112: 13505, 120: 22147, 127: 34190}
    PG8X_LOW_ATTACK_MS = {8: 3, 10: 6, 11: 8, 12: 11, 13: 14, 14: 18, 15: 23}

    def test_attack_follows_pg8x_measured_curve(self):
        self.assertEqual(jx8p.attack_ms(0), 1)                    # at least 1 ms: a ramp, not a click
        self.assertEqual(jx8p.attack_ms(0), patch("E. BASS")["AMP_ATTACK_MS"])   # GATE
        for v, ms in self.PG8X_ATTACK_TO_PEAK_MS.items():
            if v == 127:                                          # the range's end: 32 s
                self.assertEqual(jx8p.attack_ms(v), 32000)
                continue
            tol = 0.04 if v >= 40 else 0.12
            self.assertLess(abs(jx8p.attack_ms(v) / ms - 1), tol, (v, jx8p.attack_ms(v), ms))
        for v, ms in self.PG8X_LOW_ATTACK_MS.items():
            self.assertLessEqual(abs(jx8p.attack_ms(v) - ms), max(1, 0.15 * ms), (v, jx8p.attack_ms(v), ms))
        times = [jx8p.attack_ms(v) for v in range(128)]
        self.assertEqual(times, sorted(times))
        self.assertLessEqual(times[127], 32000)

    # ENV DECAY and RELEASE as PG-8X 2.0.13 plays them (match.py --param ENV2_DECAY / ENV2_REL,
    # 2026-11-10): exponential, so the engine's time is the time constant tau = t_20dB / ln 10
    PG8X_DECAY_TAU_MS = {16: 26, 32: 74, 48: 187, 64: 506, 80: 1333, 96: 3589, 112: 9644, 127: 24468}
    PG8X_RELEASE_TAU_MS = {16: 24, 32: 69, 48: 182, 64: 493, 80: 1335, 96: 3570, 112: 9617}

    # Bytes below 16, sample-accurate (2026-11-11: a C7, |analytic signal|, t_20dB after the peak or
    # note-off / ln 10). PG-8X steps in pairs there; its decay floors near 4 ms, its release does not
    PG8X_LOW_DECAY_TAU_MS = {0: 4.1, 2: 4.1, 4: 4.1, 6: 3.7, 8: 3.5, 10: 4.9, 12: 6.8, 14: 12.0}
    PG8X_LOW_RELEASE_TAU_MS = {0: 0.8, 2: 1.2, 4: 2.4, 6: 2.6, 8: 4.5, 10: 4.9, 12: 8.9, 14: 13.1}

    def test_short_decay_and_release_follow_pg8x_below_16(self):
        for table, f in ((self.PG8X_LOW_DECAY_TAU_MS, jx8p.decay_ms), (self.PG8X_LOW_RELEASE_TAU_MS, jx8p.release_ms)):
            for v, ms in table.items():
                for b in (v, v + 1):                       # PG-8X steps in pairs
                    self.assertLessEqual(abs(f(b) - ms), max(1, 0.15 * ms), (f.__name__, b, f(b), ms))
        e = patch("SOUNDTRACK")                            # ENV-2 release 77: the long curve
        self.assertEqual(e["AMP_RELEASE_MS"], jx8p.release_ms(77))
        times = [jx8p.release_ms(v) for v in range(128)]
        self.assertEqual(times, sorted(times))

    def test_decay_and_release_are_pg8x_time_constants(self):
        for table in (self.PG8X_DECAY_TAU_MS, self.PG8X_RELEASE_TAU_MS):
            for v, ms in table.items():
                self.assertLess(abs(jx8p.decay_ms(v) / ms - 1), 0.08, (v, jx8p.decay_ms(v), ms))
                self.assertLess(abs(jx8p.release_ms(v) / ms - 1), 0.08, (v, jx8p.release_ms(v), ms))
        e = patch("SOUNDTRACK")                 # ENV-2: decay 109, release 77
        self.assertEqual((e["AMP_DECAY_MS"], e["AMP_RELEASE_MS"]), (jx8p.decay_ms(109), jx8p.release_ms(77)))
        times = [jx8p.decay_ms(v) for v in range(128)]
        self.assertEqual(times, sorted(times))
        self.assertLessEqual(times[127], 32000)

    # VCA mode GATE (#63): until the engine has it, an amp envelope that is a gate
    def test_gate_mode_is_a_gate_shaped_amp_envelope(self):
        e = patch("E. BASS")                    # VCA mode 0
        self.assertEqual(e["AMP_GATE"], 1)
        self.assertEqual((e["AMP_ATTACK_MS"], e["AMP_SUSTAIN_PERMILLE"], e["AMP_RELEASE_MS"]),
                         (1, 1000, 5))

    # VCF KEY as PG-8X 2.0.13 plays it (match.py --param VCF_KEY, #95): it pivots at C2 (note 36) and
    # tracks up to 1.29 octaves an octave. The engine pivots at C4, so the converter moves the pivot by
    # adding the track's rise from C2 to C4 to the cutoff. The cutoff a note gets, as the engine plays it
    # (fm_filter_voice_cents, no envelope):
    @staticmethod
    def cutoff_at(e, note):
        return e["FILTER_CUTOFF_CENTS"] + e["FILTER_KEY_TRACK_PERMILLE"] * (note - 60) / 10

    def test_vcf_key_pivots_at_c2_and_tracks_up_to_1_29(self):
        d = bytearray(next(t.params for t in factory() if t.name == "SOUNDTRACK"))
        d[jx8p.VCF_KEY] = 127
        e = jx8p.to_engine(jx8p.Tone("KEY 127", bytes(d)))
        base = jx8p.cutoff_cents(d[jx8p.VCF_FREQ])
        self.assertLess(abs(self.cutoff_at(e, 36) - base), 5)
        self.assertLess(abs(self.cutoff_at(e, 60) - base - 2 * 1.29 * 1200), 5)
        self.assertLess(abs(self.cutoff_at(e, 84) - base - 4 * 1.29 * 1200), 5)

    # VCF ENV depth as PG-8X 2.0.13 plays it (match.py --param VCF_ENV, 2026-11-10): the cutoff's
    # rise at the envelope's peak, in semitones, about 1.25 a step
    PG8X_ENV_SHIFT = {8: 9.86, 16: 19.90, 24: 29.99, 32: 40.28, 40: 51.28, 48: 62.24}

    def test_filter_env_depth_follows_pg8x(self):
        for v, st in self.PG8X_ENV_SHIFT.items():
            got = jx8p.filter_env_cents(v) / 100
            self.assertLess(abs(got / st - 1), 0.05, (v, got, st))

    # VCF ENV MODE picks the envelope (ENV-1 or ENV-2) and its polarity: the cutoff follows the
    # engine envelope running it (below), the amount takes its sign.
    def test_vcf_env_mode_picks_the_filter_envelope_and_its_sign(self):
        e = patch("SOUNDTRACK")                 # ENV-1 normal; depth 60; ENV-1 127, 83, 54, 91
        self.assertEqual(e["FILTER_ENV_SOURCE"], 0)
        self.assertEqual(e["FILTER_ENV_AMOUNT_CENTS"], 60 * 129)
        self.assertEqual((e["FILTER_ENV_ATTACK_MS"], e["FILTER_ENV_DECAY_MS"],
                          e["FILTER_ENV_SUSTAIN_PERMILLE"]), (32000, 1616, 425))   # attack_ms(127), the range's end; decay: tau
        e = patch("VOICES")                     # ENV-1 inverted, depth 5
        self.assertEqual(e["FILTER_ENV_SOURCE"], 0)
        self.assertEqual(e["FILTER_ENV_AMOUNT_CENTS"], -5 * 129)
        e = patch("PIANO 1")                    # ENV-2 normal, VCA on ENV-2: the amp envelope
        self.assertEqual(e["FILTER_ENV_SOURCE"], 1)

    # #59: the cutoff follows the engine envelope that runs the JX-8P one VCF ENV MODE names: the
    # amp envelope (filter_env_source 1) for ENV-2 when the VCA follows ENV-2, else the filter
    # envelope (0). Inverted stays the amount's sign
    def test_vcf_env_mode_sets_the_filter_envelope_source(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["FILTER_ENV_SOURCE"].param, "FILTER_ENV_SOURCE")
        got = {m: patch_with("SOUNDTRACK", VCF_ENV_MODE=m, VCA_MODE=127)["FILTER_ENV_SOURCE"]
               for m in (0, 32, 64, 96)}
        self.assertEqual(got, {0: 1, 32: 1, 64: 0, 96: 0})
        self.assertEqual(patch_with("SOUNDTRACK", VCF_ENV_MODE=32, VCA_MODE=0)["FILTER_ENV_SOURCE"], 0)
        self.assertEqual(patch_with("SOUNDTRACK", VCF_ENV_MODE=0, VCA_MODE=127)["FILTER_ENV_AMOUNT_CENTS"],
                         -60 * 129)

    # LFO: random, square, triangle (2 positions); rate as PG-8X (below); delay cubic to 3 s;
    # depth to pitch linear to 572 cents (below), one per oscillator
    def test_lfo_shape_rate_delay_and_vibrato_depth(self):
        e = patch("SOUNDTRACK")                 # triangle, delay 78, rate 103, depths 0
        self.assertEqual((e["LFO_WAVE"], e["LFO_RATE_CENTIHZ"], e["LFO_DELAY_MS"]), (0, 585, 695))
        self.assertEqual(e["LFO_PITCH_CENTS"], 0)
        self.assertEqual(patch("CLAV")["LFO_WAVE"], 3)          # 0: random (engine FM_LFO_RANDOM)
        self.assertEqual(patch("ORGAN 2")["LFO_WAVE"], 2)       # 32: square
        self.assertEqual(patch("BUBBLES")["LFO_PITCH_CENTS"], 572)
        e = patch("CELLO SECT")                 # DCO-1 2 (a trace), DCO-2 34: mostly DCO-2 wobbles
        self.assertEqual((e["LFO_PITCH_CENTS"], e["LFO_PITCH_OSC2_CENTS"]), (9, 153))

    # DCO-1 LFO as PG-8X 2.0.13 plays it: linear, 4.5 cents per step (127 = 572 cents)
    def test_vibrato_depth_is_linear_as_pg8x_plays_it(self):
        depth = {b: patch_with("SOUNDTRACK", DCO1_LFO=b)["LFO_PITCH_CENTS"] for b in (0, 16, 64, 127)}
        self.assertEqual(depth, {0: 0, 16: 72, 64: 288, 127: 572})
        lo, hi = jx8p.engine_ranges()["LFO_PITCH_CENTS"]
        self.assertTrue(lo <= 0 and 572 <= hi, (lo, hi))

    # DCO-2 LFO the same (agent2, PG-8X 2.0.13: 4.5 cents a step, 572 at 127), on osc 2 alone
    def test_dco2_vibrato_depth_is_linear_on_osc2(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["LFO_PITCH_OSC2_CENTS"].param, "LFO_PITCH_OSC2_CENTS")
        depth = {b: patch_with("SOUNDTRACK", DCO1_LFO=0, DCO2_LFO=b)["LFO_PITCH_OSC2_CENTS"]
                 for b in (0, 16, 64, 127)}
        self.assertEqual(depth, {0: 0, 16: 72, 64: 288, 127: 572})

    # LFO RATE as PG-8X 2.0.13 plays it: 0.537 Hz at 64, x 2.664 every 16 steps, from 32 up
    # (0.076 Hz at 32, 25.4 Hz at 127)
    def test_lfo_rate_follows_pg8x_from_32_up(self):
        rate = {b: patch_with("SOUNDTRACK", LFO_RATE=b)["LFO_RATE_CENTIHZ"] for b in (32, 64, 96, 127)}
        self.assertEqual(rate, {32: 8, 64: 54, 96: 381, 127: 2544})

    # below 32 PG-8X flattens near 0.05 Hz (measured 0.050 Hz at 16): the curve stops at 5 centi-Hz,
    # which it reaches at 25, and stays there down to 0
    def test_lfo_rate_below_32_flattens_at_a_twentieth_of_a_hertz(self):
        rate = {b: patch_with("SOUNDTRACK", LFO_RATE=b)["LFO_RATE_CENTIHZ"] for b in (0, 16, 25, 26, 31)}
        self.assertEqual(rate, {0: 5, 16: 5, 25: 5, 26: 5, 31: 7})
        lo, hi = jx8p.engine_ranges()["LFO_RATE_CENTIHZ"]
        self.assertTrue(lo <= 5 and 2544 <= hi, (lo, hi))

    # CHORUS off, 1, 2 -> the engine's off, slow, deep; VCA DYNAMICS off, 1, 2, 3 -> 0..100 %
    def test_chorus_and_vca_dynamics(self):
        e = patch("SOUNDTRACK")                 # chorus 32, VCA dynamics 64
        self.assertEqual((e["CHORUS_MODE"], e["AMP_VELOCITY_PCT"]), (1, 67))
        e = patch("PIANO 2")                    # chorus 0, VCA dynamics 96
        self.assertEqual((e["CHORUS_MODE"], e["AMP_VELOCITY_PCT"]), (0, 100))

    # DCO-1 and DCO-2 ENV as PG-8X 2.0.13 plays them (#58): at full envelope 24.8 cents a step
    # less 13 (0 at 0, 3137 at 127), linear, each moving its own oscillator, signed by DCO ENV MODE
    def test_dco_env_depth_is_linear_as_pg8x_plays_it(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        for n in (1, 2):
            self.assertEqual(fields["PITCH_ENV_OSC%d_CENTS" % n].param, "PITCH_ENV_OSC%d_CENTS" % n)
        for b, want in ((0, 0), (1, 12), (64, 1574), (127, 3137)):
            e = patch_with("SOUNDTRACK", DCO1_ENV=b, DCO2_ENV=0, DCO_ENV_MODE=96)
            self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (want, 0))
            e = patch_with("SOUNDTRACK", DCO1_ENV=0, DCO2_ENV=b, DCO_ENV_MODE=64)
            self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (0, -want))
        lo, hi = jx8p.engine_ranges()["PITCH_ENV_OSC1_CENTS"]
        self.assertTrue(lo <= -3137 and 3137 <= hi, (lo, hi))

    # DCO ENV MODE: 0 ENV-2 inverted, 32 ENV-2, 64 ENV-1 inverted, 96 ENV-1 (inverted the exact
    # mirror). Like MIX ENV it follows the engine envelope that runs the JX-8P one it names: amp
    # for ENV-2 when the VCA follows ENV-2, else filter
    def test_dco_env_mode_sets_the_source_and_the_sign(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["PITCH_ENV_SOURCE"].param, "PITCH_ENV_SOURCE")
        got = {}
        for mode in (0, 32, 64, 96):
            e = patch_with("SOUNDTRACK", DCO1_ENV=64, DCO_ENV_MODE=mode, VCA_MODE=127)
            got[mode] = (e["PITCH_ENV_SOURCE"], e["PITCH_ENV_OSC1_CENTS"])
        self.assertEqual(got, {0: (1, -1574), 32: (1, 1574), 64: (0, -1574), 96: (0, 1574)})
        e = patch_with("SOUNDTRACK", DCO1_ENV=64, DCO_ENV_MODE=32, VCA_MODE=0)    # VCA on GATE
        self.assertEqual(e["PITCH_ENV_SOURCE"], 0)

    # DCO DYNAMICS off, 1, 2, 3 -> the engine's pitch_env_dyna position, which scales the depth
    # by the curve PG-8X shares across its DYNAMICS (engine/dyna.c)
    def test_dco_dynamics_is_the_dyna_position(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["PITCH_ENV_DYNA"].param, "PITCH_ENV_DYNA")
        got = {b: patch_with("SOUNDTRACK", DCO_DYNA=b)["PITCH_ENV_DYNA"] for b in (0, 32, 64, 96, 127)}
        self.assertEqual(got, {0: 0, 32: 1, 64: 2, 96: 3, 127: 3})

    # VCF DYNAMICS off, 1, 2, 3 -> the engine's filter_env_dyna position: the same shared curve
    # (engine/dyna.c) on the filter envelope's cutoff depth (#8)
    def test_vcf_dynamics_is_the_dyna_position(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["FILTER_ENV_DYNA"].param, "FILTER_ENV_DYNA")
        got = {b: patch_with("SOUNDTRACK", VCF_DYNA=b)["FILTER_ENV_DYNA"] for b in (0, 32, 64, 96, 127)}
        self.assertEqual(got, {0: 0, 32: 1, 64: 2, 96: 3, 127: 3})

    # The pitch envelope (#58) on factory tones: DCO-1 and DCO-2 ENV depth (above), the mode and
    # DCO DYNAMICS
    def test_pitch_envelope_is_kept_for_later(self):
        e = patch("T O M S")                    # DCO-1 ENV 51, ENV-2 normal
        self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (1252, 0))
        self.assertEqual(e["PITCH_ENV_SOURCE"], 1)            # the amp envelope (VCA on ENV-2)
        e = patch("BUBBLES")                    # both 127, ENV-1 inverted
        self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (-3137, -3137))
        self.assertEqual(e["PITCH_ENV_SOURCE"], 0)
        self.assertEqual(patch("PIANO 3")["PITCH_ENV_DYNA"], 3)             # DCO DYNAMICS 96

    # MIX ENV (#33): PG-8X 2.0.13 adds depth x envelope to DCO-2's level byte, clamped to 0..127
    # (measured, #102), so osc_mix_env is the depth over 127, added to osc2's level; inverted, taken
    # away. It follows the engine envelope that runs the JX-8P one it names: amp for ENV-2 when the
    # VCA follows ENV-2, else filter
    def test_mix_env_adds_to_dco2_level(self):
        fields = {f.name: f for f in jx8p.FIELDS}
        self.assertEqual(fields["OSC_MIX_ENV_PERMILLE"].param, "OSC_MIX_ENV")
        self.assertEqual(fields["OSC_MIX_ENV_SOURCE"].param, "OSC_MIX_ENV_SOURCE")
        e = patch("SOUNDTRACK")                 # 109 and 109, MIX ENV 58, ENV-2 normal
        self.assertEqual((e["OSC2_LEVEL_PERMILLE"], e["OSC_MIX_ENV_PERMILLE"]), (858, 457))
        self.assertEqual(e["OSC_MIX_ENV_SOURCE"], 1)
        e = patch("PIANO 1")                    # 90 and 85, MIX ENV 127, ENV-1 normal
        self.assertEqual((e["OSC2_LEVEL_PERMILLE"], e["OSC_MIX_ENV_PERMILLE"]), (669, 1000))
        self.assertEqual(e["OSC_MIX_ENV_SOURCE"], 0)
        e = patch("VOICES")                     # 123 and 127, MIX ENV 71, ENV-1 inverted
        self.assertEqual((e["OSC2_LEVEL_PERMILLE"], e["OSC_MIX_ENV_PERMILLE"]), (1000, -559))
        self.assertEqual(patch("ORGAN 2")["OSC_MIX_ENV_SOURCE"], 0)         # ENV-2, VCA on GATE

    # MIX ENV MODE names ENV-1 but VCF ENV MODE names ENV-2 (PIANO 1, PIANO 3, TIMBALES, WINDCHIMES,
    # E. BASS, SOFT BRASS, CHURCHBELL): the mix follows the filter envelope, which runs ENV-1's
    # times (#59), and the cutoff the amp envelope. E. BASS (VCA on GATE) stays an approximation.
    def test_mix_env_on_env1_while_the_filter_runs_env2_takes_the_filter_envelope(self):
        e = patch("PIANO 1")                    # MIX ENV MODE ENV-1, VCF ENV MODE ENV-2
        self.assertEqual(e["OSC_MIX_ENV_SOURCE"], 0)
        self.assertEqual(e["FILTER_ENV_SOURCE"], 1)

    # After #59 the engine's envelope 1 (the filter envelope) runs ENV-1's own times, so PIANO 1's
    # MIX ENV (ENV-1, while its VCF names ENV-2 and follows the amp envelope) gets its real
    # envelope. Only with the VCA on GATE, where ENV-2 runs nowhere, does envelope 1 run ENV-2's
    # times for a VCF that names it (E. BASS)
    def test_envelope_1_runs_env1s_own_times(self):
        def times(e):
            return tuple(e["FILTER_ENV_%s" % k] for k in ("ATTACK_MS", "DECAY_MS", "SUSTAIN_PERMILLE",
                                                         "RELEASE_MS"))
        for name in ("PIANO 1", "STAB BRASS"):
            t = next(t for t in factory() if t.name == name)
            e = jx8p.to_engine(t)
            self.assertEqual(times(e), jx8p.env(t.params, jx8p.ENV1), name)
        e = patch("PIANO 1")
        self.assertEqual((e["OSC_MIX_ENV_SOURCE"], e["FILTER_ENV_SOURCE"]), (0, 1))
        t = next(t for t in factory() if t.name == "E. BASS")    # VCA on GATE, VCF ENV-2
        self.assertEqual(times(jx8p.to_engine(t)), jx8p.env(t.params, jx8p.ENV2))

    # DCO-1 at 0 is osc1 off: only DCO-2 sounds, at its level plus MIX ENV, so with the envelope
    # at rest and DCO-2's byte 0 the tone is silent, as on PG-8X
    def test_with_dco1_off_osc1_is_silent_and_dco2_keeps_its_envelope(self):
        for name in ("TIMBALES", "MUSIC BOX", "WINDCHIMES", "SYNC LEAD"):
            e = patch(name)                     # DCO-1 0, MIX ENV 127
            self.assertEqual(e["OSC1_LEVEL_PERMILLE"], 0, name)
            self.assertEqual(abs(e["OSC_MIX_ENV_PERMILLE"]), 1000, name)

    # Kept for LFO to cutoff (#8), envelope key follow (#60) and the VCA level
    def test_other_features_the_engine_lacks_are_kept_for_later(self):
        e = patch("SOUNDTRACK")
        self.assertEqual(e["OSC_MIX_ENV_VELOCITY_PCT"], 33)    # MIX DYNAMICS 32
        self.assertEqual(e["FILTER_LFO_PERMILLE"], 0)
        self.assertEqual((e["ENV1_KEY_FOLLOW"], e["ENV2_KEY_FOLLOW"]), (0, 1))
        self.assertEqual(e["AMP_LEVEL_PERMILLE"], 819)      # VCA LEVEL 104


class RangeTest(unittest.TestCase):
    # the ranges of the fields the engine has come from engine/params.c, not copies of it
    def test_engine_ranges_are_read_from_params_c(self):
        ranges = jx8p.engine_ranges()
        self.assertEqual(ranges["OSC1_WAVE"], (0, 3))                       # FM_WAVE_COUNT - 1
        self.assertEqual(ranges["FILTER_CUTOFF_CENTS"], (0, 14400))         # FM_FILTER_OPEN_CENTS
        self.assertEqual(ranges["HIGHPASS_STEP"], (0, 3))
        self.assertEqual(ranges["OSC_MIX_ENV"], (-1000, 1000))
        for f in jx8p.FIELDS:
            if f.param:
                self.assertEqual((f.lo, f.hi), ranges[f.param], f.name)

    # every tone sets every field, in the field's range (the engine's range from engine/params.c
    # for the fields it has)
    def test_every_field_of_every_tone_is_in_range(self):
        names = [f.name for f in jx8p.FIELDS]
        for t in factory():
            e = jx8p.to_engine(t)
            self.assertEqual(sorted(e), sorted(names), t.name)
            for f in jx8p.FIELDS:
                self.assertTrue(f.lo <= e[f.name] <= f.hi, (t.name, f.name, e[f.name]))
                self.assertTrue(-32768 <= e[f.name] <= 32767)    # int16_t in the table


class TableTest(unittest.TestCase):
    # the generated C table reads back as the same names and values
    def test_the_c_table_round_trips(self):
        tones = factory()
        back = jx8p.read_c(jx8p.emit_c(tones))
        self.assertEqual(len(back), 32)
        for t, (name, values) in zip(tones, back):
            self.assertEqual(name, t.name)
            self.assertEqual(values, jx8p.to_engine(t))

    # firmware/patches.h's patch_field enum lists the converter's fields, in order
    def test_the_header_lists_every_field(self):
        with open(os.path.join(ROOT, "firmware", "patches.h")) as f:
            text = f.read()
        body = text[text.index("typedef enum {"):text.index("} patch_field;")]
        names = re.findall(r"^\s*PATCH_(\w+),", body, re.M)
        self.assertEqual(names, [f.name for f in jx8p.FIELDS] + ["FIELD_COUNT"])

    # a dump the converter refuses leaves the output as it was (generated first, then written)
    def test_a_bad_dump_leaves_the_output_untouched(self):
        with tempfile.TemporaryDirectory() as d:
            bad, out = os.path.join(d, "bad.syx"), os.path.join(d, "patches.c")
            with open(bad, "wb") as f:
                f.write(b"")                    # no tones
            with open(out, "w") as f:
                f.write("before")
            with self.assertRaises(ValueError):
                jx8p.main(["jx8p.py", bad, out])
            with open(out) as f:
                self.assertEqual(f.read(), "before")

    # firmware/patches.c is checked in: it must be what the converter makes from the dump now
    def test_the_checked_in_table_is_up_to_date(self):
        with open(os.path.join(ROOT, "firmware", "patches.c")) as f:
            self.assertEqual(f.read(), jx8p.generate(SYX),
                             "firmware/patches.c is stale: run python3 tools/jx8p.py")


if __name__ == "__main__":
    unittest.main()
