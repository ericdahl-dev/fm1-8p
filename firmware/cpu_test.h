/* fm1-8p CPU test: drives the unchanged engine with a fixed note pattern and keeps the worst
 * render time per voice count. Plain C, no host code: the fm1-dock module glue calls it. */
#ifndef CPU_TEST_H
#define CPU_TEST_H
#include <stdint.h>
#include "../engine/fm1_8p.h"

#define CT_FS 44100u
#define CT_BLOCK 256u                   /* frames per fm1-dock render call */
#define CT_BLOCK_TICKS 139296u          /* one block at 44100 Hz in 24 MHz ticks (fm1-dock BLOCK_TICKS) */

typedef struct {
    fm_engine engine;
    int16_t buf[2u * CT_BLOCK];         /* engine output, static: not on the ISR stack */
    volatile uint8_t want;              /* main loop writes, ISR reads */
    volatile uint32_t asks;             /* main loop: +1 per request, after `want` */
    volatile uint8_t voices;            /* ISR writes, main loop reads */
    uint32_t seen;                      /* ISR only: the last `asks` it applied */
    volatile uint32_t hold;             /* sweep: blocks per voice count, 0 = off */
    volatile uint32_t held;             /* ISR writes (blocks at the current count), main loop reads */
    volatile uint32_t worst[FM_MAX_VOICES + 1];   /* ISR writes, main loop reads; [voices] */
} cpu_test;

int cpu_test_init(cpu_test *t);
/* audio interrupt: add n (<= CT_BLOCK) stereo frames, Q15 in int32, into out */
void cpu_test_render(cpu_test *t, int32_t *out, uint32_t n);
/* main loop: ask for 1..FM_MAX_VOICES voices (clamped); the next render applies it */
void cpu_test_request_voices(cpu_test *t, int32_t n);
uint8_t cpu_test_voices(const cpu_test *t);
/* ISR, after a render: that block took `ticks` (24 MHz) at the current voice count */
void cpu_test_record(cpu_test *t, uint32_t ticks);
/* worst block so far at `voices`, in per mille of the block's time (0: none, or out of range) */
uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices);
/* main loop: step 1 -> FM_MAX_VOICES by itself, `hold` blocks each (0: stop sweeping) */
void cpu_test_sweep(cpu_test *t, uint32_t hold);
/* 1 once a sweep has held the last voice count for its full hold, else 0. */
int cpu_test_finished(const cpu_test *t);
/* the test's notes for k voices (k <= FM_MAX_VOICES): distinct, so each voice plays one; returns k */
uint32_t cpu_test_chord(uint32_t k, uint8_t *notes);
#endif
