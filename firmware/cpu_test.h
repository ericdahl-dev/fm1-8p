/* fm1-8p CPU test: drives the unchanged engine with a fixed note pattern and keeps the worst
 * render time per voice count. Plain C, no host code: the fm1-dock module glue calls it. */
#ifndef CPU_TEST_H
#define CPU_TEST_H
#include <stdint.h>
#include "../engine/fm1_8p.h"

#define CT_FS 44100u
#define CT_BLOCK 256u                   /* frames per fm1-dock render call */
#define CT_BLOCK_TICKS 139296u          /* one block at 44100 Hz in 24 MHz ticks (fm1-dock BLOCK_TICKS) */
/* The sweep's last step, after FM_MAX_VOICES: the unison burst ("U"). All voices in unison mode;
 * every block, before rendering, the previous keys go up, the 8 chord keys go down (each one
 * retriggers every voice: a 64/32 pitch divide per voice) and the detune changes (a divide per
 * voice). It counts as voice count CT_BURST everywhere a count goes. */
#define CT_BURST (FM_MAX_VOICES + 1u)

typedef struct {
    fm_engine engine;
    int16_t buf[2u * CT_BLOCK];         /* engine output, static: not on the ISR stack */
    volatile uint8_t want;              /* main loop writes, ISR reads */
    volatile uint32_t asks;             /* main loop: +1 per request, after `want` */
    volatile uint8_t voices;            /* ISR writes, main loop reads */
    uint32_t seen;                      /* ISR only: the last `asks` it applied */
    volatile uint32_t hold;             /* sweep: blocks per voice count, 0 = off */
    volatile uint32_t held;             /* ISR writes (blocks at the current count), main loop reads */
    volatile uint32_t worst[CT_BURST + 1];   /* ISR writes, main loop reads; [voices] */
    uint8_t fresh;                      /* ISR only: this block started a count (setup: not timed) */
    volatile uint8_t done[CT_BURST];    /* ISR writes: counts whose hold completed, a ring */
    volatile uint32_t done_seq;         /* ISR: +1 per completed hold, after done[] */
    uint32_t taken;                     /* main loop only: completions already taken */
} cpu_test;

int cpu_test_init(cpu_test *t);
/* audio interrupt: add n (<= CT_BLOCK) stereo frames, Q15 in int32, into out */
void cpu_test_render(cpu_test *t, int32_t *out, uint32_t n);
/* main loop: ask for 1..FM_MAX_VOICES voices, or CT_BURST for U (clamped to 1..CT_BURST); the next
 * render applies it. U picked this way bursts until the next request. */
void cpu_test_request_voices(cpu_test *t, int32_t n);
uint8_t cpu_test_voices(const cpu_test *t);
/* ISR, after a render: that block took `ticks` (24 MHz) at the current voice count. The block that
 * started a count (engine init, note on) isn't counted; starting a count clears its worst. */
void cpu_test_record(cpu_test *t, uint32_t ticks);
/* worst block so far at `voices` (1..CT_BURST), in per mille of the block's time (0: none, or out of
 * range) */
uint32_t cpu_test_worst_permille(const cpu_test *t, uint32_t voices);
/* main loop: step 1 -> FM_MAX_VOICES, then the unison burst (CT_BURST), by itself, `hold` blocks
 * each (0: stop sweeping) */
void cpu_test_sweep(cpu_test *t, uint32_t hold);
/* main loop: 1 and the count and its worst per mille, once for each count a sweep held for its
 * full hold (counts picked by hand never complete); 0 when there is none new */
int cpu_test_take_completed(cpu_test *t, uint32_t *voices, uint32_t *permille);
/* the test's notes for k voices (k <= FM_MAX_VOICES): distinct, so each voice plays one; returns k */
uint32_t cpu_test_chord(uint32_t k, uint8_t *notes);
#endif
