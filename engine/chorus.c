/*
 * chorus.c - stereo chorus.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Per sample: one delay-line write, one triangle LFO step, two
 * linearly interpolated taps, one 64-bit multiply. Divisions happen only
 * in fm_chorus_set_mode.
 */
#include "chorus.h"

#define MASK (FM_CHORUS_DELAY_LEN - 1)

typedef struct {
    uint32_t rate_mhz;  /* LFO rate, millihertz */
    uint32_t center_us; /* center delay, microseconds */
    uint32_t depth_us;  /* sweep either side of center, microseconds */
} mode_spec;

static const mode_spec modes[FM_CHORUS_MODE_COUNT] = {
    { 0, 0, 0 },         /* off */
    { 500, 5000, 1500 }, /* slow */
    { 800, 5000, 2500 }, /* deep */
    { 6000, 3000, 300 }, /* fast */
};

static void clear_line(fm_chorus *c)
{
    uint32_t i;
    for (i = 0; i < FM_CHORUS_DELAY_LEN; i++) c->line[i] = 0;
    c->pos = 0;
}

void fm_chorus_silence(fm_chorus *c)
{
    clear_line(c);
}

void fm_chorus_init(fm_chorus *c, uint32_t sample_rate)
{
    c->sample_rate = sample_rate;
    c->lfo_phase = 0;
    c->lfo_inc = 0;
    c->center = 0;
    c->depth = 0;
    c->mode = FM_CHORUS_OFF; /* the line is emptied when a mode turns on */
    c->pos = 0;
}

/* Microseconds to samples, Q16.16 (fits: 10 ms * 192 kHz * 2^16 < 2^37). */
static uint64_t us_to_q16(uint32_t us, uint32_t rate)
{
    return ((uint64_t)us * rate << 16) / 1000000u;
}

void fm_chorus_set_mode(fm_chorus *c, uint8_t mode)
{
    const mode_spec *m;
    uint64_t center, depth;
    /* the newest tap needs 1 sample, the oldest 2 below the line length */
    const uint64_t longest = (uint64_t)(FM_CHORUS_DELAY_LEN - 2) << 16;
    if (mode >= FM_CHORUS_MODE_COUNT || c->sample_rate == 0)
        mode = FM_CHORUS_OFF;
    if (mode != FM_CHORUS_OFF && c->mode == FM_CHORUS_OFF) clear_line(c);
    c->mode = mode;
    if (mode == FM_CHORUS_OFF) return;
    m = &modes[mode];
    c->lfo_inc = (uint32_t)(((uint64_t)m->rate_mhz << 32) /
                            ((uint64_t)c->sample_rate * 1000u));
    center = us_to_q16(m->center_us, c->sample_rate);
    depth = us_to_q16(m->depth_us, c->sample_rate);
    if (center + depth > longest) { /* very high rates: scale to fit */
        uint64_t total = center + depth;
        center = center * longest / total;
        depth = depth * longest / total;
    }
    if (center < depth + (1u << 16)) /* very low rates: keep >= 1 sample */
        center = depth + (1u << 16);
    c->center = (int32_t)center;
    c->depth = (int32_t)depth;
}

/* Triangle from the LFO phase, Q1.30, -1.0 .. just under +1.0. */
static int32_t triangle(uint32_t phase)
{
    uint32_t x = phase < 0x80000000u ? phase : ~phase; /* 0 .. 2^31 - 1 */
    return (int32_t)x - (1 << 30);
}

/* Delay-line value `delay` (Q16.16, >= 1.0) samples back, as Q8.24. */
static int32_t tap(const fm_chorus *c, int32_t delay)
{
    uint32_t whole = (uint32_t)delay >> 16;
    int32_t frac = (int32_t)(((uint32_t)delay & 0xFFFFu) >> 1); /* Q0.15 */
    int32_t s0 = c->line[(c->pos - whole) & MASK];
    int32_t s1 = c->line[(c->pos - whole - 1u) & MASK];
    /* |s0 * 2^15| <= 2^30 and |(s1 - s0) * frac| < 2^31 */
    int32_t v = s0 * 32768 + (s1 - s0) * frac;
    /* line units are 2^10 in Q8.24 and v has 15 more bits: drop 5, rounded */
    return (v + 16) >> 5;
}

/* (a + b) / 2, rounded once (halves up), with no overflow for any int32. */
static int32_t half_sum(int32_t a, int32_t b)
{
    return (a >> 1) + (b >> 1) + (((a & 1) + (b & 1) + 1) >> 1);
}

/* One sample with the chorus on. */
static void process_on(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right)
{
    int32_t q, sweep;
    /* Q8.24 -> line units (1.0 = 16384), rounded, saturating at +-2.0 */
    q = (in >> 10) + ((in >> 9) & 1);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    c->pos = (uint16_t)((c->pos + 1u) & MASK);
    c->line[c->pos] = (int16_t)q;

    sweep = (int32_t)(((int64_t)c->depth * triangle(c->lfo_phase)) >> 30);
    c->lfo_phase += c->lfo_inc;

    *left = half_sum(in, tap(c, c->center + sweep));
    *right = half_sum(in, tap(c, c->center - sweep));
}

void fm_chorus_process(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right)
{
    if (c->mode == FM_CHORUS_OFF) {
        *left = in;
        *right = in;
        return;
    }
    process_on(c, in, left, right);
}

void fm_chorus_process_block(fm_chorus *c, int32_t *left, int32_t *right,
                             uint32_t frames)
{
    uint32_t i;
    if (c->mode == FM_CHORUS_OFF) {
        for (i = 0; i < frames; i++) right[i] = left[i];
        return;
    }
    for (i = 0; i < frames; i++) process_on(c, left[i], &left[i], &right[i]);
}
