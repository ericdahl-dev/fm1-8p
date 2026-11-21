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

    def test_a_line_naming_a_field_twice_is_an_error(self):
        with self.assertRaises(ValueError):
            G.parse('{"kind": "cpu", "name": "plain", "status": "fail", "value": null, "why": null, '
                    '"key": null, "status": "pass"}')

    def test_fields_of_the_wrong_type_are_an_error(self):
        for bad in (dict(kind=1), dict(name=1), dict(name=["a"]), dict(value="no result"), dict(value=0),
                    dict(why=3), dict(key=5)):
            args = dict(kind="cpu", name="plain", status="pass")
            args.update(bad)
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                G.record(**args)

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
        code, problems = G.verdict([("clip", "a"), ("clip", "b"), ("cpu", "plain")],
                                   [rec("a", "pass"), rec("b", "new"), rec("plain", "skipped", kind="cpu")])
        self.assertEqual(code, 0)
        self.assertEqual(sorted(p["status"] for p in problems), ["new", "skipped"])

    def test_nothing_compared_is_2_all_new(self):
        self.assertEqual(self.code([("clip", "a")], [rec("a", "new")]), 2)

    def test_nothing_compared_is_2_all_skipped(self):
        self.assertEqual(self.code([("cpu", "plain")], [rec("plain", "skipped", kind="cpu")]), 2)

    def test_nothing_compared_is_2_only_gone(self):
        self.assertEqual(self.code([("clip", "a")], [rec("a", "new"), rec("b", "gone")]), 2)

    def test_expected_given_as_an_iterator_still_finds_the_missing(self):
        code, problems = G.verdict((("clip", n) for n in ["a", "b"]), [rec("a", "pass")])
        self.assertEqual(code, 2)
        self.assertIn("b", [p["name"] for p in problems if p["status"] == "missing"])

    def test_a_pass_with_no_figures_where_figures_are_needed_is_an_error(self):
        self.assertEqual(self.code([("cpu", "plain")], [rec("plain", "pass", kind="cpu")], figures={"cpu": ("F",)}), 2)

    def test_a_fail_with_no_figures_where_figures_are_needed_is_an_error(self):
        self.assertEqual(self.code([("cpu", "plain")], [rec("plain", "fail", kind="cpu")], figures={"cpu": ("F",)}), 2)

    def test_a_pass_whose_figure_is_not_a_number_is_an_error(self):
        for value in ({}, {"F": None}, {"F": "no result"}, {"F": True}, {"F": float("nan")}, {"F": float("inf")},
                      {"8": 13.7}):
            r = G.record("cpu", "plain", "pass", value=value)
            with self.subTest(value=value):
                self.assertEqual(self.code([("cpu", "plain")], [r], figures={"cpu": ("F",)}), 2)

    def test_a_figures_entry_naming_no_field_is_a_mistake(self):
        for figures in ({"cpu": ()}, {"cpu": ""}, {"cpu": "F"}):
            with self.subTest(figures=figures), self.assertRaises(ValueError):
                G.verdict([("cpu", "plain")], [rec("plain", "pass", kind="cpu")], figures=figures)

    def test_required_given_as_an_iterator_holds_for_every_record(self):
        r = [rec("a", "pass"), rec("t", "skipped", kind="tone"), rec("p", "skipped", kind="cpu")]
        exp = [("clip", "a"), ("tone", "t"), ("cpu", "p")]
        self.assertEqual(self.code(exp, r, required=iter(("cpu",))), 2)

    def test_required_or_compared_as_a_bare_word_is_a_mistake(self):
        for kw in (dict(required="cpu"), dict(compared="clip")):
            with self.subTest(kw=kw), self.assertRaises(ValueError):
                G.verdict([("clip", "a")], [rec("a", "pass")], **kw)

    def test_a_kind_that_is_not_text_is_a_mistake(self):
        for kw in (dict(required=b"cpu"), dict(required=(b"cpu",)), dict(required=(1,)), dict(compared=(None,))):
            with self.subTest(kw=kw), self.assertRaises(ValueError):
                G.verdict([("cpu", "p")], [rec("p", "skipped", kind="cpu")], **kw)

    def test_a_figures_entry_that_is_an_iterator_is_read_once_for_all(self):
        a = G.record("cpu", "a", "pass", value={"F": 1.0})
        b = G.record("cpu", "b", "pass")
        figures = {"cpu": (f for f in ["F"])}
        self.assertEqual(self.code([("cpu", "a"), ("cpu", "b")], [a, b], figures=figures), 2)

    def test_an_empty_iterator_names_no_field(self):
        with self.assertRaises(ValueError):
            G.verdict([("cpu", "plain")], [rec("plain", "pass", kind="cpu")], figures={"cpu": iter(())})

    def test_a_field_name_that_is_not_text_is_a_mistake(self):
        with self.assertRaises(ValueError):
            G.verdict([("cpu", "plain")], [rec("plain", "pass", kind="cpu")], figures={"cpu": (1,)})

    def test_a_huge_whole_number_is_a_number(self):
        r = G.record("cpu", "plain", "pass", value={"F": 10 ** 400})
        self.assertEqual(self.code([("cpu", "plain")], [r], figures={"cpu": ("F",)}), 0)

    def test_every_kind_named_compared_needs_a_pass_or_fail(self):
        r = [rec("a", "new"), G.record("cpu", "plain", "pass", value={"F": 41.0})]
        self.assertEqual(self.code([("clip", "a"), ("cpu", "plain")], r, compared=("clip",)), 2)

    def test_and_with_one_it_passes(self):
        r = [rec("a", "pass"), rec("b", "new")]
        self.assertEqual(self.code([("clip", "a"), ("clip", "b")], r, compared=("clip",)), 0)

    def test_a_pass_with_its_figures_is_0(self):
        r = G.record("cpu", "plain", "pass", value={"F": 41.0})
        self.assertEqual(self.code([("cpu", "plain")], [r], figures={"cpu": ("F",)}), 0)

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

    def test_a_linked_source_folder_stops_it(self):
        os.makedirs(os.path.join(self.t, "elsewhere"))
        os.symlink(os.path.join(self.t, "elsewhere"), os.path.join(self.tree, "engine/linked"))
        with self.assertRaises(OSError):
            self.key()

    def test_toggles_cannot_collide_through_their_separators(self):
        self.assertNotEqual(self.key(toggles={"a=b": "c"}), self.key(toggles={"a": "b=c"}))

    def test_tools_cannot_collide_through_their_separators(self):
        self.assertNotEqual(self.key(tools={"a b": "c"}), self.key(tools={"a": "b c"}))

    def test_a_toggle_or_tool_that_is_not_text_stops_it(self):
        for kw in (dict(toggles={"x": False}), dict(toggles={"x": 1}), dict(tools={"cc": None})):
            with self.subTest(kw=kw), self.assertRaises(ValueError):
                self.key(**kw)

    def test_a_newline_cannot_forge_a_line(self):
        self.assertNotEqual(self.key(toggles={"x": "1\ntoggle y=2"}), self.key(toggles={"x": "1", "y": "2"}))

    def test_a_binary_is_known_by_its_path_and_contents(self):
        b = os.path.join(self.t, "fm1-live")
        self.put("fm1-live", "v1")
        one = G.binary_identity(b)
        self.put("fm1-live", "v2")
        self.assertNotEqual(G.binary_identity(b), one)
        self.assertIn(b, one)

    def test_a_binary_that_is_not_there_stops_it(self):
        with self.assertRaises(OSError):
            G.binary_identity(os.path.join(self.t, "none"))

    def test_no_tool_at_all_stops_it(self):
        with self.assertRaises(OSError):
            G.tool_version([])

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
