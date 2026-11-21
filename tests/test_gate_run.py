"""Tests for tools/gate_run.py, the gate's keys and verdict (#335): one place builds every cache key."""
import os
import shutil
import sys
import tempfile
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gate_run as R  # noqa: E402


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        f.write(text)


class RenderKeyTest(unittest.TestCase):
    """A tree's render key: its sources, the case list and every MIDI file the run wrote, the host compiler."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        self.tree = os.path.join(self.d, "tree")
        for f in ("Makefile", "engine/a.c", "host/m.c", "firmware/f.c", "tools/t.py", "data/x", "examples/e"):
            write(os.path.join(self.tree, f), f)
        self.root = os.path.join(self.d, "root")
        write(os.path.join(self.root, "tools", "gate_cases.py"), "cases")
        self.midi = os.path.join(self.d, "midi")
        write(os.path.join(self.midi, "demo.mid"), "demo")
        write(os.path.join(self.midi, "chords.mid"), "chords")
        self.cc = "cc version 1"

    def key(self):
        return R.render_key(self.tree, self.root, self.midi, cc=self.cc)

    def test_the_same_inputs_give_the_same_key(self):
        self.assertEqual(self.key(), self.key())
        self.assertRegex(self.key(), r"^[0-9a-f]{16}$")

    def test_a_midi_file_changed_changes_it(self):
        k = self.key()
        write(os.path.join(self.midi, "chords.mid"), "chords, another")
        self.assertNotEqual(self.key(), k)

    def test_a_midi_file_added_changes_it(self):
        k = self.key()
        write(os.path.join(self.midi, "wide.mid"), "wide")
        self.assertNotEqual(self.key(), k)

    def test_the_case_list_changes_it(self):
        k = self.key()
        write(os.path.join(self.root, "tools", "gate_cases.py"), "cases, another")
        self.assertNotEqual(self.key(), k)

    def test_a_source_changes_it(self):
        k = self.key()
        write(os.path.join(self.tree, "engine", "a.c"), "a, another")
        self.assertNotEqual(self.key(), k)

    def test_the_compiler_changes_it(self):
        k = self.key()
        self.cc = "cc version 2"
        self.assertNotEqual(self.key(), k)

    def test_an_older_tree_without_a_folder_has_a_key_its_own(self):
        k = self.key()
        shutil.rmtree(os.path.join(self.tree, "data"))
        k2 = self.key()
        self.assertRegex(k2, r"^[0-9a-f]{16}$")
        self.assertNotEqual(k2, k)

    def test_a_tree_without_any_source_is_an_error(self):
        shutil.rmtree(self.tree)
        os.makedirs(self.tree)
        with self.assertRaises(ValueError):
            self.key()

    def test_no_midi_is_an_error(self):
        shutil.rmtree(self.midi)
        os.makedirs(self.midi)
        with self.assertRaises(ValueError):
            self.key()


class CpuKeyTest(unittest.TestCase):
    """A tree's CPU rows' key: its sources, gate_cpu.sh, the pad, the Python that stages, reads and judges
    the rows, the toggles (their defaults, gate_cpu.sh's) and the FM-1 tools' identity."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        self.tree = os.path.join(self.d, "tree")
        write(os.path.join(self.tree, "engine", "a.c"), "a")
        self.root = os.path.join(self.d, "root")
        for f in ("gate_cpu.sh", "gate_pad.c", "gate_stage.py", "gate_cpu_records.py", "gate_record.py"):
            write(os.path.join(self.root, "tools", f), f)
        self.env = {}
        self.tools = {"fm1-live": "live sha256 aa", "jieli clang": "clang 1"}

    def key(self):
        return R.cpu_key(self.tree, self.root, os.path.join(self.root, "tools", "gate_cpu.sh"), self.env, self.tools)

    def test_the_same_inputs_give_the_same_key(self):
        self.assertEqual(self.key(), self.key())

    def test_each_file_of_the_rows_changes_it(self):
        for f in ("gate_cpu.sh", "gate_pad.c", "gate_stage.py", "gate_cpu_records.py", "gate_record.py"):
            k = self.key()
            write(os.path.join(self.root, "tools", f), f + ", another")
            self.assertNotEqual(self.key(), k, f)

    def test_each_toggle_changes_it(self):
        for name, v in (("GATE_CPU_LAYOUTS", "0 512"), ("GATE_CPU_HINOTE", "0"), ("GATE_CPU_LIMIT", "50")):
            k = self.key()
            self.env[name] = v
            self.assertNotEqual(self.key(), k, name)

    def test_a_toggle_at_its_default_is_the_toggle_unset(self):
        k = self.key()
        self.env.update(GATE_CPU_LAYOUTS="0 1024 2048 3072", GATE_CPU_HINOTE="1", GATE_CPU_LIMIT="64.8")
        self.assertEqual(self.key(), k)

    def test_a_tool_changes_it(self):
        k = self.key()
        self.tools["fm1-live"] = "live sha256 bb"
        self.assertNotEqual(self.key(), k)


class Fm1ToolsTest(unittest.TestCase):
    """gate_cpu.sh --tools: the FM-1 tools' identity for the CPU key, from the inputs gate_cpu.sh resolves."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        self.live = os.path.join(self.d, "fm1-live")
        write(self.live, "a live binary")
        self.tc = os.path.join(self.d, "tc")
        clang = os.path.join(self.tc, "pi32v2", "bin", "clang")
        write(clang, "#!/bin/sh\necho 'clang version 9.9 (jieli)'\n")
        os.chmod(clang, 0o755)
        self.dock = os.path.join(self.d, "dock")
        os.makedirs(self.dock)
        self.sdk = os.path.join(self.d, "sdk")
        os.makedirs(self.sdk)

    def test_each_tool_by_its_identity(self):
        t = R.fm1_tools(self.live, self.tc, self.dock)
        self.assertEqual(t["fm1-live"], R.G.binary_identity(self.live))
        self.assertEqual(t["jieli-clang"], "clang version 9.9 (jieli)")
        self.assertEqual(t["fm1-dock"], "not a git checkout")

    def test_a_rebuilt_fm1_live_is_another_tool(self):
        a = R.fm1_tools(self.live, self.tc, self.dock)["fm1-live"]
        write(self.live, "rebuilt")
        self.assertNotEqual(R.fm1_tools(self.live, self.tc, self.dock)["fm1-live"], a)

    def test_a_clang_that_fails_stops_it(self):
        write(os.path.join(self.tc, "pi32v2", "bin", "clang"), "#!/bin/sh\nexit 1\n")
        with self.assertRaises(OSError):
            R.fm1_tools(self.live, self.tc, self.dock)

    def test_gate_cpu_sh_prints_them(self):
        import json
        import subprocess
        env = dict(os.environ, FM1_LIVE=self.live, JIELI_TOOLCHAIN=self.tc, FM1_DOCK=self.dock, AC79_SDK=self.sdk)
        env.pop("FM1_REF", None)
        p = subprocess.run(["sh", os.path.join(ROOT, "tools", "gate_cpu.sh"), "--tools"], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual(json.loads(p.stdout), R.fm1_tools(self.live, self.tc, self.dock))

    def test_gate_cpu_sh_without_its_inputs_stops_as_ever(self):
        import subprocess
        env = {k: v for k, v in os.environ.items() if k not in ("FM1_REF", "FM1_LIVE", "JIELI_TOOLCHAIN",
                                                                 "FM1_DOCK", "AC79_SDK")}
        p = subprocess.run(["sh", os.path.join(ROOT, "tools", "gate_cpu.sh"), "--tools"], env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(p.returncode, 2)
        self.assertIn(b"is not set or missing", p.stderr)


if __name__ == "__main__":
    unittest.main()
