/* fm1-8p UI: the on-device pages' logic (docs/ui/interaction.md): which page shows, what the knobs
 * do, and how values read on screen. Plain C, no host code: the module glue (firmware/fm1_8p/)
 * draws what it says, and the desktop tests run it. Main loop only. */
#ifndef UI_H
#define UI_H
#include <stdint.h>
#include "play.h"
#include "ui_map.h"

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
#define UI_BUTTONS     14u                /* button ids 0..13 */
#define UI_ENC_SELECT  0u
#define UI_ENC_ALGORITHM 1u
#define UI_ENC_KNOB1   2u
#define UI_ENC_KNOB2   3u
#define UI_ENC_KNOB3   4u
#define UI_ENC_KNOB4   5u
#define UI_ENC_PRESETS 6u

typedef enum { UI_PLAY, UI_EDIT } ui_state;


typedef struct {
    play *p;
    ui_state state;
    uint32_t focus;                     /* the knob (0..3) the focus row shows: the last one turned */
    uint32_t dirty;                     /* UI_D_*: what to redraw */
    uint32_t drawn_ms;                  /* when ui_take last handed out a redraw */
    uint32_t down_ms[UI_BUTTONS];       /* when each button went down */
    int32_t shown_note;                 /* PLAY: the last note as drawn */
    int32_t shown_oct;                  /* the octave as the header shows it */
    const char *banner;                 /* a short message for the footer ("": none) */
    uint32_t banner_ms;                 /* when it came */
    int32_t warned;                     /* PRESETS warned of edits: the way it turned (+1 / -1), 0 not */
    uint32_t warned_ms;                 /* when */
    uint32_t page;                      /* the edit page, in the page order (UI_PAGE_INFO) */
    uint32_t inv;                       /* an ENV MODE's INV while its depth is 0 (bit: ui.c inv_bit) */
} ui;

/* ui_take's bits: what changed since the last redraw */
#define UI_D_FULL   0x01u               /* the whole screen: a page opened */
#define UI_D_FOCUS  0x02u               /* the focus row */
#define UI_D_SLOT   0x04u               /* the slot (the filter curve) */
#define UI_D_HEAD   0x08u               /* the header alone (the octave changed) */
#define UI_D_COL(k) (0x10u << (k))      /* knob column k */
#define UI_D_FOOT   0x100u              /* the footer alone (the banner came or went) */
#define UI_D_ALL    0xF7u               /* all of it */
#define UI_D_NOTE   UI_D_COL(0)         /* PLAY: the last note's strip */
#define UI_TAP_MS 400u                  /* a button let go sooner is a tap (interaction.md) */
#define UI_BANNER_MS 1500u              /* how long the banner shows (interaction.md section 6) */
/* PRESETS again within this, the same way: discard and load. The warning's time on screen, so a turn
 * never discards with nothing showing (the spec had 2 s against a 1.5 s banner: PR #83 review). */
#define UI_DISCARD_MS UI_BANNER_MS
#define UI_FRAME_MS 33u                 /* redraws are at least this far apart (visual.md: 30 a second) */

void ui_init(ui *u, play *p);
#define UI_VALUE_LEN 12u                /* room for a value as the focus row reads it, and its end */
/* param id's value as the focus row reads it (interaction.md section 3), into s (UI_VALUE_LEN) */
void ui_format(uint16_t id, int32_t value, char *s);
/* the value as the focus row reads it: the number into num, the unit into unit (UI_VALUE_LEN each) */
void ui_value(uint16_t id, int32_t value, char *num, char *unit);
/* the value as a knob column reads it: the number into num, a short unit into unit (UI_VALUE_LEN
 * each): 1.25 k, 440 (Hz has no room), +24 st, 50 % */
void ui_column(uint16_t id, int32_t value, char *num, char *unit);
/* the slot's filter curve, as docs/ui/mockups/render.py draws it: its level at column i of n, in
 * tenths of a dB (0 flat, + the resonant bump) */
typedef struct { uint32_t q2, n1; int32_t cut; } ui_curve;
/* set up for a cutoff and resonance across n columns; then column i's level */
void ui_curve_begin(ui_curve *c, int32_t cutoff_cents, int32_t reso_permille, uint32_t n);
int32_t ui_curve_db(const ui_curve *c, uint32_t i);
/* the header's page strip: segment i (interaction.md's page order, 1A .. 8B) at x, w px wide */
void ui_strip(uint32_t i, int32_t *x, int32_t *w);
/* a 7-segment digit's lit segments, bits a..g = 0..6 (0 for one it can't show) */
uint32_t ui_seg7(char c);
#define UI_METER_SEGS 8u
/* the knob meter's lit segments (bit k, left to right): a range from the left, a bipolar param
 * (min = -max) out from the middle */
uint32_t ui_meter(uint16_t id, int32_t value);
/* a one-bar meter (Membrane) in a w px track: the bar from *a to *b px, a range from the left in
 * proportion, a bipolar param (min = -max) out from the middle */
void ui_meter_bar(uint16_t id, int32_t value, int32_t w, int32_t *a, int32_t *b);
/* a slot's bar (its value from ui_slot_value): an ENV size from the left over 0..its top, the rest as
 * ui_meter_bar */
void ui_slot_bar(const ui_slot *sl, int32_t value, int32_t w, int32_t *a, int32_t *b);
/* a choice slot's (order set) meter: value's knob position (a value between positions: the one
 * before it), of *n positions */
uint32_t ui_choice_pos(const ui_slot *sl, int32_t value, uint32_t *n);
/* the DCO pages' waveform picture: wave's (fm_wave) level, -1000..+1000, at column i of n, two cycles
 * across (a pulse high for width_pct of each) */
int32_t ui_wave_level(uint32_t wave, uint32_t width_pct, uint32_t i, uint32_t n);
/* 0 while a slot does nothing to the sound (shown dim, no meter): WIDTH off a pulse wave */
int ui_slot_active(const ui *u, const ui_slot *sl);
/* 7A's picture: the LFO wave's (fm_lfo_wave) level, -1000..+1000, at column i of n, two cycles across
 * (random: a fixed level each cycle) */
int32_t ui_lfo_level(uint32_t wave, uint32_t i, uint32_t n);
/* the ENV pages' ADSR picture (render.py's env_points): the level, per mille, at column i of n, for knob
 * positions a, d, r and sustain s (per mille): a linear attack, exponential decay and release */
int32_t ui_adsr_level(uint32_t a, uint32_t d, uint32_t s, uint32_t r, uint32_t i, uint32_t n);
/* the time table (interaction.md, units): its size, entry i, and a time clicks entries on (a time
 * not in it snaps to the next entry the way of the turn first), clamped at both ends */
uint32_t ui_time_count(void);
uint32_t ui_rate_count(void);           /* the same for the LFO rate table, centihertz */
int32_t ui_rate_at(uint32_t i);
int32_t ui_time_at(uint32_t i);
int32_t ui_time_step(int32_t ms, int32_t clicks);
/* a slot's value: its param's, or a paired slot's two as id | id2 << 1 */
int32_t ui_slot_value(const ui *u, const ui_slot *sl);
/* the slot on knob k (0..3) of the page shown, NULL past KNOB4 */
const ui_slot *ui_page_slot(const ui *u, uint32_t k);
/* a panel event (UI_EV_* | id, the encoder's clicks) at `ms` (the host's ms()) */
void ui_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);
/* the banner's message ("": none) */
const char *ui_banner(const ui *u);
/* the UI_D_* bits to redraw now (0: nothing, or the last redraw was under UI_FRAME_MS ago), taken */
uint32_t ui_take(ui *u, uint32_t ms);
/* the header's octave, as the host's line reads it ("OCT +1"); s: UI_VALUE_LEN */
void ui_octave_label(int32_t oct, char *s);
/* a patch's number as the screen shows it: the factory tones are the starter group, "P1" .. "P32" */
void ui_patch_number(const patch *t, char *s);
/* a slot's number as the screen shows it, two digits ("41"); s: 3 chars */
void ui_slot_number(const ui_slot *sl, char *s);
/* the host asked for all of it (draw(full)): UI_D_ALL, and nothing stays pending */
uint32_t ui_take_full(ui *u, uint32_t ms);
/* the band to draw now, out of pending (0: none). What changed (ui_take) is taken only once pending
 * is drawn, so steady turns can't starve the later bands; a new page (UI_D_FULL) is taken at once and
 * starts pending over as all of it. */
uint32_t ui_band(ui *u, uint32_t *pending, uint32_t ms);
/* the next band to draw out of pending (UI_D_* bits, in drawing order), taken out of it; 0: none */
uint32_t ui_next_band(uint32_t *pending);
/* a button's LED (interaction.md): UI_LED_OFF, UI_LED_LIT or UI_LED_GLOW (dim) */
enum { UI_LED_OFF = 0, UI_LED_LIT = 1, UI_LED_GLOW = 2 };
uint32_t ui_led(const ui *u, uint32_t button);
#endif
