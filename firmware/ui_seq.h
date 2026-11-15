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
/* SEQ view's pages (interaction.md 5): each knob's label, its full name, and 1 when it's live (0: it
 * waits on its slice and shows --; a NULL label: nothing on that knob) */
#define UI_SEQ_PAGES 3u
#define UI_SEQ_CLEAR_MS 1500u           /* SEL held this long in SEQ view clears the pattern */
typedef struct {
    const char *label, *full;
    uint8_t live;
} ui_seq_slot;
extern const char *const UI_SEQ_PAGE_NAME[UI_SEQ_PAGES];
extern const ui_seq_slot UI_SEQ_SLOTS[UI_SEQ_PAGES][4];
/* knob k's value on the SEQ page shown: TEMPO in tenths of a BPM, LENGTH, SWING %, GATE %, VEL (the
 * cursor step's, 0 for a rest or a tie), TRANSPOSE in semitones */
int32_t ui_seq_value(const ui *u, uint32_t k);
/* knob k's value as its column reads it: the number into num, a short unit into unit (UI_VALUE_LEN
 * each): TEMPO 120 / 123.5 BPM, LENGTH 16, SWING and GATE 50 %, VEL 100, TRANSPOSE +3 st; -- waits */
void ui_seq_column(const ui *u, uint32_t k, char *num, char *unit);
/* step i as notes by name ("C4 E4 G4"), REST or TIE; the chord held now by name ("" none); s:
 * UI_SEQ_TEXT_LEN */
#define UI_SEQ_TEXT_LEN 40u
void ui_seq_step_text(const ui *u, uint32_t i, char *s);
void ui_seq_chord_text(const ui *u, char *s);
/* SEQ view's footer: left the pattern playing ("PTN 11 NIGHT DRIVE*", `*` edited), right "NEXT 12"
 * while one is queued, else "" (each UI_SEQ_TEXT_LEN) */
void ui_seq_footer_text(const ui *u, char *left, char *right);
/* SEQ view: a step key (0..15) down or up */
void ui_seq_step_key(ui *u, uint32_t k, int down, uint32_t ms);
/* An event while SEQ view is open. */
void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);

#endif
