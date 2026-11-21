"""Tests for tools/gate_cpu_records.py, the gate's CPU rows as records (#335)."""
import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gate_cpu_records as C  # noqa: E402

# A real fm1-emu log (black2, sync-hn at layout 0, 2026-11): the sweep 1..8, U (9) and F (10)
LOG = """\
489 fm1-dock 9e6e08e boot, reset 00000000
653 fm1-8p cpu: sweeping 1..8 voices, then the unison burst, then the full patch
1151 fm1-8p voices/worst permille 00010031
1221 cpu1: 2 cores, screened in us 0000b03e
1650 fm1-8p voices/worst permille 0002002c
2149 fm1-8p voices/worst permille 0003003d
2648 fm1-8p voices/worst permille 0004004d
3148 fm1-8p voices/worst permille 0005005e
3647 fm1-8p voices/worst permille 0006006f
4146 fm1-8p voices/worst permille 00070080
4645 fm1-8p voices/worst permille 00080091
5144 fm1-8p voices/worst permille 000900ee
5645 fm1-8p voices/worst permille 000a0206
---
active module 1; render: last 65810 ticks (47.2 %), worst 78735 (56.5 %), 0 overruns, 1121 blocks
"""
BUILD = "module FM1-8P CPU build/modules/fm1_8p_cpu_quick.dock: code 37412 B, data 0 B, bss 8292 B\n"


class LayoutTest(unittest.TestCase):
    """One layout's run: its figures read from fm1-emu's own log lines, not from the Markdown report."""

    def test_a_whole_run_is_a_pass_with_its_figures(self):
        r = C.layout_record("sync-hn", "0", LOG, BUILD, max_voices=8)
        self.assertEqual((r["kind"], r["name"], r["status"]), ("cpu-layout", "sync-hn@0", "pass"))
        self.assertEqual(r["value"], {"8": 14.5, "U": 23.8, "F": 51.8, "overruns": 0, "code_bytes": 37412,
                                      "layout": 0, "shipped": True})

    def test_only_layout_0_shipped(self):
        r = C.layout_record("plain", "1024", LOG, BUILD, max_voices=8)
        self.assertEqual((r["value"]["layout"], r["value"]["shipped"]), (1024, False))

    def test_its_line_is_the_row_main_printed(self):
        r = C.layout_record("sync-hn", "0", LOG, BUILD, max_voices=8)
        self.assertEqual(C.layout_line(r),
                         "sync-hn@0      8 =  14.5 %   U =  23.8 %   F =  51.8 %    0 overruns code 37412 B")

    def missing(self, log=LOG, build=BUILD, max_voices=8):
        r = C.layout_record("plain", "1024", log, build, max_voices=max_voices)
        self.assertEqual(r["status"], "missing")
        self.assertIsNone(r["value"])
        return r["why"]

    def test_a_run_cut_off_before_F_is_missing(self):
        self.assertIn("no F", self.missing(LOG.replace("000a0206", "")))

    def test_a_run_cut_off_before_U_is_missing(self):
        self.assertIn("no U", self.missing(LOG.replace("000900ee", "")))

    def test_a_log_with_no_voice_counts_is_missing(self):
        self.assertIn("no voice counts", self.missing("render: last 1 ticks (0.0 %), worst 1, 0 overruns\n"))

    def test_a_count_missing_mid_sweep_is_missing(self):
        self.assertIn("no 5", self.missing(LOG.replace("0005005e", "")))

    def test_a_log_with_no_render_meter_is_missing(self):
        self.assertIn("no render meter", self.missing(LOG.replace("render: last", "render: lost")))

    def test_a_build_with_no_code_size_is_missing(self):
        self.assertIn("no code size", self.missing(build="module FM1-8P CPU built\n"))

    def test_an_older_sweep_does_not_fill_the_last_one(self):
        # a sweep that restarted (count 1 again) and stopped at 8: the first sweep's U and F are not its
        cut = LOG.replace("---", "6000 fm1-8p voices/worst permille 00010031\n---")
        self.assertIn("no 2", self.missing(cut))

    def test_ten_overruns_read_as_ten(self):
        r = C.layout_record("plain", "0", LOG.replace(" 0 overruns", " 10 overruns"), BUILD, max_voices=8)
        self.assertEqual(r["value"]["overruns"], 10)


class MaxVoicesTest(unittest.TestCase):
    """The sweep's length comes from the tree's FM_MAX_VOICES: a tree without it can't be read."""

    def tree(self, header):
        import tempfile
        d = tempfile.mkdtemp()
        self.addCleanup(__import__("shutil").rmtree, d)
        os.makedirs(os.path.join(d, "engine"))
        if header is not None:
            with open(os.path.join(d, "engine", "fm1_8p.h"), "w") as f:
                f.write(header)
        return d

    def test_it_reads_the_define(self):
        self.assertEqual(C.max_voices(self.tree("#ifndef X\n#define FM_MAX_VOICES 8   /* voices */\n")), 8)

    def test_no_define_is_an_error(self):
        with self.assertRaises(ValueError):
            C.max_voices(self.tree("#define FM_MAX_VOICE 8\n"))

    def test_two_defines_are_an_error(self):
        with self.assertRaises(ValueError):
            C.max_voices(self.tree("#define FM_MAX_VOICES 8\n#define FM_MAX_VOICES 6\n"))

    def test_no_header_is_an_error(self):
        with self.assertRaises(OSError):
            C.max_voices(self.tree(None))

    def test_the_real_tree_has_eight(self):
        self.assertEqual(C.max_voices(ROOT), 8)


def lay(layout, f, row="plain", u=23.9, overruns=0, code=1000):
    v = {"8": 14.5, "U": u, "F": f, "overruns": overruns, "code_bytes": code}
    return C.G.record("cpu-layout", f"{row}@{layout}", "pass", value=v)


# The lines main's gate_cpu.sh (728f231) printed for these layouts (black2, through the build stub)
ROW = "plain      8 = 14.5 %   U = 23.9 %   F = 41.0 % (40.0..42.0, spread 2.0, 3 layouts)    0 overruns code 1000 B"


class RowTest(unittest.TestCase):
    """A row over its layouts: F's mean judged against the limit, a layout over it only a warning."""

    LAYS = [lay("0", 40.0), lay("1024", 41.0), lay("2048", 42.0)]

    def test_a_row_under_the_limit_passes_with_main_s_line(self):
        r, lines = C.row_record("plain", self.LAYS, 3, "64.8")
        self.assertEqual(r["status"], "pass")
        self.assertEqual(lines, [ROW])
        got = {k: round(v, 9) for k, v in r["value"].items()}   # (means are doubles: 23.899999...)
        self.assertEqual(got, {"F": 41.0, "F_min": 40.0, "F_max": 42.0, "spread": 2.0, "8": 14.5,
                                      "U": 23.9, "overruns": 0, "code_bytes": 1000, "layouts": 3,
                               "limit": 64.8})

    def test_a_mean_over_the_limit_fails(self):
        r, lines = C.row_record("plain", self.LAYS, 3, "40.5")
        self.assertEqual(r["status"], "fail")
        self.assertEqual(lines, [ROW, "plain: F mean 41.0 % over the limit 40.5 %"])

    def test_a_layout_over_the_limit_only_warns(self):
        r, lines = C.row_record("plain", self.LAYS, 3, "41.5")
        self.assertEqual(r["status"], "pass")
        self.assertEqual(lines, [ROW, "plain: max 42.0 % over the limit 41.5 % at layout 2048 (mean ok)"])

    def test_a_mean_at_the_limit_is_not_over_it(self):
        r, lines = C.row_record("plain", self.LAYS, 3, "41.0")
        self.assertEqual(r["status"], "pass")
        self.assertEqual(lines[1], "plain: max 42.0 % over the limit 41.0 % at layout 2048 (mean ok)")

    def test_a_layout_short_is_an_error(self):
        r, lines = C.row_record("plain", [self.LAYS[0], self.LAYS[2]], 3, "64.8")
        self.assertEqual(r["status"], "error")
        self.assertEqual(lines, ["plain: F at only 2 of 3 layouts"])

    def test_a_missing_layout_record_does_not_count(self):
        gone = C.G.record("cpu-layout", "plain@1024", "missing", why="no F in the log's last sweep")
        r, lines = C.row_record("plain", [self.LAYS[0], gone, self.LAYS[2]], 3, "64.8")
        self.assertEqual((r["status"], lines), ("error", ["plain: F at only 2 of 3 layouts"]))

    def test_no_layout_at_all_is_an_error_said_out_loud(self):
        r, lines = C.row_record("plain", [], 3, "64.8")
        self.assertEqual((r["status"], lines), ("error", ["plain: F at only 0 of 3 layouts"]))

    def test_the_first_layout_s_code_and_the_overruns_summed(self):
        lays = [lay("0", 40.0, code=900, overruns=1), lay("1024", 41.0, code=1100, overruns=2)]
        r, lines = C.row_record("plain", lays, 2, "64.8")
        self.assertEqual((r["value"]["code_bytes"], r["value"]["overruns"]), (900, 3))
        self.assertTrue(lines[0].endswith("    3 overruns code 900 B"))

    def test_the_first_highest_layout_is_named(self):
        lays = [lay("0", 42.0), lay("1024", 40.0), lay("2048", 42.0)]
        r, lines = C.row_record("plain", lays, 3, "41.5")
        self.assertIn("at layout 0 (mean ok)", lines[1])

    def test_a_limit_that_is_not_a_number_is_an_error(self):
        with self.assertRaises(ValueError):
            C.row_record("plain", self.LAYS, 3, "64,8")


if __name__ == "__main__":
    unittest.main()
