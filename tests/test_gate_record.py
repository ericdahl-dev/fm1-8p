"""Tests for tools/gate_record.py, the gate's results as records (#335)."""
import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))

import gate_record as G  # noqa: E402


class RecordTest(unittest.TestCase):
    """A record is one result; its status is one of a fixed set, and anything else is an error."""

    def test_a_record_reads_back_from_its_line(self):
        r = G.record("cpu", "plain", "pass", value={"F": 41.0}, key="k1")
        self.assertEqual(G.parse(G.line(r)), r)

    def test_an_unknown_status_is_an_error(self):
        with self.assertRaises(ValueError):
            G.record("cpu", "plain", "ok")

    def test_an_unknown_status_read_from_a_line_is_an_error(self):
        with self.assertRaises(ValueError):
            G.parse('{"kind": "cpu", "name": "plain", "status": "PASS", "value": null, "why": null, "key": null}')

    def test_a_line_missing_a_field_is_an_error(self):
        with self.assertRaises(ValueError):
            G.parse('{"kind": "cpu", "name": "plain", "status": "pass"}')

    def test_a_line_that_is_not_json_is_an_error(self):
        with self.assertRaises(ValueError):
            G.parse("plain      F =  1.0 %")


class LoadTest(unittest.TestCase):
    """A run's records file: one record a line; a line that is not one stops the reading, by its number."""

    def write(self, text):
        import tempfile
        fd, path = tempfile.mkstemp()
        self.addCleanup(os.remove, path)
        with os.fdopen(fd, "w") as fh:
            fh.write(text)
        return path

    def test_every_line_is_a_record(self):
        a, b = G.record("clip", "a", "pass"), G.record("cpu", "plain", "missing", why="no F")
        self.assertEqual(G.load(self.write(G.line(a) + "\n" + G.line(b) + "\n")), [a, b])

    def test_an_empty_file_is_no_records(self):
        self.assertEqual(G.load(self.write("")), [])

    def test_a_bad_line_stops_it_by_its_number(self):
        a = G.line(G.record("clip", "a", "pass"))
        with self.assertRaisesRegex(ValueError, ":2:"):
            G.load(self.write(a + "\n" + "plain      F =  1.0 %\n"))

    def test_a_blank_line_is_a_bad_line(self):
        with self.assertRaises(ValueError):
            G.load(self.write("\n"))


def rec(name, status, kind="clip"):
    return G.record(kind, name, status, why=None if status == "pass" else "test")


class VerdictTest(unittest.TestCase):
    """One rule: 2 (could not run) for any error or missing, 1 for any fail, else 0. Every expected name
    needs its record; no record is missing, never a pass."""

    def code(self, expected, records, **kw):
        return G.verdict(expected, records, **kw)[0]

    def test_every_expected_result_passing_is_0(self):
        self.assertEqual(self.code([("clip", "a"), ("clip", "b")], [rec("a", "pass"), rec("b", "pass")]), 0)

    def test_a_fail_is_1(self):
        self.assertEqual(self.code([("clip", "a"), ("clip", "b")], [rec("a", "pass"), rec("b", "fail")]), 1)

    def test_an_error_is_2_even_beside_a_fail(self):
        self.assertEqual(self.code([("clip", "a"), ("clip", "b")], [rec("a", "fail"), rec("b", "error")]), 2)

    def test_a_missing_record_is_2(self):
        self.assertEqual(self.code([("clip", "a")], [rec("a", "missing")]), 2)

    def test_an_expected_name_with_no_record_is_missing(self):
        code, problems = G.verdict([("clip", "a"), ("cpu", "plain")], [rec("a", "pass")])
        self.assertEqual(code, 2)
        self.assertIn(("cpu", "plain", "missing"), [(p["kind"], p["name"], p["status"]) for p in problems])

    def test_nothing_expected_is_2(self):
        self.assertEqual(self.code([], []), 2)

    def test_and_says_why(self):
        self.assertEqual([p["status"] for p in G.verdict([], [])[1]], ["error"])

    def test_a_record_missing_a_field_is_an_error(self):
        with self.assertRaises(ValueError):
            G.verdict([("clip", "a")], [{"kind": "clip", "name": "a", "status": "pass"}])

    def test_a_record_nobody_expected_is_an_error(self):
        self.assertEqual(self.code([("clip", "a")], [rec("a", "pass"), rec("b", "pass")]), 2)

    def test_but_a_gone_one_is_reported_not_an_error(self):
        code, problems = G.verdict([("clip", "a")], [rec("a", "pass"), rec("b", "gone")])
        self.assertEqual(code, 0)
        self.assertEqual([p["name"] for p in problems], ["b"])

    def test_two_records_for_one_name_is_an_error(self):
        self.assertEqual(self.code([("clip", "a")], [rec("a", "pass"), rec("a", "pass")]), 2)

    def test_new_and_skipped_are_reported_and_pass(self):
        code, problems = G.verdict([("clip", "a"), ("cpu", "plain")],
                                   [rec("a", "new"), rec("plain", "skipped", kind="cpu")])
        self.assertEqual(code, 0)
        self.assertEqual(sorted(p["status"] for p in problems), ["new", "skipped"])

    def test_a_required_kind_skipped_is_2(self):
        self.assertEqual(self.code([("cpu", "plain")], [rec("plain", "skipped", kind="cpu")], required=("cpu",)), 2)

    def test_a_status_changed_after_the_record_was_made_is_still_an_error(self):
        r = rec("a", "pass")
        r["status"] = "ok"
        with self.assertRaises(ValueError):
            G.verdict([("clip", "a")], [r])


class KeyTest(unittest.TestCase):
    """One cache key: the tree's sources, the named inputs (the clip list, the MIDI written for the run, the
    scripts), the toggles and the tools' identity. Anything it can't read stops it: an empty or partial key
    would let one tree reuse another's results."""

    def setUp(self):
        import tempfile
        self.tmp = tempfile.TemporaryDirectory()
        self.t = self.tmp.name
        self.tree = os.path.join(self.t, "tree")
        for d in ("tree/engine", "tree/tools", "midi"):
            os.makedirs(os.path.join(self.t, d))
        self.put("tree/Makefile", "all:\n")
        self.put("tree/engine/voice.c", "int x;\n")
        self.put("tree/tools/gate_cases.py", "# cases\n")
        self.put("midi/demo.mid", "MThd")

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, rel, text):
        with open(os.path.join(self.t, rel), "w") as fh:
            fh.write(text)

    def key(self, **kw):
        args = dict(dirs=("Makefile", "engine", "tools"), inputs={"midi": os.path.join(self.t, "midi/demo.mid")},
                    toggles={"GATE_CPU_HINOTE": "1"}, tools={"cc": "cc 1.0"})
        args.update(kw)
        return G.key(self.tree, **args)

    def test_the_same_inputs_give_the_same_16_hex_key(self):
        k = self.key()
        self.assertRegex(k, "^[0-9a-f]{16}$")
        self.assertEqual(self.key(), k)

    def test_a_source_change_changes_it(self):
        k = self.key()
        self.put("tree/engine/voice.c", "int y;\n")
        self.assertNotEqual(self.key(), k)

    def test_a_new_source_file_changes_it(self):
        k = self.key()
        self.put("tree/engine/osc.c", "")
        self.assertNotEqual(self.key(), k)

    def test_a_renamed_source_file_changes_it(self):
        k = self.key()
        os.rename(os.path.join(self.tree, "engine/voice.c"), os.path.join(self.tree, "engine/voice2.c"))
        self.assertNotEqual(self.key(), k)

    def test_the_midi_changes_it(self):
        k = self.key()
        self.put("midi/demo.mid", "MThd2")
        self.assertNotEqual(self.key(), k)

    def test_a_toggle_changes_it(self):
        self.assertNotEqual(self.key(toggles={"GATE_CPU_HINOTE": "0"}), self.key())

    def test_a_tool_changes_it(self):
        self.assertNotEqual(self.key(tools={"cc": "cc 1.1"}), self.key())

    def test_a_python_cache_does_not(self):
        k = self.key()
        os.makedirs(os.path.join(self.tree, "tools/__pycache__"))
        self.put("tree/tools/__pycache__/gate_cases.cpython-315.pyc", "x")
        self.assertEqual(self.key(), k)

    def test_an_input_it_cannot_read_stops_it(self):
        with self.assertRaises(OSError):
            self.key(inputs={"midi": os.path.join(self.t, "midi/none.mid")})

    def test_a_source_folder_that_is_not_there_stops_it(self):
        with self.assertRaises(OSError):
            self.key(dirs=("Makefile", "engine", "host"))

    def test_no_source_files_stops_it(self):
        with self.assertRaises(ValueError):
            self.key(dirs=())

    def test_a_tool_whose_version_cannot_be_had_stops_it(self):
        with self.assertRaises(OSError):
            G.tool_version(["/nonexistent/cc", "--version"])

    def test_a_tool_that_fails_stops_it(self):
        with self.assertRaises(OSError):
            G.tool_version([sys.executable, "-c", "import sys; sys.exit(3)"])

    def test_a_tool_that_hangs_stops_it(self):
        with self.assertRaises(OSError):
            G.tool_version([sys.executable, "-c", "import time; time.sleep(5)"], timeout=0.5)

    def test_a_tools_version_is_its_first_line(self):
        self.assertEqual(G.tool_version([sys.executable, "-c", "print('cc 9.1'); print('more')"]), "cc 9.1")


if __name__ == "__main__":
    unittest.main()
