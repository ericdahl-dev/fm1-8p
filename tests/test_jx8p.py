"""Tests for tools/jx8p.py, the JX-8P factory patch converter (run by `make check-patches`)."""
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import jx8p  # noqa: E402

SYX = os.path.join(ROOT, "data", "jx8p-factory-1-32.syx")


def factory():
    with open(SYX, "rb") as f:
        return jx8p.parse_syx(f.read())


class ParseTest(unittest.TestCase):
    def test_the_factory_dump_holds_32_tones_in_panel_order(self):
        tones = factory()
        self.assertEqual(len(tones), 32)
        self.assertEqual(tones[0].name, "PIANO 1")
        self.assertEqual(tones[16].name, "SOUNDTRACK")
        self.assertEqual(tones[31].name, "PSYCHMELLO")


def patch(name):
    for t in factory():
        if t.name == name:
            return jx8p.to_engine(t)
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

    def test_dco2_fine_tune_is_plus_minus_50_cents_around_64(self):
        self.assertEqual(patch("HI STRINGS")["OSC2_FINE_CENTS"], 15)    # 83
        self.assertEqual(patch("SOUNDTRACK")["OSC2_FINE_CENTS"], -13)   # 48
        self.assertEqual(patch("PIANO 3")["OSC2_FINE_CENTS"], -33)      # 22

    # the engine crossfades (0 osc1 only .. 1000 osc2 only); the JX-8P has a level per DCO
    def test_mixer_levels_become_the_osc_mix(self):
        self.assertEqual(patch("SOUNDTRACK")["OSC_MIX_PERMILLE"], 500)  # 109 and 109
        self.assertEqual(patch("ORGAN 2")["OSC_MIX_PERMILLE"], 0)       # 105 and 0
        self.assertEqual(patch("PIANO 1")["OSC_MIX_PERMILLE"], 486)     # 90 and 85

    # CROSS MOD: off, 1 (sync), 2 (sync and cross mod), 3 (cross mod). The JX-8P's cross mod is
    # amplitude modulation, not the engine's FM (osc_xmod), so it waits in XMOD_AM.
    def test_cross_mod_sets_sync_and_keeps_cross_mod_for_later(self):
        def xm(name):
            e = patch(name)
            return e["OSC_SYNC"], e["XMOD_AM"]
        self.assertEqual(xm("PIANO 1"), (0, 0))      # 0
        self.assertEqual(xm("T O M S"), (1, 0))      # 32
        self.assertEqual(xm("SYNC LEAD"), (1, 1))    # 64
        self.assertEqual(xm("RES BELL"), (0, 1))     # 96

    # one step of VCF FREQ is a semitone (100 cents), from MIDI note 12 (16.4 Hz) at 0
    def test_cutoff_is_a_semitone_per_step(self):
        self.assertEqual(patch("SOUNDTRACK")["FILTER_CUTOFF_CENTS"], 1200 + 100 * 52)
        self.assertEqual(patch("BUBBLES")["FILTER_CUTOFF_CENTS"], 1200 + 100 * 59)

    def test_resonance_and_key_follow_scale_0_127_to_per_mille(self):
        e = patch("PSYCHMELLO")                 # resonance 122, key follow 93
        self.assertEqual(e["FILTER_RESONANCE_PERMILLE"], 961)
        self.assertEqual(e["FILTER_KEY_TRACK_PERMILLE"], 732)
        self.assertEqual(patch("SOUNDTRACK")["FILTER_RESONANCE_PERMILLE"], 0)

    def test_hpf_positions_are_the_engines_four_highpass_steps(self):
        self.assertEqual(patch("SOUNDTRACK")["HIGHPASS_STEP"], 0)   # 0
        self.assertEqual(patch("PIANO 1")["HIGHPASS_STEP"], 1)      # 32
        self.assertEqual(patch("WINDCHIMES")["HIGHPASS_STEP"], 2)   # 64

    # VCA mode ENV-2: ENV-2 is the amp envelope. Times are cubic: attack 1..3000 ms, decay and
    # release 2..10000 ms.
    def test_env2_is_the_amp_envelope(self):
        e = patch("SOUNDTRACK")                 # ENV-2: 80, 109, 57, 77
        self.assertEqual(e["AMP_ATTACK_MS"], 751)
        self.assertEqual(e["AMP_DECAY_MS"], 6323)
        self.assertEqual(e["AMP_SUSTAIN_PERMILLE"], 449)
        self.assertEqual(e["AMP_RELEASE_MS"], 2230)
        self.assertEqual(e["AMP_GATE"], 0)

    # VCA mode GATE (#63): until the engine has it, an amp envelope that is a gate
    def test_gate_mode_is_a_gate_shaped_amp_envelope(self):
        e = patch("E. BASS")                    # VCA mode 0
        self.assertEqual(e["AMP_GATE"], 1)
        self.assertEqual((e["AMP_ATTACK_MS"], e["AMP_SUSTAIN_PERMILLE"], e["AMP_RELEASE_MS"]),
                         (1, 1000, 5))

    # VCF ENV MODE picks the envelope (ENV-1 or ENV-2) and its polarity: the engine's filter
    # envelope takes that envelope's times, the amount its sign. FILTER_ENV_SOURCE keeps the mode
    # for #59 (0 ENV-2 inverted, 1 ENV-2, 2 ENV-1 inverted, 3 ENV-1).
    def test_vcf_env_mode_picks_the_filter_envelope_and_its_sign(self):
        e = patch("SOUNDTRACK")                 # ENV-1 normal; depth 60; ENV-1 127, 83, 54, 91
        self.assertEqual(e["FILTER_ENV_SOURCE"], 3)
        self.assertEqual(e["FILTER_ENV_AMOUNT_CENTS"], 60 * 75)
        self.assertEqual((e["FILTER_ENV_ATTACK_MS"], e["FILTER_ENV_DECAY_MS"],
                          e["FILTER_ENV_SUSTAIN_PERMILLE"]), (3000, 2793, 425))
        e = patch("VOICES")                     # ENV-1 inverted, depth 5
        self.assertEqual(e["FILTER_ENV_SOURCE"], 2)
        self.assertEqual(e["FILTER_ENV_AMOUNT_CENTS"], -5 * 75)
        e = patch("PIANO 1")                    # ENV-2 normal: the amp's times
        self.assertEqual(e["FILTER_ENV_DECAY_MS"], e["AMP_DECAY_MS"])

    # LFO: random, square, triangle (2 positions); rate 0.1..20 Hz exponential; delay cubic to 3 s;
    # depth to pitch quadratic to 1200 cents, the larger of the two DCOs' (the engine has one)
    def test_lfo_shape_rate_delay_and_vibrato_depth(self):
        e = patch("SOUNDTRACK")                 # triangle, delay 78, rate 103, depths 0
        self.assertEqual((e["LFO_WAVE"], e["LFO_RATE_CENTIHZ"], e["LFO_DELAY_MS"]), (0, 735, 695))
        self.assertEqual(e["LFO_PITCH_CENTS"], 0)
        self.assertEqual(patch("CLAV")["LFO_WAVE"], 3)          # 0: random (engine FM_LFO_RANDOM)
        self.assertEqual(patch("ORGAN 2")["LFO_WAVE"], 2)       # 32: square
        self.assertEqual(patch("BUBBLES")["LFO_PITCH_CENTS"], 1200)
        self.assertEqual(patch("CELLO SECT")["LFO_PITCH_CENTS"], 86)   # DCO-1 2, DCO-2 34

    # CHORUS off, 1, 2 -> the engine's off, slow, deep; VCA DYNAMICS off, 1, 2, 3 -> 0..100 %
    def test_chorus_and_vca_dynamics(self):
        e = patch("SOUNDTRACK")                 # chorus 32, VCA dynamics 64
        self.assertEqual((e["CHORUS_MODE"], e["AMP_VELOCITY_PCT"]), (1, 67))
        e = patch("PIANO 2")                    # chorus 0, VCA dynamics 96
        self.assertEqual((e["CHORUS_MODE"], e["AMP_VELOCITY_PCT"]), (0, 100))

    # Kept for the pitch envelope (#58): DCO-1 and DCO-2 ENV depth (quadratic to 2400 cents, signed
    # by DCO ENV MODE), the mode and DCO DYNAMICS
    def test_pitch_envelope_is_kept_for_later(self):
        e = patch("T O M S")                    # DCO-1 ENV 51, ENV-2 normal
        self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (387, 0))
        self.assertEqual(e["PITCH_ENV_SOURCE"], 1)
        e = patch("BUBBLES")                    # both 127, ENV-1 inverted
        self.assertEqual((e["PITCH_ENV_OSC1_CENTS"], e["PITCH_ENV_OSC2_CENTS"]), (-2400, -2400))
        self.assertEqual(e["PITCH_ENV_SOURCE"], 2)
        self.assertEqual(patch("PIANO 3")["PITCH_ENV_VELOCITY_PCT"], 100)   # DCO DYNAMICS 96

    # Kept for DCO-2's envelope level (#33), LFO to cutoff and filter velocity (#8), envelope key
    # follow (#60) and the VCA level
    def test_other_features_the_engine_lacks_are_kept_for_later(self):
        e = patch("SOUNDTRACK")
        self.assertEqual(e["OSC2_ENV_PERMILLE"], 457)       # MIX ENV 58, ENV-2 normal
        self.assertEqual(e["OSC2_ENV_SOURCE"], 1)
        self.assertEqual(e["OSC2_ENV_VELOCITY_PCT"], 33)    # MIX DYNAMICS 32
        self.assertEqual(e["FILTER_LFO_PERMILLE"], 0)
        self.assertEqual(e["FILTER_VELOCITY_PCT"], 33)      # VCF DYNAMICS 32
        self.assertEqual((e["ENV1_KEY_FOLLOW"], e["ENV2_KEY_FOLLOW"]), (0, 1))
        self.assertEqual(e["AMP_LEVEL_PERMILLE"], 819)      # VCA LEVEL 104


class RangeTest(unittest.TestCase):
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

    # firmware/patches.c is checked in: it must be what the converter makes from the dump now
    def test_the_checked_in_table_is_up_to_date(self):
        with open(os.path.join(ROOT, "firmware", "patches.c")) as f:
            self.assertEqual(f.read(), jx8p.generate(SYX),
                             "firmware/patches.c is stale: run python3 tools/jx8p.py")


if __name__ == "__main__":
    unittest.main()
