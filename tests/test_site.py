"""tools/site_build.py: the project page (#356) built from site/ and the factory bank."""
import os
import sys
import tempfile
import unittest
from urllib.parse import parse_qs, urlsplit

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import site_build  # noqa: E402

BASE = "https://ericdahl-dev.github.io/fm1-8p/"


class EmulatorUrlTest(unittest.TestCase):
    def test_points_fm1_emu_at_our_fwsc_by_its_absolute_url(self):
        u = urlsplit(site_build.emu_src(BASE))
        self.assertEqual((u.scheme, u.netloc, u.path), ("https", "joakimeriksson.github.io", "/fm1-emu/"))
        q = parse_qs(u.query)
        self.assertEqual(q["package"], [BASE + "fm1-8p.fwsc"])
        self.assertEqual(q["name"], ["FM1-8P"])
        self.assertEqual(q["embed"], ["1"])

    def test_a_base_without_its_trailing_slash_gives_the_same_url(self):
        self.assertEqual(site_build.emu_src(BASE.rstrip("/")), site_build.emu_src(BASE))

    def test_refuses_a_base_that_is_not_https(self):
        # fm1-emu fetches the package from its own https page: http would be blocked as mixed content
        with self.assertRaises(ValueError):
            site_build.emu_src("http://example.com/fm1-8p/")


class PageTest(unittest.TestCase):
    def build(self):
        d = tempfile.mkdtemp()
        site_build.build(ROOT, d, BASE)
        with open(os.path.join(d, "index.html"), encoding="utf-8") as fh:
            return d, fh.read()

    def test_lists_the_32_factory_tones_in_order(self):
        _, html = self.build()
        names = site_build.tone_names(ROOT)
        self.assertEqual(len(names), 32)
        self.assertEqual(names[16], "SOUNDTRACK")
        at = [html.index(f"<li>{site_build.escape(n)}</li>") for n in names]
        self.assertEqual(at, sorted(at))

    def test_embeds_the_emulator(self):
        _, html = self.build()
        self.assertIn('src="' + site_build.escape(site_build.emu_src(BASE)) + '"', html)

    def test_leaves_no_placeholder_and_copies_the_stylesheet(self):
        d, html = self.build()
        self.assertNotIn("{{", html)
        self.assertTrue(os.path.isfile(os.path.join(d, "style.css")))

    def test_escapes_names(self):
        self.assertEqual(site_build.escape('A<B>&"'), "A&lt;B&gt;&amp;&quot;")


if __name__ == "__main__":
    unittest.main()
