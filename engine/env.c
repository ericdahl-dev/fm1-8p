/*
 * env.c - ADSR envelope.
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Divisions happen only at stage changes, never per sample.
 */
#include "env.h"

/* Per-sample step that covers `span` in `samples`, rounded up so the
 * segment never takes longer than asked. `samples` == 0 jumps at once. */
static int32_t step_for(int32_t span, uint32_t samples)
{
    if (samples == 0 || span <= 0) return span > 0 ? span : 1;
    return (int32_t)(((uint32_t)span + samples - 1) / samples);
}

void fm_env_reset(fm_env *e)
{
    e->level = 0;
    e->step = 0;
    e->stage = FM_ENV_IDLE;
    e->peak = FM_ENV_FULL;
    e->sus_level = 0;
}

/* The sustain level for this note: the setting scaled by the peak
 * (exact when the peak is full). Kept so the decay needs no multiply. */
static int32_t sustain_level(fm_env *e)
{
    e->sus_level = (int32_t)(((int64_t)e->sustain * e->peak) >> 30);
    return e->sus_level;
}

void fm_env_gate_on(fm_env *e)
{
    e->stage = FM_ENV_ATTACK;
    e->step = step_for(e->peak, e->attack_samples);
    sustain_level(e);
}

void fm_env_gate_off(fm_env *e)
{
    if (e->stage == FM_ENV_IDLE) return;
    e->stage = FM_ENV_RELEASE;
    e->step = step_for(e->level, e->release_samples);
}

void fm_env_fade(fm_env *e, uint32_t samples)
{
    if (e->stage == FM_ENV_IDLE) return;
    e->stage = FM_ENV_FADE;
    e->step = step_for(e->level, samples);
}

/* Step for the DECAY stage: from the current level to sustain, in either
 * direction, over the decay time. */
static void enter_decay(fm_env *e)
{
    int32_t s = e->sus_level;
    int32_t span = e->level > s ? e->level - s : s - e->level;
    e->stage = FM_ENV_DECAY;
    e->step = step_for(span, e->decay_samples);
}

void fm_env_retime(fm_env *e)
{
    sustain_level(e);
    switch (e->stage) {
    case FM_ENV_ATTACK:
        e->step = step_for(e->peak, e->attack_samples);
        break;
    case FM_ENV_DECAY:
    case FM_ENV_SUSTAIN:
        if (e->level != e->sus_level) enter_decay(e);
        break;
    case FM_ENV_RELEASE:
        e->step = step_for(e->level, e->release_samples);
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
        if (e->peak - e->level <= e->step) {
            if (e->level < e->peak) e->level = e->peak;
            enter_decay(e);
        } else {
            e->level += e->step;
        }
        break;
    case FM_ENV_DECAY:
        s = e->sus_level;
        if (e->level >= s) {
            if (e->level - e->step <= s) {
                e->level = s;
                e->stage = FM_ENV_SUSTAIN;
            } else {
                e->level -= e->step;
            }
        } else if (s - e->level <= e->step) {
            e->level = s;
            e->stage = FM_ENV_SUSTAIN;
        } else {
            e->level += e->step;
        }
        break;
    case FM_ENV_SUSTAIN: /* holds; fm_env_retime starts a glide on change */
        break;
    case FM_ENV_RELEASE:
    case FM_ENV_FADE:
        if (e->level <= e->step) {
            e->level = 0;
            e->stage = FM_ENV_IDLE;
        } else {
            e->level -= e->step;
        }
        break;
    default:
        e->level = 0;
        break;
    }
    return e->level;
}

/*
 * Steps the stepping stages take before the one that lands on `target`,
 * from `d` (the distance to it, >= 0) at `step` (>= 1) a sample: a call
 * lands when the distance left is at most `step`, so ceil((d - step) /
 * step) calls go by first, or none when d <= step.
 */
static uint32_t steps_before_landing(int32_t d, int32_t step)
{
    if (d <= step) return 0;
    return ((uint32_t)(d - step) + (uint32_t)step - 1) / (uint32_t)step;
}

int32_t fm_env_advance(fm_env *e, uint32_t n)
{
    while (n > 0) {
        uint32_t c;
        int32_t d;
        switch (e->stage) {
        case FM_ENV_ATTACK:
            c = steps_before_landing(e->peak - e->level, e->step);
            if (n <= c) {
                e->level += (int32_t)(n * (uint32_t)e->step);
                return e->level;
            }
            /* a retrigger above a softer peak keeps its level */
            if (e->level < e->peak) e->level = e->peak;
            enter_decay(e);
            n -= c + 1;
            break;
        case FM_ENV_DECAY:
            d = e->level >= e->sus_level ? e->level - e->sus_level
                                         : e->sus_level - e->level;
            c = steps_before_landing(d, e->step);
            if (n <= c) {
                int32_t move = (int32_t)(n * (uint32_t)e->step);
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
            c = steps_before_landing(e->level, e->step);
            if (n <= c) {
                e->level -= (int32_t)(n * (uint32_t)e->step);
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
