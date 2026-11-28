/*
 * chorus.c - stereo chorus.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Per sample: one delay-line write, one triangle LFO step, two
 * linearly interpolated taps, one 64-bit multiply for the sweep, the wet
 * path (two gain multiplies and four 1-pole steps, each 32x32->64), one
 * clamp of the dry input and two dry-plus-wet adds. Divisions happen only
 * in fm_chorus_set_mode.
 *
 * Modes I and II are matched to PG-8X 2.0.13's chorus I and II (#104,
 * #312): dry plus wet, their rates, sweeps, centers and start phase, their
 * wet path (each tap 1.21 times the dry's level, through two 1-poles at
 * 4075 Hz; #312, #299), and a short delay, so the wet stays in phase
 * with the dry on a low note's bass, as on PG-8X.
 */
#include "chorus.h"
#include "units.h"

/* PG-8X 2.0.13's chorus wet path (#312, #299, measured 2026-11-20): each tap
 * 1.21 times the dry's level, through two 1-poles at 4075 Hz */
#define WET_GAIN_Q15 39649 /* 1.21 */
#define WET_HZ 4075u

#define MASK (FM_CHORUS_DELAY_LEN - 1)

typedef struct {
    uint32_t rate_mhz;  /* LFO rate, millihertz */
    uint32_t center_us; /* center delay, microseconds */
    uint32_t depth_us;  /* sweep either side of center, microseconds */
} mode_spec;

static const mode_spec modes[FM_CHORUS_MODE_COUNT] = {
    { 0, 0, 0 },         /* off */
    { 680, 2150, 1380 },  /* slow (I): PG-8X 2.0.13's chorus I rate and width (#312) */
    { 1020, 2140, 1360 }, /* deep (II): its chorus II, 1.5 times as fast (#312) */
    { 6000, 3000, 300 }, /* fast */
};

static void clear_line(fm_chorus *c)
{
    uint32_t i;
    for (i = 0; i < FM_CHORUS_DELAY_LEN; i++) c->line[i] = 0;
    for (i = 0; i < 4; i++) c->wet[i] = 0;
    c->pos = 0;
}

void fm_chorus_silence(fm_chorus *c)
{
    clear_line(c);
}

void fm_chorus_init(fm_chorus *c, uint32_t sample_rate)
{
    c->sample_rate = sample_rate;
    c->lfo_phase = 1u << 30; /* the triangle at 0, rising: PG-8X's start (#312) */
    c->lfo_inc = 0;
    c->center = 0;
    c->depth = 0;
    c->mode = FM_CHORUS_OFF; /* the line is emptied when a mode turns on */
    c->pos = 0;
}

/*
 * The wet path's 1-pole coefficient, Q16: k = 1 - e^-w, w = 2 pi WET_HZ / fs.
 * e^-w is taken as (e^(-w/4))^4: w/4 is at most 0.8 (8 kHz), where eight
 * terms of the series are within 1e-5. Each term is the last times x / n:
 * 32-bit, the products in 64 bits (FW review of #319: the series in w itself
 * overshot below 16 kHz).
 */
static int32_t wet_coefficient(uint32_t sample_rate)
{
    int32_t x = (int32_t)((411775u * WET_HZ) / sample_rate) / 4; /* 2 pi in Q16 times WET_HZ: under 2^31 */
    int32_t t = 65536, e = 65536;
    int n;
    for (n = 1; n <= 8; n++) {
        t = (int32_t)(((int64_t)t * x) >> 16) / n;
        e += (n & 1) ? -t : t;
    }
    e = (int32_t)(((int64_t)e * e) >> 16);
    e = (int32_t)(((int64_t)e * e) >> 16);
    return 65536 - e;
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
    c->wet_k = wet_coefficient(c->sample_rate);
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

/* One step of a 1-pole: *y toward x by k (Q16), Q8.24. */
static inline int32_t pole(int32_t *y, int32_t x, int32_t k)
{
    /* rounded away from 0, toward x: a step never stalls a unit short of x
     * (round-to-nearest left a -1 LSB tail after silence, FW review of
     * #319), and with k <= 1 it never passes x */
    int32_t d = x - *y;
    *y += (int32_t)(((int64_t)d * k + (65535 & ~(d >> 31))) >> 16);
    return *y;
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
 * +-2.0, and the wet path's 1.21 makes that +-2.42 (its 1-poles never
 * overshoot), so +-(2^31 - 2^26) leaves room. Only inputs beyond about
 * +-124.0 are touched, far above anything the mix reaches.
 */
#define DRY_MAX (INT32_MAX - (1 << 26))

/* One sample with the chorus on. */
static void process_on(fm_chorus *c, int32_t in, int32_t *left,
                       int32_t *right)
{
    int32_t q, sweep, dry, l, r;
    /* Q8.24 -> line units (1.0 = 16384), rounded, saturating at +-2.0 */
    q = (in >> 10) + ((in >> 9) & 1);
    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    c->pos = (uint16_t)((c->pos + 1u) & MASK);
    c->line[c->pos] = (int16_t)q;

    sweep = (int32_t)(((int64_t)c->depth * triangle(c->lfo_phase)) >> 30);
    c->lfo_phase += c->lfo_inc;

    dry = in > DRY_MAX ? DRY_MAX : in < -DRY_MAX ? -DRY_MAX : in;
    /* the wet path (#312, #299): each tap times 1.21, through two 1-poles */
    l = (int32_t)(((int64_t)tap(c, c->center + sweep) * WET_GAIN_Q15 + (1 << 14)) >> 15);
    r = (int32_t)(((int64_t)tap(c, c->center - sweep) * WET_GAIN_Q15 + (1 << 14)) >> 15);
    *left = dry + pole(&c->wet[1], pole(&c->wet[0], l, c->wet_k), c->wet_k);
    *right = dry + pole(&c->wet[3], pole(&c->wet[2], r, c->wet_k), c->wet_k);
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
