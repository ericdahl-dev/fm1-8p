"""fmhub.json, the FM-1 Firmware Hub's card for fm1-8p (#356), keeps the hub's rules
(https://thefirmwarehub.com/fmhub.md): only its fields, a plain-English `what` of 20 to 300
characters with no links or code, one of its kinds, https links, a square PNG image of
240 to 1024 px and 300 KB at most inside the repository, a #RRGGBB color, and a `sounds` line
(not "none") while the repository carries a sound bank, sample or ROM (.syx, .wav, .sf2, ...).

usage: python3 -m unittest tests/test_fmhub.py       (run from the repo root; no Pillow needed)
"""
import json
import re
import struct
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIELDS = {"what", "author", "kinds", "play", "editor", "cheat", "image", "color", "sounds", "smk37"}
KINDS = {"synth", "groove", "drums", "chords", "tracker", "other"}
SOUND_FILES = (".wav", ".aif", ".aiff", ".flac", ".mp3", ".ogg", ".syx", ".sf2")
SOUNDS = re.compile(r"(none( – .*)?|own(: .+)?|CC0: .+|public domain: .+|licensed: .+, .+)")


def png_size(data):
    """(width, height) of a PNG from its IHDR chunk, or None if it is not a PNG"""
    if data[:8] != b"\x89PNG\r\n\x1a\n" or data[12:16] != b"IHDR":
        return None
    return struct.unpack(">II", data[16:24])


class FmHubCard(unittest.TestCase):
    def setUp(self):
        self.card = json.loads((ROOT / "fmhub.json").read_text(encoding="utf-8"))

    def test_only_the_hubs_fields(self):
        self.assertLessEqual(set(self.card), FIELDS)
        self.assertLessEqual({"what", "kinds", "image"}, set(self.card))

    def test_what_is_plain_english_of_20_to_300_characters(self):
        what = self.card["what"]
        self.assertGreaterEqual(len(what), 20)
        self.assertLessEqual(len(what), 300)
        self.assertNotRegex(what, r"[<>{}`]|https?:|www\.|\b[a-z0-9-]+\.[a-z]{2,}(/|\b)(?!\d)")

    def test_kind_is_synth(self):
        self.assertEqual(self.card["kinds"], ["synth"])
        self.assertIn(self.card["kinds"][0], KINDS)

    def test_links_are_https(self):
        for k in ("play", "editor", "cheat"):
            if k in self.card:
                self.assertRegex(self.card[k], r"^https://\S+$", k)

    def test_image_is_a_square_png_of_240_to_1024_px_and_300_kb_at_most(self):
        path = ROOT / self.card["image"]
        self.assertFalse(Path(self.card["image"]).is_absolute())
        self.assertTrue(path.resolve().is_relative_to(ROOT), "image outside the repository")
        data = path.read_bytes()
        size = png_size(data)
        self.assertIsNotNone(size, "not a PNG")
        w, h = size
        self.assertEqual(w, h)
        self.assertGreaterEqual(w, 240)
        self.assertLessEqual(w, 1024)
        self.assertLessEqual(len(data), 300_000)

    def test_color_is_rrggbb(self):
        if "color" in self.card:
            self.assertRegex(self.card["color"], r"^#[0-9A-Fa-f]{6}$")

    def test_sounds_is_one_of_the_hubs_forms_when_given(self):
        s = self.card.get("sounds", "")
        if s:
            self.assertLessEqual(len(s), 300)
            self.assertRegex(s, "^" + SOUNDS.pattern + "$")
            self.assertNotRegex(s, r"https?:|www\.")

    # The repository carries the JX-8P factory bank (data/jx8p-factory-1-32.syx, firmware/patches.c
    # made from it), so the hub requires a `sounds` line, and refuses "none". Which line is Eric's
    # call (#356): until it is in, this fails as expected; once it is, drop the marker.
    @unittest.expectedFailure
    def test_a_repository_with_sounds_says_where_they_come_from(self):
        carried = [p for p in ROOT.rglob("*") if p.suffix.lower() in SOUND_FILES
                   and not any(part.startswith(".") or part == "build" for part in p.relative_to(ROOT).parts)]
        self.assertTrue(carried)
        s = self.card.get("sounds", "")
        self.assertTrue(s, f"{len(carried)} sound file(s), e.g. {carried[0].relative_to(ROOT)}, and no sounds line")
        self.assertFalse(s.startswith("none"))


if __name__ == "__main__":
    unittest.main()
