"""Tests for tools/scorecard.py, the factory-tone scorecard (fm1-8p against PG-8X 2.0.13).
Needs numpy and scipy; skipped where they aren't installed (make check-scorecard runs it with
SCORECARD_PYTHON, a Python that has them)."""
import importlib.util
import json
import os
import sys
import unittest

HAVE_NUMPY = all(importlib.util.find_spec(m) for m in ("numpy", "scipy"))
# CI sets SCORECARD_REQUIRE_NUMPY=1 and installs both (firmware/pins.env): there a missing one is a failure, not
# 59 skipped tests that look green (#404: a broken test merged that way)
if os.environ.get("SCORECARD_REQUIRE_NUMPY") == "1" and not HAVE_NUMPY:
    raise ImportError("tests/test_scorecard.py: SCORECARD_REQUIRE_NUMPY=1, but numpy or scipy is missing")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))

SR = 48000


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class MetricTest(unittest.TestCase):
    def setUp(self):
        import numpy as np
        import scorecard
        self.np, self.sc = np, scorecard
        t = np.arange(4 * SR) / SR
        self.tone = 0.25 * np.sin(2 * np.pi * 440 * t) + 0.1 * np.sin(2 * np.pi * 1320 * t)

    def test_loudness_gap_is_ours_minus_pg_in_lu(self):
        gap = self.sc.loudness_gap(self.tone * 0.5, self.tone)
        self.assertAlmostEqual(gap, -6.02, places=1)

    # The #8 clips' method: RMS dB difference of 1/3-octave bands, 60 Hz .. 8 kHz, level-matched.
    def test_spectral_distance_ignores_level_and_sees_tone(self):
        from scipy import signal
        self.assertAlmostEqual(self.sc.spectral_distance(self.tone * 0.3, self.tone), 0.0, places=6)
        b, a = signal.butter(2, 600 / (SR / 2))
        darker = signal.lfilter(b, a, self.tone + 0.05 * self.np.random.default_rng(1).standard_normal(len(self.tone)))
        self.assertGreater(self.sc.spectral_distance(darker, self.tone + 0.05 * self.np.random.default_rng(1).standard_normal(len(self.tone))), 3.0)

    # Envelope timing, sample-accurate (pg8x-measurement traps): the Hilbert magnitude, smoothed over two
    # periods of the note, so neither the waveform's ripple nor PG-8X's onset burst moves the reading.
    def saw_note(self, f0, burst_db, decay_s, off_s, release_s, total_s=2.5):
        np = self.np
        t = np.arange(int(total_s * SR)) / SR
        saw = 2 * ((f0 * t) % 1.0) - 1
        g = np.where(t < 0.01, 10 ** (burst_db / 20), 1.0)
        g = g * np.where(t < off_s, 10 ** (-20 * t / decay_s / 20) if decay_s else 1.0, 1.0)
        level_off = 10 ** (-20 * off_s / decay_s / 20) if decay_s else 1.0
        g = np.where(t >= off_s, level_off * 10 ** (-40 * (t - off_s) / release_s / 20), g)
        return 0.3 * saw * g

    def test_decay_reads_past_an_onset_burst(self):
        x = self.saw_note(261.63, 6.0, 0.3, 2.0, 0.12)
        self.assertAlmostEqual(self.sc.decay_ms(x, 0.0, 2.0, 261.63, 20), 300, delta=5)
        self.assertIsNone(self.sc.decay_ms(x, 0.0, 0.2, 261.63, 20))   # not 20 dB down inside the window

    def test_release_reads_a_c4_saw_without_frame_ripple(self):
        x = self.saw_note(261.63, 6.0, 0, 1.0, 0.12)
        self.assertAlmostEqual(self.sc.release_ms(x, 1.0, 2.0, 261.63, 40), 120, delta=3)

    # Pitch: the shift that best lines up the two log-frequency spectra, cents (+ = ours sharp).
    def test_pitch_offset_reads_cents(self):
        np = self.np
        t = np.arange(2 * SR) / SR
        saw = lambda hz: sum(np.sin(2 * np.pi * hz * k * t) / k for k in range(1, 12))
        self.assertAlmostEqual(self.sc.pitch_cents(saw(220 * 2 ** (25 / 1200)), saw(220)), 25, delta=5)
        self.assertAlmostEqual(self.sc.pitch_cents(saw(220), saw(220)), 0, delta=1)

    # Air: energy above 6.3 kHz as a share of the whole, dB, ours minus PG-8X, so a quiet buzz far above the
    # body of the sound shows even when the band-by-band distance weighs it like any other band.
    def test_air_gap_reads_added_high_hiss(self):
        np = self.np
        from scipy import signal
        b, a = signal.butter(4, 8000 / (SR / 2), "high")
        hiss = signal.lfilter(b, a, np.random.default_rng(3).standard_normal(len(self.tone))) * 0.01
        gap = self.sc.air_gap(self.tone + hiss, self.tone + hiss * 0.1)
        self.assertAlmostEqual(gap, 20.0, delta=1.0)
        self.assertAlmostEqual(self.sc.air_gap(self.tone, self.tone), 0.0, places=6)

    # Our renders are int16, PG-8X's float: a band far below the sound must not read the 16-bit floor
    # against PG-8X's silence. Bands (and the air share) are floored 60 dB under the section's loudest
    # band on both sides; a buzz above that still shows.
    def test_a_float_saw_against_itself_in_int16_scores_zero(self):
        np = self.np
        t = np.arange(2 * SR) / SR
        saw = 0.05 * (2 * ((261.63 * t) % 1.0) - 1)
        as16 = np.round(saw * 32767) / 32767
        self.assertLess(self.sc.spectral_distance(as16, saw), 0.5)
        self.assertLess(abs(self.sc.air_gap(as16, saw)), 0.5)

    def test_the_16_bit_floor_is_not_a_difference(self):
        np = self.np
        from scipy import signal
        b, a = signal.butter(8, 300 / (SR / 2))
        dark = signal.lfilter(b, a, self.tone)                  # little above 1 kHz ...
        dark *= 10 ** (-30 / 20) / np.sqrt(np.mean(dark ** 2))  # ... at -30 dBFS, as the sections are
        as16 = np.round(dark * 32767) / 32767
        self.assertLess(self.sc.spectral_distance(as16, dark), 0.5)
        self.assertLess(abs(self.sc.air_gap(as16, dark)), 0.5)
        hb, ha = signal.butter(4, 5000 / (SR / 2), "high")
        buzz = signal.lfilter(hb, ha, np.random.default_rng(4).standard_normal(len(dark)))
        buzz *= np.sqrt(np.mean(dark ** 2)) / np.sqrt(np.mean(buzz ** 2)) * 10 ** (-30 / 20)
        self.assertGreater(self.sc.spectral_distance(dark + buzz, dark), 3.0)
        self.assertGreater(self.sc.air_gap(dark + buzz, dark), 20.0)

    # A quiet chord (the chord sections sit at -40 to -50 dBFS with six voices) puts the 16-bit floor's
    # hiss above the 60 dB floor of the air share, so a dark int16 render read +5 to +33 dB of air against
    # its own float self (#299: TIMBALES, WINDCHIMES, T O M S, CLAV, E. BASS). The air gap now scores
    # PG-8X's render as it would be at our level in int16, so the floor is on both sides; a buzz over
    # it still shows.
    def test_a_quiet_dark_int16_render_has_no_air_gap_against_its_float_self(self):
        np = self.np
        from scipy import signal
        b, a = signal.butter(8, 300 / (SR / 2))
        dark = signal.lfilter(b, a, self.tone)
        for dbfs in (-40, -50):
            x = dark * 10 ** (dbfs / 20) / np.sqrt(np.mean(dark ** 2))
            as16 = np.round(x * 32767) / 32767
            self.assertLess(abs(self.sc.air_gap(as16, x)), 1.0, dbfs)
            self.assertLess(abs(self.sc.air_gap(as16, x * 3.0)), 1.0, dbfs)   # PG-8X louder: same floor
        x = dark * 10 ** (-50 / 20) / np.sqrt(np.mean(dark ** 2))
        hb, ha = signal.butter(4, 5000 / (SR / 2), "high")
        buzz = signal.lfilter(hb, ha, np.random.default_rng(5).standard_normal(len(x)))
        buzz *= np.sqrt(np.mean(x ** 2)) / np.sqrt(np.mean(buzz ** 2)) * 10 ** (-30 / 20)
        self.assertGreater(self.sc.air_gap(np.round((x + buzz) * 32767) / 32767, x), 15.0)

    # The same floor in the spectral column (s10): a quiet section (WINDCHIMES' chord sits near -63 dBFS,
    # its low note near -78) put the 16-bit floor's hiss above the bands' 60 dB floor, and read +20 to
    # +35 dB over PG-8X at 3-8 kHz. Each section now scores against PG-8X's side at our level in int16.
    def quiet_dark_score(self, dbfs, buzz_db=None):
        np = self.np
        from scipy import signal
        b, a = signal.butter(8, 300 / (SR / 2))
        t = np.arange(13 * SR) / SR
        dark = signal.lfilter(b, a, 0.25 * np.sin(2 * np.pi * 110 * t) + 0.1 * np.sin(2 * np.pi * 220 * t))
        x = dark * 10 ** (dbfs / 20) / np.sqrt(np.mean(dark ** 2))
        ours = x
        if buzz_db is not None:
            hb, ha = signal.butter(4, 3000 / (SR / 2), "high")
            buzz = signal.lfilter(hb, ha, np.random.default_rng(6).standard_normal(len(x)))
            ours = x + buzz * np.sqrt(np.mean(x ** 2) / np.mean(buzz ** 2)) * 10 ** (buzz_db / 20)
        as16 = np.clip(np.floor(ours * 32768), -32768, 32767) / 32768
        return self.sc.score_tone(np.stack([as16, as16], 1), np.stack([x, x], 1), harmonic=False)

    def test_a_quiet_int16_render_has_no_spectral_gap_against_its_float_self(self):
        for dbfs in (-60, -75):
            s = self.quiet_dark_score(dbfs)
            self.assertLess(s["spectral_db_mean"], 0.5, (dbfs, s["spectral_db"]))

    def test_a_buzz_over_the_floor_still_shows_in_a_quiet_section(self):
        s = self.quiet_dark_score(-60, buzz_db=-25)
        self.assertGreater(s["spectral_db_mean"], 3.0, s["spectral_db"])

    # Well above the floor, s10 changes nothing: a real gap scores as spectral_distance on the raw sections.
    def test_a_loud_spectral_gap_scores_as_before(self):
        np = self.np
        from scipy import signal
        rng = np.random.default_rng(7)
        pg = 0.05 * rng.standard_normal(13 * SR)
        b, a = signal.butter(2, 1500 / (SR / 2))
        ours = signal.lfilter(b, a, pg)
        ours = np.clip(np.floor(ours * 32768), -32768, 32767) / 32768
        s = self.sc.score_tone(np.stack([ours, ours], 1), np.stack([pg, pg], 1), harmonic=False)
        for k, w in self.sc.SECTIONS.items():
            raw = self.sc.spectral_distance(self.sc._cut(ours, *w), self.sc._cut(pg, *w))
            self.assertGreater(raw, 5.0, k)
            self.assertAlmostEqual(s["spectral_db"][k], raw, delta=0.05, msg=k)

    # Codex (s10): PG-8X's side scaled to our RMS can pass full scale when its peaks stand higher over its
    # RMS than ours do. Clipped, it gains harmonics neither render has; the spectral sections only want
    # the floor, so they take PG-8X's side unclipped.
    def test_a_loud_peaky_reference_is_not_clipped_into_a_gap(self):
        np = self.np
        t = np.arange(13 * SR) / SR
        ours = 0.42 * np.sin(2 * np.pi * 200 * t)                       # -10.5 dBFS, peaks at 0.42
        ours = np.floor(ours * 32768) / 32768
        pg = 0.9 * np.sin(2 * np.pi * 200 * t) * ((t * 5) % 1.0 < 0.1)  # bursts: peaks 3.3x ours at our RMS
        s = self.sc.score_tone(np.stack([ours, ours], 1), np.stack([pg, pg], 1), harmonic=False)
        for k, w in self.sc.SECTIONS.items():
            raw = self.sc.spectral_distance(self.sc._cut(ours, *w), self.sc._cut(pg, *w))
            self.assertAlmostEqual(s["spectral_db"][k], raw, delta=0.05, msg=k)

    # Codex (s10, P3-1): PG-8X's side is scaled to our level section by section, not over the whole
    # render. With PG-8X's quiet low section 15 dB louder than ours (the loud sections alike), a gain over
    # the whole render leaves PG-8X's low section 15 dB over our floor and reads it as a gap.
    def test_each_section_takes_its_own_gain(self):
        np = self.np
        from scipy import signal
        b, a = signal.butter(8, 300 / (SR / 2))
        t = np.arange(13 * SR) / SR
        dark = signal.lfilter(b, a, 0.25 * np.sin(2 * np.pi * 110 * t) + 0.1 * np.sin(2 * np.pi * 220 * t))
        dark /= np.sqrt(np.mean(dark ** 2))
        lo = self.sc.SECTIONS["low"]
        quiet = (t >= lo[0] - 0.1) & (t < lo[1] + 0.1)
        ours = dark * np.where(quiet, 10 ** (-75 / 20), 10 ** (-20 / 20))
        pg = dark * np.where(quiet, 10 ** (-60 / 20), 10 ** (-20 / 20))
        ours = np.floor(ours * 32768) / 32768
        s = self.sc.score_tone(np.stack([ours, ours], 1), np.stack([pg, pg], 1), harmonic=False)
        self.assertLess(s["spectral_db"]["low"], 0.5, s["spectral_db"])

    # PG-8X's side lands on our renderer's grid: output.c to_int16 floors (an arithmetic >> 8) and clips
    # to -32768..32767, and the reader divides by 32768.
    def test_pg_at_our_level_is_floored_and_clipped_as_our_renderer(self):
        np = self.np
        pg = np.array([-0.3, 0.3, -1.3, 1.7, 2.2, -2.2, 0.0, 0.6]) / 32768
        ours = pg * 16384      # same shape at 16384x: g = 16384, so the top two land past full scale
        q = self.sc.as_ours_int16(pg, ours) * 16384 * 32768
        np.testing.assert_allclose(q, [-4916, 4915, -21300, 27852, 32767, -32768, 0, 9830], atol=1e-6)

    # A tone's score on the scorecard MIDI (tools/gate_midi.py score.mid): against itself, all zero. On
    # our renderer's int16 grid, as every render of ours is (s10 puts PG-8X's side on that grid).
    def test_a_render_scored_against_itself_is_all_zero(self):
        np = self.np
        rng = np.random.default_rng(2)
        x = rng.standard_normal((13 * SR, 2)) * np.exp(-np.arange(13 * SR) / SR)[:, None] * 0.1
        x = np.floor(x * 32768) / 32768
        s = self.sc.score_tone(x, x)
        self.assertEqual(set(s), {"loudness_gap_lu", "spectral_db", "spectral_db_mean", "timing_gap_ms",
                                  "pitch_cents", "air_gap_db"})
        self.assertEqual(s["air_gap_db"], 0.0)
        self.assertEqual(s["loudness_gap_lu"], 0.0)
        self.assertEqual(s["spectral_db_mean"], 0.0)
        self.assertTrue(all(v == 0.0 for v in s["spectral_db"].values()))
        self.assertEqual(set(s["timing_gap_ms"]), {"high_decay20", "high_release40"})
        self.assertTrue(all(v in (0.0, None) for v in s["timing_gap_ms"].values()))
        self.assertTrue(all(v == 0.0 for v in s["pitch_cents"].values()))


    # The ruler's own noise: PG-8X's render scored against itself 10 ms later, per section. A section
    # that reads a change in the sound that small isn't reading timbre. A held sine reads 0 everywhere;
    # one that changes pitch inside v90's window reads there and nowhere else.
    def test_the_ruler_noise_reads_only_where_the_sound_moves_within_10_ms(self):
        np = self.np
        t = np.arange(13 * SR) / SR
        steady = 0.1 * np.sin(2 * np.pi * 440 * t)
        self.assertTrue(all(v < 0.01 for v in self.sc.ruler_noise(steady).values()))
        a, b = self.sc.SECTIONS["v90"]
        f = np.where((t >= a + 0.2) & (t < b), 3000.0, 440.0)
        moving = 0.1 * np.sin(2 * np.pi * np.cumsum(f) / SR)
        n = self.sc.ruler_noise(moving)
        self.assertGreater(n["v90"], 0.1)
        self.assertTrue(all(v < 0.01 for k, v in n.items() if k != "v90"), n)

    # The report's line for it: each section's mean over the tones, in SECTIONS' order.
    def test_the_ruler_noise_line_is_each_sections_mean(self):
        line = self.sc.noise_line([{"chord": 0.0, "v90": 1.0}, {"chord": 0.2, "v90": 0.5}])
        self.assertEqual(line, "ruler noise (PG-8X against itself 10 ms later, dB): chord 0.10, v90 0.75")

    # A ring or cross-mod tone has no single pitch: the correlation lines up unrelated partials (WINDCHIMES
    # read +140 c with every partial within 6 c of PG-8X's, s7), so no pitch is reported for it.
    def test_an_inharmonic_tone_reports_no_pitch(self):
        np = self.np
        rng = np.random.default_rng(3)
        x = rng.standard_normal((13 * SR, 2)) * 0.1
        s = self.sc.score_tone(x, x, harmonic=False)
        self.assertEqual(s["pitch_cents"], {"low": None, "high": None})
        self.assertEqual(s["spectral_db_mean"], 0.0)

    # Which tones get no pitch: the converted patch's ring or cross-mod (XMOD_AM) without sync. With sync
    # osc2 restarts every osc1 cycle, so even the ring's product repeats at osc1's period: it has a pitch.
    def test_ring_and_cross_mod_tones_without_sync_are_the_inharmonic_ones(self):
        tree = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        odd = self.sc.inharmonic_tones(tree)
        self.assertIn(14, odd)       # WINDCHIMES: ring
        self.assertIn(13, odd)       # MUSIC BOX: ring
        self.assertNotIn(15, odd)    # E. BASS: ring with sync
        self.assertNotIn(25, odd)    # SYNC LEAD: ring with sync
        self.assertNotIn(1, odd)     # PIANO 1
        self.assertNotIn(22, odd)    # CLAV: sync alone
        # a ring near unison (within 25 c of a whole-number ratio) sounds at osc1's pitch, beating
        self.assertNotIn(19, odd)    # RES BELL: osc2 +15 c
        self.assertNotIn(20, odd)    # WAVEAURA: osc2 -14 c
        self.assertNotIn(32, odd)    # PSYCHMELLO: osc2 -12 c

@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class BeatPhaseTest(unittest.TestCase):
    """s12 (#382): two detuned DCOs beat slower than a section is long, so one render's score reads where
    in the beat the note started. Scored over renders at evenly spread DCO-2 start offsets, power
    averaged, the beat's cross term cancels: the score no longer depends on the start."""
    def setUp(self):
        import numpy as np
        import scorecard
        self.np, self.sc = np, scorecard

    def pair(self, offset, f0=220.0, beat=0.3, seconds=13.0, decay_s=None):
        """A saw-like pair, DCO-2 `beat` Hz above DCO-1 and `offset` of a cycle ahead at the start;
        falling 20 dB every `decay_s` s from the start when given."""
        np = self.np
        t = np.arange(int(seconds * SR)) / SR
        x = 0.01 * sum((np.sin(2 * np.pi * k * f0 * t) + np.sin(2 * np.pi * k * ((f0 + beat) * t + offset))) / k
                       for k in range(1, 13))
        if decay_s:
            x = x * 10 ** (-t / decay_s)
        return np.stack([x, x], axis=1)

    def draws(self, start, k=8, **kw):
        return [self.pair(start + j / k, **kw) for j in range(k)]

    # A PG-8X draw whose MIDI started LEAD ticks late (50 samples a tick at 48 kHz) is read from its own
    # start: the same render padded with that much silence scores as the unpadded one, figure for figure
    def test_a_draw_shifted_by_its_lead_scores_as_the_same_render_unshifted(self):
        np = self.np
        ours = self.draws(0.0)
        pg = [self.pair(0.3, decay_s=8), self.pair(0.7, decay_s=8)]
        lead = [240, 1011]
        late = [(np.concatenate([np.zeros((50 * t, 2)), x]), self.sc.lead_seconds(t)) for x, t in zip(pg, lead)]
        self.assertEqual(self.sc.score_tone(ours, late), self.sc.score_tone(ours, pg))
        self.assertEqual(self.sc.ruler_noise(late), self.sc.ruler_noise(pg))

    def engine(self, lead_s, offset, seconds=16.0):
        """A toy synth with what s13 is about: a detuned pair started `offset` of a cycle apart at its note,
        0.25 s past a MIDI start `lead_s` late, through a chorus whose LFO (0.68 Hz) runs on the clock from
        the start of the render, as both engines' do"""
        np = self.np
        t = np.arange(int(seconds * SR)) / SR
        u = np.maximum(t - 0.25 - lead_s, 0)
        x = 0.01 * sum((np.sin(2 * np.pi * k * 220.0 * u) + np.sin(2 * np.pi * k * (220.3 * u + offset))) / k
                       for k in range(1, 13)) * (t >= 0.25 + lead_s)
        d = (0.0022 + 0.0014 * np.sin(2 * np.pi * 0.68 * t)) * SR          # the chorus tap, samples
        i = np.arange(len(t)) - d
        wet = np.interp(i, np.arange(len(t)), x, left=0)
        y = x + wet
        return np.stack([y, y], axis=1)

    # The chorus's phase at a section runs on the clock: ours all at lead 0 meets one phase, PG-8X's 16 leads
    # meet 16, and the same synth scores far from itself; at the reference's own leads it scores about 0
    def test_draws_at_the_references_leads_meet_its_chorus_phases(self):
        import scorecard_cmd
        leads = [0, 232, 280, 468, 864, 973, 1011, 1077, 1215, 1291, 1694, 2354, 2535, 2714, 2806, 2825]
        pg = [(self.engine(self.sc.lead_seconds(t), 0.37 + j / 16), self.sc.lead_seconds(t))
              for j, t in enumerate(leads)]
        plan = scorecard_cmd.draw_plan(leads)
        L = self.sc.lead_seconds
        ours = [(self.engine(L(t), o / 1000), L(t)) for t, o in plan]
        moved = [(self.engine(L(t), (o / 1000 + 0.31) % 1), L(t)) for t, o in plan]   # our offsets from elsewhere
        at_zero = [self.engine(0.0, o / 1000) for o in scorecard_cmd.OFFSETS_MILLI]
        matched = self.sc.score_tone(ours, pg)["spectral_db"]
        other = self.sc.score_tone(moved, pg)["spectral_db"]
        unmatched = self.sc.score_tone(at_zero, pg)["spectral_db"]
        # where our offsets start no longer matters (with 1 offset a lead, as 16 draws: 1.56 dB here) ...
        for k in matched:
            self.assertAlmostEqual(matched[k], other[k], delta=0.1, msg=k)
        # ... and the chorus's phases are met: far closer than ours all at lead 0. What is left is PG-8X's side:
        # each of its draws pairs a chorus phase with one beat phase (more PG draws would average it)
        self.assertLess(max(matched.values()), max(unmatched.values()) / 2, (matched, unmatched))

    def test_the_loudness_gap_over_spread_offsets_does_not_depend_on_where_they_start(self):
        pg = self.pair(0.3, beat=0.05)
        one = [self.sc.score_tone(self.pair(o, beat=0.05), pg)["loudness_gap_lu"] for o in (0.0, 0.25, 0.5)]
        self.assertGreater(max(one) - min(one), 1.0)
        a = self.sc.score_tone(self.draws(0.0, beat=0.05), pg)["loudness_gap_lu"]
        b = self.sc.score_tone(self.draws(1 / 16, beat=0.05), pg)["loudness_gap_lu"]
        self.assertAlmostEqual(a, b, delta=0.02)

    # The envelope's timings read the draws' mean power over time, so a beat's dip doesn't decide where a
    # decay crosses its line
    def test_a_decay_over_spread_offsets_does_not_depend_on_where_they_start(self):
        np = self.np
        one = [self.sc.decay_ms(self.pair(o, f0=1046.5, beat=1.5, seconds=3, decay_s=0.6), 0.0, 3.0, 1046.5, 20)
               for o in (0.0, 0.25, 0.5)]
        self.assertGreater(max(one) - min(one), 20)
        a, b = (self.sc.decay_ms(self.draws(s, f0=1046.5, beat=1.5, seconds=3, decay_s=0.6), 0.0, 3.0, 1046.5, 20)
                for s in (0.0, 1 / 16))
        self.assertAlmostEqual(a, b, delta=2)
        self.assertAlmostEqual(a, 600, delta=60)

    def test_a_score_over_spread_offsets_does_not_depend_on_where_they_start(self):
        pg = self.pair(0.3)
        one = [self.sc.score_tone(self.pair(o), pg)["spectral_db"] for o in (0.0, 0.25, 0.5)]
        swing = max(max(s[k] for s in one) - min(s[k] for s in one) for k in self.sc.SECTIONS)
        self.assertGreater(swing, 1.0)
        # 8 offsets cancel harmonics 1..7's cross term exactly; the 8th's (2.4 Hz here) is left, partly
        # averaged out inside the section, so a little stays
        a = self.sc.score_tone(self.draws(0.0), pg)
        b = self.sc.score_tone(self.draws(1 / 16), pg)
        for k in self.sc.SECTIONS:
            self.assertAlmostEqual(a["spectral_db"][k], b["spectral_db"][k], delta=0.15, msg=k)


# The scorecard scores PG-8X against the engine's own level (#342): its renders never take -d, the FM-1
# module's output gain, so moving that gain (#320) leaves the ruler (v5/s10) as it is. Needs no numpy.
class RenderCommandTest(unittest.TestCase):
    def test_the_scorecard_renders_at_the_engines_level_without_d(self):
        import scorecard_cmd
        cmd = scorecard_cmd.render_command("/t", ["-s", "osc1_level_permille=1000"], "a.mid", "b.wav")
        self.assertNotIn("-d", cmd)
        self.assertEqual(cmd, [os.path.join("/t", "build", "fm1-8p-render"), "-r", "48000", "-t", "3000", "-v", "6",
                               "-s", "osc1_level_permille=1000", "a.mid", "b.wav"])

    # s12 (#382): each draw of ours is a render with DCO-2 started OFFSET thousandths of a cycle on (-o)
    def test_a_draw_renders_with_its_dco2_offset(self):
        import scorecard_cmd
        cmd = scorecard_cmd.render_command("/t", ["-s", "osc1_level_permille=1000"], "a.mid", "b.wav", offset_milli=375)
        self.assertEqual(cmd, [os.path.join("/t", "build", "fm1-8p-render"), "-r", "48000", "-t", "3000", "-v", "6",
                               "-o", "375", "-s", "osc1_level_permille=1000", "a.mid", "b.wav"])

    # ours' draws: 8 offsets an eighth of a cycle apart, which cancel the cross term of a beat's first 7
    # harmonics exactly (the split-half check on the factory bank: under 0.21 dB a section, #382)
    def test_ours_are_eight_draws_an_eighth_of_a_cycle_apart(self):
        import scorecard_cmd
        self.assertEqual(scorecard_cmd.OFFSETS_MILLI, (0, 125, 250, 375, 500, 625, 750, 875))


# A PG-8X reference can be several draws (s12, #382): its DCOs run free, so the same MIDI started later
# meets them at another beat phase. The scorecard MIDI with a lead of N ticks (960 a second) is the same
# notes N ticks later. Needs no numpy.
class LeadTest(unittest.TestCase):
    # s13 (#392): our draws render the reference's leads, so whatever runs on the clock (the chorus's LFO,
    # the LFO: both free-run from init on both sides) meets the phases PG-8X's draws met; at each lead, 4
    # DCO-2 offsets a quarter cycle apart (staggered by 1 / 4K from lead to lead), so the DCO beat cancels at
    # every lead on its own and doesn't pair with the chorus's phase; a one-draw reference keeps s12's 8
    # offsets at lead 0
    def test_our_draws_follow_the_references_leads(self):
        import scorecard_cmd
        self.assertEqual(scorecard_cmd.draw_plan([0]), [(0, o) for o in scorecard_cmd.OFFSETS_MILLI])
        self.assertEqual(scorecard_cmd.draw_plan([96] * 8), [(96, o) for o in scorecard_cmd.OFFSETS_MILLI])
        self.assertEqual(scorecard_cmd.draw_plan([0, 480]),
                         [(0, 0), (0, 250), (0, 500), (0, 750), (480, 125), (480, 375), (480, 625), (480, 875)])
        leads = [0, 232, 280, 468, 864, 973, 1011, 1077, 1215, 1291, 1694, 2354, 2535, 2714, 2806, 2825]
        plan = scorecard_cmd.draw_plan(leads)
        self.assertEqual(len(plan), 64)
        self.assertEqual([t for t, _ in plan], [t for t in leads for _ in range(4)])
        self.assertEqual(plan[4:8], [(232, 16), (232, 266), (232, 516), (232, 766)])

    def test_a_lead_moves_every_event_by_whole_ticks(self):
        import gate_midi
        a, b = gate_midi.smf(gate_midi.score()), gate_midi.smf(gate_midi.score(), lead=96)
        # the first event's delta: SCORE_START (240 ticks), then 96 more; the rest as they were
        self.assertEqual(a[22:24], gate_midi.vlq(240))
        self.assertEqual(b[22:24], gate_midi.vlq(336))
        self.assertEqual(a[24:], b[24:])
        self.assertEqual(a[18:22], b[18:22])


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class HistoryTest(unittest.TestCase):
    def setUp(self):
        import tempfile
        import scorecard
        self.sc = scorecard
        self.dir = tempfile.TemporaryDirectory()
        self.path = os.path.join(self.dir.name, "history.jsonl")

    def tearDown(self):
        self.dir.cleanup()

    def test_a_tone_row_has_its_schema_commit_tone_and_metrics(self):
        score = {"loudness_gap_lu": -1.5, "spectral_db_mean": 12.0}
        row = self.sc.tone_row("a254bee", "2026-11-12T23:00:00-05:00", 25, "SYNC LEAD", score, "ref-1")
        self.assertEqual(row["schema"], 13)
        self.assertEqual(row["kind"], "tone")
        self.assertEqual((row["commit"], row["tone"], row["name"], row["ref"]), ("a254bee", 25, "SYNC LEAD", "ref-1"))
        self.assertEqual(row["loudness_gap_lu"], -1.5)

    # A base and a head are only compared on one ruler: a commit's rows on another schema are not its rows,
    # so a base scored before a ruler change has to be scored again (Eric's ruler rule).
    def test_rows_for_takes_only_the_current_schemas_rows(self):
        old = {"schema": self.sc.SCHEMA - 1, "kind": "tone", "commit": "c1", "ref": "r", "bank": "factory",
               "date": "2026-11-18T10:00:00-05:00", "tone": 1}
        new = dict(old, schema=self.sc.SCHEMA, date="2026-11-18T09:00:00-05:00", tone=2)
        self.sc.append_rows(self.path, [new, old])
        self.assertEqual([r["tone"] for r in self.sc.rows_for(self.path, "c1", "r")], [2])
        with open(self.path, "w") as fh:
            fh.write(json.dumps(old) + "\n")
        self.assertIsNone(self.sc.rows_for(self.path, "c1", "r"))

    # The 25 c line is exact: an osc2 25 c off a whole ratio is at the line, not over it (Codex on #288).
    def test_25_cents_off_a_whole_ratio_is_at_the_line_not_over(self):
        for c in (25, -25, 1225, 1175, 2425):
            self.assertLessEqual(self.sc.off_whole_ratio_cents(c), self.sc.WHOLE_RATIO_CENTS, c)
        self.assertGreater(self.sc.off_whole_ratio_cents(26), self.sc.WHOLE_RATIO_CENTS)
        self.assertEqual(self.sc.off_whole_ratio_cents(0), 0)

    # A base with rows on an older ruler only is told to score again, not that it has none.
    def test_a_base_on_another_schema_is_told_to_score_again(self):
        old = {"schema": self.sc.SCHEMA - 1, "kind": "tone", "commit": "c1", "ref": "r", "bank": "factory",
               "date": "2026-11-18T10:00:00-05:00", "tone": 1}
        self.sc.append_rows(self.path, [old])
        msg = self.sc.missing_base(self.path, "c1", "r")
        self.assertIn(f"schema {self.sc.SCHEMA - 1}", msg)
        self.assertIn("score it again", msg)
        self.assertIn("no rows", self.sc.missing_base(self.path, "zzz", "r"))

    def test_rows_are_appended_and_old_lines_stay_as_they_were(self):
        with open(self.path, "w") as fh:
            fh.write('{"schema": 1, "kind": "tone", "old": true}\n')
        before = open(self.path, "rb").read()
        self.sc.append_rows(self.path, [{"schema": 1, "kind": "tone", "n": 1}, {"schema": 1, "kind": "tone", "n": 2}])
        after = open(self.path, "rb").read()
        self.assertTrue(after.startswith(before))
        self.assertEqual(len(after.splitlines()), 3)

    def test_a_param_row_comes_from_a_match_result(self):
        result = {"name": "VCF_KEY", "group": "VCF", "status": "CURVE OFF", "worst_ratio": 14.6,
                  "metrics": [{"key": "track_84_re_36", "unit": "st", "tol": 1}],
                  "judge": {"track_84_re_36": {"err": -14.64, "at": 127, "ratio": 14.64, "n": 9}},
                  "tones_affected": ["PIANO 1", "PIANO 2"], "rows": [[0, 1, 2]]}
        row = self.sc.param_row("a254bee", "2026-11-12T23:00:00-05:00", result)
        self.assertEqual((row["schema"], row["kind"], row["param"], row["group"]), (13, "param", "VCF_KEY", "VCF"))
        self.assertEqual(row["status"], "CURVE OFF")
        self.assertEqual(row["worst"], {"track_84_re_36": {"err": -14.64, "at": 127, "unit": "st", "tol": 1}})
        self.assertEqual((row["tones_over"], row["tones"]), (2, ["PIANO 1", "PIANO 2"]))
        self.assertNotIn("rows", row)

    def rows(self, means, gaps=None):
        gaps = gaps or [0.0] * len(means)
        return [{"kind": "tone", "tone": i + 1, "name": f"T{i + 1}", "spectral_db_mean": m, "loudness_gap_lu": g}
                for i, (m, g) in enumerate(zip(means, gaps))]

    def test_tones_moved_lists_the_changes_biggest_first(self):
        base = self.rows([10.0, 20.0, 15.0, 8.0])
        head = self.rows([10.0, 12.0, 17.0, 8.0], [0.0, 0.0, 0.0, -2.0])
        got = self.sc.moved(base, head)
        self.assertEqual([m["tone"] for m in got], [2, 3, 4])
        self.assertEqual(got[0]["verdict"], "closer")
        self.assertEqual(got[1]["verdict"], "further")
        self.assertAlmostEqual(got[0]["spectral_change"], -8.0)
        text = self.sc.moved_table(got)
        self.assertIn("| 02 T2 |", text)
        self.assertIn("20.0 → 12.0", text)

    def test_a_tone_the_base_lacks_is_new_not_dropped(self):
        got = self.sc.moved(self.rows([10.0, 20.0]), self.rows([10.0, 20.0, 15.0]))
        self.assertEqual([(m["tone"], m["verdict"]) for m in got], [(3, "new")])
        self.assertEqual(got[0]["spectral_head"], 15.0)
        self.assertIsNone(got[0]["spectral_base"])
        self.assertIn("| 03 T3 | - → 15.0 |", self.sc.moved_table(got))

    def test_a_tone_the_head_lacks_is_gone_not_dropped(self):
        got = self.sc.moved(self.rows([10.0, 20.0, 15.0]), self.rows([10.0, 20.0]))
        self.assertEqual([(m["tone"], m["verdict"]) for m in got], [(3, "gone")])
        self.assertIn("| 03 T3 | 15.0 → - |", self.sc.moved_table(got))

    def test_the_report_counts_new_and_gone_tones(self):
        head = self.rows([5, 30, 1])
        for r in head:
            r.update({"spectral_db": {"chord": r["spectral_db_mean"]}, "timing_gap_ms": {},
                      "pitch_cents": {"low": 0.0, "high": 0.0}})
        base = self.rows([5, 30, 1, 7])[1:]
        text = self.sc.report(head, base)
        self.assertIn("tones moved: 2 (0 closer, 0 further, 1 new, 1 gone)", text)

    def test_with_none_new_or_gone_the_count_line_is_as_before(self):
        head = self.rows([5, 30])
        for r in head:
            r.update({"spectral_db": {"chord": r["spectral_db_mean"]}, "timing_gap_ms": {},
                      "pitch_cents": {"low": 0.0, "high": 0.0}})
        self.assertIn("tones moved: 1 (1 closer, 0 further)\n", self.sc.report(head, self.rows([5, 31])) + "\n")

    def test_a_figure_that_is_not_a_number_is_an_error(self):
        nan = float("nan")
        for base, head in (([10.0], [nan]), ([nan], [10.0])):
            with self.subTest(base=base, head=head), self.assertRaises(ValueError):
                self.sc.moved(self.rows(base, [2.0]), self.rows(head, [1.0]))
        with self.assertRaises(ValueError):
            self.sc.moved(self.rows([10.0], [nan]), self.rows([10.0], [1.0]))
        with self.assertRaises(ValueError):
            self.sc.moved(self.rows([10.0]), [dict(self.rows([10.0])[0], spectral_db_mean=True)])

    def test_a_base_given_as_a_generator_is_read_once(self):
        got = self.sc.moved((r for r in self.rows([10.0, 20.0])), self.rows([10.0, 12.0]))
        self.assertEqual([m["tone"] for m in got], [2])

    def test_the_report_has_every_tone_the_worst_five_and_what_moved(self):
        head = self.rows([5, 30, 1, 25, 9, 40, 2])
        for r in head:
            r.update({"spectral_db": {"chord": r["spectral_db_mean"]}, "timing_gap_ms": {"high_decay20": 10.0},
                      "pitch_cents": {"low": 0.0, "high": 5.0}})
        text = self.sc.report(head, self.rows([5, 20, 1, 25, 9, 40, 2]))
        self.assertIn("| 07 T7 |", text)
        self.assertIn("worst 5", text)
        self.assertIn("tones moved", text)
        self.assertIn("20.0 → 30.0", text)
        self.assertIn("no base", self.sc.report(head, None))

    def test_the_latest_rows_of_a_commit_are_read_back(self):
        old = self.sc.tone_row("abc", "2026-11-12T01:00:00-05:00", 1, "T1", {"spectral_db_mean": 9.0}, "r1")
        new = self.sc.tone_row("abc", "2026-11-12T02:00:00-05:00", 1, "T1", {"spectral_db_mean": 7.0}, "r1")
        other = self.sc.tone_row("abc", "2026-11-12T03:00:00-05:00", 1, "T1", {"spectral_db_mean": 1.0}, "r2")
        self.sc.append_rows(self.path, [old, new, other])
        got = self.sc.rows_for(self.path, "abc", "r1")
        self.assertEqual([r["spectral_db_mean"] for r in got], [7.0])
        self.assertIsNone(self.sc.rows_for(self.path, "zzz", "r1"))

    def test_rows_carry_their_bank_and_rows_without_one_are_the_factory_bank(self):
        row = self.sc.tone_row("abc", "2026-11-13T01:00:00-05:00", 1, "T1", {"spectral_db_mean": 3.0}, "r1",
                               bank="other")
        self.assertEqual(row["bank"], "other")
        self.assertEqual(self.sc.tone_row("abc", "d", 1, "T1", {}, "r1")["bank"], "factory")
        old = {"schema": self.sc.SCHEMA, "kind": "tone", "commit": "abc", "date": "2026-11-13T00:00:00-05:00", "tone": 1,
               "name": "T1", "ref": "r1", "spectral_db_mean": 9.0}
        self.sc.append_rows(self.path, [old, row])
        self.assertEqual([r["spectral_db_mean"] for r in self.sc.rows_for(self.path, "abc", "r1")], [9.0])
        self.assertEqual([r["spectral_db_mean"] for r in self.sc.rows_for(self.path, "abc", "r1", "other")], [3.0])

    def test_worst_five_are_the_largest_spectral_means(self):
        got = self.sc.worst(self.rows([5, 30, 1, 25, 9, 40, 2]), 5)
        self.assertEqual([r["tone"] for r in got], [6, 2, 4, 5, 1])


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class RunTest(unittest.TestCase):
    """The whole run on this checkout, against a "reference" of its own renders: every tone scores 0. The
    reference holds all 8 of ours' draws (s12), each at shift 0, so they meet ours' 8 draw for draw."""

    @classmethod
    def setUpClass(cls):
        import subprocess
        import tempfile
        cls.repo = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
        subprocess.run(["make", "-s", "-C", cls.repo, "build/fm1-8p-render"], check=True)
        cls.tmp = tempfile.TemporaryDirectory()
        cls.ref = os.path.join(cls.tmp.name, "ref")
        cls.hist = os.path.join(cls.tmp.name, "history.jsonl")
        import scorecard
        cls.sc = scorecard
        work = os.path.join(cls.tmp.name, "w0")
        mid = scorecard.write_midi(work)
        os.makedirs(cls.ref)
        import scorecard_cmd
        for i, paths in scorecard.render_tones(cls.repo, work, None, scorecard_cmd.draw_plan([0] * 8)):
            for j, path in enumerate(paths):
                os.replace(path, os.path.join(cls.ref, scorecard.draw_file(i, j)))
        scorecard.write_manifest(cls.ref, mid, shifts_ticks=[0] * len(paths))

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_a_run_against_its_own_renders_scores_zero_and_appends_32_rows(self):
        import json
        rows = self.sc.run(self.repo, "deadbee", self.ref, self.hist, work=os.path.join(self.tmp.name, "w1"))
        self.assertEqual(len(rows), 32)
        self.assertTrue(all(r["spectral_db_mean"] == 0.0 and r["loudness_gap_lu"] == 0.0 for r in rows))
        with open(self.hist) as fh:
            got = [json.loads(line) for line in fh]
        self.assertEqual(len([r for r in got if r["kind"] == "tone" and r["commit"] == "deadbee"]), 32)

    # A reference whose draws have leads: ours render them (4 offsets a lead), and the rows say so
    def test_a_reference_with_leads_is_met_at_its_leads(self):
        import shutil
        import scorecard_cmd
        ref = os.path.join(self.tmp.name, "leads")
        os.makedirs(ref)
        work = os.path.join(self.tmp.name, "wl0")
        mid = self.sc.write_midi(work)
        plan = scorecard_cmd.draw_plan([0, 480])
        for i, paths in self.sc.render_tones(self.repo, work, None, [(0, 0), (480, 500)]):
            for j, path in enumerate(paths):
                os.replace(path, os.path.join(ref, self.sc.draw_file(i, j)))
        self.sc.write_manifest(ref, mid, shifts_ticks=[0, 480])
        rows = self.sc.run(self.repo, "deadbee", ref, os.path.join(self.tmp.name, "hl.jsonl"),
                           work=os.path.join(self.tmp.name, "wl1"))
        mean = sum(r["spectral_db_mean"] for r in rows) / len(rows)
        print(f"\n    leads 0 and 480: our own renders score {mean:.3f}")
        # the negative: the lead-480 draw swapped for a lead-0 render (its sound half a second early) reads far
        # worse, so the leads are what lines the two sides up
        bad = os.path.join(self.tmp.name, "leads-bad")
        shutil.copytree(ref, bad)
        for i in range(1, 33):
            shutil.copy(os.path.join(ref, self.sc.draw_file(i, 0)), os.path.join(bad, self.sc.draw_file(i, 1)))
        wrong = self.sc.run(self.repo, "deadbee", bad, os.path.join(self.tmp.name, "hlb.jsonl"),
                            work=os.path.join(self.tmp.name, "wl2"))
        worse = sum(r["spectral_db_mean"] for r in wrong) / len(wrong)
        print(f"    with draw 1 rendered at lead 0: {worse:.3f}")
        self.assertLess(mean, 0.5)
        self.assertGreater(worse, mean + 1.0)
        self.assertEqual(rows[0]["draws"], {"ours_leads_ticks": [t for t, _ in plan], "ours_offsets_milli": [o for _, o in plan],
                                            "pg_shifts_ticks": [0, 480]})

    # A tone's score is cached under the hash of what decides it: our draws' wavs, the reference's draws and
    # manifest, and the scorecard's code. A second run whose renders are the same (main as every PR's base,
    # or a PR that doesn't touch the sound) reads it back instead of scoring it again, row for row
    def test_a_second_run_with_the_same_renders_reads_its_scores_from_the_cache(self):
        cache = os.path.join(self.tmp.name, "cache")
        first = self.sc.run(self.repo, "c0ffee1", self.ref, os.path.join(self.tmp.name, "hc1.jsonl"),
                            work=os.path.join(self.tmp.name, "wc1"), cache=cache)
        real = self.sc.score_tone
        def refuse(*a, **k):
            raise AssertionError("scored again")
        self.sc.score_tone = refuse
        try:
            second = self.sc.run(self.repo, "c0ffee2", self.ref, os.path.join(self.tmp.name, "hc2.jsonl"),
                                 work=os.path.join(self.tmp.name, "wc2"), cache=cache)
        finally:
            self.sc.score_tone = real
        strip = lambda r: {k: v for k, v in r.items() if k not in ("commit", "date")}
        self.assertEqual([strip(r) for r in first], [strip(r) for r in second])
        self.assertEqual({r["commit"] for r in second}, {"c0ffee2"})

    def test_the_cache_key_changes_with_any_byte_of_a_draw(self):
        import shutil
        d = os.path.join(self.tmp.name, "keys")
        os.makedirs(d, exist_ok=True)
        a, b = os.path.join(d, "a.wav"), os.path.join(d, "b.wav")
        shutil.copy(os.path.join(self.ref, self.sc.draw_file(5, 0)), a)
        shutil.copy(a, b)
        man = {"midi_sha256": "x", "selfnoise": 0}
        k1 = self.sc.tone_key([a], [b], man, True)
        self.assertEqual(k1, self.sc.tone_key([a], [b], man, True))
        with open(b, "r+b") as fh:
            fh.seek(-1, 2)
            last = fh.read(1)
            fh.seek(-1, 2)
            fh.write(bytes([last[0] ^ 1]))
        self.assertNotEqual(k1, self.sc.tone_key([a], [b], man, True))
        self.assertNotEqual(k1, self.sc.tone_key([a], [a], man, False))
        self.assertNotEqual(k1, self.sc.tone_key([a], [a], dict(man, shifts_ticks=[0]), True))

    def test_a_row_says_which_draws_it_was_scored_over(self):
        rows = self.sc.run(self.repo, "deadbee", self.ref, os.path.join(self.tmp.name, "hd.jsonl"),
                           work=os.path.join(self.tmp.name, "wd"))
        self.assertEqual(rows[0]["schema"], 13)
        self.assertEqual(rows[0]["draws"], {"ours_leads_ticks": [0] * 8,
                                            "ours_offsets_milli": [0, 125, 250, 375, 500, 625, 750, 875],
                                            "pg_shifts_ticks": [0] * 8})

    # A one-draw reference (v6) is still read: the PG-8X side is then that one draw
    def test_a_reference_of_one_draw_per_tone_is_read(self):
        import shutil
        ref = os.path.join(self.tmp.name, "one")
        os.makedirs(ref)
        for i in range(1, 33):
            shutil.copy(os.path.join(self.ref, self.sc.draw_file(i, 0)), os.path.join(ref, f"{i:02d}.wav"))
        mid = self.sc.write_midi(os.path.join(self.tmp.name, "w6"))
        self.sc.write_manifest(ref, mid)
        rows = self.sc.run(self.repo, "deadbee", ref, os.path.join(self.tmp.name, "h7.jsonl"),
                           work=os.path.join(self.tmp.name, "w7"))
        self.assertEqual(rows[0]["draws"]["pg_shifts_ticks"], [0])
        self.assertTrue(all(r["spectral_db_mean"] < 10 for r in rows))

    # Each draw of a reference names the MIDI it was rendered from: the scorecard MIDI with its lead
    def test_a_draw_rendered_from_other_midi_is_refused(self):
        import json
        import shutil
        ref = os.path.join(self.tmp.name, "badlead")
        shutil.copytree(self.ref, ref)
        with open(os.path.join(ref, "manifest.json")) as fh:
            man = json.load(fh)
        man["shifts_ticks"][3] = 48            # says 48 ticks late, but its MIDI is the unshifted one
        with open(os.path.join(ref, "manifest.json"), "w") as fh:
            json.dump(man, fh)
        with self.assertRaises(SystemExit) as e:
            self.sc.run(self.repo, "deadbee", ref, os.path.join(self.tmp.name, "h8.jsonl"), work=os.path.join(self.tmp.name, "w8"))
        self.assertIn("draw 3", str(e.exception))

    # A shift is a lead of whole ticks, 0 or more: anything else stops the run before it renders
    def test_a_shift_that_is_not_a_whole_tick_count_of_0_or_more_is_refused(self):
        import json
        import shutil
        for bad in (-300, 48.0, "48"):
            ref = os.path.join(self.tmp.name, f"badshift-{bad}")
            shutil.copytree(self.ref, ref)
            with open(os.path.join(ref, "manifest.json")) as fh:
                man = json.load(fh)
            man["shifts_ticks"][1] = bad
            with open(os.path.join(ref, "manifest.json"), "w") as fh:
                json.dump(man, fh)
            with self.assertRaises(SystemExit) as e:
                self.sc.run(self.repo, "deadbee", ref, os.path.join(self.tmp.name, "hb.jsonl"),
                            work=os.path.join(self.tmp.name, f"wb-{bad}"))
            self.assertIn("draw 1", str(e.exception))

    def test_a_reference_missing_a_draw_file_is_refused_before_rendering(self):
        import shutil
        ref = os.path.join(self.tmp.name, "missing")
        shutil.copytree(self.ref, ref)
        os.remove(os.path.join(ref, self.sc.draw_file(17, 5)))
        work = os.path.join(self.tmp.name, "wm")
        with self.assertRaises(SystemExit) as e:
            self.sc.run(self.repo, "deadbee", ref, os.path.join(self.tmp.name, "hm.jsonl"), work=work)
        self.assertIn("17-5.wav", str(e.exception))
        self.assertFalse(os.path.exists(os.path.join(work, "ours")))

    def test_another_bank_is_scored_and_its_rows_say_so(self):
        import json
        import shutil
        bank = os.path.join(self.tmp.name, "other.syx")
        shutil.copy(os.path.join(self.repo, "data", "jx8p-factory-1-32.syx"), bank)
        ref = os.path.join(self.tmp.name, "bankref")
        shutil.copytree(self.ref, ref)
        mid = self.sc.write_midi(os.path.join(self.tmp.name, "w3"))
        self.sc.write_manifest(ref, mid, bank, shifts_ticks=[0] * 8)
        hist = os.path.join(self.tmp.name, "bank-history.jsonl")
        rows = self.sc.run(self.repo, "deadbee", ref, hist, work=os.path.join(self.tmp.name, "w4"),
                           bank=bank, bank_name="other")
        self.assertEqual({r["bank"] for r in rows}, {"other"})
        self.assertTrue(all(r["spectral_db_mean"] == 0.0 for r in rows))
        with open(os.path.join(ref, "manifest.json")) as fh:
            man = json.load(fh)
        man["bank_sha256"] = "0" * 64
        with open(os.path.join(ref, "manifest.json"), "w") as fh:
            json.dump(man, fh)
        with self.assertRaises(SystemExit):
            self.sc.run(self.repo, "deadbee", ref, hist, work=os.path.join(self.tmp.name, "w5"),
                        bank=bank, bank_name="other")

    def test_a_reference_with_pg8xs_self_noise_on_is_refused(self):
        """Since v5 (#247) the reference has PG-8X's SELFNOISE at 0: its analog noise floor, under every
        sounding voice, read as a 27 dB gap in the lows that isn't timbre. A manifest that doesn't say
        selfnoise 0 (v4 and older say nothing; 0.5 is PG-8X's default) is refused."""
        import json
        import shutil
        for say in (None, 0.5):
            ref = os.path.join(self.tmp.name, f"noisyref-{say}")
            shutil.copytree(self.ref, ref)
            with open(os.path.join(ref, "manifest.json")) as fh:
                man = json.load(fh)
            man.pop("selfnoise", None)
            if say is not None:
                man["selfnoise"] = say
            with open(os.path.join(ref, "manifest.json"), "w") as fh:
                json.dump(man, fh)
            with self.assertRaises(SystemExit) as e:
                self.sc.run(self.repo, "deadbee", ref, self.hist, work=os.path.join(self.tmp.name, f"wn-{say}"))
            self.assertIn("SELFNOISE", str(e.exception))

    def test_scorecard_sh_defaults_to_v5_under_fm1_ref(self):
        """With no SCORECARD_REF, tools/scorecard.sh takes FM1_REF's pg8x/scorecard/v5 (the factory
        reference since #247), and says which folder it wanted when it isn't there."""
        import subprocess
        env = {k: v for k, v in os.environ.items() if k != "SCORECARD_REF"}
        env["FM1_REF"] = os.path.join(self.tmp.name, "no-such-ref")
        r = subprocess.run(["sh", os.path.join(self.repo, "tools", "scorecard.sh"), "HEAD"], env=env,
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        self.assertIn(os.path.join(env["FM1_REF"], "pg8x", "scorecard", "v5"), r.stderr)

    def test_a_reference_made_from_other_midi_is_refused(self):
        import json
        bad = os.path.join(self.tmp.name, "badref")
        os.makedirs(bad, exist_ok=True)
        with open(os.path.join(bad, "manifest.json"), "w") as fh:
            json.dump({"midi_sha256": "0" * 64}, fh)
        with self.assertRaises(SystemExit):
            self.sc.run(self.repo, "deadbee", bad, self.hist, work=os.path.join(self.tmp.name, "w2"))


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard): tools/scorecard.py imports them")
class BaseNoteTest(unittest.TestCase):
    """A head that lacks commits its base has isn't a fair comparison: the note names the merge base."""

    def setUp(self):
        import subprocess
        self.root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
        have = lambda c: subprocess.run(["git", "-C", self.root, "cat-file", "-e", c + "^{commit}"],
                                        capture_output=True).returncode == 0
        if not all(have(c) for c in ("47823da", "f1d85f4", "a254bee")):
            self.skipTest("needs the repo's history (a full clone)")
        sys.path.insert(0, os.path.join(self.root, "tools"))
        import scorecard
        self.sc = scorecard

    def test_a_head_behind_its_base_is_pointed_at_the_merge_base(self):
        note = self.sc.base_note(self.root, "47823da", "f1d85f4")
        self.assertIn("lacks 5 commits", note)
        self.assertIn("ebbbad9", note)

    def test_a_head_on_its_base_needs_no_note(self):
        self.assertIsNone(self.sc.base_note(self.root, "f1d85f4", "a254bee"))


class ScoreMidiTest(unittest.TestCase):
    """The scorecard MIDI (tools/gate_midi.py score()) puts a note under every section scorecard.py reads."""

    def test_no_note_starts_before_pg8xs_start_up_burst_is_over(self):
        """PG-8X 2.0.13 bursts on a note in its render's first 0.2 s and glides its settings from
        power-on values over about 0.1 s (#146, #163): the first note starts 0.25 s in."""
        import gate_midi
        self.assertGreaterEqual(min(s for _, s, _, _ in gate_midi.score()), 0.25)

    def test_each_section_lies_inside_its_note(self):
        import gate_midi
        notes = gate_midi.score()
        want = {"chord": (60, 100), "v110": (67, 110), "v60": (72, 60), "v90": (71, 90), "v40": (69, 40),
                "low": (36, 100), "high": (84, 100)}
        src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "scorecard.py")).read()
        for name, (a, b) in {"chord": (0.55, 3.15), "v110": (4.38, 4.95), "v60": (5.18, 5.75), "v90": (5.98, 6.55),
                             "v40": (6.78, 7.35), "low": (8.05, 9.35), "high": (10.25, 11.25)}.items():
            self.assertIn(f'"{name}": ({a}, {b})', src)
            note, vel = want[name]
            self.assertTrue(any(n == note and v == vel and s <= a and b <= e + 0.001 for n, s, e, v in notes),
                            (name, a, b))

    @unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
    def test_every_section_is_long_enough_to_read(self):
        """A section reads one Hann-windowed spectrum. At 170 ms (v90, s10) its gap read 5.0 dB on the 32
        factory tones where v40 cut to the same 170 ms read 4.7 and in full 3.1, and PG-8X against itself
        10 ms later read 0.96 there against 0.06 on the chord: the window's length, not the sound
        (2026-11-23). Every section is 0.5 s or longer (s11)."""
        import scorecard as sc
        for name, (a, b) in sc.SECTIONS.items():
            self.assertGreaterEqual(round(b - a, 3), 0.5, name)

    @unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
    def test_each_section_starts_past_pg8xs_onset_thump(self):
        """PG-8X 2.0.13 and 3.0.0 put a sub-230 Hz thump near -32 dBFS on every note on, a voice's first or
        not, that dies down over about 120 ms (#146, measured again 2026-11-15): no section reads it."""
        import gate_midi
        import scorecard as sc
        want = {"chord": 60, "v110": 67, "v60": 72, "v90": 71, "v40": 69, "low": 36, "high": 84}
        for name, (a, b) in sc.SECTIONS.items():
            on = max(s for n, s, e, _ in gate_midi.score() if n == want[name] and s <= a < e)
            self.assertGreaterEqual(round(a - on, 3), 0.13, name)


class ScriptBaseTest(unittest.TestCase):
    """tools/scorecard.sh stops on a BASE that is not a commit, before it builds anything (#335: it passed
    --base "" and printed a report with nothing compared). Run from a scratch repo holding only the
    script, so a regression can't build into this checkout."""

    def test_a_base_that_is_not_a_commit_stops_it(self):
        self.run_it(lambda t: os.environ)

    def test_and_a_git_hook_environment_reaches_neither_git_nor_the_script(self):
        # A hook's GIT_DIR and GIT_INDEX_FILE, aimed at a bait repo (never the real one): it has a commit
        # tagged as the BASE, so a script that read GIT_DIR would find it and not stop; and its HEAD and
        # the index file must be as they were.
        import subprocess
        state = {}

        def outer(t):
            bait = os.path.join(t, "bait")
            os.makedirs(bait)
            g = lambda *a: subprocess.run(["git", "-C", bait, *a], check=True, capture_output=True, text=True,
                                          env={k: v for k, v in os.environ.items() if not k.startswith("GIT_")})
            g("init", "-q")
            open(os.path.join(bait, "keep"), "w").write("bait\n")
            g("add", ".")
            g("-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", "commit", "-q", "-m", "bait")
            g("tag", "no-such-commit-335")
            state.update(g=g, head=g("rev-parse", "HEAD").stdout, index=os.path.join(t, "hook-index"))
            return dict(os.environ, GIT_DIR=os.path.join(bait, ".git"), GIT_INDEX_FILE=state["index"])

        self.run_it(outer, after=lambda: (
            self.assertEqual(state["g"]("rev-parse", "HEAD").stdout, state["head"], "a commit reached the bait"),
            self.assertFalse(os.path.exists(state["index"]), "an index was written at the hook's path")))

    def run_it(self, outer_env, after=lambda: None):
        import shutil
        import subprocess
        import tempfile
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        with tempfile.TemporaryDirectory() as t:
            outer = outer_env(t)
            root, ref = os.path.join(t, "repo"), os.path.join(t, "ref")
            os.makedirs(os.path.join(root, "tools"))
            os.makedirs(ref)
            shutil.copy(os.path.join(here, "tools/scorecard.sh"), os.path.join(root, "tools"))
            clean = {k: v for k, v in outer.items() if not k.startswith("GIT_")}   # (a hook's GIT_DIR, GIT_INDEX_FILE)
            git = lambda *a: subprocess.run(["git", "-C", root, *a], check=True, capture_output=True, env=clean)
            git("init", "-q")
            git("add", ".")
            git("-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false", "commit", "-q", "-m", "t")
            open(os.path.join(ref, "manifest.json"), "w").write("{}")
            env = dict(clean, SCORECARD_REF=ref, SCORECARD_HISTORY=os.path.join(ref, "h.jsonl"))
            p = subprocess.run(["sh", os.path.join(root, "tools/scorecard.sh"), "HEAD", "no-such-commit-335"],
                               env=env, capture_output=True, text=True, timeout=120)
            built = os.path.exists(os.path.join(root, "build"))
            self.assertEqual(p.returncode, 2, p.stderr[-500:])
            self.assertIn("no-such-commit-335: not a commit", p.stderr)
            self.assertFalse(built, "it archived or built before checking BASE")
            after()


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class MainBaseTest(unittest.TestCase):
    """main() with --base: a base with no rows in the history is told, and is exit 2 (#335: it printed
    "no base run to compare with" and exited 0), after the head's rows are kept."""

    def test_a_base_with_no_rows_is_exit_2_and_the_head_is_kept(self):
        self.main_with_base("nobase")

    def test_an_empty_base_is_exit_2_not_no_base(self):
        self.main_with_base("")

    def main_with_base(self, base):
        import contextlib
        import io
        import tempfile
        import scorecard as sc
        with tempfile.TemporaryDirectory() as t:
            hist = os.path.join(t, "h.jsonl")
            row = sc.tone_row("head1", "2026-11-21T00:00:00-05:00", 1, "T1",
                              {"spectral_db_mean": 3.0, "loudness_gap_lu": 0.0, "spectral_db": {"chord": 3.0},
                               "timing_gap_ms": {}, "pitch_cents": {"low": 0.0, "high": 0.0}}, "r")

            def fake_run(tree, commit, ref, history, *a, **k):
                sc.append_rows(history, [row])
                return [row]
            real, sc.run = sc.run, fake_run
            out = io.StringIO()
            try:
                with contextlib.redirect_stdout(out), self.assertRaises(SystemExit) as e:
                    sc.main(["--tree", t, "--commit", "head1", "--ref", t, "--history", hist, "--base", base])
            finally:
                sc.run = real
            self.assertEqual(e.exception.code, 2)
            self.assertIn("no rows", out.getvalue())
            self.assertEqual(len(open(hist).read().splitlines()), 1)


if __name__ == "__main__":
    unittest.main()

