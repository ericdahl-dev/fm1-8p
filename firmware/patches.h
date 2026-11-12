/* fm1-8p patches: the 32 JX-8P factory tones, converted to engine values by tools/jx8p.py
 * (firmware/patches.c is generated; docs/patches.md has the source and the mapping; the logic is by
 * hand in firmware/patch.c). Plain C, no host code. */
#ifndef PATCHES_H
#define PATCHES_H
#include <stdint.h>

#define PATCH_COUNT 32u
#define PATCH_NAME_MAX 12u              /* characters in a name, without its end */
#define PATCH_SOUNDTRACK 16u            /* tone 17: the boot default of the playable module */
#define PATCH_NONE (-1)                 /* patch_param: a field the engine has no param for yet */

/* A patch's fields, in engine units. The ones with an fm_param_id set that param (patch_param);
 * the others wait for the feature in the issue named, so a patch sounds closer as each one lands.
 * tests/test_jx8p.py checks this list against tools/jx8p.py's FIELDS. */
typedef enum {
    PATCH_OSC1_WAVE,                    /* fm_wave */
    PATCH_OSC1_PULSE_WIDTH_PCT,
    PATCH_OSC1_OCTAVE,
    PATCH_OSC1_COARSE_SEMIS,            /* waits: what osc1 lacks of DCO-1 tune, folded into osc2 */
    PATCH_OSC2_WAVE,
    PATCH_OSC2_PULSE_WIDTH_PCT,
    PATCH_OSC2_OCTAVE,
    PATCH_OSC2_COARSE_SEMIS,
    PATCH_OSC2_FINE_CENTS,
    PATCH_OSC_MIX_PERMILLE,
    PATCH_OSC_SYNC,
    PATCH_XMOD_AM,                      /* waits: the JX-8P's cross mod, 0/1 (no issue yet) */
    PATCH_HIGHPASS_STEP,
    PATCH_FILTER_CUTOFF_CENTS,
    PATCH_FILTER_RESONANCE_PERMILLE,
    PATCH_FILTER_KEY_TRACK_PERMILLE,
    PATCH_FILTER_ENV_AMOUNT_CENTS,
    PATCH_FILTER_ENV_ATTACK_MS,
    PATCH_FILTER_ENV_DECAY_MS,
    PATCH_FILTER_ENV_SUSTAIN_PERMILLE,
    PATCH_FILTER_ENV_RELEASE_MS,
    PATCH_FILTER_ENV_SOURCE,            /* 0 the filter envelope, 1 the amp envelope (#59) */
    PATCH_AMP_ATTACK_MS,
    PATCH_AMP_DECAY_MS,
    PATCH_AMP_SUSTAIN_PERMILLE,
    PATCH_AMP_RELEASE_MS,
    PATCH_AMP_VELOCITY_PCT,
    PATCH_AMP_GATE,                     /* waits #63: 1 the VCA is a gate (the amp env is one now) */
    PATCH_CHORUS_MODE,
    PATCH_LFO_WAVE,
    PATCH_LFO_RATE_CENTIHZ,
    PATCH_LFO_DELAY_MS,
    PATCH_LFO_PITCH_CENTS,              /* DCO-1's LFO depth: the engine's one moves both oscillators */
    PATCH_LFO_PITCH_OSC2_CENTS,         /* DCO-2's LFO depth (LFO_PITCH_CENTS is DCO-1's) */
    PATCH_PITCH_ENV_OSC1_CENTS,         /* signed, at full envelope (#58) */
    PATCH_PITCH_ENV_OSC2_CENTS,
    PATCH_PITCH_ENV_SOURCE,             /* 0 the filter envelope, 1 the amp envelope */
    PATCH_PITCH_ENV_DYNA,               /* DCO DYNAMICS: 0 off .. 3 */
    PATCH_OSC_MIX_ENV_PERMILLE,         /* MIX ENV: the mix's change at the envelope's peak */
    PATCH_OSC_MIX_ENV_SOURCE,           /* 0 filter envelope, 1 amp envelope */
    PATCH_OSC_MIX_ENV_VELOCITY_PCT,     /* waits: velocity on the mix envelope (no issue yet) */
    PATCH_FILTER_LFO_PERMILLE,          /* waits #8: LFO to cutoff */
    PATCH_FILTER_VELOCITY_PCT,          /* waits #8 */
    PATCH_ENV1_KEY_FOLLOW,              /* waits #60: 0 off .. 3 */
    PATCH_ENV2_KEY_FOLLOW,              /* waits #60 */
    PATCH_AMP_LEVEL_PERMILLE,           /* waits: a VCA level (no issue yet) */
    PATCH_FIELD_COUNT,
} patch_field;

/* Each value is stored + PATCH_BIAS: as plain int16_t, a small negative value (-64..-49, 0xFFCx)
 * next to another could make a 32-bit word fm1-dock rejects as a ROM address (0xFFCxxxxx). With
 * the bias no field's range reaches 0xFFC0. */
#define PATCH_BIAS 32768

typedef struct {
    char name[PATCH_NAME_MAX + 1u];
    uint16_t v[PATCH_FIELD_COUNT];      /* by patch_field, + PATCH_BIAS: read with patch_value */
} patch;

extern const patch PATCHES[PATCH_COUNT];

/* Field f of patch t, in engine units. */
static inline int32_t patch_value(const patch *t, patch_field f)
{
    return (int32_t)t->v[f] - PATCH_BIAS;
}

/* The engine param a field sets (an fm_param_id), or PATCH_NONE while it waits for its feature. */
int32_t patch_param(patch_field f);

/* Patch t as every param's value, into v[FM_PARAM_COUNT]: each field of t the engine has a param for
 * (the others wait for their features), clamped to the param's range, and the default for a param no
 * field sets. What the playable module boots with and what PRESETS loads. */
void patch_values(const patch *t, int32_t *v);
#endif
