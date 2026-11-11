/*
 * env.c - ADSR envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Divisions happen only at stage changes, never per sample.
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
    set_step(e, e->level, e->set->release_samples);
}

void fm_env_fade(fm_env *e, uint32_t samples)
{
    if (e->stage == FM_ENV_IDLE) return;
    e->stage = FM_ENV_FADE;
    set_step(e, e->level, samples);
}

/* Step for the DECAY stage: from the current level to sustain, in either
 * direction, over the decay time. */
static void enter_decay(fm_env *e)
{
    int32_t s = e->sus_level;
    int32_t span = e->level > s ? e->level - s : s - e->level;
    e->stage = FM_ENV_DECAY;
    set_step(e, span, e->set->decay_samples);
}

void fm_env_retime(fm_env *e)
{
    sustain_level(e);
    switch (e->stage) {
    case FM_ENV_ATTACK:
        set_step(e, e->peak, e->set->attack_samples);
        break;
    case FM_ENV_DECAY:
    case FM_ENV_SUSTAIN:
        if (e->level != e->sus_level) enter_decay(e);
        break;
    case FM_ENV_RELEASE:
        set_step(e, e->level, e->set->release_samples);
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
        uint32_t c;
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
            d = e->level >= e->sus_level ? e->level - e->sus_level
                                         : e->sus_level - e->level;
            c = calls_before_landing(e, d, n);
            if (c >= n) {
                int32_t move = take(e, n);
                e->level += e->level >= e->sus_level ? -move : move;
                return e->level;
            }
            e->level = e->sus_level;
            e->stage = FM_ENV_SUSTAIN;
            return e->level; /* sustain holds */
        case FM_ENV_SUSTAIN:
            return e->level;
        case FM_ENV_RELEASE:
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
