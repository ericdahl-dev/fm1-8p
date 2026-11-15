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
