#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the fm1-8p on-device UI mockups: three visual directions x four screens.

Every screen is drawn at the panel's own 240 x 240 with only what the fm1-dock host gives a
module (fill, line, pixel, the host's 16 px and 32 px fonts, blit), then quantized to RGB565 and
expanded back the way the panel shows it, so the PNGs are the colors the device can show.
Each screen is saved at 1x and as a 3x nearest-neighbor upscale (720 x 720) for a phone.

    python3 docs/ui/mockups/render.py [--font PATH/TO/felucca_font.h]

Needs Pillow. The text is the host's own font (Terminus 8x16 and its 16x32 doubling), read from
fm1-dock's firmware/gen/felucca_font.h: by default ~/Projects/fm1-8p-ref/fm1-dock, or $FM1_DOCK,
or --font. Without it the script stops: a mockup in another font would mislead.
"""
import argparse
import math
import os
import re
import sys

from PIL import Image

W = H = 240
OUT = os.path.dirname(os.path.abspath(__file__))


# ---------------------------------------------------------------- color: RGB565 in, panel out
def q(c):
    """'#RRGGBB' or (r, g, b) -> the RGB888 the panel shows for its RGB565 value."""
    if isinstance(c, str):
        c = (int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16))
    r, g, b = (int(v) & 0xFF for v in c)
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def rgb565(c):
    r, g, b = q(c)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def mix(a, b, t):
    """a toward b by t (0..1), quantized: a glow step or a ghost segment."""
    a, b = q(a), q(b)
    return q(tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3)))


def lum(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = q(c)
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


# ---------------------------------------------------------------- the host's font
def load_font(path):
    src = open(path).read()

    def arr(name):
        m = re.search(r"%s\[\d+\]\s*=\s*\{([^}]*)\}" % name, src)
        return [int(v, 0) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]

    def font(tag):
        m = re.search(r"felucca_font_t FONT_%s\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)" % tag, src)
        h, pad, first, last = (int(v) for v in m.groups())
        return dict(h=h, pad=pad, first=first, last=last, adv=arr("FONT_%s_ADV" % tag),
                    bw=arr("FONT_%s_BW" % tag), off=arr("FONT_%s_OFF" % tag), data=arr("FONT_%s_DATA" % tag))

    return font("S"), font("L")


# ---------------------------------------------------------------- a screen, drawn as a module would
class Screen:
    def __init__(self, fonts, bg):
        self.img = Image.new("RGB", (W, H), q(bg))
        self.px = self.img.load()
        self.fonts = fonts

    def pixel(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.px[int(x), int(y)] = q(c)

    def fill(self, x, y, w, h, c):
        c = q(c)
        for yy in range(max(0, int(y)), min(H, int(y + h))):
            for xx in range(max(0, int(x)), min(W, int(x + w))):
                self.px[xx, yy] = c

    def frame(self, x, y, w, h, c, t=1):
        self.fill(x, y, w, t, c)
        self.fill(x, y + h - t, w, t, c)
        self.fill(x, y, t, h, c)
        self.fill(x + w - t, y, t, h, c)

    def glow(self, x, y, w, h, accent, bg, rings=(1.0, 0.45, 0.2)):
        """The active element: its frame in the accent, then dimmer frames outside it.
        Three frames = 12 fills on the device."""
        for i, t in enumerate(rings):
            self.frame(x - i, y - i, w + 2 * i, h + 2 * i, mix(bg, accent, t))

    def line(self, x0, y0, x1, y1, c):
        x0, y0, x1, y1 = int(round(x0)), int(round(y0)), int(round(x1)), int(round(y1))
        dx, dy = abs(x1 - x0), -abs(y1 - y0)
        sx, sy = (1 if x0 < x1 else -1), (1 if y0 < y1 else -1)
        err = dx + dy
        while True:
            self.pixel(x0, y0, c)
            if x0 == x1 and y0 == y1:
                break
            e2 = 2 * err
            if e2 >= dy:
                err += dy
                x0 += sx
            if e2 <= dx:
                err += dx
                y0 += sy

    def poly(self, pts, c, thick=1):
        for (a, b), (c2, d) in zip(pts, pts[1:]):
            for o in range(thick):
                self.line(a, b - o, c2, d - o, c)

    def text_w(self, big, s):
        f = self.fonts[1 if big else 0]
        return sum(f["adv"][self._g(f, ch)] for ch in s)

    @staticmethod
    def _g(f, ch):
        o = ord(ch)
        if ord("a") <= o <= ord("z") and f["last"] < ord("a"):
            o -= 32
        if o < f["first"] or o > f["last"]:
            o = ord("?")
        return o - f["first"]

    def text(self, x, y, s, c, big=0, align="l", w=0):
        f = self.fonts[1 if big else 0]
        tw = self.text_w(big, s)
        if align == "c":
            x = x + (w - tw) // 2
        elif align == "r":
            x = x + w - tw
        for ch in s:
            gi = self._g(f, ch)
            bw = f["bw"][gi]
            bpr = (bw + 7) // 8
            off = f["off"][gi]
            for gy in range(f["h"]):
                for gx in range(bw):
                    if f["data"][off + gy * bpr + gx // 8] >> (7 - (gx & 7)) & 1:
                        self.pixel(x - f["pad"] + gx, y + gy, c)
            x += f["adv"][gi]
        return x

    # 7-segment LED digits, drawn with fills: lit segments in `on`, the rest as dim "ghost" segments
    SEG = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
           "7": "abc", "8": "abcdefg", "9": "abcdfg", "A": "abcefg", "b": "cdefg", "C": "adef", "d": "bcdeg",
           "E": "adefg", "F": "aefg", "P": "abefg", "U": "bcdef", "-": "g", " ": ""}

    def seg7(self, x, y, s, dw, dh, t, on, ghost, gap=None):
        gap = t + 2 if gap is None else gap
        for ch in s:
            lit = self.SEG.get(ch, "")
            hh = (dh - 3 * t) // 2
            j = 1 if t >= 3 else 0             # a hairline between segments, as on an LED
            segs = {
                "a": (x + t + j, y, dw - 2 * t - 2 * j, t), "g": (x + t + j, y + t + hh, dw - 2 * t - 2 * j, t),
                "d": (x + t + j, y + 2 * t + 2 * hh, dw - 2 * t - 2 * j, t),
                "f": (x, y + t + j, t, hh - 2 * j), "b": (x + dw - t, y + t + j, t, hh - 2 * j),
                "e": (x, y + 2 * t + hh + j, t, hh - 2 * j), "c": (x + dw - t, y + 2 * t + hh + j, t, hh - 2 * j),
            }
            for k, (sx, sy, sw, sh) in segs.items():
                if k in lit:
                    self.fill(sx, sy, sw, sh, on)
                elif ghost is not None:
                    self.fill(sx, sy, sw, sh, ghost)
            x += dw + gap
        return x

    def seg7_w(self, n, dw, t, gap=None):
        gap = t + 2 if gap is None else gap
        return n * dw + (n - 1) * gap

    def arc(self, cx, cy, r, a0, a1, c, thick=3):
        """Degrees, 0 = right, clockwise on screen. On the device: a canvas and a table of points."""
        if a1 < a0:
            return
        steps = max(2, int((a1 - a0) * r / 30))
        for i in range(steps + 1):
            a = math.radians(a0 + (a1 - a0) * i / steps)
            for k in range(thick):
                rr = r - k
                self.pixel(round(cx + rr * math.cos(a)), round(cy + rr * math.sin(a)), c)

    def disc_rows(self, cx, cy, r, color_for_row, skip_row=lambda y: False, y_max=H):
        """A disc as one fill per row (the synthwave sun): cheap on the device."""
        for y in range(int(cy - r), int(cy + r) + 1):
            if y >= y_max or skip_row(y):
                continue
            dx = int(math.sqrt(max(0, r * r - (y - cy) ** 2)))
            self.fill(cx - dx, y, 2 * dx + 1, 1, color_for_row(y))

    def save(self, path):
        self.img.save(path + ".png")
        self.img.resize((W * 3, H * 3), Image.NEAREST).save(path + "-3x.png")


# ---------------------------------------------------------------- what the screens show
PATCH = {"a": ("A17", "GLASS HARBOR"), "b": ("A17", "NEON TIDE"), "c": ("A17", "CHROME DUSK")}
VCF = [("41", "CUTOFF", 88), ("42", "RESO", 34), ("43", "ENV", 62), ("44", "KEYTRK", 50)]
ACTIVE_VCF = 1          # knob 2 is being turned
ENV1 = [("61", "ATK", 12, "40ms"), ("62", "DCY", 48, "480ms"), ("63", "SUS", 70, "70%"), ("64", "REL", 35, "350ms")]
ACTIVE_ENV = 1
# the 16-step pattern: (kind, label, notes as semitones from C3); a tie holds the step before it
SEQ = [("n", "C3", [0]), ("r", "", []), ("n", "Eb3", [3]), ("t", "", [3]),
       ("c", "Cm7", [0, 3, 7, 10]), ("t", "", [0, 3, 7, 10]), ("n", "G3", [7]), ("r", "", []),
       ("n", "Ab2", [-4]), ("n", "C3", [0]), ("c", "Fm", [5, 8, 12]), ("r", "", []),
       ("n", "G2", [-5]), ("t", "", [-5]), ("n", "Bb2", [-2]), ("r", "", [])]
PLAYHEAD = 6
EDIT_STEP = 4
PAGES = ["DCO-1", "DCO-2", "MIX", "VCF", "VCA", "ENV-1", "ENV-2", "LFO", "CHORUS"]


def filter_curve(cut, res, n=110):
    """A 4-pole low-pass's response in dB over a log frequency axis, 0..1 -> list of dB."""
    fc = 10 ** (-2.0 + 2.4 * cut / 127)
    qv = 0.7 + 6.0 * (res / 127) ** 1.5
    out = []
    for i in range(n):
        f = 10 ** (-2.0 + 2.6 * i / (n - 1))
        x = f / fc
        h2 = 1 / ((1 - x * x) ** 2 + (x / qv) ** 2)
        out.append(10 * math.log10(h2 * h2) / 2)
    return out


def env_points(x0, y0, w, h, a, d, s, r):
    """ADSR as a polyline: a linear attack, exponential decay and release (analog-style)."""
    ta, td, tr = 6 + a * 0.5, 8 + d * 0.6, 8 + r * 0.6
    hold = 34
    scale = w / (ta + td + hold + tr)
    ta, td, hold, tr = ta * scale, td * scale, hold * scale, tr * scale
    sl = s / 127
    pts = [(x0, y0 + h)]
    pts.append((x0 + ta, y0))
    for i in range(1, 13):
        t = i / 12
        lv = sl + (1 - sl) * math.exp(-4 * t)
        pts.append((x0 + ta + td * t, y0 + h - lv * h))
    xs = x0 + ta + td
    pts.append((xs + hold, y0 + h - sl * h))
    for i in range(1, 13):
        t = i / 12
        lv = sl * math.exp(-4 * t) * (1 - t)
        pts.append((xs + hold + tr * t, y0 + h - lv * h))
    marks = [x0 + ta, xs, xs + hold, xs + hold + tr]
    return pts, marks


def sun_cuts(hz, gaps=(4, 3, 3, 2, 2, 1), solid=(3, 4, 5, 6, 7, 8)):
    """The rows cut out of the sun, from the horizon up: wide gaps low, thin ones high."""
    rows, y = set(), hz - 1
    for g, sl in zip(gaps, solid):
        rows.update(range(y - g + 1, y + 1))
        y -= g + sl
    return rows


NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def note_name(semi):
    n = semi + 48
    return NOTE_NAMES[n % 12] + str(n // 12 - 1)


# white keys F3..G5: 16 of them, the FM-1's 27 keys
def keyboard(sc, x, y, kw, kh, white, black, led_of_white, led_on, led_glow, led_off, frame_c):
    for i in range(16):
        sc.fill(x + i * kw, y, kw - 1, kh, white)
        st = led_of_white(i)
        c = {2: led_on, 1: led_glow, 0: led_off}[st]
        sc.fill(x + i * kw + kw // 2 - 3, y + kh - 8, 5, 4, c)
    # black keys after F, G, A (not B), C, D (not E): F3 index 0
    pattern = [1, 1, 1, 0, 1, 1, 0]
    for i in range(15):
        if pattern[i % 7]:
            sc.fill(x + i * kw + kw - 4, y, 7, kh * 3 // 5, black)
    sc.frame(x - 1, y - 1, 16 * kw + 1, kh + 2, frame_c)


def step_led(i):
    """The key LED rules for the sequencer: 2 lit (a step with notes), 1 glow (an empty step in the
    pattern), 0 off; the playhead flips its step (lit -> off, glow -> lit)."""
    kind = SEQ[i][0]
    st = 1 if kind == "r" else 2
    if i == PLAYHEAD:
        st = 0 if st == 2 else 2
    return st


# ================================================================ direction A: MEMBRANE
A = dict(bg="#1B1D20", panel="#26292D", raised="#30343A", line="#3C4148", text="#ECE6D6", dim="#A6A298",
         led="#FF3020", led_ghost="#261010", led_bg="#0C0D0E", ink="#121314",
         sec={"DCO-1": "#E8823A", "DCO-2": "#E8B83A", "MIX": "#C8C0A8", "VCF": "#4FB0D0", "VCA": "#88C860",
              "ENV-1": "#D86A98", "ENV-2": "#B070C8", "LFO": "#9A88E0", "CHORUS": "#50C8B8", "SEQ": "#E85050"},
         sun=["#FFD040", "#FFB038", "#FF8C38", "#FF6848", "#F04870", "#D03890"])


def a_header(sc, sec, right):
    p = A
    sc.fill(0, 0, W, 24, p["panel"])
    col = p["sec"].get(sec, p["text"])
    w = sc.text_w(0, sec) + 16
    sc.fill(0, 0, w, 24, col)
    sc.text(0, 4, sec, p["ink"], align="c", w=w)
    sc.text(w + 8, 4, right, p["dim"])
    sc.fill(0, 24, W, 1, p["line"])


def a_footer(sc, left, mid, right):
    p = A
    sc.fill(0, 222, W, 18, p["panel"])
    sc.fill(0, 221, W, 1, p["line"])
    sc.text(4, 223, left, p["dim"])
    sc.text(0, 223, mid, p["text"], align="c", w=W)
    sc.text(0, 223, right, p["dim"], align="r", w=W - 4)


def a_ledbox(sc, x, y, w, h, s, dw=20, dh=40, t=4):
    p = A
    sc.fill(x, y, w, h, p["led_bg"])
    sc.frame(x, y, w, h, p["line"])
    sw = sc.seg7_w(len(s), dw, t)
    sc.seg7(x + (w - sw) // 2, y + (h - dh) // 2, s, dw, dh, t, p["led"], p["led_ghost"])


def a_patch(fonts):
    p = A
    sc = Screen(fonts, p["bg"])
    num, name = PATCH["a"]
    sc.fill(0, 0, W, 24, p["panel"])
    sc.text(8, 4, "fm1-8p", p["text"])
    sc.text(0, 4, "PATCH", p["dim"], align="r", w=W - 8)
    sc.fill(0, 24, W, 1, p["line"])
    # the only synthwave here: a banded sunset badge
    bx, by, bw, bh = 8, 32, 224, 36
    yy = by
    for c, hgt in zip(p["sun"], (8, 8, 8, 5, 4, 3)):
        sc.fill(bx, yy, bw, hgt, c)
        yy += hgt + (1 if hgt < 8 or c == p["sun"][2] else 0)
    sc.text(bx, by + 4, "POLY 2-DCO SYNTH", p["ink"], align="c", w=bw)
    a_ledbox(sc, 8, 78, 104, 52, num)
    sc.text(124, 82, "BANK A", p["dim"])
    sc.text(124, 100, "PATCH 17", p["text"])
    sc.text(0, 142, name, p["text"], big=1, align="c", w=W)
    sc.fill(8, 182, 224, 30, p["panel"])
    sc.frame(8, 182, 224, 30, p["line"])
    sc.text(16, 189, "6 VOICES", p["text"])
    sc.text(0, 189, "CHORUS II", p["sec"]["CHORUS"], align="r", w=224)
    a_footer(sc, "<", "PRESETS: BROWSE", ">")
    return sc


def a_vcf(fonts):
    p = A
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["VCF"]
    a_header(sc, "VCF", "A17 GLASS HARBOR")
    num = VCF[ACTIVE_VCF][0]
    a_ledbox(sc, 8, 32, 66, 50, num)
    # the response in a framed window
    gx, gy, gw, gh = 82, 32, 150, 50
    sc.fill(gx, gy, gw, gh, p["led_bg"])
    sc.frame(gx, gy, gw, gh, p["line"])
    db = filter_curve(VCF[0][2], VCF[1][2], n=gw - 4)
    pts = [(gx + 2 + i, gy + 6 + min(gh - 9, max(0, (12 - d) * (gh - 9) / 48))) for i, d in enumerate(db)]
    sc.fill(gx + 2, gy + 6 + 12 * (gh - 9) // 48, gw - 4, 1, p["line"])
    sc.poly(pts, col, thick=2)
    for k, (n, label, v) in enumerate(VCF):
        y = 92 + k * 32
        act = k == ACTIVE_VCF
        sc.fill(8, y, 224, 28, p["raised"] if act else p["panel"])
        sc.fill(8, y, 28, 28, p["led_bg"])
        sc.text(8, y + 6, n, p["led"] if act else mix(p["led"], p["led_bg"], 0.55), align="c", w=28)
        sc.text(44, y + 3, "K%d %s" % (k + 1, label), p["text"] if act else p["dim"])
        sc.text(0, y + 3, "%3d" % v, p["text"], align="r", w=226)
        bw = 182 * v // 127
        sc.fill(44, y + 21, 182, 3, p["line"])
        sc.fill(44, y + 21, bw, 3, col if act else mix(col, p["panel"], 0.4))
        if act:
            sc.frame(8, y, 224, 28, col)
    a_footer(sc, "< MIX", "VCF", "VCA >")
    return sc


def a_env(fonts):
    p = A
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["ENV-1"]
    a_header(sc, "ENV-1", "A17 GLASS HARBOR")
    gx, gy, gw, gh = 8, 32, 224, 96
    sc.fill(gx, gy, gw, gh, p["led_bg"])
    sc.frame(gx, gy, gw, gh, p["line"])
    a, d, s, r = (e[2] for e in ENV1)
    pts, marks = env_points(gx + 6, gy + 10, gw - 12, gh - 22, a, d, s, r)
    for m in marks[:-1]:
        for yy in range(gy + 4, gy + gh - 4, 4):
            sc.fill(int(m), yy, 1, 2, p["line"])
    sc.poly(pts, mix(col, p["led_bg"], 0.25), thick=2)
    seg = [pt for pt in pts if marks[0] <= pt[0] <= marks[1]]
    sc.poly(seg, col, thick=3)
    for k, lab in enumerate("ADSR"):
        x0 = gx + 6 if k == 0 else marks[k - 1]
        x1 = marks[k]
        sc.text(int((x0 + x1) / 2) - 4, gy + gh - 18, lab, p["text"] if k == ACTIVE_ENV else p["dim"])
    for k, (n, label, v, unit) in enumerate(ENV1):
        x = 8 + k * 57
        y = 136
        act = k == ACTIVE_ENV
        sc.fill(x, y, 53, 80, p["raised"] if act else p["panel"])
        sc.fill(x, y, 53, 18, p["led_bg"])
        sc.text(x, y + 1, n, p["led"] if act else mix(p["led"], p["led_bg"], 0.55), align="c", w=53)
        sc.text(x, y + 22, label, p["text"] if act else p["dim"], align="c", w=53)
        sc.text(x, y + 40, "%d" % v, p["text"], big=0, align="c", w=53)
        sc.text(x, y + 58, unit, p["dim"], align="c", w=53)
        sc.fill(x + 4, y + 75, 45, 2, p["line"])
        sc.fill(x + 4, y + 75, 45 * v // 127, 2, col)
        if act:
            sc.frame(x, y, 53, 80, col)
    a_footer(sc, "< VCA", "ENV-1", "ENV-2 >")
    return sc


def a_seq(fonts):
    p = A
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["SEQ"]
    a_header(sc, "SEQ", "PLAY  112 BPM")
    pw, ph, gap = 26, 34, 2
    for i, (kind, lab, notes) in enumerate(SEQ):
        x = 9 + (i % 8) * (pw + gap)
        y = 32 + (i // 8) * (ph + 6)
        play = i == PLAYHEAD
        face = p["text"] if play else (p["raised"] if kind != "r" else p["panel"])
        ink = p["ink"] if play else p["text"]
        sc.fill(x, y, pw, ph, face)
        sc.text(x + 2, y + 1, "%d" % (i + 1), p["ink"] if play else p["dim"])
        if kind in "nc":
            sc.text(x, y + 17, lab, ink, align="c", w=pw)
        elif kind == "t":
            sc.fill(x - gap, y + 23, pw + gap - 4, 3, col)
            sc.text(x, y + 13, ">", col, align="r", w=pw - 1)
        if i == EDIT_STEP:
            sc.frame(x, y, pw, ph, col, 2)
        if play:
            sc.fill(x + pw - 7, y + 3, 4, 4, p["led"])
    a_ledbox(sc, 8, 116, 66, 50, "05")
    sc.text(84, 118, "STEP 5  CHORD", p["text"])
    sc.text(84, 136, "C3 Eb3 G3 Bb3", col)
    sc.text(84, 154, "TIED TO 6", p["dim"])
    keyboard(sc, 8, 176, 14, 28, p["text"], p["ink"], step_led, p["led"], p["led_ghost"], p["raised"], p["line"])
    a_footer(sc, "LEN 16", "", "SWING 54")
    return sc


# ================================================================ direction B: OUTRUN
B = dict(bg="#0A0620", plate="#140C34", grid="#3A1A6A", grid_dim="#22104A", text="#F4ECFF", dim="#9A88C8",
         mag="#FF2BD6", cyan="#20E8FF", orange="#FF8A1F", yellow="#FFD23F",
         sec={"DCO-1": "#FF2BD6", "DCO-2": "#B04CFF", "MIX": "#F4ECFF", "VCF": "#20E8FF", "VCA": "#3DFFA8",
              "ENV-1": "#FF8A1F", "ENV-2": "#FFD23F", "LFO": "#FF5A8A", "CHORUS": "#4D9BFF", "SEQ": "#FF2BD6"},
         sun=["#FFE040", "#FFC038", "#FFA038", "#FF7848", "#FF5068", "#F03890", "#D028B8", "#A020C8"])


def b_header(sc, sec, right, num=None):
    p = B
    col = p["sec"].get(sec, p["text"])
    sc.text(8, 4, sec, col)
    sc.text(sc.text_w(0, sec) + 16, 4, right, p["dim"])
    if num:
        sc.seg7(W - 8 - sc.seg7_w(2, 9, 2), 4, num, 9, 16, 2, p["cyan"], mix(p["cyan"], p["bg"], 0.9))
    # a banded neon rule: four colors, four fills
    for i, c in enumerate([p["mag"], p["orange"], p["yellow"], p["cyan"]]):
        sc.fill(i * 60, 23, 60, 2, c)


def b_grid(sc, y0, y1, horizon_c, line_c, cx=120):
    sc.fill(0, y0, W, 1, horizon_c)
    depth = y1 - y0
    for k in range(-9, 10):
        sc.line(cx + k * 8, y0, cx + k * 60, y1 + depth, line_c)
    yy, step = y0 + 3, 3.0
    while yy < y1:
        sc.fill(0, int(yy), W, 1, line_c)
        step *= 1.45
        yy += step


def b_glow_text(sc, x, y, s, c, big=1, align="l", w=0):
    for dx, dy in ((-1, 0), (1, 0), (0, -1), (0, 1)):
        sc.text(x + dx, y + dy, s, mix(B["bg"], c, 0.35), big=big, align=align, w=w)
    sc.text(x, y, s, c, big=big, align=align, w=w)


def b_patch(fonts):
    p = B
    sc = Screen(fonts, p["bg"])
    num, name = PATCH["b"]
    sc.text(8, 4, "fm1-8p", p["mag"])
    sc.text(0, 4, "PATCH", p["dim"], align="r", w=W - 8)
    sc.seg7(8, 28, num, 18, 34, 3, p["cyan"], mix(p["cyan"], p["bg"], 0.9))
    sc.text(86, 30, "BANK A / 17", p["dim"])
    sc.text(86, 46, "6V  CHORUS II", p["cyan"])
    b_glow_text(sc, 0, 74, name, p["text"], align="c", w=W)
    # the sun: one fill per row, cut by widening gaps, banded in 8 colors
    hz = 168
    cy, r = 162, 54
    def row_c(y):
        i = min(len(p["sun"]) - 1, max(0, (y - (cy - r)) * len(p["sun"]) // (hz - (cy - r))))
        return p["sun"][i]
    cuts = sun_cuts(hz)
    sc.disc_rows(120, cy, r, row_c, lambda y: y in cuts, y_max=hz)
    sc.fill(0, hz + 1, W, H - hz - 1, p["bg"])
    b_grid(sc, hz, H, p["mag"], p["grid"])
    sc.fill(0, 222, W, 18, p["bg"])
    sc.text(0, 223, "< PRESETS >", p["text"], align="c", w=W)
    return sc


def b_knob(sc, x, y, w, h, n, label, v, col, act):
    p = B
    sc.fill(x, y, w, h, p["plate"])
    cx, cy, r = x + 30, y + 32, 24
    sc.arc(cx, cy, r, 135, 405, p["grid"], thick=4)
    end = 135 + 270 * v / 127
    if act:
        sc.arc(cx, cy, r + 2, 135, end, mix(p["bg"], col, 0.4), thick=2)
        sc.arc(cx, cy, r - 4, 135, end, mix(p["bg"], col, 0.4), thick=1)
    sc.arc(cx, cy, r, 135, end, col if act else mix(col, p["bg"], 0.35), thick=4)
    a = math.radians(end)
    sc.line(cx, cy, cx + (r - 8) * math.cos(a), cy + (r - 8) * math.sin(a), p["text"] if act else p["dim"])
    sc.text(cx - 12, cy + 14, n, col if act else p["dim"])
    tx = x + 60
    sc.text(tx, y + 6, label, p["text"] if act else p["dim"])
    sc.text(tx, y + 26, "%d" % v, p["text"] if act else p["dim"], big=1)
    if act:
        sc.glow(x, y, w, h, col, p["bg"])


def b_vcf(fonts):
    p = B
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["VCF"]
    b_header(sc, "VCF", "NEON TIDE", VCF[ACTIVE_VCF][0])
    # the response over a floor grid: one 240 x 64 canvas, redrawn when CUTOFF / RESO change
    gy, gh = 30, 64
    b_grid(sc, gy + gh - 18, gy + gh, p["grid_dim"], p["grid_dim"])
    db = filter_curve(VCF[0][2], VCF[1][2], n=224)
    pts = [(8 + i, gy + 4 + min(gh - 8, max(0, (12 - d) * (gh - 8) / 48))) for i, d in enumerate(db)]
    sc.poly([(x, y + 2) for x, y in pts], mix(p["bg"], col, 0.3), thick=1)
    sc.poly([(x, y - 2) for x, y in pts], mix(p["bg"], col, 0.3), thick=1)
    sc.poly(pts, col, thick=2)
    for k, (n, label, v) in enumerate(VCF):
        x = 6 + (k % 2) * 116
        y = 100 + (k // 2) * 62
        b_knob(sc, x, y, 112, 58, n, label, v, col, k == ACTIVE_VCF)
    sc.text(0, 225, "< MIX", p["dim"])
    sc.text(0, 225, "VCA >", p["dim"], align="r", w=W)
    for i, s in enumerate(PAGES):
        c = p["sec"][s]
        sc.fill(84 + i * 8, 231, 6 if s == "VCF" else 4, 6 if s == "VCF" else 4, c if s == "VCF" else mix(p["bg"], c, 0.5))
    return sc


def b_env(fonts):
    p = B
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["ENV-1"]
    b_header(sc, "ENV-1", "NEON TIDE", ENV1[ACTIVE_ENV][0])
    gx, gy, gw, gh = 8, 32, 224, 92
    a, d, s, r = (e[2] for e in ENV1)
    pts, marks = env_points(gx, gy + 6, gw, gh - 10, a, d, s, r)
    # scanline fill under the curve: every other column, on a canvas
    for x in range(gx, gx + gw, 2):
        ys = [pt[1] for pt in pts if abs(pt[0] - x) < 3]
        if ys:
            top = int(min(ys)) + 3
            sc.fill(x, top, 1, gy + gh - 4 - top, mix(p["bg"], col, 0.22))
    sc.fill(gx, gy + gh - 4, gw, 1, p["grid"])
    sc.poly([(x, y - 2) for x, y in pts], mix(p["bg"], col, 0.35))
    sc.poly([(x, y + 2) for x, y in pts], mix(p["bg"], col, 0.35))
    sc.poly(pts, col, thick=2)
    seg = [pt for pt in pts if marks[0] <= pt[0] <= marks[1]]
    sc.poly(seg, p["text"], thick=2)
    for k, (n, label, v, unit) in enumerate(ENV1):
        x = 6 + (k % 2) * 116
        y = 130 + (k // 2) * 46
        act = k == ACTIVE_ENV
        sc.fill(x, y, 112, 42, p["plate"])
        sc.text(x + 6, y + 4, n, col if act else p["dim"])
        sc.text(x + 6, y + 22, label, p["text"] if act else p["dim"])
        sc.text(x + 106 - sc.text_w(1, "%d" % v), y + 6, "%d" % v, p["text"] if act else p["dim"], big=1)
        sc.text(x + 40, y + 22, unit, mix(p["bg"], col, 0.8) if act else p["dim"])
        if act:
            sc.glow(x, y, 112, 42, col, p["bg"])
    sc.text(0, 225, "< VCA", p["dim"])
    sc.text(0, 225, "ENV-2 >", p["dim"], align="r", w=W)
    return sc


def b_seq(fonts):
    p = B
    sc = Screen(fonts, p["bg"])
    col = p["mag"]
    b_header(sc, "SEQ", "112 BPM  LEN 16", "05")
    # a piano-roll lane: 16 columns of 14 px, pitch up
    lx, ly, lh = 8, 32, 96
    lo, hi = -6, 13
    def ny(semi):
        return ly + lh - 6 - (semi - lo) * (lh - 10) // (hi - lo)
    for semi in range(lo, hi + 1):
        if semi % 12 == 0:
            sc.fill(lx, ny(semi) + 2, 224, 1, p["grid"])
    for i in range(17):
        sc.fill(lx + i * 14 - 1, ly, 1, lh, p["grid"] if i % 4 == 0 else p["grid_dim"])
    sc.fill(lx + PLAYHEAD * 14, ly, 13, lh, mix(p["bg"], p["cyan"], 0.18))
    for i, (kind, lab, notes) in enumerate(SEQ):
        x = lx + i * 14
        c = p["cyan"] if i == PLAYHEAD else (p["orange"] if kind == "c" else col)
        if kind == "t":
            c = mix(c, p["bg"], 0.25)
        for semi in notes:
            sc.fill(x - (2 if kind == "t" else 0), ny(semi), 13 + (2 if kind == "t" else 0), 4, c)
        if i == EDIT_STEP:
            sc.glow(x - 1, ly + 1, 15, lh - 2, p["orange"], p["bg"], rings=(1.0, 0.4))
    sc.fill(lx + PLAYHEAD * 14 + 6, ly, 1, lh, p["cyan"])
    sc.text(8, 134, "STEP 5", p["dim"])
    b_glow_text(sc, 8, 150, "C MIN7", p["orange"], big=1)
    sc.text(120, 134, "C3 Eb3 G3 Bb3", p["text"])
    sc.text(120, 152, "TIE > 6", p["orange"])
    sc.text(120, 168, "SWING 54", p["dim"])
    keyboard(sc, 8, 190, 14, 28, p["text"], p["bg"], step_led, p["mag"], mix(p["bg"], p["mag"], 0.4), p["grid_dim"], p["grid"])
    sc.text(0, 223, "> PLAY", p["cyan"], align="c", w=W)
    return sc


# ================================================================ direction C: NIGHT PANEL
C = dict(bg="#0E0E1A", panel="#181828", raised="#22223A", line="#2E2E4C", text="#EEECF8", dim="#9896B8",
         led_bg="#07070E",
         sec={"DCO-1": "#FF5FA0", "DCO-2": "#FF8A3D", "MIX": "#D8D4F0", "VCF": "#2FD8FF", "VCA": "#7CF07C",
              "ENV-1": "#FFC23D", "ENV-2": "#FFE070", "LFO": "#B07CFF", "CHORUS": "#3DA0FF", "SEQ": "#FF3DD8"},
         sun=["#FFD84A", "#FFB040", "#FF8840", "#FF5C60", "#F03C90", "#C030C0"], grid="#5A2A9A", hz="#FF3DD8")


def c_header(sc, sec, title, right):
    p = C
    col = p["sec"].get(sec, p["text"])
    sc.fill(0, 0, W, 3, col)
    sc.text(8, 5, sec, col)
    sc.text(sc.text_w(0, sec) + 16, 5, title, p["dim"])
    sc.text(0, 5, right, p["text"], align="r", w=W - 8)
    sc.fill(0, 23, W, 1, p["line"])


def c_pages(sc, cur):
    """The section strip: one dot per page in its own color, the current one big (and the
    section buttons' LEDs agree with it)."""
    p = C
    x = (W - (len(PAGES) * 12 - 4)) // 2
    for i, s in enumerate(PAGES):
        c = p["sec"][s]
        if s == cur:
            sc.fill(x + i * 12 - 1, 229, 10, 6, c)
        else:
            sc.fill(x + i * 12 + 1, 231, 6, 2, mix(p["bg"], c, 0.55))


def c_ledbox(sc, x, y, w, h, s, col, dw=20, dh=40, t=4):
    p = C
    sc.fill(x, y, w, h, p["led_bg"])
    sc.frame(x, y, w, h, p["line"])
    sw = sc.seg7_w(len(s), dw, t)
    sc.seg7(x + (w - sw) // 2, y + (h - dh) // 2, s, dw, dh, t, col, mix(p["led_bg"], col, 0.08))


def c_ladder(sc, x, y, n, segw, segh, gap, v, col, act, vertical=True):
    p = C
    lit = round(n * v / 127)
    for i in range(n):
        on = i < lit
        c = (col if act else mix(p["panel"], col, 0.6)) if on else p["raised"]
        if vertical:
            sc.fill(x, y + (n - 1 - i) * (segh + gap), segw, segh, c)
        else:
            sc.fill(x + i * (segw + gap), y, segw, segh, c)


def c_sun_scene(sc, y0, y1):
    p = C
    hz = y0 + (y1 - y0) * 5 // 9
    cy, r = hz + 6, 40
    def row_c(y):
        i = min(len(p["sun"]) - 1, max(0, (y - (cy - r)) * len(p["sun"]) // (hz - (cy - r))))
        return p["sun"][i]
    cuts = sun_cuts(hz, (3, 2, 2, 1, 1), (3, 4, 5, 6, 7))
    sc.disc_rows(120, cy, r, row_c, lambda y: y in cuts, y_max=hz)
    sc.fill(0, hz, W, 1, p["hz"])
    for k in range(-8, 9):
        sc.line(120 + k * 10, hz, 120 + k * 46, y1 + (y1 - hz), p["grid"])
    yy, step = hz + 2, 2.0
    while yy < y1:
        sc.fill(0, int(yy), W, 1, p["grid"])
        step *= 1.5
        yy += step
    sc.fill(0, y1, W, H - y1, p["bg"])


def c_patch(fonts):
    p = C
    sc = Screen(fonts, p["bg"])
    num, name = PATCH["c"]
    col = p["sec"]["SEQ"]
    sc.fill(0, 0, W, 3, col)
    sc.text(8, 5, "fm1-8p", p["text"])
    sc.text(0, 5, "PATCH", p["dim"], align="r", w=W - 8)
    sc.fill(0, 23, W, 1, p["line"])
    c_ledbox(sc, 8, 30, 104, 52, num, col)
    sc.text(122, 34, "BANK A", p["dim"])
    sc.text(122, 52, "6 VOICES", p["text"])
    sc.text(122, 68, "CHORUS II", p["sec"]["CHORUS"])
    sc.text(0, 92, name, p["text"], big=1, align="c", w=W)
    c_sun_scene(sc, 130, 222)
    sc.text(0, 223, "< PRESETS >", p["dim"], align="c", w=W)
    return sc


def c_vcf(fonts):
    p = C
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["VCF"]
    c_header(sc, "VCF", "FILTER", "A17")
    c_ledbox(sc, 8, 30, 64, 64, VCF[ACTIVE_VCF][0], col)
    gx, gy, gw, gh = 80, 30, 152, 64
    sc.fill(gx, gy, gw, gh, p["led_bg"])
    sc.frame(gx, gy, gw, gh, p["line"])
    db = filter_curve(VCF[0][2], VCF[1][2], n=gw - 4)
    yof = lambda d: gy + 6 + min(gh - 10, max(0, (12 - d) * (gh - 10) / 48))
    sc.fill(gx + 2, int(yof(0)), gw - 4, 1, p["line"])
    pts = [(gx + 2 + i, yof(d)) for i, d in enumerate(db)]
    for i, (x, y) in enumerate(pts):
        if i % 2 == 0:
            sc.fill(x, int(y) + 2, 1, gy + gh - 2 - int(y) - 2, mix(p["led_bg"], col, 0.16))
    sc.poly(pts, col, thick=2)
    for k, (n, label, v) in enumerate(VCF):
        x = 8 + k * 57
        act = k == ACTIVE_VCF
        sc.fill(x, 102, 53, 120, p["raised"] if act else p["panel"])
        sc.text(x, 104, n, col if act else mix(p["panel"], col, 0.6), align="c", w=53)
        sc.text(x, 120, label, p["text"] if act else p["dim"], align="c", w=53)
        c_ladder(sc, x + 15, 140, 8, 23, 4, 2, v, col, act)
        sc.text(x, 190, "%d" % v, p["text"] if act else p["dim"], big=1, align="c", w=53)
        if act:
            sc.glow(x, 102, 53, 120, col, p["bg"])
    c_pages(sc, "VCF")
    return sc


def c_env(fonts):
    p = C
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["ENV-1"]
    c_header(sc, "ENV-1", "VCF+VCA", "A17")
    gx, gy, gw, gh = 8, 30, 224, 96
    sc.fill(gx, gy, gw, gh, p["led_bg"])
    sc.frame(gx, gy, gw, gh, p["line"])
    a, d, s, r = (e[2] for e in ENV1)
    pts, marks = env_points(gx + 8, gy + 10, gw - 16, gh - 30, a, d, s, r)
    for m in marks[:-1]:
        for yy in range(gy + 4, gy + gh - 20, 4):
            sc.fill(int(m), yy, 1, 2, p["line"])
    base = gy + gh - 20
    for x in range(gx + 8, gx + gw - 8, 2):
        ys = [pt[1] for pt in pts if abs(pt[0] - x) < 3]
        if ys:
            top = int(min(ys)) + 3
            if top < base:
                inside = marks[0] <= x <= marks[1]
                sc.fill(x, top, 1, base - top, mix(p["led_bg"], col, 0.28 if inside else 0.12))
    sc.fill(gx + 8, base, gw - 16, 1, p["line"])
    sc.poly(pts, mix(p["led_bg"], col, 0.6), thick=2)
    seg = [pt for pt in pts if marks[0] <= pt[0] <= marks[1]]
    sc.poly(seg, col, thick=3)
    for k, lab in enumerate("ADSR"):
        x0 = gx + 8 if k == 0 else marks[k - 1]
        sc.text(int((x0 + marks[k]) / 2) - 4, base + 2, lab, col if k == ACTIVE_ENV else p["dim"])
    for k, (n, label, v, unit) in enumerate(ENV1):
        x = 8 + k * 57
        act = k == ACTIVE_ENV
        sc.fill(x, 132, 53, 90, p["raised"] if act else p["panel"])
        sc.text(x, 134, "%s %s" % (n, label), p["text"] if act else p["dim"], align="c", w=53)
        sc.text(x, 152, "%d" % v, p["text"] if act else p["dim"], big=1, align="c", w=53)
        c_ladder(sc, x + 4, 188, 9, 3, 6, 2, v, col, act, vertical=False)
        sc.text(x, 200, unit, col if act else p["dim"], align="c", w=53)
        if act:
            sc.glow(x, 132, 53, 90, col, p["bg"])
    c_pages(sc, "ENV-1")
    return sc


def c_seq(fonts):
    p = C
    sc = Screen(fonts, p["bg"])
    col = p["sec"]["SEQ"]
    play = p["sec"]["VCF"]
    c_header(sc, "SEQ", "> PLAY", "112 BPM")
    pw, ph, gap = 26, 44, 2
    for i, (kind, lab, notes) in enumerate(SEQ):
        x = 9 + (i % 8) * (pw + gap)
        y = 30 + (i // 8) * (ph + 6)
        is_play = i == PLAYHEAD
        face = p["raised"] if kind != "r" else p["panel"]
        sc.fill(x, y, pw, ph, face)
        sc.text(x, y + 1, "%d" % (i + 1), p["dim"] if not is_play else play, align="c", w=pw)
        if kind == "t":
            # a tie: the note's bars run on from the step before, through the gap
            for j in range(len(notes)):
                sc.fill(x - gap - 4, y + ph - 8 - j * 6, pw + gap, 4, mix(p["raised"], col, 0.45))
        elif kind in "nc":
            n = len(notes)
            for j in range(n):
                sc.fill(x + 4, y + ph - 8 - j * 6, pw - 8, 4, col)
        if i == EDIT_STEP:
            sc.frame(x, y, pw, ph, p["text"], 2)
        if is_play:
            sc.glow(x, y, pw, ph, play, p["bg"])
    c_ledbox(sc, 8, 132, 64, 52, "05", col)
    sc.text(80, 134, "STEP 5 CHORD", p["dim"])
    sc.text(80, 150, "C MIN7", p["text"], big=1)
    sc.text(0, 134, "L16", p["dim"], align="r", w=W - 8)
    sc.text(80, 188, "C3 Eb3 G3 Bb3", col)
    sc.text(0, 188, "SW54", p["dim"], align="r", w=W - 8)
    keyboard(sc, 8, 206, 14, 22, p["text"], p["bg"], step_led, col, mix(p["bg"], col, 0.4), p["line"], p["line"])
    return sc


DIRECTIONS = [
    ("a-membrane", [("1-patch", a_patch), ("2-vcf", a_vcf), ("3-env", a_env), ("4-seq", a_seq)]),
    ("b-outrun", [("1-patch", b_patch), ("2-vcf", b_vcf), ("3-env", b_env), ("4-seq", b_seq)]),
    ("c-night-panel", [("1-patch", c_patch), ("2-vcf", c_vcf), ("3-env", c_env), ("4-seq", c_seq)]),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    dock = os.environ.get("FM1_DOCK", os.path.expanduser("~/Projects/fm1-8p-ref/fm1-dock"))
    ap.add_argument("--font", default=os.path.join(dock, "firmware/gen/felucca_font.h"))
    ap.add_argument("--contrast", action="store_true", help="print each palette's text contrast")
    args = ap.parse_args()
    if not os.path.exists(args.font):
        sys.exit("no host font at %s (set $FM1_DOCK or --font)" % args.font)
    fonts = load_font(args.font)
    sheet = Image.new("RGB", (4 * W + 5 * 8, 3 * H + 4 * 8), (40, 40, 40))
    for row, (d, screens) in enumerate(DIRECTIONS):
        os.makedirs(os.path.join(OUT, d), exist_ok=True)
        for col, (name, fn) in enumerate(screens):
            sc = fn(fonts)
            sc.save(os.path.join(OUT, d, name))
            sheet.paste(sc.img, (8 + col * (W + 8), 8 + row * (H + 8)))
            print(os.path.join(d, name) + "-3x.png")
    sheet.save(os.path.join(OUT, "compare.png"))
    if args.contrast:
        for tag, p in (("A", A), ("B", B), ("C", C)):
            bgs = [k for k in ("bg", "panel", "raised", "plate", "led_bg") if k in p]
            for fg in ("text", "dim"):
                print(tag, fg, " ".join("%s %.1f" % (b, contrast(p[fg], p[b])) for b in bgs))
            for s, c in p["sec"].items():
                print(tag, s, "%04X" % rgb565(c), "%.1f" % contrast(c, p["bg"]))


if __name__ == "__main__":
    main()
