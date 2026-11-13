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
    const int8_t *order;                /* a choice: every engine value in panel order (0: steps) */
    uint8_t n_order;
    uint8_t stops;                      /* bit i: order[i] is a knob position (the rest only show) */
    uint16_t id2;                       /* paired: a second param, the value's bit 1 (id's is bit 0) */
    uint8_t paired;                     /* 1: one JX switch on two params (CROSS MOD: sync, ring) */
} ui_slot;

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
extern const uint8_t UI_RING_SEL[], UI_RING_ENV[], UI_RING_FX[], UI_RING_LFO[];

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
} ui_theme;

extern const ui_theme UI_MEMBRANE;
#endif
