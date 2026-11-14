/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p SAVE (interaction.md, SAVE): naming the patch and writing it to a user slot. Plain C; ui.c
 * hands it the events while SAVE is open. */
#ifndef UI_SAVE_H
#define UI_SAVE_H
#include "ui.h"

/* A SAVE tap in PLAY or EDIT: opens SAVE. */
void ui_save_open(ui *u, uint32_t ms);
/* From ui_take: a write that waits for silence happens on the first silent moment; until then every
 * screen says SAVING WHEN SILENT. */
void ui_save_tick(ui *u, uint32_t ms);
/* HOME tap while a write waits: cancels it (SAVE CANCELED). */
void ui_save_cancel(ui *u, uint32_t ms);
/* An event while SAVE is open. */
void ui_save_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);

#endif
