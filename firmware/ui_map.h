/* fm1-8p UI: its data tables (docs/ui/visual.md, docs/ui/interaction.md). Plain C, const. */
#ifndef UI_MAP_H
#define UI_MAP_H
#include <stdint.h>

/* What a slot holds (interaction.md section 3): a live param, one whose issue hasn't landed (shown
 * as `--`, its knob does nothing), or nothing. */
typedef enum { UI_RESERVED, UI_LIVE, UI_PENDING } ui_slot_kind;   /* (0: a slot left out is empty) */

/* One numbered parameter slot. */
typedef struct {
    uint8_t num;                        /* 41: section 4, parameter 1 */
    const char *label;                  /* the knob column's, at most 7 characters */
    const char *full;                   /* the focus row's full label */
    uint16_t id;                        /* fm_param_id, when live */
    uint8_t kind;                       /* ui_slot_kind */
    int32_t step, fast;                 /* engine units a click moves it; a fast turn's click */
    const int16_t *order;               /* a choice: every engine value in panel order (0: steps) */
    uint8_t n_order;
    uint8_t stops;                      /* bit i: order[i] is a knob position (the rest only show) */
    uint16_t id2;                       /* UI_HOW_PAIR's second param; UI_HOW_MODE's (first) depth */
    uint8_t how;                        /* ui_how: what the knob's value is */
    uint16_t id3;                       /* UI_HOW_MODE's second depth (id2 again when one) */
} ui_slot;

/* How a slot's value is made (ui_slot_value):
 * PAIR  one JX switch on two params: id | id2 << 1 (CROSS MOD: sync, ring)
 * MODE  a JX ENV MODE: the source param id | the depth's sign << 1 (inverted), the depths id2, id3
 * SIZE  a depth's size, its sign being its ENV MODE's
 * TIME  a time: a click is an entry of the time table (UI_TIMES), a fast one four
 * RATE  the LFO rate, the same on the rate table (UI_RATES) */
typedef enum { UI_HOW_PLAIN, UI_HOW_PAIR, UI_HOW_MODE, UI_HOW_SIZE, UI_HOW_TIME, UI_HOW_RATE } ui_how;

/* interaction.md's page order: 1A, 1B, 2A, 2B, 3A, 4A, 4B, 5A, 5B, 6A, 6B, 7A, 8A, 8B, 9A, 9B, 0A, 0B */
#define UI_PAGES 18u
#define UI_PAGE_5A 7u
typedef struct {
    char name[3];                       /* "5A" */
    uint8_t color;                      /* its section color (UI_SEC_*, the theme's) */
    const char *section;                /* "VCF" */
} ui_page_info;
extern const ui_page_info UI_PAGE_INFO[UI_PAGES];
extern const ui_slot UI_SLOTS[UI_PAGES * 4u];   /* page p's knob k: UI_SLOTS[4 p + k] */

/* the section buttons' rings (interaction.md 4): page indexes, UI_RING_END after the last */
#define UI_RING_END 0xFFu
/* each section's name for the number jump's list, by its digit (0 AFTERTOUCH .. 9 ENV-2) */
extern const char *const UI_SECTION_NAME[10];
extern const uint8_t UI_SECTION_PAGE[10];
/* PLAY's performance knobs (interaction.md 6): 52 CUTOFF, 53 RES, 55 ENV, 94 RELEASE, as UI_SLOTS indexes */
extern const uint8_t UI_PLAY_SLOT[4];       /* each section's page A, by its digit */
extern const uint8_t UI_RING_SEL[], UI_RING_ENV[], UI_RING_FX[], UI_RING_LFO[];

/* GLO's slots (interaction.md 4): G11..G34, each a setting (UI_SETTINGS_INFO's index), 3 pages of 4 */
typedef struct {
    uint8_t num;                        /* 34: page 3, knob 4 */
    const char *label;                  /* the column's, at most 7 characters */
    const char *full;                   /* the focus row's */
    uint8_t setting;                    /* UI_SETTINGS_INFO's index */
    uint8_t kind;                       /* UI_LIVE, or UI_PENDING: waits on its feature, shows -- */
    uint8_t step, fast;                 /* a click; a fast turn's click */
    const char *const *names;           /* a choice's names from its min (0: a number) */
    const char *const *cols;            /* shorter names for a column (0: names) */
    const char *zero;                   /* the min's name when it is a number otherwise ("ALL"), or 0 */
} ui_glo_slot;
#define UI_GLO_PAGES 3u
#define UI_GLO_SLOTS_COUNT (4u * UI_GLO_PAGES)
extern const ui_glo_slot UI_GLO_SLOTS[UI_GLO_SLOTS_COUNT];

#define UI_TIME_COUNT 122u
extern const uint16_t UI_TIMES[UI_TIME_COUNT];   /* ms, rising (interaction.md, units) */
#define UI_RATE_COUNT 109u
extern const uint16_t UI_RATES[UI_RATE_COUNT];   /* centihertz, rising (the LFO rate table) */

/* the section colors' order (visual.md's table): 1x DCO-1 .. 8x CTRL, then SEQ, GLO and PLAY */
enum { UI_SEC_DCO1, UI_SEC_DCO2, UI_SEC_MIX, UI_SEC_VCF, UI_SEC_VCF_ENV, UI_SEC_VCA, UI_SEC_LFO, UI_SEC_CTRL,
       UI_SEC_SEQ, UI_SEC_GLO, UI_SEC_PLAY, UI_SECTIONS };

/* A theme: visual.md's style table. Colors RGB565; a mix is n / 256 of the way from one color to
 * another. */
typedef struct {
    uint16_t bg, surface, surface_hi, line, text, dim, ink, window, led;
    uint16_t section[UI_SECTIONS];
    uint16_t ghost;                     /* unlit LED segments: this mix of the LED color */
    uint8_t chip;                       /* 1: the section name on a chip; 0: under a 3 px band */
    uint8_t meter_segments;             /* 0: one bar */
    uint16_t focus[3];                  /* the focus frames inward, as mixes from bg (0: none) */
    uint16_t hatch;                     /* the fill under graph curves (0: none) */
    uint16_t sun[6];                    /* the PLAY art's sunset bands, top to bottom */
    uint8_t led_section;                /* 1: the LED digits in the section's color (the led key: none) */
    uint8_t art;                        /* the PLAY art: UI_ART_BADGE or UI_ART_SUN (a sun over a grid) */
    uint16_t grid, horizon;             /* the sun's perspective grid and horizon (UI_ART_SUN) */
    uint16_t fill_only;                 /* bit per section: its color fills (chips, meters, frames) but is
                                         * too dark for text, which takes the lettering (sec_text) */
} ui_theme;
enum { UI_ART_BADGE, UI_ART_SUN };

extern const ui_theme UI_MEMBRANE, UI_NIGHT_PANEL;
#endif
