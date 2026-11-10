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

int32_t fm_env_next(fm_env *e)
{
    switch (e->stage) {
    case FM_ENV_ATTACK:
        if (e->level >= FM_ENV_FULL - e->step) {
            e->level = FM_ENV_FULL;
            e->stage = FM_ENV_DECAY;
            e->step = step_for(FM_ENV_FULL - e->sustain, e->decay_samples);
        } else {
            e->level += e->step;
        }
        break;
    case FM_ENV_DECAY:
        if (e->level <= e->sustain + e->step) {
            e->level = e->sustain;
            e->stage = FM_ENV_SUSTAIN;
        } else {
            e->level -= e->step;
        }
        break;
    case FM_ENV_SUSTAIN:
        e->level = e->sustain;
        break;
    case FM_ENV_RELEASE:
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
