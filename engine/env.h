/*
 * env.h - ADSR envelope (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Level is Q1.30: 0 = silent, FM_ENV_FULL = 1.0. In this slice every
 * segment is a straight line:
 *   attack  - rises at a rate that goes 0 -> full in the attack time
 *             (a retrigger starts from the current level, so no click)
 *   decay   - full -> sustain in the decay time
 *   sustain - holds the sustain level
 *   release - current level -> 0 in the release time, then idle
 *   fade    - like release, but over a time given by the caller and not
 *             changed by fm_env_retime (the fast fade of a stolen voice)
 *
 * When a setting changes, fm_env_retime recomputes the running stage from
 * the current level: a new release time applies at once, and a sustain
 * change (in decay or sustain) glides to the new level over the decay
 * time rather than jumping. Full parameter smoothing comes later.
 */
#ifndef FM_ENV_H
#define FM_ENV_H

#include <stdint.h>

#define FM_ENV_FULL (1 << 30)

typedef enum {
    FM_ENV_IDLE = 0,
    FM_ENV_ATTACK,
    FM_ENV_DECAY,
    FM_ENV_SUSTAIN,
    FM_ENV_RELEASE,
    FM_ENV_FADE
} fm_env_stage;

typedef struct {
    /* settings, in samples and Q1.30 */
    uint32_t attack_samples;
    uint32_t decay_samples;
    uint32_t release_samples;
    int32_t sustain;
    int32_t peak;  /* level the attack rises to (velocity), Q1.30; the
                    * sustain level is scaled by it too */
    /* state */
    int32_t sus_level; /* sustain * peak, set at gate on and retime */
    int32_t level;
    int32_t step;  /* per-sample change in the current stage */
    uint8_t stage; /* fm_env_stage */
} fm_env;

void fm_env_reset(fm_env *e);
void fm_env_gate_on(fm_env *e);
void fm_env_gate_off(fm_env *e);
/* Falls from the current level to 0 in `samples`, then goes idle. */
void fm_env_fade(fm_env *e, uint32_t samples);
/* Call after changing any setting while the envelope may be running. */
void fm_env_retime(fm_env *e);
/* Advances one sample and returns the new level (Q1.30). */
int32_t fm_env_next(fm_env *e);

#endif /* FM_ENV_H */
