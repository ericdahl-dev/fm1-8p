/* fm1-8p CPU test: drives the unchanged engine with a fixed note pattern and keeps the worst
 * render time per voice count. Plain C, no host code: the fm1-dock module glue calls it. */
#ifndef CPU_TEST_H
#define CPU_TEST_H
#include <stddef.h>
#include <stdint.h>
#include "../engine/fm1_8p.h"

#define CT_FS 44100u
#define CT_BLOCK 256u                   /* frames per fm1-dock render call */
#define CT_BLOCK_TICKS 139296u          /* one block at 44100 Hz in 24 MHz ticks (fm1-dock BLOCK_TICKS) */
/* The step after FM_MAX_VOICES: the unison burst ("U"). All voices in unison mode;
 * every block, before rendering, the previous keys go up, the 8 chord keys go down (each one
 * retriggers every voice: a 64/32 pitch divide per voice) and the detune changes (a divide per
 * voice). It counts as voice count CT_BURST everywhere a count goes. */
#define CT_BURST (FM_MAX_VOICES + 1u)
/* The step after U, the sweep's last: the full patch ("F"). A 6-voice chord (CT_FULL_VOICES, the target
 * polyphony: an overrun on the device stops the module, so F asks "do 6 voices fit") in poly mode with every
 * modulation on: a pulse and a saw mixed half and half, vibrato, pulse-width modulation, amp velocity
 * with a different velocity per note, the filter (a resonant lowpass swept by its envelope and the first
 * highpass step, never on its wide-open fast path, and swept into the fade at the top of its range) and the chorus (FULL_PATCH in cpu_test.c). With every
 * depth at 0 the LFO costs nothing and an open filter is a wire, so steps 1..8 and U never measure them;
 * F does. */
#define CT_FULL (CT_BURST + 1u)
#define CT_FULL_VOICES 6u              /* F's voices: the target polyphony on the device */
#define CT_LAST CT_FULL                 /* the sweep's last step: counts run 1..CT_LAST */

/* One param of a patch: fm_param_id and its value. */
typedef struct {
    uint16_t id;
    int32_t value;
} cpu_test_param;

typedef struct {
    fm_engine engine;
    int16_t buf[2u * CT_BLOCK];         /* engine output, static: not on the ISR stack */
    volatile uint8_t want;              /* main loop writes, ISR reads */
    volatile uint32_t asks;             /* main loop: +1 per request, after `want` */
    volatile uint8_t voices;            /* ISR writes, main loop reads */
    uint32_t seen;                      /* ISR only: the last `asks` it applied */
    volatile uint32_t hold;             /* sweep: blocks per voice count, 0 = off */
    volatile uint32_t held;             /* ISR writes (blocks at the current count), main loop reads */
    volatile uint32_t worst[CT_LAST + 1];   /* ISR writes, main loop reads; [voices] */
    uint8_t fresh;                      /* ISR only: this block started a count (setup: not timed) */
    volatile uint8_t done[CT_LAST];     /* ISR writes: counts whose hold completed, a ring */
    volatile uint32_t done_seq;         /* ISR: +1 per completed hold, after done[] */
    uint32_t taken;                     /* main loop only: completions already taken */
    uint8_t chord_full;                 /* counts 1..8 play the full patch too (the desktop bench) */
    const cpu_test_param *patch;        /* the full patch's params, patch_n of them */
    uint32_t patch_n;
    volatile uint32_t failed;           /* ISR writes, main loop reads: bit v, count v failed to start */
} cpu_test;

int cpu_test_init(cpu_test *t);
/* audio interrupt: add n (<= CT_BLOCK) stereo frames, Q15 in int32, into out */
void cpu_test_render(cpu_test *t, int32_t *out, uint32_t n);
/* main loop: ask for 1..FM_MAX_VOICES voices, CT_BURST for U or CT_FULL for F (clamped to 1..CT_LAST);
 * the next render applies it. U picked this way bursts until the next request. */
void cpu_test_request_voices(cpu_test *t, int32_t n);
uint8_t cpu_test_voices(const cpu_test *t);
/* ISR, after a render: that block took `ticks` (24 MHz) at the current voice count. The block that
 * started a count (engine init, note on) isn't counted, nor is any block of a count that failed to
 * start; starting a count clears its worst. */
void cpu_test_record(cpu_test *t, uint32_t ticks);
/* worst block so far at `voices` (1..CT_LAST), in per mille of the block's time (0: none, or out of
 * range) */
uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices);
/* main loop: step 1 -> FM_MAX_VOICES, then the unison burst (CT_BURST), then the full patch (CT_FULL),
 * by itself, `hold` blocks each (0: stop sweeping) */
void cpu_test_sweep(cpu_test *t, uint32_t hold);
/* main loop: 1 and the count and its worst per mille, once for each count a sweep held for its
 * full hold (counts picked by hand never complete); 0 when there is none new. A count that failed to
 * start completes too, with 0 per mille: check cpu_test_failed. */
int cpu_test_take_completed(cpu_test *t, uint32_t *voices, uint32_t *permille);
/* the test's notes for k voices (k <= FM_MAX_VOICES): distinct, so each voice plays one; returns k */
uint32_t cpu_test_chord(uint32_t k, uint8_t *notes);
/* Counts 1..FM_MAX_VOICES play the full patch (F's params and velocities, on their own chord) when on
 * is nonzero, from the next count started. For the desktop bench; the FM-1 module never sets it. */
void cpu_test_use_full_patch(cpu_test *t, int on);
/* Nonzero if count `voices` failed to start when it last started (its engine refused a param): it
 * plays whatever the engine was left with, and none of its blocks are timed. */
int cpu_test_failed(const cpu_test *t, uint32_t voices);
/* Replaces the full patch's params (step F, and counts 1..8 under cpu_test_use_full_patch) with
 * p[0..n), from the next count started; p NULL puts the built-in patch back. p must outlive its use. */
void cpu_test_set_full_patch(cpu_test *t, const cpu_test_param *p, uint32_t n);
/* the velocity step F strikes the chord's note i with (0 <= i < FM_MAX_VOICES): varied, so the amp
 * velocity curve does work */
uint8_t cpu_test_full_velocity(uint32_t i);
#endif
