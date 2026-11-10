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
}

void fm_env_gate_on(fm_env *e)
{
    e->stage = FM_ENV_ATTACK;
    e->step = step_for(FM_ENV_FULL, e->attack_samples);
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
    int32_t span = e->level > e->sustain ? e->level - e->sustain
                                         : e->sustain - e->level;
    e->stage = FM_ENV_DECAY;
    e->step = step_for(span, e->decay_samples);
}

void fm_env_retime(fm_env *e)
{
    switch (e->stage) {
    case FM_ENV_ATTACK:
        e->step = step_for(FM_ENV_FULL, e->attack_samples);
        break;
    case FM_ENV_DECAY:
    case FM_ENV_SUSTAIN:
        if (e->level != e->sustain) enter_decay(e);
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
    switch (e->stage) {
    case FM_ENV_ATTACK:
        /* all comparisons subtract: levels and steps are 0..2^30, so a
         * difference cannot overflow where a sum could */
        if (FM_ENV_FULL - e->level <= e->step) {
            e->level = FM_ENV_FULL;
            enter_decay(e);
        } else {
            e->level += e->step;
        }
        break;
    case FM_ENV_DECAY:
        if (e->level >= e->sustain) {
            if (e->level - e->step <= e->sustain) {
                e->level = e->sustain;
                e->stage = FM_ENV_SUSTAIN;
            } else {
                e->level -= e->step;
            }
        } else if (e->sustain - e->level <= e->step) {
            e->level = e->sustain;
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
