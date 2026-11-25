#!/usr/bin/env python3
"""The project page (#356): site/ with the 32 factory tone names from data/jx8p-factory-1-32.syx and the
fm1-emu embed pointed at our .fwsc, written to OUT. The Pages workflow (.github/workflows/pages.yml) adds the
.fwsc and .dock it builds beside it.

usage: tools/site_build.py OUT BASE_URL     (BASE_URL: the site's https address, e.g. from actions/configure-pages)
"""
import os
import shutil
import sys
from urllib.parse import urlencode

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jx8p  # noqa: E402

EMU = "https://joakimeriksson.github.io/fm1-emu/"
FWSC = "fm1-8p.fwsc"


def escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def emu_src(base_url):
    """fm1-emu's embed URL playing our .fwsc from BASE_URL (https: fm1-emu fetches it from its own https page)."""
    if not base_url.startswith("https://"):
        raise ValueError(f"{base_url}: the site's address must be https")
    package = base_url.rstrip("/") + "/" + FWSC
    return EMU + "?" + urlencode({"package": package, "name": "FM1-8P", "embed": "1"})


def tone_names(root):
    with open(os.path.join(root, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
        return [t.name.strip() for t in jx8p.parse_syx(fh.read())]


def build(root, out, base_url):
    src = os.path.join(root, "site")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(src, "index.html"), encoding="utf-8") as fh:
        html = fh.read()
    tones = "\n".join(f"        <li>{escape(n)}</li>" for n in tone_names(root))
    html = html.replace("{{TONES}}", tones).replace("{{EMU_SRC}}", escape(emu_src(base_url)))
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(html)
    shutil.copy(os.path.join(src, "style.css"), out)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    build(os.path.dirname(HERE), sys.argv[1], sys.argv[2])
