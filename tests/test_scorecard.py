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

    # A tone's score on the scorecard MIDI (tools/gate_midi.py score.mid): against itself, all zero.
    def test_a_render_scored_against_itself_is_all_zero(self):
        np = self.np
        rng = np.random.default_rng(2)
        x = rng.standard_normal((13 * SR, 2)) * np.exp(-np.arange(13 * SR) / SR)[:, None] * 0.1
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
        self.assertEqual(row["schema"], 3)
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
        self.assertEqual((row["schema"], row["kind"], row["param"], row["group"]), (3, "param", "VCF_KEY", "VCF"))
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
        old = {"schema": 2, "kind": "tone", "commit": "abc", "date": "2026-11-13T00:00:00-05:00", "tone": 1,
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

    def test_a_reference_made_from_other_midi_is_refused(self):
        import json
        bad = os.path.join(self.tmp.name, "badref")
        os.makedirs(bad, exist_ok=True)
        with open(os.path.join(bad, "manifest.json"), "w") as fh:
            json.dump({"midi_sha256": "0" * 64}, fh)
        with self.assertRaises(SystemExit):
            self.sc.run(self.repo, "deadbee", bad, self.hist, work=os.path.join(self.tmp.name, "w2"))


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

    def test_each_section_lies_inside_its_note(self):
        import gate_midi
        notes = gate_midi.score()
        want = {"chord": (60, 100), "v110": (67, 110), "v60": (72, 60), "v90": (71, 90), "v40": (69, 40),
                "low": (36, 100), "high": (84, 100)}
        src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools", "scorecard.py")).read()
        for name, (a, b) in {"chord": (0.3, 2.9), "v110": (4.03, 4.4), "v60": (4.53, 4.9), "v90": (5.03, 5.3),
                             "v40": (5.43, 6.0), "low": (6.7, 8.0), "high": (8.9, 9.9)}.items():
            self.assertIn(f'"{name}": ({a}, {b})', src)
            note, vel = want[name]
            self.assertTrue(any(n == note and v == vel and s <= a and b <= e + 0.001 for n, s, e, v in notes),
                            (name, a, b))


if __name__ == "__main__":
    unittest.main()
