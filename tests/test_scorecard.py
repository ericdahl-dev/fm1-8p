"""Tests for tools/scorecard.py, the factory-tone scorecard (fm1-8p against PG-8X 2.0.13).
Needs numpy and scipy; skipped where they aren't installed (make check-scorecard runs it with
SCORECARD_PYTHON, a Python that has them)."""
import importlib.util
import json
import os
import sys
import unittest

HAVE_NUMPY = all(importlib.util.find_spec(m) for m in ("numpy", "scipy"))
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

    # PG-8X's side lands on our renderer's grid: engine.c to_int16 floors (an arithmetic >> 8) and clips
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
        self.assertEqual(row["schema"], 10)
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
        self.assertEqual((row["schema"], row["kind"], row["param"], row["group"]), (10, "param", "VCF_KEY", "VCF"))
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
    """The whole run on this checkout, against a "reference" of its own renders: every tone scores 0."""

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
        for i, path in scorecard.render_tones(cls.repo, mid, work):
            os.replace(path, os.path.join(cls.ref, f"{i:02d}.wav"))
        scorecard.write_manifest(cls.ref, mid)

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

    def test_another_bank_is_scored_and_its_rows_say_so(self):
        import json
        import shutil
        bank = os.path.join(self.tmp.name, "other.syx")
        shutil.copy(os.path.join(self.repo, "data", "jx8p-factory-1-32.syx"), bank)
        ref = os.path.join(self.tmp.name, "bankref")
        shutil.copytree(self.ref, ref)
        mid = self.sc.write_midi(os.path.join(self.tmp.name, "w3"))
        self.sc.write_manifest(ref, mid, bank)
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
        for name, (a, b) in {"chord": (0.55, 3.15), "v110": (4.38, 4.65), "v60": (4.88, 5.15), "v90": (5.38, 5.55),
                             "v40": (5.78, 6.25), "low": (6.95, 8.25), "high": (9.15, 10.15)}.items():
            self.assertIn(f'"{name}": ({a}, {b})', src)
            note, vel = want[name]
            self.assertTrue(any(n == note and v == vel and s <= a and b <= e + 0.001 for n, s, e, v in notes),
                            (name, a, b))

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
        import shutil
        import subprocess
        import tempfile
        here = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
        with tempfile.TemporaryDirectory() as t:
            root, ref = os.path.join(t, "repo"), os.path.join(t, "ref")
            os.makedirs(os.path.join(root, "tools"))
            os.makedirs(ref)
            shutil.copy(os.path.join(here, "tools/scorecard.sh"), os.path.join(root, "tools"))
            git = lambda *a: subprocess.run(["git", "-C", root, *a], check=True, capture_output=True)
            git("init", "-q")
            git("add", ".")
            git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "t")
            open(os.path.join(ref, "manifest.json"), "w").write("{}")
            env = dict(os.environ, SCORECARD_REF=ref, SCORECARD_HISTORY=os.path.join(ref, "h.jsonl"))
            p = subprocess.run(["sh", os.path.join(root, "tools/scorecard.sh"), "HEAD", "no-such-commit-335"],
                               env=env, capture_output=True, text=True, timeout=120)
            built = os.path.exists(os.path.join(root, "build"))
        self.assertEqual(p.returncode, 2, p.stderr[-500:])
        self.assertIn("no-such-commit-335: not a commit", p.stderr)
        self.assertFalse(built, "it archived or built before checking BASE")


@unittest.skipUnless(HAVE_NUMPY, "needs numpy and scipy (make check-scorecard)")
class MainBaseTest(unittest.TestCase):
    """main() with --base: a base with no rows in the history is told, and is exit 2 (#335: it printed
    "no base run to compare with" and exited 0), after the head's rows are kept."""

    def test_a_base_with_no_rows_is_exit_2_and_the_head_is_kept(self):
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
                    sc.main(["--tree", t, "--commit", "head1", "--ref", t, "--history", hist, "--base", "nobase"])
            finally:
                sc.run = real
            self.assertEqual(e.exception.code, 2)
            self.assertIn("no rows", out.getvalue())
            self.assertEqual(len(open(hist).read().splitlines()), 1)


if __name__ == "__main__":
    unittest.main()

