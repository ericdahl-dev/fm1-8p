/*
 * lfo.h - low-frequency oscillator (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Runs at the control rate: one tick every FM_LFO_TICK_FRAMES samples,
 * not per sample. Its value is Q1.30, -1.0..+1.0 (see docs/modulation.md).
 */
#ifndef FM_LFO_H
#define FM_LFO_H

#include <stdint.h>

/* Samples per control tick (a power of two). */
#define FM_LFO_TICK_SHIFT 5
#define FM_LFO_TICK_FRAMES (1u << FM_LFO_TICK_SHIFT)

#define FM_LFO_FULL (1 << 30)

/* LFO waveforms. Stable: the patch format stores them. */
typedef enum {
    FM_LFO_TRIANGLE = 0,
    FM_LFO_SAW,    /* rising ramp */
    FM_LFO_SQUARE,
    FM_LFO_RANDOM, /* sample and hold: a new random level each cycle */
    FM_LFO_WAVE_COUNT
} fm_lfo_wave;

typedef struct {
    uint32_t phase; /* Q0.32 of a cycle */
    uint32_t inc;   /* Q0.32 per tick */
    uint32_t rand;  /* xorshift32 state, never 0 */
    int32_t held;   /* sample and hold level, Q1.30 */
    uint8_t wave;   /* fm_lfo_wave */
} fm_lfo;

void fm_lfo_reset(fm_lfo *l);
/* Unknown waveforms are ignored. */
void fm_lfo_set_wave(fm_lfo *l, uint8_t wave);
/* Rate in hundredths of a hertz (clamped to 0..FM_LFO_MAX_CENTIHZ) at the
 * engine's sample rate; one 64-by-32 divide, on events only. */
#define FM_LFO_MAX_CENTIHZ 3000
void fm_lfo_set_rate(fm_lfo *l, int32_t centihz, uint32_t sample_rate);
/* Value for this tick (Q1.30), then advances one tick. */
int32_t fm_lfo_tick(fm_lfo *l);

#endif /* FM_LFO_H */
