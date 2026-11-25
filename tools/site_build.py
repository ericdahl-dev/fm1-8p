#!/usr/bin/env python3
"""The project page (#356): site/ with the 32 factory tone names from data/jx8p-factory-1-32.syx and the
fm1-emu embed pointed at our .fwsc, written to OUT with the browser installer (site/install.js, site/installer.js
and fm1-dock's vendored code, site/vendor/fm1-dock), the self-hosted fonts (site/fonts), and the cookie-free usage
stats (site/analytics.js, with PostHog's library self-hosted in site/posthog-js).

With the .fwsc and .dock it also writes OUT/fw/: both files and fw/index.json, the catalog site/install.js
installs from (each file's SHA-256 and size, the package's identity, the module's ABI). It refuses a .fwsc whose
identity is not firmware/pins.env's FM1_IDENTITY.

usage: tools/site_build.py OUT BASE_URL [FWSC DOCK VERSION]
  BASE_URL: the site's https address, e.g. from actions/configure-pages; VERSION: e.g. the short commit
"""
import hashlib
import json
import os
import re
import shutil
import struct
import sys
from urllib.parse import urlencode

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import jx8p  # noqa: E402

EMU = "https://joakimeriksson.github.io/fm1-emu/"
FWSC = "fm1-8p.fwsc"
DOCK = "fm1_8p.dock"


def escape(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def emu_src(base_url):
    """fm1-emu's embed URL playing our .fwsc from BASE_URL (https: fm1-emu fetches it from its own https page)."""
    if not base_url.startswith("https://"):
        raise ValueError(f"{base_url}: the site's address must be https")
    package = base_url.rstrip("/") + "/fw/" + FWSC
    return EMU + "?" + urlencode({"package": package, "name": "FM1-8P", "embed": "1"})


def product_of(raw):
    """A .fwsc's package identity ("FM-1_948"), read as fm1-dock's fm1install.js productOf does: one marker byte
    after each of the first 20 blocks of 0x30 bytes, 0x7D for none."""
    s = ""
    for i in range(20):
        m = raw[i * 0x30 + 0x2F]
        if m != 0x7D:
            s += chr((m - i - 1) & 0xFF)
    return s


def dock_info(blob):
    """A .dock's ABI and module name, read from its header (hb_file_t) as fm1-dock's dock.js moduleInfo does."""
    if len(blob) < 80 or struct.unpack_from("<I", blob, 0)[0] != 0x52425248:
        raise ValueError("not an fm1-dock module file (.dock)")
    abi = struct.unpack_from("<I", blob, 4)[0]
    name = blob[12:36].split(b"\0", 1)[0].decode("latin-1")
    return {"abi": abi, "name": name}


def catalog(fwsc, dock, version, identity):
    """fw/index.json for site/install.js: our firmware package and our fm1-dock module, each with its SHA-256 and
    size (fm1-dock's checkPackage checks the hash and the identity, site/install.js the module's hash and ABI).
    Refuses a .fwsc whose identity is not ours: the page must never offer another firmware's package."""
    product = product_of(fwsc)
    if product != identity:
        raise ValueError(f"{FWSC}: identity {product}, not {identity}")
    info = dock_info(dock)
    return {
        "firmwares": {"fm1-8p": {"name": "FM1-8P", "version": version, "file": FWSC,
                                 "sha256": hashlib.sha256(fwsc).hexdigest(), "size": len(fwsc), "product": product}},
        "modules": {"fm1-8p": {"name": info["name"], "version": version, "file": DOCK,
                               "sha256": hashlib.sha256(dock).hexdigest(), "size": len(dock), "abi": info["abi"]}},
    }


def our_identity(root):
    """FM1_IDENTITY in firmware/pins.env: the identity our .fwsc must have."""
    with open(os.path.join(root, "firmware", "pins.env"), encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("FM1_IDENTITY="):
                return line.split("=", 1)[1].strip().strip('"')
    raise ValueError("firmware/pins.env has no FM1_IDENTITY")


def boot_tone(root):
    """The tone FM1-8P boots on, 1-based: firmware/patches.h's PATCH_SOUNDTRACK (0-based) + 1."""
    with open(os.path.join(root, "firmware", "patches.h"), encoding="utf-8") as fh:
        m = re.search(r"^#define PATCH_SOUNDTRACK (\d+)u?\b", fh.read(), re.M)
    if not m:
        raise ValueError("firmware/patches.h has no PATCH_SOUNDTRACK")
    return int(m.group(1)) + 1


def tone_names(root):
    with open(os.path.join(root, "data", "jx8p-factory-1-32.syx"), "rb") as fh:
        return [t.name.strip() for t in jx8p.parse_syx(fh.read())]


def write_fw(root, out, packages):
    """OUT/fw/: our .fwsc and .dock under the names the page uses, and their catalog, index.json. The catalog is
    made (and the identity checked) before anything is written."""
    fwsc_path, dock_path, version = packages
    with open(fwsc_path, "rb") as fh:
        fwsc = fh.read()
    with open(dock_path, "rb") as fh:
        dock = fh.read()
    cat = catalog(fwsc, dock, version, our_identity(root))
    fw = os.path.join(out, "fw")
    os.makedirs(fw, exist_ok=True)
    with open(os.path.join(fw, FWSC), "wb") as fh:
        fh.write(fwsc)
    with open(os.path.join(fw, DOCK), "wb") as fh:
        fh.write(dock)
    with open(os.path.join(fw, "index.json"), "w", encoding="utf-8") as fh:
        json.dump(cat, fh, indent=2, sort_keys=True)
        fh.write("\n")


def build(root, out, base_url, packages=None):
    """The page into OUT; with packages, (FWSC, DOCK, VERSION), also OUT/fw/ (write_fw)."""
    src = os.path.join(root, "site")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(src, "index.html"), encoding="utf-8") as fh:
        html = fh.read()
    names, boot = tone_names(root), boot_tone(root)
    if not 1 <= boot <= len(names):
        raise ValueError(f"boot tone {boot}: the bank has {len(names)} tones")
    mark = ' class="boot"'
    tones = "\n".join(f"        <li{mark if i == boot else ''}>{escape(n)}</li>" for i, n in enumerate(names, 1))
    html = (html.replace("{{TONES}}", tones).replace("{{EMU_SRC}}", escape(emu_src(base_url)))
            .replace("{{BOOT_NUM}}", str(boot)).replace("{{BOOT_NAME}}", escape(names[boot - 1])))
    if packages:
        write_fw(root, out, packages)
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as fh:
        fh.write(html)
    for name in ("style.css", "install.js", "installer.js", "analytics.js"):
        shutil.copy(os.path.join(src, name), out)
    for folder in ("vendor", "fonts", "posthog-js"):
        shutil.copytree(os.path.join(src, folder), os.path.join(out, folder), dirs_exist_ok=True)


if __name__ == "__main__":
    if len(sys.argv) not in (3, 6):
        raise SystemExit(__doc__)
    build(os.path.dirname(HERE), sys.argv[1], sys.argv[2], tuple(sys.argv[3:]) or None)
