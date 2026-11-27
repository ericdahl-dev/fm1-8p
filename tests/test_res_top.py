"""Tests for tools/res_top.py, the resonant top's tables (#168), and the header generated from it (run by
`make check-patches`)."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import jx8p  # noqa: E402
import res_top  # noqa: E402


class ResTopTest(unittest.TestCase):
    # the header in the tree is the one the tables generate: nobody edited either alone
    def test_the_header_is_the_generated_one(self):
        self.assertIn("engine/res_top_tables.h", jx8p.generated_params())
        with open(os.path.join(ROOT, "engine", "res_top_tables.h")) as f:
            self.assertEqual(f.read(), res_top.emit_h())

    # the knots the engine counts on: rising, the cutoff's from the blend's start (engine/filter.c RT_C0) to
    # the open cutoff, the resonance's from the handover's end (engine/filter.h FM_FILTER_TOP_RES_HI) to
    # VCF RES 127's 935
    def test_the_knots_span_the_top(self):
        self.assertEqual(list(res_top.CUTOFF), sorted(set(res_top.CUTOFF)))
        self.assertEqual(list(res_top.RESONANCE), sorted(set(res_top.RESONANCE)))
        self.assertEqual((res_top.CUTOFF[0], res_top.CUTOFF[-1]), (11500, 14400))
        self.assertEqual((res_top.RESONANCE[0], res_top.RESONANCE[-1]), (112, 935))
        self.assertEqual(res_top.RESONANCE[-1], jx8p.resonance_permille(127))
        self.assertEqual(res_top.CUTOFF[-1], jx8p.c_constants()["FM_FILTER_OPEN_CENTS"])

    # every value fits the engine's formats: int16, t under 6.72 (the tangent table's reach), k under 3.0 and
    # c at most 0.7, so that z3 = 2 c + c^2 stays under 2.0 in Q30
    def test_the_values_fit_the_engines_formats(self):
        for rate in (44100, 48000):
            for tab, shift, top in ((res_top.T, res_top.T_SHIFT, 6.72), (res_top.K, res_top.K_SHIFT, 3.0),
                                    (res_top.C, res_top.C_SHIFT, 0.7001)):
                self.assertEqual(len(tab[rate]), len(res_top.CUTOFF))
                for row in tab[rate]:
                    self.assertEqual(len(row), len(res_top.RESONANCE))
                    for v in row:
                        self.assertTrue(0 < v < 32768 and v / 2 ** shift < top, (rate, v))


if __name__ == "__main__":
    unittest.main()
