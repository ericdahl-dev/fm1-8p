/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dfont.h"
#include "../docs/ui/mockups/display_font.h"

/* #55's flash budget for the font, 6 KB: its bitmaps and tables (CI checks the generator's count too) */
_Static_assert(sizeof D24_W + sizeof D24_OFF + sizeof D24_DATA + sizeof D20_W + sizeof D20_OFF + sizeof D20_DATA <= 6144u,
               "the display font is over its 6 KB budget (#55)");

const char dfont_chars[] = DFONT_CHARS;
const uint32_t dfont_n = DFONT_N;

int32_t dfont_glyph(char c)
{
    uint32_t i;
    if (c >= 'a' && c <= 'z')
        c = (char)(c - 'a' + 'A');
    for (i = 0; i < DFONT_N; i++)
        if (dfont_chars[i] == c)
            return (int32_t)i;
    return -1;
}

const dfont DFONT_D24 = {D24_W, D24_DATA, D24_OFF, D24_H, D24_GAP};
const dfont DFONT_D20 = {D20_W, D20_DATA, D20_OFF, D20_H, D20_GAP};

/* glyph g's width in f; the box for one the font has none for is an O's (Eric, (a)) */
static int32_t width_of(const dfont *f, int32_t g)
{
    return f->w[g >= 0 ? g : dfont_glyph('O')];
}

int32_t dfont_width(const dfont *f, const char *s)
{
    int32_t w = 0;
    for (; *s; s++)
        w += width_of(f, dfont_glyph(*s)) + f->gap;
    return w ? w - f->gap : 0;
}

int32_t dfont_draw(const dfont *f, int32_t x, int32_t y, const char *s, uint16_t c, dfont_rect_fn rect)
{
    int32_t x0 = x;
    for (; *s; s++) {
        int32_t g = dfont_glyph(*s);
        uint32_t w, bpr, r, b;
        const uint8_t *row;
        if (g < 0) {                            /* none: an outline box, an O wide (Eric, (a)) */
            int32_t bw = width_of(f, g), bh = (int32_t)f->h;
            rect(x, y, bw, 1, c);
            rect(x, y + bh - 1, bw, 1, c);
            rect(x, y + 1, 1, bh - 2, c);
            rect(x + bw - 1, y + 1, 1, bh - 2, c);
            x += bw + f->gap;
            continue;
        }
        w = f->w[g], bpr = (w + 7u) / 8u, row = &f->data[f->off[g]];
        for (r = 0; r < f->h; r++, row += bpr)
            for (b = 0; b < w;) {
                uint32_t e;
                if (!(row[b >> 3] >> (7u - (b & 7u)) & 1u)) {
                    b++;
                    continue;
                }
                for (e = b; e < w && row[e >> 3] >> (7u - (e & 7u)) & 1u; e++) {}
                rect(x + (int32_t)b, y + (int32_t)r, (int32_t)(e - b), 1, c);
                b = e;
            }
        x += (int32_t)w + f->gap;
    }
    return x == x0 ? x0 : x - f->gap;
}
