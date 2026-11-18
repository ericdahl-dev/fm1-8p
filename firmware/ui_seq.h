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
#define UI_SEQ_ARM_MS 3000u             /* a tapped step with no note played this long: steps again */
/* SEQ view's step of key k (0..26 from F3): the white keys F3..G5 are steps 0..15, a black key -1 */
int32_t ui_seq_step_of_key(uint32_t k);
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
/* knob k's value in full, for the readout's last line while it has the focus: S2's BEND as the percent
 * and the semitones it gives at BEND RANGE now ("+52 % = +1.0 st"); "" for the others. s: UI_SEQ_TEXT_LEN */
void ui_seq_detail(const ui *u, uint32_t k, char *s);
/* 0 when knob k shows a value it can't change now: S2's knobs on a rest or a tie, which keep their bend,
 * mod and move (#42) but have no notes to play them with; the screen dims it */
int ui_seq_active(const ui *u, uint32_t k);
/* step i as notes by name ("C4 E4 G4"), REST or TIE; while a step is armed, the notes played for it so
 * far by name ("" none); s:
 * UI_SEQ_TEXT_LEN */
#define UI_SEQ_TEXT_LEN 40u
void ui_seq_step_text(const ui *u, uint32_t i, char *s);
void ui_seq_chord_text(const ui *u, char *s);
/* SEQ view's footer: left the pattern playing ("PTN 11 NIGHT DRIVE*", `*` edited), right "NEXT 12"
 * while one is queued, else "" (each UI_SEQ_TEXT_LEN) */
void ui_seq_footer_text(const ui *u, char *left, char *right);
/* The piano roll (visual.md, S5a): its pitch range fitted to the pattern's notes (and an armed step's
 * notes so far), at least an octave, UI_ROLL_H px tall: 3 px a semitone up to 24 rows, 2 px up to 36,
 * wider centered on the cursor step. It refits only when a note falls outside; 1 when it did (then
 * every column redraws). And the columns to redraw (bit i: step i), taken. */
#define UI_ROLL_H 72u
int ui_seq_roll_fit(ui *u);
uint32_t ui_seq_roll_take(ui *u);
/* From ui_take, in SEQ view: the playhead's columns, the armed step's blink, the header and key strip */
void ui_seq_tick(ui *u, uint32_t ms);
/* notes' chord name when they're a common triad or seventh (Eric, R3: MAJ, MIN, DIM, AUG, SUS2, SUS4, 7,
 * MAJ7, MIN7), "C MIN7"; else "". s: UI_SEQ_TEXT_LEN */
void ui_seq_chord_name(const uint8_t *note, uint32_t n, char *s);
/* A REC tap in SEQ view (or just opened by one): step record armed, or disarmed when it was */
void ui_seq_rec(ui *u);
/* The transport's clock (interaction.md 7): "INT", "MIDI", or "WAITING FOR CLOCK" while PLAY waits for
 * the clock's start. s: UI_SEQ_TEXT_LEN */
void ui_seq_clock_text(const ui *u, char *s);
/* 1 when G13 is MIDI and no clock is coming in (none yet, or lost): the screen shows MIDI NO CLOCK, and
 * no tempo (--) where the clock's would be */
int ui_seq_no_clock(const ui *u);
/* The tempo SEQ view shows, tenths of a BPM: the clock's in whole BPM when it runs the steps or they're
 * stopped (#38), else the pattern's */
uint32_t ui_seq_tempo_shown(const ui *u);
/* KEY TRANSPOSE (#39, interaction.md 5): on, with the sequencer playing, outside SEQ view, a panel key k
 * makes no sound and sets the transpose to the lowest key so held (its pitch before the octave shift)
 * minus C4, from the next step; its up is taken too, and keeps the transpose. 1 when it took the key.
 * A key's up goes where its down went, whatever has changed since. may: 0 while the keypad has the keys */
int ui_seq_ktr_key(ui *u, uint32_t k, int down, int may);
/* The same for a MIDI note in (on: a note on with a velocity), against MIDI note 60, in any view; in SEQ
 * view not while a step is armed or REC is (then it plays that step). 1 when it took the note (it
 * sounds nowhere, and its note off is taken too). */
int ui_seq_ktr_note(ui *u, uint32_t note, uint32_t vel, int on);
/* All notes off (CC 120 / 123) or a channel changed: no MIDI note is held for the transpose any more */
void ui_seq_ktr_all_off(ui *u);
/* SEQ view: a step key (0..15) down or up */
void ui_seq_step_key(ui *u, uint32_t k, int down, uint32_t ms);
/* An event while SEQ view is open. */
void ui_seq_event(ui *u, uint32_t ev, int32_t arg, uint32_t ms);

#endif
