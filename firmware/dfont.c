/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "dfont.h"
#include "../docs/ui/mockups/display_font.h"

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

int32_t dfont_width(const dfont *f, const char *s)
{
    int32_t w = 0, g;
    for (; *s; s++)
        if ((g = dfont_glyph(*s)) >= 0)
            w += f->w[g] + f->gap;
    return w ? w - f->gap : 0;
}

int32_t dfont_draw(const dfont *f, int32_t x, int32_t y, const char *s, uint16_t c, dfont_rect_fn rect)
{
    int32_t x0 = x;
    for (; *s; s++) {
        int32_t g = dfont_glyph(*s);
        uint32_t w, bpr, r, b;
        const uint8_t *row;
        if (g < 0)
            continue;
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
