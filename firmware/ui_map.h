/* fm1-8p UI: its data tables (docs/ui/visual.md, docs/ui/interaction.md). Plain C, const. */
#ifndef UI_MAP_H
#define UI_MAP_H
#include <stdint.h>

/* the section colors' order (visual.md's table): 1x DCO-1 .. 8x CTRL, then SEQ, GLO and PLAY */
enum { UI_SEC_VCF = 3, UI_SEC_SEQ = 8, UI_SEC_GLO, UI_SEC_PLAY, UI_SECTIONS };

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
