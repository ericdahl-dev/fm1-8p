/* fm1-8p UI: its data tables (docs/ui/visual.md, docs/ui/interaction.md). Plain C, const. */
#ifndef UI_MAP_H
#define UI_MAP_H
#include <stdint.h>

/* the section colors' order: 1x DCO-1 .. 8x CTRL, then SEQ, GLO and PLAY */
enum { UI_SEC_SEQ = 8, UI_SEC_GLO, UI_SEC_PLAY, UI_SECTIONS };

/* A theme: visual.md's style table, RGB565 */
typedef struct {
    uint16_t bg, surface, surface_hi, line, text, dim, ink, window, led;
    uint16_t section[UI_SECTIONS];
} ui_theme;

extern const ui_theme UI_MEMBRANE;
#endif
