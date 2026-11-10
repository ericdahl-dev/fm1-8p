/*
 * lfo.c - low-frequency oscillator.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 */
#include "lfo.h"

#define SEED 0x9E3779B9u

static uint32_t xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

void fm_lfo_reset(fm_lfo *l)
{
    l->phase = 0;
    l->inc = 0;
    l->rand = SEED;
    l->held = 0;
    l->wave = FM_LFO_TRIANGLE;
}

void fm_lfo_set_wave(fm_lfo *l, uint8_t wave)
{
    if (wave < FM_LFO_WAVE_COUNT) l->wave = wave;
}

void fm_lfo_set_rate(fm_lfo *l, int32_t centihz, uint32_t sample_rate)
{
    uint64_t inc;
    if (sample_rate == 0) { /* never divide by zero: no trap on pi32v2 */
        l->inc = 0;
        return;
    }
    if (centihz < 0) centihz = 0;
    if (centihz > FM_LFO_MAX_CENTIHZ) centihz = FM_LFO_MAX_CENTIHZ;
    /* cycles per tick = centihz / 100 * TICK_FRAMES / rate, as Q0.32;
     * at most 3000 << 37, inside 64 bits */
    inc = ((uint64_t)(uint32_t)centihz << (32 + FM_LFO_TICK_SHIFT)) /
          ((uint64_t)sample_rate * 100u);
    l->inc = inc > 0x7FFFFFFFu ? 0x7FFFFFFFu : (uint32_t)inc;
}

int32_t fm_lfo_tick(fm_lfo *l)
{
    uint32_t p = l->phase, q;
    int32_t v;
    switch (l->wave) {
    case FM_LFO_SAW: /* 0 at the start, up to +1, jump to -1, up to 0 */
        v = (int32_t)p >> 1;
        break;
    case FM_LFO_SQUARE:
        v = p < 0x80000000u ? FM_LFO_FULL : -FM_LFO_FULL;
        break;
    case FM_LFO_RANDOM:
        v = l->held;
        break;
    default: /* triangle: 0 at the start, rising */
        q = p + 0x40000000u;
        if (q & 0x80000000u) q = ~q;  /* fold: 0..2^31 and back */
        v = (int32_t)q - FM_LFO_FULL; /* -1.0..+1.0 */
        break;
    }
    l->phase = p + l->inc;
    if (l->phase < p) { /* wrapped: a new cycle */
        l->rand = xorshift32(l->rand);
        l->held = (int32_t)l->rand >> 1;
    }
    return v;
}
