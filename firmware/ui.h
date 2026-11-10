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
#define UI_BTN_FX      2u
#define UI_BTN_SEL     3u
#define UI_BTN_ENV     4u
#define UI_BTN_LFO     5u
#define UI_BTN_EDIT    6u
#define UI_BTN_HOME    8u
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
    uint32_t focus;                     /* the knob (0..3) the focus row shows: the last one turned */
    uint32_t dirty;                     /* UI_D_*: what to redraw */
    uint32_t drawn_ms;                  /* when ui_take last handed out a redraw */
    uint32_t edit_down_ms;              /* when EDIT went down */
} ui;

/* ui_take's bits: what changed since the last redraw */
#define UI_D_FULL   0x01u               /* the whole screen: a page opened */
#define UI_D_FOCUS  0x02u               /* the focus row */
#define UI_D_SLOT   0x04u               /* the slot (the filter curve) */
#define UI_D_COL(k) (0x10u << (k))      /* knob column k */
#define UI_TAP_MS 400u                  /* a button let go sooner is a tap (interaction.md) */
#define UI_FRAME_MS 33u                 /* redraws are at least this far apart (visual.md: 30 a second) */

void ui_init(ui *u, play *p);
#define UI_VALUE_LEN 12u                /* room for a value as the focus row reads it, and its end */
/* param id's value as the focus row reads it (interaction.md section 3), into s (UI_VALUE_LEN) */
void ui_format(uint16_t id, int32_t value, char *s);
/* the value as a knob column reads it: the number into num, a short unit into unit (UI_VALUE_LEN
 * each): 1.25 k, 440 (Hz has no room), +24 st, 50 % */
void ui_column(uint16_t id, int32_t value, char *num, char *unit);
/* the slot's filter curve, as docs/ui/mockups/render.py draws it: its level at column i of n, in
 * tenths of a dB (0 flat, + the resonant bump) */
int32_t ui_curve_db(int32_t cutoff_cents, int32_t reso_permille, uint32_t i, uint32_t n);
/* the header's page strip: segment i (interaction.md's page order, 1A .. 8B) at x, w px wide */
#define UI_PAGES 14u
#define UI_PAGE_4A 4u
void ui_strip(uint32_t i, int32_t *x, int32_t *w);
/* a 7-segment digit's lit segments, bits a..g = 0..6 (0 for one it can't show) */
uint32_t ui_seg7(char c);
#define UI_METER_SEGS 8u
/* the knob meter's lit segments (bit k, left to right): a range from the left, a bipolar param
 * (min = -max) out from the middle */
uint32_t ui_meter(uint16_t id, int32_t value);
/* the slot on knob k (0..3) of the page shown, NULL past KNOB4 */
const ui_slot *ui_page_slot(const ui *u, uint32_t k);
/* a panel event (UI_EV_* | id, the encoder's clicks) at `ms` (the host's ms()) */
void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);
/* the UI_D_* bits to redraw now (0: nothing, or the last redraw was under UI_FRAME_MS ago), taken */
uint32_t ui_take(ui *u, uint32_t ms);
/* a button's LED (interaction.md): UI_LED_OFF, UI_LED_LIT or UI_LED_GLOW (dim) */
enum { UI_LED_OFF = 0, UI_LED_LIT = 1, UI_LED_GLOW = 2 };
uint32_t ui_led(const ui *u, uint32_t button);
#endif
