"""Tests for tools/params.py, the param list, and the files generated from it (run by
`make check-patches`)."""
import os
import re
import sys
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import jx8p  # noqa: E402
import params  # noqa: E402


class ListTest(unittest.TestCase):
    # ids are stable (the patch store and the gate depend on them): 0, 1, 2 ... in list order
    def test_ids_count_up_from_zero_in_order(self):
        self.assertEqual([p.id for p in params.PARAMS], list(range(len(params.PARAMS))))

    def test_enums_and_names_are_unique(self):
        for field in ("enum", "name"):
            values = [getattr(p, field) for p in params.PARAMS]
            self.assertEqual(len(values), len(set(values)), field)

    # every range evaluates, and the default lies in it
    def test_each_default_lies_in_its_range(self):
        for p in params.PARAMS:
            lo, hi, de = params.values(p, ROOT)
            self.assertLessEqual(lo, de, p.name)
            self.assertLessEqual(de, hi, p.name)


class GeneratedTest(unittest.TestCase):
    # the generated files are checked in: they must be what the list makes now
    def test_generated_files_are_current(self):
        for rel, text in jx8p.generated_params().items():
            with open(os.path.join(ROOT, rel)) as f:
                self.assertEqual(f.read(), text, "%s is stale: run make generate" % rel)

    # the C table and enum agree with the list: names, ids and ranges as the tools read them
    def test_the_c_table_holds_the_list(self):
        with open(os.path.join(ROOT, "engine", "params.c")) as f:
            text = re.sub(r"/\*.*?\*/", "", f.read(), flags=re.S)
        rows = re.findall(r"\{\s*FM_PARAM_(\w+)\s*,\s*\"([^\"]*)\"", text)
        self.assertEqual(rows, [(p.enum, p.name) for p in params.PARAMS])

    # jx8p's ranges come from the list
    def test_jx8p_ranges_are_the_lists(self):
        ranges = jx8p.engine_ranges(ROOT)
        for p in params.PARAMS:
            self.assertEqual(ranges[p.enum], params.values(p, ROOT)[:2], p.name)


if __name__ == "__main__":
    unittest.main()
