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
