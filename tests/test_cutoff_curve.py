"""Tests for tools/cutoff_curve.py, VCF FREQ's cutoff curve written once (#344), and the header
generated from it (run by `make check-patches`)."""
import os
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import cutoff_curve  # noqa: E402
import jx8p  # noqa: E402


class CurveTest(unittest.TestCase):
    # the converter plays VCF FREQ through the one curve: rounded, and held at note 0 below it
    def test_the_converter_plays_the_one_curve(self):
        for v in range(128):
            self.assertEqual(jx8p.cutoff_cents(v), max(0, jx8p.rnd(cutoff_curve.cents(v))), v)
        self.assertEqual(jx8p.LINEAR_CENTS_PER_STEP, cutoff_curve.cents_per_step())

    # Migration check for #344, to be removed once it merges: the generated constants are the ones
    # engine/filter.c typed by hand before, so the engine's sound doesn't change
    def test_the_generated_constants_are_the_ones_typed_before(self):
        self.assertEqual(cutoff_curve.zero(), -1474)
        self.assertEqual(cutoff_curve.a_q32(), 32070)
        self.assertEqual(cutoff_curve.a_q32() // 2, 16035)

    # the header holds the constants it's generated from, by name
    def test_the_header_defines_the_constants(self):
        h = cutoff_curve.emit_h()
        q = cutoff_curve.a_q32()
        for line in ("#define FM_CURVE_ZERO (%d)" % cutoff_curve.zero(), "#define FM_CURVE_A_Q32 %du" % q,
                     "#define FM_CURVE_A_Q32_HALF %du" % (q // 2), "#define FM_CURVE_A2_NUM 247",
                     "#define FM_CURVE_A2_DEN 2500", "#define FM_CURVE_A1_NUM 11503",
                     "#define FM_CURVE_A1_DEN 100"):
            self.assertIn(line, h)
        self.assertIn("engine/cutoff_curve.h", jx8p.generated_params())


if __name__ == "__main__":
    unittest.main()
