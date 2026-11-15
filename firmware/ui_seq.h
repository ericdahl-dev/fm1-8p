/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p SEQ view (interaction.md 5, #39): the sequencer on the panel. Plain C; ui.c hands it the
 * events while SEQ view is open, and the transport (PLAY) from every state. */
#ifndef UI_SEQ_H
#define UI_SEQ_H
#include "ui.h"

/* A SEQ tap in PLAY or EDIT: opens SEQ view (a SEQ tap in it goes back). */
void ui_seq_open(ui *u);
/* PLAY, in any state: starts or stops the sequencer, on what was last asked (not on whether the
 * interrupt has let a stop's notes go yet, so taps alternate). */
void ui_seq_transport(ui *u);
/* An event while SEQ view is open. */
void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);

#endif
