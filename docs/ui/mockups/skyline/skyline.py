"""The skyline with a laser line (Eric, 2026-11-27): the art that replaces the sunset on PLAY and on the
boot screen, in both themes. A night city of towers with lit windows against the theme's dusk bands, one
neon laser line across the sky behind the towers, with a four-point flare in a gap between them, and a neon
street line. Every shape is a fill (no sun, no perspective grid: those read as other hub firmwares).

    python3 docs/ui/mockups/skyline/skyline.py [--font PATH/TO/felucca_font.h]

writes skyline-both-3x.png here: Membrane PLAY, Membrane boot, Night Panel PLAY, Night Panel boot. Uses
render.py's Screen, fonts and themes (Pillow and fm1-dock's host font, as render.py needs). The tower layout
is seeded (seed 8), so the firmware can bake the same towers as a table."""
import argparse, importlib.util, math, os, random, sys
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("render", os.path.join(here, "..", "render.py"))
R = importlib.util.module_from_spec(spec); spec.loader.exec_module(R)
W, H, M = R.W, R.H, R.M
R.PATCH = ("17", "SOUNDTRACK", False)
R.PATTERN = ("11", "PATTERN 11", False)


def neon(t, k):
    return t["sun"][k % len(t["sun"])]


def laser_line(sc, t, x, y, w, h, ly, cx, flare):
    """one neon laser line across at row ly, with a four-point flare of `flare` px at cx"""
    for d, c in ((2, neon(t, 5)), (1, neon(t, 4))):
        sc.fill(x, ly - d, w, 1, c)
        sc.fill(x, ly + d, w, 1, c)
    sc.fill(x, ly, w, 1, "#FFFFFF")
    lim = min(flare, ly - y, y + h - 1 - ly)
    for r, c in ((lim, neon(t, 4)), (lim * 2 // 3, neon(t, 1)), (lim // 3, "#FFFFFF")):
        sc.fill(cx, ly - r, 1, 2 * r + 1, c)
        sc.fill(cx - 2 * r, ly, 4 * r + 1, 1, c)
    for k in range(1, lim):
        c = R.mix(t["window"], neon(t, 1), max(0.0, 1 - k / lim))
        for sx, sy in ((1, 1), (1, -1), (-1, 1), (-1, -1)):
            sc.fill(cx + sx * k, ly + sy * k, 1, 1, c)
    sc.fill(cx - 1, ly - 1, 3, 3, "#FFFFFF")


def towers(sc, t, x, y, w, h, seed, street=True):
    rnd = random.Random(seed)
    tower = "#050508"
    xx = x
    while xx < x + w:
        tw = rnd.randrange(10, 24)
        th = rnd.randrange(h // 3, h - 6)
        if rnd.random() < 0.18:
            th = h - 3
        top = y + h - th
        sc.fill(xx, top, min(tw, x + w - xx), th, tower)
        if rnd.random() < 0.3:
            sc.fill(xx + tw // 2, top - 4, 1, 4, tower)
        for wy in range(top + 3, y + h - 4, 4):
            for wx in range(xx + 2, min(xx + tw - 2, x + w - 2), 3):
                if rnd.random() < 0.22:
                    sc.fill(wx, wy, 1, 2, neon(t, rnd.choice((0, 1, 2))))
        xx += tw + rnd.randrange(0, 3)
    if street:
        sc.fill(x, y + h - 2, w, 2, neon(t, 4))


def sky(sc, t, x, y, w, h, seed):
    rnd = random.Random(seed + 1)
    sc.fill(x, y, w, h, t["window"])
    bh = h // 5
    for k, c in enumerate([R.mix(t["window"], neon(t, 5), 0.10), R.mix(t["window"], neon(t, 4), 0.18), R.mix(t["window"], neon(t, 3), 0.28)]):
        sc.fill(x, y + h - (3 - k) * bh - bh, w, bh, c)
    for _ in range(w // 10):
        sc.fill(x + rnd.randrange(w), y + rnd.randrange(h // 2), 1, 1, R.mix(t["window"], t["text"], 0.6))


def art_skyline_laser_sky(sc, t, x, y, w, h, seed=8):
    """A: the laser crosses the sky behind the towers, its flare in a gap between them."""
    sky(sc, t, x, y, w, h, seed)
    laser_line(sc, t, x, y, w, h, y + h * 2 // 5, x + w * 2 // 3, h // 3)
    towers(sc, t, x, y, w, h, seed)


def play_with(fonts, t, art):
    tt = dict(t, art="badge")          # draw PLAY, then paint our art over the slot
    sc = R.scr_play(fonts, tt)
    x, y, w, h = M, R.SLOT[0], W - 2 * M, R.SLOT[1]
    sc.fill(x, y, w, h, t["bg"])
    art(sc, t, x, y, w, h)
    return sc


def boot_with(fonts, t, art):
    sc = R.Screen(fonts, t["bg"])
    art(sc, t, 0, 20, W, 112)
    sc.dtext(0, 150, "D24", "FM1-8P", t["text"], align="c", w=W)
    sc.text(0, 180, "POLY SYNTH  4 VOICES", t["dim"], align="c", w=W)
    R.boot_bar(sc, t, 208)
    sc.text(0, 220, "V1", t["dim"], align="c", w=W)
    return sc


def main():
    ap = argparse.ArgumentParser(description="the skyline art, PLAY and boot, both themes")
    dock = os.environ.get("FM1_DOCK", os.path.expanduser("~/Projects/fm1-8p-ref/fm1-dock"))
    ap.add_argument("--font", default=os.path.join(dock, "firmware/gen/felucca_font.h"))
    args = ap.parse_args()
    dfont = {k: R.bake_font(v) for k, v in R.SIZES.items()}
    fonts = (R.load_host_font(args.font), dfont)
    art = art_skyline_laser_sky
    sheet = Image.new("RGB", (4 * W + 5 * 8, H + 16), (40, 40, 40))
    col = 0
    for key, t in R.THEMES.items():
        for fn in (play_with, boot_with):
            sc = fn(fonts, t, art)
            sc.img.resize((W * 3, H * 3), Image.NEAREST).save(os.path.join(here, "%s-%s-3x.png" % (key, fn.__name__.split("_")[0])))
            sheet.paste(sc.img, (8 + col * (W + 8), 8))
            col += 1
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(here, "skyline-both-3x.png"))
    print("skyline-both-3x.png")


if __name__ == "__main__":
    main()
