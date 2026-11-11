/*
 * env.c - ADSR envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Divisions happen only at stage changes and at the start of each straight
 * segment of an exponential stage (every 64 samples at most), never per
 * sample.
 */
#include "env.h"
#ifdef FM_CHECK_ENGINE
#include <assert.h>
#endif

/* Sets the per-sample step that covers `span` in `samples`: span / samples
 * in Q8 (a whole part and an 8-bit fraction, the fraction carried from
 * sample to sample), rounded up so the segment never takes longer than
 * asked, and a long segment at a small span still takes its time to
 * within a sample or so (a whole-number step rounded up could halve it).
 * `samples` == 0 jumps at once. */
static void set_step(fm_env *e, int32_t span, uint32_t samples)
{
    uint64_t q;
    e->frac = 0;
    if (samples == 0 || span <= 0) {
        e->step = span > 0 ? span : 1;
        e->step_frac = 0;
        return;
    }
    q = (((uint64_t)(uint32_t)span << 8) + samples - 1) / samples; /* never longer than asked */
    e->step = (int32_t)(q >> 8);
    e->step_frac = (uint8_t)q;
}

/* The longest straight segment of an exponential stage, and how many a
 * time constant gets at the least: a segment is at most tau / 8, so its
 * chord stays within about 0.2 % (0.02 dB) of the curve. */
#define SEG_MAX 64u
#define SEGS_PER_TAU 8u

/*
 * Starts the next straight segment of an exponential stage at distance `d`
 * (> 0) from its target, tau = `tau` samples: it ends on the curve,
 * d x e^(-n / tau) from the target, after n calls. 1 - e^(-x) for x = n /
 * tau <= 1/8 is x - x^2/2 + x^3/6, within x^4/24 (1.0e-5 of the step).
 */
/* floor(a / n) for a < 2^38 and 0 < n <= SEG_MAX, in 32-bit divisions (the
 * FM-1 would call a 64-bit division helper): a in 16-bit halves, each
 * remainder < n < 2^7 so every step fits. */
static uint64_t div_by_seg(uint64_t a, uint32_t n)
{
    uint32_t hi = (uint32_t)(a >> 16), lo = (uint32_t)a & 0xFFFFu;
    if (n == SEG_MAX) return a >> 6; /* the usual case: a long tau */
    return ((uint64_t)(hi / n) << 16) | (((hi % n) << 16 | lo) / n);
}

static void start_segment(fm_env *e, int32_t d, uint32_t tau)
{
    uint32_t n = tau / SEGS_PER_TAU, q24, r, x, x2, x3, one;
    uint64_t span_q8, q;
    if (n > SEG_MAX) n = SEG_MAX;
    if (n == 0) n = 1;
    /* x = floor(n * 2^30 / tau), Q30 <= 1: n * 2^24 < 2^31, then the
     * remainder (< tau < 2^25 for any tau the settings allow) times 2^6 */
    q24 = (n << 24) / tau;
    r = (n << 24) - q24 * tau;
    x = (q24 << 6) + (r << 6) / tau;
    x2 = (uint32_t)(((uint64_t)x * x) >> 30);
    x3 = (uint32_t)(((uint64_t)x2 * x) >> 30);
    one = x - (x2 >> 1) + x3 / 6;                    /* 1 - e^(-x), Q30 */
    /* the segment's span in Q8, so a long tau's few-LSB spans near the
     * floor keep their fraction; then per call, rounded up as set_step */
    span_q8 = ((uint64_t)(uint32_t)d * one) >> 22;
    q = div_by_seg(span_q8 + n - 1, n);
    e->frac = 0;
    e->step = (int32_t)(q >> 8);
    e->step_frac = (uint8_t)q;
    e->seg = (uint8_t)n;
}

void fm_env_init(fm_env *e, const fm_env_settings *set)
{
    e->set = set;
    fm_env_reset(e);
}

void fm_env_reset(fm_env *e)
{
    e->level = 0;
    e->step = 0;
    e->step_frac = 0;
    e->frac = 0;
    e->seg = 0;
    e->stage = FM_ENV_IDLE;
    e->peak = FM_ENV_FULL;
    e->sus_level = 0;
}

/* The sustain level for this note: the setting scaled by the peak
 * (exact when the peak is full). Kept so the decay needs no multiply. */
static int32_t sustain_level(fm_env *e)
{
    e->sus_level = (int32_t)(((int64_t)e->set->sustain * e->peak) >> 30);
    return e->sus_level;
}

void fm_env_gate_on(fm_env *e)
{
#ifdef FM_CHECK_ENGINE /* debug builds: started with fm_env_init */
    assert(e->set);
#endif
    e->stage = FM_ENV_ATTACK;
    set_step(e, e->peak, e->set->attack_samples);
    sustain_level(e);
}

void fm_env_gate_off(fm_env *e)
{
    if (e->stage == FM_ENV_IDLE) return;
    e->stage = FM_ENV_RELEASE;
    e->seg = 0;
}

void fm_env_fade(fm_env *e, uint32_t samples)
{
    if (e->stage == FM_ENV_IDLE) return;
    e->stage = FM_ENV_FADE;
    set_step(e, e->level, samples);
}

/* Into the DECAY stage: exponential from the current level to sustain, in
 * either direction, the decay time its tau; the next call starts its first
 * segment. */
static void enter_decay(fm_env *e)
{
    e->stage = FM_ENV_DECAY;
    e->seg = 0;
}

void fm_env_retime(fm_env *e)
{
    int32_t old_sus = e->sus_level;
    sustain_level(e);
    switch (e->stage) {
    case FM_ENV_ATTACK:
        set_step(e, e->peak, e->set->attack_samples);
        break;
    case FM_ENV_DECAY:   /* a new tau: the running segment ends as it is (at
                          * most 64 samples), so setting a value again changes
                          * nothing. A new sustain, or a time of 0, acts at
                          * once: the segment was drawn toward the old one */
        if (e->set->decay_samples == 0 || e->sus_level != old_sus) e->seg = 0;
        break;
    case FM_ENV_RELEASE:
        if (e->set->release_samples == 0) e->seg = 0;
        break;
    case FM_ENV_SUSTAIN:
        if (e->level != e->sus_level) enter_decay(e);
        break;
    default:
        break;
    }
}

int32_t fm_env_next(fm_env *e)
{
    int32_t s;
    switch (e->stage) {
    case FM_ENV_ATTACK:
        /* all comparisons subtract: levels and steps are 0..2^30, so a
         * difference cannot overflow where a sum could. A retrigger above
         * a softer note's peak goes straight to the decay, from where it
         * is, so the level never jumps. */
        if (e->peak - e->level <= fm_env_reach(e)) {
            if (e->level < e->peak) e->level = e->peak;
            enter_decay(e);
        } else {
            e->level += fm_env_inc(e);
        }
        break;
    case FM_ENV_DECAY:
        s = e->sus_level;
        if (e->seg == 0) { /* a segment starts, or the decay lands */
            int32_t d = e->level >= s ? e->level - s : s - e->level;
            if (d <= FM_ENV_FLOOR || e->set->decay_samples == 0) {
                e->level = s;
                e->stage = FM_ENV_SUSTAIN;
                break;
            }
            start_segment(e, d, e->set->decay_samples);
        }
        e->seg--;
        if (e->level >= s) {
            if (e->level - fm_env_reach(e) <= s) {
                e->level = s;
                e->stage = FM_ENV_SUSTAIN;
            } else {
                e->level -= fm_env_inc(e);
            }
        } else if (s - e->level <= fm_env_reach(e)) {
            e->level = s;
            e->stage = FM_ENV_SUSTAIN;
        } else {
            e->level += fm_env_inc(e);
        }
        break;
    case FM_ENV_SUSTAIN: /* holds; fm_env_retime starts a glide on change */
        break;
    case FM_ENV_RELEASE:
        if (e->seg == 0) {
            if (e->level <= FM_ENV_FLOOR || e->set->release_samples == 0) {
                e->level = 0;
                e->stage = FM_ENV_IDLE;
                break;
            }
            start_segment(e, e->level, e->set->release_samples);
        }
        e->seg--;
        if (e->level <= fm_env_reach(e)) {
            e->level = 0;
            e->stage = FM_ENV_IDLE;
        } else {
            e->level -= fm_env_inc(e);
        }
        break;
    case FM_ENV_FADE:
        if (e->level <= fm_env_reach(e)) {
            e->level = 0;
            e->stage = FM_ENV_IDLE;
        } else {
            e->level -= fm_env_inc(e);
        }
        break;
    default:
        e->level = 0;
        break;
    }
    return e->level;
}

/* The step as one Q8 number: step + step_frac / 256. */
static uint64_t q8_step(const fm_env *e)
{
    return ((uint64_t)(uint32_t)e->step << 8) | e->step_frac;
}

/*
 * Of the next `n` calls (n >= 1) of a stepping stage at distance `d` from
 * its target, how many go by before the one that lands, or `n` when none
 * of them lands. A call lands when the distance left is at most `reach`,
 * and the first j calls move (frac + j x step) >> 8 in Q8, so that is
 * the first j with frac + j x step >= (d - reach) << 8. Without
 * a division when the n calls clearly do not land (the usual case).
 */
static uint32_t calls_before_landing(const fm_env *e, int32_t d, uint32_t n)
{
    int32_t t = d - fm_env_reach(e);
    uint64_t q = q8_step(e), need, c;
    if (t <= 0) return 0;
    need = (uint64_t)(uint32_t)t << 8;
    if (n <= 65536u && e->frac + (uint64_t)(n - 1) * q < need) return n;
    c = (need - e->frac + q - 1) / q;
    return c < n ? (uint32_t)c : n;
}

/* Moves of `k` stepping calls that do not land: their sum, carrying the
 * fraction as k calls of fm_env_inc would. */
static int32_t take(fm_env *e, uint32_t k)
{
    uint64_t a = e->frac + (uint64_t)k * q8_step(e);
    e->frac = (uint8_t)a;
    return (int32_t)(a >> 8);
}

int32_t fm_env_advance(fm_env *e, uint32_t n)
{
    while (n > 0) {
        uint32_t c, m;
        int32_t d;
        switch (e->stage) {
        case FM_ENV_ATTACK:
            c = calls_before_landing(e, e->peak - e->level, n);
            if (c >= n) {
                e->level += take(e, n);
                return e->level;
            }
            e->level += take(e, c);
            /* a retrigger above a softer peak keeps its level */
            if (e->level < e->peak) e->level = e->peak;
            enter_decay(e);
            n -= c + 1;
            break;
        case FM_ENV_DECAY:
            if (e->seg == 0) { /* a segment starts (or the decay lands) */
                fm_env_next(e);
                n--;
                break;
            }
            m = n < e->seg ? n : e->seg;
            d = e->level >= e->sus_level ? e->level - e->sus_level
                                         : e->sus_level - e->level;
            c = calls_before_landing(e, d, m);
            if (c >= m) { /* m calls inside the segment, none lands */
                int32_t move = take(e, m);
                e->level += e->level >= e->sus_level ? -move : move;
                e->seg = (uint8_t)(e->seg - m);
                n -= m;
                break;
            }
            e->level = e->sus_level;
            e->stage = FM_ENV_SUSTAIN;
            return e->level; /* sustain holds */
        case FM_ENV_SUSTAIN:
            return e->level;
        case FM_ENV_RELEASE:
            if (e->seg == 0) { /* a segment starts (or the release ends) */
                fm_env_next(e);
                n--;
                break;
            }
            m = n < e->seg ? n : e->seg;
            c = calls_before_landing(e, e->level, m);
            if (c >= m) { /* m calls inside the segment, none lands */
                e->level -= take(e, m);
                e->seg = (uint8_t)(e->seg - m);
                n -= m;
                break;
            }
            e->level = 0;
            e->stage = FM_ENV_IDLE;
            return 0;
        case FM_ENV_FADE:
            c = calls_before_landing(e, e->level, n);
            if (c >= n) {
                e->level -= take(e, n);
                return e->level;
            }
            e->level = 0;
            e->stage = FM_ENV_IDLE;
            return 0;
        default:
            e->level = 0;
            return 0;
        }
    }
    return e->level;
}
