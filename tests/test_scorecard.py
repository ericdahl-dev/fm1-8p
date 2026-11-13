"""Tests for tools/scorecard.py, the factory-tone scorecard (fm1-8p against PG-8X 2.0.13).
Needs numpy and scipy; skipped where they aren't installed (make check-scorecard runs it with
SCORECARD_PYTHON, a Python that has them)."""
import importlib.util
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

    # Envelope timing: ms from the section's peak until it is `drop` dB down (10 ms RMS frames).
    def test_fall_time_reads_an_exponential_decay(self):
        np = self.np
        t = np.arange(2 * SR) / SR
        x = np.zeros(3 * SR)
        x[SR // 2:SR // 2 + len(t)] = np.sin(2 * np.pi * 220 * t) * 10 ** (-20 * t / 0.3 / 20)
        self.assertAlmostEqual(self.sc.fall_ms(x, 0.0, 3.0, 20), 300, delta=15)
        self.assertIsNone(self.sc.fall_ms(x, 0.0, 0.6, 40))   # not 40 dB down inside the window

    # Pitch: the shift that best lines up the two log-frequency spectra, cents (+ = ours sharp).
    def test_pitch_offset_reads_cents(self):
        np = self.np
        t = np.arange(2 * SR) / SR
        saw = lambda hz: sum(np.sin(2 * np.pi * hz * k * t) / k for k in range(1, 12))
        self.assertAlmostEqual(self.sc.pitch_cents(saw(220 * 2 ** (25 / 1200)), saw(220)), 25, delta=5)
        self.assertAlmostEqual(self.sc.pitch_cents(saw(220), saw(220)), 0, delta=1)

    # A tone's score on the scorecard MIDI (tools/gate_midi.py score.mid): against itself, all zero.
    def test_a_render_scored_against_itself_is_all_zero(self):
        np = self.np
        rng = np.random.default_rng(2)
        x = rng.standard_normal((13 * SR, 2)) * np.exp(-np.arange(13 * SR) / SR)[:, None] * 0.1
        s = self.sc.score_tone(x, x)
        self.assertEqual(set(s), {"loudness_gap_lu", "spectral_db", "spectral_db_mean", "timing_gap_ms",
                                  "pitch_cents"})
        self.assertEqual(s["loudness_gap_lu"], 0.0)
        self.assertEqual(s["spectral_db_mean"], 0.0)
        self.assertTrue(all(v == 0.0 for v in s["spectral_db"].values()))
        self.assertTrue(all(v in (0.0, None) for v in s["timing_gap_ms"].values()))
        self.assertTrue(all(v == 0.0 for v in s["pitch_cents"].values()))


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
        self.assertEqual(row["schema"], 1)
        self.assertEqual(row["kind"], "tone")
        self.assertEqual((row["commit"], row["tone"], row["name"], row["ref"]), ("a254bee", 25, "SYNC LEAD", "ref-1"))
        self.assertEqual(row["loudness_gap_lu"], -1.5)

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
        self.assertEqual((row["schema"], row["kind"], row["param"], row["group"]), (1, "param", "VCF_KEY", "VCF"))
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

    def test_worst_five_are_the_largest_spectral_means(self):
        got = self.sc.worst(self.rows([5, 30, 1, 25, 9, 40, 2]), 5)
        self.assertEqual([r["tone"] for r in got], [6, 2, 4, 5, 1])


if __name__ == "__main__":
    unittest.main()
