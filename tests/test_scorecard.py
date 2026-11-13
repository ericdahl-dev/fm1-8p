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

    def test_the_report_has_every_tone_the_worst_five_and_what_moved(self):
        head = self.rows([5, 30, 1, 25, 9, 40, 2])
        for r in head:
            r.update({"spectral_db": {"chord": r["spectral_db_mean"]}, "timing_gap_ms": {"low_decay20": 10.0},
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

    def test_a_reference_made_from_other_midi_is_refused(self):
        import json
        bad = os.path.join(self.tmp.name, "badref")
        os.makedirs(bad, exist_ok=True)
        with open(os.path.join(bad, "manifest.json"), "w") as fh:
            json.dump({"midi_sha256": "0" * 64}, fh)
        with self.assertRaises(SystemExit):
            self.sc.run(self.repo, "deadbee", bad, self.hist, work=os.path.join(self.tmp.name, "w2"))


class ScoreMidiTest(unittest.TestCase):
    """The scorecard MIDI (tools/gate_midi.py score()) puts a note under every section scorecard.py reads."""

    def test_each_section_lies_inside_its_note(self):
        import gate_midi
        notes = gate_midi.score()
        want = {"chord": (60, 100), "v110": (67, 110), "v60": (72, 60), "v90": (71, 90), "v40": (69, 40),
                "low": (36, 100), "high": (84, 100)}
        src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "scorecard.py")).read()
        for name, (a, b) in {"chord": (0.3, 2.9), "v110": (4.0, 4.4), "v60": (4.5, 4.9), "v90": (5.0, 5.3),
                             "v40": (5.4, 6.0), "low": (6.7, 8.0), "high": (8.9, 9.9)}.items():
            self.assertIn(f'"{name}": ({a}, {b})', src)
            note, vel = want[name]
            self.assertTrue(any(n == note and v == vel and s <= a and b <= e + 0.001 for n, s, e, v in notes),
                            (name, a, b))


if __name__ == "__main__":
    unittest.main()
