/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "skyline.h"
#include "../docs/ui/mockups/skyline_art.h"

/* (C99: no _Static_assert) the theme indexes ui_map.c uses are skyline.py's */
typedef char skyline_membrane_is_0[SKYLINE_MEMBRANE == SKY_THEME_MEMBRANE ? 1 : -1];
typedef char skyline_night_panel_is_1[SKYLINE_NIGHT_PANEL == SKY_THEME_NIGHT_PANEL ? 1 : -1];

/* Each fill in skyline.py's order (later ones paint over earlier), those that reach rows oy .. oy + rows - 1
 * only: a strip costs its own fills. */
void skyline_draw(uint32_t screen, uint8_t theme, int32_t oy, int32_t rows, skyline_rect_fn rect)
{
    const uint8_t (*f)[5] = screen == SKYLINE_BOOT ? SKY_BOOT : SKY_PLAY;
    const uint32_t n = screen == SKYLINE_BOOT ? SKY_BOOT_N : SKY_PLAY_N;
    const uint16_t *pal = SKY_PAL[theme < SKY_THEMES ? theme : 0];
    uint32_t i;
    for (i = 0; i < n; i++) {
        const int32_t y = f[i][1], h = f[i][3];
        if (y + h <= oy || y >= oy + rows)
            continue;
        rect(f[i][0], y - oy, f[i][2], h, pal[f[i][4]]);
    }
}
