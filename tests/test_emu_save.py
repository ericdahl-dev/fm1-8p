"""Tests for tools/emu_save.py, #48's "saving never drops audio" check of an fm1-emu run (run by
`make check-patches`): synthetic logs and audio, no emulator."""
import os
import struct
import sys
import tempfile
import unittest
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import emu_save  # noqa: E402

FS = 44100
OFFSET_MS = 400                         # audio starts this long after the log's clock (the host's boot)
PRESS, RELEASE = 1500, 4000


def log(*saved_ms, overruns=0, ready=True):
    lines = ["489 fm1-dock boot"]
    if ready:
        lines.append("665 fm1-8p: ready, voices 00000004")
    lines += ["%d bank saved 97567353" % t for t in saved_ms]
    lines += ["---", "active module 1; render: last 3861 ticks (2.8 %%), worst 51283 (36.8 %%), "
              "%d overruns, 2673 blocks" % overruns]
    return "\n".join(lines) + "\n"


def audio(path, ms, sound):
    """A stereo WAV `ms` long: sound(t_ms) gives each frame's level (t on the log's clock)."""
    n = FS * ms // 1000
    frames = bytearray()
    for i in range(n):
        v = int(sound(OFFSET_MS + i * 1000 // FS))
        frames += struct.pack("<hh", v, v)
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(bytes(frames))


def chord(t, tail_end=9000):
    """A held chord from PRESS (a square, so it never sits at 0), fading to silence at tail_end."""
    if t < PRESS:
        return 0
    level = 10000 if t < RELEASE else max(0, 10000 * (tail_end - t) // (tail_end - RELEASE))
    return level if (t // 2) % 2 else -level


class CheckTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.wav = os.path.join(self.dir.name, "audio.wav")

    def tearDown(self):
        self.dir.cleanup()

    def check(self, text):
        return emu_save.check(text, self.wav, PRESS, RELEASE)

    # the good run: one write, after the release, once the tail has died away
    def test_a_write_in_silence_after_the_release_passes(self):
        audio(self.wav, 12000, chord)
        self.assertEqual(self.check(log(10500)), [])

    def test_no_write_fails(self):
        audio(self.wav, 12000, chord)
        self.assertIn("no write", " ".join(self.check(log())))

    def test_two_writes_fail(self):
        audio(self.wav, 12000, chord)
        self.assertIn("2 writes", " ".join(self.check(log(10500, 11000))))

    # the bug the check is for: a write while the chord is held
    def test_a_write_before_the_release_fails(self):
        audio(self.wav, 12000, chord)
        self.assertIn("before the release", " ".join(self.check(log(2600))))

    # after the release but in the tail: sound around the write
    def test_a_write_while_the_tail_sounds_fails(self):
        audio(self.wav, 12000, chord)
        self.assertIn("not silent", " ".join(self.check(log(5000))))

    def test_a_gap_while_the_chord_is_held_fails(self):
        audio(self.wav, 12000, lambda t: 0 if 2500 <= t < 2520 else chord(t))
        self.assertIn("gap", " ".join(self.check(log(10500))))

    def test_an_overrun_fails(self):
        audio(self.wav, 12000, chord)
        self.assertIn("overrun", " ".join(self.check(log(10500, overruns=1))))

    def test_a_module_that_never_started_fails(self):
        audio(self.wav, 12000, chord)
        self.assertIn("never started", " ".join(self.check(log(10500, ready=False))))

    def test_no_sound_at_all_fails(self):
        audio(self.wav, 12000, lambda t: 0)
        self.assertIn("no onset", " ".join(self.check(log(10500))))


if __name__ == "__main__":
    unittest.main()
