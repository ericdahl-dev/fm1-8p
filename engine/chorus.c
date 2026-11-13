/*
 * chorus.c - stereo chorus.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Per sample: one delay-line write, one triangle LFO step, two
 * linearly interpolated taps, one 64-bit multiply, one clamp of the dry
 * input and two dry-plus-wet adds. Divisions happen only in fm_chorus_set_mode.
 *
 * Modes I and II are matched to PG-8X 2.0.13's chorus (#104): dry plus
 * wet, 0.68 Hz, L/R correlation about 0.46, and a short delay, so the wet
 * stays in phase with the dry on a low note's bass, as on PG-8X.
 */
#include "chorus.h"
#include "units.h"

#define MASK (FM_CHORUS_DELAY_LEN - 1)

typedef struct {
    uint32_t rate_mhz;  /* LFO rate, millihertz */
    uint32_t center_us; /* center delay, microseconds */
    uint32_t depth_us;  /* sweep either side of center, microseconds */
} mode_spec;

static const mode_spec modes[FM_CHORUS_MODE_COUNT] = {
    { 0, 0, 0 },         /* off */
    { 680, 2000, 1600 }, /* slow (I): PG-8X's rate, delay and width */
    { 680, 2100, 2000 }, /* deep (II): the same rate, swept deeper */
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
    c->lfo_inc = fm_rate_to_phase_inc(m->rate_mhz, 1000u, c->sample_rate, 0);
    center = fm_us_to_samples_q16(m->center_us, c->sample_rate);
    depth = fm_us_to_samples_q16(m->depth_us, c->sample_rate);
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

/*
 * The dry part, limited so dry + wet fits in int32: a tap is within
 * +-2.0 (+-2^25), so +-(2^31 - 2^25) leaves room. Only inputs beyond
 * about +-126.0 are touched, far above anything the mix reaches.
 */
#define DRY_MAX (INT32_MAX - (1 << 25))

/* One sample with the chorus on. */
static void process_on(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right)
{
    int32_t q, sweep, dry;
    /* Q8.24 -> line units (1.0 = 16384), rounded, saturating at +-2.0 */
    q = (in >> 10) + ((in >> 9) & 1);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    c->pos = (uint16_t)((c->pos + 1u) & MASK);
    c->line[c->pos] = (int16_t)q;

    sweep = (int32_t)(((int64_t)c->depth * triangle(c->lfo_phase)) >> 30);
    c->lfo_phase += c->lfo_inc;

    dry = in > DRY_MAX ? DRY_MAX : in < -DRY_MAX ? -DRY_MAX : in;
    *left = dry + tap(c, c->center + sweep);
    *right = dry + tap(c, c->center - sweep);
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
