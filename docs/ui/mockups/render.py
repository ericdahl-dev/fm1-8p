#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the fm1-8p on-device UI mockups: two themes x five screens, and the display font.

One layout, two themes. Every screen function below draws from the shared grid (LAYOUT) and
reads its colors and style choices from a theme table (THEMES). Switching theme swaps the
table and nothing else, which is how the firmware is meant to do it.

Every screen is drawn at the panel's own 240 x 240 with only what the fm1-dock host gives a
module (fill, line, pixel, the host's 16 px font, blit). It is then quantized to RGB565 and
expanded back the way the panel shows it. Each screen is saved at 1x and as a 3x
nearest-neighbor upscale (720 x 720) for a phone.

    python3 docs/ui/mockups/render.py [--font PATH/TO/felucca_font.h] [--contrast]

Outputs, all next to this script:
    membrane/*.png, night-panel/*.png   the screens (and *-3x.png)
    compare.png                         both themes side by side
    font-specimen.png (+ -3x)           the display font at both sizes
    display_font.h                      the display font, 1-bit packed, as the firmware reads it

Needs Pillow. The small text is the host's own font (Terminus 8x16), read from fm1-dock's
firmware/gen/felucca_font.h: by default ~/Projects/fm1-8p-ref/fm1-dock, or $FM1_DOCK, or
--font. Without it the script stops: a mockup in another font would mislead.
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
    """a toward b by t (0..1), quantized: a glow step, a ghost segment, a dimmed accent."""
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


# ---------------------------------------------------------------- the host's font (Terminus 8x16)
def load_host_font(path):
    src = open(path).read()

    def arr(name):
        m = re.search(r"%s\[\d+\]\s*=\s*\{([^}]*)\}" % name, src)
        return [int(v, 0) for v in m.group(1).replace("\n", " ").split(",") if v.strip()]

    m = re.search(r"felucca_font_t FONT_S\s*=\s*\{\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", src)
    h, pad, first, last = (int(v) for v in m.groups())
    return dict(h=h, pad=pad, first=first, last=last, adv=arr("FONT_S_ADV"), bw=arr("FONT_S_BW"),
                off=arr("FONT_S_OFF"), data=arr("FONT_S_DATA"))


# ---------------------------------------------------------------- fm1-8p's own display font
# An original geometric face, designed here as strokes on a grid 4 units wide (some glyphs wider
# or narrower) and 6 units tall, with 45-degree chamfers in place of curves. Each size is the
# same strokes drawn with a square pen and baked to 1 bit per pixel, so both sizes come from one
# design. Upper case, digits and - + . % # / : and space only.
GLYPHS = {
    "0": (4, [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)], [(1, 4.4), (3, 1.6)]]),
    "1": (3, [[(0, 1.2), (1.5, 0), (1.5, 6)], [(0, 6), (3, 6)]]),
    "2": (4, [[(0, 1), (1, 0), (3, 0), (4, 1), (4, 2.4), (0, 6), (4, 6)]]),
    "3": (4, [[(0, 0), (4, 0), (1.6, 2.6), (3, 2.6), (4, 3.6), (4, 5), (3, 6), (1, 6), (0, 5)]]),
    "4": (4, [[(3, 6), (3, 0), (0, 4), (4, 4)]]),
    "5": (4, [[(4, 0), (0, 0), (0, 2.6), (3, 2.6), (4, 3.6), (4, 5), (3, 6), (0, 6)]]),
    "6": (4, [[(3.6, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3.6), (3, 2.6), (0, 2.6)]]),
    "7": (4, [[(0, 0), (4, 0), (4, 1), (1.6, 6)]]),
    "8": (4, [[(1, 0), (3, 0), (4, 1), (4, 2), (3, 2.8), (1, 2.8), (0, 2), (0, 1), (1, 0)],
              [(1, 2.8), (0, 3.8), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3.8), (3, 2.8)]]),
    "9": (4, [[(0.4, 6), (3, 6), (4, 5), (4, 1), (3, 0), (1, 0), (0, 1), (0, 2.4), (1, 3.4), (4, 3.4)]]),
    "A": (4, [[(0, 6), (0, 1), (1, 0), (3, 0), (4, 1), (4, 6)], [(0, 3.4), (4, 3.4)]]),
    "B": (4, [[(0, 3), (3, 3), (4, 2), (4, 1), (3, 0), (0, 0), (0, 6), (3, 6), (4, 5), (4, 4), (3, 3)]]),
    "C": (4, [[(4, 0.8), (3.2, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3.2, 6), (4, 5.2)]]),
    "D": (4, [[(0, 0), (2.6, 0), (4, 1.4), (4, 4.6), (2.6, 6), (0, 6), (0, 0)]]),
    "E": (4, [[(4, 0), (0, 0), (0, 6), (4, 6)], [(0, 3), (3, 3)]]),
    "F": (4, [[(4, 0), (0, 0), (0, 6)], [(0, 3), (3, 3)]]),
    "G": (4, [[(4, 0.8), (3.2, 0), (1, 0), (0, 1), (0, 5), (1, 6), (3, 6), (4, 5), (4, 3.2), (2.2, 3.2)]]),
    "H": (4, [[(0, 0), (0, 6)], [(4, 0), (4, 6)], [(0, 3), (4, 3)]]),
    "I": (2, [[(0, 0), (2, 0)], [(1, 0), (1, 6)], [(0, 6), (2, 6)]]),
    "J": (4, [[(1.6, 0), (4, 0), (4, 5), (3, 6), (1, 6), (0, 5)]]),
    "K": (4, [[(0, 0), (0, 6)], [(4, 0), (1, 3), (0, 3)], [(1, 3), (4, 6)]]),
    "L": (4, [[(0, 0), (0, 6), (4, 6)]]),
    "M": (5, [[(0, 6), (0, 0), (2.5, 2.6), (5, 0), (5, 6)]]),
    "N": (4, [[(0, 6), (0, 0), (4, 6), (4, 0)]]),
    "O": (4, [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)]]),
    "P": (4, [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2.4), (3, 3.4), (0, 3.4)]]),
    "Q": (4, [[(1, 0), (3, 0), (4, 1), (4, 5), (3, 6), (1, 6), (0, 5), (0, 1), (1, 0)], [(2.4, 4.4), (4, 6)]]),
    "R": (4, [[(0, 6), (0, 0), (3, 0), (4, 1), (4, 2.4), (3, 3.4), (0, 3.4)], [(2, 3.4), (4, 6)]]),
    "S": (4, [[(4, 0.8), (3.2, 0), (1, 0), (0, 1), (0, 2), (1, 3), (3, 3), (4, 4), (4, 5), (3, 6), (0.8, 6), (0, 5.2)]]),
    "T": (4, [[(0, 0), (4, 0)], [(2, 0), (2, 6)]]),
    "U": (4, [[(0, 0), (0, 5), (1, 6), (3, 6), (4, 5), (4, 0)]]),
    "V": (4, [[(0, 0), (0, 3.6), (2, 6), (4, 3.6), (4, 0)]]),
    "W": (5, [[(0, 0), (0, 6), (2.5, 3.4), (5, 6), (5, 0)]]),
    "X": (4, [[(0, 0), (0, 1), (4, 5), (4, 6)], [(4, 0), (4, 1), (0, 5), (0, 6)]]),
    "Y": (4, [[(0, 0), (0, 1.4), (2, 3.4), (4, 1.4), (4, 0)], [(2, 3.4), (2, 6)]]),
    "Z": (4, [[(0, 0), (4, 0), (4, 1), (0, 5), (0, 6), (4, 6)]]),
    "-": (3, [[(0, 3.2), (3, 3.2)]]),
    "+": (4, [[(0, 3.2), (4, 3.2)], [(2, 1.2), (2, 5.2)]]),
    ".": (0, [[(0, 6)]]),
    ":": (0, [[(0, 1.8)], [(0, 5)]]),
    "%": (5, [[(0.6, 6), (4.4, 0)], [(0, 0.2)], [(5, 5.8)]]),
    "#": (4, [[(1.2, 0), (1.2, 6)], [(2.8, 0), (2.8, 6)], [(0, 2), (4, 2)], [(0, 4), (4, 4)]]),
    "/": (3, [[(0, 6), (3, 0)]]),
    " ": (2, []),
}
FONT_ORDER = " #%+-./0123456789:ABCDEFGHIJKLMNOPQRSTUVWXYZ"
# the two sizes: cap height in px, pen (stroke) in px, px per horizontal unit, gap after a glyph
SIZES = {"D24": dict(h=24, pen=3, ux=2.0, gap=2), "D20": dict(h=20, pen=3, ux=2.0, gap=2)}


def rnd(v):
    """Half rounds up, always. (Python's round() rounds half to even, which made glyphs lopsided.)"""
    return math.floor(v + 0.5)


def mrnd(v, span):
    """Round a pen position in 0..span with halves toward the middle: below it as rnd(), above it
    as the mirror of rnd(). v and span - v then land on mirrored pixels, so O and H stay
    symmetric top to bottom and 0 under a half turn."""
    return rnd(v) if v <= span / 2 else span - rnd(span - v)


def bake_font(spec):
    h, pen, ux, gap = spec["h"], spec["pen"], spec["ux"], spec["gap"]
    uy = (h - pen) / 6
    font = {}
    for ch in FONT_ORDER:
        units, strokes = GLYPHS[ch]
        span_x = rnd(units * ux)
        w = span_x + pen if strokes else span_x + 1
        bits = [[0] * w for _ in range(h)]
        for path in strokes:
            span_y = h - pen
            # a bar exactly on the middle row of an odd span has no symmetric place for a 3 px
            # pen (24 px tall: rows 10.5..13.5): it is drawn one row thicker, centered, instead
            mid = [y * uy == span_y / 2 and span_y % 2 for _, y in path]
            pts = [(mrnd(x * ux, span_x), span_y // 2 if m else mrnd(y * uy, span_y))
                   for (x, y), m in zip(path, mid)]
            if len(pts) == 1:
                pts, mid = pts * 2, mid * 2
            for (x0, y0), (x1, y1), m0, m1 in zip(pts, pts[1:], mid, mid[1:]):
                n = max(abs(x1 - x0), abs(y1 - y0), 1)
                for i in range(n + 1):
                    px = mrnd(x0 + (x1 - x0) * i / n, span_x)
                    py = mrnd(y0 + (y1 - y0) * i / n, span_y)
                    for dy in range(pen + (1 if m0 and m1 else 0)):
                        for dx in range(pen):
                            if 0 <= px + dx < w and 0 <= py + dy < h:
                                bits[py + dy][px + dx] = 1
        font[ch] = dict(w=w, adv=w + gap, bits=bits)
    return font


def check_font(font, size):
    """O and H mirror top to bottom; 0 (its slash runs corner to corner) turns 180 degrees onto
    itself. A rounding slip shows up here before it shows up on the screen."""
    for ch in "OH":
        b = font[ch]["bits"]
        assert b == b[::-1], "%s %s is not vertically symmetric" % (size, ch)
    b = font["0"]["bits"]
    assert b == [row[::-1] for row in b[::-1]], "%s 0 is not symmetric" % size


def pack_font(name, font, h):
    """1 bit per pixel, MSB first, each row ceil(w / 8) bytes; a glyph's rows in order."""
    data, offs, widths = [], [], []
    for ch in FONT_ORDER:
        g = font[ch]
        offs.append(len(data))
        widths.append(g["w"])
        bpr = (g["w"] + 7) // 8
        for row in g["bits"]:
            for b in range(bpr):
                byte = 0
                for k in range(8):
                    x = b * 8 + k
                    if x < g["w"] and row[x]:
                        byte |= 0x80 >> k
                data.append(byte)
    return data, offs, widths


def write_font_header(path, fonts):
    total = 0
    out = ["/* SPDX-License-Identifier: GPL-3.0-only */",
           "/* fm1-8p's display font, generated by docs/ui/mockups/render.py: an original geometric face,",
           " * two sizes of one stroke design. Glyphs are 1 bit per pixel, MSB first, each row ceil(w / 8)",
           " * bytes, rows top to bottom. A glyph's index is its place in DFONT_CHARS; the advance is",
           " * width + the size's gap (D24_GAP, D20_GAP). Lower case folds to upper case; anything else draws as a space.",
           " * A firmware that uses one size defines DFONT_NO_D24 or DFONT_NO_D20 to leave the other out. */",
           "#pragma once", "#include <stdint.h>", "",
           '#define DFONT_CHARS "%s"' % FONT_ORDER, "#define DFONT_N %d" % len(FONT_ORDER), ""]
    for name, (font, spec) in fonts.items():
        data, offs, widths = pack_font(name, font, spec["h"])
        size = len(data) + 2 * len(offs) + len(widths)
        total += size
        out.append("/* %s: %d px tall, pen %d px; %d bytes of bitmaps + %d of tables = %d bytes */" % (
            name, spec["h"], spec["pen"], len(data), 3 * len(offs), size))
        out.append("#ifndef DFONT_NO_%s" % name)
        out.append("#define %s_H %d" % (name, spec["h"]))
        out.append("#define %s_GAP %d" % (name, spec["gap"]))
        out.append("static const uint8_t %s_W[DFONT_N] = {%s};" % (name, ", ".join(map(str, widths))))
        out.append("static const uint16_t %s_OFF[DFONT_N] = {%s};" % (name, ", ".join(map(str, offs))))
        out.append("static const uint8_t %s_DATA[%d] = {" % (name, len(data)))
        for i in range(0, len(data), 16):
            out.append("    " + ", ".join("0x%02x" % v for v in data[i:i + 16]) + ",")
        out.append("};")
        out.append("#endif")
        out.append("")
    out.insert(11, "/* total: %d bytes of flash */" % total)
    open(path, "w").write("\n".join(out))
    return total


# ---------------------------------------------------------------- a screen, drawn as a module would
class Screen:
    def __init__(self, fonts, bg):
        self.img = Image.new("RGB", (W, H), q(bg))
        self.px = self.img.load()
        self.host, self.dfont = fonts

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

    # the host's Terminus (S, 16 px)
    def _g(self, ch):
        f = self.host
        o = ord(ch)
        if o < f["first"] or o > f["last"]:
            o = ord("?")
        return o - f["first"]

    def text_w(self, s):
        return sum(self.host["adv"][self._g(ch)] for ch in s)

    def text(self, x, y, s, c, align="l", w=0):
        f = self.host
        tw = self.text_w(s)
        x = x + (w - tw) // 2 if align == "c" else (x + w - tw if align == "r" else x)
        for ch in s:
            gi = self._g(ch)
            bw, off = f["bw"][gi], f["off"][gi]
            bpr = (bw + 7) // 8
            for gy in range(f["h"]):
                for gx in range(bw):
                    if f["data"][off + gy * bpr + gx // 8] >> (7 - (gx & 7)) & 1:
                        self.pixel(x - f["pad"] + gx, y + gy, c)
            x += f["adv"][gi]
        return x

    # fm1-8p's display font (D24 values, D20 names): what the firmware blits from a row buffer
    def dtext_w(self, size, s):
        f = self.dfont[size]
        s = s.upper()
        return sum(f[ch if ch in f else " "]["adv"] for ch in s) - (SIZES[size]["gap"] if s else 0)

    def dtext(self, x, y, size, s, c, align="l", w=0):
        f = self.dfont[size]
        tw = self.dtext_w(size, s)
        x = x + (w - tw) // 2 if align == "c" else (x + w - tw if align == "r" else x)
        for ch in s.upper():
            g = f[ch if ch in f else " "]
            for gy, row in enumerate(g["bits"]):
                for gx, b in enumerate(row):
                    if b:
                        self.pixel(x + gx, y + gy, c)
            x += g["adv"]
        return x

    # 7-segment digits, drawn with fills: lit segments in `on`, the rest as faint ghost segments
    SEG = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
           "7": "abc", "8": "abcdefg", "9": "abcdfg", "A": "abcefg", "b": "cdefg", "C": "adef", "d": "bcdeg",
           "E": "adefg", "F": "aefg", "G": "acdef", "P": "abefg", "U": "bcdef", "-": "g", " ": ""}

    def seg7(self, x, y, s, dw, dh, t, on, ghost, gap=None):
        gap = t + 2 if gap is None else gap
        hh = (dh - 3 * t) // 2
        j = 1 if t >= 3 else 0                 # a hairline between segments, as on an LED
        for ch in s:
            lit = self.SEG.get(ch, "")
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

    @staticmethod
    def seg7_w(n, dw, t, gap=None):
        gap = t + 2 if gap is None else gap
        return n * dw + (n - 1) * gap

    def disc_rows(self, cx, cy, r, color_for_row, skip_row, y_max):
        """A disc as one fill per row (the sun): cheap on the device."""
        for y in range(int(cy - r), int(cy + r) + 1):
            if y >= y_max or skip_row(y):
                continue
            dx = int(math.sqrt(max(0, r * r - (y - cy) ** 2)))
            if dx < 3:                    # no 1 px nub on top
                continue
            self.fill(cx - dx, y, 2 * dx + 1, 1, color_for_row(y))

    def save(self, path):
        self.img.save(path + ".png")
        self.img.resize((W * 3, H * 3), Image.NEAREST).save(path + "-3x.png")


# ================================================================ the themes: data, not code
# Every key exists in both. A layout function never asks which theme it is drawing.
THEMES = {
    "membrane": dict(
        name="MEMBRANE",
        # the JX-8P's colors: the button strip's dark gray (screen), the body's mid gray (rules), lettering
        bg="#2E3135", surface="#3E4145", surface_hi="#46494E", line="#837E7A", text="#F6F5F0",
        dim="#C6C8CA", ink="#1E2023", window="#050506",
        led="#A8F0F4",              # one pale cyan for every LED window (the JX-8P's display)
        ghost=0.10,                 # unlit segments: this much of the LED color in the window
        header="chip",              # section name on a chip of its color
        meter_segments=0,           # 0: one continuous bar
        meter_gap=0,
        focus=(1.0, 1.0),           # frames inward from the edge, as mixes of bg -> accent
        hatch=0.0,                  # fill under graph curves (0: none)
        art="badge",                # what fills the graph slot on PLAY
        playhead="#F6F5F0", cursor="#DC9C9E", tie_mix=0.45,
        black_key="#1E2023",        # the key strip's black keys
        index=0, short="MEMB",      # THEME's value: its index, and its label in a 56 px column
        # the buttons: red (top left, COPY/WRITE), amber (KEY MODE, AFTER TOUCH), cream-gray (TONE),
        # light gray (EDIT), blue (PATCH CHAIN), lightened to meet the contrast rules
        sec={"DCO-1": "#DC9C9E", "DCO-2": "#D8AC5F", "MIX": "#CDCAC4", "VCF": "#8DB0D6", "VCF ENV": "#E5B6B7",
             "VCA": "#C2BCAE", "LFO": "#B0C8E2", "CTRL": "#CCA869", "SEQ": "#DC9C9E", "GLO": "#AAB0AC",
             "PLAY": "#D6D5CE"},
        sun=["#FFD040", "#FFB038", "#FF8C38", "#FF6848", "#F04870", "#D03890"], grid=None, horizon=None,
    ),
    "night-panel": dict(
        name="NIGHT PANEL",
        bg="#0E0E1A", surface="#181828", surface_hi="#22223A", line="#2E2E4C", text="#EEECF8",
        dim="#9896B8", ink="#0E0E1A", window="#07070E",
        led=None,                   # None: LED windows take the section's color
        ghost=0.08,
        header="band",              # a 3 px band of the section's color, the name in that color
        meter_segments=8,           # an LED ladder
        meter_gap=2,
        focus=(1.0, 0.45, 0.2),     # a glow
        hatch=0.16,
        art="sun",
        playhead="#2FD8FF", cursor="#EEECF8", tie_mix=0.45,
        black_key="#0E0E1A",
        index=1, short="NIGHT",
        sec={"DCO-1": "#FF5FA0", "DCO-2": "#FF8A3D", "MIX": "#D8D4F0", "VCF": "#2FD8FF", "VCF ENV": "#FFC23D",
             "VCA": "#7CF07C", "LFO": "#B07CFF", "CTRL": "#3DA0FF", "SEQ": "#FF3DD8", "GLO": "#D8D4F0",
             "PLAY": "#FF3DD8"},
        sun=["#FFD84A", "#FFB040", "#FF8840", "#FF5C60", "#F03C90", "#C030C0"], grid="#5A2A9A", horizon="#FF3DD8",
    ),
}

# ================================================================ the shared grid
# (top row, height) for each band; the same in both themes
LAYOUT = dict(
    margin=5,
    header=(0, 24),         # rows 0..23, the host's HB_TOP band
    focus=(28, 46),         # LED window + focus readout
    slot=(78, 56),          # graph / art / theme cards: one 230 x 56 canvas
    cols=(138, 84),         # four knob columns
    footer=(224, 16),       # status, or the banner for 1.5 s
    col_w=56, col_gap=2,
    # SEQ view's own body, between the same header and footer
    roll=(26, 72),          # 16 step columns of 14 px
    readout=(101, 52),      # the cursor step
    cells=(156, 32),        # the SEQ page's four knob values
    legend=(191, 16),       # what the section buttons mean here
    keys=(208, 12),         # the 27-key strip; its underline ends at row 222
)
M = LAYOUT["margin"]
FOCUS, SLOT, COLS, FOOTER = LAYOUT["focus"], LAYOUT["slot"], LAYOUT["cols"], LAYOUT["footer"]
COL_W, COL_GAP = LAYOUT["col_w"], LAYOUT["col_gap"]
PAGE_ORDER = ["1A", "2A", "2B", "3A", "4A", "4B", "5A", "5B", "6A", "6B", "7A", "7B", "8A", "8B"]   # interaction.md


def col_x(k):
    return M + k * (COL_W + COL_GAP)


# ---------------------------------------------------------------- what the screens show
PATCH = ("23", "CHROME DUSK", True)       # number, name, edited
PATTERN = ("41", "NIGHT DRIVE", True)
TRANSPORT = dict(playing=True, step=7, tempo=112, clock="INT")
PERF = [("41", "CUTOFF", "1.25", "k", ("range", 0.62), "VCF"), ("42", "RESO", "34", "", ("range", 0.34), "VCF"),
        ("43", "ENV", "+24", "st", ("bipolar", 0.25), "VCF"), ("64", "RELEASE", "350", "ms", ("range", 0.41), "VCA")]
VCF = [("41", "CUTOFF", "1.25", "k", ("range", 0.62)), ("42", "RESO", "34", "", ("range", 0.34)),
       ("43", "ENV", "+24", "st", ("bipolar", 0.25)), ("44", "KEY", "50", "%", ("range", 0.5))]
VCF_FOCUS = (1, "RESONANCE", "34", "")
ENV = [("51", "ATTACK", "40", "ms", ("range", 0.22)), ("52", "DECAY", "480", "ms", ("range", 0.47)),
       ("53", "SUSTAIN", "70", "", ("range", 0.70)), ("54", "RELEASE", "350", "ms", ("range", 0.41))]
ENV_FOCUS = (1, "DECAY", "480", "ms")
GLO = [("G31", "CURVE", "100", "%", ("range", 0.33)), ("G32", "FIX VEL", "100", "", ("range", 0.78)),
       ("G33", "KEY LED", "ON", "", ("choice", 1, 2)), ("G34", "THEME", "NIGHT", "", ("choice", 1, 2))]
# the 16-step pattern: (kind, notes as semitones from C3); a tie holds the step before it
SEQ = [("n", [0]), ("r", []), ("n", [3]), ("t", [3]), ("c", [0, 3, 7, 10]), ("t", [0, 3, 7, 10]),
       ("n", [7]), ("r", []), ("n", [-4]), ("n", [0]), ("c", [5, 8, 12]), ("r", []),
       ("n", [-5]), ("t", [-5]), ("n", [-2]), ("r", [])]
LENGTH = 16
PLAYHEAD = 6                              # step 7
CURSOR = 4                                # step 5
HELD = [16, 19, 23]                       # chord keys A4 C5 E5 held now
NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]


def note_name(semi):
    n = semi + 48
    return NOTE_NAMES[n % 12] + str(n // 12 - 1)


def key_name(k):                          # key 0 = F3
    return note_name(k + 5)


def filter_curve(cut, res, n):
    fc = 10 ** (-2.0 + 2.4 * cut)
    qv = 0.7 + 6.0 * res ** 1.5
    out = []
    for i in range(n):
        x = 10 ** (-2.0 + 2.6 * i / (n - 1)) / fc
        h2 = 1 / ((1 - x * x) ** 2 + (x / qv) ** 2)
        out.append(10 * math.log10(h2 * h2) / 2)
    return out


def env_points(x0, y0, w, h, a, d, s, r):
    """ADSR as a polyline: a linear attack, exponential decay and release."""
    ta, td, tr, hold = 6 + a * 60, 8 + d * 70, 8 + r * 70, 34
    sc = w / (ta + td + hold + tr)
    ta, td, hold, tr = ta * sc, td * sc, hold * sc, tr * sc
    pts = [(x0, y0 + h), (x0 + ta, y0)]
    for i in range(1, 13):
        t = i / 12
        pts.append((x0 + ta + td * t, y0 + h - (s + (1 - s) * math.exp(-4 * t)) * h))
    xs = x0 + ta + td
    pts.append((xs + hold, y0 + h - s * h))
    for i in range(1, 13):
        t = i / 12
        pts.append((xs + hold + tr * t, y0 + h - s * math.exp(-4 * t) * (1 - t) * h))
    return pts, [x0 + ta, xs, xs + hold, xs + hold + tr]


def sun_cuts(hz, gaps, solid):
    rows, y = set(), hz - 1
    for g, sl in zip(gaps, solid):
        rows.update(range(y - g + 1, y + 1))
        y -= g + sl
    return rows


# ================================================================ shared parts
def accent(t, sec):
    return t["sec"][sec]


def led_color(t, sec):
    return t["led"] or t["sec"][sec]


def draw_transport(sc, t, right):
    """Right-aligned in the header: play/stop mark, step, tempo, clock source."""
    tr = TRANSPORT
    s1, s2 = "%02d" % tr["step"], " %d %s" % (tr["tempo"], tr["clock"])
    x = right - sc.text_w(s1 + s2) - 10
    col = t["playhead"] if tr["playing"] else t["dim"]
    if tr["playing"]:                     # a triangle, 9 rows tall: one fill per row
        for i in range(9):
            sc.fill(x, 7 + i, 5 - abs(4 - i) + 1, 1, col)
    else:
        sc.fill(x, 8, 7, 7, col)
    x = sc.text(x + 10, 4, s1, t["text"])
    sc.text(x, 4, s2, t["dim"])


def draw_header(sc, t, sec, chip, context, page=None):
    col = accent(t, sec)
    if t["header"] == "chip":
        hy, hh = LAYOUT["header"]
        sc.fill(0, hy, W, hh, t["surface"])
        w = sc.text_w(chip) + 12
        sc.fill(0, hy, w, hh, col)
        sc.text(0, 4, chip, t["ink"], align="c", w=w)
        x = w + 6
    else:
        sc.fill(0, 0, W, 3, col)
        x = sc.text(M, 4, chip, col) + 8
    for part, bright in (context if isinstance(context, list) else [(context, False)]):
        x = sc.text(x, 4, part, t["text"] if bright else t["dim"])
    draw_transport(sc, t, W - M)
    sc.fill(0, sum(LAYOUT["header"]) - 1, W, 1, t["line"])
    if page is not None:                  # where the page sits: a strip on the header's rule
        cur, n = page if isinstance(page, tuple) else (PAGE_ORDER.index(page), len(PAGE_ORDER))
        span = W - 2 * M - (n - 1) * 2       # segments share the full 230 px:
        x = M                                 # the leftover px go one each to
        for i in range(n):                    # the first segments
            sw = span // n + (1 if i < span % n else 0)
            sc.fill(x, 22, sw, 2, col if i == cur else t["line"])
            x += sw + 2
        assert x - 2 == W - M


def draw_footer(sc, t, left, right, right_c=None):
    y = FOOTER[0]
    sc.fill(0, y - 1, W, 1, t["line"])
    sc.fill(0, y, W, FOOTER[1], t["surface"])
    sc.text(M, y, left, t["text"])
    sc.text(0, y, right, right_c or t["dim"], align="r", w=W - M)


def status_footer(sc, t):
    num, name, edited = PATCH
    pn, _, ped = PATTERN
    draw_footer(sc, t, "%s %s%s" % (num, name, "*" if edited else ""), "PTN %s%s" % (pn, "*" if ped else ""))


def led_window(sc, t, x, y, w, h, s, sec, dw=16, dh=36, th=4):
    sc.fill(x, y, w, h, t["window"])
    sc.frame(x, y, w, h, t["line"])
    on = led_color(t, sec)
    sw = sc.seg7_w(len(s), dw, th)
    sc.seg7(x + (w - sw) // 2, y + (h - dh) // 2, s, dw, dh, th, on, mix(t["window"], on, t["ghost"]))


def focus_frames(sc, t, x, y, w, h, col, bg):
    for i, m in enumerate(t["focus"]):
        sc.frame(x + i, y + i, w - 2 * i, h - 2 * i, mix(bg, col, m))


def meter(sc, t, x, y, w, h, val, col, active, bg):
    """A knob's position. ('range', f), ('bipolar', f -1..1) or ('choice', i, n). A theme with
    meter_segments 0 draws a continuous bar; a choice always draws one segment per choice."""
    on_c = col if active else mix(bg, col, 0.6)
    off_c = t["line"]
    kind = val[0]
    if kind == "choice":
        _, i, n = val
        g = 2
        sw = (w - (n - 1) * g) // n
        for k in range(n):
            sc.fill(x + k * (sw + g), y, sw, h, on_c if k == i else off_c)
        return
    n, g = t["meter_segments"], t["meter_gap"]
    if n == 0:
        sc.fill(x, y, w, h, off_c)
        if kind == "range":
            sc.fill(x, y, round(w * val[1]), h, on_c)
        else:
            mid = x + w // 2
            ww = round(w / 2 * val[1])
            sc.fill(min(mid, mid + ww), y, abs(ww), h, on_c)
            sc.fill(mid, y - 1, 1, h + 2, t["dim"])
        return
    sw = (w - (n - 1) * g) / n
    for k in range(n):
        sx = x + round(k * (sw + g))
        if kind == "range":
            lit = k < round(n * val[1])
        else:
            c = (k + 0.5) / n * 2 - 1     # -1..1 at the segment's middle
            lit = (0 <= c <= val[1]) or (val[1] <= c <= 0)
        sc.fill(sx, y, round(sw), h, on_c if lit else off_c)


def value_text(sc, t, x, w, y, val, unit, col):
    """D24 value with a Terminus unit after it, centered as one."""
    tw = sc.dtext_w("D24", val) + (3 + sc.text_w(unit) if unit else 0)
    x0 = x + (w - tw) // 2
    xe = sc.dtext(x0, y, "D24", val, col)
    if unit:
        sc.text(xe + 1, y + 8, unit, mix(t["surface"], col, 0.7))


def knob_columns(sc, t, rows, focus, sec_of=None):
    y, h = COLS
    for k, row in enumerate(rows):
        n, label, val, unit, mval = row[:5]
        sec = sec_of(k) if sec_of else None
        col = accent(t, sec)
        x = col_x(k)
        act = k == focus
        bg = t["surface_hi"] if act else t["surface"]
        sc.fill(x, y, COL_W, h, bg)
        sc.text(x, y + 3, n, col, align="c", w=COL_W)
        sc.text(x, y + 19, label, t["text"] if act else t["dim"], align="c", w=COL_W)
        meter(sc, t, x + 6, y + 40, COL_W - 12, 6, mval, col, act, bg)
        if sc.dtext_w("D24", val) <= COL_W - 4 or not val.isalpha():
            value_text(sc, t, x, COL_W, y + 52, val, unit, t["text"] if act else t["dim"])
        else:                             # a long choice falls back to Terminus
            sc.text(x, y + 56, val, t["text"] if act else t["dim"], align="c", w=COL_W)
        if act:
            focus_frames(sc, t, x, y, COL_W, h, col, t["bg"])


def focus_block(sc, t, sec, secname, num, label, val, unit):
    y, h = FOCUS
    led_window(sc, t, M, y, 64, h, num, sec)
    x = M + 72
    col = accent(t, sec)
    xe = sc.text(x, y, secname, col)
    sc.text(xe + 8, y, label, t["text"])
    xe = sc.dtext(x, y + 20, "D24", val, t["text"])
    if unit:
        sc.text(xe + 3, y + 28, unit, t["dim"])


def slot_window(sc, t):
    y, h = SLOT
    sc.fill(M, y, W - 2 * M, h, t["window"])
    sc.frame(M, y, W - 2 * M, h, t["line"])
    return M, y, W - 2 * M, h


# ================================================================ the screens
def scr_play(fonts, t):
    sc = Screen(fonts, t["bg"])
    draw_header(sc, t, "PLAY", "PLAY", "fm1-8p")
    num, name, edited = PATCH
    y, h = FOCUS
    led_window(sc, t, M, y, 64, h, num, "PLAY")
    sc.text(M + 72, y, "PATCH %s" % num, t["dim"])
    if edited:
        sc.text(0, y, "EDITED", accent(t, "PLAY"), align="r", w=W - M)
    sc.dtext(M + 72, y + 22, "D20", name, t["text"])
    # the art: drawn once on entry, never per frame
    x, y, w, h = M, SLOT[0], W - 2 * M, SLOT[1]
    if t["art"] == "badge":
        yy = y
        for c, hgt in zip(t["sun"], (11, 11, 11, 9, 7, 4)):
            sc.fill(x, yy, w, hgt, c)
            yy += hgt + (0 if yy - y < 22 else 1)
        sc.text(x, y + 9, "fm1-8p  POLY SYNTH", t["ink"], align="c", w=w)
    else:
        hz = y + 34
        cy, r = hz + 4, 30
        rows = len(t["sun"])
        cuts = sun_cuts(hz, (3, 2, 2, 1, 1), (3, 3, 4, 5, 6))
        sc.disc_rows(120, cy, r, lambda yy: t["sun"][min(rows - 1, max(0, (yy - (cy - r)) * rows // (hz - (cy - r))))],
                     lambda yy: yy in cuts, hz)
        sc.fill(x, hz, w, 1, t["horizon"])
        for k in range(-8, 9):            # clipped to the slot: a line ends where it leaves it
            x0, x1, y1 = 120 + k * 9, 120 + k * 40, y + h - 1
            if not (x <= x1 <= x + w - 1):
                xe = x if x1 < x else x + w - 1
                y1 = hz + (y1 - hz) * (xe - x0) / (x1 - x0)
                x1 = xe
            sc.line(x0, hz, x1, y1, t["grid"])
        yy, step = hz + 2, 2.0
        while yy < y + h:
            sc.fill(x, int(yy), w, 1, t["grid"])
            step *= 1.6
            yy += step
        sc.fill(0, y + h, W, COLS[0] - y - h, t["bg"])
    knob_columns(sc, t, PERF, focus=-1, sec_of=lambda k: PERF[k][5])
    status_footer(sc, t)
    return sc


def scr_vcf(fonts, t):
    sc = Screen(fonts, t["bg"])
    sec = "VCF"
    draw_header(sc, t, sec, "4 VCF", [("A", True), ("/B", False)], page="4A")
    k, label, val, unit = VCF_FOCUS
    focus_block(sc, t, sec, "VCF", VCF[k][0], label, val, unit)
    x, y, w, h = slot_window(sc, t)
    col = accent(t, sec)
    db = filter_curve(VCF[0][4][1], VCF[1][4][1], n=w - 4)
    yof = lambda d: y + 5 + min(h - 8, max(0, (12 - d) * (h - 8) / 48))
    sc.fill(x + 2, int(yof(0)), w - 4, 1, t["line"])
    pts = [(x + 2 + i, yof(d)) for i, d in enumerate(db)]
    if t["hatch"]:
        for i, (px, py) in enumerate(pts):
            if i % 2 == 0:
                sc.fill(px, int(py) + 2, 1, y + h - 2 - int(py) - 2, mix(t["window"], col, t["hatch"]))
    sc.poly(pts, col, thick=2)
    knob_columns(sc, t, VCF, focus=k, sec_of=lambda _: sec)
    status_footer(sc, t)
    return sc


def scr_env(fonts, t):
    sc = Screen(fonts, t["bg"])
    sec = "VCF ENV"
    draw_header(sc, t, sec, "5 VCF ENV", [("A", True), ("/B", False)], page="5A")
    k, label, val, unit = ENV_FOCUS
    focus_block(sc, t, sec, "VCF ENV", ENV[k][0], label, val, unit)
    x, y, w, h = slot_window(sc, t)
    col = accent(t, sec)
    a, d, s, r = (e[4][1] for e in ENV)
    base = y + h - 6
    pts, marks = env_points(x + 6, y + 6, w - 40, base - y - 6, a, d, s, r)
    for m in marks[:-1]:
        for yy in range(y + 3, base, 4):
            sc.fill(int(m), yy, 1, 2, t["line"])
    stage = ([x + 6] + marks)[k:k + 2]   # the focused parameter's stage: A, D, S or R

    def curve_y(px):                      # the curve's y at px, between the vertices around it
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            if ax <= px <= bx and bx > ax:
                return ay + (by - ay) * (px - ax) / (bx - ax)
        return base
    if t["hatch"]:
        for px in range(x + 6, int(marks[-1]), 2):
            top = int(curve_y(px)) + 3
            inside = stage[0] <= px <= stage[1]
            if top < base:
                sc.fill(px, top, 1, base - top, mix(t["window"], col, t["hatch"] * (1.8 if inside else 1)))
    sc.fill(x + 6, base, w - 12, 1, t["line"])
    sc.poly(pts, mix(t["window"], col, 0.55), thick=2)
    sc.poly([p for p in pts if stage[0] <= p[0] <= stage[1]], col, thick=3)
    for i, lab in enumerate("ADSR"):
        x0 = x + 6 if i == 0 else marks[i - 1]
        sc.text(int((x0 + marks[i]) / 2) - 4, y + 2, lab, col if i == k else t["dim"])
    knob_columns(sc, t, ENV, focus=k, sec_of=lambda _: sec)
    status_footer(sc, t)
    return sc


def scr_glo(fonts, t, themes):
    sc = Screen(fonts, t["bg"])
    sec = "GLO"
    draw_header(sc, t, sec, "GLO", "GLOBAL", page=(2, 3))
    y, h = FOCUS
    led_window(sc, t, M, y, 64, h, "34", sec)
    xe = sc.text(M + 72, y, "G34", accent(t, sec))
    sc.text(xe + 8, y, "THEME", t["text"])
    sc.dtext(M + 72, y + 22, "D20", t["name"], t["text"])
    # the slot: a swatch card per theme, each in its own colors; the chosen one framed
    sy, sh = SLOT
    cw = (W - 2 * M - 4) // 2
    for i, (key, th) in enumerate(themes.items()):
        cx = M + i * (cw + 4)
        sc.fill(cx, sy, cw, sh, th["bg"])
        c = th["sec"]["VCF"]
        if th["header"] == "chip":
            sc.fill(cx, sy, cw, 10, th["surface"])
            sc.fill(cx, sy, 26, 10, c)
        else:
            sc.fill(cx, sy, cw, 2, c)
        for j in range(4):
            jx = cx + 6 + j * ((cw - 12) // 4)
            sc.fill(jx, sy + 14, (cw - 12) // 4 - 3, 20, th["surface_hi"] if j == 1 else th["surface"])
            mx, mw = jx + 3, (cw - 12) // 4 - 9
            if th["meter_segments"]:
                for s in range(4):
                    sc.fill(mx + s * (mw // 4), sy + 28, mw // 4 - 1, 3, c if s < 2 else th["line"])
            else:
                sc.fill(mx, sy + 28, mw, 3, th["line"])
                sc.fill(mx, sy + 28, mw // 2, 3, c)
        sc.seg7(cx + 6, sy + 16, "42", 5, 9, 1, th["led"] or c, None, gap=2)
        sc.text(cx, sy + 38, th["name"], th["text"], align="c", w=cw)
        if th is t:
            focus_frames(sc, t, cx, sy, cw, sh, accent(t, sec), t["bg"])
        else:
            sc.frame(cx, sy, cw, sh, t["line"])
    rows = list(GLO)
    rows[3] = ("G34", "THEME", t["short"], "", ("choice", t["index"], len(themes)))
    knob_columns(sc, t, rows, focus=3, sec_of=lambda _: sec)
    pn, _, ped = PATTERN
    draw_footer(sc, t, "GLOBAL: NOT IN PATCH", "PTN %s%s" % (pn, "*" if ped else ""))
    return sc


def key_is_white(k):
    return (k + 5) % 12 in (0, 2, 4, 5, 7, 9, 11)


def key_led(k):
    """Interaction.md's key LED rules in SEQ view (a still frame: blinks show their lit phase).
    0 off, 1 lit, 2 glow, as hb_host_t.led takes them."""
    if k >= 16:
        return 1 if k in HELD else 2
    if k >= LENGTH:
        return 0
    kind = SEQ[k][0]
    st = 2 if kind == "r" else 1
    if k == PLAYHEAD:
        st = 0 if st == 1 else 1
    return st


def key_strip(sc, t, x, y, kw, kh, col):
    """All 27 keys, F3..G5, each with its LED; the step keys (F3..G#4) underlined in the SEQ
    color, the chord keys in dim. Mirrors the key LEDs."""
    wx, pos = 0, {}
    for k in range(27):
        if key_is_white(k):
            pos[k] = (x + wx * kw, True)
            wx += 1
        else:
            pos[k] = (x + wx * kw - 4, False)
    led_c = {1: col, 2: mix(t["text"], col, 0.35), 0: t["line"]}
    for k in range(27):
        px, white = pos[k]
        if white:
            sc.fill(px, y, kw - 1, kh, mix(t["bg"], t["text"], 0.85))
            sc.fill(px + kw // 2 - 3, y + kh - 6, 5, 3, led_c[key_led(k)] if key_led(k) != 0 else mix(t["bg"], t["text"], 0.6))
    for k in range(27):
        px, white = pos[k]
        if not white:
            sc.fill(px, y, 7, kh * 3 // 5, t["black_key"])
            st = key_led(k)
            sc.fill(px + 1, y + kh * 3 // 5 - 4, 5, 2, {1: col, 2: mix(t["bg"], col, 0.45), 0: t["line"]}[st])
    split = pos[16][0]
    sc.fill(x, y + kh + 1, split - x - 1, 2, col)
    sc.fill(split, y + kh + 1, x + wx * kw - split - 1, 2, t["dim"])


def scr_seq(fonts, t):
    sc = Screen(fonts, t["bg"])
    sec = "SEQ"
    col = accent(t, sec)
    pn, pname, ped = PATTERN
    draw_header(sc, t, sec, "SEQ", "S2 STEP")
    # the roll: 16 step columns of 14 px, each its own small canvas (14 x 72)
    (ry, rh), rx, cw = LAYOUT["roll"], 8, 14
    lo, hi = -6, 13
    ny = lambda semi: ry + rh - 5 - (semi - lo) * (rh - 8) // (hi - lo)
    for i, (kind, notes) in enumerate(SEQ):
        x = rx + i * cw
        play = i == PLAYHEAD
        bg = mix(t["window"], t["playhead"], 0.16) if play else (t["window"] if (i // 4) % 2 == 0 else t["surface"])
        sc.fill(x, ry, cw - 1, rh, bg)
        for semi in (0, 12):
            sc.fill(x, ny(semi) + 1, cw - 1, 1, t["line"])
        block = t["playhead"] if play else col
        for semi in notes:
            if kind == "t":               # a tie: the held notes run on from the left, through the gap
                sc.fill(x - 1, ny(semi), cw - 2, 3, mix(t["window"], col, t["tie_mix"]))
            else:
                sc.fill(x + 1, ny(semi), cw - 3, 3, block)
        if i == CURSOR:
            sc.frame(x - 1, ry, cw + 1, rh, t["cursor"], 1)
    sc.fill(rx + PLAYHEAD * cw + 6, ry, 1, rh, t["playhead"])
    sc.fill(rx, ry + rh, 16 * cw - 1, 1, t["line"])
    # the cursor step in detail
    oy, oh = LAYOUT["readout"]
    led_window(sc, t, M, oy, oh, oh, "05", sec)
    x = M + 60
    sc.dtext(x, oy, "D20", "C MIN7", t["text"])
    sc.text(0, oy + 2, "TIE > 6", col, align="r", w=W - M)
    sc.text(x, oy + 21, "C3 Eb3 G3 Bb3", col)
    xe = sc.text(x, oy + 37, "HELD ", t["dim"])
    sc.text(xe, oy + 37, " ".join(key_name(k) for k in HELD), t["text"])
    # S2: the cursor step's four knobs
    for k, (lab, val) in enumerate((("VEL", "100"), ("BEND", "+0%"), ("MOD", "0"), ("MOVE", "JUMP"))):
        cx = col_x(k)
        cy, ch = LAYOUT["cells"]
        sc.fill(cx, cy, COL_W, ch, t["surface"])
        sc.text(cx, cy, lab, t["dim"], align="c", w=COL_W)
        sc.text(cx, cy + 16, val, t["text"], align="c", w=COL_W)
    # what the section buttons mean here
    x = M
    for b, m in (("SEL", "REST"), ("ENV", "TIE"), ("LFO", "MOVE"), ("FX", "--")):
        w = sc.text_w(b) + 4
        ly, lh = LAYOUT["legend"]
        sc.fill(x, ly, w, lh, t["surface_hi"])
        sc.text(x + 2, ly, b, t["dim"])
        x = sc.text(x + w + 2, ly, m, t["text"] if m != "--" else t["dim"]) + 6
    key_strip(sc, t, 8, LAYOUT["keys"][0], 14, LAYOUT["keys"][1], col)
    draw_footer(sc, t, "PTN %s %s%s" % (pn, pname, "*" if ped else ""), "NEXT 42", col)
    return sc


SCREENS = [("1-play", scr_play), ("2-vcf", scr_vcf), ("3-env", scr_env), ("4-seq", scr_seq), ("5-glo", scr_glo)]


def specimen(fonts):
    img = Screen(fonts, "#0E0E1A")
    img.text(5, 4, "fm1-8p display font", "#9896B8")
    img.dtext(5, 26, "D24", "0123456789", "#EEECF8")
    img.dtext(5, 54, "D24", "-+.%#/: 1.25 +24", "#2FD8FF")
    img.dtext(5, 86, "D20", "ABCDEFGHIJKLM", "#EEECF8")
    img.dtext(5, 110, "D20", "NOPQRSTUVWXYZ", "#EEECF8")
    img.dtext(5, 140, "D20", "CHROME DUSK", "#FF3DD8")
    img.dtext(5, 164, "D20", "NIGHT DRIVE", "#FFC23D")
    img.dtext(5, 190, "D24", "480", "#EEECF8")
    img.text(48, 198, "ms", "#9896B8")
    img.text(80, 194, "24 px values", "#9896B8")
    img.text(80, 210, "20 px names", "#9896B8")
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    dock = os.environ.get("FM1_DOCK", os.path.expanduser("~/Projects/fm1-8p-ref/fm1-dock"))
    ap.add_argument("--font", default=os.path.join(dock, "firmware/gen/felucca_font.h"))
    ap.add_argument("--contrast", action="store_true", help="print each theme's colors and contrast")
    args = ap.parse_args()
    if not os.path.exists(args.font):
        sys.exit("no host font at %s (set $FM1_DOCK or --font)" % args.font)
    dfont = {k: bake_font(v) for k, v in SIZES.items()}
    for k, f in dfont.items():
        check_font(f, k)
    fonts = (load_host_font(args.font), dfont)
    total = write_font_header(os.path.join(OUT, "display_font.h"), {k: (dfont[k], SIZES[k]) for k in SIZES})
    print("display font: %d bytes" % total)
    specimen(fonts).save(os.path.join(OUT, "font-specimen"))
    sheet = Image.new("RGB", (5 * W + 6 * 8, 2 * H + 3 * 8), (40, 40, 40))
    for row, (key, t) in enumerate(THEMES.items()):
        os.makedirs(os.path.join(OUT, key), exist_ok=True)
        for c, (name, fn) in enumerate(SCREENS):
            sc = fn(fonts, t, THEMES) if fn is scr_glo else fn(fonts, t)
            sc.save(os.path.join(OUT, key, name))
            sheet.paste(sc.img, (8 + c * (W + 8), 8 + row * (H + 8)))
            print(os.path.join(key, name) + "-3x.png")
    sheet.save(os.path.join(OUT, "compare.png"))
    if args.contrast:
        for key, t in THEMES.items():
            for k in ("bg", "surface", "surface_hi", "line", "text", "dim", "ink", "window"):
                print(key, k, t[k], "0x%04X" % rgb565(t[k]))
            for fg in ("text", "dim"):
                print(key, fg, "on", " ".join("%s %.1f" % (b, contrast(t[fg], t[b]))
                                             for b in ("bg", "surface", "surface_hi", "window")))
            for s, c in t["sec"].items():
                print(key, s, c, "0x%04X" % rgb565(c), "%.1f" % contrast(c, t["bg"]),
                      "%.1f" % contrast(c, t["surface"]))


if __name__ == "__main__":
    main()
