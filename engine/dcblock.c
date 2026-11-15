/*
 * dcblock.c - the output's DC block (#265 B): two first-order highpasses
 * at FM_DCBLOCK_HZ_X10 / 10 Hz, as PG-8X 2.0.13's output rolls off (see
 * dcblock.h).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Each stage is x less a one-pole lowpass of x, lp += c (x - lp), its
 * state kept 16 bits finer than the signal (Q8.40) so a slow lowpass
 * doesn't stall short of the signal. Per sample: two 32 x 32 -> 64
 * multiplies, no divide. A constant input settles to exactly 0.
 */
#include "dcblock.h"

void fm_dcblock_clear(fm_dcblock *b)
{
    b->lp[0] = b->lp[1] = 0;
}

void fm_dcblock_init(fm_dcblock *b, uint32_t sample_rate)
{
    /* x = 2 pi f / rate in Q30: 2 pi f 2^30 / 256 over the rate, times
     * 256, a 32-bit divide (on init); then c = 1 - e^-x = x - x^2 / 2 to
     * well under a part per million at any rate the engine takes */
    uint32_t x;
    fm_dcblock_clear(b);
    if (!sample_rate) {
        b->c = 0;
        return;
    }
    /* 2 pi x 0.1 Hz x 2^30 / 256 = 2635359; times FM_DCBLOCK_HZ_X10 */
    x = (2635359u * FM_DCBLOCK_HZ_X10 / sample_rate) * 256u +
        ((2635359u * FM_DCBLOCK_HZ_X10 % sample_rate) * 256u) / sample_rate;
    b->c = (int32_t)(x - (uint32_t)(((uint64_t)x * x) >> 31));
}

int32_t fm_dcblock_process(fm_dcblock *b, int32_t x)
{
    int i;
    for (i = 0; i < 2; i++) {
        int32_t d = x - (int32_t)(b->lp[i] >> 16); /* the stage's output */
        b->lp[i] += ((int64_t)d * b->c) >> 14;     /* Q8.24 x Q1.30 -> Q8.40 */
        x = d;
    }
    return x;
}
