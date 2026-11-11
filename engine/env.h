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

/* An envelope's times and sustain, in samples and Q1.30. One struct is
 * shared by every voice's envelope of a kind (the engine owns it), so a
 * parameter change is written once (#19). */
typedef struct {
    uint32_t attack_samples;
    uint32_t decay_samples;
    uint32_t release_samples;
    int32_t sustain;
} fm_env_settings;

typedef struct {
    /* state; the render loop's inline step reads these first ones */
    int32_t level;
    int32_t step;  /* per-sample change in the current stage, whole part */
    uint8_t stage; /* fm_env_stage */
    uint8_t step_frac; /* ... and its fraction, Q8 (step + step_frac / 256),
                        * in the padding after stage: no bigger (#19) */
    uint8_t frac; /* the fraction carried so far, Q8 */
    int32_t peak;  /* level the attack rises to (velocity), Q1.30; the
                    * sustain level is scaled by it too */
    int32_t sus_level; /* sustain * peak, set at gate on and retime */
    const fm_env_settings *set; /* the shared settings (fm_env_init) */
} fm_env;

/* Starts an envelope, idle, on `set`: the only way to give it settings,
 * which it keeps (by pointer) from then on. */
void fm_env_init(fm_env *e, const fm_env_settings *set);
/* Back to idle, keeping its settings. */
void fm_env_reset(fm_env *e);
void fm_env_gate_on(fm_env *e);
void fm_env_gate_off(fm_env *e);
/* Falls from the current level to 0 in `samples`, then goes idle. */
void fm_env_fade(fm_env *e, uint32_t samples);
/* Call after changing a setting in *set while the envelope runs (an idle
 * one needs none: the next gate on reads the settings). */
void fm_env_retime(fm_env *e);
/* Advances one sample and returns the new level (Q1.30). */
int32_t fm_env_next(fm_env *e);

/*
 * fm_env_next for the render loop, inline: a sample that only adds or
 * subtracts the step (most of them) is done here, the same comparisons as
 * fm_env_next; one that lands on the peak, the sustain or 0, or an idle
 * envelope, calls it. Exactly the same level, stage and step.
 */
/* This sample's change: the whole step plus the carry of its fraction,
 * so a segment moves step + step_frac / 256 a sample on average. */
static inline int32_t fm_env_inc(fm_env *e)
{
    uint32_t f = (uint32_t)e->frac + e->step_frac;
    e->frac = (uint8_t)f;
    return e->step + (int32_t)(f >> 8);
}

/* The most one sample moves: the step, + 1 with a fraction. A stage lands
 * on its target from at most this far. */
static inline int32_t fm_env_reach(const fm_env *e)
{
    return e->step + (e->step_frac != 0);
}

static inline int32_t fm_env_step(fm_env *e)
{
    switch (e->stage) {
    case FM_ENV_SUSTAIN:
        return e->level;
    case FM_ENV_ATTACK:
        if (e->peak - e->level > fm_env_reach(e)) return e->level += fm_env_inc(e);
        break;
    case FM_ENV_DECAY:
        if (e->level >= e->sus_level) {
            if (e->level - fm_env_reach(e) > e->sus_level) return e->level -= fm_env_inc(e);
        } else if (e->sus_level - e->level > fm_env_reach(e)) {
            return e->level += fm_env_inc(e);
        }
        break;
    case FM_ENV_RELEASE:
    case FM_ENV_FADE:
        if (e->level > fm_env_reach(e)) return e->level -= fm_env_inc(e);
        break;
    default:
        break;
    }
    return fm_env_next(e);
}

/*
 * Advances `n` samples at once: exactly `n` calls of fm_env_next, in a
 * few operations per stage crossed (one 32-bit division each) instead of
 * `n` steps. For an envelope that only needs to be current at times, not
 * every sample. Returns the level.
 */
int32_t fm_env_advance(fm_env *e, uint32_t n);

#endif /* FM_ENV_H */
