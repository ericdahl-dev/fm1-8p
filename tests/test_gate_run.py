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

    def key(self, env=None):
        return R.render_key(self.tree, self.root, self.midi, cc=self.cc, env=env or {})

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

    def test_the_cc_and_cflags_strings_change_it(self):
        k = self.key()
        for env in ({"CC": "cc -ffast-math"}, {"CFLAGS": "-O0"}, {"CFLAGS": ""}):
            self.assertNotEqual(self.key(env), k, env)

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
        clang = os.path.join(self.tc, "pi32v2", "bin", "clang")
        self.assertEqual(t["jieli-clang"], "clang version 9.9 (jieli); " + R.G.binary_identity(clang))
        self.assertEqual(t["fm1-dock"], "not a git checkout")

    def test_a_rebuilt_fm1_live_is_another_tool(self):
        a = R.fm1_tools(self.live, self.tc, self.dock)["fm1-live"]
        write(self.live, "rebuilt")
        self.assertNotEqual(R.fm1_tools(self.live, self.tc, self.dock)["fm1-live"], a)

    def test_a_rebuilt_clang_of_the_same_version_is_another_tool(self):
        a = R.fm1_tools(self.live, self.tc, self.dock)["jieli-clang"]
        clang = os.path.join(self.tc, "pi32v2", "bin", "clang")
        write(clang, "#!/bin/sh\necho 'clang version 9.9 (jieli)'\n# rebuilt\n")
        os.chmod(clang, 0o755)
        self.assertNotEqual(R.fm1_tools(self.live, self.tc, self.dock)["jieli-clang"], a)

    def git_dock(self):
        import subprocess
        g = ["git", "-C", self.dock, "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false"]
        env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
        subprocess.run(["git", "init", "-q", self.dock], check=True, env=env)
        write(os.path.join(self.dock, "dock.py"), "v1")
        subprocess.run(g + ["add", "."], check=True, env=env)
        subprocess.run(g + ["commit", "-q", "-m", "1"], check=True, env=env)

    def dock_id(self):
        return R.fm1_tools(self.live, self.tc, self.dock)["fm1-dock"]

    def test_a_dock_edited_twice_is_two_docks(self):
        self.git_dock()
        clean = self.dock_id()
        write(os.path.join(self.dock, "dock.py"), "v2")
        once = self.dock_id()
        write(os.path.join(self.dock, "dock.py"), "v3")
        twice = self.dock_id()
        self.assertEqual(len({clean, once, twice}), 3)

    def test_a_dock_s_new_file_counts_by_its_contents(self):
        self.git_dock()
        write(os.path.join(self.dock, "new.py"), "a")
        a = self.dock_id()
        write(os.path.join(self.dock, "new.py"), "b")
        self.assertNotEqual(self.dock_id(), a)

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


class ClipsTest(unittest.TestCase):
    """The clips compared byte for byte: pass, fail (differs), new, gone; the lines gate.sh printed."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        self.b, self.h = os.path.join(self.d, "b"), os.path.join(self.d, "h")

    def side(self, o, clips):
        os.makedirs(o, exist_ok=True)
        with open(os.path.join(o, ".cases"), "w") as f:
            for name, wav in clips:
                f.write(f"{name}\tdemo.mid\t--x 1\n")
                if wav is not None:
                    with open(os.path.join(o, name + ".wav"), "wb") as w:
                        w.write(wav)

    def clips(self):
        return R.clip_records(self.b, self.h)

    def test_identical_clips_pass(self):
        self.side(self.b, [("a", b"1"), ("b", b"2")])
        self.side(self.h, [("a", b"1"), ("b", b"2")])
        records, lines = self.clips()
        self.assertEqual([(r["name"], r["status"]) for r in records], [("a", "pass"), ("b", "pass")])
        self.assertEqual(lines, ["0 of 2 clips differ"])

    def test_a_clip_that_differs_fails(self):
        self.side(self.b, [("a", b"1"), ("b", b"2")])
        self.side(self.h, [("a", b"1"), ("b", b"3")])
        records, lines = self.clips()
        self.assertEqual(records[1]["status"], "fail")
        self.assertEqual(lines, ["differs: b", "1 of 2 clips differ"])

    def test_new_and_gone_in_gate_sh_s_order(self):
        self.side(self.b, [("a", b"1"), ("old", b"9"), ("b", b"2")])
        self.side(self.h, [("n1", b"5"), ("a", b"1"), ("b", b"3"), ("n2", b"6")])
        records, lines = self.clips()
        self.assertEqual(lines, ["new: n1", "differs: b", "new: n2", "gone: old",
                                 "1 of 2 clips differ (2 new, not compared)"])
        st = {r["name"]: r["status"] for r in records}
        self.assertEqual(st, {"n1": "new", "a": "pass", "b": "fail", "n2": "new", "old": "gone"})

    def test_a_backslash_in_a_name_is_that_name(self):
        self.side(self.b, [("back\\slash", b"1")])
        self.side(self.h, [("back\\slash", b"1")])
        self.assertEqual(self.clips()[1], ["0 of 1 clips differ"])

    def test_a_base_clip_listed_but_not_rendered_is_an_error(self):
        self.side(self.b, [("a", None)])
        self.side(self.h, [("a", b"1")])
        records, lines = self.clips()
        self.assertEqual(records[0]["status"], "error")
        self.assertIn("listed on the base but not rendered", records[0]["why"])

    def test_a_head_clip_listed_but_not_rendered_is_an_error_not_a_differ(self):
        self.side(self.b, [("a", b"1")])
        self.side(self.h, [("a", None)])
        records, lines = self.clips()
        self.assertEqual(records[0]["status"], "error")
        self.assertIn("listed on HEAD but not rendered", records[0]["why"])

    def test_nothing_on_both_trees_says_so(self):
        self.side(self.b, [("a", b"1")])
        self.side(self.h, [("b", b"1")])
        records, lines = self.clips()
        self.assertEqual(R.G.verdict([("clip", "b")], records, compared=("clip",))[0], 2)


class JudgeTest(unittest.TestCase):
    """judge: the gate's exit from its records, and why, when it could not run."""

    def setUp(self):
        self.d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self.d)
        with open(os.path.join(self.d, ".cases"), "w") as f:
            f.write("a\tdemo.mid\t\nb\tdemo.mid\t\n")

    def judge(self, records, cpu=False):
        import contextlib
        import io
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = R.judge(records, self.d, cpu)
        return code, err.getvalue()

    def rec(self, name, status, kind="clip"):
        return R.G.record(kind, name, status)

    def test_all_pass_is_0_and_quiet(self):
        self.assertEqual(self.judge([self.rec("a", "pass"), self.rec("b", "pass")]), (0, ""))

    def test_a_clip_that_differs_is_1(self):
        self.assertEqual(self.judge([self.rec("a", "pass"), self.rec("b", "fail")])[0], 1)

    def test_a_name_twice_is_2_and_says_so(self):
        code, err = self.judge([self.rec("a", "pass"), self.rec("a", "pass"), self.rec("b", "pass")])
        self.assertEqual(code, 2)
        self.assertIn("gate: clip a: two records for one name", err)

    def test_head_s_cpu_run_missing_is_2_and_says_so(self):
        code, err = self.judge([self.rec("a", "pass"), self.rec("b", "pass")], cpu=True)
        self.assertEqual(code, 2)
        self.assertIn("gate: cpu-run head: no record", err)

    def test_head_s_cpu_error_beats_a_differing_clip(self):
        code, _ = self.judge([self.rec("a", "fail"), self.rec("b", "pass"), self.rec("head", "error", "cpu-run")],
                             cpu=True)
        self.assertEqual(code, 2)


if __name__ == "__main__":
    unittest.main()
