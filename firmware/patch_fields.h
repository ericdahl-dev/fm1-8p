/* fm1-8p patch fields: the patch_field enum. GENERATED from tools/params.py (PATCH_FIELDS) by
 * tools/jx8p.py (make generate): edit the list, not this file. */
#ifndef PATCH_FIELDS_H
#define PATCH_FIELDS_H

/* A patch's fields, in engine units. The ones with an fm_param_id set that param (patch_param);
 * the others wait for the feature in the issue named, so a patch sounds closer as each one lands. */
typedef enum {
    /* fm_wave */
    PATCH_OSC1_WAVE,
    PATCH_OSC1_PULSE_WIDTH_PCT,
    PATCH_OSC1_OCTAVE,
    /* waits: what osc1 lacks of DCO-1 tune, folded into osc2 */
    PATCH_OSC1_COARSE_SEMIS,
    PATCH_OSC2_WAVE,
    PATCH_OSC2_PULSE_WIDTH_PCT,
    PATCH_OSC2_OCTAVE,
    PATCH_OSC2_COARSE_SEMIS,
    PATCH_OSC2_FINE_CENTS,
    /* DCO-1 level over 127, summed with DCO-2 (#102) */
    PATCH_OSC1_LEVEL_PERMILLE,
    /* VCA LEVEL, 0.30 dB a step below 127 */
    PATCH_AMP_LEVEL_PERMILLE,
    /* DCO-2 level over 127, before MIX ENV */
    PATCH_OSC2_LEVEL_PERMILLE,
    PATCH_OSC_SYNC,
    /* osc_ring: the JX-8P's ring, SYNC 2 and X-MOD (#105) */
    PATCH_XMOD_AM,
    PATCH_HIGHPASS_STEP,
    PATCH_FILTER_CUTOFF_CENTS,
    PATCH_FILTER_RESONANCE_PERMILLE,
    PATCH_FILTER_KEY_TRACK_PERMILLE,
    PATCH_FILTER_ENV_AMOUNT_CENTS,
    PATCH_FILTER_ENV_ATTACK_MS,
    PATCH_FILTER_ENV_DECAY_MS,
    PATCH_FILTER_ENV_SUSTAIN_PERMILLE,
    PATCH_FILTER_ENV_RELEASE_MS,
    /* 0 the filter envelope, 1 the amp envelope (#59) */
    PATCH_FILTER_ENV_SOURCE,
    PATCH_AMP_ATTACK_MS,
    PATCH_AMP_DECAY_MS,
    PATCH_AMP_SUSTAIN_PERMILLE,
    PATCH_AMP_RELEASE_MS,
    /* VCA DYNAMICS: 0 off .. 3 (engine/dyna.c) */
    PATCH_AMP_DYNA,
    /* VCA MODE GATE: 1 the VCA is the engine's gate (#63) */
    PATCH_AMP_GATE,
    PATCH_CHORUS_MODE,
    PATCH_LFO_WAVE,
    PATCH_LFO_RATE_CENTIHZ,
    PATCH_LFO_DELAY_MS,
    /* DCO-1's LFO depth: the engine's one moves both oscillators */
    PATCH_LFO_PITCH_CENTS,
    /* DCO-2's LFO depth (LFO_PITCH_CENTS is DCO-1's) */
    PATCH_LFO_PITCH_OSC2_CENTS,
    /* signed, at full envelope (#58) */
    PATCH_PITCH_ENV_OSC1_CENTS,
    PATCH_PITCH_ENV_OSC2_CENTS,
    /* 0 the filter envelope, 1 the amp envelope */
    PATCH_PITCH_ENV_SOURCE,
    /* DCO DYNAMICS: 0 off .. 3 */
    PATCH_PITCH_ENV_DYNA,
    /* MIX ENV: the mix's change at the envelope's peak */
    PATCH_OSC_MIX_ENV_PERMILLE,
    /* 0 filter envelope, 1 amp envelope */
    PATCH_OSC_MIX_ENV_SOURCE,
    /* MIX DYNAMICS: 0 off .. 3 */
    PATCH_OSC_MIX_ENV_DYNA,
    /* waits #8: LFO to cutoff */
    PATCH_FILTER_LFO_PERMILLE,
    /* VCF DYNAMICS: 0 off .. 3 */
    PATCH_FILTER_ENV_DYNA,
    /* ENV-2 KEY: 260 per mille a position (#60) */
    PATCH_AMP_KEY_FOLLOW_PERMILLE,
    /* the filter envelope's (ENV-1 KEY, or ENV-2's) */
    PATCH_FILTER_ENV_KEY_FOLLOW_PERMILLE,
    /* 1: levels, filter and LFO depths glide as PG-8X's (#146) */
    PATCH_SETTING_GLIDE,
    PATCH_FIELD_COUNT,
} patch_field;

#endif
