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
#include "../engine/fm1_8p.h"

#define SEQ_STEPS 16u                   /* a pattern's steps (32 / 64 later) */
#define SEQ_NOTES 6u                    /* a step's notes: the voice count after #34; more are dropped */
#define SEQ_PPQ 4u                      /* steps per quarter note: a step is a 16th */
#define SEQ_TEMPO_MIN 300u              /* tempo, tenths of a BPM: 30.0 .. 300.0 */
#define SEQ_TEMPO_MAX 3000u
#define SEQ_TEMPO_DEF 1200u             /* 120.0 BPM */
#define SEQ_GATE_MIN 5u                 /* gate, % of a step: 5..100 (interaction.md, S1 PATTERN) */
#define SEQ_GATE_DEF 50u
#define SEQ_SWING_DEF 50u               /* swing, %: 50 straight .. 75 (the even steps late by half a step) */
#define SEQ_OUT_EV 8u                   /* #286: starts and stops the main loop has yet to send */
#define SEQ_OUT_M 16u                   /* #286: a followed clock's bytes, between two takes */
#define SEQ_TRANSPOSE_MAX 24            /* transpose, semitones: -24..+24 (interaction.md, S3 PLAY) */
#define SEQ_VEL_DEF 100u
/* #38: external MIDI clock, 24 ticks a quarter note (6 a step), its times on the host's 24 MHz
 * counter (fm1-dock's ticks()) */
#define SEQ_CLK_HZ 24000000u
#define SEQ_CLK_PPQN 24u
#define SEQ_CLK_HIST 25u                /* ticks kept for the tempo: a beat's worth of lengths */
enum { SEQ_CLK_AUTO = 0, SEQ_CLK_INT = 1, SEQ_CLK_MIDI = 2 };

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
    /* #42: a notes step's bend (-8192..8191, center 0) and mod wheel (0..127): JUMP sets them at its
     * start, GLIDE ramps to them from the values before it over its length. Rests and ties keep. */
    int16_t bend;
    uint8_t mod;
    uint8_t glide;                      /* 0 JUMP, 1 GLIDE */
} seq_step;

typedef struct {
    uint8_t length;                     /* steps played, 1..SEQ_STEPS */
    uint8_t gate_pct;
    uint8_t swing_pct;
    uint16_t tempo;                     /* tenths of a BPM */
    seq_step step[SEQ_STEPS];
} seq_pattern;

/* What the interrupt hands play_render, `at` frames into the block (0 .. n-1): a note on or off, or a bend
 * or mod wheel value (#42). seq_apply_event plays one into the engine. */
enum { SEQ_EV_NOTE = 0, SEQ_EV_BEND = 1, SEQ_EV_MOD = 2 };
#define SEQ_MOD_LIVE 255                /* a SEQ_EV_MOD value: back to the live wheel (the last MIDI CC1) */
#define SEQ_GLIDE_FRAMES 32u            /* a GLIDE step's bend and mod move every this many frames */
typedef struct {
    uint16_t at;
    uint8_t kind;                       /* SEQ_EV_* */
    uint8_t note;
    uint8_t vel;                        /* a note's: 0, note off */
    int16_t value;                      /* a bend's (-8192..8191) or a mod's (0..127, or SEQ_MOD_LIVE) */
} seq_event;
/* the most one block holds: a step is over SEQ_BLOCK frames even at 300 BPM and 8 kHz, so at most a
 * gate's offs, a step's offs and ons, its gate's offs, a glide's updates (bend and mod) and a step's
 * bend and mod twice */
#define SEQ_EVENTS (4u * SEQ_NOTES + 2u * (SEQ_BLOCK / SEQ_GLIDE_FRAMES) + 4u)

typedef struct {
    /* main loop: the pattern being edited, and its version (+1 per edit published). While `editing`
     * is set the interrupt doesn't take it (play's `loading`), so a step never plays half an edit. */
    seq_pattern edit;
    volatile uint32_t editing;
    volatile uint32_t edits;
    /* main loop: asks; the interrupt acts at its next block */
    volatile uint32_t ask_play;         /* +1 per play asked */
    volatile uint32_t ask_stop;         /* +1 per stop asked */
    volatile uint32_t want;             /* main loop writes (1 from seq_play to seq_stop), ISR reads */
    volatile int32_t transpose;         /* main loop writes: semitones, taken at a step's start */
    /* pattern loads (#40): the copy last loaded or saved (main loop: seq_edited), and the next pattern,
     * queued while playing: the main loop writes it, then sets next_pending; the interrupt takes it in
     * place of the running pattern after the pass's last step, and counts next_taken */
    seq_pattern stored;
    seq_pattern next;
    volatile uint32_t next_pending;
    volatile uint32_t next_taken;
    uint32_t next_taken_seen;           /* main loop: the last next_taken it adopted */
    /* interrupt only */
    int32_t run_transpose;              /* the transpose playing */
    seq_pattern run;                    /* the pattern playing, taken from edit at a step's start */
    uint32_t edits_seen, play_seen, stop_seen;
    uint32_t pos;                       /* the step playing */
    uint32_t acc;                       /* the clock: tempo x SEQ_PPQ a sample, a step at fs x 600 */
    uint32_t gated;                     /* 1 once this step's notes are let go (its gate passed) */
    uint8_t sounding[SEQ_NOTES];        /* the notes the sequencer has on */
    uint8_t n_sounding;
    int32_t cur_bend, cur_mod;          /* #42: the bend and mod it last sent */
    int32_t from_bend, from_mod, to_bend, to_mod;   /* a GLIDE step's move */
    uint32_t glide_on, glide_next;      /* gliding; the clock (acc) at its next update */
    /* #38, external MIDI clock. Main loop: the clock source (G13) and the clock as it comes in, each
     * tick stamped with the host's 24 MHz counter (SEQ_CLK_HZ) */
    uint8_t clk_src;                    /* SEQ_CLK_AUTO, _INT, _MIDI */
    uint8_t clk_present;                /* ticks are coming (none for the time out: gone) */
    uint8_t clk_pending;                /* a start (1) or continue (2) waiting for its first tick */
    uint8_t clk_running;                /* the transport is the clock's (started by it) */
    uint8_t clk_armed;                  /* PLAY pressed with the clock in charge: waiting for a start */
    uint8_t clk_started;                /* a start has played since init (a continue has somewhere to go on) */
    uint32_t clk_t[SEQ_CLK_HIST];       /* the last ticks' times, a ring (clk_n is the next slot) */
    uint32_t clk_n;                     /* ticks since the clock appeared */
    uint32_t clk_dt;                    /* a tick's length now, host counts (0: not measured yet) */
    uint32_t clk_shown;                 /* the tick length, smoothed, Q4 (the rate's) */
    uint32_t clk_view;                  /* smoothed again, Q4: the tempo shown */
    uint32_t clk_pend_t;                /* when the pending start or continue came (AUTO drops it after
                                         * the clock's gone time with no tick) */
    uint32_t clk_whole;                 /* clk_view in whole BPM, tenths, held: it moves only past
                                         * CLK_HOLD of it, so a clock's jitter can't flip the digits */
    int32_t clk_freq;                   /* the loop's integral, units a frame, Q4 */
    uint32_t clk_win;                   /* ticks in the length's window (since a tempo change) */
    uint32_t clk_target;                /* the units the latest tick should be at, from tick 0 */
    uint32_t clk_k;                     /* ticks since the start's tick 0 (or the loop's last hard pull) */
    uint32_t clk_far;                   /* ticks running the steps have been far off them */
    uint32_t clk_late;                  /* the last tick came held up, and was taken when it was due (1;
                                                 * 2: and more came bunched after it) */
    uint32_t clk_raw;                   /* when the last tick came (clk_t has when it was taken) */
    /* main loop writes, interrupt reads: follow the clock at ext_inc units a frame, a start's
     * first tick at host time ext_t0 */
    volatile uint32_t ext;
    volatile uint32_t ext_inc;
    volatile uint32_t ext_t0;
    volatile uint32_t ext_cont;         /* the ask is a continue: on from the step after the last */
    volatile uint32_t ext_hold;         /* a start is coming: no next step till it does */
    /* interrupt writes at each block's start (seq_block_time), main loop reads, as a whole while
     * blk_seq is even (a seqlock) */
    volatile uint32_t blk_seq, blk_t, blk_u, blk_inc, blk_fs;
    uint32_t upos;                      /* interrupt: units since a start's tick 0, wrapping */
    uint32_t fs;                        /* interrupt: the rate of the last render */
    /* interrupt writes, main loop reads */
    volatile uint32_t playing;          /* 1 while the transport runs */
    volatile int32_t step_now;          /* the step playing, 0 .. length-1; -1 stopped */
    volatile uint32_t steps;            /* +1 per step played: the UI polls it to redraw */
    /* #286 G14 CLOCK OUT. The main loop sets out_want; a transport's start latches it into out_on (a G14
     * change waits for the next PLAY). On its own clock the interrupt counts the ticks due, 24 a quarter
     * (out_acc, unswung), and puts each start and stop in out_ev stamped with the tick count then; the
     * main loop takes them in order (seq_clock_out_take) and sends them, as only it may. */
    uint8_t out_want;                   /* main loop: G14 */
    volatile uint8_t out_on;            /* this transport sends: latched at its start */
    uint32_t out_acc;                   /* interrupt: the clock since the last tick, as acc counts it */
    volatile uint32_t out_ticks;        /* interrupt: ticks due, all told */
    uint32_t out_sent;                  /* main loop: ticks sent */
    struct { uint8_t st; uint32_t at; } out_ev[SEQ_OUT_EV];   /* interrupt: a start or stop, at a tick count */
    volatile uint32_t out_w;            /* interrupt: out_ev written */
    uint32_t out_r;                     /* main loop: out_ev taken */
    uint8_t out_m[SEQ_OUT_M];           /* main loop: a followed clock's bytes to pass on (Eric, Q2) */
    uint32_t out_mw, out_mr;
} seq;

/* At init (play_init): the empty pattern (every step a rest), length 16, the defaults; stopped. */
void seq_init(seq *s);
/* p as the empty pattern: 16 rests, LENGTH 16, the default TEMPO, SWING and GATE (an empty pattern slot's) */
void seq_pattern_empty(seq_pattern *p);

/* main loop: the pattern. Each call publishes the edit (the interrupt takes it at a step's start). */
uint32_t seq_length(const seq *s);
void seq_set_length(seq *s, uint32_t length);          /* clamped to 1..SEQ_STEPS */
const seq_step *seq_get_step(const seq *s, uint32_t i);   /* the edited pattern's step i */
/* step i plays notes[0..n-1] (n clamped to SEQ_NOTES; 0 makes it a rest) at velocity vel */
void seq_set_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel);
/* (seq_set_step makes its bend center, its mod 0 and its move JUMP) */
/* step i all at once, as one edit published (#42: a step written with the live bend and mod, so the
 * interrupt never starts it between its notes and its bend): seq_set_step, then bend, mod and move as
 * seq_set_step_bend_mod has them */
void seq_write_step(seq *s, uint32_t i, const uint8_t *notes, uint32_t n, uint32_t vel, int32_t bend,
                    int32_t mod, uint32_t glide);
/* step i's bend (clamped to -8192..8191), mod (0..127) and move (0 JUMP, else GLIDE), #42 */
void seq_set_step_bend_mod(seq *s, uint32_t i, int32_t bend, int32_t mod, uint32_t glide);
/* a notes step i's velocity (clamped to 1..127), nothing else of it; a rest or a tie: nothing */
void seq_set_step_vel(seq *s, uint32_t i, uint32_t vel);
void seq_clear_step(seq *s, uint32_t i);               /* a rest */
void seq_toggle_tie(seq *s, uint32_t i);               /* a tie, or back to a rest */
/* main loop: load a pattern (#40). Stopped (or before play has started), it loads at once. Playing, it
 * is queued: it takes over after the running pattern's last step (seq_main_tick then has it in the edit
 * buffer); a later load replaces the queued one. Either way it is not edited (seq_edited 0). */
void seq_load_pattern(seq *s, const seq_pattern *p);
uint32_t seq_next_queued(const seq *s);                 /* 1 while a loaded pattern waits for the pass */
void seq_cancel_next(seq *s);                           /* the running pattern goes on round */
/* main loop, each draw: 1 when the queued pattern has taken over since the last call (the edit buffer is
 * it now) */
uint32_t seq_main_tick(seq *s);
/* PATTERN EDITED: 1 when the edit buffer differs from the pattern last loaded or saved */
uint32_t seq_edited(const seq *s);
void seq_mark_saved(seq *s);                            /* (after a SAVE: not edited) */
/* main loop: the transpose (a performance setting, not part of the pattern), -24..+24 semitones,
 * from the next step; a note it puts outside 0..127 is left out */
void seq_set_transpose(seq *s, int32_t semis);
int32_t seq_transpose(const seq *s);
uint32_t seq_tempo(const seq *s);
void seq_set_tempo(seq *s, uint32_t tenths_bpm);       /* clamped to SEQ_TEMPO_MIN..MAX */
void seq_set_gate(seq *s, uint32_t pct);               /* clamped to SEQ_GATE_MIN..100 */
void seq_set_swing(seq *s, uint32_t pct);              /* clamped to 50..75 */

/* main loop: the transport. play starts at step 1; stop lets go of every note the sequencer started, and
 * loads a queued pattern at once. */
void seq_play(seq *s);
void seq_stop(seq *s);
/* 1 from seq_play until the stop has let every note go: play_silent's too (#201), so a SAVE waits */
uint32_t seq_playing(const seq *s);
int32_t seq_step_now(const seq *s);                    /* the playhead, -1 stopped */
uint32_t seq_steps(const seq *s);                      /* changes once per step: redraw when it does */

/* #38, external MIDI clock. main loop: the clock source (G13: SEQ_CLK_AUTO follows clock while it
 * comes, INT ignores it, MIDI only follows it) */
void seq_set_clock_source(seq *s, uint32_t src);
/* main loop: a real-time byte as it comes in (0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop; others
 * are ignored) at host time t (SEQ_CLK_HZ counts, wrapping) */
void seq_clock_in(seq *s, uint32_t st, uint32_t t);
/* main loop, often: the time now; a clock gone for its time out stops a transport it was running */
void seq_clock_poll(seq *s, uint32_t t);
/* 1 while the clock source in use is MIDI (the header's MIDI, TEMPO's MIDI CLOCK) */
uint32_t seq_clock_external(const seq *s);
/* the clock's tempo, tenths of a BPM (0: no clock coming in) */
uint32_t seq_clock_tempo(const seq *s);
/* the same to read: a whole BPM (in tenths), held against the clock's jitter (it moves when the clock is
 * 0.75 BPM off it); 0: none */
uint32_t seq_clock_tempo_whole(const seq *s);
/* 1 while the sequencer waits for the clock: PLAY armed for its start, or a start or continue come in
 * with no tick yet (WAITING FOR CLOCK; the PLAY LED blinks) */
uint32_t seq_waiting(const seq *s);
/* main loop: G14 CLOCK OUT (#286), from the next PLAY: MIDI start, a clock at 24 a quarter, and stop */
void seq_set_clock_out(seq *s, uint32_t on);
/* main loop: the next MIDI real-time byte to send (0xF8 clock, 0xFA start, 0xFC stop) into *st, in the
 * order they fell due; 0 when there's none. Call it often (each main loop pass): a tick waits till then */
int seq_clock_out_take(seq *s, uint8_t *st);
/* audio interrupt, before seq_render: the block's start, at host time t */
void seq_block_time(seq *s, uint32_t t);

/* audio interrupt (play_render): the next n frames (1..SEQ_BLOCK) at fs Hz (8000..192000), a step's
 * chord cut to its first `voices` notes. Writes their events, in time order (at one frame: the offs, a
 * step's bend and mod, then its ons), into ev (room for SEQ_EVENTS), and returns how many. */
#define SEQ_BLOCK 256u
uint32_t seq_render(seq *s, uint32_t n, uint32_t fs, uint32_t voices, seq_event *ev);
/* audio interrupt: one event into the engine (live_mod: the live wheel, for SEQ_MOD_LIVE) */
void seq_apply_event(fm_engine *e, const seq_event *ev, uint32_t live_mod);
#endif
