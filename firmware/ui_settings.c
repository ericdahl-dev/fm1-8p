/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "ui_settings.h"

#define S(f, lo, hi, d) {(uint8_t)offsetof(ui_settings, f), lo, hi, d}
/* interaction.md 4, GLO: the defaults. THEME's is Membrane (Eric, overriding
 * visual.md's first Night Panel default). */
const ui_setting_info UI_SETTINGS_INFO[UI_SETTINGS_COUNT] = {
    S(midi_in, 0, 16, 0), S(midi_out, 1, 16, 1), S(clock_src, 0, 2, 0), S(clock_out, 0, 1, 0),
    S(cc_recv, 0, 1, 1), S(cc_send, 0, 1, 0), S(prog_change, 0, 3, 1), S(ext_cc, 0, 1, 0),
    S(vel_curve_pct, 50, 200, 100), S(fixed_vel, 1, 127, 100), S(key_leds, 0, 1, 1),
    S(theme, UI_THEME_MEMBRANE, UI_THEME_NIGHT, UI_THEME_MEMBRANE),
};

void ui_settings_default(ui_settings *s)
{
    uint32_t i;
    for (i = 0; i < UI_SETTINGS_COUNT; i++)
        ((uint8_t *)s)[UI_SETTINGS_INFO[i].offset] = UI_SETTINGS_INFO[i].def;
}
