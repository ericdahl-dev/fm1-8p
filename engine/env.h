/*
 * env.h - ADSR envelope (internal to the engine).
 *
 * Copyright (C) 2026 fm1-8p contributors. GPL-3.0-or-later.
 *
 * Level is Q1.30: 0 = silent, FM_ENV_FULL = 1.0.
 *   attack  - an RC charge toward 1.6 x the peak, stopped on the peak at
 *             the attack time, as PG-8X (a retrigger starts from the
 *             current level, so no click)
 *   decay   - exponential toward sustain, the decay time its time constant
 *             tau (the distance left falls by 1/e every tau), as PG-8X
 *   sustain - holds the sustain level
 *   release - exponential toward 0, the release time its tau, then idle
 *             once below FM_ENV_FLOOR (-80 dB)
 * An exponential stage is drawn as straight segments of up to 64 samples
 * (fewer for a short tau, see env.c), each ending on the curve, each a
 * whole-number step a sample with the remainder moved on its first.
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
#ifdef FM_CHECK_ENGINE
#include <assert.h>
#endif

#define FM_ENV_FULL (1 << 30)
/* -80 dB of full: a release below it goes idle, a decay this close to its
 * sustain lands on it */
#define FM_ENV_FLOOR (FM_ENV_FULL / 10000)

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
    int32_t key_follow; /* per mille: the times x 2^(-this x octaves above
                         * C2) for the note (fm_env_key) */
    uint32_t attack_min; /* the shortest attack, in samples, after key
                          * follow (the amp's, #274); 0 for none */
} fm_env_settings;

typedef struct {
    /* state; the render loop's inline step reads these first ones */
    int32_t level;
    int32_t step;  /* per-sample change in this segment, signed */
    uint8_t stage; /* fm_env_stage */
    uint8_t seg;   /* calls left in this straight segment (attack, decay,
                    * release) or hold (sustain, idle); 0: the next
                    * call starts one, or lands */
    uint8_t frac;  /* the fraction of an LSB the last segment's span left
                    * over, Q8, carried into the next */
    uint8_t ramp;  /* attack segments still easing in (#274): the step at
                    * 2^-ramp of the curve's, each at most RAMP_SAMPLES
                    * long */
    int32_t peak;  /* level the attack rises to (velocity), Q1.30; the
                    * sustain level is scaled by it too */
    int32_t sus_level; /* sustain * peak, set at gate on and retime */
    const fm_env_settings *set; /* the shared settings (fm_env_init) */
    uint32_t ks; /* this note's time scale, Q16.16 (key follow; 1.0 at 0) */
    uint32_t lag; /* samples passed but not yet stepped (fm_env_skip):
                   * caught up when the level is read or changed */
} fm_env;

/* Starts an envelope, idle, on `set`: the only way to give it settings,
 * which it keeps (by pointer) from then on. */
void fm_env_init(fm_env *e, const fm_env_settings *set);
/* Back to idle, keeping its settings. */
void fm_env_reset(fm_env *e);
/* The time scale for `note` from the key follow setting: call before
 * fm_env_gate_on. Exactly 1.0 when the setting is 0. */
void fm_env_key(fm_env *e, uint8_t note);
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
 * fm_env_next for the render loop, inline: inside a segment or a hold (all
 * but one sample in up to 64, or 255 when holding) the count and an add,
 * as fm_env_next; the sample that starts a segment or lands calls it.
 * Exactly the same level, stage and step. No segment lands inside itself:
 * an attack segment stops short of the peak, and a decay or release one
 * starts more than FM_ENV_FLOOR from its target and moves at most 2/3 of
 * that.
 */
static inline int32_t fm_env_step(fm_env *e)
{
#ifdef FM_CHECK_ENGINE /* debug builds: a stepped envelope is never skipped */
    assert(e->lag == 0);
#endif
    if (e->seg) {
        e->seg--;
        return e->level += e->step;
    }
    return fm_env_next(e);
}

/*
 * Advances `n` samples at once: exactly `n` calls of fm_env_next, in a
 * few operations per segment crossed instead of
 * `n` steps. For an envelope that only needs to be current at times, not
 * every sample. Catches up a skip first. Returns the level.
 */
int32_t fm_env_advance(fm_env *e, uint32_t n);

/* The most samples a skip leaves uncaught: one catch-up steps fewer than
 * this plus the skip that reached it, so its cost is bounded (a segment
 * crosses up to 64 samples in a few operations). One FM-1 render block
 * (256 frames): voices started together catch up in the same block, so a
 * larger bound piles every voice's catch-up onto one block (4096 took the
 * 8-voice CPU row from 12.9 to 23.3 % of a block, #70). At a block or less
 * every lazy envelope catches up in every block, so in steady state each
 * block steps one block's samples, and a stagger would not lower it. */
#define FM_ENV_LAG_MAX (1u << 8)

/* Catches up the lag, then steps `n`: a skip that reaches FM_ENV_LAG_MAX. */
void fm_env_skip_far(fm_env *e, uint32_t n);

/*
 * The lazy envelope: `n` samples pass without stepping it. It catches up
 * by itself, exactly (as fm_env_advance), when its level is read
 * (fm_env_level) or it is changed (key, gate on, gate off, fade,
 * retime, advance), so a caller never syncs it. An envelope is either stepped
 * (fm_env_step, fm_env_next: they do not catch up) or skipped, not both.
 */
static inline void fm_env_skip(fm_env *e, uint32_t n)
{
    if (n >= FM_ENV_LAG_MAX - e->lag) /* lag < FM_ENV_LAG_MAX */
        fm_env_skip_far(e, n);
    else
        e->lag += n;
}

/* The level now: caught up first. */
int32_t fm_env_level(fm_env *e);
/* Catches up now. Needed only before writing settings the envelope
 * shares with others (fm_env_settings): caught up after, it would step
 * the samples before the write on the new value. */
void fm_env_catch_up(fm_env *e);

#endif /* FM_ENV_H */
