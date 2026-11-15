/*
 * dcblock.h - the output's DC block (#265 B): PG-8X 2.0.13 rolls off
 * below a few hertz (two first-order highpasses at FM_DCBLOCK_HZ, fitted
 * to its HPF 1 within 1.24 dB rms from 0.5 to 32 Hz), so a DC offset the
 * voices make (a ring's peaks bent by a clamp, envelopes moving it) never
 * reaches its output. Internal to the engine; fm1_8p.h includes it only to
 * size fm_engine.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#ifndef FM_DCBLOCK_H
#define FM_DCBLOCK_H

#include <stdint.h>

#define FM_DCBLOCK_HZ_X10 55 /* 5.5 Hz */

typedef struct {
    int64_t lp[2]; /* each stage's one-pole lowpass, Q8.40 (Q8.24 with 16 more fraction bits) */
    int32_t c;     /* the lowpasses' coefficient, Q1.30: 1 - e^(-2 pi f / rate) */
} fm_dcblock;

/* Sets the corner for `sample_rate` and empties the state. */
void fm_dcblock_init(fm_dcblock *b, uint32_t sample_rate);
/* Empties the state (silence). */
void fm_dcblock_clear(fm_dcblock *b);
/* One sample, Q8.24 in and out (|x| within 64.0). */
int32_t fm_dcblock_process(fm_dcblock *b, int32_t x);

#endif /* FM_DCBLOCK_H */
