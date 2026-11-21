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
    def run_with(self, gcov_body, notes=True):
        with tempfile.TemporaryDirectory() as t:
            stub = os.path.join(t, "stub")
            objdir, srcdir, runs = (os.path.join(t, d) for d in ("obj", "src", "runs"))
            for d in (stub, objdir, os.path.join(srcdir, "engine"), os.path.join(runs, "clip")):
                os.makedirs(d)
            if notes:
                open(os.path.join(objdir, "osc.o.gcno"), "wb").close()
            open(os.path.join(objdir, "osc.o.gcda"), "wb").close()
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


if __name__ == "__main__":
    unittest.main()
