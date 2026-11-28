/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The skyline with a laser line (Eric, 2026-11-27): PLAY's art and the boot screen's, baked from the mockup
 * (docs/ui/mockups/skyline/skyline.py --table makes docs/ui/mockups/skyline_art.h). */
#ifndef FM1_SKYLINE_H
#define FM1_SKYLINE_H

#include <stdint.h>

enum { SKYLINE_PLAY, SKYLINE_BOOT };            /* the screens: PLAY's slot, the boot screen's top */
enum { SKYLINE_MEMBRANE, SKYLINE_NIGHT_PANEL }; /* a theme's palette (ui_theme's sky) */

typedef void (*skyline_rect_fn)(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c);

/* screen's art in theme's colors, the part in screen rows oy .. oy + rows - 1, into a canvas whose top
 * is row oy (rect's y is the canvas's: screen y - oy; rect clips) */
void skyline_draw(uint32_t screen, uint8_t theme, int32_t oy, int32_t rows, skyline_rect_fn rect);

#endif
