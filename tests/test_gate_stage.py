"""Tests for tools/gate_stage.py, the CPU rows' staging edits (#335): what GNU sed did, on macOS too."""
import os
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gate_stage as S  # noqa: E402

CPU_TEST = "firmware/cpu_test.c"
CPU_MOD = "firmware/fm1_8p_cpu/fm1_8p_cpu.c"
HP = "    {FM_PARAM_HIGHPASS_STEP, FM_FILTER_HIGHPASS_BOOST},\n"
HN = ("static const uint8_t CHORD[FM_MAX_VOICES] = {115, 117, 119, 121, 122, 123, 124, 125};"
      "   /* #269: the high notes */\n")


class StageTest(unittest.TestCase):
    """stage(DIR, MODE, BYTES, PAD): DIR, a copy of the tree, edited for one CPU row at one layout."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        for f in (CPU_TEST, CPU_MOD, "engine/fm1_8p.h"):
            os.makedirs(os.path.dirname(os.path.join(self.d, f)), exist_ok=True)
            shutil.copy(os.path.join(ROOT, f), os.path.join(self.d, f))

    def read(self, f):
        with open(os.path.join(self.d, f)) as fh:
            return fh.read()

    def orig(self, f):
        with open(os.path.join(ROOT, f)) as fh:
            return fh.read()

    def stage(self, mode, nbytes=0):
        S.stage(self.d, mode, nbytes, os.path.join(ROOT, "tools", "gate_pad.c"))

    def test_plain_at_layout_0_changes_nothing(self):
        self.stage("plain")
        self.assertEqual(self.read(CPU_TEST), self.orig(CPU_TEST))
        self.assertEqual(self.read(CPU_MOD), self.orig(CPU_MOD))
        self.assertFalse(os.path.exists(os.path.join(self.d, "engine", "aa_pad.c")))

    def test_sync_adds_sync_after_the_highpass_line(self):
        self.stage("sync")
        self.assertEqual(self.read(CPU_TEST), self.orig(CPU_TEST).replace(HP, HP + "    {FM_PARAM_OSC_SYNC, 1},\n"))

    def test_xmod_adds_the_ring(self):
        self.stage("xmod")
        self.assertEqual(self.read(CPU_TEST), self.orig(CPU_TEST).replace(HP, HP + "    {FM_PARAM_OSC_RING, 1},\n"))

    def test_ring_and_seq_add_sync_then_the_ring(self):
        both = HP + "    {FM_PARAM_OSC_SYNC, 1},\n    {FM_PARAM_OSC_RING, 1},\n"
        for mode in ("ring", "seq"):
            self.setUp()
            self.stage(mode)
            self.assertEqual(self.read(CPU_TEST), self.orig(CPU_TEST).replace(HP, both), mode)

    def test_ui_is_plain(self):
        self.stage("ui")
        self.assertEqual(self.read(CPU_TEST), self.orig(CPU_TEST))

    def test_a_high_note_row_has_the_high_chord_and_its_mode_s_patch(self):
        self.stage("ring-hn")
        lines = self.read(CPU_TEST).splitlines(True)
        self.assertIn(HN, lines)
        self.assertEqual([x for x in lines if x.startswith("static const uint8_t CHORD")], [HN])
        self.assertIn(HP + "    {FM_PARAM_OSC_SYNC, 1},\n    {FM_PARAM_OSC_RING, 1},\n", self.read(CPU_TEST))

    def test_a_layout_s_dead_code_is_that_many_bytes_and_linked(self):
        self.stage("plain", 2048)
        pad = self.read("engine/aa_pad.c")
        self.assertEqual(pad, self.orig("tools/gate_pad.c").replace('".space 3072"', '".space 2048"'))
        mod = self.read(CPU_MOD)
        self.assertEqual(mod, self.orig(CPU_MOD)
                         .replace("static int init(const hb_host_t *h)\n",
                                  "int32_t fm1_8p_gate_pad(int32_t x);\nstatic int init(const hb_host_t *h)\n")
                         .replace("    H = h;\n", "    H = h;\n    if (fm1_8p_gate_pad(3) == 7) return 1;\n"))

    def test_no_chord_stops_it_saying_so(self):
        with open(os.path.join(self.d, CPU_TEST), "w") as f:
            f.write(self.orig(CPU_TEST).replace("static const uint8_t CHORD", "static uint8_t CHORD"))
        with self.assertRaisesRegex(S.StageError, "step F's chord not found"):
            self.stage("plain-hn")

    def test_two_highpass_lines_stop_it(self):
        with open(os.path.join(self.d, CPU_TEST), "w") as f:
            f.write(self.orig(CPU_TEST).replace(HP, HP + HP))
        with self.assertRaisesRegex(S.StageError, "step F's patch found 2 times"):
            self.stage("sync")

    def test_no_pad_hook_stops_it(self):
        with open(os.path.join(self.d, CPU_MOD), "w") as f:
            f.write(self.orig(CPU_MOD).replace("    H = h;\n", "    H  = h;\n"))
        with self.assertRaisesRegex(S.StageError, "pad hook not found"):
            self.stage("plain", 1024)

    def test_an_unknown_mode_stops_it(self):
        with self.assertRaises(S.StageError):
            self.stage("plian")


if __name__ == "__main__":
    unittest.main()
