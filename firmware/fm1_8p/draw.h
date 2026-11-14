/* FM1-8P's screen (draw.c): the pages, drawn through fm1-dock's host. Main loop only. */
#ifndef FM1_8P_DRAW_H
#define FM1_8P_DRAW_H
#include "hb_module.h"
#include "play.h"
#include "ui.h"

/* the host, and the play and ui the pages show */
void ui_draw_init(const hb_host_t *h, play *p, ui *u);
/* one draw() call's worth: what ui_take says changed (full: all of it), a band part at most */
void ui_draw_frame(uint32_t full);
/* the theme (visual.md's style table): from the next frame, all of the screen redrawn in it */
void ui_draw_theme(const ui_theme *t);
#endif
