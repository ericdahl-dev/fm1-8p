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


class StoreTagTest(unittest.TestCase):
    # store tags are explicit and frozen (docs/patches.md, Storage): unique, and dense from 0, so a
    # new param takes the next one
    def test_tags_are_unique_and_dense(self):
        self.assertEqual(sorted(p.tag for p in params.PARAMS), list(range(len(params.PARAMS))))


class PatchFieldTest(unittest.TestCase):
    def test_each_field_sets_a_listed_param_or_waits(self):
        enums = {p.enum for p in params.PARAMS}
        for f in params.PATCH_FIELDS:
            if f.param:
                self.assertIn(f.param, enums, f.name)
                self.assertIsNone(f.waits, f.name)
            else:
                self.assertTrue(f.waits, f.name)
        sets = [f.param for f in params.PATCH_FIELDS if f.param]
        self.assertEqual(len(sets), len(set(sets)))

    # each field's JX-8P SysEx numbers (#195) name every byte it comes from: changing any other byte of
    # a factory tone leaves the field as it was
    def test_jx_numbers_name_every_source_byte(self):
        with open(os.path.join(ROOT, "data", "jx8p-factory-1-32.syx"), "rb") as f:
            tones = jx8p.parse_syx(f.read())
        for t in tones:
            base = jx8p.to_engine(t)
            for b in range(jx8p.NAME_LEN, len(t.params)):
                for v in (0, 40, 70, 127):
                    data = bytearray(t.params)
                    data[b] = v
                    e = jx8p.to_engine(t._replace(params=bytes(data)))
                    for f in params.PATCH_FIELDS:
                        if e[f.name] != base[f.name]:
                            self.assertIn(b, f.jx, "%s: %s changes with byte %d" % (t.name, f.name, b))

    # a param's JX numbers are its field's; params no JX-8P byte sets have none
    def test_a_params_jx_numbers_are_its_fields(self):
        by_param = {f.param: f for f in params.PATCH_FIELDS if f.param}
        for p in params.PARAMS:
            self.assertEqual(params.jx(p), by_param[p.enum].jx if p.enum in by_param else ())

    # docs/patches.md's "What waits for the engine" table names the waiting fields
    def test_the_docs_list_the_waiting_fields(self):
        with open(os.path.join(ROOT, "docs", "patches.md")) as f:
            text = f.read()
        table = text[text.index("| Field |", text.index("## What waits for the engine")):]
        table = table[:table.index("\n\n")]
        named = set(re.findall(r"`(\w+)`", table))
        self.assertEqual(named, {f.name for f in params.PATCH_FIELDS if f.waits})


class DisplayTest(unittest.TestCase):
    # every param says how the screen shows it (#196): a new param fails here until it does
    def test_every_param_has_a_display_kind(self):
        self.assertEqual(sorted(params.DISPLAY), sorted(p.enum for p in params.PARAMS))
        for enum, d in params.DISPLAY.items():
            self.assertIn(d.kind, params.DISPLAY_KINDS, enum)

    # a choice's names are one a value of its range: a wave or mode added to the engine without its
    # name fails make generate, not the screen (the WAVE[] and FM_WAVE_COUNT case of #261)
    def test_a_choice_without_a_name_for_each_value_does_not_generate(self):
        jx8p.emit_ui_params_h()                 # the list as it is generates
        saved = params.DISPLAY['OSC1_WAVE']
        try:
            params.DISPLAY['OSC1_WAVE'] = saved._replace(names=saved.names[:-1])
            with self.assertRaisesRegex(ValueError, "OSC1_WAVE"):
                jx8p.emit_ui_params_h()
            params.DISPLAY['OSC1_WAVE'] = saved._replace(kind='NOT_A_KIND')
            with self.assertRaisesRegex(ValueError, "OSC1_WAVE"):
                jx8p.emit_ui_params_h()
        finally:
            params.DISPLAY['OSC1_WAVE'] = saved

    # each kind that shows names has as many as it reads: NONZERO 2, MASK3 4, one for the ones that
    # name only their zero or below; a quote or backslash would break the C string
    def test_each_named_kind_has_the_names_it_reads(self):
        for enum, cut in (('VOICE_MODE', 1), ('OSC_SYNC', 1), ('LFO_PITCH_CENTS', 1), ('AMP_DYNA', 1),
                          ('AMP_KEY_FOLLOW', 1)):
            saved = params.DISPLAY[enum]
            try:
                params.DISPLAY[enum] = saved._replace(names=saved.names[:-cut])
                with self.assertRaisesRegex(ValueError, enum):
                    jx8p.emit_ui_params_h()
                params.DISPLAY[enum] = saved._replace(unit='"')
                with self.assertRaisesRegex(ValueError, enum):
                    jx8p.emit_ui_params_h()
            finally:
                params.DISPLAY[enum] = saved


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
