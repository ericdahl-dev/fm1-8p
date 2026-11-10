/* fm1-8p UI: the on-device pages' logic (docs/ui/interaction.md): which page shows, what the knobs
 * do, and how values read on screen. Plain C, no host code: the module glue (firmware/fm1_8p/)
 * draws what it says, and the desktop tests run it. Main loop only. */
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "play.h"

/* ui_event's events: the same values as fm1-dock's hb_module.h (the glue passes them on as they are) */
#define UI_EV_BTN_DOWN 0x01000000u
#define UI_EV_BTN_UP   0x02000000u
#define UI_EV_ENC      0x03000000u
#define UI_BTN_ENV     4u
#define UI_BTN_EDIT    6u
#define UI_ENC_KNOB1   2u
#define UI_ENC_KNOB2   3u
#define UI_ENC_KNOB3   4u
#define UI_ENC_KNOB4   5u

typedef enum { UI_PLAY, UI_EDIT } ui_state;

/* What a slot holds (interaction.md section 3): a live param, one whose issue hasn't landed (shown
 * as `--`, its knob does nothing), or nothing. */
typedef enum { UI_LIVE, UI_PENDING, UI_RESERVED } ui_slot_kind;

/* One numbered parameter slot. */
typedef struct {
    uint8_t num;                        /* 41: section 4, parameter 1 */
    const char *label;                  /* the knob column's, at most 7 characters */
    const char *full;                   /* the focus row's full label */
    uint16_t id;                        /* fm_param_id, when live */
    uint8_t kind;                       /* ui_slot_kind */
    int32_t step, fast;                 /* engine units a click moves it; a fast turn's click */
} ui_slot;

typedef struct {
    play *p;
    ui_state state;
} ui;

void ui_init(ui *u, play *p);
#define UI_VALUE_LEN 12u                /* room for a value as the focus row reads it, and its end */
/* param id's value as the focus row reads it (interaction.md section 3), into s (UI_VALUE_LEN) */
void ui_format(uint16_t id, int32_t value, char *s);
/* the slot on knob k (0..3) of the page shown, NULL past KNOB4 */
const ui_slot *ui_page_slot(const ui *u, uint32_t k);
/* a panel event (UI_EV_* | id, the encoder's clicks) at `ms` (the host's ms()) */
void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);
#endif
