/*
 * env.c - ADSR envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Divisions happen only at stage changes and at the start of each straight
 * segment (every 64 samples at most), never per sample. Every moving stage
 * is a run of segments with a whole-number step: a sample inside one is the
 * count and an add, nothing else (fm_env_step).
 */
#include "env.h"
#ifdef FM_CHECK_ENGINE
#include <assert.h>
#endif

/* The longest straight segment of an exponential stage, and how many a
 * time constant gets at the least: a segment is at most tau / 8, so its
 * chord stays within about 0.2 % (0.02 dB) of the curve. */
#define SEG_MAX 64u
#define SEGS_PER_TAU 8u

/* A stage that holds (sustain, idle): no change for the longest count, so
 * the inline step passes it by without a call. */
#define HOLD 255u

static void hold(fm_env *e)
{
    e->step = 0;
    e->seg = (uint8_t)HOLD;
}

/*
 * Starts the next straight segment of an exponential stage at distance `d`
 * (> 0) from its target, tau = `tau` samples: it ends on the curve,
 * d x e^(-n / tau) from the target, after n calls. 1 - e^(-x) for x = n /
 * tau <= 1/8 is x - x^2/2 + x^3/6, within x^4/24 (1.0e-5 of the step).
 * Sets the step (toward the target, > 0 here) and the count, and returns
 * the remainder: n x step + remainder is the span, and the remainder
 * (< n <= 64 LSB) is moved with the first call, so the segment ends on the
 * curve exactly.
 */
static int32_t start_segment(fm_env *e, int32_t d, uint32_t tau)
{
    uint32_t n = tau / SEGS_PER_TAU, q24, r, x, x2, x3, one, span, q;
    uint64_t span_q8;
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
    /* the span in Q8, with the fraction the last segment left over, so a
     * long tau's few-LSB spans near the floor keep their rate */
    span_q8 = (((uint64_t)(uint32_t)d * one) >> 22) + e->frac;
    e->frac = (uint8_t)span_q8;
    span = (uint32_t)(span_q8 >> 8);
    q = n == SEG_MAX ? span >> 6 : span / n;          /* the usual case: a long tau */
    e->step = (int32_t)q;
    e->seg = (uint8_t)n;
    return (int32_t)(span - q * n);
}

/* A time in samples at this note's scale: untouched at 1.0. Out of line:
 * once per segment, and three copies cost code. */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static uint32_t scaled(const fm_env *e, uint32_t t)
{
    uint64_t s;
    if (e->ks == 1u << 16) return t;
    s = ((uint64_t)t * e->ks) >> 16;
    return s > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)s;
}

/*
 * The attack charges like PG-8X's, an RC toward 1.6 x the peak, and stops on
 * the peak, which takes tau x ln(1.6 / 0.6) = 0.98 tau: so tau is the attack
 * time x 1.0195 (66817 / 65536). Starts the next segment toward that target,
 * cut short where it would reach the peak, and moves its remainder. Returns 0
 * when this call lands instead (on the peak, or a retrigger already above it,
 * or no attack time).
 */
static int attack_segment(fm_env *e)
{
    int32_t d = e->peak - e->level, rem;
    uint32_t t = scaled(e, e->set->attack_samples), k;
    if (d <= 0 || t == 0) return 0;
    rem = start_segment(e, d + e->peak / 5 * 3,
                        (uint32_t)(((uint64_t)t * 66817u) >> 16));
    /* the calls that stay below the peak: rem + k x step < d */
    if (rem + (int32_t)e->seg * e->step >= d) {
        if (e->step == 0 || rem >= d) return 0;
        k = (uint32_t)(d - 1 - rem) / (uint32_t)e->step;
        if (k == 0) return 0;
        e->seg = (uint8_t)k;
    }
    e->level += rem;
    return 1;
}

void fm_env_init(fm_env *e, const fm_env_settings *set)
{
    e->set = set;
    e->ks = 1u << 16;
    fm_env_reset(e);
}

void fm_env_reset(fm_env *e)
{
    e->level = 0;
    e->step = 0;
    e->seg = 0;
    e->frac = 0;
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

/* Envelope key follow: C2 (MIDI 36) keeps the set times (PG-8X 2.0.13's
 * ENV KEY; the time halves every 2 octaves up at 500 per mille). */
#define KEY_CENTER 36

/* 2^(i / 16) - 1, Q16, i = 0..15: 2^(cents / 1200) in 75-cent steps */
static const uint16_t exp2_16th[16] = {
    0, 2902, 5932, 9096, 12400, 15850, 19454, 23216,
    27146, 31249, 35534, 40009, 44682, 49562, 54658, 59979
};

/* 2^(cents / 1200), Q16.16, from 1/256 to 256 (clamped): linear between
 * the table's steps, within 0.03 %. 32-bit; on note on only. */
static uint32_t exp2_cents(int32_t cents)
{
    int32_t k, f, i, a, b;
    uint32_t v;
    if (cents < -8 * 1200) cents = -8 * 1200;
    if (cents > 8 * 1200) cents = 8 * 1200;
    k = (cents + 8 * 1200) / 1200 - 8; /* floor, cents >= -9600 */
    f = cents - k * 1200;              /* 0..1199 */
    i = f / 75;
    a = exp2_16th[i];
    b = i == 15 ? 65536 : exp2_16th[i + 1];
    v = (uint32_t)(65536 + a + (b - a) * (f - i * 75) / 75);
    return k >= 0 ? v << k : v >> -k;
}

void fm_env_key(fm_env *e, uint8_t note)
{
    int32_t f = e->set->key_follow;
    e->ks = f ? exp2_cents(-f * ((int32_t)note - KEY_CENTER) / 10) : (uint32_t)1 << 16;
}

void fm_env_gate_on(fm_env *e)
{
#ifdef FM_CHECK_ENGINE /* debug builds: started with fm_env_init */
    assert(e->set);
#endif
    e->stage = FM_ENV_ATTACK;
    e->seg = 0; /* the next call starts the first segment */
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
    e->seg = 0;
    /* down by a whole step, rounded up so it never takes longer than asked */
    e->step = -(int32_t)(samples ? ((uint32_t)e->level + samples - 1) / samples
                                 : (uint32_t)e->level);
    if (e->step == 0) e->step = -1;
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
    case FM_ENV_ATTACK: /* a new segment on the new time at once: from the
                         * level it is at, the same time continues the same
                         * curve (an RC charge has no memory) */
        e->seg = 0;
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
    int32_t s, d, rem;
    if (e->seg) { /* inside a segment or a hold: the count alone */
        e->seg--;
        return e->level += e->step;
    }
    switch (e->stage) {
    case FM_ENV_ATTACK:
        /* all comparisons subtract: levels and steps are 0..2^30, so a
         * difference cannot overflow where a sum could. A retrigger above
         * a softer note's peak goes straight to the decay, from where it
         * is, so the level never jumps. */
        if (!attack_segment(e)) {
            if (e->level < e->peak) e->level = e->peak;
            enter_decay(e);
            break;
        }
        e->seg--;
        e->level += e->step;
        break;
    case FM_ENV_DECAY: /* a segment starts, or the decay lands */
        s = e->sus_level;
        d = e->level >= s ? e->level - s : s - e->level;
        if (d <= FM_ENV_FLOOR || e->set->decay_samples == 0) {
            e->level = s;
            e->stage = FM_ENV_SUSTAIN;
            hold(e);
            break;
        }
        rem = start_segment(e, d, scaled(e, e->set->decay_samples));
        if (e->level > s) {
            e->step = -e->step;
            rem = -rem;
        }
        e->seg--;
        e->level += e->step + rem;
        break;
    case FM_ENV_RELEASE: /* a segment starts, or the release ends */
        if (e->level <= FM_ENV_FLOOR || e->set->release_samples == 0) {
            e->level = 0;
            e->stage = FM_ENV_IDLE;
            hold(e);
            break;
        }
        rem = start_segment(e, e->level, scaled(e, e->set->release_samples));
        e->step = -e->step;
        e->seg--;
        e->level += e->step - rem;
        break;
    case FM_ENV_FADE:
        if (e->level <= -e->step) {
            e->level = 0;
            e->stage = FM_ENV_IDLE;
            hold(e);
        } else {
            e->level += e->step;
        }
        break;
    case FM_ENV_SUSTAIN: /* holds; fm_env_retime starts a glide on change */
        hold(e);
        break;
    default:
        e->level = 0;
        hold(e);
        break;
    }
    return e->level;
}

int32_t fm_env_advance(fm_env *e, uint32_t n)
{
    while (n > 0) {
        if (e->seg) { /* no segment lands inside itself */
            uint32_t m = n < e->seg ? n : e->seg;
            if (e->step == 0) return e->level; /* a hold: sustain or idle */
            e->level += (int32_t)m * e->step;
            e->seg = (uint8_t)(e->seg - m);
            n -= m;
        } else if (e->stage == FM_ENV_FADE) {
            /* the calls before the one that lands: level > (j + 1) x step */
            uint32_t S = (uint32_t)-e->step;
            uint32_t c = e->level > 0 ? ((uint32_t)e->level - 1) / S : 0;
            if (n <= c) return e->level -= (int32_t)(n * S);
            e->level = 0;
            e->stage = FM_ENV_IDLE;
            hold(e);
            return 0;
        } else { /* a segment starts, or a stage lands */
            fm_env_next(e);
            n--;
        }
    }
    return e->level;
}
