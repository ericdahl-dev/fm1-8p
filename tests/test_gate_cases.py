"""Tests for tools/gate_cases.py and tools/gate_midi.py, the no-sound gate's clips (#140)."""
import os
import sys
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
sys.path.insert(0, os.path.join(ROOT, "tools"))


def voices_at_once(notes):
    """The most notes sounding together in a (note, start s, end s, velocity) list."""
    ev = sorted([(s, 1) for _, s, _, _ in notes] + [(e, -1) for _, _, e, _ in notes])
    now = most = 0
    for _, d in ev:
        now += d
        most = max(most, now)
    return most


class WideChordClipTest(unittest.TestCase):
    """SOUNDTRACK on the device's 4 voices, a wide chord at velocity 127 (#320): the loudest everyday case,
    where the engine's soft clip bends the most, as a gate clip on the engine path."""

    def setUp(self):
        import gate_cases
        self.cases = {name: (mid, args) for name, mid, args in gate_cases.cases(ROOT)}

    def test_the_clip_is_tone_17_on_4_voices(self):
        mid, args = self.cases["tone-17-4v-v127-wide"]
        self.assertEqual(mid, "wide127.mid")
        self.assertEqual(args.count("-v"), 1)
        self.assertEqual(args[args.index("-v") + 1], "4")
        tone17 = self.cases["tone-17"][1]
        sets = lambda a: [a[i + 1] for i, x in enumerate(a) if x == "-s"]
        self.assertEqual(sets(args), sets(tone17))

    def test_its_midi_is_a_wide_velocity_127_chord_that_steals_no_voice(self):
        import gate_midi
        notes = gate_midi.FILES["wide127.mid"]()
        self.assertEqual(sorted(n for n, _, _, _ in notes), [36, 48, 79, 91])
        self.assertTrue(all(v == 127 for _, _, _, v in notes))
        self.assertLessEqual(voices_at_once(notes), 4)


if __name__ == "__main__":
    unittest.main()
