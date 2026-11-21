"""Tests for tools/gate_coverage.py, the gate's coverage (#140): a gcov that fails or lists nothing stops
it (exit 2, naming the reason), never a count of 0 for every function (#335). Apple's gcov (llvm-cov)
writes no JSON for -j: that is the second case, so a Mac says so instead of passing quietly."""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
SCRIPT = os.path.join(ROOT, "tools", "gate_coverage.py")


class GcovFailsTest(unittest.TestCase):
    def run_with(self, gcov_body, notes=True, clips=True, clip_data=True):
        with tempfile.TemporaryDirectory() as t:
            stub = os.path.join(t, "stub")
            objdir, srcdir, runs = (os.path.join(t, d) for d in ("obj", "src", "runs"))
            for d in (stub, objdir, os.path.join(srcdir, "engine"), runs):
                os.makedirs(d)
            if clips:
                os.makedirs(os.path.join(runs, "clip"))
            if notes:
                open(os.path.join(objdir, "osc.o.gcno"), "wb").close()
            open(os.path.join(objdir, "osc.o.gcda"), "wb").close()
            if clips and clip_data:
                open(os.path.join(runs, "clip", "osc.o.gcda"), "wb").close()
            open(os.path.join(srcdir, "engine", "osc.c"), "w").close()
            with open(os.path.join(stub, "gcov"), "w") as fh:
                fh.write("#!/bin/sh\n" + gcov_body + "\n")
            os.chmod(os.path.join(stub, "gcov"), 0o755)
            env = dict(os.environ, PATH=stub + os.pathsep + os.environ["PATH"])
            return subprocess.run([sys.executable, "-B", SCRIPT, objdir, srcdir, runs], env=env,
                                  capture_output=True, text=True, timeout=60)

    def test_a_gcov_that_fails_stops_it(self):
        p = self.run_with("echo 'gcov: unknown option -j' >&2; exit 1")
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("gcov", p.stderr)
        self.assertIn("unknown option -j", p.stderr)

    def test_a_gcov_that_writes_no_json_stops_it(self):
        p = self.run_with("exit 0")
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("no JSON for osc.c", p.stderr)
        self.assertNotIn("clips reaching", p.stdout)

    def test_no_engine_function_at_all_stops_it(self):
        p = self.run_with("exit 0", notes=False)
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("no engine function", p.stderr)


class NoRunDataTest(GcovFailsTest):
    """A gcov that works, writing JSON for osc.c with one function, but runs that hold no data."""
    GOOD = ("python3 -c 'import gzip, json, sys; "
            "gzip.open(\"osc.gcov.json.gz\", \"wt\").write(json.dumps({\"files\": [{\"file\": \"osc.c\", "
            "\"functions\": [{\"name\": \"f\"}], \"lines\": [{\"count\": 1, \"function_name\": \"f\"}]}]}))'")

    def test_with_data_it_counts(self):
        p = self.run_with(self.GOOD)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertIn("osc.c", p.stdout)

    def test_no_clip_folder_stops_it(self):
        p = self.run_with(self.GOOD, clips=False)
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("no clip", p.stderr)

    def test_a_clip_with_no_run_data_stops_it(self):
        p = self.run_with(self.GOOD, clip_data=False)
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("no run data", p.stderr)

    def test_json_without_its_source_stops_it(self):
        p = self.run_with(self.GOOD.replace('"file": "osc.c"', '"file": "other.c"'))
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("osc.c", p.stderr)


if __name__ == "__main__":
    unittest.main()
