/* SPDX-License-Identifier: GPL-3.0-or-later */
/* fm1-8p sequencer core (#35, #36): a 16-step poly pattern played into the engine at exact sample
 * times. Plain C99: no float, no malloc, no host code. `play` owns one (play_seq), so the UI reaches it
 * through the `play *` it already has: play.h gains `seq *play_seq(play *p)` in S1.
 *
 * Two sides, as play.h splits them:
 * - main loop: edits the pattern and asks for play / stop. An edit is published whole (a version
 *   count, as play's loads), and the audio interrupt takes it at the next step's start, so a step is
 *   never played half edited.
 * - audio interrupt (play_render): advances the clock in samples and turns steps into note on / off
 *   events at their sample offset inside the block. It writes the playhead fields below; the main loop
 *   only reads them.
 *
 * DRAFT (S1): the API S2 (the SEQ panel, agent1) is written against. Names and shapes may still move
 * before S1 lands; S2 gets told. */
#ifndef SEQ_H
#define SEQ_H
#include <stdint.h>

#define SEQ_STEPS 16u                   /* a pattern's steps (32 / 64 later) */
#define SEQ_NOTES 6u                    /* a step's notes: the voice count after #34; more are dropped */
#define SEQ_PPQ 4u                      /* steps per quarter note: a step is a 16th */
#define SEQ_TEMPO_MIN 300u              /* tempo, tenths of a BPM: 30.0 .. 300.0 */
#define SEQ_TEMPO_MAX 3000u
#define SEQ_TEMPO_DEF 1200u             /* 120.0 BPM */
#define SEQ_GATE_DEF 50u                /* gate, % of a step (S3: 1..100) */
#define SEQ_SWING_DEF 50u               /* swing, % (S3: 50 none .. 75) */
#define SEQ_VEL_DEF 100u

/* What a step plays. */
typedef enum {
    SEQ_REST = 0,                       /* nothing; a note held from a tie ends at this step's start */
    SEQ_NOTES_ON = 1,                   /* its notes (one: a note, more: a chord), for the gate */
    SEQ_TIE = 2                         /* the notes sounding from the step before go on through it */
} seq_kind;

typedef struct {
    uint8_t kind;                       /* seq_kind */
    uint8_t n;                          /* SEQ_NOTES_ON: notes in note[] (1..SEQ_NOTES) */
    uint8_t vel;                        /* 1..127, every note of the step (S3: per step, already here) */
    uint8_t note[SEQ_NOTES];            /* MIDI notes, before transpose */
} seq_step;

typedef struct {
    uint8_t length;                     /* steps played, 1..SEQ_STEPS */
    uint8_t gate_pct;
    uint8_t swing_pct;
    uint16_t tempo;                     /* tenths of a BPM */
    seq_step step[SEQ_STEPS];
} seq_pattern;

/* A note on or off the interrupt hands play_render: `at` frames into the block (0 .. n-1). */
typedef struct {
    uint16_t at;
    uint8_t note;
    uint8_t vel;                        /* 0: note off */
} seq_event;
#define SEQ_EVENTS (2u * SEQ_NOTES * 2u)    /* the most one block can hold (a step's offs and ons, twice) */

typedef struct {
    /* main loop: the pattern being edited, and its version (+1 per edit published) */
    seq_pattern edit;
    volatile uint32_t edits;
    /* main loop: asks; the interrupt acts at its next block */
    volatile uint32_t ask_play;         /* +1 per play asked */
    volatile uint32_t ask_stop;         /* +1 per stop asked */
    /* interrupt only */
    seq_pattern run;                    /* the pattern playing, taken from edit at a step's start */
    uint32_t edits_seen, play_seen, stop_seen;
    uint32_t frac;                      /* the clock: position in the step, in samples << 16 */
    uint8_t sounding[SEQ_NOTES];        /* the notes the sequencer has on (0xFF: none) */
    /* interrupt writes, main loop reads */
    volatile uint32_t playing;          /* 1 while the transport runs */
    volatile int32_t step_now;          /* the step playing, 0 .. length-1; -1 stopped */
    volatile uint32_t steps;            /* +1 per step played: the UI polls it to redraw */
} seq;

/* At init (play_init): the empty pattern (every step a rest), length 16, the defaults; stopped. */
void seq_init(seq *s);

/* main loop: the pattern. Each call publishes the edit (the interrupt takes it at a step's start). */
uint32_t seq_length(const seq *s);
void seq_set_length(seq *s, uint32_t length);          /* clamped to 1..SEQ_STEPS */
const seq_step *seq_get_step(const seq *s, uint32_t i);   /* the edited pattern's step i */
/* step i plays notes[0..n-1] (n clamped to SEQ_NOTES; 0 makes it a rest) at velocity vel */
void seq_set_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel);
void seq_clear_step(seq *s, uint32_t i);               /* a rest */
void seq_toggle_tie(seq *s, uint32_t i);               /* a tie, or back to a rest */
uint32_t seq_tempo(const seq *s);
void seq_set_tempo(seq *s, uint32_t tenths_bpm);       /* clamped to SEQ_TEMPO_MIN..MAX */
void seq_set_gate(seq *s, uint32_t pct);               /* (S3; stored, not yet used) */
void seq_set_swing(seq *s, uint32_t pct);              /* (S3; stored, not yet used) */

/* main loop: the transport. play starts at step 1; stop lets go of every note the sequencer started. */
void seq_play(seq *s);
void seq_stop(seq *s);
uint32_t seq_playing(const seq *s);                    /* 1 while it runs (also play_silent's: #201) */
int32_t seq_step_now(const seq *s);                    /* the playhead, -1 stopped */
uint32_t seq_steps(const seq *s);                      /* changes once per step: redraw when it does */

/* audio interrupt (play_render): the next n frames (n <= 256) at fs Hz. Writes their note events, in
 * time order, into ev (room for SEQ_EVENTS) and returns how many. */
uint32_t seq_render(seq *s, uint32_t n, uint32_t fs, seq_event *ev);
#endif
