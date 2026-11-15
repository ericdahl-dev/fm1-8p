/* fm1-8p UI: the instrument's settings (interaction.md 4, GLO), in RAM. The GLO pages edit them;
 * firmware's settings store (agent2's) keeps them in flash as (tag, value) entries and loads them at
 * the first draw(), clamping each from UI_SETTINGS_INFO. Plain C. */
#ifndef UI_SETTINGS_H
#define UI_SETTINGS_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t midi_in;                    /* G11: 0 ALL, 1..16 */
    uint8_t midi_out;                   /* G12: 1..16 */
    uint8_t clock_src;                  /* G13: 0 AUTO, 1 INT, 2 MIDI (waits on #38) */
    uint8_t clock_out;                  /* G14: OFF, ON (waits on #38) */
    uint8_t cc_recv;                    /* G21: OFF, ON (waits on #49) */
    uint8_t cc_send;                    /* G22: OFF, ON (waits on #49) */
    uint8_t prog_change;                /* G23: 0 OFF, 1 RECV, 2 SEND, 3 BOTH */
    uint8_t ext_cc;                     /* G24: 0 JUMP, 1 PICKUP */
    uint8_t vel_curve_pct;              /* G31: 50..200 % */
    uint8_t fixed_vel;                  /* G32: 1..127, keys with no force sensing */
    uint8_t key_leds;                   /* G33: OFF, ON */
    uint8_t theme;                      /* G34: UI_THEME_MEMBRANE, UI_THEME_NIGHT */
    uint8_t cc_map;                     /* CC MAP: UI_CC_MAP_JX, UI_CC_MAP_STANDARD (#49, Eric) */
} ui_settings;
enum { UI_THEME_MEMBRANE, UI_THEME_NIGHT };
enum { UI_CC_MAP_JX, UI_CC_MAP_STANDARD };

/* each setting's range and default, and where it sits in ui_settings */
typedef struct {
    uint8_t offset;                     /* offsetof(ui_settings, field) */
    uint8_t min, max, def;
} ui_setting_info;
#define UI_SETTINGS_COUNT 13u
extern const ui_setting_info UI_SETTINGS_INFO[UI_SETTINGS_COUNT];

void ui_settings_default(ui_settings *s);
#endif
