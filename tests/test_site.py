"""tools/site_build.py: the project page (#356) built from site/ and the factory bank."""
import hashlib
import json
import os
import re
import shutil
import struct
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
        self.assertEqual(q["package"], [BASE + "fw/fm1-8p.fwsc"])
        self.assertEqual(q["name"], ["FM1-8P"])
        self.assertEqual(q["embed"], ["1"])

    def test_a_base_without_its_trailing_slash_gives_the_same_url(self):
        self.assertEqual(site_build.emu_src(BASE.rstrip("/")), site_build.emu_src(BASE))

    def test_refuses_a_base_that_is_not_https(self):
        # fm1-emu fetches the package from its own https page: http would be blocked as mixed content
        with self.assertRaises(ValueError):
            site_build.emu_src("http://example.com/fm1-8p/")


def fake_fwsc(identity, tail=b"\x00" * 64):
    """A package-shaped fixture: fm1-dock's identity markers (one byte after each of the first 20 blocks of 0x30)."""
    raw = bytearray(20 * 0x30) + bytearray(tail)
    for i in range(20):
        raw[i * 0x30 + 0x2F] = (ord(identity[i]) + i + 1) & 0xFF if i < len(identity) else 0x7D
    return bytes(raw)


class PackageIdentityTest(unittest.TestCase):
    def test_reads_the_identity_fm1_dock_reads(self):
        self.assertEqual(site_build.product_of(fake_fwsc("FM-1_948")), "FM-1_948")

    def test_our_identity_comes_from_pins_env(self):
        self.assertEqual(site_build.our_identity(ROOT), "FM-1_948")


def fake_dock(name="FM1-8P", abi=2, version=1, size=200):
    """A .dock-shaped fixture: fm1-dock's module header (hb_file_t) as dock.js moduleInfo reads it."""
    raw = bytearray(size)
    struct.pack_into("<III", raw, 0, 0x52425248, abi, 0)
    raw[12:12 + len(name)] = name.encode()
    struct.pack_into("<I", raw, 36, version)
    return bytes(raw)


class DockInfoTest(unittest.TestCase):
    def test_reads_the_abi_and_name_dock_js_reads(self):
        self.assertEqual(site_build.dock_info(fake_dock("FM1-8P", abi=2)), {"abi": 2, "name": "FM1-8P"})

    def test_refuses_a_file_that_is_not_a_module(self):
        with self.assertRaises(ValueError):
            site_build.dock_info(b"\x00" * 100)


class CatalogTest(unittest.TestCase):
    """fw/index.json: what site/install.js reads to install the .fwsc and to add the .dock to fm1-dock."""

    def test_firmware_entry_names_our_file_its_hash_size_identity_and_version(self):
        fwsc = fake_fwsc("FM-1_948")
        fw = site_build.catalog(fwsc, fake_dock(), "abc1234", "FM-1_948")["firmwares"]["fm1-8p"]
        self.assertEqual(fw["name"], "FM1-8P")
        self.assertEqual(fw["file"], "fm1-8p.fwsc")
        self.assertEqual(fw["sha256"], hashlib.sha256(fwsc).hexdigest())
        self.assertEqual(fw["size"], len(fwsc))
        self.assertEqual(fw["product"], "FM-1_948")
        self.assertEqual(fw["version"], "abc1234")

    def test_module_entry_carries_the_abi_and_name_from_the_dock_header(self):
        dock = fake_dock("FM1-8P", abi=2, size=300)
        mod = site_build.catalog(fake_fwsc("FM-1_948"), dock, "abc1234", "FM-1_948")["modules"]["fm1-8p"]
        self.assertEqual(mod["file"], "fm1_8p.dock")
        self.assertEqual(mod["sha256"], hashlib.sha256(dock).hexdigest())
        self.assertEqual(mod["size"], 300)
        self.assertEqual(mod["abi"], 2)
        self.assertEqual(mod["name"], "FM1-8P")
        self.assertEqual(mod["version"], "abc1234")

    def test_refuses_a_package_whose_identity_is_not_ours(self):
        with self.assertRaisesRegex(ValueError, "FM-1_921"):
            site_build.catalog(fake_fwsc("FM-1_921"), fake_dock(), "abc1234", "FM-1_948")


class VendoredTest(unittest.TestCase):
    """site/vendor/fm1-dock: fm1-dock's installer, unmodified, at the commit its README names."""
    DIR = os.path.join(ROOT, "site", "vendor", "fm1-dock")

    def test_every_file_matches_the_sha256_its_readme_records(self):
        with open(os.path.join(self.DIR, "README.md"), encoding="utf-8") as fh:
            rows = re.findall(r"^\| `([^`]+)` \| `([0-9a-f]{64})` \|$", fh.read(), re.M)
        self.assertEqual(sorted(r[0] for r in rows),
                         ["LICENSE", "dock.js", "install/fm1install.js", "install/fm1ota.js", "install/ports.js"])
        for rel, sha in rows:
            with open(os.path.join(self.DIR, rel), "rb") as fh:
                self.assertEqual(hashlib.sha256(fh.read()).hexdigest(), sha, rel)

    def test_the_readme_names_the_pinned_commit(self):
        with open(os.path.join(self.DIR, "README.md"), encoding="utf-8") as fh:
            self.assertIn("60a89df6554f6df3823f03a4c893b6715bfef9b5", fh.read())


class PageTest(unittest.TestCase):
    def build(self):
        d = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, d)
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

    def test_with_the_packages_writes_them_and_their_catalog_into_fw(self):
        src = tempfile.mkdtemp()
        fwsc, dock = os.path.join(src, "a.fwsc"), os.path.join(src, "b.dock")
        with open(fwsc, "wb") as fh:
            fh.write(fake_fwsc("FM-1_948"))
        with open(dock, "wb") as fh:
            fh.write(fake_dock())
        d = tempfile.mkdtemp()
        site_build.build(ROOT, d, BASE, (fwsc, dock, "abc1234"))
        with open(os.path.join(d, "fw", "index.json"), encoding="utf-8") as fh:
            cat = json.load(fh)
        self.assertEqual(cat["firmwares"]["fm1-8p"]["product"], "FM-1_948")
        for name in ("fm1-8p.fwsc", "fm1_8p.dock"):
            self.assertTrue(os.path.isfile(os.path.join(d, "fw", name)), name)

    def test_refuses_to_publish_a_package_of_another_identity(self):
        src = tempfile.mkdtemp()
        fwsc, dock = os.path.join(src, "a.fwsc"), os.path.join(src, "b.dock")
        with open(fwsc, "wb") as fh:
            fh.write(fake_fwsc("FM-1_921"))
        with open(dock, "wb") as fh:
            fh.write(fake_dock())
        d = tempfile.mkdtemp()
        with self.assertRaises(ValueError):
            site_build.build(ROOT, d, BASE, (fwsc, dock, "abc1234"))
        self.assertFalse(os.path.exists(os.path.join(d, "fw", "index.json")))

    def test_copies_the_installer_and_the_vendored_fm1_dock_code(self):
        d, _ = self.build()
        for rel in ("install.js", "installer.js", "vendor/fm1-dock/dock.js", "vendor/fm1-dock/install/fm1install.js",
                    "vendor/fm1-dock/install/fm1ota.js", "vendor/fm1-dock/install/ports.js", "vendor/fm1-dock/LICENSE"):
            self.assertTrue(os.path.isfile(os.path.join(d, rel)), rel)

    def install_section(self):
        _, html = self.build()
        return html[html.index('<section id="install">'):html.index("</section>", html.index('<section id="install">'))]

    def test_install_section_says_the_installer_is_not_tested_on_hardware(self):
        self.assertIn("not been tested on an FM-1", self.install_section())

    def test_install_section_has_the_installer_and_the_dock_route(self):
        sec = self.install_section()
        self.assertIn('id="fm1-install-btn"', sec)
        self.assertIn('id="fm1-add-btn"', sec)
        self.assertIn("Add FM1-8P to my fm1-dock", sec)
        self.assertIn('href="fw/fm1_8p.dock"', sec)
        self.assertIn('href="fw/fm1-8p.fwsc"', sec)

    def test_install_section_says_how_to_go_back_and_the_rescue(self):
        sec = self.install_section()
        self.assertIn("https://thefirmwarehub.com/", sec)
        self.assertIn("V15", sec)
        self.assertIn("OCT-", sec)
        self.assertIn("OCT+", sec)

    def test_install_section_names_the_browsers_and_the_identity(self):
        sec = self.install_section()
        self.assertIn("Chrome or Edge", sec)
        self.assertIn("FM-1_948", sec)

    def test_page_loads_the_installer_and_credits_the_vendored_code(self):
        _, html = self.build()
        self.assertIn('<script type="module" src="install.js"></script>', html)
        footer = html[html.index("<footer>"):]
        self.assertIn("https://github.com/joakimeriksson/fm1-dock", footer)
        self.assertIn("Leo Kuroshita", footer)
        self.assertIn("GPL-3.0", footer)
    def test_links_the_files_the_workflow_puts_beside_the_page(self):
        _, html = self.build()
        with open(os.path.join(ROOT, ".github", "workflows", "pages.yml"), encoding="utf-8") as fh:
            workflow = fh.read()
        for name in (site_build.FWSC, "fm1_8p.dock"):
            self.assertIn(f'href="fw/{name}"', html)
            self.assertIn(f"build/fm1/{name}", workflow)

    def test_escapes_tone_names_in_the_page(self):
        real = site_build.tone_names
        site_build.tone_names = lambda root: ["A<B&C"]
        self.addCleanup(setattr, site_build, "tone_names", real)
        _, html = self.build()
        self.assertIn("<li>A&lt;B&amp;C</li>", html)

    def test_escapes_names(self):
        self.assertEqual(site_build.escape('A<B>&"'), "A&lt;B&gt;&amp;&quot;")


if __name__ == "__main__":
    unittest.main()
